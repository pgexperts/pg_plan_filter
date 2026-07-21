---
id: 0004
title: Transaction cost limit accumulates at execution time, without savepoint refunds
date: 2026-07-21
status: Accepted
summary: transaction_cost_limit charges each execution's plan cost in an ExecutorStart hook, resets via xact callback, and never refunds rolled-back subtransactions.
---

# 0004. Transaction cost limit accumulates at execution time, without savepoint refunds

## Context

Issue #4 (from PostgREST) asks for `plan_filter.transaction_cost_limit`:
a cap on the *sum* of statement costs within one transaction, so a batch
endpoint cannot smuggle unbounded work through many individually-cheap
statements, with the error mapped to HTTP 429 at the application edge.
The module was purely stateless per statement; this feature needs
cross-statement state and a decision about *when* cost is charged.

## Decision

Charge cost per **execution** in a new `ExecutorStart` hook: add the
finished plan's `total_cost` to a static per-backend accumulator, error
(same SQLSTATE 54001 as the statement limit, distinct message) when a
non-zero limit is exceeded, before the statement begins executing.  The
accumulator always accrues, even with the limit unset, so `SET LOCAL`
can disable the check but not erase history.  It is zeroed by a
`RegisterXactCallback` callback on commit, abort, and prepare (including
the parallel variants).  Skipped entirely: `EXEC_FLAG_EXPLAIN_ONLY`
(plain `EXPLAIN` stays free and usable), parallel workers (the leader
already charged the plan), and non-SELECT when `filter_select_only` is
on (mirrors the statement check).  `ROLLBACK TO SAVEPOINT` does not
refund cost.  The existing per-statement planner-hook check is
untouched.

## Alternatives considered

- **Accumulate in the existing planner hook** — smallest diff, but
  defeated by the exact threat the feature targets: repeated `EXECUTE`
  of a prepared statement stops invoking the planner once a generic
  plan is cached, so a PREPARE/EXECUTE loop escapes a plan-time
  accumulator.  It also charges plain `EXPLAIN`.  The regression suite
  contains a `force_generic_plan` test that fails under this design.
- **Move the per-statement check to ExecutorStart too** — one uniform
  accounting point, but a silent behavior change to the existing
  feature (plain `EXPLAIN` of an over-limit query is deliberately
  blocked by the statement filter today, and its documented escape
  hatch depends on that).
- **Refund savepoint rollbacks** — more "correct" as bookkeeping, but
  the limit guards attempted resource consumption (the work was done),
  and refunds would require tracking a subtransaction cost stack for a
  case the security use case does not want refunded anyway.

## Consequences

*Careless* batch clients are bounded even through prepared-statement
loops.  A *hostile* client is not: the charged value is the planner's
estimate, computed from cost GUCs (`seq_page_cost`, `cpu_tuple_cost`,
the parallelism costs) that are all `USERSET`, so any role that can run
`SET` can drive its own estimated costs to zero and evade the limit
entirely.  This bypass is inherent to plan-cost-based limiting — it
cannot be closed from inside the hook without re-planning under pinned
coefficients — and it is shared with `statement_cost_limit`.  The
feature is therefore a cooperative resource-shaping guard, not a
security boundary against an adversary that controls its own session;
the README says so, and the batch-throttling use case must keep it
behind an application-level limit.  The `PGC_SUSET` context on the limit
GUCs stops a user *raising the limit* but is not what makes the feature
safe against a hostile role, and must not be mistaken for it.

The module now keeps per-backend mutable state and registers a
transaction callback it never unregisters (harmless: the library cannot
be unloaded since PG 15).  Costs of statements that fail mid-execution
remain charged, which is the intended semantics but can surprise: a
transaction that keeps erroring and retrying under savepoints burns
budget with every attempt.  Utility statements that run without a plan
(most DDL) are not counted; those that wrap a query (`CREATE TABLE ...
AS`, `COPY (SELECT ...)`) execute a plan and are counted — the same
statements the per-statement limit has always seen.
