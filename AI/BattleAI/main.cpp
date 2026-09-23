/*
 * main.cpp — restored from 1.7.5 (fork removed it but runtime still LoadLibrary's BattleAI.dll)
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "BattleAI.h"

#ifdef __GNUC__
#define strcpy_s(a, b, c) strncpy(a, c, b)
#endif

static const char * const g_cszAiName = "Battle AI";

extern "C" DLL_EXPORT int GetGlobalAiVersion()
{
	return AI_INTERFACE_VER;
}

extern "C" DLL_EXPORT void GetAiName(char* name)
{
	strcpy_s(name, strlen(g_cszAiName) + 1, g_cszAiName);
}

extern "C" DLL_EXPORT void GetNewBattleAI(std::shared_ptr<CBattleGameInterface> &out)
{
	out = std::make_shared<CBattleAI>();
}
