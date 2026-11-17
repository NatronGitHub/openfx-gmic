#!/bin/sh
# Generate the gmic_stdlib_community.h of Natron's COMMERCIAL builds (make LICENSE=COMMERCIAL):
# G'MIC's standard library and only the community filters whose licence allows it, without the
# filters and commands listed in gmic_stdlib_commercial.txt (see the explanations there).
# Usage: gmic_stdlib_commercial.sh <gmic_<ver>.tar.gz> <G'MIC version> <output .h>
# Uses $CXX (default c++), $ZLIB_CFLAGS and $ZLIB_LIBS (zlib) to build a G'MIC command-line
# interpreter from the tarball (a build tool only, not shipped), which generates the library as
# G'MIC's own src/Makefile does (parse_gui update, serialize). Works in gmic_stdlib_commercial/,
# in a directory named after the version and checksum of the tarball, so that a new G'MIC
# version or tarball gets a new extraction and interpreter. Fails if a kept filter can reach a
# command that only the left-out files define (see check_reachable below).
# Also writes <output .h>.commands, the names of the commands it defines (for gmic_hotfixes.h).
# The header starts with a ' #  openfx-gmic : COMMERCIAL standard library' line, which
# CMakeLists.txt and GMIC_OFX/Makefile check. It is only written (renamed) once complete.
# Only POSIX sh, tar, curl, awk, sed and sha256sum or shasum are used.
set -e
tarball=$1
ver=$2
out=$3
conf=$(dirname "$0")/gmic_stdlib_commercial.txt
top=gmic_stdlib_commercial
CXX=${CXX:-c++}

