#
#ident	"@(#)Makefile	1.18	93/08/12 SMI"	/* SVr4.0 1.1	*/
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#
#	cmd/src/localdef/colltbl/Makefile

include  ../../Makefile.cmd
include ../Makefile.locale

COLLATE_FILE=	$(BUILDDIRS:%=coll.%.so)
ROOTCOLLATEDIR=	$(BUILDDIRS:%=$(ROOTLOCALE)/%/LC_COLLATE)
ROOTCOLLATE=	$(ROOTCOLLATEDIR:%=%/coll.so)
ROOTDIRS += 	$(ROOTCOLLATEDIR)

PROG=	colltbl
# anything related to NPROG should be removed

OBJS=	colltbl.o collfcns.o diag.o parse.o lex.o utils.o source.o
SRCS=	$(OBJS:%.o=%.c)

LDLIBS += -ly

CLEANFILES=	$(OBJS)  parse.c 
CLOBBERFILES=	$(COLLATE_FILE)


.KEEP_STATE:


all:	$(PROG) $(COLLATE_FILE)

install: all $(ROOTPROG) $(ROOTLOCALE) $(ROOTDIRS) $(ROOTSYMLN) \
	$(ROOTCOLLATE)

coll.%.so: colltbl.%.c
	$(LINK.c) -o $@ -K pic -G $<
	$(POST_PROCESS_SO)

$(ROOTLOCALE)/%/LC_COLLATE/coll.so: coll.%.so
	$(INS.rename)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)


lint: lint_SRCS

parse.c:	parse.y
	$(YACC.y) -d parse.y
	$(MV) y.tab.c parse.c

clean:
	-$(RM) $(CLEANFILES)

include ../Makefile.target
include ../../Makefile.targ
