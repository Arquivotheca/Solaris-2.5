#
# Makefile for "ifconfig" 
#
# "@(#)Makefile 1.13     94/05/05 SMI"
#

PROG =		ifconfig
OBJS=		nisplus_common.o switch_err.o getmaskbyaddr.o ifconfig.o revarp.o
SRCS=		$(OBJS:%.o=%.c)

# duplicate ROOTSBIN value needed for installation rule
ROOTSBIN = $(ROOT)/sbin
$(ROOTSBIN)/%: %.ps
	$(INS.rename)

include ../../../Makefile.cmd

CPPFLAGS +=     -DSYSV -DSTRNET -DBSD_COMP
LDLIBS +=       -lsocket -lnsl -ldevinfo

# `PS[ps]' is meant to imply "partially static" because
# the version of ifconfig(1) that goes in $(ROOT)/sbin
# is dynamic wrt ld.so, but static wrt all others.
PS = $(PROG:%=%.ps)
$(PS) := LDLIBS = $(LDLIBS.cmd) $(NEEDU) -Bdynamic -ldl -Bstatic -ldevinfo -lsocket -lnsl -lintl -lelf -lc
$(PROG) := LDLIBS += -ldl -lelf
$(PS) := LDFLAGS = -dy $(LDFLAGS.cmd)
ROOTPSPROG = $(PROG:%=$(ROOTSBIN)/%)
CLOBBERFILES = $(PS)
#
# We need to make these become externally available so
# that nss_files.so.1 (ps) can find them at run time.
NEEDU=		-u __ctype \
		-u isspace \
		-u llseek \
		-u memcmp \
		-u nlist \
		-u rewind \
		-u strcasecmp \
		-u strstr \
		-u vfprintf \
		-u __nss2herrno

.KEEP_STATE:

all:	$(PROG) $(PS)

$(PROG):$(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(PS):	$(OBJS)
	LD_OPTIONS=-I/etc/lib/ld.so.1 $(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install:	all $(ROOTUSRSBINPROG)  $(ROOTPSPROG)

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

include ../../../Makefile.targ
