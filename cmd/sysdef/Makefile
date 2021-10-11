#
#ident	"@(#)Makefile	1.9	92/12/15 SMI" /* SVr4.0 1.6.6.2 */
#
# Copyright (c) 1990,1991 by Sun Microsystems, Inc.
#
# cmd/sysdef/Makefile
#

PROG= 		sysdef

OBJECTS=	sysdef.o  sdevinfo.o
SRCS=		$(OBJECTS:.o=.c)

include ../Makefile.cmd

OWNER=		root
GROUP=		sys
FILEMODE=	2555

RELUSRSBIN=     ../usr/sbin
ROOTSYMLINK=    $(ROOTETC)/$(PROG)
LDLIBS +=	-lkvm -lelf

.KEEP_STATE:

all: 		$(PROG) 

$(PROG):        $(OBJECTS)
	$(LINK.c) -o $@ $(OBJECTS) $(LDLIBS)
	$(POST_PROCESS)

install: 	all $(ROOTUSRSBINPROG) $(ROOTSYMLINK)

$(ROOTSYMLINK):
	-$(RM) $@; $(SYMLINK) $(RELUSRSBIN)/$(PROG) $@

clean:
	$(RM) $(OBJECTS)

lint:   lint_SRCS

include ../Makefile.targ

