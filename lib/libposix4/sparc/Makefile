#
#ident        "@(#)Makefile 1.5     92/05/22 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# libposix4/sparc/Makefile

# placeholder
LIBRARY =	libposix4.a
VERS =		.1


COMDIR =	../common
INCLIST =	-I. -I$(COMDIR) -Iinc -I../../libc/inc

#local objects
#
COMOBJS =	aio.o clock_timer.o fdatasync.o mqueue.o sched.o \
		sem.o shm_open.o shm_unlink.o sigrt.o

STD_MACHOBJS =

NONSTD_MACHOBJS = 

OBJECTS =	$(COMOBJS) $(STD_MACHOBJS) $(NONSTD_MACHOBJS)

#include library definitions
include		../../Makefile.lib

# rtld build rules, all pic for now
pics/%.o:	./%.s
		$(BUILD.s)
		$(POST_PROCESS_O)

pics/%.o:	 ../common/%.c
		$(COMPILE.c) -o $@ $<
		$(POST_PROCESS_O)

LIBS = $(DYNLIB)
CPPFLAGS =	$(INCLIST) $(CPPFLAGS.master) -D_REENTRANT
ASFLAGS =	-K pic -P -D_SYS_SYS_S -D_LOCORE -D_ASM -DPIC -DLOCORE $(CPPFLAGS)
DYNFLAGS =	 -h $(DYNLIB) -ztext
BUILD.s =	$(AS) $(ASFLAGS) $< -o $@

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

