---
id: 0002
title: Behavioral pg_regress suite with version-independent expected output
date: 2026-07-21
status: Accepted
summary: Testing uses a standard pg_regress suite whose output contains no cost numbers or plans, so one expected file serves every supported major on every platform.
---

# 0002. Behavioral pg_regress suite with version-independent expected output

## Context

The only test was `testlimit.sql`, a manual psql script: it built a
100-million-row table, reconnected as a superuser test role with `\c`,
and had no expected output — a human eyeballed the results.  Nothing
about it could run under CI, and it had no way to fail automatically.
The obvious hazard in testing a cost-based filter is that cost estimates
differ across majors, platforms, and planner changes, so naive expected
files (anything containing `EXPLAIN` costs) would need per-version
variants and constant re-blessing.

## Decision

Test through the standard PGXS `REGRESS` machinery (`sql/plan_filter.sql`
+ `expected/plan_filter.out`, run via `make installcheck`) and keep every
assertion behavioral: statements either succeed or raise the module's
error.  Cost numbers never appear in the output — the one `EXPLAIN` in
the suite uses `COSTS OFF` and exists only to prove EXPLAIN itself is
filtered.  The threshold trick that makes this work: a 10,000-row table
with `statement_cost_limit = 1` sits so far from the boundary on both
sides (the filtered statements plan at a total cost around 190, trivial
statements around 0.01) that no realistic planner change can flip an
assertion.  One expected file covers all supported majors on all
platforms.  The suite pins both GUCs to their defaults immediately after
`LOAD`, so it also passes against a cluster whose configuration preloads
the module with a limit already set.  It exercises: limit off, limit
exceeded, cheap statement passing, EXPLAIN blocked, `SET LOCAL` escape
hatch and its expiry, `filter_select_only` in both positions, and the
superuser-only (`PGC_SUSET`) enforcement of both GUCs.  `testlimit.sql`
is deleted, not kept alongside.

## Alternatives considered

- **Keep `testlimit.sql` as a manual supplement** — an unrunnable
  near-duplicate of the real suite would only drift; the regress suite
  covers everything it did except sheer table size, which tested the
  planner's arithmetic rather than this module's logic.
- **Per-version expected files (`plan_filter_1.out`, ...)** — pg_regress
  supports alternates, but they are a standing maintenance tax and this
  design makes them unnecessary by construction.
- **TAP tests (`PROVE_TESTS`)** — strictly more powerful (could test
  `shared_preload_libraries` mode), but needs a Perl toolchain and
  `PostgreSQL::Test` modules that pgdg packages don't ship cleanly for
  extensions; pg_regress is the conventional floor for a module this
  small.

## Consequences

`make installcheck` is a real pass/fail gate usable locally and in CI,
against any running server of any supported version.  The suite runs in
about a second.  What is *not* covered: `shared_preload_libraries`
loading (the suite uses per-session `LOAD`; the code path difference is
negligible for a planner hook) and the interaction of reserved GUC
prefixes with preexisting placeholder settings.  Expected-file updates
are only ever needed if the module's own messages change.
