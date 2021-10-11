#
#ident	"@(#)Makefile	1.39	93/05/20 SMI"	/* SVr4.0 %I	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/sendmail/src/Makefile



PROG=	sendmail

include 	../../Makefile.cmd

# additional .o files needed
OBJADD=nisplus.o text.o nsswitch.o getcanonname.o nisutil.o sun_compat.o
 
BASEOBJS= alias.o arpadate.o clock.o collect.o conf.o convtime.o daemon.o \
	deliver.o domain.o envelope.o err.o headers.o macro.o main.o \
	map.o mci.o parseaddr.o queue.o readcf.o recipient.o \
	savemail.o srvrsmtp.o stab.o stats.o sysexits.o \
	trace.o udb.o usersmtp.o util.o version.o ${OBJADD}


OBJS=		$(BASEOBJS)

LDLIBS += 	-lkstat -lresolv -lsocket -lnsl -L$(ROOT)/usr/ccs/lib -lelf

CPPFLAGS = 	-I../include -DSOLARIS -DV1SUN_COMPAT -DBUGFIX -DVENDOR_SPECIFIC_FEATURE -DNDBM -DNIS -DINTER $(CPPFLAGS.master)

FILEMODE= 	4551
OWNER=		root

ROOTSYMLINKS=	$(ROOTBIN)/newaliases $(ROOTBIN)/mailq 

# build rule
#

.KEEP_STATE:
all:		$(PROG)

$(PROG):	$(BASEOBJS)
	$(LINK.c) -o $@ $(BASEOBJS) $(LDLIBS)
	$(POST_PROCESS)

install:	$(ROOTLIBPROG) $(ROOTSYMLINKS)

$(ROOTSYMLINKS):
	$(RM) $@; $(SYMLINK) ../lib/sendmail $@

clean:
	$(RM) $(OBJS)

lint:		lint_PROG

include		../../Makefile.targ
