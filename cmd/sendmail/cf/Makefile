#
#ident	"@(#)Makefile	1.16	91/06/25 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/sendmail/cf/Makefile

include		../../Makefile.cmd

TXTS=		base.m4 ddnm.m4 localm.m4 suucpm.m4 version.m4 compat.m4 \
		etherm.m4 sunbase.m4 uucpm.m4 zerobase.m4

ETCCF=		sendmail.cf main.cf subsidiary.cf 

CFS=		$(ETCCF)

DERIVEDCFS=	sendmail.cf main.cf subsidiary.cf 

ROOTETCMAIL = $(ROOTETC)/mail

ROOTETCMAILCF=	$(ETCCF:%=$(ROOTETCMAIL)/%)

FILEMODE=	444
M4=		m4

# build rule
%.cf:		%.mc
	$(M4) $< > $@

.KEEP_STATE:

all:		$(CFS) $(TXTS)

sendmail.cf :	subsidiary.cf
	$(CP) subsidiary.cf $@

install:	all $(ROOTETCMAILCF)

clean:
	$(RM) $(DERIVEDCFS)

clobber: 	clean

$(DERIVEDCFS): $(TXTS)

$(ROOTETCMAIL)/%.cf: %.cf
	$(INS.file)
