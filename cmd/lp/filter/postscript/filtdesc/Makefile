#
# ident	"@(#)Makefile	1.7	95/07/13 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/filter/postscript/filtdesc/Makefile
#

include		../../../Makefile.lp

ROOTETCLPFD=	$(ROOTETCLP)/fd

FILES=		catv.fd		\
		download.fd	\
		dpost.fd	\
		postdaisy.fd	\
		postdmd.fd	\
		postio.fd	\
		postior.fd	\
		postmd.fd	\
		postplot.fd	\
		postprint.fd	\
		postreverse.fd	\
		posttek.fd \
		postpages.fd \
		pr.fd

ROOTFILES =	$(FILES:%=$(ROOTETCLPFD)/%)

TXTS =		README 

FILEMODE =	664

.KEEP_STATE:

all:		$(TXTS) $(FILES)

install:	all $(ROOTETCLPFD) $(ROOTFILES)

$(ROOTETCLPFD) :
		$(INS.dir)

$(ROOTETCLPFD)/% :	%
		$(INS.file)

clean clobber strip lint:
