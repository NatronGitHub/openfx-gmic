SUBDIRS = GMIC_OFX

# LICENSE (GPL or COMMERCIAL) and FFT (fftw3 or pocketfft): see license.mk
include license.mk

# All G'MIC sources come from the release tarball: since G'MIC 3, it includes the C API
# (gmic_libc.*) and the standard library with the community filters (gmic_stdlib_community.h).
# Only gmic_libc.h is used from the C API: gmic_plugin_call.cpp replaces gmic_libc.cpp.
# gmic_stdlib_community.h is generated instead for COMMERCIAL builds (see below).
GMICSOURCES = CImg.h gmic.cpp gmic.h gmic_libc.h
GMICVER=4.0.7
GMICSRC=gmic_$(GMICVER).tar.gz
GMICSRCDIR=gmic-$(GMICVER)


all: subdirs

.PHONY: sources subdirs clean install uninstall $(SUBDIRS)

nomulti:
	$(MAKE) SUBDIRS="$(SUBDIRS_NOMULTI)"

subdirs: $(SUBDIRS)

$(SUBDIRS): sources
	(cd $@ && $(MAKE))

clean:
	@for i in $(SUBDIRS) $(SUBDIRS_NOMULTI); do \
	  echo "(cd $$i && $(MAKE) $@)"; \
	  (cd $$i && $(MAKE) $@); \
	done

install:
	@for i in $(SUBDIRS) ; do \
	  echo "(cd $$i && $(MAKE) $@)"; \
	  (cd $$i && $(MAKE) $@); \
	done

uninstall:
	@for i in $(SUBDIRS) ; do \
	  echo "(cd $$i && $(MAKE) $@)"; \
	  (cd $$i && $(MAKE) $@); \
	done

# gmic_sources.license records the LICENSE (first line) and FFT of the sources: it is only written
# once they are all made, and removed when LICENSE or FFT change.
sources: $(GMICSOURCES) gmic_stdlib_community.h gmic_hotfixes.h $(if $(filter pocketfft,$(FFT)),pocketfft_hdronly.h Licence_pocketfft.txt)
	@printf '%s\nFFT=%s\n' '$(LICENSE)' '$(FFT)' | cmp -s - gmic_sources.license || \
	  { printf '%s\nFFT=%s\n' '$(LICENSE)' '$(FFT)' > gmic_sources.license.tmp && mv gmic_sources.license.tmp gmic_sources.license; }

# The LICENSE and FFT the sources are made for (CImg.h is only patched for pocketfft;
# gmic_stdlib_community.h and gmic_hotfixes.h depend on LICENSE): they are made again when these
# change.
gmic_sources.request: gmic_hotfixes_force
	@printf '%s %s\n' '$(LICENSE)' '$(FFT)' | cmp -s - $@ || \
	  { rm -f gmic_sources.license; printf '%s %s\n' '$(LICENSE)' '$(FFT)' > $@; }

# Hot fixes for G'MIC bugs, embedded into the plug-in as a null-terminated byte array (no string
# literal, which MSVC limits to 16 KB per piece). They were last reviewed for one G'MIC version:
# warn if GMICVER is another one, as a reminder to drop the fixes that upstream has made. This is
# not an error: the plug-in checks each fix against the built-in G'MIC definitions, applies it
# only where the bug is still there, and warns (failing the CI describe test) where the code
# changed otherwise (see the top of gmic_hotfixes.gmic). The gmic_hotfixes.version stamp holds
# GMICVER, so that 'make GMICVER=...' also redoes the check. Carriage returns (Windows checkouts)
# are removed.
gmic_hotfixes.version: gmic_hotfixes_force
	@echo '$(GMICVER)' | cmp -s - $@ || echo '$(GMICVER)' > $@

.PHONY: gmic_hotfixes_force
gmic_hotfixes_force:

# COMMERCIAL builds only keep the fixes of commands of their standard library (the others fix
# code under a licence that these builds leave out, see gmic_stdlib_commercial.txt): a block is
# left out when its command, or one of its callees, is not in gmic_stdlib_community.h.commands
# (in "#@hotfix <command> <original> <fixed> [callee:checksum...]", fields 2 and 5 onwards).
ifeq ($(LICENSE),COMMERCIAL)
HOTFIXES_FILTER = awk -v cmds=gmic_stdlib_community.h.commands ' \
  BEGIN { while ((getline c < cmds) > 0) def[c] = 1; keep = 1 } \
  /^\#@hotfix / { keep = 1; for (i = 2; i <= NF; i++) if (i == 2 || i > 4) { c = $$i; sub(/:.*/, "", c); if (!(c in def)) keep = 0 } \
    if (!keep) print "gmic_hotfixes.h: leaving out the fix of " $$2 " (not in the COMMERCIAL standard library)" > "/dev/stderr" } \
  keep { print }'
