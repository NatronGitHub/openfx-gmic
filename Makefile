SUBDIRS = GMIC_OFX

GMICSOURCES = CImg.h gmic.cpp gmic.h gmic_stdlib.h
GMICVER=2.8.4
#GMICVER=2.9.0 # config file changed & broke plugin (shows 0 plugins)
GMICSRC=gmic_$(GMICVER).tar.gz
GMICSRCDIR=gmic-$(GMICVER)

GITSOURCES = gmic_libc.cpp gmic_libc.h gmic_stdlib_gmic.h


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

sources: $(GMICSOURCES) $(GITSOURCES)

# gmic.eu may refuse downloads (HTTP 403 seen in 2026): try each mirror in turn, use curl -f so
# that an HTTP error page is never saved as the tarball, and verify the checksum.
GMICSRC_SHA256_2.8.4=b2a279bdf981595b30d8d7828fabf9a620cde06eae8db345a8cddf2160c25379
GMICSRC_SHA256=$(GMICSRC_SHA256_$(GMICVER))
# GreycLab/gmic on GitHub is now the main G'MIC repository; its releases (3.3.1 and later)
# attach gmic_<ver>.tar.gz. It is tried last because it has no 2.x release.
GMICSRC_URLS=https://gmic.eu/files/source/$(GMICSRC) https://distfiles.macports.org/gmic/$(GMICSRC) https://github.com/GreycLab/gmic/releases/download/v.$(GMICVER)/$(GMICSRC)

$(GMICSRC):
	@for url in $(GMICSRC_URLS); do \
	  rm -f $@.tmp; \
	  echo "Fetching $$url"; \
	  if curl -f --retry 5 --retry-all-errors -L -s -S -o $@.tmp "$$url" && gzip -t $@.tmp 2>/dev/null; then \
	    if [ -z "$(GMICSRC_SHA256)" ]; then \
	      echo "Warning: no known sha256 for $(GMICSRC), not verified"; mv $@.tmp $@; exit 0; \
	    fi; \
	    if command -v sha256sum >/dev/null 2>&1; then sum=`sha256sum $@.tmp | cut -d' ' -f1`; \
	    else sum=`shasum -a 256 $@.tmp | cut -d' ' -f1`; fi; \
	    if [ "$$sum" = "$(GMICSRC_SHA256)" ]; then mv $@.tmp $@; exit 0; fi; \
	    echo "Warning: checksum mismatch for $$url"; \
	  fi; \
	done; \
	rm -f $@.tmp; echo "Error: could not download a valid $(GMICSRC)"; exit 1

$(GMICSOURCES): $(GMICSRC)
	tar --strip-components 2 -zxvf $< $(patsubst %,$(GMICSRCDIR)/src/%, $@)

distclean: clean
	-rm $(GMICSRC) $(GMICSOURCES) $(GITSOURCES)

################
# gmic-community
# commit 249ac90ac824eb99d233d8738f95658e92f270c9 is 20 dec 2017
# commit d6ef1347c0c200281122e438c0188d396445486c is 23 apr 2018
# commit 09c9f56ba2fe02568667d0cf03c132c828c67bef is 15 may 2018
# commit cb58e3713bf5c1dda100edd200fec063a9067b00 is  6 jun 2018
# commit ccfb3190aa414c4c09db643e1ea6ff446dbbd67f is 26 jun 2018
# commit 67f4417aebb7f789473121855179e3cfd5b38a20 is  9 may 2019 (gmic 2.6.2)
# commit 730aada04c09befe89190574ef7457927f83e840 if  5 jun 2019 (gmic 2.6.5)
# commit 6ce0f346dbfd9012d131bd07d30866136318204f is 20 jun 2019 (gmic 2.6.7)
# commit 91e4cf77ae4ff9885152d468d080c8d557902241 is 12 aug 2019 (gmic 2.7.0)
# commit 52a2635def99f391823222eaaf925bcbb4cc5166 is 21 sep 2019 (gmic 2.7.2)
# commit 4cb2a8aaf931c86f57de8c843ed2c9bce0657197 is 11 oct 2019 (gmic 2.7.4)
# commit 7a7e08e3a021050f4ef72d05edf163303131e03e is  5 dec 2019 (gmic 2.8.0)
# commit 09f1dda72ce37d8452ba015732a5de04b7760fa4 is 16 dec 2019 (gmic 2.8.1)
# commit 2c643575e212b5a677e481769f3fd896f6cc8ecb is 11 jan 2020 (gmic 2.8.2)
# commit 09ffcc59fcbaae0546eedeb81bc076e51f10a4fa is 21 jan 2020 (gmic 2.8.3)
# commit 6a9d0e344162af3376aee7b652721d972a070e6b is 10 feb 2020 (gmic 2.8.4)
# commit 003f70a0be5b5f6758a52edc77a129f5b2692887 is 27 mar 2020 (gmic 2.9.0)
GMICCOMMUNITYVERSION=6a9d0e344162af3376aee7b652721d972a070e6b
#GMICCOMMUNITYVERSION=003f70a0be5b5f6758a52edc77a129f5b2692887 # shows 0 plugins

GMIC_BASE_URL=https://raw.githubusercontent.com/GreycLab/gmic-community/$(GMICCOMMUNITYVERSION)/libcgmic
gmic_libc.cpp: gmic_libc.h gmic_stdlib_gmic.h
	curl --retry 5 --retry-all-errors -L -s -S -o $@ $(GMIC_BASE_URL)/$@

gmic_libc.h:
	curl --retry 5 --retry-all-errors -L -s -S -o $@ $(GMIC_BASE_URL)/$@

gmic_stdlib_gmic.h:
	curl --retry 5 --retry-all-errors -L -s -S -o $@ $(GMIC_BASE_URL)/$@
