#!/bin/bash
# Runs openfx-gmic's Natron tests (see README.md): headless_*.py with NatronRenderer -t, test_*.py
# (GUI) with Natron --test.
# Usage: run_tests.sh [test.py ...]   (default: every headless_*.py and test_*.py next to this script)
# Environment:
#   NATRON_BIN_DIR       the directory of the Natron and NatronRenderer launchers (required)
#   GMIC_PLUGIN_DIR      the directory holding GMIC.ofx.bundle (required)
#   OFX_PLUGIN_PATH      more plug-ins, e.g. openfx-misc for its Constant generator (optional)
#   NATRON_COMMIT        the Natron commit whose Tests/gui helpers are fetched (default RB-2.6)
#   NATRON_TEST_WRAPPER  a command to run Natron and NatronRenderer with, e.g. a debugger script (optional)
#   OUT                  the work directory (default ./natron-tests): logs, screenshots, caches
#   TEST_TIMEOUT         seconds per test (default 600)
# Exit status: 1 if a test failed, 77 if Natron is missing or every test was skipped, else 0.
set -u
here=$(cd "$(dirname "$0")" && pwd)
out=${OUT:-$PWD/natron-tests}
tmo=${TEST_TIMEOUT:-600}
natron=${NATRON_BIN_DIR:-}/Natron
renderer=${NATRON_BIN_DIR:-}/NatronRenderer
if [ -z "${NATRON_BIN_DIR:-}" ] || [ ! -x "$renderer" ]; then
  echo "SKIP: no NatronRenderer in NATRON_BIN_DIR (${NATRON_BIN_DIR:-unset})"
  exit 77
fi
[ -d "${GMIC_PLUGIN_DIR:-}/GMIC.ofx.bundle" ] || { echo "FAIL: no GMIC.ofx.bundle in GMIC_PLUGIN_DIR (${GMIC_PLUGIN_DIR:-unset})"; exit 1; }
plugin_path=$GMIC_PLUGIN_DIR${OFX_PLUGIN_PATH:+:$OFX_PLUGIN_PATH}
# shellcheck disable=SC2206  # a command and its arguments
wrapper=(${NATRON_TEST_WRAPPER:-})

# the tests and Natron's helpers in one directory, which Natron puts on sys.path
scripts=$out/scripts
rm -rf "$scripts" && mkdir -p "$scripts" "$out/empty-natron-path" || exit 1
cp "$here"/*.py "$here"/*.gmic "$scripts/"
for f in natrontest.py natronheadless.py; do
  curl -fsSL --retry 3 -o "$scripts/$f" \
    "https://raw.githubusercontent.com/NatronGitHub/Natron/${NATRON_COMMIT:-RB-2.6}/Tests/gui/$f" ||
    { echo "FAIL: cannot fetch Tests/gui/$f of Natron ${NATRON_COMMIT:-RB-2.6}"; exit 1; }
done

[ $# -gt 0 ] || set -- "$here"/headless_*.py "$here"/test_*.py
gui_display=1
case "$(uname -s)" in
  Linux) [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] || gui_display=0 ;;
esac
pass=0 fail=0 skip=0 summary=""
for t in "$@"; do
  name=$(basename "$t" .py)
  o=$out/$name
  rm -rf "$o" && mkdir -p "$o/home/.config/gmic"
  # no user filter file but the test's own (the user's would change the plug-ins)
  [ -f "$here/$name.gmic" ] && cp "$here/$name.gmic" "$o/home/.config/gmic/gmic_ofx.gmic"
  # the OpenFX plug-in cache is in the disk cache: shared, except with a user filter file
  cache=$out/cache
  [ -f "$here/$name.gmic" ] && cache=$o/cache
  start=$(date +%s)
  case "$name" in
    headless_*) cmd=("$renderer" -t "$scripts/$name.py") ;;
    *)
      if [ $gui_display = 0 ] || [ ! -x "$natron" ]; then
        cmd=()
      else
        cmd=("$natron" --test "$scripts/$name.py")
      fi ;;
  esac
  if [ ${#cmd[@]} = 0 ]; then
    rc=77; echo "no display or no Natron GUI: skipped" > "$o/log.txt"
  else
    ( cd "$o" && HOME="$o/home" OFX_PLUGIN_PATH="$plugin_path" NATRON_PLUGIN_PATH="$out/empty-natron-path" \
        NATRON_DISK_CACHE_PATH="$cache" GMIC_TEST_OUT="$o" NATRON_GUI_TESTS=1 NATRON_TEST_TIMEOUT="$tmo" \
        timeout -s TERM -k 60 $((tmo + 60)) ${wrapper[@]+"${wrapper[@]}"} "${cmd[@]}" < /dev/null > "$o/log.txt" 2>&1 )
    rc=$?
  fi
  case $rc in
    0) r=PASS; pass=$((pass + 1)) ;;
    77) r=SKIP; skip=$((skip + 1)) ;;
    *) r="FAIL (exit status $rc)"; fail=$((fail + 1)) ;;
  esac
  line="$name: $r, $(($(date +%s) - start)) s"
  echo "$line"
  summary="$summary$line"$'\n'
  case $rc in
    0) ;;
    77) grep -m 3 "SKIP\|skip" "$o/log.txt" | sed 's/^/    /' ;;
    *) tail -n 40 "$o/log.txt" | sed 's/^/    /' ;;
  esac
done
echo "--- $pass passed, $fail failed, $skip skipped (logs and screenshots in $out)"
printf '%s' "$summary" > "$out/summary.txt"
[ $fail = 0 ] || exit 1
[ $pass -gt 0 ] || exit 77
exit 0
