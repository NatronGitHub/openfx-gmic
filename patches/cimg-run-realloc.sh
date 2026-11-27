#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that a fill or eval
# expression whose run() re-allocates the image list fails with an error, instead of writing into
# (and reading) the freed image. Usage: cimg-run-realloc.sh <CImg.h>
# https://github.com/NatronGitHub/openfx-gmic/issues/243, https://github.com/GreycLab/gmic/issues/231
#
# G'MIC's 'eval[...]' and 'fill' run the math parser on images[i], a CImg<T> of the image list,
# and gmic::mp_run() runs the pipeline of run() on that same list. When the pipeline inserts
# enough images, CImgList::insert() moves the images to a larger array and frees the old one, so
# the expression's image (*this in CImg::_fill(), mp.imgout) is freed: its later writes are lost,
# e.g. 100,100,1,1,7 eval[0] "run('1x20');i(0,0)=3" leaves 7 (heap use-after-free). The stdlib's
# 'arrange' (Montage) of 13 or 14 images lost tiles this way. The patch has two parts:
# - mp_run(): upstream's fix (GreycLab/gmic 6938fc8 with GreycLab/CImg e3e326b) makes run() fail
#   with "Running pipeline has re-allocated the image list." when the list's array changed during
#   a pixelwise evaluation (fill or eval of an image), and leaves the other evaluations (e.g.
#   'eval' without a selection) as they were. It passes the math parser to gmic::mp_run() for that,
#   which G'MIC 4.0.8's cimg_mp_func_run(str,is_parallel_run) doesn't do. This part makes the same
#   check in CImg's mp_run(), which has the math parser: 4.0.8's mp.is_fill is develop's
#   mp.is_pixelwise_evaluation. cimg_mp_func_run() returns, so it moves into a helper,
#   _mp_run_func(). On a CImg.h whose cimg_mp_func_run() takes the math parser only (develop), this
#   part does nothing.
# - _fill(): after that error, _fill() still read the freed *this: 'fill' fell back to
#   _fill_from_values() on it, which, on a zeroed header (macOS), reported success and dropped the
#   error (the writes were still lost), and could write through its null _data (Linux, an
#   expression starting with "1;"); the error message read its dimensions. This part remembers the
#   list's array at entry, and rethrows the error without touching *this when it changed. CImg
#   develop has the same fallback: this part applies to it too. Both parts are inside
#   '#ifdef cimg_mp_func_run', which only G'MIC defines (openfx-misc copies this script).
#
# Running it again on a patched file does nothing. The script fails in any other case: the code
# not found as expected (e.g. after a GMICVER bump: then check what upstream changed, and update
# or remove a part of this patch), or CRLF line endings. Only POSIX sh, awk and wc are used, for
# Linux, macOS and MSYS2.
set -e
f="$1"

