#
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# lib/libc2
#
LIBRARY= libc2stubs.a 

OBJECTS=audit_acct.o audit_clri.o audit_fsck.o audit_fsdb.o audit_halt.o \
	audit_init.o audit_login.o audit_logins.o audit_ncheck.o \
	audit_newgrp.o audit_passwd.o audit_pmadm.o audit_pt_chmod.o \
	audit_reboot.o audit_sacadm.o audit_su.o audit_sulogin.o \
	audit_priocntl.o audit_ftpd.o

VERS = .1
CLOBBERFILES += libc2stubs.so

# include library definitions
include ../Makefile.lib
#
# to enable printfs from the stubs library uncomment the next line
#CFLAGS += -DC2_DEBUG
DYNLIBS=	$(DYNLIB:%=$(ROOTLIBDIR)/%)

# Need to remove the -h $(SONAME) from DYNFLAGS because libc2* are just
# symlinks to either libbsm* or libc2stubs.  If we leave it in, programs
# that depend on stubs when BSM is not installed and depend on libbsm
# when it is installed, will get unresolved references.  Example: login.
DYNFLAGS=-ztext

ROOTUL = $(ROOT)/usr/lib

.KEEP_STATE:

all: $(LIBS) $(DYNLIB)


install: all $(ROOTLIBS) $(ROOTLINKS) install_links

install_links:
	rm -f $(ROOTUL)/libc2.a $(ROOTUL)/libc2.so $(ROOTUL)/libc2.so.1
	- $(SYMLINK) ./libc2stubs.a $(ROOTUL)/libc2.a
	- $(SYMLINK) ./libc2stubs.so.1 $(ROOTUL)/libc2.so
	- $(SYMLINK) ./libc2stubs.so.1 $(ROOTUL)/libc2.so.1

# include library targets
include ../Makefile.targ
