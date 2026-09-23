/*
 * Client.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOKERNEL
#define NOKERNEL
#endif
#ifndef NOSOUND
#define NOSOUND
#endif
#include <windows.h> // [THREAD] 插桩 GetCurrentThreadId (09-09)
#else
static inline unsigned long GetCurrentThreadId() { return 0; }
#endif
#include "Global.h"
#include "Client.h"

#include "CPlayerInterface.h"
#include "PlayerLocalState.h"
#include "CServerHandler.h"
#include "ClientNetPackVisitors.h"
#include "adventureMap/AdventureMapInterface.h"
#include "battle/BattleInterface.h"
#include "GameEngine.h"
#include "GameInstance.h"
#include "gui/WindowHandler.h"
#include "mapView/mapHandler.h"

// 2026-08-01: 跨 .so 直传 red 冒险 AI 名 (MLClient.cpp 定义, settings 写读层不一致绕过)
extern "C" {
#ifdef ENABLE_ML
    extern char g_adventure_allied_ai[64];
#else
    char g_adventure_allied_ai[64] = {0};  // ML 关闭时本地定义, 无写入方保持空
#endif
}

#include "../lib/CConfigHandler.h"
#include "../lib/GameLibrary.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/battle/CPlayerBattleCallback.h"
#include "../lib/callback/CCallback.h"
#include "../lib/callback/CDynLibHandler.h"
#include "../lib/callback/CGlobalAI.h"
#include "../lib/callback/IGameInfoCallback.h"
#include "../lib/filesystem/Filesystem.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/CPlayerState.h"
#include "../lib/CThreadHelper.h"
#include "../lib/VCMIDirs.h"
#include "../lib/UnlockGuard.h"
#include "../lib/mapObjects/army/CArmedInstance.h"
#include "../lib/mapping/CMapService.h"
#include "../lib/pathfinder/CGPathNode.h"
#include "../lib/serializer/GameConnection.h"

#include <memory>
#include <vcmi/events/EventBus.h>

#if SCRIPTING_ENABLED
#include "../lib/ScriptHandler.h"
#endif

#ifdef VCMI_ANDROID
#include "lib/CAndroidVMHelper.h"
#endif

CPlayerEnvironment::CPlayerEnvironment(PlayerColor player_, CClient * cl_, std::shared_ptr<CCallback> mainCallback_)
	: player(player_),
	cl(cl_),
	mainCallback(mainCallback_)
{

}

const Services * CPlayerEnvironment::services() const
{
	return LIBRARY;
}

const CPlayerEnvironment::BattleCb * CPlayerEnvironment::battle(const BattleID & battleID) const
{
	return mainCallback->getBattle(battleID).get();
}

const CPlayerEnvironment::GameCb * CPlayerEnvironment::game() const
{
	return mainCallback.get();
}

CClient::CClient()
{
	waitingRequest.clear();
}

CClient::~CClient() = default;

IGameInfoCallback & CClient::gameInfo()
{
	return *gamestate;
}

const Services * CClient::services() const
{
	return LIBRARY; //todo: this should be LIBRARY
}

const CClient::BattleCb * CClient::battle(const BattleID & battleID) const
{
	return nullptr; //todo?
}

const CClient::GameCb * CClient::game() const
{
	return gamestate.get();
}

void CClient::newGame(std::shared_ptr<CGameState> initializedGameState)
{
	GAME->server().th->update();
	assert(initializedGameState);
	gamestate = initializedGameState;
	gamestate->preInit(LIBRARY);
	logNetwork->trace("\tCreating gamestate: %i", GAME->server().th->getDiff());

	initMapHandler();
	initPlayerEnvironments();
	initPlayerInterfaces();
}

void CClient::loadGame(std::shared_ptr<CGameState> initializedGameState)
{
	logNetwork->info("Loading procedure started!");

	logNetwork->info("Game state was transferred over network, loading.");
	gamestate = initializedGameState;
	gamestate->preInit(LIBRARY);
	gamestate->updateOnLoad(*GAME->server().si);
	logNetwork->info("Game loaded, initialize interfaces.");

	initMapHandler();
	initPlayerEnvironments();
	initPlayerInterfaces();
}

void CClient::endNetwork()
{
	GAME->map().endNetwork();

	if (CPlayerInterface::battleInt)
		CPlayerInterface::battleInt->endNetwork();

	for(auto & i : playerint)
	{
		auto interface = std::dynamic_pointer_cast<CPlayerInterface>(i.second);
		if (interface)
			interface->endNetwork();
	}
}

void CClient::finishGameplay()
{
	waitingRequest.requestTermination();

	//suggest interfaces to finish their stuff (AI should interrupt any bg working threads)
	for(auto & i : playerint)
		i.second->finish();
}

void CClient::endGame()
{
#if SCRIPTING_ENABLED
	clientScripts.reset();
#endif

	logNetwork->info("Ending current game!");
	removeGUI();

	GAME->setMapInstance(nullptr);

	logNetwork->info("Deleted mapHandler and gameState.");

	CPlayerInterface::battleInt.reset();
	playerint.clear();
	battleints.clear();
	battleCallbacks.clear();
	playerEnvironments.clear();

	//FIXME: gamestate->currentBattles.clear() will use gamestate. So it shoule be callded before gamestate.reset()
	gamestate->currentBattles.clear();
	gamestate.reset();

	logNetwork->info("Deleted playerInts.");
	logNetwork->info("Client stopped.");
}

void CClient::initMapHandler()
{
	// TODO: CMapHandler initialization can probably go somewhere else
	// It's can't be before initialization of interfaces
	// During loading CPlayerInterface from serialized state it's depend on MH
	// FIXED: headless also needs mapInstance — netpack visitors (TryMoveHero etc.) call GAME->map()
	GAME->setMapInstance(std::make_unique<CMapHandler>(&gameState().getMap()));
	logNetwork->trace("Creating mapHandler: %d ms", GAME->server().th->getDiff());
}

void CClient::initPlayerEnvironments()
{
	playerEnvironments.clear();

	auto allPlayers = GAME->server().getAllClientPlayers(GAME->server().logicConnection->connectionID);
	bool hasHumanPlayer = false;
	for(auto & color : allPlayers)
	{
		logNetwork->info("Preparing environment for player %s", color.toString());
		playerEnvironments[color] = std::make_shared<CPlayerEnvironment>(color, this, std::make_shared<CCallback>(gamestate, color, this));
		
		if(color.isValidPlayer() && !hasHumanPlayer && gameState().players.at(color).isHuman())
			hasHumanPlayer = true;
	}

	if(!hasHumanPlayer && !settings["session"]["headless"].Bool())
	{
		Settings session = settings.write["session"];
		session["spectate"].Bool() = true;
		session["spectate-skip-battle-result"].Bool() = true;
		session["spectate-ignore-hero"].Bool() = true;
	}
	
	if(settings["session"]["spectate"].Bool())
	{
		playerEnvironments[PlayerColor::SPECTATOR] = std::make_shared<CPlayerEnvironment>(PlayerColor::SPECTATOR, this, std::make_shared<CCallback>(gamestate, std::nullopt, this));
	}
}

void CClient::initPlayerInterfaces()
{
	for(const auto & playerInfo : gameState().getStartInfo()->playerInfos)
	{
		PlayerColor color = playerInfo.first;
		if(!vstd::contains(GAME->server().getAllClientPlayers(GAME->server().logicConnection->connectionID), color))
			continue;

		if(!vstd::contains(playerint, color))
		{
			logNetwork->info("Preparing interface for player %s", color.toString());
			if(playerInfo.second.isControlledByAI() || settings["session"]["onlyai"].Bool())
			{
				bool alliedToHuman = false;
				for(const auto & allyInfo : gameState().getStartInfo()->playerInfos)
					if (gameState().getPlayerTeam(allyInfo.first) == gameState().getPlayerTeam(playerInfo.first) && allyInfo.second.isControlledByHuman())
						alliedToHuman = true;

				// ML: player 0 (red) is always the RL model via MMAI (adventureAlliedAI).
				// Other players use the configured AI (adventureEnemyAI = Nullkiller2 for real opponents).
				// 2026-08-01: settings 写读层不一致 (MLClient 写 session, 这里读配置层) — 改用跨 .so 全局直传
				auto AiToGive = (color == PlayerColor(0))
					? (g_adventure_allied_ai[0] ? g_adventure_allied_ai : settings["ai"]["adventureAlliedAI"].String())
					: aiNameForPlayer(playerInfo.second, false, alliedToHuman);
				logNetwork->info("Player %s will be lead by %s", color.toString(), AiToGive);
				installNewPlayerInterface(CDynLibHandler::getNewAI(AiToGive), color);
			}
			else
			{
				logNetwork->info("Player %s will be lead by human", color.toString());
				installNewPlayerInterface(std::make_shared<CPlayerInterface>(color), color);
			}

			if(!advInterfaceReadySent.contains(color))
			{
				advInterfaceReadySent.insert(color);
				AdvInterfaceReady air;
				sendRequest(air, color, /* waitTillRealize = */ false);
			}
		}
	}

	if(settings["session"]["spectate"].Bool())
	{
		installNewPlayerInterface(std::make_shared<CPlayerInterface>(PlayerColor::SPECTATOR), PlayerColor::SPECTATOR, true);
	}

	if(GAME->server().getAllClientPlayers(GAME->server().logicConnection->connectionID).count(PlayerColor::NEUTRAL))
		installNewBattleInterface(CDynLibHandler::getNewBattleAI(settings["ai"]["combatNeutralAI"].String()), PlayerColor::NEUTRAL);

	logNetwork->trace("Initialized player interfaces %d ms", GAME->server().th->getDiff());
}

std::string CClient::aiNameForPlayer(const PlayerSettings & ps, bool battleAI, bool alliedToHuman) const
{
	if(ps.name.size())
	{
		const boost::filesystem::path aiPath = VCMIDirs::get().fullLibraryPath("AI", ps.name);
		if(boost::filesystem::exists(aiPath))
			return ps.name;
	}

	return aiNameForPlayer(battleAI, alliedToHuman);
}

std::string CClient::aiNameForPlayer(bool battleAI, bool alliedToHuman) const
{
	const int sensibleAILimit = settings["session"]["oneGoodAI"].Bool() ? 1 : PlayerColor::PLAYER_LIMIT_I;
	std::string goodAdventureAI = alliedToHuman ? settings["ai"]["adventureAlliedAI"].String() : settings["ai"]["adventureEnemyAI"].String();
	std::string goodBattleAI = settings["ai"]["combatNeutralAI"].String();
	std::string goodAI = battleAI ? goodBattleAI : goodAdventureAI;
	std::string badAI = battleAI ? "StupidAI" : "EmptyAI";

	//TODO what about human players
	if(battleints.size() >= sensibleAILimit)
		return badAI;

	return goodAI;
}

void CClient::installNewPlayerInterface(std::shared_ptr<CGameInterface> gameInterface, PlayerColor color, bool battlecb)
{
	playerint[color] = gameInterface;

	logGlobal->trace("	Initializing the interface for player %s", color.toString());
	auto cb = std::make_shared<CCallback>(gamestate, color, this);
	battleCallbacks[color] = cb;
	gameInterface->initGameInterface(playerEnvironments.at(color), cb, aiCombatOptions);

	installNewBattleInterface(gameInterface, color, battlecb);
}

