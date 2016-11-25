PLUGINNAME = GMIC
PLUGINOBJECTS = GMIC.o

SRCDIR = ..
PATHTOROOT = $(SRCDIR)/OpenFX/Examples
include Makefile.master

RESOURCES = GMIC.png $(SRCDIR)/GMIC/gmic_stdlib.gmic
CXXFLAGS += -I$(SRCDIR)/Common -I$(SRCDIR)/GMIC -I$(SRCDIR)/OpenFX/include -I$(SRCDIR)/OpenFX/Examples/include
LINKFLAGS += -L$(SRCDIR)/GMIC -lcgmic

VPATH += \
$(SRCDIR)/GMIC \
$(SRCDIR)/Common
