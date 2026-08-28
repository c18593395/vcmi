/*
 * router.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "callback/CBattleCallback.h"
#include "callback/CDynLibHandler.h"
#include "callback/IGameInfoCallback.h"
#include "filesystem/Filesystem.h"
#include "lib/CRandomGenerator.h"
#include "json/JsonUtils.h"

#include "BAI/factory.h"
#include "BAI/fallback/scripted_model.h"
#include "BAI/router.h"
#include "BAI/fallback/MLBot.h"
#include "BAI/v13/BAI.h"

#include "common.h"

namespace MMAI::BAI
{
// key => {model, path}
using ModelStorage = std::map<std::string, std::pair<std::shared_ptr<MMAI::Schema::IModel>, std::string>>;

namespace
{
	struct ModelRepository
	{
		mutable std::mutex mutex;
		mutable ModelStorage models;
		float temperature = 1.0;
		uint64_t seed = 0;
		std::map<std::string, std::string> paths;
		std::shared_ptr<ScriptedModel> fallbackModel;
		std::string fallbackName;
	};

	std::shared_ptr<ModelRepository> InitModelRepository(Schema::Baggage * baggage = nullptr)
	{
		auto repo = std::make_unique<ModelRepository>();

		if (baggage) {
			// Since ML client cannot load onnx models, it sends PATH
			// "models" whose getName() returns the path to the model to load
			// (in ML mode, the "MMAI" mod hence its config are not present)
			if(baggage->modelLeft->getType() == Schema::ModelType::PATH)
			{
				const auto path = baggage->modelLeft->getName();
				repo->models.try_emplace("attacker", CreateNNModel(path, repo->temperature, repo->seed), path);
				repo->paths.try_emplace("attacker", path);
			}
			if(baggage->modelRight->getType() == Schema::ModelType::PATH)
			{
				const auto path = baggage->modelRight->getName();
				repo->models.try_emplace("defender", CreateNNModel(path, repo->temperature, repo->seed), path);
				repo->paths.try_emplace("defender", path);
			}
			return repo;
		}

		auto json = JsonUtils::assembleFromFiles("MMAI/CONFIG/mmai-settings.json");
		std::string fallback;

		if(json.isStruct())
		{
			JsonUtils::validate(json, "vcmi:mmaiSettings", "mmai");
			repo->temperature = static_cast<float>(json["temperature"].Float());

			repo->seed = json["seed"].Integer();
			if(repo->seed == 0)
				repo->seed = CRandomGenerator::getDefault().nextInt();

			for(const auto & [key, node] : json["models"].Struct())
				repo->paths.try_emplace(key, "MMAI/models/" + node.String());

			fallback = json["fallback"].isNull() ? "BattleAI" : json["fallback"].String();
		}
		else
		{
			logAi->error("Could not load MMAI config. Is MMAI mod enabled?");
			// ML fix (2026-08-16): config 缺失时 fallback 用 StupidAI — BattleAI 在 headless
			// 无头服务器下等待回调卡死 (MLClient.cpp 注释早有记录), StupidAI 是验证过的自动裁决
			fallback = "StupidAI";
		}

		logAi->debug("MMAI: repo initialized with fallback: %s", fallback);
		repo->fallbackModel = std::make_unique<ScriptedModel>(fallback);
		repo->fallbackName = fallback;

		return repo;
	}

	const ModelRepository * GetModelRepository(Schema::Baggage * baggage = nullptr)
	{
		static const auto repo = InitModelRepository(baggage);
		return repo.get();
	}

	Schema::IModel * GetModel(const std::string & key, Schema::Baggage * baggage = nullptr)
	{
		const auto * repo = GetModelRepository(baggage);
		auto lock = std::lock_guard<std::mutex>(repo->mutex);

		std::shared_ptr<Schema::IModel> model = nullptr;

		// Search for "foo.bar.baz" -> "foo.bar" -> "foo"
		auto currentKey = key;
		auto keysToAdd = std::vector<std::string>{};

		while(true)
		{
			logAi->debug("MMAI: trying %s", currentKey);
			auto it = repo->models.find(currentKey);

			if(it != repo->models.end())
			{
				model = it->second.first;
				logAi->debug("MMAI: cache hit: %s (%s)", currentKey, it->second.second);
				break;
			}

			logAi->debug("MMAI: cache miss: %s", currentKey);
			keysToAdd.push_back(currentKey);

			auto itPath = repo->paths.find(currentKey);
			if(itPath != repo->paths.end())
			{
				const auto & path = itPath->second;
				logAi->debug("MMAI: Loading %s model from: %s", currentKey, path);
				try
				{
					model = CreateNNModel(path, repo->temperature, repo->seed);
					for(const auto & k : keysToAdd)
					{
						logAi->debug("MMAI: cache write: %s (%s)", k, path);
						repo->models.try_emplace(k, model, path);
					}
					break;
				}
				catch(std::exception & e)
				{
					logAi->error("MMAI: load error: %s: %s", currentKey, std::string(e.what()));
				}
			}
			else
			{
				logAi->warn("MMAI: load error: %s: no path configured", currentKey);
			}

			// Try next key (if any)
			auto pos = currentKey.rfind('.');
			if(pos == std::string::npos)
				break;

			currentKey = currentKey.substr(0, pos);
		}

		if(!model)
		{
			logAi->error("MMAI: %s: falling back to %s", key, repo->fallbackName);
			ASSERT(repo->fallbackModel, "fallback error: model is null");
			model = repo->fallbackModel;
		}

		return model.get();
	}

	// Convert a memory address for logging purposes
	std::string MakeAddrStr(const void * p)
	{
		std::ostringstream oss;
		oss << p;
		return oss.str();
	}

}

#define MMAI_LOG_TAG LogTag _(logtag + "." + __func__)

Router::Router() : addrstr(MakeAddrStr(this)), logtag(addrstr + ":MMAI") {}

Router::~Router()
{
	cb->waitTillRealize = wasWaitingForRealize;
}

void Router::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AICombatOptions aiCombatOptions_)
{
	env = ENV;
	cb = CB;
	colorname = cb->getPlayerID()->toString();
	wasWaitingForRealize = cb->waitTillRealize;
	aiCombatOptions = aiCombatOptions_;

	// During training, baggage is used for injecting the model-in-training
	// (which acts as a bridge to vcmi-gym)
	auto & any = aiCombatOptions.other;
	if(any.has_value())
	{
		auto & t = typeid(Schema::Baggage *);
		ASSERT(
			any.type() == t,
			boost::str(
				boost::format("Bad std::any payload type for aiCombatOptions.other: want: %s/%u, have: %s/%u") % boost::core::demangle(t.name())
				% t.hash_code() % boost::core::demangle(any.type().name()) % any.type().hash_code()
			)
		);
		baggage = std::any_cast<Schema::Baggage *>(aiCombatOptions.other);

		logAi->info("Baggage decoded");
#ifndef ENABLE_ML
		throw std::runtime_error("ENABLE_ML IS UNDEFINED, but baggage was given!");
#endif
	}
	else
	{
		logAi->debug("No baggage given");
	}
	cb->waitTillRealize = false;
	bai.reset();
}

/*
 * Delegated methods
 */

