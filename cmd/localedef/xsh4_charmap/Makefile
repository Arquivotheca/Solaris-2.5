#
#ident	"@(#)Makefile	1.28	95/07/10 SMI"
#
# Copyright (c) 1995, by Sun Microsystems, Inc.
# All Rights Reserved.
#
# cmd/localedef/xsh4_charmap/Makefile

include ../../Makefile.cmd

PROG= 		xsh4_charmap
PSRC_Y=		charmap.y
COMMON_OBJS=	../lib/common.o ../lib/lex.o
C_OBJS=		main.o utils.o global.o
PORTABLECMAP=	Portable.cmap
PORTABLESRC=	Portable.src

LDLIBS += -lintl
YFLAGS = -d
CPPFLAGS = -I../head $(CPPFLAGS.master)
CLOBBERFILES += $(PORTABLECMAP)

OBJS =		$(C_OBJS) $(PSRC_Y:%.y=%.o)
PSRC_C =	$(PSRC_Y:%.y=%.c)
SRCS =		$(C_OBJS:%.o=%.c) $(PSRC_C) $(COMMON_OBJS:%.o=%.c)

SED= sed

.KEEP_STATE:

all:	$(PROG) $(PORTABLECMAP)

install: all $(ROOTLOCALEPROG) $(ROOTCHARMAP)/$(PORTABLECMAP)

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

$(PORTABLECMAP): $(PORTABLESRC) $(PROG)
	./$(PROG) -i $(PORTABLESRC) -o $(PORTABLECMAP)

lint: $(SRCS) lint_SRCS

clean:
	$(RM) $(OBJS) $(PSRC_C) y.tab.h

include ../../Makefile.targ
