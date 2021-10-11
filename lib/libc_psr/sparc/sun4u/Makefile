#
#ident	"@(#)Makefile 1.8 95/05/01 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/libc_psr/sparc/sun4u/Makefile
#
# Platform specific Makefile for libc_psr.
#
# PLATFORM is the target for the binary installation.
#
SRCDIR		= ../..
PLATFORM	= sun4u

OBJECTS		= memcpy.o memset.o memcmp.o

include $(SRCDIR)/Makefile.com

#
# comment out the following until real contents appear
#
ASFLAGS		+= -xarch=v8plusa

.KEEP_STATE:

all:	$(LIBS)

install: all $(USR_PSM_LIBS)

include $(SRCDIR)/Makefile.targ
