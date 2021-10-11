#
#ident	"@(#)Makefile.com	1.58	94/10/25 SMI"
#
# Copyright (c) 1990-1994 by Sun Microsystems, Inc.
#
# stand/lib/common/Makefile.com
#
# Standalone Library common makefile
#
# This file is included by ../[sun,i386]/Makefile and the target libraries
# are built in ../[sun,i386].
# 
# Certain symbols must be defined before this Makefile may be included:
#
#	KARCH		The target architecture (eg, "sun4c" or "i86pc")
#			KARCH is used in place of KERNARCH to override the
#			value in Makefile.master.
#	ARCHOBJ		architecture-specific objects in ../sun or ../i386
#	CONFOBJ		Common devices as listed in ../[sun,i386]/conf.c

# kadb uses libsadb.a and libkadbprom.a
LIBSA=		libsa.a
LIBKADB=	libsadb.a
LIBNFS_INET=	libnfs_inet.a
LIBCOMPFS=	libcompfs.a
LIBCOM=		libcom.a
LIBUFS=		libufs.a
LIBPCFS=	libpcfs.a
LIBHSFS=	libhsfs.a
LIBPROM=	libprom.a
LIBPROMKADB=	libkadbprom.a
ALLLIBS=	$(LIBSA) $(LIBPROMDB) $(LIBPROM) $(LIBKADB) $(LIBPCFS) \
			$(LIBCOM) $(LIBPROMKADB) $(LIBUFS) $(LIBHSFS) \
			$(LIBNFS_INET) $(LIBCOMPFS)

SPRINTF=	sprintf.o
LOC_CPP=

# Common object files
CMNOBJ=	common.o memlist.o printf.o $(SPRINTF) standalloc.o subr_xxx.o

# The library also includes files in ../$(KARCH) and ../$(MACH)
OBJECTS= $(ARCHOBJ) $(MACHOBJ) $(CMNOBJ) $(CONFOBJ)

.PARALLEL: $(ARCHOBJ) $(MACHOBJ) $(CMNOBJ) $(CONFOBJ)

# include global library definitions
include ../../../lib/Makefile.lib

# overwrite OBJS macro to allow (different) destination directories.
# for now, objs (for boot) and kadbobjs (for kadb)
OBJS=		$(OBJECTS:%=$(OBJSDIR)/%)
LINTOBJS=	$(OBJECTS:%.o=$(OBJSDIR)/%.ln)

AROBJS=		$(OBJS)

STANDDIR=	../..
SYSDIR=		../../../uts

FSDIR=		../fs
COMPFSDIR=	$(FSDIR)/compfs
COMDIR=		$(FSDIR)/common
UFSDIR=		$(FSDIR)/ufs
HSFSDIR=	$(FSDIR)/hsfs
NFS_INETDIR=	$(FSDIR)/nfs_inet
PCFSDIR=	$(FSDIR)/pcfs

# Use I386BOOT to differentiate from KADB in compiling promif code
# and other appropriate places.  (for i386 only)
CPPDEFS= 	$(ARCHOPTS) -D$(KARCH) -D_BOOT -D_KERNEL -D_MACHDEP

CPPINCS-i386= 	$(LOC_CPP) -I. -I$(STANDDIR)/i386 -I$(STANDDIR) \
		-I$(SYSDIR)/$(KARCH) -I$(SYSDIR)/$(ARCH) -I$(SYSDIR)/$(MMU) \
		-I$(SYSDIR)/$(MACH) -I$(SYSDIR)/common

CPPINCS-sparc= 	$(LOC_CPP) -I. -I$(STANDDIR) -I$(SYSDIR)/$(KARCH) \
		-I$(SYSDIR)/$(MMU) \
		-I$(SYSDIR)/sparc -I$(SYSDIR)/sun -I$(SYSDIR)/common

CPPINCS= 	$(CPPINCS-$(MACH))

i386-CFLAGS=    -O
sparc-CFLAGS=   -O
CFLAGS=         $($(MACH)-CFLAGS)

CPPFLAGS=	$(CPPDEFS) $(CPPINCS) $(CPPBOOT) $(CPPFLAGS.master)
AS_CPPFLAGS=	$(CPPDEFS) $(CPPINCS) $(CPPBOOT) $(CPPFLAGS.master)
ASFLAGS=	-P -D__STDC__ -D_ASM

LINTLIBSA	= llib-lsa.ln
LINTLIBKADB	= llib-lsadb.ln
LINTLIBNFS_INET	= llib-lnfs_inet.ln
LINTLIBUFS	= llib-lufs.ln
LINTLIBPCFS	= llib-pcfs.ln
LINTLIBHSFS	= llib-lhsfs.ln
LINTLIBCOMPFS	= llib-lcompfs.ln
LINTLIBCOM	= llib-lcom.ln
LINTLIBPROMKADB	= llib-lkadbprom.ln
LINTLIBPROM	= llib-lprom.ln
LINTALLLIBS	= $(LINTLIBSA) $(LINTLIBUFS) $(LINTLIBNFS_INET) $(LINTLIBPROM) \
		  $(LINTLIBCOMPFS) $(LINTLIBCOM) \
		  $(LINTLIBPCFS) $(LINTLIBKADB) $(LINTLIBHSFS) $(LINTLIBPROMKADB)

CMNDIR=	../common
CMNSRC=	$(CMNOBJ:%.o=$(CMNDIR)/%.c)
CMNLINTOBJ=$(CMNOBJ:%.o=$(CMNDIR)/%.c)

# conditional assignments, affecting the lists of OBJECTS used by targets
#
$(LIBKADB)	:=	SPRINTF =
$(LIBKADB)	:=	CONFOBJ =

# Rules for common .c files
$(OBJSDIR)/%.o: $(CMNDIR)/%.c
	$(COMPILE.c) $(BOOTCFLAGS) -o $@ $<
	$(POST_PROCESS_O)

$(OBJSDIR)/%.ln: $(CMNDIR)/%.c
	@($(LHEAD) $(LINT.c) $(BOOTCFLAGS) -c $< $(LTAIL))
	@$(MV) $(@F) $@

.PRECIOUS: $(ALLLIBS)

include $(MACHDIR)/Makefile.mach

.KEEP_STATE:

alllibs: $(ALLLIBS)

$(ALLLIBS): $(OBJSDIR) .WAIT $$(OBJS)
	$(BUILD.AR)

$(OBJSDIR):
	-@[ -d $@ ] || mkdir $@

clobber: clean
	$(RM) $(LINTOBJ) $(ALLLIBS) $(LINTALLLIBS)

clean:
	$(RM) kadbobjs/*.o objs/*.o *.a a.out *.i core make.out
	$(RM) kadbobjs/*.ln objs/*.ln

alllintlibs:  $(OBJSDIR) .WAIT $(LINTALLLIBS)

$(LINTLIBSA): $$(LINTOBJS)
	@-$(ECHO) "\n (libsa lint library construction):"
	@$(LINT) -o sa -xum $(LINTFLAGS) $(LINTOBJS)

install:
