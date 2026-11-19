#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that on Windows,
# cimg::load_network() doesn't pass --compressed to the curl command. Usage:
# cimg-curl-compressed.sh <CImg.h>
#
# Without libcurl (cimg_use_curl), CImg downloads by running
#   "<curl>" -L --max-redirs 20 [-m <timeout>] [-e <referer>] -A "<agent>" -f --silent --compressed -o "<file>" "<url>"
# The curl.exe of Windows (System32, Windows 10 1803 and later, which the plug-in runs: see
# gmic_plugin_call.cpp) is built without zlib, and refuses --compressed ("the installed libcurl
# version doesn't support this"): every download then falls back to PowerShell's
# Invoke-WebRequest, then wget. --compressed only asks the server for a compressed transfer, so
# leaving it out on Windows changes nothing else. Other systems keep it.
#
# This script puts the --compressed option of that command line in a macro, defined just before
# it as "" on Windows (cimg_OS==2) and "--compressed " elsewhere. "If the fix is already there,
# don't apply it": the script does nothing on a CImg.h that has the fix (patched, or fixed
# upstream: no --compressed at all), applies it to the 4.0.7 code (the command line once, no other
# --compressed), and fails on anything else (e.g. after a GMICVER bump that changed this code
# otherwise), leaving the file untouched: then check what upstream did, and update or remove this
# patch. Lines are compared without their leading and trailing blanks. Only POSIX sh, awk, cmp and
# wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

# the two lines to patch (the second one is changed), the patched line and the lines inserted
# before the first one
l1='cimg_snprintf(command,command._width,'
l2='"\"%s\" -L --max-redirs 20 %s%s-A \"%s\" -f --silent --compressed -o \"%s\" \"%s\"",'
p2='"\"%s\" -L --max-redirs 20 %s%s-A \"%s\" -f --silent " _cimg_curl_compressed "-o \"%s\" \"%s\"",'
i1='#if cimg_OS==2 // patches/cimg-curl-compressed.sh: Windows'"'"' curl.exe has no zlib, refuses --compressed'
i2='#define _cimg_curl_compressed ""'
i3='#else'
i4='#define _cimg_curl_compressed "--compressed "'
i5='#endif'
# passed to awk through the environment: awk -v would interpret the backslashes
export l1 l2 p2 i1 i2 i3 i4 i5
vars='BEGIN { l1 = ENVIRON["l1"]; l2 = ENVIRON["l2"]; p2 = ENVIRON["p2"]; i1 = ENVIRON["i1"]; i2 = ENVIRON["i2"]; i3 = ENVIRON["i3"]; i4 = ENVIRON["i4"]; i5 = ENVIRON["i5"] }'

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-curl-compressed.sh" >&2
  rm -f "$f.patched" "$f.stripped1" "$f.stripped2"
  exit 1
}
trap 'rm -f "$f.patched" "$f.stripped1" "$f.stripped2"; exit 1' INT TERM HUP

case "$f" in
  *CImg.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"
cr=`printf '\r'`
if awk -v cr="$cr" 'index($0, cr) { found = 1; exit } END { exit !found }' "$f"; then
  fail "CRLF line endings are not supported (extract CImg.h with LF line endings)"
fi

# state <file>: number of original command lines (l1, l2), of patched ones (i1..i5, l1, p2), and of
# other lines that contain --compressed
state() {
  awk "$vars"'
    function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
    { t[++n] = trim($0) }
    END {
      for (i = 1; i <= n; ++i) {
        if (i + 1 <= n && t[i] == l1 && t[i + 1] == l2) { ++orig; ++i; continue }
        if (i + 6 <= n && t[i] == i1 && t[i + 1] == i2 && t[i + 2] == i3 && t[i + 3] == i4 && t[i + 4] == i5 &&
            t[i + 5] == l1 && t[i + 6] == p2) { ++patched; i += 6; continue }
        if (t[i] ~ /--compressed/) ++others
      }
      print orig + 0, patched + 0, others + 0
    }' "$1"
}

found=$(state "$f")
if [ "$found" = "0 1 0" ]; then
  echo "$f: already patched (no curl --compressed on Windows)"
  exit 0
fi
if [ "$found" = "0 0 0" ]; then
  echo "$f: already fixed upstream (no curl --compressed)"
  exit 0
fi
[ "$found" = "1 0 0" ] || fail "expected the curl command line of cimg::load_network() once and no other --compressed, as in G'MIC 4.0.7, or the patched code (found original, patched, other --compressed lines: $found)"

awk "$vars"'
  function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
  { o[++n] = $0; t[n] = trim($0) }
  END {
    for (i = 1; i <= n; ++i) {
      if (i + 1 <= n && t[i] == l1 && t[i + 1] == l2) {
        print i1; print i2; print i3; print i4; print i5
        print o[i]
        match(o[i + 1], /^[ \t]*/)
        print substr(o[i + 1], 1, RLENGTH) p2
        ++i
        continue
      }
      print o[i]
    }
  }' "$f" > "$f.patched" || fail "awk failed"
[ "$(state "$f.patched")" = "0 1 0" ] || fail "the patch did not apply"
[ "$(wc -l < "$f.patched")" -eq $(($(wc -l < "$f") + 5)) ] || fail "the patch changed the number of lines unexpectedly"
# nothing else changed: without the command line (original) and the patched lines, both files are
# the same
awk "$vars"'
  function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
  { o[++n] = $0; t[n] = trim($0) }
  END { for (i = 1; i <= n; ++i) { print o[i]; if (i + 1 <= n && t[i] == l1 && t[i + 1] == l2) ++i } }' \
  "$f" > "$f.stripped1" || fail "awk failed"
awk "$vars"'
  function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
  { o[++n] = $0; t[n] = trim($0) }
  END {
    for (i = 1; i <= n; ++i) {
      if (i + 6 <= n && t[i] == i1 && t[i + 1] == i2 && t[i + 2] == i3 && t[i + 3] == i4 && t[i + 4] == i5 &&
          t[i + 5] == l1 && t[i + 6] == p2) { print o[i + 5]; i += 6; continue }
      print o[i]
    }
  }' "$f.patched" > "$f.stripped2" || fail "awk failed"
cmp -s "$f.stripped1" "$f.stripped2" || fail "the patch changed other lines"
rm -f "$f.stripped1" "$f.stripped2"
mv "$f.patched" "$f"
echo "$f: patched to not pass --compressed to curl on Windows"
