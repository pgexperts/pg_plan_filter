# Design: plan_filter.transaction_cost_limit

Implements [issue #4](https://github.com/pgexperts/pg_plan_filter/issues/4):
cap the cumulative estimated plan cost of the statements executed within a
single transaction, so a batch interface cannot smuggle unbounded work
through many individually-cheap statements.  Requested by PostgREST for
mapping to HTTP 429 at the application edge.

## GUC

`plan_filter.transaction_cost_limit` — real, default 0 (feature off),
range 0..DBL_MAX, `PGC_SUSET`, covered by the reserved `plan_filter.`
prefix.  Errors use the same SQLSTATE as the statement limit
(`ERRCODE_STATEMENT_TOO_COMPLEX`, 54001) so one client-side mapping
catches both, with a distinct message: `transaction cost limit exceeded`.

## Accounting model (the load-bearing decision)

Cost is accumulated **per execution, not per plan**, in a new
`ExecutorStart` hook (the existing planner-hook statement check is
untouched).  Rationale: repeated `EXECUTE` of a prepared statement stops
invoking the planner once a generic plan is cached, so plan-time
accounting can be defeated by a `PREPARE` + `EXECUTE` loop — exactly the
misbehaving-batch-client threat the feature exists for.  Every execution
passes through `ExecutorStart`.

The hook, before chaining to `standard_ExecutorStart` (so a doomed
statement never begins executing):

- skips `EXEC_FLAG_EXPLAIN_ONLY` (plain `EXPLAIN` neither consumes budget
  nor is blocked by an exhausted one — it remains the documented way to
  inspect an expensive plan);
- skips `IsParallelWorker()` (leader already counted the plan);
- skips non-SELECT when `filter_select_only` is on (mirrors the statement
  check);
- otherwise **always adds** `plannedstmt->planTree->total_cost` to the
  accumulator, and raises the error only when the limit is non-zero and
  exceeded.  Always-accumulate means `SET LOCAL` can disable the check
  but cannot erase history.

Utility statements never reach the executor and are not counted — the
same statements the per-statement limit has never seen.

## State and reset

One `static double xact_cost_total`, zeroed in a `RegisterXactCallback`
callback on `XACT_EVENT_COMMIT`, `XACT_EVENT_PARALLEL_COMMIT`,
`XACT_EVENT_ABORT`, `XACT_EVENT_PARALLEL_ABORT`, and
`XACT_EVENT_PREPARE` (the callback only assigns a double — safe in that
context).  No subtransaction callback: `ROLLBACK TO SAVEPOINT` does
**not** refund cost.  The limit is a resource-consumption guard; the
work was attempted, and refusing refunds also removes the need to track
a savepoint stack.

## Tests (extend the existing suite; same one-expected-file discipline)

1. Two ~190-cost SELECTs under limit 300 in one transaction: first
   passes, second errors.
2. Reset: a fresh transaction passes again after COMMIT and after
   ROLLBACK of an aborted transaction.
3. No refund: pass, SAVEPOINT, error, `ROLLBACK TO`, next statement
   still errors.
4. Execution accounting discriminator: `plan_cache_mode =
   force_generic_plan`, `PREPARE`, two `EXECUTE`s — the second must
   error (it would not under plan-time accounting).
5. Plain `EXPLAIN` consumes nothing: after one SELECT (~190/300), two
   consecutive `EXPLAIN (COSTS OFF)` both succeed (if EXPLAIN consumed,
   the second would error).
6. SUSET: unprivileged `SET plan_filter.transaction_cost_limit` is
   denied.

The suite's post-`LOAD` pin block also pins the new GUC to 0.

## Documentation

README section for the new GUC (semantics above, spelled out); ADR 0004
recording execution-time accounting with plan-time as the rejected
alternative; ARCHITECTURE.md map/invariants updated.

## Compatibility

`ExecutorStart_hook_type (QueryDesc *, int)`, `RegisterXactCallback`,
`IsParallelWorker()`, and `plan_cache_mode` are uniform across
PostgreSQL 14-18 (PG 18's bool-returning `ExecutorStart` experiment was
reverted before release).  The CI matrix proves each major.
