#
# @(#)Makefile 93/08/25 SMI; SunOS BSM
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/allocate/Makefile
#

include		../Makefile.cmd

TEXT_DOMAIN=	SUNW_BSM_ALLOCATE
POS=		allocate.po audio_clean.po clean.po
include		../Makefile.cmd.bsm

ROOTSEC =	$(ROOTETC)/security
ROOTSECDEV =	$(ROOTSEC)/dev
ROOTSECLIB =	$(ROOTSEC)/lib
ROOTDIRS =	$(ROOTSECDEV) $(ROOTSECLIB)

PROG1 =		allocate
PROG2 =		mkdevmaps mkdevalloc
PROG =		$(PROG1) $(PROG2)
DEFILE =	deallocate
LISTFILE =	list_devices
RTLCKS =	audio fd0 sr0 st0 st1
SCRIPTS =	st_clean fd_clean sr_clean audio_clean

ROOTPROG =	$(PROG1:%=$(ROOTUSRSBIN)/%) $(PROG2:%=$(ROOTUSRSBIN)/%)
ROOTLOCKS=	$(RTLCKS:%=$(ROOTSECDEV)/%)
ROOTSCRIPTS=	$(SCRIPTS:%=$(ROOTSECLIB)/%)

allocate :=	POBJS = allocate.o  allocate3.o
mkdevmaps :=	POBJS = mkdevmaps.o
mkdevalloc :=	POBJS = mkdevalloc.o
audio_clean :=	POBJS = audio_clean.o

OBJS =		allocate.o allocate3.o mkdevmaps.o mkdevalloc.o
SRCS =		$(OBJS:%.o=%.c)

FILEMODE=	0755
DIRMODE=	0755
OWNER=		root
GROUP=		sys
$(ROOTUSRSBIN)/% :=	FILEMODE = 04755
$(ROOTUSRSBIN)/% :=	OWNER = root
$(ROOTUSRSBIN)/% :=	GROUP = bin
$(ROOTSECDEV)/%  :=	FILEMODE = 0600
$(ROOTSECDEV)/%  :=	OWNER = bin
$(ROOTSECDEV)/%  :=	GROUP = bin

LDLIBS +=	-lnsl -lbsm -lintl

.KEEP_STATE:

all :		$(PROG) $(RTLCKS) $(SCRIPTS)

install :	all $(ROOTDIRS) $(ROOTPROG) $(ROOTLOCKS) $(ROOTSCRIPTS)
		$(RM) $(ROOTUSRSBIN)/$(DEFILE)
		$(LN) $(ROOTUSRSBIN)/$(PROG1) $(ROOTUSRSBIN)/$(DEFILE)
		$(RM) $(ROOTUSRSBIN)/$(LISTFILE)
		$(LN) $(ROOTUSRSBIN)/$(PROG1) $(ROOTUSRSBIN)/$(LISTFILE)

$(PROG) :	$$(POBJS)
		$(LINK.c) $(POBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

$(RTLCKS):
	touch $@

$(ROOTDIRS):
		$(INS.dir)

$(ROOTSECDEV)/%: %
	$(INS.file)

$(ROOTSECLIB)/%: %
	$(INS.file)

clean :
		$(RM) $(OBJS) $(RTLCKS) $(ROOTCLEAN) $(SCRIPTS)

lint :		lint_SRCS

strip :
		$(STRIP) $(PROG)

include		../Makefile.targ
