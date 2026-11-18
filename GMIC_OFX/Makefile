PLUGINNAME = GMIC
PLUGINSOURCES = gmic.cpp gmic_plugin.cpp gmic_plugin_call.cpp gmic_parser.cpp RFX_FileUtils.cpp RFX_StringUtils.cpp
PLUGINOBJECTS = $(PLUGINSOURCES:.cpp=.o)

SRCDIR = ..
PATHTOROOT = $(SRCDIR)/openfx/Examples
include $(PATHTOROOT)/Makefile.master
# LICENSE (GPL or COMMERCIAL) and FFT (pocketfft or fftw3): see license.mk
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
# FFTW (license: GPLv2), multithreaded, opt-in (FFT=fftw3, GPL builds only)
# fftw_make_planner_thread_safe (see gmic_plugin.cpp) needs FFTW 3.3.5 (not checked for clean)
ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
ifeq ($(shell pkg-config --atleast-version=3.3.5 fftw3 && echo ok),)
  $(error FFT=fftw3 needs FFTW >= 3.3.5 (pkg-config fftw3))
endif
endif
FFT_CXXFLAGS = `pkg-config --cflags fftw3` -Dcimg_use_fftw3
FFT_MODULES = fftw3
FFT_LIBS = -lfftw3_threads
endif
ifeq ($(FFT),pocketfft)
# pocketfft (BSD-3-Clause, the default), header-only, multithreaded (patches/cimg-pocketfft.sh)
FFT_CXXFLAGS = -Dcimg_use_pocketfft
endif
# COMMERCIAL: the parser does not report curated filters that its standard library leaves out
ifeq ($(LICENSE),COMMERCIAL)
CXXFLAGS += -DOFX_GMIC_COMMERCIAL
endif

# The libraries, through pkg-config: zlib (to decompress the G'MIC standard library), libpng
# (HaldCLUTs), libcurl (downloads of filter data, except on Linux) and FFTW (FFT=fftw3).
#
# Linux: GMIC.ofx needs only system libraries (libc, libm, libpthread, libdl, librt, libstdc++,
# libgcc_s, libgomp), so that one binary runs on every distribution (Natron's release builds:
# Rocky Linux 8, glibc 2.28). No libcurl: G'MIC downloads with the external curl or wget command
# instead (cimg::load_network). zlib, libpng and FFTW are linked statically (GMIC_STATIC_DEPS=1,
# the default on Linux): their static archives (libz.a, libpng16.a, libfftw3.a,
# libfftw3_threads.a, built with -fPIC, which distributions' archives may not be: see
# .github/workflows/build_static_deps.sh) must be in pkg-config's libdir or the compiler's library
# path. GMIC_STATIC_DEPS=0 links the shared libraries, e.g. for distribution packages. The
# plug-in's version script (linuxSymbols) keeps the symbols of the static libraries local.
# Other systems: shared libraries, with libcurl (macOS and Windows bundles copy them).
DEPS_MODULES = libpng zlib $(FFT_MODULES)
ifeq ($(OS),Linux)
  GMIC_STATIC_DEPS ?= 1
  # Makefile.master links OpenGL to every plug-in: G'MIC doesn't use it
  LINKFLAGS := $(filter-out -lGL,$(LINKFLAGS))
else
  GMIC_STATIC_DEPS = 0
  DEPS_MODULES += libcurl
  CXXFLAGS += -Dcimg_use_curl
endif
CXXFLAGS += $(FFT_CXXFLAGS) -Dcimg_use_zlib -Dcimg_use_png
ifeq ($(GMIC_STATIC_DEPS),1)
# the static archive lib<name>.a, in the libdirs of the modules or the compiler's library path
STATIC_DEPS_SYSLIBS = -lm -ldl -lpthread -lrt -lc
static_archive = $(or $(firstword $(wildcard $(addsuffix /lib$(1).a,$(STATIC_DEPS_DIRS)))),$(filter /%,$(shell $(CXX) -print-file-name=lib$(1).a)),$(error GMIC_STATIC_DEPS=1: lib$(1).a not found (in $(STATIC_DEPS_DIRS) or the compiler's library path): build them with -fPIC (.github/workflows/build_static_deps.sh <prefix> then PKG_CONFIG_PATH=<prefix>/lib/pkgconfig) or build with GMIC_STATIC_DEPS=0))
ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
  STATIC_DEPS_DIRS := $(patsubst -L%,%,$(shell pkg-config --static --libs-only-L $(DEPS_MODULES))) \
    $(foreach m,$(DEPS_MODULES),$(shell pkg-config --variable=libdir $(m)))
  STATIC_DEPS_L := $(FFT_LIBS) $(shell pkg-config --static --libs-only-l $(DEPS_MODULES))
  STATIC_DEPS_ARCHIVES := $(foreach l,$(filter-out $(STATIC_DEPS_SYSLIBS),$(STATIC_DEPS_L)),$(call static_archive,$(patsubst -l%,%,$(l))))
endif
CXXFLAGS += `pkg-config --cflags $(DEPS_MODULES)`
LINKFLAGS += $(STATIC_DEPS_ARCHIVES) $(filter $(STATIC_DEPS_SYSLIBS),$(STATIC_DEPS_L))
else
CXXFLAGS += `pkg-config --cflags $(DEPS_MODULES)`
LINKFLAGS += `pkg-config --libs $(DEPS_MODULES)` $(FFT_LIBS)
endif

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
