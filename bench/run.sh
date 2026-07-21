#!/bin/sh
# Measures the planner-hook overhead of plan_filter: pgbench select-only
# throughput with the module absent vs. loaded with a cost limit high enough
# that the check runs but never fires.  Numbers from shared CI runners are
# too noisy to gate on, so this is for trend-tracking only.
#
# Environment:
#   PGBIN           bin directory of the PostgreSQL to use (default: pg_config)
#   BENCH_SECONDS   per-configuration pgbench duration (default: 10)
#   PLAN_FILTER_DIR if set, added to dynamic_library_path so an uninstalled
#                   build directory can be benchmarked
set -eu

PGBIN=${PGBIN:-$(pg_config --bindir)}
DATADIR=$(mktemp -d)
export PGHOST="$DATADIR" PGPORT=55433 PGDATABASE=postgres

# On failure, dump the server log before it is removed with the datadir.
# The INT/TERM traps exist because POSIX shells do not run the EXIT trap
# when killed by an unhandled signal (Ctrl-C, CI job cancellation).
cleanup() {
    status=$?
    if [ "$status" -ne 0 ] && [ -f "$DATADIR/log" ]; then
        echo "--- server log tail ---" >&2
        tail -20 "$DATADIR/log" >&2
    fi
    "$PGBIN/pg_ctl" -D "$DATADIR/data" stop -m immediate >/dev/null 2>&1 || true
    rm -rf "$DATADIR"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

"$PGBIN/initdb" -D "$DATADIR/data" -A trust >/dev/null
{
    echo "port = $PGPORT"
    echo "listen_addresses = ''"
    echo "unix_socket_directories = '$DATADIR'"
    echo "fsync = off"
    if [ -n "${PLAN_FILTER_DIR:-}" ]; then
        echo "dynamic_library_path = '$PLAN_FILTER_DIR:\$libdir'"
    fi
} >> "$DATADIR/data/postgresql.conf"
"$PGBIN/pg_ctl" -D "$DATADIR/data" -l "$DATADIR/log" start >/dev/null

"$PGBIN/createdb" bench
"$PGBIN/pgbench" -i -s 1 -q bench >/dev/null

# Capture pgbench's output in a variable so its exit status is not masked
# by a pipeline; fail if the expected tps line never appears.
run() {
    out=$("$PGBIN/pgbench" -S -n -c 4 -j 4 -T "${BENCH_SECONDS:-10}" bench)
    printf '%s\n' "$out" |
        awk -v label="$1" '/^tps/ {printf "%-28s %s tps\n", label, $3; found = 1; exit}
                           END {if (!found) exit 1}'
}

run "baseline (no module)"

"$PGBIN/psql" -q -d bench \
    -c "ALTER DATABASE bench SET session_preload_libraries = 'plan_filter'" \
    -c "ALTER DATABASE bench SET plan_filter.statement_cost_limit = 1e9"

run "plan_filter (limit unhit)"
