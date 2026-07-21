/*-------------------------------------------------------------------------
 *
 * plan_filter.c
 *
 * Loadable PostgreSQL module to filter statements according to configured
 * criteria and stop them before they start to run.
 *
 * The currently implemented criterion is the plan's estimated maximum cost.
 *
 * Copyright 2012-2015 PostgreSQL Experts, Inc.
 *
 * Distributed under The PostgreSQL License
 * see License file for terms
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <float.h>

#include "access/parallel.h"
#include "access/xact.h"
#include "executor/executor.h"
#include "optimizer/planner.h"
#include "utils/guc.h"

#if PG_VERSION_NUM < 140000
#error "pg_plan_filter requires PostgreSQL 14 or later"
#endif

/*
 * PG_MODULE_MAGIC_EXT (PostgreSQL 18+) lets the server report the module's
 * name and version; older releases only have the plain magic block.
 */
#ifdef PG_MODULE_MAGIC_EXT
PG_MODULE_MAGIC_EXT(.name = "plan_filter", .version = "1.0.0");
#else
PG_MODULE_MAGIC;
#endif

static double statement_cost_limit = 0.0;

static double transaction_cost_limit = 0.0;

static bool module_loaded = false;

static bool filter_select_only = false;

/*
 * Estimated cost accumulated by the statements executed so far in the
 * current transaction.  Always maintained (even with no limit set), so
 * that enabling the limit mid-transaction still sees the full history;
 * zeroed by plan_filter_xact_callback at end of transaction.
 */
static double xact_cost_total = 0.0;

static planner_hook_type prev_planner_hook = NULL;

static ExecutorStart_hook_type prev_ExecutorStart_hook = NULL;

static PlannedStmt *limit_func(Query *parse, const char *query_string,
							   int cursorOptions,
							   ParamListInfo boundParams);
static void xact_limit_func(QueryDesc *queryDesc, int eflags);
static void plan_filter_xact_callback(XactEvent event, void *arg);

void		_PG_init(void);

/*
 * Module load callback.
 *
 * There is deliberately no _PG_fini: PostgreSQL 15 removed the ability to
 * unload a shared library, and it was not reachable in practice before
 * that, so the hook stays installed for the life of the backend.
 */
void
_PG_init(void)
{
	/* Define custom GUC variable. */
	DefineCustomRealVariable("plan_filter.statement_cost_limit",
							 "Sets the maximum allowed plan cost above which "
							 "a statement will not run.",
							 "Zero turns this feature off.",
							 &statement_cost_limit,
							 0.0,
							 0.0, DBL_MAX,
							 PGC_SUSET,
							 0, /* no flags required */
							 NULL,
							 NULL,
							 NULL);

	/* Define custom GUC variable. */
	DefineCustomRealVariable("plan_filter.transaction_cost_limit",
							 "Sets the maximum allowed total plan cost of "
							 "statements executed in one transaction.",
							 "Zero turns this feature off.",
							 &transaction_cost_limit,
							 0.0,
							 0.0, DBL_MAX,
							 PGC_SUSET,
							 0, /* no flags required */
							 NULL,
							 NULL,
							 NULL);

	/* Define custom GUC variable. */
	DefineCustomBoolVariable("plan_filter.module_loaded",
							 "true if the module is loaded ",
							 NULL,
							 &module_loaded,
							 true,
							 PGC_BACKEND,
							 0, /* no flags required */
							 NULL,
							 NULL,
							 NULL);

	/* Define custom GUC variable. */
	DefineCustomBoolVariable("plan_filter.filter_select_only",
							 "Limit the filter to SELECT queries "
							 "only.",
							 "true turns this feature on (Default is false).",
							 &filter_select_only,
							 false,
							 PGC_SUSET,
							 0, /* no flags required */
							 NULL,
							 NULL,
							 NULL);

	/*
	 * Reserve the "plan_filter." prefix so that a misspelled setting is an
	 * error rather than a silently-ignored placeholder that would leave the
	 * filter turned off.  PostgreSQL 14 only has the older call, which just
	 * warns about existing placeholders at load time.
	 */
#if PG_VERSION_NUM >= 150000
	MarkGUCPrefixReserved("plan_filter");
#else
	EmitWarningsOnPlaceholders("plan_filter");
#endif

	/* install the hooks */
	prev_planner_hook = planner_hook;
	planner_hook = limit_func;
	prev_ExecutorStart_hook = ExecutorStart_hook;
	ExecutorStart_hook = xact_limit_func;

	RegisterXactCallback(plan_filter_xact_callback, NULL);
}

