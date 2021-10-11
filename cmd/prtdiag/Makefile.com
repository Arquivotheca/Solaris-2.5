#
#ident	"@(#)Makefile.com	1.16	93/06/06 SMI"
#
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
# cmd/prtdiag/Makefile.com
#

PROG=	prtdiag

OBJS=	main.o pdevinfo.o display.o

# allow additional kernel-architecture dependent objects to be specified.

OBJS +=	$(KARCHOBJS)

SRCS=	$(OBJS:%.o=%.c)

LNSRC=	$(OBJS:%.o=%.ln)

include ../../Makefile.cmd
include ../../../Makefile.psm

POFILE= 	prtdiag.po
POFILES=	$(OBJS:%.o=%.po)

FILEMODE = 2755
OWNER =	root
GROUP = sys

# These names describe the layout on the target machine

LDLIBS += -lintl

IFLAGS = -I.. -I$(ROOT)/usr/platform/$(PLATFORM)/include

CPPFLAGS = $(IFLAGS) $(CPPFLAGS.master)

# build rules

%.o: ../%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

%.po: ../%.c
	$(COMPILE.cpp) $<  > $<.i
	$(BUILD.po)

%.ln: ../%.c
	$(LINT) -cax $(CPPFLAGS) $<

.KEEP_STATE:

all: $(PROG) 

install: all $(USR_PSM_SBIN_PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE):	$(POFILES)
	$(RM)	$@
	cat	$(POFILES)	> $@

clean:
	$(RM) $(OBJS) $(LNSRC)

lint: $(LNSRC)
	$(LINT.c) $(LNSRC)

include ../../Makefile.targ
include ../../../Makefile.psm.targ
