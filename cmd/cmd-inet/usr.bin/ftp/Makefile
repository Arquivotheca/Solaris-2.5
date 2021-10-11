#
#ident	"@(#)Makefile	1.6	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= ftp
OBJS=	cmds.o cmdtab.o ftp.o getpass.o glob.o main.o pclose.o \
	ruserpass.o domacro.o
SRCS= $(OBJS:%.o=%.c)

include ../../../Makefile.cmd

CPPFLAGS +=	-DSYSV -DSTRNET -DBSD_COMP
LDLIBS +=	-lsocket -lnsl

.KEEP_STATE:

all: $(PROG) 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG)

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

include ../../../Makefile.targ
