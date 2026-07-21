--
-- Behavioral tests for plan_filter.  Cost estimates never appear in the
-- output (no EXPLAIN with costs on), so the expected file is stable across
-- PostgreSQL versions and platforms.
--
LOAD 'plan_filter';
SHOW plan_filter.module_loaded;

-- Pin both GUCs before doing anything plannable, so the suite also passes
-- on a cluster whose configuration preloads the module with a limit set.
SET plan_filter.statement_cost_limit = 0;
SET plan_filter.filter_select_only = false;

CREATE TABLE plan_filter_test AS
    SELECT g AS x, g % 100 AS y FROM generate_series(1, 10000) g;
ANALYZE plan_filter_test;

-- a limit of zero means no filtering
SET plan_filter.statement_cost_limit = 0;
SELECT count(*) FROM plan_filter_test;

-- a tiny limit blocks the sequential scan
SET plan_filter.statement_cost_limit = 1;
SELECT count(*) FROM plan_filter_test;

-- cheap statements still pass
SELECT 1 AS cheap;

-- EXPLAIN plans the query, so it is blocked as well
EXPLAIN (COSTS OFF) SELECT count(*) FROM plan_filter_test;

-- SET LOCAL provides a temporary escape hatch
BEGIN;
SET LOCAL plan_filter.statement_cost_limit = 0;
SELECT count(*) FROM plan_filter_test;
COMMIT;

-- and the limit comes back after commit
SELECT count(*) FROM plan_filter_test;

-- filter_select_only exempts non-SELECT statements from the limit
SET plan_filter.filter_select_only = true;
BEGIN;
UPDATE plan_filter_test SET y = y + 1;
ROLLBACK;

-- but SELECT is still filtered
SELECT count(*) FROM plan_filter_test;

-- with filter_select_only off, expensive DML is blocked too
SET plan_filter.filter_select_only = false;
BEGIN;
UPDATE plan_filter_test SET y = y + 1;
ROLLBACK;

-- both GUCs are superuser-only: an unprivileged role cannot lift the limit
CREATE ROLE regress_plan_filter_user;
SET ROLE regress_plan_filter_user;
SET plan_filter.statement_cost_limit = 0;
SET plan_filter.filter_select_only = true;
RESET ROLE;
DROP ROLE regress_plan_filter_user;

DROP TABLE plan_filter_test;
