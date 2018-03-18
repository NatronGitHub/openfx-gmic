PLUGINNAME = GMIC
PLUGINSOURCES = gmic.cpp gmic_libc.cpp gmic_openfx.cpp gmic_parser.cpp RFX_StringUtils.cpp
PLUGINOBJECTS = $(PLUGINSOURCES:.cpp=.o)

SRCDIR = ..
PATHTOROOT = $(SRCDIR)/openfx/Examples
include $(PATHTOROOT)/Makefile.master

RESOURCES = GMIC.png

CXXFLAGS += \
-I$(SRCDIR)/Common \
-I$(SRCDIR)/openfx/include \
-I$(SRCDIR)/openfx/Examples/include \
-Dgmic_build -Dgmic_is_parallel -Dcimg_use_abort -Dcimg_display=0 -Dcimg_appname=\"gmic\" -Dcimg_namespace_suffix=openfx_gmic -Dcimg_verbosity=0 -Dcimg_library=cimg_library_openfx_gmic

VPATH = $(SRCDIR)/Common

# openmp
ifeq ($(OPENMP),1)
  CXXFLAGS += -Dcimg_use_openmp -fopenmp
  LINKFLAGS += -fopenmp
endif

# gmic_is_parallel requires POSIX threads
ifeq ($(OS),$(filter $(OS),Linux FreeBSD Darwin))
  CXXFLAGS += -Dgmic_is_parallel
endif

.PHONY: sources

sources: $(PLUGINSOURCES)

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

#            $(WGET) gmic_libc.cpp https://raw.githubusercontent.com/dtschump/gmic-community/master/libcgmic/gmic_libc.cpp; \
#            $(WGET) gmic_libc.h https://raw.githubusercontent.com/dtschump/gmic-community/master/libcgmic/gmic_libc.h; \
#            $(WGET) use_libcgmic.c https://raw.githubusercontent.com/dtschump/gmic-community/master/libcgmic/use_libcgmic.c; \

realclean:
	rm CImg.h gmic.cpp gmic.h gmic_stdlib.h
