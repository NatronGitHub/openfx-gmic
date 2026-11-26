#!/bin/bash
# shellcheck disable=SC2016 # (the G'MIC expressions are in single quotes on purpose)
# Checks the local fixes of G'MIC, CImg and gmic-community bugs (patches/*.sh, gmic_hotfixes.gmic)
# with a G'MIC command-line interpreter built from the sources of the build (the patched CImg.h
# and gmic.cpp, and the build's standard library: fft_smoke.sh builds one).
# Usage: gmic_fix_tests.sh <gmic> <gmic_stdlib_community.h> [<command file>]
# Each check runs a repro of an openfx-gmic issue and compares its output with the expected one.
# The filter checks load the hot fixes that the plug-in applies to that standard library
# (hotfix_blocks.py). <command file> (optional) is loaded first, e.g. G'MIC develop's update file
# with an interpreter whose built-in library is another one. Fails if any check fails.
set -uo pipefail
# (absolute: the checks run in the work directory)
abspath() { case $1 in /*) printf '%s\n' "$1";; *) printf '%s\n' "$PWD/$1";; esac; }
GMIC=$(abspath "$1")
STDLIB=$2
EXTRA=${3:+$(abspath "$3")}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
python3 "$HERE/hotfix_blocks.py" "$STDLIB" "$HERE/../../gmic_hotfixes.gmic" "$WORK/hotfixes.gmic" || exit 1
load=()
[ -n "$EXTRA" ] && load=(m "$EXTRA")
load+=(m "$WORK/hotfixes.gmic")
status=0

# check <issue> <description> <expected output> <gmic arguments>...: gmic must succeed, and the
# last line of its output be <expected>, or, for '!~<regex>', be non-empty and not match the
# extended regular expression
check() {
  local issue=$1 what=$2 expected=$3 out rc=0 ok=0
  shift 3
  out=$(cd "$WORK" && timeout 300 "$GMIC" -v -1 "${load[@]}" "$@" 2>&1) || rc=$?
  out=$(printf '%s\n' "$out" | tail -n 1)
  if [ $rc = 0 ]; then
    case $expected in
      '!~'*) [ -n "$out" ] && ! printf '%s\n' "$out" | grep -Eq -- "${expected#!~}" && ok=1;;
      *) [ "$out" = "$expected" ] && ok=1;;
    esac
  else
    out="$out (exit status $rc)"
  fi
  if [ $ok = 1 ]; then
    echo "OK   #$issue $what"
  else
    echo "FAIL #$issue $what: got '$out', expected '$expected'"
    status=1
  fi
}

# the math parser (CImg patches)
check 232 "fusion of '+' with an assignment on the right" "0,2,4,6" 4,1,1,1,"z=1+(y=x*2);y" e '{^}'
check 232 "fusion of '*' with an assignment on the right" "0,0.5,1,1.5" 4,1,1,1,"z=5*(y=x/2);y" e '{^}'
check 232 "fusion of '-' with an assignment on the right" "0,2,4,6" 4,1,1,1,"z=5-(y=x*2);y" e '{^}'
check 232 "fusion of '+' with an assignment on the left" "0,2,4,6" 4,1,1,1,"y=0;(y=x*2)+1;y" e '{^}'
check 232 "fusion of '/' with an assignment on the left" "0,0.5,1,1.5" 4,1,1,1,"z=(q=x/2)/3;q" e '{^}'
check 232 "fusions of temporaries still give the same values" "1,3,5,7" 4,1,1,1,"1+(x*2)" e '{^}'
check 245 "'(V) = expr' keeps the source vector" "3" e '{A=[3,1,2];B=sort(A);(A)=B;size(B)}'
check 245 "'(V) = f(V)' with f() reading V" "2,1,4,3" e '{A=[1,2,3,4];B=[0,1,1,0];(A)=mul(A,B,2);A}'
check 245 "'V = expr' keeps the source vector" "3" e '{A=[3,1,2];B=sort(A);A=B;size(B)}'
check 245 "'V = f(V)' with f() reading V" "2,1,4,3" e '{A=[1,2,3,4];B=[0,1,1,0];A=mul(A,B,2);A}'
check 245 "'X = sort(X)' still sorts in place" "1,2,3" e '{A=[3,1,2];A=sort(A);A}'
check 246 "same() doesn't overwrite a live temporary" "1.8414709848078965" e '{a=1;sin(a)+same(vector36(a),vector36(a))}'
check 244 "hypot() doesn't overflow" "1.414213562373095e+200" e '{hypot(1e200,1e200)}'
check 244 "hypot() doesn't underflow" "1.414213562373095e-200" e '{hypot(1e-200,1e-200)}'

# filters of the standard library and of gmic-community (hot fixes)
check 229 "Zone System (fx_apply_curve)" "ok" 64,64,1,3,"x*4" fx_zonesystem 1,10,1,1,0,255,0 e ok
check 229 "Test Dither Color (negative)" "ok" 64,64,1,3,"x*4" samj_test_Dither_Color 2,0,230,230,150,0,0 e ok
check 229 "CeKoaSa 010 (transfer_colors)" "ok" 64,64,1,3,"x*4" samj_CeKoaSa_010 30,10,0.1,2,10 e ok
check 229 "Chalk It Up with Invert Colors on (negative)" "ok" \
  64,64,1,3,"x*4" samj_chalkitup 5,2.5,1.5,50,1,5,5,1,0,7,0.8,1.9,7,0 e ok
# a constant hue of 90 degrees: more green than red (with the bug, a hue under 1 degree: red)
check 234 "Hair Locks' hues (_hcolor)" "1" 64,64,1,3 _hcolor 90,0.9,0.5,0,1,1,1 s c e '{ia#1>ia#0}'
check 239 "JPEG Encoder's status" "{80}{21}{8}{8}{16}{16}{16}{16}{1}{1}_2{2}_2{3}_2{4}_2{5}_2{6}_2" \
  96,80,1,3,x fx_jpeg_preview 80,21,8,8,16,16,16,16,1,1,2,3,4,5,6 e '${}'
check 240 "Object Animation (fx_zoom)" "ok" \
  128,128,1,3,"[x*2,y*2,(x+y)]" 128,128,1,3 circle. 50%,50%,30,1,255 fx_tk_animateobject 0,0,0,0.5,0.5,400,2,0,0,0,0,0,1,0,1,1 e ok
check 240 "Pixel Denoise, Hybrid Median without parallel processing" "ok" \
  128,128,1,3,"[x*2,y*2,(x+y)]" noise 20 iain_pixel_denoise_p 1,1,11,0,0 e ok
# Mixer [PCA]'s status: one {...} group per value, no value without braces ('...{0}1{8,...')
check 242 "Mixer [PCA]'s status" '!~\}[-0-9.]+\{' \
  128,128,1,3,"[x*2,y*2,(x+y)]" fx_mix_pca 0,0,0,0,0,0,0,0,0,0,0,0,1,-1,0,0,50,50 e '${}'
check 247 "gcd_boxfilter_local_hp with a 1-channel reference" "0" 64,64,1,3,x 64,64,1,1,5 gcd_boxfilter_local_hp.. . rm. e '{isnan(is)}'
check 247 "gcd_boxfilter_local with a 1-channel reference" "0" 64,64,1,3,x 64,64,1,1,5 gcd_boxfilter_local.. . rm. e '{isinf(is)||isnan(is)}'
exit $status
