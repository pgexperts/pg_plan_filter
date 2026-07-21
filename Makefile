# plan_filter Makefile

MODULE_big = plan_filter
OBJS = plan_filter.o $(WIN32RES)
PGFILEDESC = "filter statements meeting plan criteria - currently by plan cost"
DOCS         = $(wildcard doc/*.md)
REGRESS = plan_filter

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
