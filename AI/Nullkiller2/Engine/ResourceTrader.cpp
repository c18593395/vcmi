/*
* ResourceTrader.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*/
#include "StdInc.h"

// ML fix (09-24): 规划链耗时打点 (trade 循环无上界 + 锁内全量重算取证)
struct MlTimeGuard {
	const char * name;
	std::chrono::steady_clock::time_point t0;
	MlTimeGuard(const char * n) : name(n), t0(std::chrono::steady_clock::now())
	{
		fprintf(stderr, "[ML-time] %s ENTER\n", name);
	}
	~MlTimeGuard()
	{
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
		fprintf(stderr, "[ML-time] %s EXIT %lldms\n", name, (long long)ms);
	}
};
#include "ResourceTrader.h"

namespace NK2AI
{
bool ResourceTrader::trade(BuildAnalyzer & buildAnalyzer, CCallback & cc, const TResources & freeResources)
{
	bool haveTraded = false;
	ObjectInstanceID marketId;

	// TODO: Mircea: What about outside town markets that have better rates than a single town for example?
	// Are those used anywhere? To inspect.
	for(const auto * const town : cc.getTownsInfo())
	{
		if(town->hasBuiltResourceMarketplace())
		{
			marketId = town->id;
			break;
		}
	}

	if(!marketId.hasValue())
		return false;

	const CGObjectInstance * obj = cc.getObj(marketId, false);
	assert(obj);
	// if (!obj)
	// return false;

	const auto * market = dynamic_cast<const IMarket *>(obj);
	assert(market);
	// if (!market)
	// return false;

	bool shouldTryToTrade = true;
	int mlTradePasses = 0;  // ML fix (09-24): 交易循环熔断 cap 16 — 循环无上界时每轮全量 update() 持共享锁狂转 → 写锁饥饿冻结
	auto mlTradeT0 = std::chrono::steady_clock::now();
	while(shouldTryToTrade)
	{
		shouldTryToTrade = false;
		buildAnalyzer.update();
		if(++mlTradePasses > 16)
		{
			fprintf(stderr, "[ML-fix] trade BREAK pass=%d (loop cap)\n", mlTradePasses);
			break;
		}


		// if we favor getResourcesRequiredNow is better on short term, if we favor getTotalResourcesRequired is better on long term
		TResources missingNow = buildAnalyzer.getMissingResourcesNow(ARMY_GOLD_RATIO_PER_MAKE_TURN_PASS);
		if(missingNow.empty())
			break;

		const TResources income = buildAnalyzer.getDailyIncome();
		// We don't want to sell something that's necessary later on, though that could make short term a bit harder sometimes
		// TODO: Mircea: Consider allowing the sale of just a few resources even if necessary long term if critical short term
		// to buy a capitol for example
		TResources freeAfterMissingTotal = buildAnalyzer.getFreeResourcesAfterMissingTotal(ARMY_GOLD_RATIO_PER_MAKE_TURN_PASS);

		logAi->info(
			"ResourceTrader: Free %s. FreeAfterMissingTotal %s. MissingNow  %s",
			freeResources.toString(),
			freeAfterMissingTotal.toString(),
			missingNow.toString()
		);

		if(tradeHelper(EXPENDABLE_BULK_RATIO, *market, missingNow, income, freeAfterMissingTotal, buildAnalyzer, cc))
		{
			haveTraded = true;
			shouldTryToTrade = true;
		}
	}
	auto mlTradeMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - mlTradeT0).count();
	fprintf(stderr, "[ML-time] trade EXIT passes=%d %lldms\n", mlTradePasses, (long long)mlTradeMs);
	return haveTraded;
}

