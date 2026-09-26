/*
 * T14Nullkiller.cpp — T14 blue-side strategic planner (layer-2 C++ plugin)
 *
 * T14.3 (09-26): R13 VictoryPursuer harvest NUdGE — onPursueTurn re-asserts
 *   "strongest unlocked hero -> enemy town" via targetObject (only influences
 *   the decompose window + blocking-dialog judgement; the target is reset every
 *   pass by updateState()).
 *
 * T14.3b (09-27): capability upgrade, NK2 engine side = zero-default hooks only:
 *   ① onCommitPursuit — HARD goal injection: splice a real ExecuteHeroChain march
 *      task into the selected task vector (executeTask actually runs it; the
 *      committed march leads by priority above NK2's organic cap).
 *   ② onAfterInit    — Settings weight axis: one-shot archetype overrides via
 *      gated setters (NK2 never calls them -> stock behavior unchanged).
 *
 * Archetype: FNV1a(mapname#difficulty#playerID) % 5 — same map+difficulty ->
 * same behavior (reproducible). P0 turtle 20d / P1 rush 7d / P2 guerrilla
 * (no central march) / P3 flyer (furthest main town) / P4 balanced (nearest).
 */
#include "StdInc.h"
#include "T14Nullkiller.h"
#include "../Nullkiller2/Engine/Nullkiller.h"
#include "../Nullkiller2/Goals/AbstractGoal.h"
#include "../Nullkiller2/Goals/ExecuteHeroChain.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/StartInfo.h"
#include "../../lib/constants/Enumerations.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace
{
	uint32_t fnv1a(const std::string & s)
	{
		uint32_t h = 2166136261u;
		for(char c : s)
		{
			h ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
			h *= 16777619u;
		}
		return h;
	}
}

namespace T14Blue
{
	void T14Nullkiller::resolveArchetype()
	{
		if(archetypeResolved)
			return;
		const StartInfo * si = cc->getStartInfo();
		std::string key = "T14BLUE";
		if(si)
			key = si->mapname + "#" + std::to_string((int)si->difficulty) + "#" + std::to_string(playerID.getNum());
		const uint32_t hash = fnv1a(key);
		archetype  = hash % 5;
		aggression = (hash >> 3) % 4;
		archetypeResolved = true;
		logAi->info("[T14-ACT] T14Blue resolved archetype=P%d aggr=%d seed=%u map=%s",
		            (int)archetype, aggression, hash, si ? si->mapname.c_str() : "?");
	}

	int T14Nullkiller::gateDayFor() const
	{
		int gateDay;
		switch(archetype)
		{
		case 0:  gateDay = 20; break; // P0 turtle
		case 1:  gateDay =  7; break; // P1 rush
		case 3:  gateDay = 10; break; // P3 flyer
		default: gateDay =  0; break; // P4 balanced: keep pushing
		}
		return std::max(0, gateDay - aggression);
	}

	bool T14Nullkiller::pickPursuit(const CGHeroInstance *& hero, const CGTownInstance *& town) const
	{
		hero = nullptr;
		town = nullptr;

		for(const CGHeroInstance * h : cc->getHeroesInfo())
		{
			if(!h || isHeroLocked(h))
				continue;
			if(!hero || h->getTotalStrength() > hero->getTotalStrength())
				hero = h;
		}
		if(!hero)
			return false;

		const bool wantFurthest = (archetype == 3);
		const int3 refPos = hero->anchorPos();
		si32 bestDist = wantFurthest ? -1 : 1 << 30;
		for(const CGTownInstance * t : cc->getTownsInfo(false)) // all visible towns
		{
			if(!t)
				continue;
			if(cc->getPlayerRelations(playerID, t->getOwner()) != PlayerRelations::ENEMIES)
				continue;
			const int3 tp = t->anchorPos();
			const si32 d = std::abs(tp.x - refPos.x) + std::abs(tp.y - refPos.y);
			if((wantFurthest && d > bestDist) || (!wantFurthest && d < bestDist))
			{
				bestDist = d;
				town = t;
			}
		}
		return town != nullptr;
	}

	void T14Nullkiller::onPursueTurn()
	{
		// T14.3 R13 harvest nudge (kept as a safety net below the hard commit).
		resolveArchetype();

		if(archetype == 2)
			return; // P2 guerrilla: stay dispersed, no central march

		if(cc->getCalendar().getCurrentDay() < gateDayFor())
			return;

		const CGHeroInstance * best = nullptr;
		const CGTownInstance * target = nullptr;
		if(!pickPursuit(best, target))
			return;

		setActive(best, target->anchorPos());
		setTargetObject(target->id.getNum());
		objectClusterizer->reset();

		logAi->info("[T14-ACT] day=%d P%d aggr=%d nudge->enemyTown obj=%d heroStr=%llu",
		            cc->getCalendar().getCurrentDay(), (int)archetype, aggression,
		            target->id.getNum(), (unsigned long long)best->getTotalStrength());
	}

