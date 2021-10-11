#
#ident	"@(#)Makefile	1.13	93/11/22 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG=		in.ftpd
OBJS1=		ftpd.o glob.o popen.o logwtmp.o vers.o
OTHEROBJ=	ftpcmd.o
OBJS=		$(OBJS1) $(OTHEROBJ)
SRCS=		$(OBJS1:%.o=%.c)

include ../../../Makefile.cmd

CPPFLAGS +=	-DSYSV -DSTRNET -DBSD_COMP -DBSDSECURE
LDLIBS +=	-lsocket -lnsl -lbsm -lauth -ldl
YFLAGS +=		-d

.KEEP_STATE:

all: $(PROG) 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG)

clean:
	$(RM) $(OBJS) y.tab.h y.tab.c ftpcmd.c

lint:	lint_SRCS

include ../../../Makefile.targ
