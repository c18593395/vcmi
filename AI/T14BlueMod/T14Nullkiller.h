/*
 * T14Nullkiller.h — T14 blue-side strategic planner (layer-2 C++ plugin)
 * Derives NK2AI::Nullkiller, overrides onPursueTurn() (R13 harvest nudge).
 * NK2 base default = empty -> zero behavior change for stock Nullkiller.
 */
#pragma once
#include "StdInc.h"
#include "../Nullkiller2/Engine/Nullkiller.h"

namespace T14Blue
{
	class T14Nullkiller : public NK2AI::Nullkiller
	{
	public:
		void onPursueTurn() override;

	private:
		bool archetypeResolved = false;
		int  archetype   = 4;  // P4 balanced default
		int  aggression  = 0;  // 0-3
	};
}