void Router::actionFinished(const BattleID & bid, const BattleAction & action)
{
	MMAI_LOG_TAG;
	bai->actionFinished(bid, action);
}

void Router::actionStarted(const BattleID & bid, const BattleAction & action)
{
	MMAI_LOG_TAG;
	bai->actionStarted(bid, action);
}

void Router::activeStack(const BattleID & bid, const CStack * astack)
{
	MMAI_LOG_TAG;
	bai->activeStack(bid, astack);
}

void Router::battleAttack(const BattleID & bid, const BattleAttack * ba)
{
	MMAI_LOG_TAG;
	bai->battleAttack(bid, ba);
}

void Router::battleCatapultAttacked(const BattleID & bid, const CatapultAttack & ca)
{
	MMAI_LOG_TAG;
	bai->battleCatapultAttacked(bid, ca);
}

void Router::battleEnd(const BattleID & bid, const BattleResult * br, QueryID queryID)
{
	MMAI_LOG_TAG;
	bai->battleEnd(bid, br, queryID);
}

void Router::battleGateStateChanged(const BattleID & bid, const EGateState state)
{
	MMAI_LOG_TAG;
	bai->battleGateStateChanged(bid, state);
};

void Router::battleLogMessage(const BattleID & bid, const std::vector<MetaString> & lines)
{
	MMAI_LOG_TAG;
	bai->battleLogMessage(bid, lines);
};

