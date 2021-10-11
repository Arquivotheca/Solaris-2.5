#
# Copyright (c) 1993-1994 by Sun Microsystems, Inc.
# All rights reserved.
#
#ident	"@(#)Makefile	1.8	94/12/30 SMI"
#
# cmd/ssa/cli/Makefile
#
#      SSA CLI (Command Line Interface) Makefile 
#

include	../../Makefile.cmd

PROG		= ssacli ssaadm
ALL		= $(PROG)
CPPFLAGS	+= -I../sys
LDLIBS		+= ../lib/libssa.a -lintl

OPT_LVL		= 0

OBJS		= ssacli.o ssaadm.o
SRCS		= $(OBJS:%.o=%.c)

.KEEP_STATE:

# Create the Sparc Storage Array (Pluto) user interface programs
all debug browser debug_browser: $(PROG)
debug	:= CFLAGS += -g
browser	:= CFLAGS += -xsb
debug_browser	:= CFLAGS += -xsb -g

$(OBJS):	${SRCS}

install: all $(ROOTUSRSBINPROG)

lint:
	$(LINT.c) ${SRCS}

clean:
	$(RM) $(OBJS)

include	../../Makefile.targ
