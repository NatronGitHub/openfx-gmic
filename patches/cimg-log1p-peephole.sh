#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser's log(1 + x) -> log1p(x) optimization only rewrites code compiled for log()'s own
# argument. Usage: cimg-log1p-peephole.sh <CImg.h>
# Upstream: TBD (https://github.com/GreycLab/CImg/issues)
#
# CImg 4.0.8's log() handler rewrites the last compiled opcode (code.back()) into log1p (or
# vector_log1p) when it is an increment (mp_increment, mp_vector_increment) that writes log()'s
# argument. Unlike the older peepholes of the '+', '-', '*' and '/' operators, which first save
# p3 = code.size() and then check code.size()>p3, it doesn't check that this opcode was emitted
# while compiling the argument. When the argument is a variable, which compiles to no code, the
# opcode is the one that computed the variable earlier, and the variable itself is overwritten:
# 'y=1+x;log(y);y' gives log(1 + x) instead of 1 + x, and 'Y=[x,x]+1;Z=log(Y);Y[0]' gives
# log(1 + x) instead of x + 1 (and Z gets the same values).
# The patch adds the same guard: 'p3 = code.size();' before compiling the argument, and
# 'is_sth = code.size()>p3;' instead of 'is_sth = code.size()?true:false;'.
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h whose log()
# handler already has the guard or no longer has the peephole (an upstream fix). The script fails
# in any other case: the expected code missing (e.g. after a GMICVER bump: then check what
# upstream changed, and update or remove this patch), or CRLF line endings. Only POSIX sh, awk and
# wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

head='if (!std::strncmp(ss,"log(",4)) { // Natural logarithm'
comp="arg1 = compile(ss4,se1,depth1,0,block_flags);"
old="is_sth = code.size()?true:false;"
marker="p3 = code.size(); // openfx-gmic patch: log1p peephole only on the code of log()'s argument"
new="is_sth = code.size()>p3;"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-log1p-peephole.sh" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<log() handlers> <markers> <argument compilations> <unguarded is_sth>
# <guarded is_sth> <log1p rewrites>", inside the log() handler (up to the log10() one)
counts() {
  awk -v head="$head" -v comp="$comp" -v old="$old" -v marker="$marker" -v new="$new" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    t == head { h++; inlog = 1; next }
    inlog && index(t, "if (!std::strncmp(ss,\"log10(\",6))") { inlog = 0 }
    inlog && t == marker { m++ }
    inlog && t == comp { c++ }
    inlog && t == old { o++ }
    inlog && t == new { n++ }
    inlog && (t == "pop[0] = (ulongT)mp_log1p;" || t == "pop[0] = (ulongT)mp_vector_log1p;") { r++ }
    END { print h + 0, m + 0, c + 0, o + 0, n + 0, r + 0 }' "$1"
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
if [ "$1" = 1 ] && [ "$2" = 1 ] && [ "$3" = 1 ] && [ "$4" = 0 ] && [ "$5" = 1 ] && [ "$6" = 2 ]; then
  echo "$f: already patched (log1p peephole)"
  exit 0
fi
if [ "$1" = 1 ] && [ "$2" = 0 ] && [ "$4" = 0 ] && { [ "$5" = 1 ] || [ "$6" = 0 ]; }; then
  echo "$f: the log() handler has no unguarded log1p peephole: not patched (fixed upstream)"
  exit 0
fi
[ "$1" = 1 ] && [ "$2" = 0 ] && [ "$3" = 1 ] && [ "$4" = 1 ] && [ "$5" = 0 ] && [ "$6" = 2 ] ||
  fail "expected one log() handler with '$comp', then '$old' and 2 log1p rewrites; found $1 handlers, $3 argument compilations, $4 unguarded and $5 guarded is_sth, $6 rewrites ($2 patch markers)"

awk -v head="$head" -v comp="$comp" -v old="$old" -v marker="$marker" -v new="$new" '
{ line = $0; t = line; gsub(/^[ \t]+|[ \t]+$/, "", t); ind = line; sub(/[^ \t].*$/, "", ind) }
t == head { inlog = 1; print; next }
inlog && t == comp { print ind marker; print; sawcomp = 1; next }
inlog && t == old {
  if (!sawcomp) { print "line " NR ": is_sth before the argument is compiled" > "/dev/stderr"; exit 1 }
  print ind new; inlog = 0; next
}
{ print }' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 1 ] && [ "$2" = 1 ] && [ "$3" = 1 ] && [ "$4" = 0 ] && [ "$5" = 1 ] && [ "$6" = 2 ] || fail "the patch did not apply"
[ "`wc -l < "$f.patched"`" -eq $((`wc -l < "$f"` + 1)) ] || fail "the patch did not add 1 line"
mv "$f.patched" "$f"
echo "$f: patched so that log(y) no longer rewrites the code that computed a variable y"
