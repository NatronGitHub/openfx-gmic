PLUGINNAME = GMIC
PLUGINSOURCES = gmic.cpp gmic_ofx.cpp gmic_plugin.cpp gmic_plugin_call.cpp gmic_parser.cpp gmic_log.cpp RFX_StringUtils.cpp
PLUGINOBJECTS = $(PLUGINSOURCES:.cpp=.o)

SRCDIR = ..
# the OpenFX C++ Support library: Plugins/Makefile.master compiles its sources (SUPPORTOBJECTS)
# into the plug-in
PATHTOROOT = $(SRCDIR)/openfx/Support
include $(PATHTOROOT)/Plugins/Makefile.master
# LICENSE (GPL or COMMERCIAL) and FFT (pocketfft or fftw3): see license.mk
include $(SRCDIR)/license.mk

# the licences of the plug-in and G'MIC (COPYING: dual CeCILL-C / CeCILL v2; COMMERCIAL builds use
# CeCILL-C)
RESOURCES = GMIC.png $(SRCDIR)/COPYING $(SRCDIR)/Licence_CeCILL-C_V1-en.txt $(SRCDIR)/Licence_CeCILL_V2-en.txt
# COMMERCIAL: the CeCILL-B licence of the community files without a licence in their header
# (gmic-community's include/COPYING, see gmic_stdlib_commercial.txt)
ifeq ($(LICENSE),COMMERCIAL)
RESOURCES += $(SRCDIR)/Licence_CeCILL-B_V1-en.txt
endif
# pocketfft's licence (BSD-3-Clause), which asks for its notice in binary distributions
ifeq ($(FFT),pocketfft)
RESOURCES += $(SRCDIR)/Licence_pocketfft.txt
endif

# The list of the plug-ins (README, "Choosing the plug-ins"), Contents/Resources/gmic_ofx_plugins.txt,
# every plug-in enabled: written by the plug-in itself, after each link, through a small loader
# (gmic_ofx_plugin_list.cpp), as CMakeLists.txt does; it loads the bundle's copy (on macOS, the
# one that osxDeploy.sh changed in release builds). GMIC_PLUGIN_LIST=0: no list, which registers
# every plug-in too, for a plug-in that the build machine can't load. It is the default when the
# compiler targets another processor than the build machine's (uname -m; not checked on macOS,
# whose builds are universal): pass GMIC_PLUGIN_LIST=1 if the build machine runs it anyway (e.g.
# i686 on x86_64, or MSYS2's emulated x86_64 uname on Windows on Arm).
ifneq ($(OS),Darwin)
  # (the names of the same processor in uname -m and in the compiler's target triplet)
  norm_machine = $(patsubst powerpc64le,ppc64le,$(patsubst armv%,arm,$(patsubst aarch64,arm64,$(patsubst amd64,x86_64,$(1)))))
  TARGET_MACHINE := $(call norm_machine,$(firstword $(subst -, ,$(shell $(CXX) -dumpmachine 2>/dev/null))))
  ifneq ($(and $(TARGET_MACHINE),$(filter-out $(call norm_machine,$(shell uname -m)),$(TARGET_MACHINE))),)
    GMIC_PLUGIN_LIST ?= 0
  endif
endif
GMIC_PLUGIN_LIST ?= 1
ifeq ($(GMIC_PLUGIN_LIST),1)
PLUGINLIST = $(OBJECTPATH)/$(PLUGINNAME).ofx.bundle/Contents/Resources/gmic_ofx_plugins.txt
PLUGINLIST_TOOL = $(OBJECTPATH)/gmic_ofx_plugin_list$(if $(MINGW_OS),.exe)
all: $(PLUGINLIST)
$(PLUGINLIST_TOOL): gmic_ofx_plugin_list.cpp
	@mkdir -p $(OBJECTPATH)
	$(CXX) $(BITSFLAG) -std=$(or $(CXXSTD),c++17) -O1 $< -o $@ $(if $(filter Linux,$(OS)),-ldl) $(if $(MINGW_OS),-municode)
$(PLUGINLIST): $(OBJECTPATH)/$(PLUGINNAME).ofx $(PLUGINLIST_TOOL) | $(OBJECTPATH)/$(PLUGINNAME).ofx.bundle
	@mkdir -p $(@D)
	$(PLUGINLIST_TOOL) $(OBJECTPATH)/$(PLUGINNAME).ofx.bundle/Contents/$(ARCH)/$(PLUGINNAME).ofx $@
endif

# LICENSE, FFT, OPENMP and GMIC_STATIC_DEPS change the compiled code, the linked libraries and the
# bundled files: when they change, rebuild the objects and the bundle (whose Contents/Libraries
# and Resources would keep stale files).
# OFX_DEFINES (below) changes the Support library's class layouts: rebuild its objects too.
BUILD_KEY = LICENSE=$(LICENSE) FFT=$(FFT) OPENMP=$(OPENMP) GMIC_STATIC_DEPS=$(GMIC_STATIC_DEPS) OFX_DEFINES=$(OFX_DEFINES)
$(OBJECTPATH)/build.license: gmic_sources
	@mkdir -p $(OBJECTPATH)
	@echo '$(BUILD_KEY)' | cmp -s - $@ || { echo '$(BUILD_KEY)' > $@; rm -rf $(OBJECTPATH)/$(PLUGINNAME).ofx.bundle; }
$(addprefix $(OBJECTPATH)/,$(PLUGINOBJECTS) $(SUPPORTOBJECTS)): $(OBJECTPATH)/build.license

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
$(OBJECTPATH)/gmic_plugin_call.o: $(GMIC_HEADERS) $(SRCDIR)/gmic_libc.h $(SRCDIR)/gmic_plugin_call.h $(SRCDIR)/Helpers/gmic_log.h
$(OBJECTPATH)/gmic_plugin.o: $(SRCDIR)/gmic_libc.h $(SRCDIR)/gmic_plugin_call.h $(SRCDIR)/gmic_plugin.h $(SRCDIR)/Helpers/gmic_log.h
$(OBJECTPATH)/gmic_ofx.o: $(SRCDIR)/gmic_libc.h $(SRCDIR)/gmic_plugin.h $(SRCDIR)/Helpers/gmic_log.h
$(OBJECTPATH)/gmic_parser.o $(OBJECTPATH)/gmic_log.o: $(SRCDIR)/Helpers/gmic_log.h

# The OpenFX extensions of openfx's CMake defaults (openfx/CMakeLists.txt's OFX_DEFINITIONS), as
# CMake builds get them through openfx::Api and openfx::Support: both builds ship the same Support
# library. They change its class layouts: every object is compiled with the same set.
OFX_DEFINES = -DOFX_EXTENSIONS_NATRON -DOFX_EXTENSIONS_NUKE -DOFX_EXTENSIONS_VEGAS -DOFX_EXTENSIONS_TUTTLE -DOFX_SUPPORTS_OPENGLRENDER
CXXFLAGS += $(OFX_DEFINES) \
-I$(SRCDIR) \
-I$(SRCDIR)/Helpers \
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
# pocketfft (BSD-3-Clause, the default), header-only, multithreaded (CImg's cimg_use_pocketfft)
FFT_CXXFLAGS = -Dcimg_use_pocketfft
# Windows: single-threaded pocketfft. Its static thread pool is destroyed when the host unloads
# the plug-in (FreeLibrary): the destructor joins the worker threads under the loader lock, which
# the exiting threads wait for (deadlock). The rest of G'MIC is still multithreaded (OpenMP).
ifneq ($(MINGW_OS),)
FFT_CXXFLAGS += -DPOCKETFFT_NO_MULTITHREADING
endif
endif
# COMMERCIAL: the parser does not report curated filters that its standard library leaves out
ifeq ($(LICENSE),COMMERCIAL)
CXXFLAGS += -DOFX_GMIC_COMMERCIAL
endif

