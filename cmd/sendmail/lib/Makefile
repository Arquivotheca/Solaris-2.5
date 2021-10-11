#
#ident	"@(#)Makefile	1.8	92/04/12 SMI"	/* SVr4.0 %I	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/sendmail/lib/Makefile

include		../../Makefile.cmd

SRCS=		aliases sendmail.hf sendmail

ROOTINIT_D =		$(ROOTETC)/init.d
ROOTRC0_D =		$(ROOTETC)/rc0.d
ROOTRC1_D =		$(ROOTETC)/rc1.d
ROOTRC2_D =		$(ROOTETC)/rc2.d
ROOTINIT_D_PROG =	$(ROOTINIT_D)/sendmail
ROOTRCLINKS =		$(ROOTRC0_D)/K57sendmail \
			$(ROOTRC1_D)/K57sendmail \
			$(ROOTRC2_D)/S88sendmail
ROOTETCMAIL =		$(ROOTETC)/mail
ROOTETCMAILF =		$(ROOTETCMAIL)/aliases $(ROOTETCMAIL)/sendmail.hf
ROOTETCSYMLINKS =	$(ROOTETC)/aliases


# conditional assignments
$(ROOTETCMAILF) :=	FILEMODE =	644
$(ROOTETCMAILF) :=	OWNER =		root

$(ROOTINIT_D_PROG) $(ROOTRCLINKS) :=	OWNER =		root
$(ROOTINIT_D_PROG) $(ROOTRCLINKS) :=	GROUP =		sys
$(ROOTINIT_D_PROG) $(ROOTRCLINKS) :=	FILEMODE =	744

.KEEP_STATE:

all:		$(SRCS)

install:	all $(ROOTETCMAILF) $(ROOTETCSYMLINKS) \
		$(ROOTINIT_D_PROG) $(ROOTRCLINKS)

clean:

clobber:

$(ROOTETCMAIL)/%: %
	$(INS.file) 

$(ROOTINIT_D)/%: %
	$(INS.file)

$(ROOTRCLINKS): $(ROOTINIT_D_PROG)
	$(RM) $@; $(LN) $(ROOTINIT_D_PROG) $@
	
$(ROOTETCSYMLINKS):
	$(RM) $@; $(SYMLINK) mail/aliases $@