void CClient::installNewBattleInterface(std::shared_ptr<CBattleGameInterface> battleInterface, PlayerColor color, bool needCallback)
{
	fprintf(stderr, "[DBG installNewBattleInterface] ENTER: color=%s needCallback=%d battleInterface=%p\n", color.toString().c_str(), needCallback, (void*)battleInterface.get());
	battleints[color] = battleInterface;
	fprintf(stderr, "[DBG installNewBattleInterface] battleints assigned OK\n");

	if(needCallback)
	{
		logGlobal->trace("\tInitializing the battle interface for player %s", color.toString());
		fprintf(stderr, "[DBG installNewBattleInterface] BEFORE make_shared CBattleCallback\n");
		auto cbc = std::make_shared<CBattleCallback>(color, this);
		fprintf(stderr, "[DBG installNewBattleInterface] CBattleCallback created OK: %p\n", (void*)cbc.get());
		battleCallbacks[color] = cbc;
		fprintf(stderr, "[DBG installNewBattleInterface] battleCallbacks assigned OK\n");

		fprintf(stderr, "[DBG installNewBattleInterface] BEFORE playerEnvironments.at(%s)\n", color.toString().c_str());
		auto &env = playerEnvironments.at(color);
		fprintf(stderr, "[DBG installNewBattleInterface] env OK: %p\n", (void*)env.get());

		fprintf(stderr, "[DBG installNewBattleInterface] BEFORE initBattleInterface\n");
		battleInterface->initBattleInterface(env, cbc, aiCombatOptions);
		fprintf(stderr, "[DBG installNewBattleInterface] initBattleInterface RETURNED OK\n");
	}
	fprintf(stderr, "[DBG installNewBattleInterface] EXIT OK\n");
}

