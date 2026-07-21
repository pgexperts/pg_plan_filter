Plan Filter Module for PostgreSQL
=================================

[![CI](https://github.com/pgexperts/pg_plan_filter/actions/workflows/ci.yml/badge.svg)](https://github.com/pgexperts/pg_plan_filter/actions/workflows/ci.yml)

This loadable module will test statements against specific configured criteria
before execution, raising an error if the criteria are violated. This 
allows administrators to prevent execution of certain queries on 
production databases.

Two criteria are supported: the estimated cost of an individual statement's
plan, and the combined estimated cost of all the statements in a transaction.
`pg_plan_filter` could be extended to support other kinds of filters.

The module is loaded either via the `LOAD` statement or the 
`shared_preload_libraries` setting in the `postgresql.conf` file. The latter 
mechanism is strongly recommended - `LOAD` is only for use during development 
or testing.

Also, the `statement_cost_limit` must be specified. The default limit is 0, 
which means do not apply any filter. So a typical pair of settings in the 
`postgresql.conf` file might be:

    shared_preload_libraries = 'plan_filter'
    plan_filter.statement_cost_limit = 100000.0

`filter_select_only` limits filtering to SELECT statements only. The default is false.

    plan_filter.filter_select_only = true

turns it on.
Be aware that SELECT != READONLY, since SELECT statements might also modify data.

`transaction_cost_limit` limits the *combined* estimated cost of the
statements executed within a single transaction, raising the same error
class once the running total would be exceeded.  This is aimed at batch
interfaces: many individually-cheap statements can no longer add up to
unbounded work.  The default of 0 turns it off.

    plan_filter.transaction_cost_limit = 500000.0

Details of the accounting:

- Cost is charged per *execution*, so repeatedly executing a prepared
  statement counts every execution, even after PostgreSQL switches to a
  cached generic plan.
- Plain `EXPLAIN` neither consumes budget nor is refused once the budget
  is exhausted, so plans remain inspectable.
- `ROLLBACK TO SAVEPOINT` does not refund cost already charged: the limit
  guards attempted resource consumption, not net effect.
- Statements that execute without a plan — most DDL, such as `CREATE
  INDEX` — are not counted.  Utility statements that *wrap* a query,
  however, do execute a plan and are counted: `CREATE TABLE ... AS`,
  `CREATE MATERIALIZED VIEW`, `COPY (SELECT ...) TO`.  This mirrors
  `statement_cost_limit`, which sees the same statements.
- When `filter_select_only` is on, it applies to this limit as well.

When this module is running with a non-zero `statement_cost_limit`, it 
will also prevent `EXPLAIN` on expensive queries. The solution would be 
to `set statement_cost_limit` temporarily to 0 and then run the `EXPLAIN`, 
like this:

    BEGIN;
    SET LOCAL plan_filter.statement_cost_limit = 0;
    EXPLAIN select ....;
    COMMIT;

As with other settings, `plan_filter.statement_cost_limit` can be set 
per user to override the default and whatever is set in `postgresql.conf`. 
To do this, run something like:

    ALTER USER can_run_expensive SET plan_filter.statement_cost_limit = 0;
    ALTER USER only_cheap_queries SET plan_filter.statement_cost_limit = 10000;

Building and Installing
-----------------------

No configuration is required. Building and installing can be achieved
like this:

    export PATH=/path/to/pgconfig/directory:$PATH
    make && make install
    
As `pg_plan_filter` is a loadable module rather than an Extension, it cannot be
installed using PGXN or other extension-management tools.

This module supports all PostgreSQL major versions with upstream support,
currently 14 through 18, and requires at least 14 to build.  Each supported
version is exercised in CI on every pull request and every push to master.

Testing
-------

With the module built and installed, and a server from the same build
running, the regression suite can be run with:

    make installcheck

Standard libpq environment variables (`PGHOST`, `PGPORT`, and so on) select
the server to test against.  The test session loads the module with `LOAD`,
so nothing needs to be added to `shared_preload_libraries` first.

Warnings
--------

Both limits cancel plans based on their estimated cost.  The PostgreSQL
planner can and does return cost estimates which are unrelated to the actual
query execution time.  As such, you should be prepared for "false positive"
cancellations if you use `pg_plan_filter`, and you should set the limits
generously.

More importantly, the estimate these limits compare against is computed from
the planner cost settings — `seq_page_cost`, `cpu_tuple_cost`, the
parallelism costs, and so on — and every one of those is `USERSET`: any role
that can run `SET` can lower them, even to zero, and so deflate the estimated
cost of its own statements below any limit.  The limit GUCs themselves are
superuser-only and cannot be raised by an ordinary user, but that does not
close this gap, because the attacker changes the number being compared rather
than the limit.  Treat `pg_plan_filter` as a cooperative guard against
accidental or careless load — a misconfigured ORM, a runaway report — not as
a hard security boundary against a hostile role that controls its own
session.  For the batch-throttling use case in particular, keep it as one
layer behind an application-level limit rather than the sole defense.


Credits
-------

This module is primarily the work of Andrew Dunstan, with support from the 
PostgreSQL Experts, Inc. staff.

Development of the module was sponsored by [Twitch.TV](http://www.twitch.tv). 
