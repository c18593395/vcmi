/*
 * T14BlueGateway.h — T14 blue-side strategic AI gateway (layer-2 C++ plugin)
 * Thin derivative of NK2AI::AIGateway: whole planning/event skeleton reused,
 * only the planner swapped to T14Nullkiller (onPursueTurn harvest hook).
 * R6 battle routing / R17 skill pick = inherited from NK2, zero override.
 */
#pragma once
#include "StdInc.h"
#include "../Nullkiller2/AIGateway.h"

namespace T14Blue
{
	class T14Nullkiller; // fwd

	class T14BlueGateway : public NK2AI::AIGateway
	{
	public:
		T14BlueGateway();
		~T14BlueGateway() = default;
	};
}
