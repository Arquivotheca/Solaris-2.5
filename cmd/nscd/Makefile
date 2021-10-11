#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
#ident	"@(#)Makefile	1.1	94/12/05 SMI"
#
# Makefile for name service cache daemon
#
# cmd/nscd/Makefile
#

PROG=	nscd
NISPROG=	nscd_nischeck

include ../Makefile.cmd

OBJS=	server.o getpw.o getgr.o gethost.o hash.o \
	nscd_biggest.o nscd_parse.o nscd_wait.o 

NISOBJS=	nscd_nischeck.o  

CLOBBERFILES=	nscd nscd_nischeck

SRCS=	${OBJS:%.o=%.c} 

NISSRC=	${NISOBJS:%.o=%.c}

CPPFLAGS += 	-D_REENTRANT -DSUN_THREADS

# TCOV_FLAG=	-ql
# GPROF_FLAG=	-xpg
# DEBUG_FLAG=	-g

LDLIBS += 	-ldoor -lresolv -lnsl -lthread

# install macros and rule
#
GROUP=	bin
OWNER=	bin
ROOTPROG=	${ROOTUSRSBIN}/nscd
ROOTNISPROG=	${ROOTLIB}/nscd_nischeck

.KEEP_STATE:

all: $(PROG) $(NISPROG)

${PROG}: ${OBJS}
	${LINK.c} ${OPT} -o $@ ${OBJS} ${LDLIBS}
	${POST_PROCESS}

${NISPROG}: ${NISOBJS}
	${LINK.c} ${OPT} -o $@ ${NISOBJS} ${LDLIBS}

lint :=	LINTFLAGS=-x -b -u

lint:
	$(LINT.c) ${SRCS}

lint2:
	 $(LINT.c) ${NISSRC}

cstyle:
	${CSTYLE} ${SRCS} ${NISSRC}

install: all $(ROOTPROG) $(ROOTNISPROG)

clean:     
	${RM} ${OBJS} ${NISOBJS}

${ROOTUSRSBIN}/%: %
	${INS.file}

${ROOTUSRLIB}/%: %
	${INS.file}



include ../Makefile.targ



