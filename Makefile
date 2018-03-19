SUBDIRS = GMIC_OFX

GITSOURCES = CImg.h gmic.cpp gmic.h gmic_stdlib.h gmic_libc.cpp gmic_libc.h gmic_stdlib_gmic.h

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
	rm $(GITSOURCES)

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

sources: $(GITSOURCES)

# CImg
# commit 88fab6de7bfc141a1f577e3cf1b17b9fb1e4f438 is CImg 2.2.1
CIMGVERSION=88fab6de7bfc141a1f577e3cf1b17b9fb1e4f438

CImg.h:
	curl -s -S -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/$@

# G'MIC
# commit a1ec46d32e6206f693d02b50353cd08695997681 is GMIC 2.2.1
GMICVERSION=e5c0bc48e05f9650cce7873072bf29fc0a441c69

gmic.cpp: CImg.h gmic.h gmic_stdlib.h
	curl -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic/$(GMICVERSION)/src/$@

gmic.h:
	curl -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic/$(GMICVERSION)/src/$@

# get the latest gmic_stdlib.h

gmic_stdlib.h:
	curl -s -S -o $@ http://gmic.eu/gmic_stdlib.h

# gmic-community
# commit 249ac90ac824eb99d233d8738f95658e92f270c9 is 20 dec 2017
GMICCOMMUNITYVERSION=249ac90ac824eb99d233d8738f95658e92f270c9

gmic_libc.cpp: gmic_libc.h gmic_stdlib_gmic.h
	curl -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic-community/$(GMICCOMMUNITYVERSION)/libcgmic/$@

gmic_libc.h:
	curl -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic-community/$(GMICCOMMUNITYVERSION)/libcgmic/$@

gmic_stdlib_gmic.h:
	curl -s -S -o $@ https://raw.githubusercontent.com/dtschump/gmic-community/$(GMICCOMMUNITYVERSION)/libcgmic/$@