void Router::battleNewRound(const BattleID & bid)
{
	MMAI_LOG_TAG;
	bai->battleNewRound(bid);
}

void Router::battleNewRoundFirst(const BattleID & bid)
{
	MMAI_LOG_TAG;
	bai->battleNewRoundFirst(bid);
}

void Router::battleObstaclesChanged(const BattleID & bid, const std::vector<ObstacleChanges> & obstacles)
{
	MMAI_LOG_TAG;
	bai->battleObstaclesChanged(bid, obstacles);
};

void Router::battleSpellCast(const BattleID & bid, const BattleSpellCast * sc)
{
	MMAI_LOG_TAG;
	bai->battleSpellCast(bid, sc);
}

void Router::battleStackMoved(const BattleID & bid, const CStack * stack, const BattleHexArray & dest, int distance, bool teleport)
{
	MMAI_LOG_TAG;
	bai->battleStackMoved(bid, stack, dest, distance, teleport);
}

void Router::battleStacksAttacked(const BattleID & bid, const std::vector<BattleStackAttacked> & bsa, bool ranged)
{
	MMAI_LOG_TAG;
	bai->battleStacksAttacked(bid, bsa, ranged);
}

void Router::battleStacksEffectsSet(const BattleID & bid, const SetStackEffect & sse)
{
	MMAI_LOG_TAG;
	bai->battleStacksEffectsSet(bid, sse);
}

