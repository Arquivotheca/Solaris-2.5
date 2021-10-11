#
#ident	"@(#)Makefile.com	1.18	94/12/07 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# psm/stand/cpr/common/Makefile.com
#
GREP	=	egrep
TOPDIR	=	../../../..

include $(TOPDIR)/Makefile.psm

SYSDIR	=  	$(TOPDIR)/uts
BOOTDIR	= 	../../boot
COMDIR	=  	../common
SUN4MDIR=	../sun4m
ARCHDIR	= 	$(SYSDIR)/${ARCH}
MACHDIR	= 	$(SYSDIR)/$(MACH)
MMUDIR	=	$(SYSDIR)/$(MMU)
CPRDIR  =       $(TOPDIR)/uts/sun4m/cpr
PROMLIBDIR=	$(BOOTDIR)/sparc/obp
PROMLIB=	$(PROMLIBDIR)/libprom.a

LOCORE	=	locore.o

OBJ	=	cpr.o support.o common.o stubs.o cpr_compress.o

L_OBJS	=	$(OBJ:%.o=%.ln)

CPPDEFS=	$(ARCHOPTS) -D$(ARCH) -D_KERNEL -D_MACHDEP -D__ELF
CPPINCS=	-I. -I${COMDIR} -I$(ARCHDIR) -I$(MMUDIR) -I$(MACHDIR) \
		-I$(MACHDIR)/$(ARCHVER)	\
		-I$(CPRDIR) -I$(SYSDIR)/sun4m/sys -I$(SYSDIR)/sun4m \
		-I$(SYSDIR)/sun -I$(SYSDIR)/common -I$(TOPDIR)/head
CPPOPTS=  	$(CPPDEFS)
COPTS=
CFLAGS=		${COPTS} ${CPPOPTS}
ASFLAGS= 	-P -D_ASM $(CPPOPTS) -DLOCORE -D_LOCORE -D__STDC__
CPPFLAGS=	$(CPPINCS) $(CCYFLAG)$(SYSDIR)/common $(CPPFLAGS.master)
AS_CPPFLAGS=	$(CPPINCS) $(CPPFLAGS.master)

CPRBOOTERS=	cprboot

# install values
CPRFILES=	$(CPRBOOTERS:%=$(ROOT_PSM_DIR)/$(ARCH)/%)
FILEMODE=	644
OWNER=		root
GROUP=		sys

# lint stuff
LINTFLAGS += -Dlint
LOPTS = -hbxn
LTMP = /tmp/lint.$(BOOTBLK)

# install rule
$(ROOT_PSM_DIR)/$(ARCH)/%: %
	$(INS.file)

ALL=		cprboot
all:	$(ALL)

install: all $(CPRFILES)

# build rule
%.o: $(COMDIR)/%.c
	$(COMPILE.c) $<
%.o: $(CMDIR)/%.c
	$(COMPILE.c) $<

.KEEP_STATE:

cprboot: ucpr.o mapfile 
	${LD} -dn -M mapfile -e start -Bstatic -o $@ ucpr.o

cpr_compress.o:	$(SYSDIR)/common/cpr/cpr_compress.c
	${NATIVECC} -c -g $@ $(CFLAGS) $(CPPFLAGS) \
		$(SYSDIR)/common/cpr/cpr_compress.c

cpr.o:	${COMDIR}/cpr.c 
	${NATIVECC} -c -g $@ $(CFLAGS) $(CPPFLAGS) ${COMDIR}/cpr.c

ucpr.o: $(LOCORE) ${OBJ} ${ARCHOBJ} $(PROMLIB)
	${LD} -r -o $@ $(LOCORE) ${OBJ} ${ARCHOBJ} $(PROMLIB)

$(PROMLIB): FRC
	@cd $(@D); pwd; $(MAKE) $(@F)
	@pwd


$(ROOTDIR):
	$(INS.dir)

lint: $(L_OBJS)
	@#cat $(L_OBJS) > $(LTMP)
	@#echo "Global Cross-checks:"
	@#${LINT2} $(LTMP)
	@#$(RM) $(LTMP)

clean:
	$(RM) $(SUN4MDIR)/*.o

clobber: clean
	$(RM) cprboot

%.ln: %.c
	$(LINT) $(LINTFLAGS) $(CPPDEFS) $(CPPFLAGS) -c $<

%.ln: $(COMDIR)/%.c
	$(LINT) $(LINTFLAGS) $(CPPDEFS) $(CPPFLAGS) -c $<

FRC:
