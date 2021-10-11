#
#ident	"@(#)Makefile.com 1.3 94/12/06 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/libc_psr/Makefile.com
#

LIBRARY		= libc_psr.a
VERS		= .1

include $(SRCDIR)/../Makefile.lib
include $(SRCDIR)/../../Makefile.psm

LIBS		= $(DYNLIB)
IFLAGS		= -I$(SRCDIR)/../libc/inc -I$(ROOT)/usr/platform/$(PLATFORM)/include
CPPFLAGS	= -D_REENTRANT -D$(MACH) $(IFLAGS) $(CPPFLAGS.master)
ASDEFS		= -D__STDC__ -D_ASM $(CPPFLAGS)
ASFLAGS		= -P $(ASDEFS)

#
# install rule
#
$(USR_PSM_LIB_DIR)/%: % $(USR_PSM_LIB_DIR)
	$(INS.file)

#
# build rules
#
pics/%.o: %.s
	$(AS) $(ASFLAGS) $< -o $@
	$(POST_PROCESS_O)
