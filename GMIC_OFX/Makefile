PLUGINNAME = GMIC
PLUGINSOURCES = gmic.cpp gmic_plugin.cpp gmic_plugin_call.cpp gmic_parser.cpp RFX_FileUtils.cpp RFX_StringUtils.cpp
PLUGINOBJECTS = $(PLUGINSOURCES:.cpp=.o)

SRCDIR = ..
PATHTOROOT = $(SRCDIR)/openfx/Examples
include $(PATHTOROOT)/Makefile.master
# LICENSE (GPL or COMMERCIAL) and FFT (fftw3 or pocketfft): see license.mk
include $(SRCDIR)/license.mk

# the licences of the plug-in and G'MIC (COPYING: dual CeCILL-C / CeCILL v2; COMMERCIAL builds use
# CeCILL-C), and a note on the LGPL libraries that libcurl uses (bundled on macOS and Windows)
RESOURCES = GMIC.png $(SRCDIR)/COPYING $(SRCDIR)/Licence_CeCILL-C_V1-en.txt $(SRCDIR)/Licence_CeCILL_V2-en.txt \
  $(SRCDIR)/LGPL-libraries.txt
# COMMERCIAL: the CeCILL-B licence of the community files without a licence in their header
# (gmic-community's include/COPYING, see gmic_stdlib_commercial.txt)
ifeq ($(LICENSE),COMMERCIAL)
RESOURCES += $(SRCDIR)/Licence_CeCILL-B_V1-en.txt
endif
# pocketfft's licence (BSD-3-Clause), which asks for its notice in binary distributions
ifeq ($(FFT),pocketfft)
RESOURCES += $(SRCDIR)/Licence_pocketfft.txt
endif

# LICENSE, FFT and OPENMP change the compiled code and the bundled files: when they change, rebuild
# the objects and the bundle (whose Contents/Libraries and Resources would keep stale files).
BUILD_KEY = LICENSE=$(LICENSE) FFT=$(FFT) OPENMP=$(OPENMP)
$(OBJECTPATH)/build.license: gmic_sources
	@mkdir -p $(OBJECTPATH)
	@echo '$(BUILD_KEY)' | cmp -s - $@ || { echo '$(BUILD_KEY)' > $@; rm -rf $(OBJECTPATH)/$(PLUGINNAME).ofx.bundle; }
$(addprefix $(OBJECTPATH)/,$(PLUGINOBJECTS)): $(OBJECTPATH)/build.license

# The G'MIC sources, gmic_hotfixes.h, gmic_stdlib_community.h (generated for COMMERCIAL builds),
# pocketfft and its licence are made by the top-level Makefile: let it update them also when make
# is run in this directory, in one sub-make (so that two of them never generate the same file).
.PHONY: gmic_sources
gmic_sources:
	$(MAKE) -C $(SRCDIR) sources
$(SRCDIR)/gmic.h $(SRCDIR)/gmic.cpp $(SRCDIR)/CImg.h $(SRCDIR)/gmic_libc.h $(SRCDIR)/gmic_hotfixes.h \
  $(SRCDIR)/gmic_stdlib_community.h $(SRCDIR)/pocketfft_hdronly.h $(SRCDIR)/Licence_pocketfft.txt: gmic_sources ;
$(OBJECTPATH)/gmic_plugin.o: $(SRCDIR)/gmic_hotfixes.h
# COMMERCIAL: check that gmic_stdlib_community.h is the generated one (gmic_stdlib_commercial.sh)
ifeq ($(LICENSE),COMMERCIAL)
$(OBJECTPATH)/gmic.o: $(OBJECTPATH)/gmic_stdlib.checked
$(OBJECTPATH)/gmic_stdlib.checked: $(SRCDIR)/gmic_stdlib_community.h
	@grep -q '^ #  openfx-gmic : COMMERCIAL standard library' $< || { echo "Error: $< is not the COMMERCIAL standard library: run make LICENSE=COMMERCIAL in $(abspath $(SRCDIR))"; exit 1; }
	@mkdir -p $(OBJECTPATH) && touch $@
endif

# Makefile.master's rule only depends on the .cpp: rebuild the objects when the G'MIC headers
# change, e.g. re-extracted with a new GMICVER or a changed patch (patches/), so that no object
# keeps inline CImg code compiled from the old headers.
GMIC_HEADERS = $(SRCDIR)/gmic.h $(SRCDIR)/CImg.h
ifeq ($(FFT),pocketfft)
GMIC_HEADERS += $(SRCDIR)/pocketfft_hdronly.h
endif
$(OBJECTPATH)/gmic.o: $(GMIC_HEADERS) $(SRCDIR)/gmic_stdlib_community.h
$(OBJECTPATH)/gmic_plugin_call.o: $(GMIC_HEADERS) $(SRCDIR)/gmic_libc.h $(SRCDIR)/gmic_plugin_call.h
$(OBJECTPATH)/gmic_plugin.o: $(SRCDIR)/gmic_libc.h $(SRCDIR)/gmic_plugin_call.h

