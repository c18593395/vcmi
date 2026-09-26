/*
 * T14Nullkiller.h — T14 blue-side strategic planner (layer-2 C++ plugin)
 * Derives NK2AI::Nullkiller and overrides three NK2 zero-default hooks:
 *   - onPursueTurn()      : T14.3 R13 VictoryPursuer harvest nudge (re-assert targetObject)
 *   - onAfterInit()       : T14.3b ② one-shot archetype Settings weight axis
 *   - onCommitPursuit()   : T14.3b ① hard goal injection (committed march task)
 * NK2 base defaults = empty -> zero behavior change for stock Nullkiller.
 */
#pragma once
#include "StdInc.h"
#include "../Nullkiller2/Engine/Nullkiller.h"

class CGHeroInstance;
class CGTownInstance;

namespace T14Blue
{
	// T14.3b ①: hard-commit priority. Above DEFENSIVE_EMERGENCY_PRIORITY (1e6,
	// the highest organic value NK2 produces) so the committed march leads.
	constexpr float T14_PURSUIT_PRIORITY = 10000000.0f;

	class T14Nullkiller : public NK2AI::Nullkiller
	{
	public:
		void onPursueTurn() override;                                  // T14.3 R13 nudge
		void onAfterInit() override;                                     // T14.3b ② weight axis
		void onCommitPursuit(NK2AI::Goals::TTaskVec & selectedTasks) override; // T14.3b ① hard commit

	private:
		void resolveArchetype();
		int  gateDayFor() const;
		bool pickPursuit(const CGHeroInstance *& hero, const CGTownInstance *& town) const;

		bool archetypeResolved = false;
		int  archetype   = 4;   // P4 balanced default
		int  aggression  = 0;   // 0-3
	};
}