else
HOTFIXES_FILTER = cat
endif
gmic_hotfixes.h: gmic_hotfixes.gmic gmic_hotfixes.version gmic_sources.request $(if $(filter COMMERCIAL,$(LICENSE)),gmic_stdlib_community.h)
	@if ! tr -d '\r' < $< | grep -qxF '#@openfx_gmic_hotfixes $(GMICVER)'; then \
	  echo "Warning: $< was reviewed for another G'MIC version than $(GMICVER) (no '#@openfx_gmic_hotfixes $(GMICVER)' line): drop the fixes that G'MIC $(GMICVER) has, and run the describe test, see the top of $<"; \
	fi
	{ echo '// Generated from $< by the Makefile ($(LICENSE)): do not edit.'; \
	  echo 'static const unsigned char gmic_hotfixes[] = {'; \
	  tr -d '\r' < $< | $(HOTFIXES_FILTER) | od -An -v -tx1 | sed -e 's/ *\([0-9a-f][0-9a-f]\)/0x\1,/g'; \
	  echo '0x00 };'; } > $@.tmp && mv $@.tmp $@

# gmic.eu may refuse downloads (HTTP 403 seen in 2026): try each mirror in turn, use curl -f so
# that an HTTP error page is never saved as the tarball, and verify the checksum.
GMICSRC_SHA256_2.8.4=b2a279bdf981595b30d8d7828fabf9a620cde06eae8db345a8cddf2160c25379
GMICSRC_SHA256_4.0.7=f84b215f40cb00de37bff5ec801103af211679794c3602d39c5430dab76005de
GMICSRC_SHA256=$(GMICSRC_SHA256_$(GMICVER))
# GreycLab/gmic on GitHub is the main G'MIC repository, and its releases (3.3.1 and later) attach
# gmic_<ver>.tar.gz: try it first, then gmic.eu (which may answer 403) and the MacPorts mirror.
GMICSRC_URLS=https://github.com/GreycLab/gmic/releases/download/v.$(GMICVER)/$(GMICSRC) https://gmic.eu/files/source/$(GMICSRC) https://distfiles.macports.org/gmic/$(GMICSRC)
# CMakeLists.txt reads GMICSOURCES, GMICVER, GMICSRC, GMICSRCDIR, GMICSRC_SHA256_<version>,
# GMICSRC_URLS, POCKETFFTCOMMIT and POCKETFFT_SHA256 from this file: keep each on one line, as a
# plain "NAME = value" assignment (no backslash continuation, not inside ifeq), using no other
# variables than $(GMICVER) and $(GMICSRC).

# The checksum is mandatory: when bumping GMICVER, add GMICSRC_SHA256_<version>
# (or pass GMICSRC_SHA256=<sha256> on the make command line).
$(GMICSRC):
	@if [ -z "$(GMICSRC_SHA256)" ]; then \
	  echo "Error: no sha256 known for $(GMICSRC): add GMICSRC_SHA256_$(GMICVER) to the Makefile"; exit 1; \
	fi; \
	for url in $(GMICSRC_URLS); do \
	  rm -f $@.tmp; \
	  echo "Fetching $$url"; \
	  if curl -f --retry 5 --retry-all-errors -L -s -S -o $@.tmp "$$url" && gzip -t $@.tmp 2>/dev/null; then \
	    if command -v sha256sum >/dev/null 2>&1; then sum=`sha256sum $@.tmp | cut -d' ' -f1`; \
	    else sum=`shasum -a 256 $@.tmp | cut -d' ' -f1`; fi; \
	    if [ "$$sum" = "$(GMICSRC_SHA256)" ]; then \
	      if mv $@.tmp $@; then exit 0; fi; \
	      echo "Error: could not install $@"; break; \
	    fi; \
	    echo "Warning: checksum mismatch for $$url (got $$sum)"; \
	  fi; \
	done; \
	rm -f $@.tmp; echo "Error: could not download a valid $(GMICSRC)"; exit 1

# tar -m: the extracted files get the current time, not the (older) one from the tarball, so that
# they are newer than the tarball (not extracted again by every make), and than the objects
# compiled from older sources (GMIC_OFX/Makefile rebuilds them when the G'MIC headers change).
$(GMICSOURCES): $(GMICSRC)
	tar -m --strip-components 2 -zxvf $< $(patsubst %,$(GMICSRCDIR)/src/%, $@)
	@case $@ in gmic.h|gmic.cpp) sh patches/gmic-apple-abort.sh $@ || { rm -f $@; exit 1; };; esac
	@case $@ in CImg.h) for p in $(CIMG_PATCHES); do sh $$p $@ || { rm -f $@; exit 1; }; done;; esac