fail() { echo "Error: $0: $1" >&2; rm -f "$out.tmp" "$out.commands.tmp"; exit 1; }
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
[ -f "$tarball" ] || fail "$tarball not found"
rm -f "$out.tmp" "$out.commands.tmp"
key=$ver-$(sha256 "$tarball" | cut -c1-16)
work=$top/$key
mkdir -p "$top/community"
# remove the work directories of other versions or tarballs
for d in "$top"/*; do
  [ -d "$d" ] && [ "$d" != "$work" ] && [ "$d" != "$top/community" ] && rm -rf "$d"
done
mkdir -p "$work"

# The G'MIC sources, as in the tarball (the interpreter is built with its standard library)
if [ ! -f "$work/src/.complete" ]; then
  rm -rf "$work/src" && mkdir -p "$work/src"
  for f in gmic.cpp gmic.h CImg.h gmic_stdlib_community.h gmic_cli.cpp gmic_stdlib.gmic; do
    tar -m --strip-components 2 -C "$work/src" -zxf "$tarball" "gmic-$ver/src/$f" || fail "could not extract $f from $tarball"
  done
  touch "$work/src/.complete"
fi
# The interpreter, and a program that writes the standard library built into it (the tarball's)
if [ ! -x "$work/gmic" ] || [ ! -x "$work/gmic_stdlib_dump" ]; then
  echo "Building a G'MIC interpreter to generate the standard library (takes a few minutes)"
  cat > "$work/gmic_stdlib_dump.cpp" <<'EOF2'
#include <cstdio>
#include <cstring>
#include "gmic.h"
int main() {
  const gmic_image<char>& stdlib = gmic::decompress_stdlib();
  if (!stdlib._data) return 1;
  std::fwrite(stdlib._data, 1, std::strlen(stdlib._data), stdout);
  return 0;
}
EOF2
  # shellcheck disable=SC2086
  $CXX -O1 -w -Dgmic_core -Dcimg_display=0 -Dcimg_use_zlib $ZLIB_CFLAGS -c "$work/src/gmic.cpp" -o "$work/gmic.o" &&
  $CXX -O1 -w -Dcimg_display=0 -Dcimg_use_zlib $ZLIB_CFLAGS -c "$work/src/gmic_cli.cpp" -o "$work/gmic_cli.o" &&
  $CXX -O1 -w -Dcimg_display=0 -Dcimg_use_zlib $ZLIB_CFLAGS -I"$work/src" -c "$work/gmic_stdlib_dump.cpp" -o "$work/gmic_stdlib_dump.o" &&
  $CXX "$work/gmic.o" "$work/gmic_cli.o" -o "$work/gmic.tmp" $ZLIB_LIBS &&
  $CXX "$work/gmic.o" "$work/gmic_stdlib_dump.o" -o "$work/gmic_stdlib_dump.tmp" $ZLIB_LIBS ||
    fail "could not build the G'MIC interpreter"
  mv "$work/gmic_stdlib_dump.tmp" "$work/gmic_stdlib_dump"
  mv "$work/gmic.tmp" "$work/gmic"
fi
gmic="$work/gmic"

# The community files, from the pinned gmic-community commit, checked against their sha256
commit=$(awk '$1 == "commit" { print $2 }' "$conf")
[ -n "$commit" ] || fail "no commit in $conf"
files=
for entry in $(awk '$1 == "community" { print $2 "=" $3 }' "$conf"); do
  name=${entry%%=*}; sum=${entry#*=}; f="$top/community/$name.gmic"
  if [ ! -f "$f" ] || [ "$(sha256 "$f")" != "$sum" ]; then
    url="https://raw.githubusercontent.com/GreycLab/gmic-community/$commit/include/$name.gmic"
    echo "Fetching $url"
    curl -f --retry 5 --retry-all-errors -L -s -S -o "$f.tmp" "$url" || fail "could not download $url"
    [ "$(sha256 "$f.tmp")" = "$sum" ] || { rm -f "$f.tmp"; fail "checksum mismatch for $url"; }
    mv "$f.tmp" "$f"
  fi
  files="$files it $f"
done

# Generate the update file as G'MIC does ('parse_gui update'). straprilfool is redefined, so that
# a build on April 1st does not get G'MIC's April fool filter names.
printf 'straprilfool :\n  u "$*"\n' > "$work/no_april_fool.gmic"
# shellcheck disable=SC2086
"$gmic" -v -1 m "$work/no_april_fool.gmic" it "$work/src/gmic_stdlib.gmic" $files parse_gui update ot "$work/update.gmic" \
  > "$work/parse_gui.log" 2>&1 || { cat "$work/parse_gui.log"; fail "parse_gui failed"; }
[ -s "$work/update.gmic" ] || fail "parse_gui gave no output"

# Remove the listed filters (their '#@gui' block, in every language) and command definitions. In
# the update file, a filter is a '#@gui[_xx] Name:command,...' line followed by its '#@gui[_xx] :'
# lines, and a command definition goes from its 'name :' line to the next definition or '#@'
# line. Each listed command must be found. The header gets the licence of this selection, and no
# generation date (so that the output only depends on the inputs).
awk -v conf="$conf" -v ver="$ver" '
  BEGIN {
    while ((getline l < conf) > 0) {
      split(l, w, " ")
      if (w[1] == "filter") filt[w[2]] = 1
      else if (w[1] == "command") { cmd[w[2]] = 1; filt[w[2]] = 1 }
    }
    close(conf)
  }
  # a definition is a "name :" or "+name :" line (not "name :=")
  function defname(s) { return match(s, /^\+?[A-Za-z_][A-Za-z0-9_]*[ \t]*:/) && substr(s, RLENGTH + 1, 1) != "=" ? s : "" }
  /^#  License      : / { print "#  License      : this selection is under CeCILL-C (G\047MIC standard library: dual CeCILL-C /"; print "#                 CeCILL v2.1; community filters: see openfx-gmic\047s gmic_stdlib_commercial.txt)"; next }
  /^#                 \( https:\/\/cecill.info\/licences\/Licence_CeCILL_V2.1-en.html \)$/ { next }
  /^#  Generated on : / { print "#  Generated by : openfx-gmic\047s gmic_stdlib_commercial.sh, for G\047MIC " ver; next }
  /^#@gui/ {
    skipdef = 0
    if (match($0, /^#@gui(_[a-z]+)?[ \t]*:/)) { if (skipgui) next; print; next }
    skipgui = 0
    if (match($0, /^#@gui(_[a-z]+)?[ \t]+[^:]*:[ \t]*[A-Za-z_][A-Za-z0-9_]*/)) {
      c = substr($0, 1, RLENGTH); sub(/^.*:[ \t]*/, "", c)
      if (c in filt) { skipgui = 1; removed[c]++; next }
    }
    print; next
  }
  /^#@/ { skipgui = 0; skipdef = 0; print; next }
  {
    skipgui = 0
    if (defname($0) != "") {
      n = $0; sub(/[ \t]*:.*$/, "", n); sub(/^\+/, "", n)
      skipdef = (n in cmd)
      if (skipdef) defined[n]++
    }
    if (!skipdef) print
  }
  END {
    for (c in filt) if (!(c in cmd) && !(c in removed)) { print "no filter runs " c > "/dev/stderr"; bad = 1 }
    for (c in cmd) if (!(c in defined)) { print "command " c " not found" > "/dev/stderr"; bad = 1 }
    if (bad) exit 1
  }' "$work/update.gmic" > "$work/update_commercial.gmic" || fail "could not remove the listed filters and commands (see above)"

