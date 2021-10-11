#
#ident	"@(#)Makefile	1.4	95/07/18 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#

TOPDIR =	../../../../..
PLATFORM =	sun4m
KARCH =		sun4c		 # XXX	Evil.  See comments in ../Makefile.com
ARCHVERS =	v7
PROMVERS =	obp
ARCHOPTS =

ARCH_C_SRC =	fiximp_sun4c.c	sunm_map_prog.c sun4x_memlist.c	fiximp_sun4m.c \
		module_ross.c	module_ross625.c
ARCH_S_SRC =	sparcv7_subr.s	sunm_cache.s	sunm_map.s	module.s \
		module_ross625_asm.s
SRT0_S =	sun4x_srt0.s
INLINES =

LDFLAGS	+=	-L$(TOPDIR)/psm/stand/lib/promif/$(MACH)/$(PROMVERS)
LDLIBS +=

# Conflict with Evil above.
fiximp_sun4m.o module_ross.o module_ross625.o module.o module_ross625_asm.o := \
	CPPINCS = -I$(ROOT)/usr/platform/$(PLATFORM)/include

include ../Makefile.com