# G'MIC's standard library with the community filters: from the tarball (GPL builds), or, for
# COMMERCIAL builds, generated from the G'MIC standard library and the community files whose
# licence allows it (see gmic_stdlib_commercial.txt), with a G'MIC interpreter built from the
# tarball (a few minutes). gmic_stdlib_community.h.commands lists the commands it defines.
gmic_stdlib_community.h: $(GMICSRC) gmic_sources.request $(if $(filter COMMERCIAL,$(LICENSE)),gmic_stdlib_commercial.sh gmic_stdlib_commercial.txt)
ifeq ($(LICENSE),COMMERCIAL)
	CXX="$(CXX)" ZLIB_CFLAGS="`pkg-config --cflags zlib`" ZLIB_LIBS="`pkg-config --libs zlib`" \
	  sh gmic_stdlib_commercial.sh $(GMICSRC) $(GMICVER) $@
else
	rm -f $@.commands
	tar -m --strip-components 2 -zxf $< -O $(GMICSRCDIR)/src/$@ > $@.tmp && mv $@.tmp $@
endif

# pocketfft (FFT=pocketfft), C++ header-only version, at a pinned commit of its 'cpp' branch
POCKETFFTCOMMIT=4d865cba24359b05b37da4e75aee24498c3b78e7
POCKETFFT_SHA256=518a95c552aeaee352aa30122137cd9e135d23c91e2863d9272aaa758744c86d
pocketfft_hdronly.h:
	curl -f --retry 5 --retry-all-errors -L -s -S -o $@.tmp https://raw.githubusercontent.com/mreineck/pocketfft/$(POCKETFFTCOMMIT)/pocketfft_hdronly.h
	@if command -v sha256sum >/dev/null 2>&1; then sum=`sha256sum $@.tmp | cut -d' ' -f1`; \
	else sum=`shasum -a 256 $@.tmp | cut -d' ' -f1`; fi; \
	if [ "$$sum" != "$(POCKETFFT_SHA256)" ]; then rm -f $@.tmp; echo "Error: checksum mismatch for $@ (got $$sum)"; exit 1; fi
	mv $@.tmp $@
# its licence (BSD-3-Clause, with the copyright notices), from the header's first comment
Licence_pocketfft.txt: pocketfft_hdronly.h
	awk 'NR == 1 && $$0 != "/*" { exit 1 } NR > 1 && $$0 == "*/" { done = 1; exit } NR > 1 { print }  END { if (!done) exit 1 }' $< > $@.tmp
	grep -q 'Redistributions in binary form must reproduce' $@.tmp
	mv $@.tmp $@

# Local patches to the extracted G'MIC sources (re-extracted when a patch changes):
# - gmic-apple-abort.sh: enable G'MIC's abort support on macOS too, without which an error inside
#   an OpenMP loop terminates the host (https://github.com/NatronGitHub/openfx-gmic/issues/41,
#   reported upstream as https://github.com/GreycLab/gmic/issues/219)
# - cimg-math-macro-overread.sh: the math parser reads past the end of a macro body ending with an
#   argument name (https://github.com/GreycLab/CImg/issues/496, fixed after G'MIC 4.0.7)
# - cimg-omp-rand.sh: the random numbers of rand, noise and the math parser in OpenMP loops do not
#   depend on thread scheduling, so that most seeded filters give the same result at each render
#   (https://github.com/NatronGitHub/openfx-gmic/issues/44)
# - cimg-ij-boundary.sh: I()/J() read out of bounds with linear or cubic interpolation and periodic
#   or Neumann boundaries when y or z is outside the image, which crashes "Spiral Distortion" on
#   small inputs (https://github.com/GreycLab/CImg/issues/498, fix proposed in GreycLab/CImg#499)
# - cimg-symmetric-eigen.sh: eigen and eig() return NaN eigenvectors for rank-deficient symmetric
#   matrices, e.g. in Smooth [Patch-PCA] (https://github.com/GreycLab/gmic/issues/221,
#   https://github.com/NatronGitHub/openfx-gmic/issues/71, fixed after G'MIC 4.0.7)
# - cimg-pocketfft.sh: let CImg compute its FFTs with pocketfft (cimg_use_pocketfft), which
#   COMMERCIAL builds use instead of FFTW (only applied for FFT=pocketfft)
gmic.h gmic.cpp: patches/gmic-apple-abort.sh
CIMG_PATCHES = patches/cimg-math-macro-overread.sh patches/cimg-omp-rand.sh patches/cimg-ij-boundary.sh patches/cimg-symmetric-eigen.sh
ifeq ($(FFT),pocketfft)
CIMG_PATCHES += patches/cimg-pocketfft.sh
endif
CImg.h: gmic_sources.request $(CIMG_PATCHES)

distclean: clean
	-rm -f $(GMICSRC) $(GMICSOURCES) gmic_stdlib_community.h gmic_stdlib_community.h.commands gmic_hotfixes.h gmic_hotfixes.version gmic_sources.license gmic_sources.request pocketfft_hdronly.h Licence_pocketfft.txt gmic_libc.cpp gmic_stdlib_gmic.h # the last two: left by older builds
	-rm -rf gmic_stdlib_commercial
