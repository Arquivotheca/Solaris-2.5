#
#ident	"@(#)Makefile	1.16	95/03/15 SMI"
#
# Copyright (c) 1995, by Sun Microsystems, Inc.
# All Rights Reserved.
#
# cmd/localedef/localedef/Makefile
#

include ../../Makefile.cmd

PROG= 		localedef
PSRC_Y=		localedef.y
COMMON_OBJS=	../lib/common.o
C_OBJS=		main.o yylex.o

LDLIBS += -lintl
YFLAGS = -d
CPPFLAGS = -I../head $(CPPFLAGS.master)

OBJS =		$(C_OBJS) $(PSRC_Y:%.y=%.o)
PSRC_C =	$(PSRC_Y:%.y=%.c)
SRCS =		$(C_OBJS:%.o=%.c) $(PSRC_C) $(COMMON_OBJS:%.o=%.c)

SED= sed

.KEEP_STATE:

all:	$(PROG)

install: all $(ROOTPROG)

$(PROG): $(OBJS) $(COMMON_OBJS)
	$(LINK.c) -o $@ $(OBJS) $(COMMON_OBJS) $(LDLIBS)

$(COMMON_OBJS):
	@cd ../lib; pwd; $(MAKE) $(@F)
	@pwd

$(PSRC_C) + y.tab.h:	$(PSRC_Y)
	$(YACC) $(YFLAGS) $(PSRC_Y)
	$(MV) y.tab.c $(PSRC_C)

$(OBJS): y.tab.h

catalog:	$(POFILE)

$(POFILE):	$(SRCS)
	$(RM) $@
	$(COMPILE.cpp) $(SRCS)	> $(POFILE).i
	$(XGETTEXT) $(XGETFLAGS)	$(POFILE).i
	$(SED) "/^domain/d" messages.po > $@
	$(RM) $(POFILE).i messages.po

lint: $(SRCS) .WAIT lint_SRCS

clean:
	$(RM) $(OBJS) $(PSRC_C) y.tab.h

include ../../Makefile.targ
