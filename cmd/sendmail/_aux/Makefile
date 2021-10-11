#
#ident	"@(#)Makefile	1.22	94/04/13 SMI"	/* SVr4.0 %I	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/sendmail/aux/Makefile
#
include		../../Makefile.cmd

PROG= 		mailstats mconnect vacation mailcompat aliasadm

LIBPROG=	mail.local

OBJS= 		$(PROG:%=%.o) nisplus.o nisedit.o

SRCS= 		$(PROG:%=%.c) nisplus.c nisedit.c

OTHERSRC= 	../src/convtime.c

OTHEROBJ=	$(OTHERSRC:../src/%.c=%.o)

include		../../Makefile.cmd

LDLIBS +=	-lsocket -lnsl

NISLIB = # comment this back in when ZNS works -lnis

CPPFLAGS =	-I../include -I../src -DSYSV -DSOLARIS -DV1SUN_COMPAT $(CPPFLAGS.master)


# build rule
%.o: %.c
	$(COMPILE.c) $<
	$(POST_PROCESS_O)

.KEEP_STATE:

all:		$(PROG) $(LIBPROG)

vacation:	vacation.o convtime.o
	$(LINK.c) vacation.o convtime.o -o $@ $(LDLIBS)
	$(POST_PROCESS)

aliasadm:	aliasadm.o nisplus.o nisedit.o
	$(LINK.c) aliasadm.o nisplus.o nisedit.o -o $@ $(NISLIB) $(LDLIBS)
	$(POST_PROCESS)

mail.local:	mail.local.o
	$(LINK.c) mail.local.o -o $@ $(LDLIBS) -lmail
	$(POST_PROCESS)

install: all $(ROOTPROG) $(ROOTLIB)/mail.local

clean:
	$(RM) $(OBJS) $(OTHEROBJ)

lint:		lint_PROG



include	../../Makefile.targ
