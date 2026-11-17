#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser functions I() and J() do not read out of bounds with linear or cubic interpolation and
# periodic or Neumann boundary conditions. Usage: cimg-ij-boundary.sh <CImg.h>
#
# In _cimg_math_parser::_mp_IJxyz1(), these cases only wrap (periodic) or clamp (Neumann) x: y
# and z are passed unchanged to _cubic_atX_p(), _cubic_atX(), _linear_atX_p() and _linear_atX(),
# which read outside the image when y or z is outside it. The filter "Spiral Distortion" crashes
# on small inputs this way (its preshift uses J(...,2)), and so does 'gmic 320,240 f "J(0,-500,0,2,2)"'.
# https://github.com/GreycLab/CImg/issues/498. This applies the upstream fix proposed in
# https://github.com/GreycLab/CImg/pull/499: y and z are wrapped with cimg::mod() (periodic) or
# clamped with cimg::cut() (Neumann), as the mirror and nearest-neighbour cases already do.
#
# Each of the four 3-line cases below becomes a 4-line block, as in GreycLab/CImg#499. The script
# fails if the source does not contain the expected code (e.g. after a GMICVER bump): then check
# whether upstream fixed it, and update or remove this patch. Running it again on a patched file
# does nothing, and so does running it on a CImg.h that already has the fix of GreycLab/CImg#499.
# Only POSIX sh, awk and wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-ij-boundary.sh (GreycLab/CImg#498)" >&2
  rm -f "$f.patched" "$f.stripped1" "$f.stripped2"
  exit 1
}
trap 'rm -f "$f.patched" "$f.stripped1" "$f.stripped2"' INT TERM HUP

# ijawk <mode> <file>: mode 'count' prints the number of unpatched and of patched cases found
# once (each of the four must be found once), or "duplicate" if a case is found more than once, 'apply' prints the patched file, and 'strip' prints the
# file without the lines of the cases (patched or not), to check that nothing else changed. Lines
# are compared without their leading and trailing blanks; 'apply' keeps the indentation.
ijawk() {
  awk -v mode="$1" '
    BEGIN {
      put = "cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img."
      p = "case 2 : // Periodic"; pn = "case 2 : { // Periodic"
      n = "case 1 : // Neumann"; nn = "case 1 : { // Neumann"
      dp = "const int my = cimg::mod(y,img.height()), mz = cimg::mod(z,img.depth());"
      dn = "const int cy = cimg::cut(y,0,img.height() - 1), cz = cimg::cut(z,0,img.depth() - 1);"
      h[1] = p; f[1] = "_cubic_atX_p"; d[1] = dp; a[1] = "my,mz"
      h[2] = n; f[2] = "_cubic_atX"; d[2] = dn; a[2] = "cy,cz"
      h[3] = p; f[3] = "_linear_atX_p"; d[3] = dp; a[3] = "my,mz"
      h[4] = n; f[4] = "_linear_atX"; d[4] = dn; a[4] = "cy,cz"
      hn[1] = pn; hn[2] = nn; hn[3] = pn; hn[4] = nn
      for (k = 1; k <= 4; k++) {
        bo[k] = put f[k] "((float)x,y,z,c);"
        bn[k] = put f[k] "((float)x," a[k] ",c);"
      }
    }
    { r[NR] = $0; t = $0; gsub(/^[ \t]+|[ \t\r]+$/, "", t); s[NR] = t }
    END {
      for (i = 1; i <= NR; i++) {
        for (k = 1; k <= 4; k++) {
          if (s[i] == h[k] && s[i + 1] == bo[k] && s[i + 2] == "break;") { old[k]++; ko[i] = k }
          if (s[i] == hn[k] && s[i + 1] == d[k] && s[i + 2] == bn[k] && s[i + 3] == "} break;") { new[k]++; kn[i] = k }
        }
      }
      if (mode == "count") {
        no = nn2 = 0
        for (k = 1; k <= 4; k++) { if (old[k] > 1 || new[k] > 1) { print "duplicate"; exit }
                                   if (old[k] == 1) no++; if (new[k] == 1) nn2++ }
        print no " " nn2; exit
      }
      for (i = 1; i <= NR; i++) {
        if (i in ko) {
          if (mode == "apply") {
            k = ko[i]; ih = r[i]; sub(/[^ \t].*$/, "", ih); ib = r[i + 1]; sub(/[^ \t].*$/, "", ib)
            print ih hn[k]; print ib d[k]; print ib bn[k]; print ih "} break;"
          }
          i += 2; continue
        }
        if (mode == "strip" && (i in kn)) { i += 3; continue }
        print r[i]
      }
    }' "$2"
}

case "$f" in
  *CImg.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"

counts=$(ijawk count "$f") || fail "awk failed"
# already patched, or fixed upstream as in GreycLab/CImg#499: nothing to do
if [ "$counts" = "0 4" ]; then
  echo "$f: already patched (GreycLab/CImg#498)"
  exit 0
fi
# the code to patch must be found exactly
[ "$counts" = "4 0" ] || fail "expected the four periodic and Neumann cases of linear and cubic interpolation in _mp_IJxyz1(), once each and either all unpatched or all patched (found: $counts; otherwise the numbers of unpatched and patched cases)"

ijawk apply "$f" > "$f.patched" || fail "awk failed"
[ "$(ijawk count "$f.patched")" = "0 4" ] || fail "the patch did not apply"
[ "$(wc -l < "$f.patched")" -eq "$(($(wc -l < "$f") + 4))" ] || fail "the patch did not add 4 lines"
# the rest of the file must be unchanged (no diff: MSYS2 does not install it by default)
ijawk strip "$f" > "$f.stripped1" || fail "awk failed"
ijawk strip "$f.patched" > "$f.stripped2" || fail "awk failed"
[ "$(wc -l < "$f.stripped1")" -eq "$(wc -l < "$f.stripped2")" ] || fail "the patch changed other lines"
changed=$(awk 'NR == FNR { a[FNR] = $0; next } $0 != a[FNR] { n++ } END { print n + 0 }' "$f.stripped1" "$f.stripped2")
[ "$changed" = 0 ] || fail "the patch changed $changed other lines"
rm -f "$f.stripped1" "$f.stripped2"
mv "$f.patched" "$f"
echo "$f: patched for out-of-bounds reads in I()/J() with periodic or Neumann boundaries (GreycLab/CImg#498)"
