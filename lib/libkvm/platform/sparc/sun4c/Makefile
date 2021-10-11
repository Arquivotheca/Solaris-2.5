#
#ident	"@(#)Makefile	1.12	93/12/03 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libkvm/platform/sparc/sun4c/Makefile
#
# Makefile for the platform dependent libkvm adjunct for sun4c.  This only
# has any utility as a shared object.
#
LIBKVM_BASE =	../../..
PLATFORM =	sun4c
KARCH =		sun4c

OBJECTS =	kvmplatdep.o

#
# Include library definitions.
#
include $(LIBKVM_BASE)/platform/Makefile.com

#
# Additional build rule to allow SMCC platforms to share source.
#
objs/%.o profs/%.o pics/%.o: ../smcc/%.c
	$(COMPILE.c) -G -o $@ $<
	$(POST_PROCESS_O)
 
.KEEPSTATE:

all:		$(DYNLIB)

#
# Include library targets.
#
include $(LIBKVM_BASE)/platform/Makefile.targ
