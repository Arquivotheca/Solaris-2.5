#
#ident	"@(#)Makefile	1.4	94/08/12 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# lib/libmp/Makefile

LIBRARY= libmp.a
VERS= .1

OBJECTS= gcd.o madd.o mdiv.o mout.o msqrt.o mult.o pow.o util.o

# include library definitions
include ../Makefile.lib

LIBS += $(DYNLIB)

.KEEP_STATE:

all: $(LIBS)

install: all $(ROOTLIBS) $(ROOTLINKS)

# include library targets
include ../Makefile.targ

