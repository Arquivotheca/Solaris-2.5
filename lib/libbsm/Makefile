#
# Copyright (c) 1988 by Sun Microsystems, Inc.
#
#ident	@(#)Makefile	1.64	93/11/19 SMI
#
# lib/libbsm/Makefile
#

LIBRARY =	libbsm.a
VERS = 		.1
OBJECTS=	adr.o \
		adrf.o \
		adrm.o \
		au_open.o \
		au_preselect.o \
		au_to.o \
		au_usermask.o \
		audit_allocate.o \
		audit_class.o \
		audit_cron.o \
		audit_event.o \
		audit_ftpd.o \
		audit_halt.o \
		audit_inetd.o \
		audit_login.o \
		audit_mountd.o \
		audit_passwd.o \
		audit_reboot.o \
		audit_rexd.o \
		audit_rexecd.o \
		audit_rshd.o \
		audit_su.o \
		audit_user.o \
		bsm.o \
		generic.o \
		getacinfo.o \
		getauditflags.o \
		getdaent.o \
		getdment.o \
		getfaudflgs.o

#
# Include common library definitions.
#
include ../Makefile.lib

LIBS += 	$(DYNLIB)

#
# Macros for libbsm header files. These define user-level only interfaces.
#
HDRS = 		audit_uevents.h libbsm.h devices.h generic.h
ROOTHDRDIR = 	$(ROOT)/usr/include/bsm
ROOTHDRS = 	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS =	$(HDRS:%.h=%.check)
$(ROOTHDRS) := 	FILEMODE = 0644
CHECKHDRS =	$(HDRS:%.h=%.check)

#
# Macros for libbsm database files. These should probably be installed
# from somewhere else. Until we find that better place, install them
# from here.
#
ETCSECURITYFILES = 	audit_class audit_control audit_event audit_user
ETCSECURITYSRC =	$(ETCSECURITYFILES:%=%.txt)
ROOTETCSECURITY = 	$(ROOT)/etc/security
$(ROOTETCSECURITY) := 	DIRMODE = 0755
$(ROOTETCSECURITY) := 	OWNER = root
$(ROOTETCSECURITY) := 	GROUP = sys
ROOTETCSECURITYFILES =	$(ETCSECURITYFILES:%=$(ROOTETCSECURITY)/%)
$(ROOTETCSECURITYFILES) := FILEMODE = 0644
$(ROOTETCSECURITYFILES) := OWNER = root
$(ROOTETCSECURITYFILES) := GROUP = sys

#
# /etc/security/audit/localhost/files is a symbolic link to /var/audit.
# This is provided so that auditreduce will work in the default configuration.
#
RESA=$(ROOTETCSECURITY)/audit
RESAL=$(RESA)/localhost
VARAUDIT=$(ROOT)/var/audit
AUDITDIRS=$(RESA) $(RESAL) $(VARAUDIT)
$(AUDITDIRS) := FILEMODE = 0750
$(AUDITDIRS) := OWNER = root
$(AUDITDIRS) := GROUP = sys

ARSYMLNK=$(RESAL)/files

.KEEP_STATE:

all: $(LIBS)

install: all install_data install_dirs \
		$(ROOTUSRLIB) $(ROOTLIBS) $(ROOTLINKS)

install_h: $(ROOTHDRDIR) $(ROOTHDRS)

check:	$(CHECKHDRS)

install_data: $(ETCSECURITYSRC) $(ROOTETCSECURITY) $(ROOTETCSECURITYFILES)

install_dirs: $(AUDITDIRS) $(ARSYMLNK)

audit_uevents.h: mkhdr.sh audit_event.txt
	sh mkhdr.sh

$(ROOTHDRDIR):
	$(INS.dir)

$(ROOTHDRDIR)/%:%
	$(INS.file)

$(AUDITDIRS):
	$(INS.dir)

$(ARSYMLNK): 
	$(RM) $@
	$(SYMLINK) ../../../../var/audit $@

$(ROOTETCSECURITY):
	$(INS.dir)

$(ROOTETCSECURITY)/%: %.txt
	$(INS.rename)

include ../Makefile.targ
