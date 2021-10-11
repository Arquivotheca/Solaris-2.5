# Copyright 1992, Sun Microsystems
#
# Makefile for Removable Media Mounter.
#
# "@(#)Makefile	1.14	95/09/06 SMI"
#
# cmd/volmgt/rmm/Makefile

PROG=	rmmount


include ../../Makefile.cmd
include ../Makefile.volmgt

OBJS=	rmm.o rmm_util.o rmm_config.o
SRCS=	$(OBJS:%.o=%.c) ${ACTS:%.so.$(ACT_VERS)=%.c}

# identification functions are in src/cmd/fs.d/<fstype>.
IDENT_VERS=1

# actions
ACT_VERS=1
ACTS=	action_workman.so.${ACT_VERS} action_filemgr.so.${ACT_VERS}

CFLAGS += -v

CPPFLAGS += 	-DACT_VERS=${ACT_VERS} -DIDENT_VERS=${IDENT_VERS}

$(PROG) :=	LDLIBS = $(LDLIBS.cmd) -lintl -lgen -ldl -lvolmgt -ladm
$(ACTS) :=	LDLIBS = $(LDLIBS.cmd) -lvolmgt

#
# Rules for making shared objects out of .c files.  Works well if
# we have a one-to-one mapping.  Applies in all cases so far.
#
.SUFFIXES: .so.${ACT_VERS} ${SUFFIXES}

.c.so.${ACT_VERS}:
	${CC} ${CPPFLAGS} ${CFLAGS} -G -o $@ $< $(LDLIBS)
	$(POST_PROCESS_SO)

# install macros and rule
#
ROOTUSRSBINPROG=	${PROG:%=${ROOTUSRSBIN}/%}
RMMLIBOBJS=		${ACTS:%=${RMMLIBDIR}/%}

${RMMLIBDIR}/%: %
	${INS.file}

.KEEP_STATE:

all: ${PROG} ${ACTS}

${PROG}: ${OBJS}
	${LINK.c} -o $@ ${OBJS} ${LDLIBS}
	$(POST_PROCESS)

lint:
	$(LINT.c) ${SRCS}

cstyle:
	${CSTYLE} ${SRCS}

install: all ${RMMLIBDIR} ${RMMLIBOBJS} ${ROOTUSRSBINPROG}

${RMMLIBDIR}:
	${INS.dir}

clean: 
	${RM} $(OBJS) ${ACTS} core a.out

clobber: clean
	${RM} $(PROG)
