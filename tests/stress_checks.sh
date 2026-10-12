#!/bin/sh
# Runs the plugin checks concurrently from one build directory: JOBS parallel loops of RUNS runs of --check-render and
# --check-state each (increment 0020: every run keeps its settings in a folder of its own, so none can disturb another).
# Prints the pass count per check and exits non-zero when any run failed.
#   tests/stress_checks.sh [path/to/MdPluginCheck] [jobs] [runs]
# Needs MD_OS_SYX (the OS file); MD_KITS_SYX adds the kit cases of --check-state.
bin=${1:-build/src/plugin/MdPluginCheck_artefacts/Release/MdPluginCheck}
jobs=${2:-8}
runs=${3:-3}
: "${MD_OS_SYX:?set MD_OS_SYX to the Machinedrum OS file}"
out=$(mktemp -d "${TMPDIR:-/tmp}/md-stress.XXXXXX") || exit 1
trap 'rm -rf "$out"' EXIT

loop() {   # loop <check> <job>
    for r in $(seq "$runs"); do
        if "$bin" "--check-$1" > "$out/$1-$2-$r.log" 2>&1; then
            echo ok
        else
            echo "FAIL $1 job $2 run $r (exit $?); last lines:" >&2
            tail -n 3 "$out/$1-$2-$r.log" >&2
            echo fail
        fi
    done > "$out/$1-$2.result"
}

status=0
for check in render state; do
    for j in $(seq "$jobs"); do loop "$check" "$j" & done
    wait
    passed=$(cat "$out/$check"-*.result | grep -c '^ok$')
    total=$((jobs * runs))
    echo "--check-$check: $passed of $total passed"
    [ "$passed" -eq "$total" ] || status=1
done
exit $status
