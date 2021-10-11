#
#ident	"@(#)Makefile	1.6	92/12/15 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#

PROG=		nstest
PROGB=		nsquery
OBJS=		$(PROG:%=%.o)
SRCS=		$(OBJS:%.o=%.c)
OBJSB=		nsquery.o herror.o
SRCSB=		nsquery.c herror.c
SUBDIRS=	nslookup

include ../../../../Makefile.cmd

all:=           TARGET= all
install:=       TARGET= install
clean:=         TARGET= clean
clobber:=       TARGET= clobber
lint:=          TARGET= lint

CPPFLAGS +=	-DSYSV -DSTRNET -DBSD_COMP -DDEBUG
LDLIBS +=	-lresolv -lsocket -lnsl

.KEEP_STATE:

all: $(PROG) $(PROGB) $(SUBDIRS)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(PROGB): $(OBJSB)
	$(LINK.c) $(OBJSB) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG) $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:

clean: $(SUBDIRS)
	$(RM) $(OBJS)

lint:	lint_SRCS

include ../../../../Makefile.targ
