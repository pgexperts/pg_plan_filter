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

#include "optimizer/planner.h"
#include "utils/guc.h"

#define PG13_GTE (PG_VERSION_NUM >= 130000)

#if PG13_GTE
#define PLANNER_HOOK_PARAMS \
	Query *parse, const char *queryString \
, int cursorOptions, ParamListInfo boundParams
#else
#define PLANNER_HOOK_PARAMS \
	Query *parse \
, int cursorOptions, ParamListInfo boundParams
#endif

#if PG13_GTE
#define PLANNER_HOOK_ARGS \
	parse, queryString \
, cursorOptions, boundParams
#else
#define PLANNER_HOOK_ARGS \
	parse \
, cursorOptions, boundParams
#endif


PG_MODULE_MAGIC;

static double statement_cost_limit = 0.0;

static bool module_loaded = false;

static bool filter_select_only = false;

static planner_hook_type prev_planner_hook = NULL;

static PlannedStmt *limit_func(PLANNER_HOOK_PARAMS);

void		_PG_init(void);
void		_PG_fini(void);

/*
 * Module load callback
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

	/* install the hook */
	prev_planner_hook = planner_hook;
	planner_hook = limit_func;

}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	/* Uninstall hook. */
	planner_hook = prev_planner_hook;
	/* reset loaded var */
	module_loaded = false;
}

static PlannedStmt *
limit_func(PLANNER_HOOK_PARAMS)
{
	PlannedStmt *result;

	/* this way we can daisy chain planner hooks if necessary */
	if (prev_planner_hook != NULL)
		result = (*prev_planner_hook) (PLANNER_HOOK_ARGS);
	else
		result = standard_planner(PLANNER_HOOK_ARGS);

    if(filter_select_only && parse->commandType != CMD_SELECT)
		return result;

	if (statement_cost_limit > 0.0 &&
		result->planTree->total_cost > statement_cost_limit)
		ereport(ERROR,
				(errcode(ERRCODE_STATEMENT_TOO_COMPLEX),
				 errmsg("plan cost limit exceeded"),
			  errhint("The plan for your query shows that it would probably "
					  "have an excessive run time. This may be due to a "
					  "logic error in the SQL, or it maybe just a very "
					  "costly query. Rewrite your query or increase the "
					  "configuration parameter "
					  "\"plan_filter.statement_cost_limit\".")));

	return result;
}
