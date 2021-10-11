#
#ident	"@(#)Makefile.com	1.13	95/01/19 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# psm/stand/boot/sparc/Makefile.sparc

include $(TOPDIR)/psm/stand/boot/Makefile.boot

BOOTSRCDIR	= ../..

CMN_DIR		= $(BOOTSRCDIR)/common
MACH_DIR	= ../common
PLAT_DIR	= .

CONF_SRC	= ufsconf.c hsfsconf.c nfsconf.c

CMN_C_SRC	= boot.c heap_kmem.c readfile.c

MACH_C_SRC	= boot_plat.c bootops.c bootprop.c
MACH_C_SRC	+= fiximp.c get.c netops.c

CONF_OBJS	= $(CONF_SRC:%.c=%.o)
CONF_L_OBJS	= $(CONF_OBJS:%.o=%.ln)

SRT0_OBJ	= $(SRT0_S:%.s=%.o)
SRT0_L_OBJ	= $(SRT0_OBJ:%.o=%.ln)
ISRT0_OBJ	= inet_$(SRT0_S:%.s=%.o)
ISRT0_L_OBJ	= $(SRT0_OBJ:%.o=%.ln)		# cheesy hack

C_SRC		= $(CMN_C_SRC) $(MACH_C_SRC) $(ARCH_C_SRC) $(PLAT_C_SRC)
S_SRC		= $(MACH_S_SRC) $(ARCH_S_SRC) $(PLAT_S_SRC)

OBJS		= $(C_SRC:%.c=%.o) $(S_SRC:%.s=%.o)
L_OBJS		= $(OBJS:%.o=%.ln)

# XXX	Gasp! The use of KARCH here is a total hack.  What needs to
#	happen is the boot source needs to be separated out to do
#	compile-time rather than run-time switching.  At present, the
#	sun4c-specific sources are compiled into all platforms, so
#	this kludge is needed.

CPPDEFS		= $(ARCHOPTS) -D$(PLATFORM) -D_BOOT -D_KERNEL -D_MACHDEP
CPPINCS		+= -I$(ROOT)/usr/platform/$(KARCH)/include
CPPINCS		+= -I$(ROOT)/usr/include/$(ARCHVERS)
CPPINCS		+= -I$(PSMSYSHDRDIR) -I$(STANDDIR)
CPPFLAGS	= $(CPPDEFS) $(CPPFLAGS.master) $(CPPINCS)
CPPFLAGS	+= $(CCYFLAG)$(STANDDIR)
ASFLAGS		+= $(CPPDEFS) -P -D_ASM $(CPPINCS)
#
# XXX	Should be globally enabled!
# CFLAGS	+= -v

LDFLAGS		+= -L$(PSMNAMELIBDIR)/$(PLATFORM) -L$(SYSLIBDIR)
LDLIBS		+= -lnames -lsa -lprom $(PSMLIBS) 

#
# Used to convert ELF to an a.out and ensure alignment
#
ELFCONV	= stripalign

.KEEP_STATE:

.PARALLEL:	$(OBJS) $(CONF_OBJS) $(SRT0_OBJ) $(ISRT0_OBJ)
.PARALLEL:	$(L_OBJS) $(CONF_L_OBJS) $(SRT0_L_OBJ) $(ISRT0_L_OBJ)
.PARALLEL:	$(UFSBOOT) $(HSFSBOOT) $(NFSBOOT)

all: $(ELFCONV) $(UFSBOOT) $(HSFSBOOT) $(NFSBOOT)

$(ELFCONV): $(CMN_DIR)/$$(@).c
	$(NATIVECC) -o $@ $(CMN_DIR)/$@.c

# 4.2 ufs filesystem booter

UFS_MAPFILE	= $(MACH_DIR)/mapfile
UFS_LDFLAGS	= -dn -M $(UFS_MAPFILE) -e _start $(LDFLAGS)
UFS_SRT0	= $(SRT0_OBJ)
UFS_OBJS	= $(OBJS) ufsconf.o
UFS_LIBS	= -lufs $(LDLIBS)
UFS_L_OBJS	= $(SRT0_L_OBJ) $(UFS_OBJS:%.o=%.ln)

