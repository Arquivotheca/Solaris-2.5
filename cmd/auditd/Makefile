#
# Makefile for auditd
#

PROG = auditd

include ../Makefile.cmd

TEXT_DOMAIN=SUNW_BSM_AUDITD
POS=auditd.po
include ../Makefile.cmd.bsm

LDLIBS += -lbsm -lsocket -lnsl -lintl

OBJS = auditd.o flock.o
SRCS = auditd.c flock.c

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

clean:
	rm -f $(OBJS) $(POS) 

include ../Makefile.targ
