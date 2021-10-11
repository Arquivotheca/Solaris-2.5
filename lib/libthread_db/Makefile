#
#ident	"@(#)Makefile	1.7	94/10/18 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libthread_db/Makefile
#

LIBRARY= libthread_db.a
VERS= .0

OBJECTS=  \
thread_db.o

# libsocket build rules
#objs/%.o profs/%.o pics/%.o: inet/%.c
#	$(COMPILE.c) -o $@ $<
#	$(POST_PROCESS_O)
#
#objs/%.o profs/%.o pics/%.o: socket/%.c
#	$(COMPILE.c) -o $@ $<
#	$(POST_PROCESS_O)

# include library definitions
include ../Makefile.lib

SRCS=	$(OBJECTS:%.o=%.c)

THR = -I../libthread
CPPFLAGS += $(THR)/inc $(THR)/common $(THR)/$(MACH) $(THR)/$(MACH)/ml $(THR)/sys/inc
LIBS += $(DYNLIB)
LDLIBS += -ldl

.KEEP_STATE:

all: $(LIBS)
	(cd $(MACH); make all;)

install: all $(ROOTLIBS) $(ROOTLINKS)
	(cd $(MACH); make install;)

clean:	clean_mach

clean_mach:
	(cd $(MACH); make clean;)

# include library targets
include ../Makefile.targ

lint: 