$(UFSBOOT): $(UFS_MAPFILE) $(UFS_SRT0) $(UFS_OBJS)
	$(LD) $(UFS_LDFLAGS) -o $@ $(UFS_SRT0) $(UFS_OBJS) $(UFS_LIBS)
	$(POST_PROCESS)

$(UFSBOOT)_lint: $(UFS_L_OBJS)
	$(LINT.2) $(LDFLAGS) $(UFS_L_OBJS) $(UFS_LIBS)

# High-sierra filesystem booter.  Probably doesn't work.

HSFS_MAPFILE	= $(MACH_DIR)/mapfile
HSFS_LDFLAGS	= -dn -M $(HSFS_MAPFILE) -e _start $(LDFLAGS)
HSFS_SRT0	= $(SRT0_OBJ)
HSFS_OBJS	= $(OBJS) hsfsconf.o
HSFS_LIBS	= -lhsfs $(LDLIBS)
HSFS_L_OBJS	= $(SRT0_L_OBJ) $(HSFS_OBJS:%.o=%.ln)

$(HSFSBOOT): $(UFS_MAPFILE) $(HSFS_SRT0) $(HSFS_OBJS)
	$(LD) $(HSFS_LDFLAGS) -o $@ $(HSFS_SRT0) $(HSFS_OBJS) $(HSFS_LIBS)
	$(POST_PROCESS)

$(HSFSBOOT)_lint: $(HSFS_L_OBJS)
	$(LINT.2) $(LDFLAGS) $(HSFS_L_OBJS) $(HSFS_LIBS)

# NFS version 2 over UDP/IP booter

NFS_MAPFILE	= $(MACH_DIR)/mapfile.inet
NFS_LDFLAGS	= -dn -M $(NFS_MAPFILE) -e _start $(LDFLAGS)
NFS_SRT0	= $(ISRT0_OBJ)
NFS_OBJS	= $(OBJS) nfsconf.o
NFS_LIBS	= -lnfs_inet $(LDLIBS)
NFS_L_OBJS	= $(ISRT0_L_OBJ) $(NFS_OBJS:%.o=%.ln)

$(NFSBOOT): $(ELFCONV) $(NFS_MAPFILE) $(NFS_SRT0) $(NFS_OBJS)
	$(LD) $(NFS_LDFLAGS) -o $@.elf $(NFS_SRT0) $(NFS_OBJS) $(NFS_LIBS)
	@strip $@.elf
	@mcs -d -a "`date`" $@.elf
	$(RM) $@; ./$(ELFCONV) $@.elf $@

$(NFSBOOT)_lint: $(NFS_L_OBJS)
	$(LINT.2) $(LDFLAGS) $(NFS_L_OBJS) $(NFS_LIBS)

#
# NFS_SRT0 is derived from srt0.s with -DINETBOOT
# UFS_SRT0 and HSFS_SRT0 are built using standard rules.
#
$(NFS_SRT0): $(MACH_DIR)/$(SRT0_S)
	$(COMPILE.s) -DINETBOOT -o $@ $(MACH_DIR)/$(SRT0_S)
	$(POST_PROCESS_O)

$(NFS_SRT0:%.o=%.ln): $(MACH_DIR)/$(SRT0_S)
	@($(LHEAD) $(LINT.s) -DINETBOOT $(MACH_DIR)/$(SRT0_S) $(LTAIL))

include $(BOOTSRCDIR)/Makefile.rules

clean:
	$(RM) $(OBJS) $(CONF_OBJS)
	$(RM) $(SRT0_OBJ) $(ISRT0_OBJ) $(NFSBOOT).elf
	$(RM) $(L_OBJS) $(CONF_L_OBJS)
	$(RM) $(SRT0_L_OBJ) $(ISRT0_L_OBJ)

clobber: clean
	$(RM) $(UFSBOOT) $(HSFSBOOT) $(NFSBOOT) $(ELFCONV)

lint: $(UFSBOOT)_lint $(HSFSBOOT)_lint $(NFSBOOT)_lint

include $(BOOTSRCDIR)/Makefile.targ

FRC:
