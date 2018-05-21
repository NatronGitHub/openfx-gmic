SUBDIRS = GMIC_OFX

GMICSOURCES = CImg.h gmic.cpp gmic.h gmic_stdlib.h
GMICVER=2.2.3
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
	-rm $(GMICSRC) $(GMICSOURCES) $(GITSOURCES)

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

# # CImg
# # commit 88fab6de7bfc141a1f577e3cf1b17b9fb1e4f438 is CImg 2.2.1
# # commit 8761ce483600ccbbea05a14b18d916744f3580af is CImg 2.2.2
# # commit fcef55e14b1d84513f024c6c0dba9311f4b91272 is CImg 2.2.3
# CIMGVERSION=fcef55e14b1d84513f024c6c0dba9311f4b91272

# CImg.h:
# 	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/$@

# # G'MIC
# # commit a1ec46d32e6206f693d02b50353cd08695997681 is GMIC 2.2.1
# # commit 7fbac10ed68a7a971ecf50e9f1a0baa47837a245 is GMIC 2.2.2
# # commit 1d10367db113c16d8eea66625f99a720289ad3d4 is GMIC 2.2.3
# GMICVERSION=1d10367db113c16d8eea66625f99a720289ad3d4

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
GMICCOMMUNITYVERSION=09c9f56ba2fe02568667d0cf03c132c828c67bef

gmic_libc.cpp: gmic_libc.h gmic_stdlib_gmic.h
	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic-community/$(GMICCOMMUNITYVERSION)/libcgmic/$@

gmic_libc.h:
	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic-community/$(GMICCOMMUNITYVERSION)/libcgmic/$@

gmic_stdlib_gmic.h:
	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic-community/$(GMICCOMMUNITYVERSION)/libcgmic/$@
