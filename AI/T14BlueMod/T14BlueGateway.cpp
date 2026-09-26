/*
 * T14BlueGateway.cpp — T14 blue-side strategic AI gateway (layer-2 C++ plugin)
 * Base AIGateway ctor creates a stock Nullkiller; we swap in T14Nullkiller.
 * Safe: init() has not run yet (it runs later in initGameInterface).
 */
#include "StdInc.h"
#include "T14BlueGateway.h"
#include "T14Nullkiller.h"
#include "../Nullkiller2/Engine/Nullkiller.h"

#include <memory>

namespace T14Blue
{
	T14BlueGateway::T14BlueGateway()
	{
		nullkiller = std::make_unique<T14Nullkiller>();
	}
}