	void T14Nullkiller::onAfterInit()
	{
		// T14.3b ②: one-shot archetype Settings weight axis (gated setters; stock
		// Nullkiller never invokes them -> zero behavior change for NK2).
		resolveArchetype();
		switch(archetype)
		{
		case 0: // P0 turtle: hoard gold, late push, conservative retreat
			settings->setT14Weights(/*maxGoldPressure*/0.6f,
			                        /*retreatThresholdRelative*/0.5f,
			                        /*mainHeroTurnDistanceLimit*/10);
			break;
		case 1: // P1 rush: spend fast, long-range main hero, loose retreat
			settings->setT14Weights(/*maxGoldPressure*/0.2f,
			                        /*retreatThresholdRelative*/0.2f,
			                        /*mainHeroTurnDistanceLimit*/14);
			break;
		case 3: // P3 flyer: longest main hero reach
			settings->setT14Weights(/*maxGoldPressure*/0.3f,
			                        /*retreatThresholdRelative*/0.3f,
			                        /*mainHeroTurnDistanceLimit*/20);
			break;
		case 4: // P4 balanced: slightly more aggressive than stock
			settings->setT14Weights(/*maxGoldPressure*/0.4f,
			                        /*retreatThresholdRelative*/0.4f,
			                        /*mainHeroTurnDistanceLimit*/12);
			break;
		default: // P2 guerrilla: keep stock settings (stay dispersed)
			break;
		}
		logAi->info("[T14-ACT] WEIGHTS P%d goldPressure=%g retreatRel=%g mainDist=%d",
		            (int)archetype,
		            (double)settings->getMaxGoldPressure(),
		            (double)settings->getRetreatThresholdRelative(),
		            settings->getMainHeroTurnDistanceLimit());
	}

	void T14Nullkiller::onCommitPursuit(NK2AI::Goals::TTaskVec & selectedTasks)
	{
		// T14.3b ①: hard goal injection — splice the committed march into the
		// selected task vector; executeTask then actually performs it.
		resolveArchetype();

		if(archetype == 2)
			return; // P2 guerrilla: no central commit

		const int day = cc->getCalendar().getCurrentDay();
		if(day < gateDayFor())
			return;

		const CGHeroInstance * best = nullptr;
		const CGTownInstance * target = nullptr;
		if(!pickPursuit(best, target))
			return;

		// Pick the safest usable path toward the target town (prefer the pursuit
		// hero, fall back to the cheapest path overall).
		const auto paths = pathfinder->getPathInfo(target->anchorPos());
		const NK2AI::AIPath * preferred = nullptr;
		const NK2AI::AIPath * fallback = nullptr;
		uint64_t preferredDanger = std::numeric_limits<uint64_t>::max();
		uint64_t fallbackDanger = std::numeric_limits<uint64_t>::max();
		for(const auto & p : paths)
		{
			if(p.nodes.empty() || !p.targetHero)
				continue;
			const uint64_t danger = p.getTotalDanger();
			if(p.targetHero == best && danger < preferredDanger)
			{
				preferredDanger = danger;
				preferred = &p;
			}
			if(danger < fallbackDanger)
			{
				fallbackDanger = danger;
				fallback = &p;
			}
		}
		const NK2AI::AIPath * chosen = preferred ? preferred : fallback;
		if(!chosen)
			return;

		// Replace the Invalid placeholder with the committed march, or front-insert
		// it ahead of weaker tasks.
		auto commitTask = NK2AI::Goals::taskptr(
			NK2AI::Goals::ExecuteHeroChain(*chosen, target).setpriority(T14_PURSUIT_PRIORITY));

		for(size_t i = 0; i < selectedTasks.size(); ++i)
		{
			if(selectedTasks[i]->getHero() == chosen->targetHero)
			{
				selectedTasks[i] = commitTask; // overwrite same-hero task with the commit
				logAi->info("[T14-ACT] day=%d P%d aggr=%d COMMIT(overw) march->obj=%d danger=%llu hero=%s",
				            day, (int)archetype, aggression, target->id.getNum(),
				            (unsigned long long)(chosen == preferred ? preferredDanger : fallbackDanger),
				            chosen->targetHero->getObjectName().c_str());
				return;
			}
		}
		selectedTasks.insert(selectedTasks.begin(), commitTask);
		logAi->info("[T14-ACT] day=%d P%d aggr=%d COMMIT(front) march->obj=%d danger=%llu hero=%s",
		            day, (int)archetype, aggression, target->id.getNum(),
		            (unsigned long long)(chosen == preferred ? preferredDanger : fallbackDanger),
		            chosen->targetHero->getObjectName().c_str());
	}
}
