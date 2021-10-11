#
#ident	"@(#)Makefile	1.3	95/01/19 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#

TOPDIR =	../../../../..
PLATFORM =	sun4d
KARCH =		sun4c		 # XXX	Evil.  See comments in ../Makefile.com
ARCHVERS =	v7
PROMVERS =	obp
ARCHOPTS =

ARCH_C_SRC =	fiximp_sun4c.c	sunm_map_prog.c sun4x_memlist.c
ARCH_S_SRC =	sparcv7_subr.s	sunm_cache.s	sunm_map.s
SRT0_S =	sun4x_srt0.s
INLINES =

LDFLAGS	+=	-L$(TOPDIR)/psm/stand/lib/promif/$(MACH)/$(PROMVERS)
LDLIBS +=

include ../Makefile.com
