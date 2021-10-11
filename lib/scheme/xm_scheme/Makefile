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
LDLIBS += -lc -lauth -lnsl -lsocket -lintl

# resolve with local variables in shared library
DYNFLAGS += -znodefs -Bsymbolic

LINTFLAGS=

DYNLIB= pam_extern.so$(VERS)
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
# Build the default scheme .o's.  The standard entry points are renamed
# from sa_XXX to def_sa_XXX so we can link the default scheme in and 
# dynamically load a scheme with name sa_XXX.

#pics/def_% \
#objs/def_% := CPPFLAGS += -D"sa_auth_user=__def_sa_auth_user" \
#                          -D"sa_auth_netuser=__def_sa_auth_netuser" \
#                          -D"sa_auth_port=__def_sa_auth_port" \
#                          -D"sa_auth_acctmg=__def_sa_auth_acctmg" \
#                          -D"sa_open_session=__def_sa_open_session" \
#                          -D"sa_close_session=__def_sa_close_session" \
#                          -D"sa_setcred=__def_sa_setcred" \
#                          -D"sa_set_authtokattr=__def_sa_set_authtokattr" \
#                          -D"sa_get_authtokattr=__def_sa_get_authtokattr" \
#                          -D"sa_chauthtok=__def_sa_chauthtok" \
#                          -D"sa_start=__def_sa_start" \
#                          -D"sa_end=__def_sa_end" \
#                          -K pic -DPIC -D_TS_ERRNO

# build rule for def_ objects
#objs/def_%.o pics/def_%.o : %.c
#	$(COMPILE.c) -o $@ $< 
#	$(POST_PROCESS_O)
#
.KEEP_STATE:

.PARALLEL:	$(DEFOBJS) $(DEFPICS)

all: $(LIBS) 

defobjs: $(OBJ_DIR) .WAIT $(DEFOBJS)

defpics: $(PIC_DIR) .WAIT $(DEFPICS)

$(ROOTLIBDIR) $(ROOTETCLIBDIR):
	$(INS.dir)

install: all $(ROOTLIBDIR) $(ROOTLIBS) $(ROOTLIBDIR)/pam_extern.so \
	$(ROOTETCLIBDIR) $(ROOTETCLIBS) $(ROOTETCLIBDIR)/pam_extern.so

$(ROOTETCLIBDIR)/%: %
	$(INS.file)

$(ROOTLIBDIR)/pam_extern.so $(ROOTETCLIBDIR)/pam_extern.so:
	$(RM) $@; $(SYMLINK) $(DYNLIB) $@

# include library targets
include ../../Makefile.targ

