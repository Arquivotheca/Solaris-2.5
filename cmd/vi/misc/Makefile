#
#ident	"@(#)Makefile	1.8	94/11/30 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/vi/misc/Makefile

CFLAGS += v

PROG= ctags
XPG4PROG= ctags
OTHERPROGS= fold cxref
SRCS= $(PROG).c fold.c mkstr.c xstr.c
TXTS= READ_ME del.h.cb mkstr.c xstr.c
#
# For message catalogue file
#
POFILES= $(SRCS:%.c=%.po)

include ../../Makefile.cmd
$(XPG4) := LDLIBS += -lintl
$(XPG4) := CFLAGS += -DXPG4 

POFILE= misc.po

CLOBBERFILES += $(OTHERPROGS)

ctags:= LDLIBS += -lintl

.KEEP_STATE:

all: $(PROG) $(XPG4) $(OTHERPROGS) $(TXTS)

catalog: $(POFILE)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $(POFILE)


install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

lint:	lint_SRCS

include ../../Makefile.targ
