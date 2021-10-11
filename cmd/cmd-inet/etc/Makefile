#
#ident	"@(#)Makefile	1.7	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG=	hosts inetd.conf networks protocols \
	services netmasks
SUBDIRS= init.d
TXTS= hosts.equiv named.boot

include ../../Makefile.cmd

all:=		TARGET= all
install:=	TARGET= install

INETDIR= $(ROOTETC)/inet
DIRS= $(INETDIR)
SYMDIR= inet
ETCINETPROG=	$(PROG:%=$(INETDIR)/%)
SYMETCPROG=	$(PROG:%=sym_%)

FILEMODE= 0444
OWNER= root
GROUP= sys

.KEEP_STATE:

all: $(PROG) $(TXTS) $(SUBDIRS)

install: all $(DIRS) $(ETCINETPROG) $(SYMETCPROG) $(SUBDIRS)

$(INETDIR)/% : %
	$(INS.file)

sym_% : %
	$(RM) $(ROOTETC)/$<
	$(SYMLINK) $(SYMDIR)/$< $(ROOTETC)/$<

$(DIRS):
	$(INS.dir)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:

clean clobber lint:
