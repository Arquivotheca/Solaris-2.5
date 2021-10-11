#
#ident	"@(#)Makefile	1.5	92/12/16 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= installcmd

PROG2= install

PROGSRC= install.c

include ../Makefile.ucbcmd

FILEMODE=	755

LDLIBS += -lintl

# for messaging catalog
#
POFILE= install.po


.KEEP_STATE:

all: $(PROG) 

$(PROG):	$(PROGSRC)
		$(LINK.c) $(PROGSRC) -o $@ $(LDLIBS)
		$(POST_PROCESS)

install: all $(ROOTPROG)
	rm -f $(ROOTBIN)/$(PROG2)
	mv $(ROOTBIN)/$(PROG) $(ROOTBIN)/$(PROG2)

clean:

lint:	lint_PROG

include ../Makefile.ucbtarg
