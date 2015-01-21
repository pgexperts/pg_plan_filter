
-- set up a big table if it's not already there, we're using 100m rows here

do $$

begin

    perform 1 from pg_class where relname = 'bigtest';

    if not found 
    then

        create table bigtest as 
               select x , y 
               from generate_series(1,10000) x, 
                    generate_series(10001,20000) y;
        alter table bigtest add primary key (x,y);
        analyse bigtest;

    end if;

end;

$$;

select coalesce((select setting::boolean from pg_settings where name = 'plan_filter.module_loaded'),false) as have_plan_filter_module;

LOAD 'plan_filter';
SET plan_filter.statement_cost_limit = 100000;

select coalesce((select setting::boolean from pg_settings where name = 'plan_filter.module_loaded'),false) as have_plan_filter_module;

-- should fail

explain select * from bigtest;

select * from bigtest;


-- temporary override
begin;
set local plan_filter.statement_cost_limit = 0;
explain select * from bigtest;
commit;

-- back to limited

explain select * from bigtest;

-- override by user
-- test probably runs best with trust authentication

drop role if exists testuser;

create user testuser superuser; -- so they can run LOAD
alter user testuser set plan_filter.statement_cost_limit = 100000;

\c - testuser

LOAD 'plan_filter';

-- should fail without them setting a limit in this session

explain select * from bigtest;

select * from bigtest;

-- override the limit

SET plan_filter.statement_cost_limit = 0;

explain select * from bigtest;
