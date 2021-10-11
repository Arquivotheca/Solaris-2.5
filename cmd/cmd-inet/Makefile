#
#ident	"@(#)Makefile	1.5	93/03/27 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
#
# include global definitions
include	../Makefile.cmd

SUBDIR1= etc
SUBDIR2= usr.bin usr.sbin
SUBDIRS= $(SUBDIR1) $(SUBDIR2)
POFILES=        $(SUBDIR2:%=%/%.po)
POFILE= cmd-inet.po

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint
_msg:=          TARGET= catalog

.KEEP_STATE:

.PARALLEL:	$(SUBDIRS)

all install clean lint: $(SUBDIRS)

clobber:	$(SUBDIRS) local_clobber

local_clobber:
	$(RM) $(CLOBBERFILES)

_msg: $(SUBDIR2)
	$(RM) $(POFILE)
	cat $(POFILES) > $(POFILE)
	$(RM)  $(MSGDOMAIN)/$(POFILE)
	cp $(POFILE) $(MSGDOMAIN)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:
