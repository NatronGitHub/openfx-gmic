PLUGINNAME = GMIC
PLUGINSOURCES = gmic.cpp gmic_libc.cpp gmic_plugin.cpp gmic_parser.cpp RFX_FileUtils.cpp RFX_StringUtils.cpp
PLUGINOBJECTS = $(PLUGINSOURCES:.cpp=.o)

SRCDIR = ..
PATHTOROOT = $(SRCDIR)/openfx/Examples
include $(PATHTOROOT)/Makefile.master

RESOURCES = GMIC.png

# we need zlib to decompress the G'MIC standard library, and FFTW3 for many effects that use the FFT (the CImg builtin FFT only works with power-of-two dimensions)
CXXFLAGS += \
-DOFX_PLUGIN \
-I$(SRCDIR)/Helpers \
-I$(SRCDIR)/openfx/include \
-I$(SRCDIR)/openfx/Examples/include \
-Dgmic_build -Dgmic_is_parallel -Dcimg_use_abort -Dcimg_display=0 -Dcimg_appname=\"gmic\" -Dcimg_namespace_suffix=openfx_gmic -Dcimg_verbosity=0 -Dcimg_library=cimg_library_openfx_gmic -Dgmic_is_parallel

# FFTW (license: GPLv2), necessary to get Fourier transform on
# images with dimensions that are not power-of-two
FFTW3_CXXFLAGS =`pkg-config --cflags fftw3` -Dcimg_use_fftw3
FFTW3_LINKFLAGS =`pkg-config --libs fftw3`
# fftw_init_threads is in a separate lib on Unix
ifeq ($(OS),$(filter $(OS),Linux FreeBSD Darwin))
  FFTW3_LINKFLAGS += -lfftw3_threads
endif

# Zlib, necessary to uncompress the glic stdlib
ZLIB_CXXFLAGS=`pkg-config --cflags zlib` -Dcimg_use_zlib
ZLIB_LINKFLAGS=`pkg-config --libs zlib`

CXXFLAGS += $(FFTW3_CXXFLAGS) $(ZLIB_CXXFLAGS)
LINKFLAGS += $(FFTW3_LINKFLAGS) $(ZLIB_LINKFLAGS)

VPATH = $(SRCDIR):$(SRCDIR)/Helpers

# openmp
ifeq ($(OPENMP),1)
  CXXFLAGS += -Dcimg_use_openmp -fopenmp
  LINKFLAGS += -fopenmp
endif

