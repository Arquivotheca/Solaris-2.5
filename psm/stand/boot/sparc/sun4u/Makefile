#
#ident	"@(#)Makefile	1.13	95/04/25 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#

TOPDIR =	../../../../..
PLATFORM =	sun4u
KARCH =		$(PLATFORM)
ARCHVERS =	v9
PROMVERS =	ieee1275
ARCHOPTS =	-D__sparcv9
ASFLAGS +=	-xarch=v8plus

PLAT_C_SRC =	machdep.c
ARCH_C_SRC =	sun4u_memlist.c
ARCH_S_SRC =	sparcv9_subr.s
SRT0_S =	sun4u_srt0.s
INLINES =

LDFLAGS +=	-L$(TOPDIR)/psm/stand/lib/promif/$(MACH)/$(PROMVERS)/common
LDFLAGS +=	-L$(TOPDIR)/psm/stand/lib/promif/$(MACH)/$(PROMVERS)/$(PLATFORM)
PSMLIBS +=	-lplat

#	MPSAS support
MPSAS_BUILD:sh=		echo \\043
$(MPSAS_BUILD)ARCHOPTS += -DMPSAS

include ../Makefile.com
