#
#ident	"@(#)Makefile	1.4	92/12/16 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libplot/t450/Makefile
#
LIBRARY= lib450.a

OBJECTS=  \
arc.o     box.o     circle.o  close.o   dot.o     erase.o   label.o   line.o  \
linmod.o  move.o    open.o    point.o   space.o   subr.o

# include library definitions
include ../../Makefile.lib


.KEEP_STATE:

all: $(IBS)

install: $(ROOTLIBS)

# include library targets
include ../../Makefile.targ
