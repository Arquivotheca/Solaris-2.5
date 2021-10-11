#
#ident	"@(#)Makefile	1.12	95/03/15 SMI"
#
# Copyright (c) 1995, by Sun Microsystems, Inc.
# All Rights Reserved.
#
# cmd/localedef/lib/Makefile

include ../../Makefile.cmd

OBJS=		common.o charmap.o lex.o
POFILE=		lib.po

SRCS =		$(OBJS:%.o=%.c)

SED= sed

.KEEP_STATE:

# nothing to install
all install:	$(OBJS)

catalog:	$(POFILE)

$(POFILE):	$(SRCS)
	$(RM) $@
	$(XGETTEXT) $(XGETFLAGS) $(SRCS)
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po

lint: lint_SRCS

clean:
	$(RM) $(OBJS)

include ../../Makefile.targ
