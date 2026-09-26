/*
 * T14Nullkiller.cpp — T14 blue-side strategic planner (layer-2 C++ plugin)
 * R13 VictoryPursuer harvest nudge (方案_T14规则设计_20260925 层2首件).
 *
 * 语义: NK2 本地最优循环常"攒经济不出门" -> 全池 5000 天截断 DRAW (S-7 根因).
 * 本钩子每回合把 "最强未锁英雄 -> 敌城" re-assert 为规划目标, 破 DRAW 根因.
 * 目标选择按原型差异化 (P0 龟速20天 / P1 速攻7天 / P2 游击不推进 / P3 飞取最远主城
 * / P4 均衡最近), 同地图+难度 -> 同原型 (可复现).
 *
 * 边界: nudge 非硬目标注入 -- targetObject 被 NK2 每 pass 的 updateState() 重置,
 * 本钩子在 makeTurn 的 defender 预留后 re-set, 影响主 decompose 窗 + blocking 判定.
 * 真·硬目标注入需 decompose 私有面 = T14.3b 另排.
 */
#include "StdInc.h"
#include "T14Nullkiller.h"
#include "../Nullkiller2/Engine/Nullkiller.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/StartInfo.h"
#include "../../lib/constants/Enumerations.h"

#include <cstdint>
#include <cstdlib>

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
	void T14Nullkiller::onPursueTurn()
	{
		if(!archetypeResolved)
		{
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

		if(archetype == 2)
			return; // P2 guerrilla: stay dispersed, no central march

		const int day = cc->getCalendar().getCurrentDay();

		int gateDay;
		switch(archetype)
		{
		case 0:  gateDay = 20; break; // P0 turtle
		case 1:  gateDay =  7; break; // P1 rush
		case 3:  gateDay = 10; break; // P3 flyer
		default: gateDay =  0; break; // P4 balanced: keep pushing
		}
		gateDay = std::max(0, gateDay - aggression);
		if(day < gateDay)
			return;

		const CGHeroInstance * best = nullptr;
		for(const CGHeroInstance * h : cc->getHeroesInfo())
		{
			if(!h || isHeroLocked(h))
				continue;
			if(!best || h->getTotalStrength() > best->getTotalStrength())
				best = h;
		}
		if(!best)
			return;

		const bool wantFurthest = (archetype == 3);
		const CGTownInstance * target = nullptr;
		const int3 refPos = best->anchorPos();
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
				target = t;
			}
		}
		if(!target)
			return;

		setActive(best, target->anchorPos());
		setTargetObject(target->id.getNum());
		objectClusterizer->reset();

		logAi->info("[T14-ACT] day=%d P%d aggr=%d pursue->enemyTown obj=%d dist=%d heroStr=%llu",
		            day, (int)archetype, aggression, target->id.getNum(), (int)bestDist,
		            (unsigned long long)best->getTotalStrength());
	}
}