void CClient::handlePack(CPackForClient & pack)
{
	ApplyClientNetPackVisitor afterVisitor(*this, gameState());
	ApplyFirstClientNetPackVisitor beforeVisitor(*this, gameState());

	pack.visit(beforeVisitor);
	logNetwork->trace("\tMade first apply on cl: %s", typeid(pack).name());
	{
		std::unique_lock lock(CGameState::mutex);
		gameState().apply(pack);
	}
	logNetwork->trace("\tApplied on gs: %s", typeid(pack).name());
	pack.visit(afterVisitor);
	logNetwork->trace("\tMade second apply on cl: %s", typeid(pack).name());
}

std::optional<BattleAction> CClient::makeSurrenderRetreatDecision(PlayerColor player, const BattleID & battleID, const BattleStateInfoForRetreat & battleState)
{
	return playerint[player]->makeSurrenderRetreatDecision(battleID, battleState);
}

// ML fix (09-23, #298): 网络线程标记定义 (由 CServerHandler::threadRunNetwork 置位)
thread_local bool CClient::onNetworkThread = false;

int CClient::sendRequest(const CPackForServer & request, PlayerColor player, bool waitTillRealize)
{
	ui32 requestID = requestCounter++;
	if(std::string(typeid(request).name()).find("UpgradeCreature") != std::string::npos)
		fprintf(stderr, "[ML-upg] UpgradeCreature req=%d waitTillRealize=%d onNetThread=%d\n", requestID, (int)waitTillRealize, (int)CClient::onNetworkThread);
	logNetwork->trace("Sending a request \"%s\". It'll have an ID=%d.", typeid(request).name(), requestID);

	waitingRequest.pushBack(requestID);
	request.requestID = requestID;
	request.player = player;
	GAME->server().sendGamePack(request);
	if(vstd::contains(playerint, player))
		playerint[player]->requestSent(&request, requestID);

	if(waitTillRealize)
	{
		if(CClient::onNetworkThread)
		{
			// ML fix (09-23, #298): 网络线程内跳过等待 — PackageApplied(解除等待的唯一路径)
			// 只能由本线程处理, 在此等待 = 自锁 (实锤: 红方 MMAI yourTurn 经 visitPlayerStartsTurn
			// 在 runNetwork 线程同步执行 endTurn → 300s adventure_wait 超时 → 静默吞局)
			fprintf(stderr, "[ML-fix] skip waitTillRealize on network thread: req=%d %s\n", requestID, typeid(request).name());
		}
		else
		{
			logGlobal->trace("We'll wait till request %d is answered.\n", requestID);
			auto gsUnlocker = vstd::makeUnlockSharedGuard(CGameState::mutex);
			waitingRequest.waitWhileContains(requestID);
		}
	}

	return requestID;
}

