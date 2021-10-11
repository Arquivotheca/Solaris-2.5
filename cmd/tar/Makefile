#
#pragma ident	"@(#)Makefile	1.33	94/12/20 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/tar/Makefile

PROG= tar
STATPROG= tar
CLOBBERFILES= $(STATPROG).static

include ../Makefile.cmd

SED= sed
DCFILE= $(PROG).dc

DFLTD= $(ROOTETC)/default
DIRS= $(DFLTD)

LDLIBS += -lcmd -lintl -lw -lsec
# A list of static libraries that use libdl_stubs which fakes the real
# dynamic linker by returning static symbols from the pre-linked libraries.
#
STATICLIBS = -ldl_stubs -lnss_files -lnss_nis -lnss_nisplus \
	-lnss_compat -lnss_dns -lresolv -lsocket -lstraddr -lnsl
$(STATIC) := LDLIBS += -lcmd -lintl -lc $(STATICLIBS)

CFLAGS += -v
CPPFLAGS += -DEUC

ROOTSYMLINK=	$(ROOTPROG)

ETCDFLTPROG = $(PROG:%=$(DFLTD)/%)
$(ETCDFLTPROG) := FILEMODE = $(LIBFILEMODE)
$(ETCDFLTPROG) := OWNER = root
$(ETCDFLTPROG) := GROUP = sys

XGETFLAGS += -a -x tar.xcl

.KEEP_STATE:

all: $(PROG) $(PROG).dfl $(STATIC) 

install: all $(DIRS) $(ROOTUSRSBINPROG) $(ETCDFLTPROG) $(ROOTSYMLINK) \
	$(ROOTSTATPROG)
	-$(RM) -r $(ROOTETCPROG)
	-$(SYMLINK) ../usr/sbin/$(PROG) $(ROOTETCPROG)

$(ROOTSYMLINK):
	$(RM) $@; $(SYMLINK) ../sbin/$(PROG) $@

$(DIRS):
	$(INS.dir)

$(DFLTD)/% : %
	$(MV) $(PROG) $(PROG).save
	$(LN) $(PROG).dfl $(PROG)
	$(INS.file)
	$(RM) $(PROG)
	$(MV) $(PROG).save $(PROG)

clean:

lint:	lint_PROG

$(DCFILE):
	$(RM) messages.po
	$(XGETTEXT) -c TRANSLATION_NOTE -t $(PROG).c
	$(SED) -e '/^domain/d' messages.po > $@
	$(RM) messages.po

include ../Makefile.targ
