#
#ident	"@(#)Makefile	1.2	94/12/10 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# psm/stand/bootblks/hsfs/sparc/sun4c/Makefile
#

#
# Platform specific Makefile for the boot block.
#
# PLATFORM is the target for the binary installation.
#
# PLATLINKS is a list of platforms which are to be linked to this binary
#	at the file level.
#
BASEDIR		= ../../..
PLATFORM	= sun4c
PLATLINKS	=

include $(BASEDIR)/hsfs/Makefile.hsfs
include $(BASEDIR)/obp-c/Makefile.rules

.KEEP_STATE:

all:	$(PROG)

include $(BASEDIR)/obp-c/Makefile.targ
include $(BASEDIR)/Makefile.targ
