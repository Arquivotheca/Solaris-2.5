#
# ident	"@(#)Makefile	1.17	94/03/29 SMI"	/* SVr4.0 1.13	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/lpsched/lpNet/Makefile
#

include		../../../Makefile.lp

PROG =		lpNet

SUBDIRS =	parent svChild bsdChild

OTHERPROG =	Systems

ROOTOTHERPROG =	$(OTHERPROG:%=$(ROOTETCLP)/%)

TXTS =		$(OTHERPROG)

$(ROOTLIBLPPROG) :=	GROUP=	tty
$(ROOTOTHERPROG) :=	FILEMODE = 664

NETLIBS =	parent/parent.a \
		svChild/svChild.a \
		bsdChild/bsdChild.a

LPLIBS=		$(LIBNET) \
		$(LIBBSD) \
		$(LIBOAM) \
		$(LIBREQ) \
		$(LIBMSG) \
		$(LIBSEC) \
		$(LIBPRT) \
		$(LIBSYS) \
		$(LIBCLS) \
		$(LIBLP)

# XXX - this can't be optimal...
SYSLIBS=	-lnls -lnsl -lgen -lcurses -lintl -lw

# for dl use
#SYSLIBS=	-lnsl -lnsl -ldl -lgen -lcurses

LDLIBS +=	$(NETLIBS) $(LPLIBS) $(SYSLIBS)

# for messageing catalog file
#
POFILE= lpNet.po
POFILES= $(SUBDIRS:%=%/%.po)
XGETFLAGS +=-a -x $(LPROOT)/lp.xcl

.KEEP_STATE:

all:		$(TXTS) $(PROG)

catalog:        $(SUBDIRS)
	$(RM)	$(POFILE)
	cat     $(POFILES)      > $(POFILE)


install:	all $(ROOTLIBLPPROG) $(ROOTOTHERPROG)

clean:		$(SUBDIRS)
		-$(RM) $(OBJS)

clobber:	$(SUBDIRS) local_clobber

local_clobber:
		$(RM) $(OBJS) $(PROG) $(POFILE) $(POFILES)

strip:
		$(STRIP) $(PROG)

lint:		$(SUBDIRS)

$(PROG):	$(NETLIBS)
		$(LINK.c) -o $@ $(LDLIBS)
		$(POST_PROCESS)

$(NETLIBS) :	FRC
		@cd $(@D); $(MAKE) $(@F)

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
