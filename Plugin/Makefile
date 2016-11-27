PLUGINNAME = GMIC
PLUGINOBJECTS = GMIC.o

SRCDIR = ..
PATHTOROOT = $(SRCDIR)/OpenFX/Examples
include Makefile.master

RESOURCES = GMIC.png

CXXFLAGS += \
-I$(SRCDIR)/Common \
-I$(SRCDIR)/GMIC \
-I$(SRCDIR)/OpenFX/include \
-I$(SRCDIR)/OpenFX/Examples/include

LINKFLAGS += \
-lcgmic \
-L$(SRCDIR)/GMIC

VPATH += \
$(SRCDIR)/GMIC \
$(SRCDIR)/Common
