/*
 * CQuery.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "QueriesProcessor.h"

#include "CQuery.h"

void QueriesProcessor::popQuery(PlayerColor player, QueryPtr query)
{
	LOG_TRACE_PARAMS(logGlobal, "player='%s', query='%s'", player % query);
	if(topQuery(player) != query)
	{
		logGlobal->trace("Cannot remove, not a top!");
		fprintf(stderr, "[ML-q] popQuery FAIL player=%d target=%s\n",
			player.getNum(), query->toString().c_str());
		for(const auto & q : queries[player])
			fprintf(stderr, "  [ML-q]   stack: %s\n", q->toString().c_str());
		fprintf(stderr, "[ML-stk] POP-FAIL player=%d depth=%d %s\n", player.getNum(), (int)queries[player].size(), query->toString().c_str());
		return;
	}

	queries[player] -= query;
	fprintf(stderr, "[ML-stk] POP player=%d depth=%d %s\n", player.getNum(), (int)queries[player].size(), query->toString().c_str());
	auto nextQuery = topQuery(player);

	query->onRemoval(player);

	//Exposure on query below happens only if removal didn't trigger any new query
	if(nextQuery && nextQuery == topQuery(player))
		nextQuery->onExposure(query);
}

void QueriesProcessor::popQuery(const CQuery &query)
{
	LOG_TRACE_PARAMS(logGlobal, "query='%s'", query);

	assert(query.players.size());
	for(auto player : query.players)
	{
		auto top = topQuery(player);
		if(top.get() == &query)
			popQuery(top);
		else
		{
			logGlobal->trace("Cannot remove query %s", query.toString());
			logGlobal->trace("Queries found:");
			for(auto q : queries[player])
			{
				logGlobal->trace(q->toString());
			}
		}
	}
}

void QueriesProcessor::popQuery(QueryPtr query)
{
	for(auto player : query->players)
		popQuery(player, query);
}

void QueriesProcessor::addQuery(QueryPtr query)
{
	for(auto player : query->players)
		addQuery(player, query);

	for(auto player : query->players)
		query->onAdded(player);
}

void QueriesProcessor::addQuery(PlayerColor player, QueryPtr query)
{
	LOG_TRACE_PARAMS(logGlobal, "player='%d', query='%s'", player.getNum() % query);
	query->onAdding(player);
	queries[player].push_back(query);
	fprintf(stderr, "[ML-stk] PUSH player=%d depth=%d %s\n", player.getNum(), (int)queries[player].size(), query->toString().c_str());
}

void QueriesProcessor::setListener(IQueryStackListener * listener)
{
	queriesStackListener = listener;
}

int QueriesProcessor::countQuery(const QueryPtr & query) const
{
	int count = 0;
	for(const auto & playerQueries : queries)
		for(const auto & q : playerQueries)
			if(q == query)
				count++;
	return count;
}

QueryPtr QueriesProcessor::topQuery(PlayerColor player)
{
	return vstd::backOrNull(queries[player]);
}

void QueriesProcessor::removeQuery(QueryPtr query)
{
	// ML fix (2026-08-17): 任意位置强制移除 (战斗结算兜底)。
	// 1) [09-14 修正/09-23 恢复] onRemoval 每玩家各调一次 (对齐上游 popQuery per-player 语义):
	//    旧 removalDone 守卫让 PvP 共享 query 只调一次 → remainingBattleQueriesCount 欠减
	//    → BattleEnded 永不发 → currentBattles 残留 (+ 无人判 LOSER)。见踩坑 #228。
	// 2) 移除后必须触发暴露链 (对齐 popQuery): 否则被压的 visitQuery 永不 onExposure
	//    → 访问永不结束 (obj=1 永久) → 怪物战斗收尾断 (battleFinished 不触发)。
	if(!query)
		return;
	for(PlayerColor player : query->players)
	{
		auto & v = queries[player];
		auto it = std::find(v.begin(), v.end(), query);
		if(it != v.end())
		{
			v.erase(it);
			fprintf(stderr, "[ML-stk] FORCE-REMOVE player=%d depth=%d %s\n", player.getNum(), (int)queries[player].size(), query->toString().c_str());
				query->onRemoval(player);
			auto nextQuery = topQuery(player);
			if(nextQuery)
				nextQuery->onExposure(query);
		}
	}
}

void QueriesProcessor::popIfTop(QueryPtr query)
{
	LOG_TRACE_PARAMS(logGlobal, "query='%d'", query);
	if(!query)
		logGlobal->error("The query is nullptr! Ignoring.");

	popIfTop(*query);
}

void QueriesProcessor::popIfTop(const CQuery & query)
{
	for(PlayerColor color : query.players)
	{
		auto top = topQuery(color);
		if(top.get() == &query)
		{
			fprintf(stderr, "[ML-stk] POPIFTOP-HIT player=%d depth=%d %s\n", color.getNum(), (int)queries[color].size(), query.toString().c_str());
			popQuery(color, top);
		}
		else
		{
			// ML debug (ring 6): popIfTop FAIL = blocking query on top
			fprintf(stderr, "[ML-stk] POPIFTOP-FAIL player=%d depth=%d %s\n", color.getNum(), (int)queries[color].size(), query.toString().c_str());
			fprintf(stderr, "[ML-q] popIfTop FAIL color=%d target=%s top=%s\n",
				color.getNum(), query.toString().c_str(),
				top ? top->toString().c_str() : "null");
			for(const auto & q : queries[color])
				fprintf(stderr, "  [ML-q]   stack: %s\n", q->toString().c_str());
		}
	}
}

QueriesProcessor::AllQueriesView QueriesProcessor::allQueries()
{
	return AllQueriesView{ queries };
}

QueriesProcessor::AllQueriesViewConst QueriesProcessor::allQueries() const
{
	return AllQueriesViewConst{ queries };
}

QueryPtr QueriesProcessor::getQuery(QueryID queryID)
{
	for(auto & playerQueries : queries)
		for(auto & query : playerQueries)
			if(query->queryID == queryID)
				return query;
	return nullptr;
}
