#!/bin/sh
# Patch gmic.cpp, extracted from the G'MIC sources by the top-level Makefile, so that the
# 'displacement' and 'serialize' commands no longer write past G'MIC's 1-byte empty string.
# Usage: gmic-argx-overflow.sh <gmic.cpp>   (prints "already patched" on a patched file)
# Upstream: https://github.com/GreycLab/gmic/issues/227
#
# In gmic::_run(), argx points to the 1-byte sentinel _c0 (an empty string on the stack) until
# gmic_use_argx allocates its 256-byte buffer. Two commands strcpy() into it without that:
# - 'displacement' without a guide image (gmic_use_argx is only in the sscanf() of the 6-argument
#   form), with nb_scales=0 (the default): std::strcpy(argx,"auto-"), 6 bytes. Depth Map
#   Reconstruction does this on every render. With nb_scales>0, cimg_snprintf() into a 0-byte
#   buffer writes nothing (no overflow, the message lacks the number of scales).
# - 'serialize' with an argument that doesn't parse (e.g. 'serialize a,b,c,d'):
#   std::strcpy(argx,"auto"), 5 bytes.
# AddressSanitizer reports a stack-buffer-overflow at both. The patch adds 'gmic_use_argx;' before
# the first line, and uses std::strcpy(gmic_use_argx,"auto") in the second.
#
# Running it again on a patched file does nothing (it says so), and so does running it on a
# gmic.cpp where upstream has fixed both sites (gmic_use_argx on the line before the first one, or
# in its strcpy(); gmic_use_argx in the second strcpy(): it says so too). The script fails in any
# other case (a line not found, or found more than once: e.g. after a GMICVER bump, then check what
# upstream changed, and update or remove this patch). Only POSIX sh and awk are used, for Linux,
# macOS, MSYS2 and Git for Windows.
set -e
f="$1"
me="patches/$(basename "$0")"
tag='// openfx-gmic patch: argx may still be the 1-byte _c0'
old1='if (nb_scales) cimg_snprintf(argx,_argx.width(),"%g ",nb_scales); else std::strcpy(argx,"auto-");'
add1="gmic_use_argx; $tag"
old2='else { std::strcpy(argx,"auto"); is_compressed = is_compressed0?1U:0U; is_gmz = 1; }'
new2='else { std::strcpy(gmic_use_argx,"auto"); is_compressed = is_compressed0?1U:0U; is_gmz = 1; }'

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the G'MIC sources changed: update or remove $me" >&2
  rm -f "$f.patched"
  exit 1
}

# state <file>: "<old1> <ours1> <up1> <old2> <ours2> <up2>": how many times each site is in the
# file, unpatched, patched by this script, or fixed upstream. Lines are compared without their
# indentation.
state() {
  awk -v old1="$old1" -v add1="$add1" -v old2="$old2" -v new2="$new2" -v tag="$tag" '
    { t = $0; sub(/^[ \t]+/, "", t) }
    t == old1 {
      if (prev == add1) ours1++
      else if (index(prev, "gmic_use_argx") && !index(prev, tag)) up1++
      else o1++
    }
    t != old1 && index(t, "std::strcpy(gmic_use_argx,\"auto-\")") && !index(t, tag) { up1++ }
    t == old2 { o2++ }
    t == new2 " " tag { ours2++ }
    t != new2 " " tag && index(t, "std::strcpy(gmic_use_argx,\"auto\")") && !index(t, tag) { up2++ }
    { prev = t }
    END { printf "%d %d %d %d %d %d\n", o1, ours1, up1, o2, ours2, up2 }' "$1"
}

set -- $(state "$f")
[ $(($1 + $2 + $3)) = 1 ] || fail "expected the strcpy(argx,\"auto-\") line of 'displacement' once (found $1 unpatched, $2 patched, $3 fixed upstream)"
[ $(($4 + $5 + $6)) = 1 ] || fail "expected the strcpy(argx,\"auto\") line of 'serialize' once (found $4 unpatched, $5 patched, $6 fixed upstream)"
if [ "$1" = 0 ] && [ "$4" = 0 ]; then
  if [ "$2" = 1 ] && [ "$5" = 1 ]; then
    echo "$f: already patched ('displacement' and 'serialize' no longer write past argx's 1-byte sentinel)"
  elif [ "$3" = 1 ] && [ "$6" = 1 ]; then
    echo "$f: not patched: G'MIC already allocates argx in 'displacement' and 'serialize' (fixed upstream?): check and remove $me"
  else
    echo "$f: already patched where G'MIC doesn't allocate argx itself (one site fixed upstream?): check and update $me"
  fi
  exit 0
fi

awk -v old1="$old1" -v add1="$add1" -v old2="$old2" -v new2="$new2" -v tag="$tag" '
  { t = $0; sub(/^[ \t]+/, "", t); ind = substr($0, 1, length($0) - length(t)) }
  t == old1 && prev != add1 && !index(prev, "gmic_use_argx") { print ind add1 }
  t == old2 { print ind new2 " " tag; prev = t; next }
  { print; prev = t }' "$f" > "$f.patched"
set -- $(state "$f.patched")
[ "$1 $4" = "0 0" ] && [ $(($2 + $3)) = 1 ] && [ $(($5 + $6)) = 1 ] ||
  fail "the patched file is not as expected ($*)"
mv "$f.patched" "$f"
[ $(($3 + $6)) = 0 ] || echo "$f: note: G'MIC already allocates argx at one of the two sites (fixed upstream?): check and update $me"
echo "$f: patched so that 'displacement' and 'serialize' no longer write past argx's 1-byte sentinel"
