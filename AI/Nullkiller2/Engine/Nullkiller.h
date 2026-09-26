/*
* Nullkiller.h, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#pragma once

#include "PriorityEvaluator.h"
#include "FuzzyHelper.h"
#include "Settings.h"
#include "AIMemory.h"
#include "DeepDecomposer.h"
#include "../Analyzers/DangerHitMapAnalyzer.h"
#include "../Analyzers/BuildAnalyzer.h"
#include "../Analyzers/ArmyManager.h"
#include "../Analyzers/HeroManager.h"
#include "../Analyzers/ObjectClusterizer.h"
#include "../Helpers/ArmyFormation.h"

#include "../../../lib/ConditionalWait.h"

VCMI_LIB_NAMESPACE_BEGIN

class PathfinderCache;

VCMI_LIB_NAMESPACE_END

namespace NK2AI
{

const float MIN_PRIORITY = 0.01f;
const float SMALL_SCAN_MIN_PRIORITY = 0.4f;

enum class HeroLockedReason
{
	NOT_LOCKED = 0,

	STARTUP = 1,

	DEFENCE = 2,

	HERO_CHAIN = 3
};

enum class ScanDepth
{
	MAIN_FULL = 0,

	SMALL = 1,

	ALL_FULL = 2
};

struct TaskPlanItem
{
	std::vector<ObjectInstanceID> affectedObjects;
	Goals::TSubgoal task;

	TaskPlanItem(const Goals::TSubgoal& goal);
};

class TaskPlan
{
private:
	std::vector<TaskPlanItem> tasks;

public:
	Goals::TTaskVec getTasks() const;
	void mergeAndFilter(const Goals::TSubgoal& task);
};

enum class TaskFailureAction
{
	TRY_NEXT_TASK,
	REPLAN,
	STOP_TURN
};

TaskFailureAction chooseTaskFailureAction(bool hasAnySuccess, bool hasRemainingTasks, bool canReplan);

class Nullkiller
{
private:
	const CGHeroInstance * activeHero;
	int3 targetTile;
	ObjectInstanceID targetObject;
	HeroMap<HeroLockedReason> lockedHeroes;
	std::unique_ptr<PathfinderCache> pathfinderCache;
	ScanDepth scanDepth;
	TResources lockedResources;
	bool useHeroChain;
	bool openMap;
	bool useObjectGraph;
	bool pathfinderInvalidated;

public:
	static std::unique_ptr<ObjectGraph> baseGraph;

	std::unique_ptr<DangerHitMapAnalyzer> dangerHitMap;
	std::unique_ptr<BuildAnalyzer> buildAnalyzer;
	std::unique_ptr<ObjectClusterizer> objectClusterizer;
	std::unique_ptr<PriorityEvaluator> priorityEvaluator;
	std::unique_ptr<SharedPool<PriorityEvaluator>> priorityEvaluators;
	std::unique_ptr<AIPathfinder> pathfinder;
	std::atomic_int32_t pathfinderTurnStorageMisses;
	std::unique_ptr<HeroManager> heroManager;
	std::unique_ptr<ArmyManager> armyManager;
	std::unique_ptr<AIMemory> memory;
	std::unique_ptr<FuzzyHelper> dangerEvaluator;
	std::unique_ptr<DeepDecomposer> decomposer;
	std::unique_ptr<ArmyFormation> armyFormation;
	std::unique_ptr<Settings> settings;
	/// Same value as AIGateway->playerID
	PlayerColor playerID;
	AIGateway * aiGw;
	/// Same value as AIGateway->cc
	std::shared_ptr<CCallback> cc;
	std::mutex aiStateMutex;
	mutable ThreadInterruption makingTurnInterruption;

	Nullkiller();
	virtual ~Nullkiller();
	void init(const std::shared_ptr<CCallback> & cbInput, AIGateway * aiGwInput);
	virtual void makeTurn();
	/// T14 (09-26): per-turn pursuit hook, called from makeTurn after defender reservation.
	/// NK2 default = empty (zero behavior change); T14Blue::T14Nullkiller overrides it (R13 harvest nudge).
	virtual void onPursueTurn() {}
	/// T14.3b (09-27): post-selection hard-goal-injection hook, called from makeTurn after
	/// the selected task vector is sorted; NK2 default = empty (zero behavior change);
	/// T14Blue::T14Nullkiller overrides it (committed march task spliced into the stream).
	virtual void onCommitPursuit(Goals::TTaskVec & selectedTasks) {}
	/// T14.3b (09-27): post-init hook, called at the tail of init(); NK2 default = empty;
	/// T14Blue::T14Nullkiller overrides it (one-shot archetype Settings weight axis).
	virtual void onAfterInit() {}
	bool updateStateAndExecutePriorityPass(Goals::TGoalVec& tempResults, int passIndex);
	bool isActive(const CGHeroInstance * hero) const { return activeHero == hero; }
	bool isHeroLocked(const CGHeroInstance * hero) const;
	HeroPtr getActiveHero() { return HeroPtr(activeHero, cc.get()); }
	HeroLockedReason getHeroLockedReason(const CGHeroInstance * hero) const;
	int3 getTargetTile() const { return targetTile; }
	ObjectInstanceID getTargetObject() const { return targetObject; }
	void setTargetObject(int objid) { targetObject = ObjectInstanceID(objid); }
	void setActive(const CGHeroInstance * hero, int3 tile) { activeHero = hero; targetTile = tile; }
	void lockHero(const CGHeroInstance * hero, HeroLockedReason lockReason);
	void unlockHero(const CGHeroInstance * hero);
	bool arePathHeroesLocked(const AIPath & path) const;
	TResources getFreeResources() const;
	int32_t getFreeGold() const { return getFreeResources()[EGameResID::GOLD]; }
	void lockResources(const TResources & res);
	const TResources & getLockedResources() const { return lockedResources; }
	ScanDepth getScanDepth() const { return scanDepth; }
	bool isOpenMap() const { return openMap; }
	bool isObjectGraphAllowed() const { return useObjectGraph; }
	void invalidatePathfinderData();
	std::shared_ptr<const CPathsInfo> getPathsInfo(const CGHeroInstance * h) const;
	void invalidatePaths();
	HeroMap<HeroRole> getHeroesForPathfinding() const;

private:
	void resetState();
	void updateState();
	void reserveRequiredTownDefenders();
	const CGHeroInstance * findRequiredTownDefender(const CGTownInstance * town) const;
	void decompose(Goals::TGoalVec & results, const Goals::TSubgoal& behavior, int decompositionMaxDepth) const;
	Goals::TTask choseBestTask(Goals::TGoalVec & tasks) const;
	using EvaluationContextMap = std::map<const Goals::AbstractGoal *, EvaluationContext>;
	EvaluationContextMap buildEvaluationContexts(const Goals::TGoalVec & tasks) const;
	Goals::TTaskVec buildPlanAndFilter(
		Goals::TGoalVec & tasks,
		const EvaluationContextMap & evaluationContexts,
		int priorityTier) const;
	bool executeTask(const Goals::TTask & task);
	bool areAffectedObjectsPresent(const Goals::TTask & task) const;
	HeroRole getTaskRole(const Goals::TTask & task) const;
	std::vector<const CGHeroInstance *> getTaskHeroes(const Goals::TTask & task) const;
	void lockTaskHeroes(const Goals::TTask & task, HeroLockedReason lockReason);
	bool hasUnlockedHeroWithMovement() const;
	void tracePlayerStatus(bool beginning) const;
};

}
