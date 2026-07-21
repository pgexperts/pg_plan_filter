# ARCHITECTURE

`pg_plan_filter` is a single-file PostgreSQL loadable module (not an
extension: no `.control`, no SQL objects) that installs a `planner_hook`
and raises `ERRCODE_STATEMENT_TOO_COMPLEX` when a completed plan's
`total_cost` exceeds `plan_filter.statement_cost_limit`.  Decision
rationale lives in `docs/adr/`; this file is the map and the invariants.

## Map

| Path | Role |
|---|---|
| `plan_filter.c` | Everything: GUC definitions, hook install, the filter |
| `Makefile` | PGXS `MODULE_big` build; `REGRESS = plan_filter` |
| `sql/` + `expected/` | pg_regress suite (`make installcheck`) |
| `bench/run.sh` | Self-contained pgbench overhead harness (report-only) |
| `.github/workflows/ci.yml` | Matrix CI; see ADR 0003 |
| `docs/adr/` | Decision records; index in its README.md |

## Invariants

- **Support window** is exactly the upstream-supported majors (14–18 as
  of 2026); a `#error` enforces the floor.  Autumn maintenance: bump the
  CI matrix, raise the floor when the oldest major EOLs, update the
  hard-coded PG-18 references in the lint/benchmark jobs and the README
  version sentence (ADR 0001, 0003).
- **Hook discipline**: `limit_func` calls exactly one of
  `prev_planner_hook` or `standard_planner`, and checks the cost only
  *after* planning completes, on the finished `PlannedStmt`.  The error
  is thrown, never returned.
- **Both feature GUCs are `PGC_SUSET` on purpose** — an unprivileged user
  must not be able to lift an administrator's limit.  The regression
  suite tests this; do not downgrade the context.
- **`plan_filter.module_loaded`** exists only as a presence signal
  (`PGC_BACKEND`, defined at load time); nothing reads it in C.
- **No `_PG_fini`** — PostgreSQL 15 removed library unloading; do not
  reintroduce it.
- **Expected output must stay version-independent**: no cost numbers, no
  `EXPLAIN` without `COSTS OFF`, no version-varying messages.  One
  expected file serves every supported major on every platform
  (ADR 0002).
- **The suite pins both GUCs immediately after `LOAD`** so it passes on
  clusters that preload the module with a limit configured.  Keep any new
  plannable statement below that pin point out of the file.
- **CI gates on correctness only**; the benchmark job is report-only and
  must stay `continue-on-error` (ADR 0003).

## Landmines

- The `plan_filter.` GUC prefix is reserved at load on 15+
  (`MarkGUCPrefixReserved`): misspelled `SET plan_filter.x` errors.  On
  14 the older call only warns about preexisting placeholders — behavior
  differs by major.
- `EXPLAIN` is filtered too (it plans).  The documented escape is
  `SET LOCAL plan_filter.statement_cost_limit = 0` in a transaction.
- `filter_select_only` exempts by `parse->commandType != CMD_SELECT`;
  a SELECT with data-modifying CTEs is still `CMD_SELECT` and still
  filtered — SELECT ≠ read-only, as the README warns.

## Working on this repo

- Build/test: `make && make install`, then `make installcheck` against a
  running server (libpq env vars select it).
- Testing without install rights: add the build directory to a scratch
  cluster's `dynamic_library_path`; `LOAD 'plan_filter'` finds the built
  library there.
- Full pre-push rehearsal: the CI steps run verbatim in an
  `ubuntu:24.04` container using `apt.postgresql.org.sh`; build with
  `with_llvm=no` if clang is absent.
- New/changed C code must build warning-free (`COPT=-Werror` is the CI
  gate) and follow PostgreSQL backend conventions (tabs, `/* */`
  comments, errors via `ereport`).
