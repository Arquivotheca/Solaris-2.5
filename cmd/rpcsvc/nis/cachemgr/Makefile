#
#	Copyright (c) 1988-1992 Sun Microsystems Inc
#	All Rights Reserved.
#
#	@(#)Makefile	1.6	92/04/21 SMI
#
# Sample makefile to build the cache manager
#
HDRS= 	cachemgr.h pkeyhash.h dircache.h cache_entry.h sundry.h

SRCS= \
    cachemgr_init.c   cache_svc.c       cacheadd.cc   cachemgr.cc \
    cachesvc.cc       pkeyhash.cc

OBJS= \
    cachemgr_init.o   cache_svc.o       cacheadd.o   cachemgr.o \
    cachesvc.o        pkeyhash.o

PROG= nis_cachemgr

include $(SRC)/cmd/Makefile.cmd

LDLIBS += -lnsl

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTUSRSBINPROG)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

nis_cachemgr: $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

include $(SRC)/cmd/Makefile.targ
