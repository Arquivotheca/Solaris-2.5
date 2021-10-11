#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)Makefile	1.10	94/03/30 SMI"

# cmd/localedef/msgfmt/Makefile

include ../../Makefile.cmd
include ../Makefile.locale

# LC_MESSAGES directories to be made.
# No directories will be made for "C", "POSIX" or iso_8859_1 because
# gettext doesn't read C's .mo, POSIX is a symlink to C,
# and iso_8859_1 is a partial locale that only has LC_CTYPE.
ROOTLOCALEDIRS= $(BUILDDIRS:%=$(ROOTLOCALE)/%)	# /usr/lib/locale/xx
ROOTMESSAGEDIRS= $(BUILDDIRS:%=$(ROOTLOCALE)/%/LC_MESSAGES)
					#   /usr/lib/locale/xx/LC_MESSAGES

OBJS= xgettext.o xgettext.lx.o

PROG=  msgfmt xgettext
SRCS= $(PROG:%=%.c)

ROOTMESSAGE=	$(BUILDDIRS:%=$(ROOTLOCALE)/%/LC_MESSAGES)

ROOTDIRS += $(ROOTMESSAGE)

CFLAGS += -Xa

CLOBBERFILES += read_mo

.KEEP_STATE:

all: $(PROG) read_mo

xgettext: $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all  $(ROOTDIRS) $(ROOTSYMLN) $(ROOTPROG)

clean:  
	$(RM) $(OBJS)

lint: lint_SRC
	
include ../Makefile.target
include ../../Makefile.targ
