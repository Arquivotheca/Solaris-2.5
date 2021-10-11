#
# Copyright (c) 1993-1994 by Sun Microsystems, Inc.
# All rights reserved.
#
#ident	"@(#)Makefile	1.2	94/12/30 SMI"
#
# cmd/ssa/lib/Makefile
#
#      Pluto interface library Makefile 
#

include		../../../lib/Makefile.lib

LIBRARY		= libssa.a

CPPFLAGS	+= -D_REENTRANT -I../sys

OPT_LVL		= 0

OBJS		=  funct.o io.o uscsi_cmd.o genf.o
SRCS		= $(OBJS:%.o=%.c)

.KEEP_STATE:

# Create a static library 
all debug browser debug_browser:	$(LIBRARY)
debug	:= CFLAGS += -g
browser	:= CFLAGS += -xsb
debug_browser	:= CFLAGS += -xsb -g

$(LIBRARY):	$(OBJS)	
	$(AR) crv libssa.a ${OBJS}

$(OBJECTS):	${SRCS}

install: all

lint:
	$(LINT.c) ${SRCS}

clean:
	$(RM) $(OBJS)
    
clobber: clean
	$(RM) $(LIBRARY)

.PRECIOUS: $(LIBRARY)
