#!/bin/bash
# Build an openfx-gmic checkout (Linux, CMake) and run the CI tests on it, recording each test's
# outcome instead of stopping at the first failure: for the G'MIC develop job (#226), which runs it
# on a checkout with the pinned G'MIC release and on one with the develop sources
# (gmic_develop_sources.sh), and compares the results (gmic_develop_report.py).
# The tests are those of ci.yml's linux job (keep them in sync), with the same options.
# Usage: gmic_develop_tests.sh <openfx-gmic checkout> <results dir> <expected number of plug-ins>
# The results dir gets one <test>.log per test, status.tsv (<test><tab><exit status>), the
# describe host's log (describe-host.log: PLUGIN, GROUPING, RENDERINFO lines), the plug-in list
# (plugins.txt), the host's output of each failed render (render-logs/<id>.log) and the standard
# library of the build (gmic_stdlib_community.h); the renders stay in <checkout>/render-all, for
# gmic_develop_report.py's comparison of the outputs. The script fails only when the build fails;
# the tests after the describe test are skipped without its plug-in list.
set -uo pipefail

TREE=$(cd "$1" && pwd)
mkdir -p "$2"
RESULTS=$(cd "$2" && pwd)
COUNT=$3
: > "$RESULTS/status.tsv"

# run_test <name> <command>...: runs the command in the checkout, logs its output
run_test() {
    local name=$1 status=0
    shift
    echo "::group::$name"
    (cd "$TREE" && "$@") > "$RESULTS/$name.log" 2>&1 || status=$?
    tail -n 40 "$RESULTS/$name.log"
    echo "::endgroup::"
    printf '%s\t%s\n' "$name" "$status" >> "$RESULTS/status.tsv"
    echo "$name: exit status $status"
    return "$status"
}

# shared zlib and libpng (GMIC_STATIC_DEPS=OFF): the bundle is tested, not shipped
# shellcheck disable=SC2016
run_test build sh -c 'cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOFX_CXX_STANDARD=17 \
    -DGMIC_DOWNLOAD_DIR="$PWD" -DGMIC_STATIC_DEPS=OFF -DGMIC_BUILD_TESTS=ON &&
    cmake --build build -j4 && cmake --install build --prefix install' || exit 1
cp "$TREE/build/gmic/gmic_stdlib_community.h" "$RESULTS/"
stdlib=build/gmic/gmic_stdlib_community.h
plugins=install/OFX/Plugins

run_test describe env DESCRIBE_LOG="$RESULTS/describe-host.log" PLUGIN_LIST="$RESULTS/plugins.txt" \
    .github/workflows/describe_plugins.sh "$plugins" "$COUNT"
[ -s "$RESULTS/plugins.txt" ] || { echo "No plug-in list: the other tests are skipped"; exit 0; }
run_test fft_smoke .github/workflows/fft_smoke.sh build/gmic
run_test render env OFX_GMIC_MULTIRES=1 python3 .github/workflows/render_plugins.py --host build \
    --plugins "$plugins" --list "$RESULTS/plugins.txt" --stdlib "$stdlib" --subset all --jobs 2 \
    --work "$TREE/render-all"
# the host's output for each failed render (FAIL <id>: <problem>)
mkdir -p "$RESULTS/render-logs"
sed -n -e 's/^FAIL eu\.gmic\.\([A-Za-z0-9_]*\): .*/\1/p' "$RESULTS/render.log" | while read -r id; do
    cp "$TREE/render-all/$id/hostDemo.log" "$RESULTS/render-logs/$id.log" 2> /dev/null || true
done
run_test user_file python3 .github/workflows/user_filters_test.py --host build --plugins "$plugins" \
    --stdlib "$stdlib" --work "$TREE/render-user-file"
run_test update_file python3 .github/workflows/user_filters_test.py --host build --plugins "$plugins" \
    --update-file --runs 1 --work "$TREE/render-update-file"
run_test user_source python3 .github/workflows/user_filters_test.py --host build --plugins "$plugins" \
    --user-source --runs 1 --work "$TREE/render-user-source"
exit 0
