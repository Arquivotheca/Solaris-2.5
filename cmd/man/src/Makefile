#
# ident	"@(#)Makefile	1.7	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# Makefile for cmd/man/src
#

include		../../Makefile.cmd
LDLIBS += -lintl

BINPROG =	man
LIBPROG =	getNAME
LIBSHELL =	makewhatis
LNKPROG =	catman apropos whatis

PROG =		$(BINPROG) $(LIBPROG)

SRCS =		man.c getNAME.c

SHSRCS =	makewhatis.sh

OBJS =		$(SRCS:%.c=%.o)


CLOBBERFILES +=	$(LNKPROG) $(LIBSHELL)

ROOTPROG =	$(BINPROG:%=$(ROOTBIN)/%)
ROOTLIBPROG =	$(LIBPROG:%=$(ROOTLIB)/%)
ROOTLIBSHELL =	$(LIBSHELL:%=$(ROOTLIB)/%)
ROOTLNKPROG =	$(LNKPROG:%=$(ROOTBIN)/%)

OWNER =		root
# for messaging catalog files
POFILE= src.po
POFILES= $(SRCS:%.c=%.po)

.KEEP_STATE :

all :		$(PROG) $(LIBSHELL) $(LNKPROG) 

makewhatis :	$$@.sh
		cat $@.sh > $@

install :	all $(ROOTPROG) $(ROOTLIBPROG)	\
		$(ROOTLIBSHELL) $(ROOTLNKPROG)

$(ROOTLNKPROG) :	$(ROOTBIN)/man
		-$(RM) $@; $(LN) $(ROOTBIN)/man $@

$(LNKPROG) :	man
		-$(RM) $@; $(LN) man $@

catalog:        $(POFILE)

$(POFILE):      $(POFILES)
	$(RM)	$@
	cat     $(POFILES)      > $@


clean :
		$(RM) $(OBJS)

strip :
		$(STRIP) $(PROG)

lint : 		lint_SRCS

include		../../Makefile.targ
