#
#ident	"@(#)Makefile	1.3	93/09/15 SMI"
#
# Copyright (c) 1982 by Sun Microsystems, Inc.
#
#  /usr/src/cmd/lib/fs/pcfs is the directory of all pcfs specific commands
#  whose executable reside in $(INSDIR1) and $(INSDIR2).
#

SUBDIRS=	mount ident fstyp

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint

.KEEP_STATE:

.PARALLEL:	$(SUBDIRS)

all:	$(SUBDIRS)

install clean clobber lint: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:
