#
# ident	"@(#)Makefile	1.9	94/08/30 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/bsd/Makefile
#

SUBDIR1 =                        lptest
SUBDIR2 =       lpc lpq lpr lprm
SUBDIRS =       $(SUBDIR1) $(SUBDIR2)


all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint
strip:=		TARGET= strip
catalog:=       TARGET= catalog

# for messaging catalog
#
POFILE= bsd.po
POFILES = $(SUBDIR2:%=%/%.po)
XGETFLAGS += -a -x $(LPROOT)/lp.xcl


.KEEP_STATE :

all:		$(SUBDIRS)

catalog:        $(SUBDIR2)
	$(RM)	$(POFILE)
	cat     $(POFILES)      > $(POFILE)

install clean clobber strip lint:	$(SUBDIRS)

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
