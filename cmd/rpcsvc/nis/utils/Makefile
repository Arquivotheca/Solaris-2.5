#
# Copyright 1992 Sun Microsystems Inc.
#
#ident	"@(#)Makefile	1.15	94/12/06 SMI"
#
# cmd/rpcsvc/nis/utils/Makefile
#
# Makefile for the nis_plus utility programs.
#

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint

SUBDIRS= nisaddcred

# links to nischmod
LPROG=	nischgrp  nischown   nischttl

PROG=	niscat    nismatch   nistest    nisdefaults nischmod    nisrm      \
        nisgrep   nismkdir   nisrmdir   nistbladm   nisgrpadm   nisln      \
        nisls     niserror   nispath

SRCS=	niscat.c  nismatch.c nistest.c  nisdefaults.c nischmod.c  nisrm.c    \
	nisgrep.c nismkdir.c nisrmdir.c nistbladm.c   nisgrpadm.c nisln.c    \
        nisls.c   niserror.c nispath.c

OBJS=	niscat.o  nismatch.o nistest.o  nisdefaults.o nischmod.o  nisrm.o    \
	nisgrep.o nismkdir.o nisrmdir.o nistbladm.o   nisgrpadm.o nisln.o    \
        nisls.o   niserror.o nispath.o

RPROG=  nisaddent
RSRCS=  nisaddent.c
ROBJS=  nisaddent.o

UTILSRCS= nis_util.c nis_util_regex.c nis_util_random.c
UTILOBJS= nis_util.o nis_util_regex.o nis_util_random.o

include ../../../Makefile.cmd

ROOTNSLIB=	$(ROOT)/usr/lib/nis
ROOTNSPROG=	$(RPROG:%=$(ROOTNSLIB)/%)

ROOTLPROG= $(LPROG:%=$(ROOTBIN)/%)
LDLIBS += -lnsl util.a
nispasswd := LDLIBS = $(LDLIBS.cmd) -lnsl

.KEEP_STATE:

all: util.a $(SUBDIRS) $(PROG) $(RPROG) $(LPROG)

install: all $(SUBDIRS) $(ROOTPROG) $(ROOTNSPROG) $(ROOTLPROG)

clean:
	$(RM) $(OBJS) $(UTILOBJS)

lint: lint_SRCS

nispasswd:
	@$(ECHO)echo "\`\`nispasswd'' is now a link to \`\`passwd''."

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

#
# build an archive library so that those programs that need stuff can
# get it, but others won't link in unneeded routines.
#

util.a: $(UTILOBJS)
	$(AR) -crv $@ $(UTILOBJS)

$(PROG:%=%): $$@.o util.a
	$(LINK.c) -o $@ $@.o $(LDLIBS)

$(LPROG): nischmod
	-$(RM) $@ ; $(LN) nischmod $@

$(ROOTLPROG): $(ROOTBIN)/nischmod
	-$(RM) $@ ; $(LN) $(ROOTBIN)/nischmod $@

$(ROOTNSLIB):
	$(INS.dir)

$(ROOTNSLIB)/%: % $(ROOTNSLIB)
	$(INS.file)

include ../../../Makefile.targ

FRC:

