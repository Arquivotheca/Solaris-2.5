#
#ident	"@(#)Makefile.com 1.4     93/12/17 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libkvm/platform/Makefile.com
#
# This Makefile contains the common definitions for building the platform
# specific libkvm adjunct, lkvm_pd.so.
#

# Note that this archive is never built or installed, but its name must be
# defined for the standard library build rules to function.
#
LIBRARY=lkvm_pd.a

#
# Include library definitions
#
include $(LIBKVM_BASE)/../Makefile.lib

#
# Override values set in Makefile.lib.  Only the dynamic versions of lkvm_pd
# are built. The version (VERS) need not remain in sync with that of libkvm.so.
# 
LIBS = $(DYNLIB)
VERS = .1
CLOBBERFILES =

#
# Include PSM definitions
#
include $(LIBKVM_BASE)/../../Makefile.psm

#
# The following include path gets the platform dependent headers and kvm.h
# from the installed proto area.  To get kvm.h locally, add -I(LIBKVM_BASE)
# to the path.
#
IFLAGS =	-I$(ROOT)/usr/platform/$(PLATFORM)/include
CPPFLAGS = 	-D_KMEMUSER -D_MACHDEP -D$(KARCH) $(IFLAGS) $(CPPFLAGS.master)
LDLIBS =	-lkvm