# Check that no kept filter can reach a command that only the left-out files define, i.e. that is
# in the tarball's standard library but not in update_commercial.gmic (it would fail, and its code
# is under a licence these builds leave out). From each filter's command and preview command, the
# commands it calls are followed: the words of its definition, also inside double-quoted strings
# (command lines passed to e.g. 'ac' or 'foreach'), without a leading '+' or '-', cut at the first
# character that is not part of a name (e.g. the selection of 'negative[0]'), that are defined
# commands. Variables ($name), assignments (name=..., name.=...), math functions (name(...)) and
# the rest of the line after messages and checks (e "...", error "...", check "...", ...) are not
# taken for calls.
"$work/gmic_stdlib_dump" > "$work/stdlib_tarball.gmic" || fail "could not read the tarball's standard library"
awk '
  function defname(s,   n) {
    if (!match(s, /^\+?[A-Za-z_][A-Za-z0-9_]*[ \t]*:/) || substr(s, RLENGTH + 1, 1) == "=") return ""
    n = s; sub(/[ \t]*:.*$/, "", n); sub(/^\+/, "", n); return n
  }
  FNR == 1 { file++ }
  file == 1 { n = defname($0); if (n != "") full[n] = 1; next }
  /^#@gui/ {
    cur = ""
    if (!match($0, /^#@gui(_[a-z]+)?[ \t]*:/) && match($0, /^#@gui(_[a-z]+)?[ \t]+[^:]*:/)) {
      r = substr($0, RLENGTH + 1); gsub(/[ \t]/, "", r); split(r, c, ",")
      for (i = 1; i <= 2; i++) { sub(/[^A-Za-z0-9_].*$/, "", c[i]); if (c[i] != "" && c[i] != "_none_") root[c[i]] = 1 }
    }
    next
  }
  /^#/ { if (/^#@/) cur = ""; next }
  { n = defname($0); if (n != "") { cur = n; kept[n] = 1; $0 = substr($0, index($0, ":") + 1) }
    if (cur != "") body[cur] = body[cur] "\n" $0 }
  END {
    for (n in full) if (!(n in kept)) only[n] = 1
    # the calls of each kept command; via[n]: the callee through which n reaches a command only
    # defined in left-out files (or that command itself, when n calls it)
    for (n in kept) {
      b = body[n]
      # messages and checks are not command lines
      gsub(/(^|[ \t\n;])(e|echo|error|warn|check|skip|u|status|nm|name|v|verbose|_text)(\[[^]]*\])?[ \t]+"[^\n]*/, " ", b)
      m = split(b, w, /[ \t\n;"]+/)
      for (j = 1; j <= m; j++) {
        t = w[j]; sub(/^[-+]+/, "", t)
        if (t ~ /^[A-Za-z_][A-Za-z0-9_]*([^A-Za-z0-9_]?=|\()/) continue
        sub(/[^A-Za-z0-9_].*$/, "", t)
        if (t == "" || t == n) continue
        if (t in only) { if (!(n in via)) via[n] = t }
        else if (t in kept) calls[n] = calls[n] " " t
      }
    }
    do {
      changed = 0
      for (n in calls) if (!(n in via)) {
        m = split(calls[n], c, " ")
        for (j = 1; j <= m; j++) if (c[j] in via) { via[n] = c[j]; changed = 1; break }
      }
    } while (changed)
    for (r in root) {
      if (!(r in kept)) { if (r in only) { print "filter command " r " is only in left-out files" > "/dev/stderr"; bad = 1 }; continue }
      if (!(r in via)) continue
      chain = r; n = r; k = 0
      while ((n in via) && k++ < 100) { n = via[n]; chain = chain " -> " n }
      print "the filter command " r " reaches a command only in left-out files: " chain > "/dev/stderr"; bad = 1
    }
    if (bad) exit 1
  }' "$work/stdlib_tarball.gmic" "$work/update_commercial.gmic" ||
  fail "kept filters call commands of left-out files (see above): add these filters to gmic_stdlib_commercial.txt"

# The names of the defined commands (for the hot fixes)
awk 'match($0, /^\+?[A-Za-z_][A-Za-z0-9_]*[ \t]*:/) && substr($0, RLENGTH + 1, 1) != "=" { sub(/[ \t]*:.*$/, ""); sub(/^\+/, ""); print }' \
  "$work/update_commercial.gmic" | sort -u > "$out.commands.tmp"

# Serialize it into a C++ header, as G'MIC's src/Makefile does (target do_gmic_stdlib_community.h)
{
  echo "/*"
  echo " #  File        : gmic_stdlib_community.h (v.$ver), for Natron's COMMERCIAL builds"
  echo " #  Description : G'MIC's standard library with the community filters that are not under the"
  echo " #                GPL or a CeCILL-only licence, compressed. Generated by openfx-gmic's"
  echo " #                gmic_stdlib_commercial.sh (see gmic_stdlib_commercial.txt): do not edit."
  echo " #  Licence     : CeCILL-C (see Licence_CeCILL-C_V1-en.txt)"
  echo " #  openfx-gmic : COMMERCIAL standard library, sha256 $(sha256 "$work/update_commercial.gmic")"
  echo "*/"
  "$gmic" -v -1 it "$work/update_commercial.gmic" "(0)" a y serialize uint8,1,0 o -.h | sed 's/uint8/const unsigned char/' | sed 's/unnamed/gmic/'
  printf "\nconst unsigned long size_data_gmic = (unsigned long)sizeof(data_gmic);\n"
} > "$out.tmp"
grep -q 'const unsigned char data_gmic\[\]' "$out.tmp" || { rm -f "$out.tmp"; fail "could not serialize the standard library"; }
mv "$out.commands.tmp" "$out.commands"
mv "$out.tmp" "$out"
echo "$out: $(grep -c '^#@gui [^: ][^:]*:' "$work/update_commercial.gmic") filters (COMMERCIAL)"
