#
#ident	"@(#)Makefile	1.17	95/07/14 SMI"
#

# Copyright (c) 1989 by Sun Microsystems, Inc.

#
# ucblib/libucb/sparc/Makefile
#
# This builds all objects needed for libucb, including those derived
# from source under ../port.
#
LIBRARY= libucb.a
VERS = .1
M4=	 m4
CPP=	 /usr/lib/cpp

# objects are grouped by source directory.

# local objects
#
GENOBJS=

SYSOBJS= \
signal.o    syscall.o

# objects from source under ../port
# the last two objects in PORTGEN come from derived C source
#
PORTGEN= \
_psignal.o  bcmp.o      bcopy.o     bzero.o    ftime.o \
getwd.o     index.o \
nice.o      nlist.o     psignal.o \
rand.o      readdir.o   regex.o     rindex.o \
scandir.o   setbuffer.o setpriority.o \
siglist.o   statfs.o \
times.o     ualarm.o    usleep.o    sleep.o

PORTSTDIO= \
doprnt.o    fopen.o     fprintf.o   printf.o    sprintf.o  \
vfprintf.o  vprintf.o   vsprintf.o

# NOTE GETRUSAGE IS BEING BOUGHT BACK INTO BASE, REMOVE
# FROM THAT LIST WHEN THAT HAPPENS

PORTSYS= \
flock.o         getdtblsize.o    gethostid.o    gethostname.o\
getpagesize.o   gettimeofday.o   killpg.o       mctl.o \
reboot.o        setjmp.o         wait3.o \
wait4.o         getrusage.o      setpgrp.o

# GENTXTS = portable source unused because of sparc-specific sources, see TXTS
GENTXTS=

OBJECTS= $(GENOBJS) $(SYSOBJS) $(PORTGEN) $(PORTSTDIO) $(PORTSYS)

# libc build rules
objs/%.o profs/%.o pics/%.o: crt/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: crt/%.s
	$(BUILD.s)
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: gen/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: gen/%.s
	$(BUILD.s)
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: fp/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: fp/%.s
	$(BUILD.s)
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: sys/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: sys/%.s
	$(BUILD.s)
	$(POST_PROCESS_O)
#
# libc build rules for objects built from "portable" source in ../port
objs/%.o profs/%.o pics/%.o %.o: ../port/gen/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: ../port/print/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: ../port/stdio/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: ../port/sys/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)


# include common library definitions
include ../../../lib/Makefile.lib

CPPFLAGS= -Dsparc -I$(ROOT)/usr/ucbinclude -Iinc -I../inc $(CPPFLAGS.master)
ASFLAGS=  -P -D__STDC__ -DLOCORE -D_SYS_SYS_S -D_ASM $(CPPFLAGS)
LDLIBS += -lc -lelf

pics/%.o:= ASFLAGS += -K pic

BUILD.s=  $(AS) $(ASFLAGS) $< -o $@

# libc method of building an archive, using AT&T ordering
BUILD.AR= $(RM) $@ ; \
	$(AR) q $@ `$(LORDER) $(OBJECTS:%=$(DIR)/%)| $(TSORT)`

# the TXTS macro is used for NSE bootstrapping.
TXTS=	inc/SYS.h inc/PIC.h $(GENTXTS:%=../port/gen/%)

LIBS = $(LIBRARY) $(DYNLIB) $(LINTLIB)

ROOTLIBDIR=     $(ROOT)/usr/ucblib

# list of C source for lint
SRCS= \
$(PORTGEN:%.o=../port/gen/%.c)      \
$(PORTSTDIO:%.o=../port/stdio/%.c)  $(PORTSYS:%.o=../port/sys/%.c)

.KEEP_STATE:

all: $(TXTS) $(LIBS) $(PLIB)

$(LINTLIB):= SRCS=llib-lucb
$(LINTLIB):= LINTFLAGS=-nvx
$(LINTLIB):= TARGET_ARCH=

# include common library targets
include ../../../lib/Makefile.targ

# special cases


# derived C source and related explicit dependencies
#

# explicit dependency for the NSE, needed because the build from
# above does not use the all target.
$(LIBRARY): $(TXTS)
