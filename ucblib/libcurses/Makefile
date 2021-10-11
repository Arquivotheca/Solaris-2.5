#
#ident	"@(#)Makefile	1.3	91/12/02 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# ucblib/libcurses/Makefile
#
LIBRARY= libcurses.a

OBJECTS= \
	addch.o addstr.o box.o clear.o clrtobot.o clrtoeol.o cr_put.o \
	cr_tty.o curses.o delch.o deleteln.o delwin.o endwin.o erase.o \
	fullname.o getch.o getstr.o id_subwins.o idlok.o initscr.o insch.o \
	insertln.o longname.o move.o mvprintw.o mvscanw.o mvwin.o newwin.o \
	overlay.o overwrite.o printw.o putchar.o refresh.o scanw.o \
	scroll.o standout.o toucholap.o touchwin.o tstp.o unctrl.o

# include library definitions
include ../../lib/Makefile.lib

ROOTLIBDIR=	$(ROOT)/usr/ucblib

LIBS += $(LINTLIB)

LINTSRC= $(LINTLIB:%.ln=%)
ROOTLINTDIR= $(ROOTLIBDIR)
ROOTLINT= $(LINTSRC:%=$(ROOTLINTDIR)/%)

$(ROOTLINTDIR)/%: %
	$(INS.file)

CPPFLAGS = -I. -I$(ROOT)/usr/ucbinclude $(CPPFLAGS.master)

objs/refresh.o \
objs/standout.o := CPPFLAGS = -I. $(CPPFLAGS.master)

$(LINTLIB):= SRCS=llib-lcurses
$(LINTLIB):= LINTFLAGS=-nvx
$(LINTLIB):= TARGET_ARCH=

.KEEP_STATE:

all: $(LIBS)

install: $(ROOTLIBS) $(ROOTLINT)

install_h: $(ROOTHDRS)

# include library targets
include ../../lib/Makefile.targ
include ../Makefile.ucbtarg
