#
#ident	"@(#)Makefile	1.28	95/02/14 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
#
# psm/stand/kadb/sparc/sun4c/Makefile
#
# a kernel-architecture-specific kadb Makefile
#

# override global macros as necessary
TARGET_ARCH =	sun4c
ARCH =		sun4c
MACH=		sparc
KSUN=   	sun
MMU=		sunmmu
ARCHVERS =	v7
PROMTYPE=	obp
PROMLIB=	libprom.a

ARCHOPTS =

ARCHOBJ= $(SPECIAL) machdep.o map.o

# include architecture specific kadb definitions
include ../common/Makefile.sparc

# include common kadb definitions and initial targets
include ../../common/Makefile.com

$(ARCHOBJ): assym.s

assym.s: genassym
	./genassym > $@

genassym: genassym.c
	$(NATIVECC) $(CFLAGS) $(CPPINCS) -o $@ genassym.c
