# Copyright 1992, Sun Microsystems
#
# Makefile for volume management utilities
#
# "@(#)Makefile	1.22	94/10/05 SMI"
#
# cmd/volmgt/util/Makefile

include ../../Makefile.cmd
include ../Makefile.volmgt

OBJS=		eject.o volcancel.o volstat.o volmissing.o volcheck.o
SRCS=		$(OBJS:%.o=%.c)

LDLIBS +=	-lvolmgt -ladm -lintl
LPROG=		volcancel volstat volmissing
BPROG=		eject volcheck

# install macros and rules
#
ROOTBINPROG=	${BPROG:%=${ROOTBIN}/%}
VOLLIBPROG=	${LPROG:%=${VOLLIBDIR}/%} ${VOLLIBDIR}/volsetup

#
# eject and volcheck need to be suid root
#
${ROOTBIN}/eject := FILEMODE = 04555
${ROOTBIN}/volcheck := FILEMODE = 04555
${ROOTBIN}/eject := OWNER = root
${ROOTBIN}/volcheck := OWNER = root

.KEEP_STATE:

all: volsetup ${LPROG} ${BPROG}

eject:	eject.o
	${LINK.c} -o $@ eject.o ${LDLIBS}
	$(POST_PROCESS)

volcancel: volcancel.o
	${LINK.c} -o $@ volcancel.o ${LDLIBS}
	$(POST_PROCESS)

volmissing: volmissing.o
	${LINK.c} -o $@ volmissing.o ${LDLIBS}
	$(POST_PROCESS)

volstat: volstat.o
	${LINK.c} -o $@ volstat.o ${LDLIBS}
	$(POST_PROCESS)

volcheck: volcheck.o
	${LINK.c} -o $@ volcheck.o ${LDLIBS}
	$(POST_PROCESS)

${VOLLIBDIR}/%: %
	${INS.file}

${VOLLIBDIR}:
	${INS.dir}

install: all ${VOLLIBDIR} ${VOLLIBPROG} ${ROOTBINPROG}

cstyle:
	${CSTYLE} ${SRCS}

lint:
	${LINT.c} ${SRCS}

clean:
	${RM} ${OBJS} *.o a.out core

clobber: clean
	${RM} ${LPROG} $(BPROG)

