PLUGINNAME = GMIC
PLUGINSOURCES = gmic.cpp gmic_libc.cpp gmic_plugin.cpp gmic_parser.cpp RFX_FileUtils.cpp RFX_StringUtils.cpp
PLUGINOBJECTS = $(PLUGINSOURCES:.cpp=.o)

SRCDIR = ..
PATHTOROOT = $(SRCDIR)/openfx/Examples
include $(PATHTOROOT)/Makefile.master

RESOURCES = GMIC.png

CXXFLAGS += \
-DOFX_PLUGIN \
-I$(SRCDIR)/Helpers \
-I$(SRCDIR)/openfx/include \
-I$(SRCDIR)/openfx/Examples/include \
-Dgmic_build -Dgmic_is_parallel -Dcimg_use_abort -Dcimg_display=0 -Dcimg_appname=\"gmic\" -Dcimg_namespace_suffix=openfx_gmic -Dcimg_verbosity=0 -Dcimg_library=cimg_library_openfx_gmic


VPATH = $(SRCDIR):$(SRCDIR)/Helpers

# openmp
ifeq ($(OPENMP),1)
  CXXFLAGS += -Dcimg_use_openmp -fopenmp
  LINKFLAGS += -fopenmp
endif

# gmic_is_parallel requires POSIX threads
ifeq ($(OS),$(filter $(OS),Linux FreeBSD Darwin))
  CXXFLAGS += -Dgmic_is_parallel
endif
