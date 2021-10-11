#
#ident	"@(#)Makefile	1.10	94/09/06 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= diff diffh
SRCS=	$(PROG:%=%.c)
DIFF= diff
DIFFH= diffh

include ../Makefile.cmd
#
# for messaging catalog
#
POFILE= diff.po
XGETFLAGS += -a -x diff.xcl

LDLIBS += -lintl -lw

ROOTBINDIFF = $(DIFF:%=$(ROOTBIN)/%)
ROOTLIBDIFFH = $(DIFFH:%=$(ROOTLIB)/%)

.KEEP_STATE:

all: $(PROG) 

#
# Use private rule
#
$(POFILE):
	$(RM) $@
	$(COMPILE.cpp)   $(SRCS) > diff.po.i
	$(XGETTEXT) $(XGETFLAGS) diff.po.i
	sed "/^domain/d"        messages.po     > $@
	$(RM)  diff.po.i messages.po

install: all $(ROOTBINDIFF) $(ROOTLIBDIFFH)

clean:

lint: lint_SRCS

include ../Makefile.targ
