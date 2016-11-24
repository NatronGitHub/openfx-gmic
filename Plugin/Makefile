PLUGINNAME = GMIC
PLUGINOBJECTS = GMIC.o
RESOURCES = fr.inria.openfx.GMIC.png

SRCDIR = ..
PATHTOROOT = $(SRCDIR)/OpenFX/Examples
include Makefile.master

CXXFLAGS += -I$(SRCDIR)/Common -I$(SRCDIR)/GMIC -I$(SRCDIR)/OpenFX/include -I$(SRCDIR)/OpenFX/Examples/include
LINKFLAGS += -lcgmic

VPATH += \
$(SRCDIR)/GMIC \
$(SRCDIR)/Common
