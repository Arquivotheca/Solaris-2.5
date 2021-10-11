#
#ident	"@(#)Makefile	1.18	95/02/14 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
#
# psm/stand/kadb/sparc/sun4d/Makefile
#
# a kernel-architecture-specific kadb Makefile
#

# override global macros as necessary
#TARGET_ARCH =	sun4d
TARGET_ARCH =	sun4c
ARCH =		sun4d
MACH=		sparc
KSUN=   	sun
MMU=		srmmu
ARCHVERS=	v7
PROMTYPE=	obp
PROMLIB=	libprom.a

ARCHOPTS=	-DMP

ARCHOBJ= $(SPECIAL) machdep.o map.o

# include architecture specific kadb definitions
include ../common/Makefile.sparc

# include common kadb definitions and initial targets
include ../../common/Makefile.com

$(ARCHOBJ): assym.s 

assym.s: genassym 
	./genassym > $@ 

genassym: genassym.c
	(unset LD_LIBRARY_PATH; \
	$(NATIVECC) $(CFLAGS) $(CPPINCS) -o $@ genassym.c)
