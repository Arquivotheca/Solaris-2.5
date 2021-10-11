#
#ident	"@(#)Makefile	1.6	92/12/15 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#

PROG=		nslookup
HELPFILE=	nslookup.help
OBJS=		main.o getinfo.o debug.o send.o skip.o list.o subr.o \
		commands.o herror.o
SRCS=		$(OBJS:%.o=%.c)

include ../../../../../Makefile.cmd

CPPFLAGS +=	-DSYSV -DSTRNET -DBSD_COMP
LDLIBS +=	-ll -lresolv -lsocket -lnsl

.KEEP_STATE:


all: $(PROG) $(HELPFILE)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

ROOTLIBHELP=    $(ROOTLIB)/$(HELPFILE)

$(ROOTLIBHELP):=	FILEMODE = 0444

install: all $(ROOTUSRSBINPROG) $(ROOTLIBHELP)

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

include ../../../../../Makefile.targ

herror.o: ../herror.c
	cc -c -O $(CPPFLAGS) ../herror.c -o herror.o
