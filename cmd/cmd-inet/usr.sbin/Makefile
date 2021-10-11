#
#ident	"@(#)Makefile	1.39	94/03/07 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/cmd-inet/usr.sbin/Makefile

SYNCPROG=	syncinit syncloop syncstat

PROG=		arp gettable hostconfig in.comsat in.fingerd in.rarpd \
		in.rexecd in.rlogind in.rshd in.rwhod in.telnetd in.tftpd \
		in.tnamed inetd ping route ndd in.rdisc \
		$(SYNCPROG)

SYNCOBJS=	$(SYNCPROG:%=%.o)
DLPIOBJS=	dlcommon.o
DLPISRC=	dlcommon.c
PS= 		hostconfig.ps
SUIDPROG=	arp ping
RPCSVCPROG=	hostconfig
AUDITPROG=	in.rexecd in.rlogind in.rshd in.telnetd inetd
KSTATPROG=	in.rwhod
PROGSRCS=	$(PROG:%=%.c)
TFTPDOBJS=	in.tftpd.o tftpsubs.o
OTHERSRC=	../usr.bin/tftp/tftpsubs.c
SRCS=		$(PROGSRCS) $(OTHERSRC) $(DLPISRC)

SUBDIRS=	htable ifconfig in.ftpd in.named in.routed in.talkd \
		snoop

# duplicate ROOTSBIN value needed for installation rule
ROOTSBIN = $(ROOT)/sbin
$(ROOTSBIN)/%: %.ps
	$(INS.rename)

ROOTPSPROG =	$(PS:%.ps=$(ROOTSBIN)/%)

include ../../Makefile.cmd
#
# Message catalog
#
POFILES= in.comsat.po
POFILE= usr.sbin.po
#


all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint

CLOBBERFILES +=	$(PS)
#
# We need to make these become externally available so
# that tcpip.so (ps) can find them at run time.
NEEDU=		-u t_optmgmt \
		-u sscanf \
		-u inet_netof \
		-u strpbrk \
		-u yp_get_default_domain \
		-u yp_first \
		-u strrchr \
		-u strncmp \
		-u strtok \
		-u t_alloc \
		-u memset \
		-u t_free \
		-u t_unbind

$(ROOTUSRSBIN)/arp	:=	FILEMODE= 02555
$(ROOTUSRSBIN)/arp	:=	OWNER= root
$(ROOTUSRSBIN)/ping	:=	FILEMODE= 04555
$(ROOTUSRSBIN)/ping	:=	OWNER= root

CPPFLAGS +=	-DSYSV -DSTRNET -DBSD_COMP
LDLIBS +=	-lsocket -lnsl -lintl -lw -ldl

$(AUDITPROG)		:=	LDLIBS += -lbsm -lauth
$(KSTATPROG)		:=	LDLIBS += -lkstat
$(RPCSVCPROG)		:=	LDLIBS += -lrpcsvc
$(PS) 			:= 	LDLIBS = $(LDLIBS.cmd) $(NEEDU) -Bdynamic \
				-ldl -Bstatic -lsocket -lnsl -lintl \
				-lrpcsvc -lc
$(PS) 			:= 	LDFLAGS = $(LDFLAGS.cmd) -dy 

.KEEP_STATE:

all: $(PROG) $(PS) $(SUBDIRS) $(DLPIOBJS)

#
# message catalog
#
catalog: $(POFILE)

syncutil: $(SYNCPROG)

$(SYNCPROG):	$$@.o dlcommon.o
	$(LINK.c) $@.o dlcommon.o -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@
#

in.tftpd: $(TFTPDOBJS)
	$(LINK.c) $(TFTPDOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

tftpsubs.o: $(OTHERSRC)
	$(COMPILE.c) $(OTHERSRC) -c $@
	$(POST_PROCESS_O)

dlcommon.o: $(DLPISRC)
	$(COMPILE.c) $(DLPISRC) -c $@
	$(POST_PROCESS_O)

hostconfig: hostconfig.o
	$(LINK.c) hostconfig.o -o $@ $(LDLIBS)

hostconfig.ps: hostconfig.o
	LD_OPTIONS="-I/etc/lib/ld.so.1" $(LINK.c) hostconfig.o -o hostconfig.ps $(LDLIBS)
	$(POST_PROCESS)

install: $(PROG) $(PS) $(DIRS) $(ROOTUSRSBINPROG) $(ROOTPSPROG) $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

clean:	$(SUBDIRS)
	$(RM) $(TFTPDOBJS) $(DLPIOBJS) $(SYNCOBJS)

clobber: $(SUBDIRS)
	$(RM) $(TFTPDOBJS) $(DLPIOBJS) $(SYNCOBJS) $(PROG)

lint: $(SUBDIRS)
	$(LINT.c) $(SRCS)
