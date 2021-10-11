#
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.18	94/09/16 SMI"
#
# lib/scheme/unix_scheme/Makefile
#
VERS = .1

include Makefile.objs

OBJECTS= $(SCHOBJECTS)
SRCS= $(OBJECTS:%.o=%.c)

# include library definitions
include ../../Makefile.lib

# library dependency
# LDLIBS+= -lcmd
LDLIBS += -lc -lauth -lnsl -lsocket -lintl -lcmd

# resolve with local variables in shared library
DYNFLAGS += -znodefs -Bsymbolic

LINTFLAGS=

DYNLIB= pam_entry.so$(VERS)
LIBS = $(DYNLIB)

# override ROOTLIBDIR and ROOTLINKS
ROOTLIBDIR=	$(ROOT)/usr/lib/security
ROOTLIBS=       $(LIBS:%=$(ROOTLIBDIR)/%)
ROOTETCLIBDIR=	$(ROOT)/etc/lib
ROOTETCLIBS=	$(LIBS:%=$(ROOTETCLIBDIR)/%)
OWNER = root
GROUP = sys
#$(ROOTLIBS):=			FILEMODE = 755
$(ROOTETCLIBDIR)/$(DYNLIB):=		FILEMODE = 755

OBJ_DIR=	objs
PIC_DIR=	pics

.KEEP_STATE:

.PARALLEL:	$(DEFOBJS) $(DEFPICS)

all: $(LIBS) 

defobjs: $(OBJ_DIR) .WAIT $(DEFOBJS)

defpics: $(PIC_DIR) .WAIT $(DEFPICS)

$(ROOTLIBDIR) $(ROOTETCLIBDIR):
	$(INS.dir)

install: all $(ROOTLIBDIR) $(ROOTLIBS) $(ROOTLIBDIR)/pam_entry.so \
	$(ROOTETCLIBDIR) $(ROOTETCLIBS) $(ROOTETCLIBDIR)/pam_entry.so

$(ROOTETCLIBDIR)/%: %
	$(INS.file)

$(ROOTLIBDIR)/pam_entry.so $(ROOTETCLIBDIR)/pam_entry.so:
	$(RM) $@; $(SYMLINK) $(DYNLIB) $@

# include library targets
include ../../Makefile.targ

