#
# ident	"@(#)Makefile	1.20	94/12/09 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/lp/cmd/Makefile
#

include 	../Makefile.lp

SUBDIRS =	bsd lpadmin lpsched lpstat lpdata

BINPROG =	cancel lp

BINLINKS =	disable enable

SBINPROG1 =	lpfilter lpforms lpmove lpshut \
		lpsystem lpusers dumpolp

SBINPROG2 =	accept

SBINPROG =	$(SBINPROG1) $(SBINPROG2)

SBINLINK =	reject

LIBLINKS =	$(SBINPROG) $(SBINLINK)

PROG =		$(BINPROG) $(SBINPROG)

COMMONOBJ=	comb.o
OTHEROBJS=	cancel.o lp.o $(SBINPROG1:=.o)

OBJS=		$(COMMONOBJ) $(OTHEROBJS)

ROOTBINPROG=	$(BINPROG:%=$(ROOTBIN)/%)
ROOTSBINPROG=	$(SBINPROG:%=$(ROOTUSRSBIN)/%)
ROOTSBINLINK=	$(SBINLINK:%=$(ROOTUSRSBIN)/%)
ROOTSYMLINKS=	$(LIBLINKS:%=$(ROOTLIB)/%)
ROOTSYMLINKS2=	$(BINLINKS:%=$(ROOTBIN)/%)


CPPFLAGS =	-I$(LPINC) $(CPPFLAGS.master)

# conditional assignments
#
$(BINPROG):=	LDLIBS += -lintl -lw
$(SBINPROG1):=	LDLIBS += -lintl -lw
accept:=	LDLIBS += $(LIBMSG) $(LIBACC) $(LIBOAM) $(LIBLP) -lintl -lw
cancel:=	LDLIBS += $(LIBREQ) $(LIBMSG) $(LIBOAM) $(LIBLP)
lp:=		LDLIBS += $(LIBPRT) $(LIBREQ) $(LIBMSG) $(LIBOAM) $(LIBLP)
lpfilter:=	LDLIBS += $(LIBFLT) $(LIBMSG) $(LIBACC) $(LIBOAM) $(LIBLP) \
		-lgen
lpforms:=	LDLIBS += $(LIBFRM) $(LIBMSG) $(LIBREQ) $(LIBOAM) \
		$(LIBACC) $(LIBLP)
lpmove:=	LDLIBS += $(LIBMSG) $(LIBACC) $(LIBOAM) $(LIBLP)
lpshut:=	LDLIBS += $(LIBMSG) $(LIBOAM) $(LIBLP)
lpsystem:=	LDLIBS += $(LIBSYS) $(LIBMSG) $(LIBOAM) $(LIBLP) -lnsl
lpusers:=	LDLIBS += $(LIBMSG) $(LIBACC) $(LIBOAM) $(LIBUSR) $(LIBLP)
dumpolp:=	LDLIBS += $(LIBLP)

# for messaging catalog
#
POFILE = cmd.po
MSGSUBDIRS = bsd lpadmin lpsched lpstat 
POFILES1 = $(MSGSUBDIRS:%=%/%.po)
POFILES2 = $(OBJS:%.o=%.po)
POFILES = $(POFILES1) $(POFILES2)
XGETFLAGS += -a -x ../lp.xcl

.KEEP_STATE:

all:		$(PROG) $(SUBDIRS)

# for messaging catalog
#
catalog: $(MSGSUBDIRS) $(POFILES2)
	$(RM) $(POFILE)
	cat $(POFILES) > $(POFILE)

accept:		$(COMMONOBJ)
		$(LINK.c) -o $@ $(COMMONOBJ) $(LDFLAGS) $(LDLIBS)
		$(POST_PROCESS)

install:	$(PROG) $(ROOTBINPROG) $(ROOTSBINPROG) $(ROOTSBINLINK) \
		$(ROOTSYMLINKS) $(ROOTSYMLINKS2) $(SUBDIRS)

clean:		$(SUBDIRS)
		$(RM) $(OBJS)

clobber:	$(SUBDIRS) local_clobber

local_clobber:
		$(RM) $(OBJS) $(PROG) $(POFILE) $(POFILES)

strip:		$(SUBDIRS)
		$(STRIP) $(PROG)

lint:
		$(LINT.c) $(SRCS)

$(BINPROG) $(SBINPROG1): $$@.o
		$(LINK.c) -o $@ $@.o $(LDFLAGS) $(LDLIBS)
		$(POST_PROCESS)

$(ROOTSYMLINKS):
		$(RM) $@; $(SYMLINK) ../sbin/$(@F) $@

$(ROOTSYMLINKS2):
		$(RM) $@; $(SYMLINK) ../sbin/accept $@

$(ROOTSBINLINK):	$(ROOTUSRSBIN)/accept
		$(RM) $@; $(LN) $(ROOTUSRSBIN)/accept $@

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
