#
#ident	"@(#)Makefile	1.12	93/11/12 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/logins/Makefile
#

PROG= 	logins
PKGINFO= pkginfo
TXTS= 	$(PKGINFO)
XGETFLAGS +=-a -x logins.xcl

include ../Makefile.cmd
TEXT_DOMAIN= SUNW_OST_ADMIN

LDLIBS += -lintl

FILEMODE= 0750

.KEEP_STATE:

all: $(PROG) $(TXTS)

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
