# Copyright 1994, Sun Microsystems
# All rights reserved.
#
# Makefile for volume management daemon
#
# "@(#)Makefile	1.31	95/02/14 SMI"
#
# cmd/volmgt/vold/Makefile

PROG=	vold

include ../../Makefile.cmd
include ../Makefile.volmgt

OBJS=	nfs_server.o nfs_trace.o vold_err.o vold_main.o vold_proc.o \
	vold_node.o vold_util.o vold_dev.o vold_config.o vold_vol.o \
	vold_label.o vold_db.o vold_path.o vold_props.o vold_action.o \
	vold_obj.o vold_thread.o vold_md4.o vold_mnt.o

TMPOBJ= nfs_prot.o vold_mem.o

# drivers for devices
DEV_VERS=1
DEVS=	dev_cdrom.so.${DEV_VERS} dev_floppy.so.${DEV_VERS} \
	dev_test.so.${DEV_VERS} dev_pcmem.so.${DEV_VERS}

# drivers for labels
LABEL_VERS=1
LABS=	label_test.so.${LABEL_VERS} label_cdrom.so.${LABEL_VERS}\
	label_sun.so.${LABEL_VERS} label_dos.so.${LABEL_VERS}

# databases
DB_VERS=1
DBS=	db_nis.so.${DB_VERS} db_mem.so.${DB_VERS}

SOFILES=	${LABS} ${DEVS} ${DBS}

#
# Rules for making shared objects out of .c files.  Works well if
# we have a one-to-one mapping.  Applies in all cases so far.
#
.SUFFIXES: .so.${DB_VERS} .so.${LABEL_VERS} .so.${DEV_VERS}  ${SUFFIXES}

.c.so.${DB_VERS}:
	${CC} ${CPPFLAGS} ${CFLAGS} -G -o $@ $<
	$(POST_PROCESS_SO)

.c.so.${LABEL_VERS}:
	${CC} ${CPPFLAGS} ${CFLAGS} -G -o $@ $<
	$(POST_PROCESS_SO)

.c.so.${DEV_VERS}:
	${CC} ${CPPFLAGS} ${CFLAGS} -G -o $@ $<
	$(POST_PROCESS_SO)

XFILE=	${ROOT}/usr/include/rpcsvc/nfs_prot.x

SRCS=	${OBJS:%.o=%.c} ${DEVS:%.so.${DEV_VERS}=%.c} \
	${LABS:%.so.${LABEL_VERS}=%.c} ${DBS:%.so.${DB_VERS}=%.c}

CPPFLAGS += 	-D_REENTRANT -DSUN_THREADS -DLABEL_VERS=${LABEL_VERS}\
		-DDB_VERS=${DB_VERS} -DDEV_VERS=${DEV_VERS}

# TCOV_FLAG=	-ql
# GPROF_FLAG=	-xpg
# DEBUG_FLAG=	-g

LDLIBS += 	-lintl -lthread -ldl -lnsl -ladm

# install macros and rule
#
GROUP=		sys
ROOTVOLD=	${ROOTLIB}/vold
ROOTSOFILES=	${SOFILES:%=${ROOTVOLD}/%}

.KEEP_STATE:


all: $(PROG) $(SOFILES)

dso dsos libs: $(SOFILES)

${PROG}: ${OBJS} ${TMPOBJ}
	${LINK.c} ${OPT} -o $@ ${OBJS} ${TMPOBJ} ${LDLIBS}
	${POST_PROCESS}

lint :=	LINTFLAGS=-x

lint:
	$(LINT.c) ${SRCS}

cstyle:
	${CSTYLE} ${SRCS}

install: all ${ROOTVOLD} ${ROOTUSRSBINPROG} ${ROOTSOFILES}

clean:     
	${RM} $(OBJS) ${TMPOBJ} $(SOFILES) nfs_prot.c core a.out

clobber: clean
	${RM} $(PROG)

nfs_prot.c: $(XFILE)
	$(RPCGEN) -c $(XFILE) -o $@

${ROOTVOLD}/%: %
	${INS.file}

${ROOTVOLD}:
	${INS.dir}
