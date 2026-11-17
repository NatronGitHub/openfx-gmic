SUBDIRS = GMIC_OFX

# All G'MIC sources come from the release tarball: since G'MIC 3, it includes the C API
# (gmic_libc.*) and the standard library with the community filters (gmic_stdlib_community.h).
# Only gmic_libc.h is used from the C API: gmic_plugin_call.cpp replaces gmic_libc.cpp.
GMICSOURCES = CImg.h gmic.cpp gmic.h gmic_stdlib_community.h gmic_libc.h
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

sources: $(GMICSOURCES) gmic_hotfixes.h

# Hot fixes for G'MIC bugs, embedded into the plug-in as a null-terminated byte array (no string
# literal, which MSVC limits to 16 KB per piece). They are written for one G'MIC version: fail if
# GMICVER changed, so that each fix gets reviewed (see the top of gmic_hotfixes.gmic). The
# gmic_hotfixes.version stamp holds GMICVER, so that 'make GMICVER=...' also redoes the check.
# Carriage returns (Windows checkouts) are removed.
gmic_hotfixes.version: gmic_hotfixes_force
	@echo '$(GMICVER)' | cmp -s - $@ || echo '$(GMICVER)' > $@

.PHONY: gmic_hotfixes_force
gmic_hotfixes_force:

gmic_hotfixes.h: gmic_hotfixes.gmic gmic_hotfixes.version
	@if ! tr -d '\r' < $< | grep -qxF '#@openfx_gmic_hotfixes $(GMICVER)'; then \
	  echo "Error: $< is not for G'MIC $(GMICVER) (no '#@openfx_gmic_hotfixes $(GMICVER)' line): review each fix against G'MIC $(GMICVER), see the top of $<"; exit 1; \
	fi
	{ echo '// Generated from $< by the Makefile: do not edit.'; \
	  echo 'static const unsigned char gmic_hotfixes[] = {'; \
	  tr -d '\r' < $< | od -An -v -tx1 | sed -e 's/ *\([0-9a-f][0-9a-f]\)/0x\1,/g'; \
	  echo '0x00 };'; } > $@.tmp && mv $@.tmp $@

# gmic.eu may refuse downloads (HTTP 403 seen in 2026): try each mirror in turn, use curl -f so
# that an HTTP error page is never saved as the tarball, and verify the checksum.
GMICSRC_SHA256_2.8.4=b2a279bdf981595b30d8d7828fabf9a620cde06eae8db345a8cddf2160c25379
GMICSRC_SHA256_4.0.7=f84b215f40cb00de37bff5ec801103af211679794c3602d39c5430dab76005de
GMICSRC_SHA256=$(GMICSRC_SHA256_$(GMICVER))
# GreycLab/gmic on GitHub is the main G'MIC repository, and its releases (3.3.1 and later) attach
# gmic_<ver>.tar.gz: try it first, then gmic.eu (which may answer 403) and the MacPorts mirror.
GMICSRC_URLS=https://github.com/GreycLab/gmic/releases/download/v.$(GMICVER)/$(GMICSRC) https://gmic.eu/files/source/$(GMICSRC) https://distfiles.macports.org/gmic/$(GMICSRC)

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
	@case $@ in \
	  gmic.h|gmic.cpp) sh patches/gmic-apple-abort.sh $@ || { rm -f $@; exit 1; };; \
	  CImg.h) sh patches/cimg-omp-rand.sh $@ || { rm -f $@; exit 1; };; \
	esac

# Local patches to the extracted G'MIC sources (re-extracted when a patch changes):
# - gmic-apple-abort.sh: enable G'MIC's abort support on macOS too, without which an error inside
#   an OpenMP loop terminates the host (https://github.com/NatronGitHub/openfx-gmic/issues/41,
#   reported upstream as https://github.com/GreycLab/gmic/issues/219)
# - cimg-omp-rand.sh: the random numbers of rand, noise and the math parser in OpenMP loops do not
#   depend on thread scheduling, so that most seeded filters give the same result at each render
#   (https://github.com/NatronGitHub/openfx-gmic/issues/44)
gmic.h gmic.cpp: patches/gmic-apple-abort.sh
CImg.h: patches/cimg-omp-rand.sh

distclean: clean
	-rm -f $(GMICSRC) $(GMICSOURCES) gmic_hotfixes.h gmic_hotfixes.version gmic_libc.cpp gmic_stdlib_gmic.h # the last two: left by older builds
