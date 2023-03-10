PLUGINNAME = GMIC
PLUGINSOURCES = gmic.cpp gmic_libc.cpp gmic_plugin.cpp gmic_parser.cpp RFX_FileUtils.cpp RFX_StringUtils.cpp
PLUGINOBJECTS = $(PLUGINSOURCES:.cpp=.o)

SRCDIR = ..
PATHTOROOT = $(SRCDIR)/openfx/Examples
include $(PATHTOROOT)/Makefile.master

RESOURCES = GMIC.png

# we need zlib to decompress the G'MIC standard library, and FFTW3 for many effects that use the FFT (the CImg builtin FFT only works with power-of-two dimensions)
CXXFLAGS += \
-DOFX_EXTENSIONS_NATRON \
-DOFX_EXTENSIONS_NUKE \
-DOFX_PLUGIN \
-I$(SRCDIR)/Helpers \
-I$(SRCDIR)/openfx/include \
-I$(SRCDIR)/openfx/Examples/include \
-Dgmic_build \
-Dgmic_is_parallel \
-Dcimg_use_abort \
-Dcimg_display=0 \
-Dcimg_appname=\"gmic\" \
-Dcimg_namespace_suffix=openfx_gmic \
-Dcimg_verbosity=0 \
-Dcimg_library=cimg_library_openfx_gmic

# FFTW (license: GPLv2), necessary to get Fourier transform on
# images with dimensions that are not power-of-two
FFTW3_CXXFLAGS =`pkg-config --cflags fftw3` -Dcimg_use_fftw3
FFTW3_LINKFLAGS =`pkg-config --libs fftw3` -lfftw3_threads

# Zlib, necessary to uncompress the glic stdlib
ZLIB_CXXFLAGS=`pkg-config --cflags zlib` -Dcimg_use_zlib
ZLIB_LINKFLAGS=`pkg-config --libs zlib`

# libcurl, necessary to download presets:
LIBCURL_CXXFLAGS=`pkg-config --cflags libcurl` -Dcimg_use_curl
LIBCURL_LINKFLAGS=`pkg-config --libs libcurl`

# libpng, necessary for HaldCLUTs:
LIBPNG_CXXFLAGS=`pkg-config --cflags libpng` -Dcimg_use_png
LIBPNG_LINKFLAGS=`pkg-config --libs libpng`

CXXFLAGS += $(FFTW3_CXXFLAGS) $(ZLIB_CXXFLAGS) $(LIBCURL_CXXFLAGS) $(LIBPNG_CXXFLAGS)
LINKFLAGS += $(FFTW3_LINKFLAGS) $(ZLIB_LINKFLAGS) $(LIBCURL_LINKFLAGS) $(LIBPNG_LINKFLAGS)

VPATH = $(SRCDIR):$(SRCDIR)/Helpers

# openmp
ifeq ($(OPENMP),1)
  CXXFLAGS += -Dcimg_use_openmp -fopenmp
  LINKFLAGS += -fopenmp
endif

