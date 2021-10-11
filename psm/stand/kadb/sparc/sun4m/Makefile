#
#ident "@(#)Makefile	1.18	95/02/14 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
#
# psm/stand/kadb/sparc/sun4m/Makefile
#
# a kernel-architecture-specific kadb Makefile
#

# override global macros as necessary
#TARGET_ARCH =	sun4m
TARGET_ARCH =	sun4c
ARCH =		sun4m
MACH=		sparc
KSUN= 		sun
MMU=		srmmu
ARCHVERS =	v7
PROMTYPE=	obp
PROMLIB=	libprom.a

#
# XXX	-DVAC is provided by machparam.h .. but clearly this
#	isn't going to work too well on a PAC sun4m ..
#
ARCHOPTS= -DMP

ARCHOBJ= $(SPECIAL) machdep.o map.o \
	module_conf.o module.o \
	module_asm.o module_ross.o module_ross_asm.o \
	module_ross625.o module_ross625_asm.o \
	module_vik.o module_tsu.o module_swift.o \
	module_swift_asm.o module_srmmu_asm.o

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
