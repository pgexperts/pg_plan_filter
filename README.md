Plan Filter Module for PostgreSQL
=================================

[![CI](https://github.com/pgexperts/pg_plan_filter/actions/workflows/ci.yml/badge.svg)](https://github.com/pgexperts/pg_plan_filter/actions/workflows/ci.yml)

This loadable module will test statements against specific configured criteria
before execution, raising an error if the criteria are violated. This 
allows administrators to prevent execution of certain queries on 
production databases.

The only criterion currently supported is the maximum allowed estimated 
cost of the statement plan.  However, `pg_plan_filter` could be extended to
support a number of different filters.

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

`statement_cost_limit` will cancel plans based on their estimated cost.  The PostgreSQL 
planner can and does return cost estimates which are unrelated to the actual
query execution time.  As such, you should be prepared for "false positive"
cancellations if you use `pg_plan_filter`, and you should set `statement_cost_limit` 
generously.


Credits
-------

This module is primarily the work of Andrew Dunstan, with support from the 
PostgreSQL Experts, Inc. staff.

Development of the module was sponsored by [Twitch.TV](http://www.twitch.tv). 
