#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# ident	"@(#)Makefile.cmd	1.35	95/07/10 SMI"
#
# cmd/Makefile.cmd
#
# Definitions common to command source.
#
# include global definitions; SRC should be defined in the shell.
# SRC is needed until RFE 1026993 is implemented.

include $(SRC)/Makefile.master

LN=		ln
CP=		cp
SH=		sh
ECHO=		echo
MKDIR=		mkdir
TOUCH=		touch

FILEMODE=	0555
LIBFILEMODE=	0444
STATIC=		$(STATPROG:%=%.static)
XPG4=		$(XPG4PROG:%=%.xpg4)

ROOTBIN=	$(ROOT)/usr/bin
ROOTLIB=	$(ROOT)/usr/lib
ROOTSHLIB=	$(ROOT)/usr/share/lib
ROOTSBIN=	$(ROOT)/sbin
ROOTUSRSBIN=	$(ROOT)/usr/sbin
ROOTUSRSBINSTAT=$(ROOT)/usr/sbin/static
ROOTETC=	$(ROOT)/etc
ROOTCCSBIN=	$(ROOT)/usr/ccs/bin
ROOTUSRKVM=	$(ROOT)/usr/kvm
ROOTXPG4=	$(ROOT)/usr/xpg4
ROOTXPG4BIN=	$(ROOT)/usr/xpg4/bin
ROOTLOCALEDEF=	$(ROOT)/usr/lib/localedef
ROOTCHARMAP=	$(ROOTLOCALEDEF)/charmap

# storing LDLIBS in two macros allows reordering of options
LDLIBS.cmd =	$(ENVLDLIBS1)  $(ENVLDLIBS2)  $(ENVLDLIBS3)
LDLIBS =	$(LDLIBS.cmd)
LDFLAGS.cmd =	$(STRIPFLAG) $(ENVLDFLAGS1) $(ENVLDFLAGS2) $(ENVLDFLAGS3)
LDFLAGS =	$(LDFLAGS.cmd)
$(STATIC) :=	LDFLAGS = $(LDFLAGS.cmd) -dn

LINTFLAGS=	-ax
LINTOUT=	lint.out

ROOTPROG=	$(PROG:%=$(ROOTBIN)/%)
ROOTSHFILES=	$(SHFILES:%=$(ROOTBIN)/%)
ROOTLIBPROG=	$(PROG:%=$(ROOTLIB)/%)
ROOTLIBSHFILES= $(SHFILES:%=$(ROOTLIB)/%)
ROOTSHLIBPROG=	$(PROG:%=$(ROOTSHLIB)/%)
ROOTSBINPROG=	$(PROG:%=$(ROOTSBIN)/%)
ROOTUSRSBINPROG=$(PROG:%=$(ROOTUSRSBIN)/%)
ROOTBOOTPROG=	$(STATPROG:%=$(ROOTSBINSTAT)/%)
ROOTSTATPROG=	$(STATPROG:%=$(ROOTUSRSBINSTAT)/%)
ROOTETCPROG=	$(PROG:%=$(ROOTETC)/%)
ROOTCCSBINPROG=	$(PROG:%=$(ROOTCCSBIN)/%)
ROOTUSRKVMPROG=	$(PROG:%=$(ROOTUSRKVM)/%)
ROOTXPG4PROG=	$(XPG4PROG:%=$(ROOTXPG4BIN)/%)
ROOTLOCALEPROG=	$(PROG:%=$(ROOTLOCALEDEF)/%)

$(ROOTBIN)/%: %
	$(INS.file)

$(ROOTLIB)/%: %
	$(INS.file)

$(ROOTSHLIB)/%: %
	$(INS.file)

$(ROOTSBIN)/%: %
	$(INS.file)

$(ROOTUSRSBIN)/%: %
	$(INS.file)

$(ROOTETC)/%: %
	$(INS.file)

$(ROOTCCSBIN)/%: %
	$(INS.file)

$(ROOTUSRKVM)/%: %
	$(INS.file)

$(ROOTUSRSBINSTAT)/%: %.static
	$(INS.rename)

$(ROOTXPG4BIN)/%: %.xpg4
	$(INS.rename)

$(ROOTLOCALEDEF)/%: %
	$(INS.file)

$(ROOTCHARMAP)/%: %
	$(INS.file)

# build rule for statically linked programs with single source file.
%.static: %.c
	$(LINK.c) -o $@ $< $(LDLIBS)
	$(POST_PROCESS)

%.xpg4: %.c
	$(LINK.c) -o $@ $< $(LDLIBS)
	$(POST_PROCESS)

# Define the majority text domain in this directory.
TEXT_DOMAIN= SUNW_OST_OSCMD	
DCMSGDOMAIN= $(MSGROOT)/LC_TIME/$(TEXT_DOMAIN)

CLOBBERFILES += $(XPG4) $(STATIC) $(DCFILE)