void CClient::battleStarted(const BattleID & battleID)
{
	fprintf(stderr, "[ML-battleStarted] begin bid=%d\n", battleID.getNum());
	const BattleInfo * info = gameState().getBattle(battleID);

	std::shared_ptr<CPlayerInterface> att;
	std::shared_ptr<CPlayerInterface> def;
	const auto & leftSide = info->getSide(BattleSide::LEFT_SIDE);
	const auto & rightSide = info->getSide(BattleSide::RIGHT_SIDE);

	for(auto & battleCb : battleCallbacks)
	{
		if(!battleCb.first.isValidPlayer() || battleCb.first == leftSide.color || battleCb.first == rightSide.color)
		{
			fprintf(stderr, "[ML-battleStarted] onBattleStarted color=%s\n", battleCb.first.toString().c_str());
			battleCb.second->onBattleStarted(info);
		}
	}

	//If quick combat is not, do not prepare interfaces for battleint
	auto callBattleStart = [&](PlayerColor color, BattleSide side)
	{
		if(vstd::contains(battleints, color))
		{
			fprintf(stderr, "[ML-battleStarted] battleStart color=%s\n", color.toString().c_str());
			battleints[color]->battleStart(info->battleID, leftSide.getArmy(), rightSide.getArmy(), info->tile, leftSide.getHero(), rightSide.getHero(), side, info->replayAllowed);
		}
	};
	
	callBattleStart(leftSide.color, BattleSide::LEFT_SIDE);
	callBattleStart(rightSide.color, BattleSide::RIGHT_SIDE);
	callBattleStart(PlayerColor::UNFLAGGABLE, BattleSide::RIGHT_SIDE);
	if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool())
		callBattleStart(PlayerColor::SPECTATOR, BattleSide::RIGHT_SIDE);
	
	if(vstd::contains(playerint, leftSide.color) && playerint[leftSide.color]->human)
		att = std::dynamic_pointer_cast<CPlayerInterface>(playerint[leftSide.color]);

	if(vstd::contains(playerint, rightSide.color) && playerint[rightSide.color]->human)
		def = std::dynamic_pointer_cast<CPlayerInterface>(playerint[rightSide.color]);
	
	//Remove player interfaces for auto battle (quickCombat option)
	if((att && att->isAutoFightOn) || (def && def->isAutoFightOn))
	{
		auto endTacticPhaseIfEligible = [info](const CPlayerInterface * interface)
		{
			if (interface->cb->getBattle(info->battleID)->battleGetTacticDist())
			{
				auto side = interface->cb->getBattle(info->battleID)->playerToSide(interface->playerID);

				if(interface->playerID == info->getSide(info->tacticsSide).color)
				{
					auto action = BattleAction::makeEndOFTacticPhase(side);
					interface->cb->battleMakeTacticAction(info->battleID, action);
				}
			}
		};

		if(att && att->isAutoFightOn)
			endTacticPhaseIfEligible(att.get());
		else // def && def->isAutoFightOn
			endTacticPhaseIfEligible(def.get());

		att.reset();
		def.reset();
	}

	if(!settings["session"]["headless"].Bool())
	{
		if(att || def)
		{
			CPlayerInterface::battleInt = std::make_shared<BattleInterface>(info->getBattleID(), leftSide.getArmy(), rightSide.getArmy(), leftSide.getHero(), rightSide.getHero(), att, def);
		}
		else if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool())
		{
			//TODO: This certainly need improvement
			auto spectratorInt = std::dynamic_pointer_cast<CPlayerInterface>(playerint[PlayerColor::SPECTATOR]);
			spectratorInt->cb->onBattleStarted(info);
			CPlayerInterface::battleInt = std::make_shared<BattleInterface>(info->getBattleID(), leftSide.getArmy(), rightSide.getArmy(), leftSide.getHero(), rightSide.getHero(), att, def, spectratorInt);
		}
	}

	if(info->tacticDistance)
	{
		auto tacticianColor = info->getSide(info->tacticsSide).color;

		if (vstd::contains(battleints, tacticianColor))
			battleints[tacticianColor]->yourTacticPhase(info->battleID, info->tacticDistance);
	}
}

