#
#ident	"@(#)Makefile	1.7	92/12/15 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/cmd-inet/usr.bin/rdist/Makefile

PROG=	rdist
OBJS=	docmd.o expand.o gram.o lookup.o main.o server.o
SRCS=	$(OBJS:.o=.c)

include ../../../Makefile.cmd

CPPFLAGS +=	-DSYSV -DSTRNET -DBSD_COMP
LDLIBS +=	-lsocket -lnsl -lgen

ROOTSYMLINK=	$(ROOT)/usr/ucb/$(PROG)

# conditional assignments
$(ROOTPROG) :=      FILEMODE=       04555
$(ROOTPROG) :=      OWNER=          root

.KEEP_STATE:

all: $(PROG) 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG) $(ROOTSYMLINK)

$(ROOTSYMLINK):
	$(RM) $@; $(SYMLINK) ../bin/$(PROG) $@

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

include ../../../Makefile.targ
