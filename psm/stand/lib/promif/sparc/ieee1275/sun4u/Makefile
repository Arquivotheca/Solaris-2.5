#
#ident	"@(#)Makefile	1.5	95/02/14 SMI"
#
# Copyright (c) 1994, Sun Microsystems, Inc.
# All rights reserved.
#
# psm/stand/boot/sparc/ieee1275/sun4u/Makefile
#
# create libplat.a from ieee1275 promif platform dependent source.
#
# NOTE that source is included from the /usr/src/psm/promif/ieee1275/* dirs
#

TOPDIR =	../../../../../../..

include $(TOPDIR)/Makefile.master
include $(TOPDIR)/lib/Makefile.lib
include $(TOPDIR)/psm/stand/lib/Makefile.lib

PLATDIR =	$(TOPDIR)/psm/promif/ieee1275/sun4u
SYSDIR =	$(TOPDIR)/uts

LIBPLAT =	libplat.a
LINTLIBPLAT =	llib-lplat.ln

PLAT_PFILES = 			\
	prom_alloc.c		\
	prom_cpuctl.c		\
	prom_getunum.c		\
	prom_heartbeat.c	\
	prom_idprom.c		\
	prom_init.c		\
	prom_macaddr.c		\
	prom_map.c		\
	prom_mem.c		\
	prom_mmu.c		\
	prom_sparc.c

KARCH =		sun4u
MMU =		sfmmu

OBJSDIR =	objs

PLAT_POBJ =	$(PLAT_PFILES:%.c=$(OBJSDIR)/%.o)
OBJS =		$(PLAT_POBJ)
L_OBJS =	$(OBJS:%.o=%.ln)

ARCHOPTS=	-Dsun4u -D__sparcv9
ASFLAGS =	-P -D__STDC__ -D_BOOT -D_ASM
CPPDEFS	=	$(ARCHOPTS) -D$(KARCH) -D_BOOT -D_KERNEL -D_MACHDEP
CPPINCS =	-I. -I$(SYSDIR)/$(KARCH) -I$(SYSDIR)/$(MMU) \
		-I$(SYSDIR)/sparc/v9 -I$(SYSDIR)/sparc \
		-I$(SYSDIR)/sun -I$(SYSDIR)/common
CPPFLAGS=	$(CPPDEFS) $(CPPINCS) $(CPPFLAGS.master)
CFLAGS +=	-v

.KEEP_STATE:

.PARALLEL:	$(OBJS) $(L_OBJS)

all install: $(LIBPLAT) lint

lint: $(LINTLIBPLAT)

clean:
	$(RM) $(OBJS) $(L_OBJS)

clobber: clean
	$(RM) $(LIBPLAT) $(LINTLIBPLAT) a.out core

$(LIBPLAT): $(OBJSDIR) .WAIT $(OBJS)
	$(BUILD.AR) $(OBJS)

$(LINTLIBPLAT): $(OBJSDIR) .WAIT $(L_OBJS)
	@$(ECHO) "\nlint library construction:" $@
	@$(LINT) $(LINTFLAGS.lib) -o plat $(L_OBJS)

$(OBJSDIR):
	-@[ -d $@ ] || mkdir $@

#
# build rules using standard library object subdirectory
#
$(OBJSDIR)/%.o: $(PLATDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

$(OBJSDIR)/%.o: $(PLATDIR)/%.s
	$(COMPILE.s) -o $@ $<
	$(POST_PROCESS_O)

$(OBJSDIR)/%.ln: $(PLATDIR)/%.c
	@($(LHEAD) $(LINT.c) $< $(LTAIL))
	@$(MV) $(@F) $@

$(OBJSDIR)/%.ln: $(PLATDIR)/%.s
	@($(LHEAD) $(LINT.s) $< $(LTAIL))
	@$(MV) $(@F) $@
