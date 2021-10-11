#
# ident	"@(#)Makefile	1.11	93/08/18 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#


include ../../Makefile.cmd

PROG = fmli vsig
SRCS = vsig.c main.c
FILEMODE =	0755

# fmli local libraries 
LOCAL = wish oh oeu form qued menu proc vt sys
LDLIBS.local = $(LOCAL:%=../%/lib%.a)
fmli := LDLIBS += $(LDLIBS.local) -lgen -lcurses

CPPFLAGS = -I../inc $(CPPFLAGS.master)
$(PROG) := LDFLAGS +=

.KEEP_STATE:

all: $(PROG) 

fmli: main.c 
	$(LINK.c) -o $@ main.c $(LDLIBS)
	$(POST_PROCESS)

# check if local libraries are up to date.
$(LDLIBS.local):
	@cd $(@D); pwd; $(MAKE) $(MFLAGS) all

install: all $(ROOTPROG)

clean:

lint: $(SRCS)
	$(LINT.c) main.c
	$(LINT.c) vsig.c

include ../../Makefile.targ