# The libraries, through pkg-config: zlib (to decompress the G'MIC standard library), libpng
# (HaldCLUTs) and FFTW (FFT=fftw3). No libcurl, on any system: G'MIC downloads filter data with
# the system's curl command (cimg::load_network; gmic_plugin_call.cpp sets its path: macOS
# /usr/bin/curl, Windows 10 1803 and later System32\curl.exe, other systems curl or wget from PATH).
#
# Linux: GMIC.ofx needs only system libraries (libc, libm, libstdc++, libgcc_s, libgomp), so
# that one binary runs on every distribution (Natron 2.6's baseline: Rocky Linux 9, glibc 2.34,
# GCC 11). zlib, libpng and FFTW are linked statically (GMIC_STATIC_DEPS=1,
# the default on Linux): their static archives (libz.a, libpng16.a, libfftw3.a,
# libfftw3_threads.a, built with -fPIC, which distributions' archives may not be: see
# .github/workflows/build_static_deps.sh) must be in pkg-config's libdir or the compiler's library
# path. GMIC_STATIC_DEPS=0 links the shared libraries, e.g. for distribution packages. The
# plug-in's version script (linuxSymbols) keeps the symbols of the static libraries local.
# Other systems: shared libraries (macOS and Windows bundles copy them).
DEPS_MODULES = libpng zlib $(FFT_MODULES)
ifeq ($(OS),Linux)
  GMIC_STATIC_DEPS ?= 1
  # Makefile.master links OpenGL to every plug-in: G'MIC doesn't use it
  LINKFLAGS := $(filter-out -lGL,$(LINKFLAGS))
  # (openfx >= #161 moves -lGL -pthread -ldl into SYSLIBS, at the end of the link line)
  SYSLIBS := $(filter-out -lGL,$(SYSLIBS))
else
  override GMIC_STATIC_DEPS = 0
endif
CXXFLAGS += $(FFT_CXXFLAGS) -Dcimg_use_zlib -Dcimg_use_png
ifeq ($(GMIC_STATIC_DEPS),1)
# the static archive lib<name>.a, in the libdirs of the modules or the compiler's library path
STATIC_DEPS_SYSLIBS = -lm -ldl -lpthread -lrt -lc
static_archive = $(or $(firstword $(wildcard $(addsuffix /lib$(1).a,$(STATIC_DEPS_DIRS)))),$(filter /%,$(shell $(CXX) -print-file-name=lib$(1).a)),$(error GMIC_STATIC_DEPS=1: lib$(1).a not found (in $(STATIC_DEPS_DIRS) or the compiler's library path): build them with -fPIC (.github/workflows/build_static_deps.sh <prefix> then PKG_CONFIG_PATH=<prefix>/lib/pkgconfig) or build with GMIC_STATIC_DEPS=0))
ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
  # the libdirs of libpng, zlib, then FFTW, then the -L directories: the archives of
  # build_static_deps.sh come before those of FFTW's libdir (e.g. /usr/lib/<arch>, which may also
  # have non-PIC libpng and zlib archives)
  STATIC_DEPS_DIRS := $(foreach m,$(DEPS_MODULES),$(shell pkg-config --variable=libdir $(m))) \
    $(patsubst -L%,%,$(shell pkg-config --static --libs-only-L $(DEPS_MODULES)))
  STATIC_DEPS_L := $(FFT_LIBS) $(shell pkg-config --static --libs-only-l $(DEPS_MODULES))
  STATIC_DEPS_ARCHIVES := $(foreach l,$(filter-out $(STATIC_DEPS_SYSLIBS),$(STATIC_DEPS_L)),$(call static_archive,$(patsubst -l%,%,$(l))))
endif
CXXFLAGS += `pkg-config --cflags $(DEPS_MODULES)`
LINKFLAGS += $(STATIC_DEPS_ARCHIVES) $(filter $(STATIC_DEPS_SYSLIBS),$(STATIC_DEPS_L))
else
CXXFLAGS += `pkg-config --cflags $(DEPS_MODULES)`
LINKFLAGS += `pkg-config --libs $(DEPS_MODULES)` $(FFT_LIBS)
endif

# (after Plugins/Makefile.master's VPATH, the Support library's sources)
VPATH += $(SRCDIR) $(SRCDIR)/Helpers

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
