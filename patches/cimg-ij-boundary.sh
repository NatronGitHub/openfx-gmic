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
# Each of the four 3-line cases below becomes a 4-line block, as in GreycLab/CImg#499. Upstream
# fixed it differently after G'MIC 4.0.7, in GreycLab/CImg 41e121c (develop,
# https://github.com/GreycLab/CImg/commit/41e121cf9047910a30d5f0336ca0edc0d8ed2473): the four
# cases are unchanged, but _mp_IJxyz1() first brings z, then y, back inside the image for every
# boundary condition. The script fails if the source does not contain the expected code (e.g.
# after a GMICVER bump): then check whether upstream fixed it, and update or remove this patch.
# Running it again on a patched file does nothing, and so does running it on a CImg.h that already
# has the fix of GreycLab/CImg#499 or of GreycLab/CImg 41e121c (the z and y blocks of that commit
# found once, followed in the same function by the four unchanged cases).
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
# once (each of the four must be found once), then 1 if the four unpatched cases follow the z and
# y blocks of GreycLab/CImg 41e121c in the same function ("partial" if that function has only
# some of their lines, else 0), or "duplicate" if a case or those blocks are found more than once, 'apply' prints the patched file, and 'strip' prints the
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
      # the fix of GreycLab/CImg 41e121c: z, then y, brought back inside the image
      nu = split("z depth d2 mz|y height h2 my", ax, "|")
      m = 0
      for (k = 1; k <= nu; k++) {
        split(ax[k], v, " ")
        u[++m] = "if (" v[1] "<0 || " v[1] ">=img." v[2] "()) switch (boundary_conditions) { // Out-of-bound access along " v[1]
        u[++m] = "case 0 : std::memset(ptrd,0,siz*sizeof(double)); return cimg::type<double>::nan(); // Dirichlet"
        u[++m] = "case 1 : " v[1] " = cimg::cut(" v[1] ",0,img." v[2] "() - 1); break; // Neumann"
        u[++m] = "case 2 : " v[1] " = cimg::mod(" v[1] ",img." v[2] "()); break; // Periodic"
        u[++m] = "default : { // Mirror"
        u[++m] = "const int " v[3] " = 2*img." v[2] "(), " v[4] " = cimg::mod(" v[1] "," v[3] ");"
        u[++m] = v[1] " = " v[4] "<img." v[2] "()?" v[4] ":" v[3] " - " v[4] " - 1;"
        u[++m] = "}"
        u[++m] = "}"
        if (k < nu) u[++m] = ""
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
        no = nn2 = nup = 0
        for (k = 1; k <= 4; k++) { if (old[k] > 1 || new[k] > 1) { print "duplicate"; exit }
                                   if (old[k] == 1) no++; if (new[k] == 1) nn2++ }
        for (i = 1; i + m - 1 <= NR; i++) {
          for (j = 1; j <= m && s[i + j - 1] == u[j]; j++) ;
          if (j > m) { nup++; ub = i }
        }
        if (nup > 1) { print "duplicate"; exit }
        # with the four unpatched cases in one function: 1 if these blocks come before them in that
        # function, "partial" if only some of their lines are there
        up = 0
        if (no == 4) {
          for (i in ko) { if (!lo || i + 0 < lo) lo = i + 0; if (i + 0 > hi) hi = i + 0 }
          for (fs = lo; fs > 1 && s[fs] !~ /^static /; fs--) ;
          for (fe = lo; fe < NR && s[fe] !~ /^static /; fe++) ;
          if (nup == 1 && ub > fs && ub + m <= lo && fe > hi) up = 1
          else for (i = fs + 1; i < fe; i++) if (s[i] ~ /Out-of-bound access along/) up = "partial"
        }
        print no " " nn2 " " up; exit
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
if [ "$counts" = "0 4 0" ]; then
  echo "$f: already patched (GreycLab/CImg#498)"
  exit 0
fi
# fixed upstream as in GreycLab/CImg 41e121c: nothing to do
if [ "$counts" = "4 0 1" ]; then
  echo "$f: already fixed upstream (GreycLab/CImg#498, GreycLab/CImg 41e121c)"
  exit 0
fi
# the code to patch must be found exactly
[ "$counts" = "4 0 0" ] || fail "expected the four periodic and Neumann cases of linear and cubic interpolation in _mp_IJxyz1(), once each and either all unpatched or all patched, or unpatched after the z and y blocks of GreycLab/CImg 41e121c (found: $counts; otherwise the numbers of unpatched and patched cases, then 1 if fixed as in GreycLab/CImg 41e121c)"

ijawk apply "$f" > "$f.patched" || fail "awk failed"
[ "$(ijawk count "$f.patched")" = "0 4 0" ] || fail "the patch did not apply"
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
