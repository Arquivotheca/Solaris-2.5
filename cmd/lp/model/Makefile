#
# ident	"@(#)Makefile	1.9	92/12/17 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/model/Makefile
#

include			../Makefile.lp

PROG =			lp.set		\
			lp.cat		\
			lp.tell		\
			drain.output

SRCS =			$(PROG:%=%.c)

OBJS =			$(PROG:%=%.o)

MODELS =		standard

MISC =			alert.proto


ROOTLIBLPMODEL=		$(ROOTLIBLP)/model
ROOTLIBLPBIN =		$(ROOTLIBLP)/bin

ROOTMISC =		$(MISC:%=$(ROOTLIBLPBIN)/%)
ROOTLPPROG =		$(PROG:%=$(ROOTLIBLPBIN)/%)
ROOTMODELS =		$(MODELS:%=$(ROOTLIBLPMODEL)/%)

CPPFLAGS =		-I$(LPINC) $(CPPFLAGS.master)

# conditional assignments
lp.tell :=		LDLIBS += $(LIBMSG) $(LIBLP) -lcurses
lp.set drain.output :=	LDLIBS += $(LIBLP) -lcurses
lp.cat :=		LDLIBS += $(LIBLP) -lcurses -lintl

$(ROOTMISC) :=		FILEMODE = 0444

.KEEP_STATE:

# for messaging catalog
#
POFILE= model.po
XGETFLAGS=-a -x $(LPROOT)/lp.xcl
POFILES= $(SRCS:%.c=%.po)


all:			$(PROG) $(MODELS) $(MISC)

install:		all $(ROOTLPPROG) $(ROOTMODELS) $(ROOTMISC)

$(ROOTLIBLPMODEL)/% $(ROOTLIBLPBIN)/%:	%
			$(INS.file)

catalog:        $(POFILE)

$(POFILE):      $(POFILES)
	$(RM)	$@
	cat     $(POFILES)      > $@

clean:
			$(RM) $(OBJS)

strip:
			$(STRIP) $(PROG)

lint :			lint_SRCS

include			../../Makefile.targ
