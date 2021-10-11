#
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.19	95/05/01 SMI"
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

LDLIBS += -lc -lauth -lnsl -lmp -lsocket -lintl -lcmd

# resolve with local variables in shared library
DYNFLAGS += -znodefs -Bsymbolic

LINTFLAGS=


DYNLIB= pam_pwmgt.so$(VERS)
LIBS = $(DYNLIB)

PROTOCOL_DIR = $(ROOT)/usr/include/rpcsvc
DERIVED_FILES= nispasswd_xdr.c

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

all: $(DERIVED_FILES) $(LIBS)

defobjs: $(OBJ_DIR) .WAIT $(DEFOBJS)

defpics: $(PIC_DIR) .WAIT $(DEFPICS)

$(ROOTLIBDIR) $(ROOTETCLIBDIR):
	$(INS.dir)

install: all $(ROOTLIBDIR) $(ROOTLIBS) $(ROOTLIBDIR)/pam_pwmgt.so \
	$(ROOTETCLIBDIR) $(ROOTETCLIBS) $(ROOTETCLIBDIR)/pam_pwmgt.so

$(ROOTETCLIBDIR)/%: %
	$(INS.file)

$(ROOTLIBDIR)/pam_pwmgt.so $(ROOTETCLIBDIR)/pam_pwmgt.so:
	$(RM) $@; $(SYMLINK) $(DYNLIB) $@

# include library targets
include ../../Makefile.targ


# DERIVED_FILES
nispasswd_xdr.c: $(PROTOCOL_DIR)/nispasswd.x
	$(RPCGEN) -c -C -M $(PROTOCOL_DIR)/nispasswd.x > nispasswd_xdr.c

lint:
	$(LINT.c) $(SRCS) > lint.out 2>&1
