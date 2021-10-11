#
# ident	"@(#)Makefile	1.6	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/etc/Makefile
#

include			../Makefile.lp

ROOTINIT_D =		$(ROOTETC)/init.d
ROOTRC0_D =		$(ROOTETC)/rc0.d
ROOTRC2_D =		$(ROOTETC)/rc2.d

OWNER =			root
GROUP =			sys
FILEMODE =		0744

PROG =			lp

ROOTINIT_DPROG =	$(ROOTINIT_D)/$(PROG)
ROOTLNKPROGS =		$(ROOTRC0_D)/K20lp	\
			$(ROOTRC2_D)/K20lp	\
			$(ROOTRC2_D)/S80lp


.KEEP_STATE:

all :			$(PROG)

install:		all $(ROOTINIT_DPROG) $(ROOTLNKPROGS)

$(ROOTLNKPROGS) :	$(ROOTINIT_DPROG)
			$(RM) $@; $(LN) $(ROOTINIT_DPROG) $@

$(ROOTINIT_D)/% :	%
			$(INS.file)

clean clobber strip lint:
