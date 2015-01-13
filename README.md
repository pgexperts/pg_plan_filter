Plan Filter Module for PostgreSQL
=================================

This module will filter statements accoriding to certain configuration 
criteria and if the criteria are not met raise an exception instead of 
running the statements.

Currently, to only criterion provided for is the maximum allowed estimated 
cost of the statement plan.

The module is loaded either via the `LOAD` statement or the 
`shared_preload_libraries` setting in the `postgresql.conf` file. The latter 
mechanism is strongly recommended - `LOAD` is only for use during development 
or testing.

Also, the `statement_cost_limit` must be specified. The default limit is 0, 
which means do not apply any filter. So a typical pair of settings in the 
`postgresql.conf` file might be:

    shared_preload_libraries = 'plan_filter'
    plan_filter.statement_cost_limit = 100000.0

If you're using this with a version of PostgreSQL prior to 9.2, you will 
need also to have a line like this before the above lines:

    custom_variable_classes = 'plan_filter'

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
