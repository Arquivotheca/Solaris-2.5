#
#ident	"@(#)Makefile	1.6	93/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG=	getent

include ../Makefile.cmd

OBJECTS=	getent.o dogetpw.o dogetgr.o dogethost.o \
	dogetserv.o dogetproto.o dogetethers.o dogetnet.o
SRCS=	$(OBJECTS:.o=.c)

LDLIBS	+= -lsocket -lnsl -lintl

#
# for message catalog
#
POFILE= gettent.po
POFILES= $(SRCS:%.c=%.po)

.KEEP_STATE:

all: $(PROG)

$(POFILE):      $(POFILES)
	$(RM)	$@
	cat     $(POFILES)      > $@


$(PROG): $(OBJECTS)
	$(LINK.c) -o $@ $(OBJECTS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG)

clean:
	$(RM) $(OBJECTS)

lint:	lint_SRCS

include ../Makefile.targ
