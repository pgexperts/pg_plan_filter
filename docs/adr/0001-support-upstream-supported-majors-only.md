---
id: 0001
title: Support only upstream-supported PostgreSQL majors (currently 14-18)
date: 2026-07-21
status: Accepted
summary: The module targets the PostgreSQL majors with upstream support, enforces a hard floor of 14 at compile time, and drops all pre-13 compatibility scaffolding.
---

# 0001. Support only upstream-supported PostgreSQL majors (currently 14-18)

## Context

The module was written for PostgreSQL 9.x and last touched for
compatibility when PostgreSQL 13 changed the planner-hook signature (it
gained `const char *query_string`).  The source carried `#if`-macro
scaffolding (`PLANNER_HOOK_PARAMS` / `PLANNER_HOOK_ARGS`) so one body
could compile against both pre-13 and 13+ signatures, plus a `_PG_fini`
unload callback.  As of July 2026 the upstream-supported majors are 14
through 18; everything the macros existed for is end-of-life.  PostgreSQL
15 removed library unloading entirely (so `_PG_fini` is dead code) and
added `MarkGUCPrefixReserved`, which turns a misspelled
`plan_filter.*` setting from a silently-ignored placeholder into an error
— a meaningful safety property for a module whose whole job is being a
guard rail.

## Decision

Track the upstream support window and nothing older.  Concretely: a
compile-time `#error` below `PG_VERSION_NUM 140000`; the 13+ planner-hook
signature written directly with the compatibility macros deleted;
`_PG_fini` removed; the `plan_filter.` GUC prefix reserved
(`MarkGUCPrefixReserved` on 15+, `EmitWarningsOnPlaceholders` on 14); and
`PG_MODULE_MAGIC_EXT` with a module name and version on 18+, falling back
to plain `PG_MODULE_MAGIC` earlier.

## Alternatives considered

- **Keep the compatibility macros and let pre-13 keep building** — the
  macros cost little, but untested-is-unsupported: nothing older than 14
  is exercised by CI, and advertising support that is never verified is
  how version bit-rot went unnoticed here for years in the first place.
- **Set the floor at 13** — the code is identical under 13, but 13 went
  end-of-life in November 2025 and would be one more untested
  configuration; users on 13 can build from an earlier checkout.
- **Version-sniff with `#ifdef` around every changed API instead of a
  floor** — appropriate for extensions that must span many majors;
  pointless here since the 14-18 API surface this module touches is
  uniform except for the two guarded calls noted above.

## Consequences

The source is a single straight-line file with two small version guards
(GUC-prefix reservation, module magic).  Each autumn's new major and
EOL is a policy update, not an archaeology project: bump the CI matrix,
raise the floor when the oldest supported major moves.  Users on EOL
versions must build from an older commit — a deliberate cost.  The
GUC-prefix reservation is a minor behavior change on 15+: `SET
plan_filter.typo = ...` now errors instead of being silently accepted.
