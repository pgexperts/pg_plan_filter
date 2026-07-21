---
id: 0003
title: GitHub Actions CI on the pgdg apt matrix, with report-only benchmarks
date: 2026-07-21
status: Accepted
summary: CI builds with -Werror and runs installcheck against pgdg packages for every supported major, runs cppcheck as lint, and publishes pgbench numbers as a non-gating report.
---

# 0003. GitHub Actions CI on the pgdg apt matrix, with report-only benchmarks

## Context

The project had no CI; version compatibility was discovered by user bug
report (the PG 13 fix arrived that way).  A cost-filter module is pure
hook-and-GUC code, so the entire risk surface is "does it still compile
and behave against major N" — exactly what a version matrix answers.
Per-project convention, correctness must gate the pipeline while
benchmark numbers from shared runners must not, because noisy-neighbor
CI hardware produces 2x swings on identical code.

## Decision

One GitHub Actions workflow with three job groups, running on push to
master and on pull requests:

- **test** (gating): matrix over PostgreSQL 14-18 from apt.postgresql.org
  on `ubuntu-latest`, via the `apt.postgresql.org.sh` helper that ships
  in the runner image's `postgresql-common`.  Each leg builds with
  `COPT=-Werror`, installs, `initdb`s its own throwaway cluster as the
  runner user (socket in `/tmp`, port 55432), and runs `make
  installcheck`.  On failure it dumps `regression.diffs` and the server
  log.
- **lint** (gating): `cppcheck --enable=warning,portability` against the
  PG 18 server headers.
- **benchmark** (report-only, `continue-on-error`): pgbench select-only
  throughput, module absent vs. loaded with an unreached limit, on PG 18;
  results go to the job summary and an uploaded artifact for
  trend-tracking across commits.

## Alternatives considered

- **Use the Debian-packaged auto-created cluster** (`pg_ctlcluster`) —
  it runs as the `postgres` OS user, so `make installcheck` from the
  runner-owned checkout hits permission problems writing `results/`, and
  its socket lives in a directory owned by the `postgres` user that the
  runner user cannot create sockets in.  A self-initdb'd cluster
  owned by the runner user avoids the whole class of problem and pins
  server and headers to the same pgdg major.
- **Docker `postgres:N` images as services** — the official images ship
  no server headers or pgxs; building inside a container per version
  means maintaining Dockerfiles that duplicate what pgdg packages already
  provide.
- **Third-party setup actions** (`ankane/setup-postgres` and kin) — one
  more supply-chain dependency to pin and audit, replacing two lines of
  apt.
- **Gating on benchmark numbers** — rejected outright: shared-runner
  variance exceeds any plausible regression signal for a module whose
  hot path is one float comparison.

## Consequences

Every pull request and push to master now proves build cleanliness
(warnings are errors) and behavior on all five supported majors in a few
minutes.  Autumn maintenance is editing the matrix list (add the new
major, drop the EOL one) and the hard-coded PG-18 version references in
the lint and benchmark jobs.  The benchmark provides a paper trail rather than protection — a
real perf regression must be caught by a human reading the trend.  The
workflow depends on pgdg publishing packages for new majors promptly,
which it reliably does.
