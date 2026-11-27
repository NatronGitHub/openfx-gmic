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
# e.g. 100,100,1,1,7 eval[0] "run('1x20');i(0,0)=3" leaves 7 (heap use-after-free).
# Upstream's fix (GreycLab/gmic 6938fc8 with GreycLab/CImg e3e326b) makes run() fail with
# "Running pipeline has re-allocated the image list." when the list's array changed during a
# pixelwise evaluation (fill or eval of an image), and leaves the other evaluations (e.g. 'eval'
# without a selection) as they were. It passes the math parser to gmic::mp_run() for that, which
# G'MIC 4.0.8's cimg_mp_func_run(str,is_parallel_run) doesn't do. This patch makes the same check
# in CImg's mp_run(), which has the math parser: 4.0.8's mp.is_fill is develop's
# mp.is_pixelwise_evaluation. cimg_mp_func_run() returns, so it moves into a helper, _mp_run_func().
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h whose
# cimg_mp_func_run() takes the math parser only (the upstream fix). The script fails in any other
# case: the call not found as expected (e.g. after a GMICVER bump: then check what upstream
# changed, and update or remove this patch), or CRLF line endings. Only POSIX sh, awk and wc are
# used, for Linux, macOS and MSYS2.
set -e
f="$1"

old="cimg_mp_func_run(str._data,n_thread && mp.is_noncritical_run);"
up="cimg_mp_func_run(str._data);"
ret="return cimg::type<double>::nan();"
marker="// openfx-gmic patch: fail if run() re-allocates the image list of a fill/eval (#243)"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-run-realloc.sh (https://github.com/NatronGitHub/openfx-gmic/issues/243)" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<markers> <old calls followed by return nan and '}'> <upstream calls> <is_fill
# members>"
counts() {
  awk -v old="$old" -v up="$up" -v ret="$ret" -v marker="$marker" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    state == 2 { if (t == "}") o++; state = 0 }
    state == 1 { state = (t == ret) ? 2 : 0 }
    t == old { state = 1 }
    t == up { u++ }
    t == marker { m++ }
    index(t, "bool is_end_code, is_fill, is_noncritical_run,") { fill++ }
    END { print m + 0, o + 0, u + 0, fill + 0 }' "$1"
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
if [ "$1" = 1 ] && [ "$2" = 0 ] && [ "$3" = 0 ] && [ "$4" = 1 ]; then
  echo "$f: already patched (run() re-allocating the image list of a fill/eval, #243)"
  exit 0
fi
if [ "$1" = 0 ] && [ "$2" = 0 ] && [ "$3" = 1 ]; then
  echo "$f: cimg_mp_func_run() takes the math parser: not patched (fixed upstream: G'MIC checks the image list in gmic::mp_run(), #243)"
  exit 0
fi
[ "$1" = 0 ] && [ "$2" = 1 ] && [ "$3" = 0 ] && [ "$4" = 1 ] ||
  fail "expected one '$old' followed by '$ret' and '}', and the math parser's is_fill member; found $2 such calls, $3 '$up', $4 is_fill declarations ($1 patch markers)"

awk -v old="$old" -v ret="$ret" -v marker="$marker" '
{ t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
state == 2 {
  # the closing brace of mp_run(): add the helper after it
  print
  print ""
  print "      // cimg_mp_func_run() returns: openfx-gmic patch (#243)"
  print "      static double _mp_run_func(_cimg_math_parser& mp, char *const str, const bool is_parallel_run) {"
  print "        cimg_mp_func_run(str,is_parallel_run);"
  print "        return cimg::type<double>::nan();"
  print "      }"
  state = 0; next
}
state == 1 { state = 2; next }
t == old {
  ind = $0; sub(/[^ \t].*$/, "", ind)
  print ind marker
  print ind "const CImg<T> *const imglist_data = mp.imglist._data;"
  print ind "const double res = _mp_run_func(mp,str._data,n_thread && mp.is_noncritical_run);"
  print ind "if (mp.is_fill && imglist_data && imglist_data!=mp.imglist._data)"
  print ind "  throw CImgArgumentException(\"[\" cimg_appname \"_math_parser] CImg<%s>: Function \047run()\047: \""
  print ind "                              \"Running pipeline has re-allocated the image list.\","
  print ind "                              cimg::type<T>::string());"
  print ind "return res;"
  state = 1; next
}
{ print }' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 1 ] && [ "$2" = 0 ] && [ "$3" = 0 ] && [ "$4" = 1 ] || fail "the patch did not apply ($*)"
[ "`wc -l < "$f.patched"`" -eq $((`wc -l < "$f"` + 12)) ] || fail "the patch did not add 12 lines"
mv "$f.patched" "$f"
echo "$f: patched so that a fill/eval fails if its run() re-allocates the image list (#243)"