static PlannedStmt *
limit_func(Query *parse, const char *query_string, int cursorOptions,
		   ParamListInfo boundParams)
{
	PlannedStmt *result;

	/* this way we can daisy chain planner hooks if necessary */
	if (prev_planner_hook != NULL)
		result = (*prev_planner_hook) (parse, query_string, cursorOptions,
									   boundParams);
	else
		result = standard_planner(parse, query_string, cursorOptions,
								  boundParams);

	if (filter_select_only && parse->commandType != CMD_SELECT)
		return result;

	if (statement_cost_limit > 0.0 &&
		result->planTree->total_cost > statement_cost_limit)
		ereport(ERROR,
				(errcode(ERRCODE_STATEMENT_TOO_COMPLEX),
				 errmsg("plan cost limit exceeded"),
				 errhint("The plan for your query shows that it would probably "
						 "have an excessive run time. This may be due to a "
						 "logic error in the SQL, or it may be just a very "
						 "costly query. Rewrite your query or increase the "
						 "configuration parameter "
						 "\"plan_filter.statement_cost_limit\".")));

	return result;
}

/*
 * ExecutorStart hook: charge each execution's plan cost against the
 * transaction's budget.
 *
 * Accounting happens here rather than in the planner hook so that every
 * execution is counted: repeated EXECUTE of a prepared statement stops
 * invoking the planner once a generic plan is cached, which would let a
 * PREPARE/EXECUTE loop evade a plan-time accumulator entirely.
 *
 * The check runs before chaining to the rest of ExecutorStart, so a
 * statement that busts the budget never begins executing.
 */
static void
xact_limit_func(QueryDesc *queryDesc, int eflags)
{
	/*
	 * Plain EXPLAIN neither consumes budget nor is refused by an exhausted
	 * one; it stays usable for inspecting plans.  Parallel workers are
	 * excluded because the leader already counted the plan.
	 */
	if (!(eflags & EXEC_FLAG_EXPLAIN_ONLY) &&
		!IsParallelWorker() &&
		!(filter_select_only && queryDesc->operation != CMD_SELECT))
	{
		xact_cost_total += queryDesc->plannedstmt->planTree->total_cost;

		if (transaction_cost_limit > 0.0 &&
			xact_cost_total > transaction_cost_limit)
			ereport(ERROR,
					(errcode(ERRCODE_STATEMENT_TOO_COMPLEX),
					 errmsg("transaction cost limit exceeded"),
					 errhint("The combined plan cost of the statements "
							 "executed so far in this transaction exceeds the "
							 "configured limit. End the transaction, or "
							 "increase the configuration parameter "
							 "\"plan_filter.transaction_cost_limit\".")));
	}

	if (prev_ExecutorStart_hook)
		(*prev_ExecutorStart_hook) (queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);
}

/*
 * Zero the transaction cost accumulator at end of transaction.  These
 * callbacks may not raise errors; a plain assignment is safe.  There is
 * deliberately no subtransaction callback: ROLLBACK TO SAVEPOINT does not
 * refund cost already charged, since the limit guards resource
 * consumption and the work was attempted.
 */
static void
plan_filter_xact_callback(XactEvent event, void *arg)
{
	switch (event)
	{
		case XACT_EVENT_COMMIT:
		case XACT_EVENT_PARALLEL_COMMIT:
		case XACT_EVENT_ABORT:
		case XACT_EVENT_PARALLEL_ABORT:
		case XACT_EVENT_PREPARE:
			xact_cost_total = 0.0;
			break;
		default:
			break;
	}
}
