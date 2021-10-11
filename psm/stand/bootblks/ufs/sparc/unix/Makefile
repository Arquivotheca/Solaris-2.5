#
#ident	"@(#)Makefile	1.2	94/12/10 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# psm/stand/bootblks/ufs/sparc/unix/Makefile
#

#
# Build the "Unix" version of the boot block.
# Useful solely for testing.  DO NOT INSTALL IT!
#

BASEDIR		= ../../..
PLATFORM	= unix
PLATLINKS	=

include $(BASEDIR)/ufs/Makefile.ufs
include $(BASEDIR)/obp-c/Makefile.rules

UNIX_OBJS	= ufs.o unix_devio.o

$(UNIX_OBJS) :=	CFLAGS += -g

.KEEP_STATE:

all:	$(PROG)

$(PROG): $(UNIX_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -g -o $@ $(UNIX_OBJS)

lint: FRC

clean:
	$(RM) $(UNIX_OBJS)

clobber: clean
	$(RM) $(PROG)

install: FRC

FRC:

