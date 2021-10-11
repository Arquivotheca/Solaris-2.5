#
#ident	"@(#)Makefile	1.11	93/11/12 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= sac sacadm pmadm

include ../Makefile.cmd

SACOBJ= sac.o readtab.o global.o log.o misc.o
SACOBJS= $(SACOBJ) util1.o
SACADMOBJ= sacadm.o admutil.o
SACADMOBJS= $(SACADMOBJ) util2.o
PMADMOBJ= pmadm.o admutil.o
PMADMOBJS= $(PMADMOBJ) util2.o
SACSRC= $(SACOBJ:%.o=%.c)
SACSRCS= $(SACSRC) util.c
SACADMSRC= $(SACADMOBJ:%.o=%.c)
SACADMSRCS= $(SACADMSRC) util.c
PMADMSRC= $(PMADMOBJ:%.o=%.c)
PMADMSRCS= $(PMADMSRC) util.c
OBJS= $(SACOBJS) $(SACADMOBJS) $(PMADMOBJS)

LIBSAFD = $(ROOTLIB)/saf
DIRS= $(LIBSAFD)
SACF= sac
ADMF= sacadm pmadm
ROOTSACF= $(SACF:%=$(LIBSAFD)/%)
ROOTADMF= $(ADMF:%=$(ROOTUSRSBIN)/%)

LDLIBS += -lnsl -lauth -ldl -lsocket
LINTFLAGS += -b -x
OWNER= root
GROUP= sys

util1.o := CPPFLAGS += -DSAC
util2.o := CPPFLAGS += -USAC

$(ROOTUSRSBIN)/sacadm :=	FILEMODE =	04755

$(LIBSAFD)/% : %
	$(INS.file)

.KEEP_STATE:

all: $(PROG) 

sac: $(SACOBJS)
	$(LINK.c) $(SACOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

sacadm: $(SACADMOBJS)
	$(LINK.c) $(SACADMOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

pmadm: $(PMADMOBJS)
	$(LINK.c) $(PMADMOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

util1.o: util.c
	$(COMPILE.c) util.c
	$(MV) util.o util1.o

util2.o: util.c
	$(COMPILE.c) util.c
	$(MV) util.o util2.o

install: all $(ROOTSACF) $(ROOTADMF)

# Don't install dirs already installed by Targetdirs
#$(DIRS):
#	$(INS.dir)

clean:
	$(RM) $(OBJS)

lint:
	$(LINT.c) $(SACSRCS)
	$(LINT.c) $(SACADMSRCS)
	$(LINT.c) $(PMADMSRCS)

include ../Makefile.targ
