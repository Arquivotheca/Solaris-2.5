#
#ident	"@(#)Makefile	1.25	95/07/26 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#

PROG= sulogin

include ../Makefile.cmd

CPPFLAGS += -DSECURITY

#
LDLIBS_LIST = -lcrypt_i -lauth -lmp -lcmd -lintl \
	-lnss_files -lnss_nis -lnss_nisplus -lnss_compat -lnss_dns \
	-lsocket -lnsl -lstraddr

LDLIBS = -Bstatic -lelf -lc $(LDLIBS_LIST) -lgen -Bdynamic -ldl -Bstatic

LDFLAGS = $(LDLIBS.cmd) -dy -R/etc/lib $(LDFLAGS.cmd)

FILEMODE = 04555
OWNER = root
GROUP = bin

.KEEP_STATE:

all: $(PROG)

$(PROG): $(PROG).c
	LD_OPTIONS=-I/etc/lib/ld.so.1 $(LINK.c) $(PROG).c -o $@ `nm -hu $(ROOT)/etc/lib/nss_files.so.1 | sed 's/ */-u /' | egrep -v '\.'` $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTSBINPROG)
	$(RM) $(ROOTETCPROG)
	$(SYMLINK) ../sbin/$(PROG) $(ROOTETCPROG)


clean:

lint:	lint_PROG

include ../Makefile.targ
