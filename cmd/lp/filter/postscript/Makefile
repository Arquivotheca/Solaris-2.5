#
# ident	"@(#)Makefile	1.7	93/10/21 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/filter/postscript/Makefile
#

include 	../../Makefile.lp

COMMON_SUBDIRS = \
		common		\
		postreverse	\
        	dpost		\
		postio		\
        	postprint	\
        	posttek		\
        	postdmd		\
        	postdaisy	\
        	postmd		\
		postplot	\
        	postscript	\
		postcomm	\
		picpack		\
		download	\
        	font		\
		filtdesc	\
		macros

SUBDIRS= $(COMMON_SUBDIRS)

ROOTDIRS =	$(ROOTLIBLPPOST)

TXTS=		README

.KEEP_STATE:

all:  		$(TXTS) $(SUBDIRS)

install:	$(ROOTDIRS) $(SUBDIRS)

clean clobber strip lint :	$(SUBDIRS)

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

$(ROOTDIRS):
		$(INS.dir)

FRC:
