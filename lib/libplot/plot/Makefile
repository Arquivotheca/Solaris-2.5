#
#ident	"@(#)Makefile	1.5	92/12/16 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libplot/plot/Makefile
#
LIBRARY=	libplot.a

OBJECTS=  \
arc.o     box.o     circle.o  close.o   cont.o    dot.o     erase.o   label.o  \
line.o    linmod.o  move.o    open.o    point.o   putsi.o   space.o

# include library definitions
include ../../Makefile.lib

# TXTS allows the AT&T makefile to be bootstrapped into the NSE.

.KEEP_STATE:

all: $(LIBS)

install: $(ROOTLIBS)

# include library targets
include ../../Makefile.targ