bool ResourceTrader::tradeHelper(
	const float expendableBulkRatio,
	const IMarket & market,
	TResources missingNow,
	TResources income,
	TResources freeAfterMissingTotal,
	const BuildAnalyzer & buildAnalyzer,
	CCallback & cc
)
{
	constexpr int EMPTY = -1;
	int mostWanted = EMPTY;
	TResource mostWantedScoreNeg = std::numeric_limits<TResource>::max();
	int mostExpendable = EMPTY;
	TResource mostExpendableAmountPos = 0;

	// Find the most wanted resource
	for(int i = 0; i < missingNow.size(); ++i)
	{
		if(missingNow[i] == 0)
			continue;

		TResource score = income[i] - missingNow[i];
		if(i != GameResID::GOLD)
		{
			int givenPerUnit;
			int receivedPerUnit;
			market.getOffer(i, GameResID::GOLD, givenPerUnit, receivedPerUnit, EMarketMode::RESOURCE_RESOURCE);
			score *= receivedPerUnit;
			logAi->trace("ResourceTrader: mostWantedScoreNeg %d for %d with market receivedPerUnit %d", mostWantedScoreNeg, i, receivedPerUnit);
		}

		if(score < mostWantedScoreNeg)
		{
			mostWanted = i;
			mostWantedScoreNeg = score;
		}
	}

	// Find the most expendable resource
	for(int i = 0; i < missingNow.size(); ++i)
	{
		const TResource amountToSell = freeAfterMissingTotal[i];
		if(amountToSell == 0)
			continue;

		bool okToSell = false;
		if(i == GameResID::GOLD)
		{
			// TODO: Mircea: Check if we should negate isGoldPressureOverMax() instead
			if(income[GameResID::GOLD] > 0 && !buildAnalyzer.isGoldPressureOverMax())
				okToSell = true;
		}
		else
		{
			okToSell = true;
		}

		if(okToSell && amountToSell > mostExpendableAmountPos)
		{
			mostExpendable = i;
			mostExpendableAmountPos = amountToSell;
		}
	}

	logAi->trace(
		"ResourceTrader: mostWanted: %d, mostWantedScoreNeg %d, mostExpendable: %d, mostExpendableAmountPos %d",
		mostWanted,
		mostWantedScoreNeg,
		mostExpendable,
		mostExpendableAmountPos
	);

	if(mostExpendable == mostWanted || mostWanted == EMPTY || mostExpendable == EMPTY)
		return false;

	int givenPerUnit;
	int receivedPerUnit;
	market.getOffer(mostExpendable, mostWanted, givenPerUnit, receivedPerUnit, EMarketMode::RESOURCE_RESOURCE);

	if(!givenPerUnit || !receivedPerUnit)
	{
		logGlobal->error(
			"ResourceTrader: No offer for %d of %d, given %d, received %d. Should never happen", mostExpendable, mostWanted, givenPerUnit, receivedPerUnit
		);
		return false;
	}

	// TODO: Mircea: if 15 wood and 14 gems, gems can be used a lot more for buying other things
	if(givenPerUnit > mostExpendableAmountPos)
		return false;

	TResource multiplier = std::min(
		static_cast<int>(mostExpendableAmountPos * expendableBulkRatio / givenPerUnit),
		missingNow[mostWanted] / receivedPerUnit
	); // for gold we have to / receivedUnits, because 1 ore gives many gold units
	if(multiplier == 0) // could happen for very small values due to EXPENDABLE_BULK_RATIO
		multiplier = 1;

	const TResource givenMultiplied = givenPerUnit * multiplier;
	if(givenMultiplied > freeAfterMissingTotal[mostExpendable])
	{
		logGlobal->error("ResourceTrader: Something went wrong with the multiplier %d", multiplier);
		return false;
	}

	cc.trade(market.getObjInstanceID(), EMarketMode::RESOURCE_RESOURCE, GameResID(mostExpendable), GameResID(mostWanted), givenMultiplied);
	logAi->info("ResourceTrader: Traded %d of %s for %d receivedPerUnit of %s", givenMultiplied, mostExpendable, receivedPerUnit, mostWanted);
	return true;
}
}