void CClient::battleFinished(const BattleID & battleID)
{
	for(auto side : { BattleSide::ATTACKER, BattleSide::DEFENDER })
	{
		if(battleCallbacks.count(gameState().getBattle(battleID)->getSide(side).color))
			battleCallbacks[gameState().getBattle(battleID)->getSide(side).color]->onBattleEnded(battleID);
	}

	if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool())
		battleCallbacks[PlayerColor::SPECTATOR]->onBattleEnded(battleID);
}

void CClient::startPlayerBattleAction(const BattleID & battleID, PlayerColor color)
{
	// 0909 插桩: 战斗 stack 轮转调用 (unlockGuard 临时放锁区间)
	fprintf(stderr, "[THREAD] startPlayerBattleAction ENTER tid=%lu color=%d\n", GetCurrentThreadId(), color.getNum()); fflush(stderr);
	if (battleints.count(color) == 0)
		return; // not our combat in MP

	auto battleint = battleints.at(color);

	auto * battle = gameState().getBattle(battleID);
	if (!battle)
	{
		fprintf(stderr, "[BTL-DBG] startPlayerBattleAction: getBattle(%d) == nullptr color=%d\n", battleID.getNum(), color.getNum());
		return;
	}
	auto * stack = battle->battleGetStackByID(battle->activeStack, false);
	if (!stack)
	{
		fprintf(stderr, "[BTL-DBG] startPlayerBattleAction: activeStack %d not found color=%d\n", (int)battle->activeStack, color.getNum());
		return;
	}

	if (!battleint->human)
	{
		// we want to avoid locking gamestate and causing UI to freeze while AI is making turn
		// headless: ENGINE 为 null (P22) — 无 UI 无需 unlock guard
		if (ENGINE)
		{
			fprintf(stderr, "[THREAD] startPlayerBattleAction unlockGuard+activeStack tid=%lu\n", GetCurrentThreadId()); fflush(stderr);
			auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
			battleint->activeStack(battleID, stack);
			fprintf(stderr, "[THREAD] startPlayerBattleAction relock done tid=%lu\n", GetCurrentThreadId()); fflush(stderr);
		}
		else
		{
			battleint->activeStack(battleID, stack);
		}
	}
	else
	{
		battleint->activeStack(battleID, stack);
	}
	fprintf(stderr, "[THREAD] startPlayerBattleAction EXIT tid=%lu\n", GetCurrentThreadId()); fflush(stderr);
}

void CClient::onNewSystemMessageReceived(const std::string & messageText)
{
	logGlobal->info("System message: %s", messageText);
}

void CClient::removeGUI() const
{
	// CClient::endGame
	ENGINE->windows().clear();
	adventureInt.reset();
	logGlobal->info("Removed GUI.");

	GAME->setInterfaceInstance(nullptr);
}

void CClient::registerBattleInterface(std::shared_ptr<IBattleEventsReceiver> battleEvents, PlayerColor color)
{
	additionalBattleInts[color].push_back(battleEvents);
}

void CClient::unregisterBattleInterface(std::shared_ptr<IBattleEventsReceiver> battleEvents, PlayerColor color)
{
	additionalBattleInts[color] -= battleEvents;
}
