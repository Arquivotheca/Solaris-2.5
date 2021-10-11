#
#ident	"@(#)Makefile	1.10	93/06/01 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libcmd/Makefile
#
LIBRARY= libcmd.a

OBJECTS= deflt.o getterm.o magic.o sum.o

# include library definitions
include ../Makefile.lib

CPPFLAGS= -K pic -DPIC -D_REENTRANT -Dsparc $(RTLDINC) $(CPPFLAGS.master)

.KEEP_STATE:

all: $(TXTS) $(LIBS)

install: $(ROOTLIBS)

#
# create message catalogue files
#
TEXT_DOMAIN= SUNW_OST_OSLIB
_msg: $(MSGDOMAIN) catalog

catalog: 
	sh ./makelibcmdcatalog.sh $(MSGDOMAIN)

$(MSGDOMAIN):
	$(INS.dir)

# include library targets
include ../Makefile.targ
