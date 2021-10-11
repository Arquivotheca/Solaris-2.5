#
#ident	"@(#)Makefile 1.1	94/11/09 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#

# placeholder
LIBRARY = libdoor.a
VERS = .1


COMDIR=		../common
INCLIST=	-I. -I$(COMDIR) -Iinc -I../../libc/inc

#local objects
#
COMOBJS= door_calls.o

OBJECTS= $(COMOBJS)

#include library definitions
include		../../Makefile.lib

# rtld build rules, all pic for now
pics/%.o:	./%.s
		$(BUILD.s)
		$(POST_PROCESS_O)

pics/%.o:	 ../common/%.c
		$(COMPILE.c) -o $@ $<
		$(POST_PROCESS_O)

objs/%.o:	./%.s
		$(BUILD.s)
		$(POST_PROCESS_O)

objs/%.o:	 ../common/%.c
		$(COMPILE.c) -o $@ $<
		$(POST_PROCESS_O)

LIBS = $(DYNLIB)
CPPFLAGS=	$(INCLIST) $(CPPFLAGS.master)
ASFLAGS=	-K pic -P -D_SYS_SYS_S -D_LOCORE -D_ASM -DPIC -DLOCORE $(CPPFLAGS)
DYNFLAGS=	 -h $(DYNLIB) -ztext
BUILD.s=	$(AS) $(ASFLAGS) $< -o $@

$(ROOTLIBDIR)/$(DYNLIB):=	FILEMODE = 755

# list of sources for lint
SRCS=		$(COMOBJS:%.o=../common/%.c)  $(MACHOBJS:.o=.c)
LINTFLAGS +=	-Dsun -DDEBUG
CLEANFILES +=	$(LINTOUT)

.KEEP_STATE:

all:		$(DYNLIB)

install:	$(ROOTLINKS) $(DYNLIB)

include ../../Makefile.targ

lint:		$(LINTOUT)

$(LINTOUT):	$(SRCS)
		$(LINT.c) $(SRCS) > $(LINTOUT) 2>&1
