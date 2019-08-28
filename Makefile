SUBDIRS = GMIC_OFX

GMICSOURCES = CImg.h gmic.cpp gmic.h gmic_stdlib.h
GMICVER=2.7.0
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

$(GMICSRC):
	curl -L -s -S -o $@ http://gmic.eu/files/source/$(GMICSRC)

$(GMICSOURCES): $(GMICSRC)
	tar --strip-components 2 -zxvf $< $(patsubst %,$(GMICSRCDIR)/src/%, $@)

distclean: clean
	-rm $(GMICSRC) $(GMICSOURCES) $(GITSOURCES)

# # CImg
# # commit f510b430acfa3194ef935a189346ddda012dc68e is CImg 2.2.3+
# CIMGVERSION=f510b430acfa3194ef935a189346ddda012dc68e

# CImg.h:
# 	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/$@

# # G'MIC
# # commit fec116d3380454f2667322fa196fb3bdb246acb9 is GMIC 2.2.3+
# GMICVERSION=fec116d3380454f2667322fa196fb3bdb246acb9

# gmic.cpp: CImg.h gmic.h gmic_stdlib.h
# 	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic/$(GMICVERSION)/src/$@

# gmic.h:
# 	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic/$(GMICVERSION)/src/$@

# # get the latest gmic_stdlib.h

# gmic_stdlib.h:
# 	curl -L -s -S -o $@ http://gmic.eu/gmic_stdlib.h

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
GMICCOMMUNITYVERSION=91e4cf77ae4ff9885152d468d080c8d557902241

gmic_libc.cpp: gmic_libc.h gmic_stdlib_gmic.h
	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic-community/$(GMICCOMMUNITYVERSION)/libcgmic/$@

gmic_libc.h:
	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic-community/$(GMICCOMMUNITYVERSION)/libcgmic/$@

gmic_stdlib_gmic.h:
	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic-community/$(GMICCOMMUNITYVERSION)/libcgmic/$@
