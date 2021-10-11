#ident	"@(#)Makefile	1.15	94/09/07 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# Makefile to support tools used for linker development:
#
#  o	`c' programs are built from the $(MACH)/Makefile
#
#  o	info: summarizes relocation overhead, and a sorted string listing.
#
#  o	interface: produces a `physical binary interface' for a file.
#
#  o	proto: creates a proto directory structure suitable for sgs make
#	installs (basically a subset of Targetdirs).
#  o	sovv: shared object version verification.
#
#  o	lsd: analyze the different interfaces between two shared objects.
#
#  o	so425.sh: convert a 4.x shared object into a 5.x version suitable for
#	the link-editor to process.
#
# cmd/sgs/tools/Makefile

include		../../../Makefile.master

LSRCS=		info.sh		interface.sh		proto.sh
GSRCS=		lsd.sh		sovv.sh			so425.sh

SRCS=		$(LSRCS)  $(GSRCS)

LEXECS=		$(LSRCS:%.sh=%)
GEXECS=		$(GSRCS:%.sh=%)

EXECS=		$(LEXECS) $(GEXECS)

ROOTDIR=	$(ROOT)/opt/SUNWonld/bin
ROOTPROGS=	$(GEXECS:%=$(ROOTDIR)/%)

SUBDIRS=	$(MACH) man

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint

.KEEP_STATE:

all install:	$(EXECS)  $(SUBDIRS)  $(ROOTPROGS)

clean clobber:	$(SUBDIRS)
		$(RM) $(RMFLAGS) $(EXECS)

lint:		$(SUBDIRS)

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

$(ROOTDIR)/%:	%
		$(INS.file)

FRC:

execs:		$(EXECS)
