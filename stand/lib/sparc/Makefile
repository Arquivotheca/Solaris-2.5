#
#ident	"@(#)Makefile	1.39	94/11/21 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# stand/lib/sparc/Makefile
#
# SPARC architecture Makefile for Standalone Library
#

TOPDIR	=	../../..

# XXX - libsa.a is supposedly architecture-neutral; however, some more or
# less generic headers include machine specific headers like machparam.h.
# To satisfy such inclusions, even though the contents of those headers
# aren't used, we have to define $(KARCH) and $(ARCHVERS).
KARCH=	sun4c
ARCHVERS= v7

include $(TOPDIR)/Makefile.master
include $(TOPDIR)/lib/Makefile.lib

LIBSA=		libsa.a
LIBKADB=	libsadb.a
LIBUFS=		libufs.a
LIBHSFS=	libhsfs.a
LIBNFS_INET=	libnfs_inet.a
ALLLIBS=	$(LIBSA) $(LIBUFS) $(LIBHSFS) $(LIBNFS_INET) $(LIBKADB)

STANDDIR=	../..
SYSDIR	=	$(TOPDIR)/uts

CPPDEFS	= 	$(ARCHOPTS) -D$(KARCH) -D_BOOT -D_KADB -D_KERNEL -D_MACHDEP
CPPINCS	= 	$(LOC_CPP) -I. -I$(STANDDIR) -I$(SYSDIR)/sparc \
		-I$(SYSDIR)/$(KARCH) -I$(SYSDIR)/$(MACH)/$(ARCHVERS) \
		-I$(SYSDIR)/sun -I$(SYSDIR)/common
ASINCS	=	$(CPPINCS)
CPPFLAGS=	$(CPPDEFS) $(CPPINCS) $(CPPBOOT) $(CPPFLAGS.master)
AS_CPPFLAGS=	$(CPPFLAGS)
ASFLAGS=	-P -D__STDC__ -D_ASM

FSDIR=		../fs
FSCMNDIR=	$(FSDIR)/common
UFSDIR=		$(FSDIR)/ufs
HSFSDIR=	$(FSDIR)/hsfs
NFS_INETDIR=	$(FSDIR)/nfs_inet

include $(UFSDIR)/Makefile.ufs
include $(HSFSDIR)/Makefile.hsfs
include $(NFS_INETDIR)/Makefile.nfs_inet

# CMNDIR - architecture independent code
# OBJSDIR - where the .o's go

CMNDIR=		../common
OBJSDIR=	objs

# conditional assignments, affecting the lists of OBJECTS used by targets
#
SPRINTF=	sprintf.o
$(LIBKADB)	:=	SPRINTF =

CMNOBJ= printf.o standalloc.o memlist.o
FSCMNOBJ= cache.o diskread.o fsswitch.o
MACHOBJ= sadiv.o strsubr.o _setjmp.o samul.o

GENOBJS=	$(CMNOBJ) $(FSCMNOBJ) $(MACHOBJ)
PAROBJS=	$(GENOBJS:%=$(OBJSDIR)/%)
.PARALLEL:	$(PAROBJS)

OBJECTS= $(GENOBJS) $(SPRINTF) 

OBJS=		$(OBJECTS:%=$(OBJSDIR)/%)

# Arch-specific device configuration as listed in conf.c
CONFOBJ=

LDFLAGS= -r

.KEEP_STATE:

all install alllibs: $(ALLLIBS)

lint:
	@echo "lint not in service for libsa.a yet"

$(ALLLIBS): $(OBJSDIR) .WAIT $$(OBJS)
	$(BUILD.AR) $(OBJS)

$(OBJSDIR):
	-@[ -d $@ ] || mkdir $@

clobber: clean
	$(RM) $(LINTOBJ) $(ALLLIBS) $(LINTALLLIBS)

clean:
	$(RM) kadbobjs/*.o objs/*.o *.a a.out *.i core make.out
	$(RM) kadbobjs/*.ln objs/*.ln

# build rules using standard library object subdirectory
#
$(OBJSDIR)/%.o: %.s
	$(COMPILE.s) -o $@ $<
	$(POST_PROCESS_O)
$(OBJSDIR)/%.o: %.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
$(OBJSDIR)/%.o: $(CMNDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
$(OBJSDIR)/%.o: $(CMNDIR)/%.s
	$(COMPILE.s) -o $@ $<
	$(POST_PROCESS_O)
$(OBJSDIR)/%.o: $(UFSDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
$(OBJSDIR)/%.o: $(HSFSDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
$(OBJSDIR)/%.o: $(NFS_INETDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
$(OBJSDIR)/%.o: $(FSCMNDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
