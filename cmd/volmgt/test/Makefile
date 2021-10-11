# Copyright 1992, Sun Microsystems
#
# Makefile for volume management test programs
#
# "@(#)Makefile	1.7	95/01/27 SMI"
#

include ../Makefile.volmgt

OBJS=		vttest.o voltestdrv.o
SRCS=		$(OBJS:%.o=%.c)

KSRC=		${CODEMGR_WS}/usr/src/uts

RAM_ARCH:sh=	uname -m
MACH:sh=	uname -p

KERNEL_CDEFS =	-D_KERNEL -DDEBUG -DSYSACCT -DC2_AUDIT -DQUOTA

sparc_KERNEL_CINC =	-I${KSRC}/sparc
i386_KERNEL_CINC =	-I${KSRC}/i86pc -I${KSRC}/i86
KERNEL_CINC =	$($(MACH)_KERNEL_CINC) -I${KSRC}/sun -Y I,${KSRC}/common

sparc_CFLAGS =	-xcg89 -Wa,-cg92
i386_CFLAGS=
COPTFLAG=-O
CFLAGS= $($(MACH)_CFLAGS)

PROGS= 	vttest voltestdrv

all: ${PROGS}

vttest:	vttest.o
	${CC} ${CFLAGS} -o $@ vttest.o

voltestdrv: voltestdrv.c voltestdrv.h
	${COMPILE.c} $(COPTFLAG) ${KERNEL_CDEFS} ${KERNEL_CINC} $@.c
	${LD} -r -o $@ $@.o

install: all

cstyle:
	${CSTYLE} ${SRCS}

lint:
	${LINT.c} ${CFLAGS} ${SRCS}

depend:

clean:
	${RM} *.o a.out core

clobber: clean
	${RM} ${PROGS}