# we need zlib to decompress the G'MIC standard library, and an FFT library (FFT) for the effects that use the FFT
CXXFLAGS += \
-DOFX_EXTENSIONS_NATRON \
-DOFX_EXTENSIONS_NUKE \
-DOFX_PLUGIN \
-I$(SRCDIR) \
-I$(SRCDIR)/Helpers \
-I$(SRCDIR)/openfx/include \
-I$(SRCDIR)/openfx/Examples/include \
-Dgmic_core \
-Dgmic_is_parallel \
-Dcimg_use_abort \
-Dcimg_display=0 \
-Dcimg_appname=\"gmic\" \
-Dcimg_verbosity=0
# G'MIC >= 3 compiles CImg in its own namespace (gmic_library, with CImg renamed gmic_image), so
# it cannot clash with the cimg_library of other plug-ins (e.g. openfx-misc) loaded in the same host.

# FFT library, for Fourier transforms of any image size (CImg's own FFT fails on sizes that are
# not powers of two), see license.mk:
ifeq ($(FFT),fftw3)
# FFTW (license: GPLv2), multithreaded
# fftw_make_planner_thread_safe (see gmic_plugin.cpp) needs FFTW 3.3.5 (not checked for clean)
ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
ifeq ($(shell pkg-config --atleast-version=3.3.5 fftw3 && echo ok),)
  $(error FFT=fftw3 needs FFTW >= 3.3.5 (pkg-config fftw3))
endif
endif
FFT_CXXFLAGS = `pkg-config --cflags fftw3` -Dcimg_use_fftw3
FFT_LINKFLAGS = `pkg-config --libs fftw3` -lfftw3_threads
endif
ifeq ($(FFT),pocketfft)
# pocketfft (BSD-3-Clause), header-only, multithreaded (patches/cimg-pocketfft.sh)
FFT_CXXFLAGS = -Dcimg_use_pocketfft
endif
# COMMERCIAL: the parser does not report curated filters that its standard library leaves out
ifeq ($(LICENSE),COMMERCIAL)
CXXFLAGS += -DOFX_GMIC_COMMERCIAL
endif

# Zlib, necessary to uncompress the glic stdlib
ZLIB_CXXFLAGS=`pkg-config --cflags zlib` -Dcimg_use_zlib
ZLIB_LINKFLAGS=`pkg-config --libs zlib`

# libcurl, necessary to download presets:
LIBCURL_CXXFLAGS=`pkg-config --cflags libcurl` -Dcimg_use_curl
LIBCURL_LINKFLAGS=`pkg-config --libs libcurl`

# libpng, necessary for HaldCLUTs:
LIBPNG_CXXFLAGS=`pkg-config --cflags libpng` -Dcimg_use_png
LIBPNG_LINKFLAGS=`pkg-config --libs libpng`

CXXFLAGS += $(FFT_CXXFLAGS) $(ZLIB_CXXFLAGS) $(LIBCURL_CXXFLAGS) $(LIBPNG_CXXFLAGS)
LINKFLAGS += $(FFT_LINKFLAGS) $(ZLIB_LINKFLAGS) $(LIBCURL_LINKFLAGS) $(LIBPNG_LINKFLAGS)

VPATH = $(SRCDIR):$(SRCDIR)/Helpers

# C++ standard: by default none is added, so the compiler's default applies (gnu++17 with current
# compilers), or a -std option given in CXX by the caller (as Natron's build does). Pass e.g.
# CXXSTD=c++20 to choose one: it comes after CXX on the compile line, so it overrides a -std in CXX.
ifneq ($(CXXSTD),)
  CXXFLAGS += -std=$(CXXSTD)
endif

# openmp
ifeq ($(OPENMP),1)
  ifeq ($(OS),Darwin) # OS is set by Makefile.master
    # Apple clang has no -fopenmp driver support: use the OpenMP runtime from MacPorts (libomp)
    LIBOMP_PREFIX ?= /opt/local
    CXXFLAGS += -Dcimg_use_openmp -Xpreprocessor -fopenmp -I$(LIBOMP_PREFIX)/include/libomp
    LINKFLAGS += -L$(LIBOMP_PREFIX)/lib/libomp -lomp
  else
    CXXFLAGS += -Dcimg_use_openmp -fopenmp
    LINKFLAGS += -fopenmp
  endif
endif

# G'MIC needs NaN and infinities (nan, inf and isnan() in its math parser, points removed as
# nan,nan...), which -ffinite-math-only (in -ffast-math and -Ofast) assumes never occur: keep
# them with such an OPTFLAG, CXX or CXXFLAGS_EXTRA, which come before on the compile line.
CXXFLAGS += -fno-finite-math-only
