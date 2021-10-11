#
#ident	"@(#)Makefile	1.5	92/09/23 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libmalloc/Makefile
#
LIBRARY= libmalloc.a

OBJECTS= malloc.o

# include library definitions
include ../Makefile.lib

CPPFLAGS += -D_REENTRANT
ROOTLIBDIR= $(ROOT)/usr/ccs/lib

.KEEP_STATE:

all: $(TXTS) $(LIBS) # $(PLIB) later

install: $(ROOTLIBS) # $(ROOTPLIB) later

# include library targets
include ../Makefile.targ
