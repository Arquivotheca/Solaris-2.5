#
# ident	"@(#)Makefile	1.7	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/lpsched/Makefile
#

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
strip :=	TARGET= strip
catalog :=      TARGET= catalog

SUBDIR1=        lpNet lpsched
SUBDIR2=
SUBDIRS =       $(SUBDIR1) $(SUBDIR2)



# for messaging catalog
#
POFILE= lpsched.po
POFILES= $(SUBDIR1:%=%/%.po)
XGETFLAGS += -a -x $(LPROOT)/lp.xcl

.KEEP_STATE :

all :		$(SUBDIRS)

# for messaging catalog
#
catalog:        $(SUBDIRS)
	$(RM)	$(POFILE)
	cat     $(POFILES)      > $(POFILE)

install clean clobber strip :	$(SUBDIRS)

$(SUBDIRS) :	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
