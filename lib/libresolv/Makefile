#
#ident	"@(#)Makefile	1.10	93/04/13 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libresolv/Makefile
#
LIBRARY= libresolv.a
VERS= .1

PICS=   pics/res_comp.o pics/res_debug.o pics/res_init.o \
	pics/res_mkquery.o pics/res_query.o pics/res_send.o \
	pics/res_gethost.o pics/res_sethost.o pics/res_strcase.o

objs/%.o pics/%.o: %.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

OBJECTS= \
res_gethost.o   res_comp.o      res_debug.o     res_init.o    res_mkquery.o  \
res_query.o     res_send.o      res_sethost.o   res_strcase.o

# include library definitions
include ../Makefile.lib

CPPFLAGS += -DDEBUG -DSYSV -D_REENTRANT
LDLIBS += -lsocket -lnsl

.KEEP_STATE:

LIBS += $(LIBRARY) $(DYNLIB)

all: $(TXTS) $(LIBS)

install: all $(ROOTLIBS) $(ROOTLINKS)

# include library targets
include ../Makefile.targ
