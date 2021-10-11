#
#ident	"@(#)Makefile	1.4	92/12/15 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/fmli/vt/Makefile
#
LIBRARY = libvt.a

OBJECTS = fits.o hide.o highlight.o indicator.o lp.o makebox.o \
	message.o move.o offscreen.o physical.o redraw.o system.o \
	vclose.o vcolor.o vcreate.o vctl.o vcurrent.o vdebug.o \
	vflush.o vfork.o vinit.o vmark.o vreshape.o wclrwin.o \
	wdelchar.o wgetchar.o wgo.o winschar.o wprintf.o wputchar.o \
	wputs.o wreadchar.o wscrollwin.o showmail.o showdate.o \
	working.o

# include library definitions
include ../../../lib/Makefile.lib


# inc must be first.
CPPFLAGS = -I../inc $(CPPFLAGS.master)

.KEEP_STATE:

all: $(LIBS)

install: all
	@echo "$(LIBRARY) is not installed."

# include library targets
include ../../../lib/Makefile.targ
