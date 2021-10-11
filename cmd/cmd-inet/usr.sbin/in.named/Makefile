#
#ident	"@(#)Makefile	1.10	95/04/07 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#

NMD=		in.named
XFER=		named-xfer
PROG=		$(NMD) $(XFER)
NMDOBJS=	db_dump.o db_reload.o ns_forw.o ns_req.o \
		db_glue.o db_save.o ns_init.o ns_resp.o version.o \
		db_load.o db_update.o ns_main.o ns_sort.o \
		db_lookup.o ns_maint.o ns_stats.o
XFEROBJS=	named-xfer.o db_glue.o
OBJS=		$(NMDOBJS) named-xfer.o
SRCS=		$(OBJS:%.o=%.c)
SUBDIRS=	tools

include ../../../Makefile.cmd

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint


# want debugging to be on so "-d" option works
CPPFLAGS +=	-DSYSV -DSTRNET -DBSD_COMP -DBSD=43 -DDEBUG -DSTATS

LDLIBS +=	-lresolv -lsocket -lnsl

.KEEP_STATE:

all: $(PROG) $(SUBDIRS)

$(NMD): $(NMDOBJS)
	$(LINK.c) $(NMDOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(XFER): $(XFEROBJS)
	$(LINK.c) $(XFEROBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG) $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:

clean:	$(SUBDIRS)
	$(RM) $(OBJS)

clobber: $(SUBDIRS)
	$(RM) $(OBJS) $(PROG)

lint: $(SUBDIRS)
	$(LINT.c) $(SRCS)