void Router::battleStart(
	const BattleID & bid,
	const CCreatureSet * army1,
	const CCreatureSet * army2,
	int3 tile,
	const CGHeroInstance * hero1,
	const CGHeroInstance * hero2,
	BattleSide side,
	bool replayAllowed
)
{
	// ML fix (C8.5): 整体防御 — neutral 玩家 playerID 无效导致 ASSERT 抛异常 → std::unexpected 崩溃
	try {
	MMAI_LOG_TAG;
	Schema::IModel * model;
	bool allowMlBot = false;

	std::string modelkey = side == BattleSide::ATTACKER ? "attacker" : "defender";

	// ML fix (2026-08-16): neutral/无 playerID 战斗 (red 打野) — 直接 StupidAI 自动裁决。
	// 原 C8.5 方案用 baggage->modelRight (blue 模型), blue=Nullkiller2 时加载 Nullkiller2
	// 战斗模型失败 → fallback BattleAI → headless 卡死。StupidAI 是无头环境验证过的自动裁决。
	if (baggage && (!cb->getPlayerID() || !cb->getPlayerID()->hasValue()))
	{
		bai = CDynLibHandler::getNewBattleAI("StupidAI");
		bai->initBattleInterface(env, cb, aiCombatOptions);
		bai->battleStart(bid, army1, army2, tile, hero1, hero2, side, replayAllowed);
		return;
	}

	if(baggage)
	{
		// During training, the model object is provided via the baggage
		// This is a special model (a bridge between VCMI and vcmi-gym).
		// Additionally, the `cb->getPlayerID` is used instead of `side`
		// to accomodate for the "side swapping" training feature.
		// XXX: dev mode assumes there are no neutral players in battle
		ASSERT(baggage, "baggage is nullptr");
		auto pid = cb->getPlayerID();
		if (pid && pid->hasValue()) {
			if (pid->num) {
				model = baggage->modelRight;
				allowMlBot = baggage->allowMlBotRight;
			} else {
				model = baggage->modelLeft;
				allowMlBot = baggage->allowMlBotLeft;
			}
		} else {
			// ML fix (C8.5): neutral/无 playerID 玩家 — 用 defender 模型, 避免 ASSERT 崩溃
			model = baggage->modelRight;
			allowMlBot = baggage->allowMlBotRight;
		}
		ASSERT(model, "model is nullptr");
		if(model->getType() == Schema::ModelType::PATH)
		{
			// If the baggage does not carry a model, it means we must load one from file
			// This occurs when training is done vs. another pre-trained model
			// For example, attacker is an injected model (being trained),
			// while defender is nullptr which stands for "load a pre-trained
			// model as usual"
			model = GetModel(modelkey);
		}

		ASSERT(model, "model is nullptr");
	}
	else
	{
		if(cb->getBattle(bid)->battleGetWallState(EWallPart::GATE) != EWallState::NONE)
			modelkey += ".siege";

		model = GetModel(modelkey);
	}

	logtag += ".v" + std::to_string(model->getVersion());
	LogTag _2(logtag + "." + __func__);

	auto modelside = model->getSide();
	auto realside = static_cast<Schema::Side>(EI(side));

	if(modelside != realside && modelside != Schema::Side::BOTH)
		logAi->warn("The loaded '%s' model was not trained to play as %s", modelkey, modelkey);

	switch(model->getType())
	{
		case Schema::ModelType::SCRIPTED:
			if(model->getName() == "StupidAI")
			{
				bai = CDynLibHandler::getNewBattleAI("StupidAI");
				bai->initBattleInterface(env, cb, aiCombatOptions);
			}
			else if(model->getName() == "BattleAI")
			{
				bai = CDynLibHandler::getNewBattleAI("BattleAI");
				bai->initBattleInterface(env, cb, aiCombatOptions);
			}
#ifdef ENABLE_ML
			else if(model->getName() == "MMAI_BATTLEAI")
			{
                bai = std::make_shared<MLBot>("BattleAI");
                bai->initBattleInterface(env, cb, aiCombatOptions);
			}
#endif
			else
			{
				// ML fix: 未知 scripted 模型名 (如 blue=Nullkiller2 时 Scripted("Nullkiller2"))
				// fallback StupidAI 自动裁决 (BattleAI headless 卡死, 勿用)
				logAi->warn("Unknown scripted model name: %s — falling back to StupidAI", model->getName());
				bai = CDynLibHandler::getNewBattleAI("StupidAI");
				bai->initBattleInterface(env, cb, aiCombatOptions);
			}
			break;
		case Schema::ModelType::NN:
        case Schema::ModelType::USER:
			// XXX: must not call initBattleInterface here
			bai = CreateBAI(model, env, cb, aiCombatOptions.enableSpellsUsage);
#ifdef ENABLE_ML
        	{
				auto bai_ = dynamic_cast<V13::BAI*>(bai.get());
				ASSERT(bai_, "dynamic cast to V13::BAI failed");
				bai_->allowMlBot = allowMlBot;
			}
#endif
			break;
		default:
			THROW_FORMAT("Unexpected model type: %d", EI(model->getType()));
	}

	bai->battleStart(bid, army1, army2, tile, hero1, hero2, side, replayAllowed);
	} catch (const std::exception & e) {
		fprintf(stderr, "[Router] battleStart exception: %s — fallback to StupidAI\n", e.what());
		try {
			bai = CDynLibHandler::getNewBattleAI("StupidAI");
			bai->initBattleInterface(env, cb, aiCombatOptions);
			bai->battleStart(bid, army1, army2, tile, hero1, hero2, side, replayAllowed);
		} catch (...) {
			fprintf(stderr, "[Router] battleStart fallback also failed\n");
			bai = nullptr;
		}
	} catch (...) {
		fprintf(stderr, "[Router] battleStart unknown exception — fallback to StupidAI\n");
		try {
			bai = CDynLibHandler::getNewBattleAI("StupidAI");
			bai->initBattleInterface(env, cb, aiCombatOptions);
			bai->battleStart(bid, army1, army2, tile, hero1, hero2, side, replayAllowed);
		} catch (...) {
			fprintf(stderr, "[Router] battleStart fallback also failed\n");
			bai = nullptr;
		}
	}
}

void Router::battleTriggerEffect(const BattleID & bid, const BattleTriggerEffect & bte)
{
	MMAI_LOG_TAG;
	bai->battleTriggerEffect(bid, bte);
}

void Router::battleUnitsChanged(const BattleID & bid, const std::vector<UnitChanges> & changes)
{
	MMAI_LOG_TAG;
	bai->battleUnitsChanged(bid, changes);
}

void Router::yourTacticPhase(const BattleID & bid, int distance)
{
	MMAI_LOG_TAG;
	bai->yourTacticPhase(bid, distance);
}
}
