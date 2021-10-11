#
#ident	"@(#)Makefile	1.6	95/05/08 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# psm/stand/bootblks/ufs/sparc/sun4u/Makefile
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
PLATFORM	= sun4u
PLATLINKS	=

include $(BASEDIR)/ufs/Makefile.ufs
include $(BASEDIR)/Makefile.1275

.KEEP_STATE:

all:	$(PROG)

include $(BASEDIR)/Makefile.targ
