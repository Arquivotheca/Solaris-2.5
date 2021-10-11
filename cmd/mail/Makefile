#
#ident	"@(#)Makefile	1.23	93/02/04 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/mail/Makefile
#

LIBRARY=	libmail.a

BINPROG=	mail 

PROG= 	       $(BINPROG)

HDR =		maillock.h

GREP=		grep

include ../Makefile.cmd

include ../../lib/Makefile.lib

LIBS=		$(LIBRARY) # later $(LINTLIB)

CLOBBERFILES=	$(PROG)
CLEANFILES=	$(MAILOBJS)

MAILOBJS= 	add_recip.o cat.o ckdlivopts.o \
		cksaved.o clr_hinfo.o copyback.o copylet.o \
		copymt.o createmf.o del_recipl.o Dout.o  delete.o \
		done.o doopen.o dumpaff.o dumprcv.o \
		errmsg.o gendeliv.o getarg.o getcomment.o \
		gethead.o goback.o init.o \
		isheader.o isit.o islocal.o istext.o legal.o \
		lock.o  main.o mkdead.o mta_ercode.o \
		new_recipl.o parse.o pckaffspot.o pckrcvspot.o \
		pickFrom.o pipletr.o poplist.o printhdr.o printmail.o \
		pushlist.o savehdrs.o sel_disp.o  sendlist.o \
		sendmail.o  setsig.o \
		stamp.o Tout.o maid.o

MAILSRC=	 $(MAILOBJS:%.o=%.c)

# for LIBRARY
OBJECTS= 	abspath.o  basename.o  casncmp.o    config.o    copystream.o \
		delempty.o getdomain.o maillock.o   myregexpr.o notifyu.o \
		popenvp.o  s_string.o  setup_exec.o strmove.o   skipspace.o \
		substr.o   systemvp.o  trimnl.o     xgetenv.o


CPROG=		mail
CPROGSRC= 	$(CPROG:%=_%.c)

# ROOT directories
ROOTMAILD=	$(ROOTLIB)/mail
ROOTINC=	$(ROOT)/usr/include

# installed modules
ROOTHDR=	$(HDR:%=$(ROOTINC)/%)
ROOTBINPROG=	$(BINPROG:%=$(ROOTBIN)/%)

ROOTSYMLINKS=	$(ROOTBIN)/rmail


EDITPATH= \
	sed -e 's!REAL_PATH!/usr/bin!g' \
	    -e 's!USR_SHARE_LIB!/usr/share/lib!g' \
	    -e 's!VAR_MAIL!/var/mail!g' < $? > $@

CPPFLAGS =	-DSVR4 -I. $(CPPFLAGS.master)

LDLIBS +=	$(LIBRARY) -lsocket -lnsl

# conditional assignments
#
all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint

# file modes, owners and groups for the install target
#


$(ROOTBIN)/mail :=	FILEMODE =	02511
$(ROOTBIN)/mail :=	GROUP =	mail


# install rules
$(ROOTINC)/% : %
	$(INS.file)

.KEEP_STATE:

all:		$(LIBS) $(PROG) 

mail: 		$(MAILOBJS) $(LIBRARY)
	$(LINK.c) -o $@ $(MAILOBJS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTHDR) $(ROOTLIBS)  $(ROOTBINPROG) $(ROOTSYMLINKS)

install_h: $(ROOTHDR)

install_lib: $(LIBS) $(ROOTLIBS)

# ROOTSYMLINKS
#
$(ROOTBIN)/rmail:
	$(RM) $@; $(SYMLINK) mail $@

lint: 
	$(LINT.c) $(MAILSRC)

CHECKHDRS=	$(HDR:%.h=%.check)

check:	$(CHECKHDRS)

# define common library targets
#
include ../../lib/Makefile.targ

clean clobber:	$(SUBDIRS)