old="cimg_mp_func_run(str._data,n_thread && mp.is_noncritical_run);"
up="cimg_mp_func_run(str._data);"
ret="return cimg::type<double>::nan();"
marker="// openfx-gmic patch: fail if run() re-allocates the image list of a fill/eval (#243)"
entry="if (result_end) result_end->assign();"
seq="// Try to fill values according to a value sequence."
fallback="if (!is_done && mode&1) is_done = !_fill_from_values(expression,repeat_values);"
marker1="// openfx-gmic patch (#243)"
marker2="// openfx-gmic patch: run() re-allocated the image list, *this may be freed: don't read it (#243)"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-run-realloc.sh (https://github.com/NatronGitHub/openfx-gmic/issues/243)" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<mp_run markers> <old calls followed by return nan and '}'> <upstream calls>
# <is_fill members> <_fill() entries ('cimg_abort_init;' then the result_end line)> <value-sequence
# fallbacks (the comment then the call)> <_fill() entry markers> <_fill() fallback markers>"
counts() {
  awk -v old="$old" -v up="$up" -v ret="$ret" -v marker="$marker" -v entry="$entry" -v seq="$seq" \
      -v fallback="$fallback" -v marker1="$marker1" -v marker2="$marker2" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    state == 2 { if (t == "}") o++; state = 0 }
    state == 1 { state = (t == ret) ? 2 : 0 }
    t == old { state = 1 }
    t == up { u++ }
    t == marker { m++ }
    index(t, "bool is_end_code, is_fill, is_noncritical_run,") || index(t, "bool is_end_code, is_pixelwise_evaluation, is_noncritical_run,") { fill++ }
    t == entry && prev == "cimg_abort_init;" { e++ }
    t == fallback && prev == seq { s++ }
    index(t, marker1) { m1++ }
    t == marker2 { m2++ }
    { prev = t }
    END { print m + 0, o + 0, u + 0, fill + 0, e + 0, s + 0, m1 + 0, m2 + 0 }' "$1"
}

case "$f" in
  *CImg.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"
cr=`printf '\r'`
if awk -v cr="$cr" 'index($0, cr) { found = 1; exit } END { exit !found }' "$f"; then
  fail "CRLF line endings are not supported (extract CImg.h with LF line endings)"
fi

set -- `counts "$f"`
[ "$4" = 1 ] || fail "expected one declaration of the math parser's is_fill (or is_pixelwise_evaluation) member, found $4"
[ "$5" = 1 ] && [ "$6" = 1 ] || fail "expected _fill()'s '$entry' after 'cimg_abort_init;', and its value-sequence fallback, once each; found $5 and $6"
# mp_run(): patch it (1), already patched or fixed upstream (0)
if [ "$1" = 0 ] && [ "$2" = 1 ] && [ "$3" = 0 ]; then
  run=1; lines=12
elif [ "$1" = 1 ] && [ "$2" = 0 ] && [ "$3" = 0 ]; then
  run=0; lines=0; echo "$f: mp_run() already patched (#243)"
elif [ "$1" = 0 ] && [ "$2" = 0 ] && [ "$3" = 1 ]; then
  run=0; lines=0; echo "$f: cimg_mp_func_run() takes the math parser: mp_run() not patched (fixed upstream: G'MIC checks the image list in gmic::mp_run(), #243)"
else
  fail "expected one '$old' followed by '$ret' and '}', or one '$up'; found $2 and $3 ($1 patch markers)"
fi
# _fill(): patch it (1), already patched (0)
if [ "$7" = 0 ] && [ "$8" = 0 ]; then
  fill=1; lines=$((lines + 10))
elif [ "$7" = 1 ] && [ "$8" = 1 ]; then
  fill=0; echo "$f: _fill() already patched (#243)"
else
  fail "_fill() is partly patched ($7 entry markers, $8 fallback markers)"
fi
[ "$run$fill" = 00 ] && exit 0

awk -v run="$run" -v fill="$fill" -v old="$old" -v ret="$ret" -v marker="$marker" -v entry="$entry" \
    -v seq="$seq" -v marker1="$marker1" -v marker2="$marker2" '
{ t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t); ind = $0; sub(/[^ \t].*$/, "", ind) }
state == 2 {
  # the closing brace of mp_run(): add the helper after it
  print
  print ""
  print "      // cimg_mp_func_run() returns: openfx-gmic patch (#243)"
  print "      static double _mp_run_func(_cimg_math_parser& mp, char *const str, const bool is_parallel_run) {"
  print "        cimg_mp_func_run(str,is_parallel_run);"
  print "        return cimg::type<double>::nan();"
  print "      }"
  state = 0; prev = t; next
}
state == 1 { state = 2; prev = t; next }
run && t == old {
  print ind marker
  print ind "const CImg<T> *const imglist_data = mp.imglist._data;"
  print ind "const double res = _mp_run_func(mp,str._data,n_thread && mp.is_noncritical_run);"
  print ind "if (mp.is_fill && imglist_data && imglist_data!=mp.imglist._data)"
  print ind "  throw CImgArgumentException(\"[\" cimg_appname \"_math_parser] CImg<%s>: Function \047run()\047: \""
  print ind "                              \"Running pipeline has re-allocated the image list.\","
  print ind "                              cimg::type<T>::string());"
  print ind "return res;"
  state = 1; prev = t; next
}
fill && t == entry && prev == "cimg_abort_init;" {
  print
  print "#ifdef cimg_mp_func_run"
  print ind "const CImg<t> *const list_data = list_images?list_images->_data:0; " marker1
  print "#endif"
  prev = t; next
}
fill && t == seq {
  print "#ifdef cimg_mp_func_run"
  print ind marker2
  print ind "if (!is_done && list_images && list_images->_data!=list_data) {"
  print ind "  cimg::exception_mode(excmode);"
  print ind "  throw CImgArgumentException(\"%s\",is_error_expr?is_error_expr._data:\"\");"
  print ind "}"
  print "#endif"
  print
  prev = t; next
}
{ print; prev = t }' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$7" = 1 ] && [ "$8" = 1 ] || fail "the _fill() part did not apply ($*)"
[ "$run" = 0 ] || { [ "$1" = 1 ] && [ "$2" = 0 ] && [ "$3" = 0 ]; } || fail "the mp_run() part did not apply ($*)"
[ "`wc -l < "$f.patched"`" -eq $((`wc -l < "$f"` + lines)) ] || fail "the patch did not add $lines lines"
mv "$f.patched" "$f"
echo "$f: patched so that a fill/eval fails if its run() re-allocates the image list (#243)"
