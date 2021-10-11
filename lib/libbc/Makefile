#
#ident	"@(#)Makefile	1.14	95/08/29 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/libbc/Makefile
#
LIBRARY= libc.a
VERS= .1.9

# include common library definitions
include ../Makefile.lib
# the bcp libraries go in /usr/4lib. override ROOTLIBDIR from ../Makefile.lib
ROOTLIBDIR=     $(ROOT)/usr/4lib

DYNLIBS5= libc.so.2.9

#DYNLIBS5 is also defined in sparc/Makefile

LIBS = $(DYNLIB) $(DYNLIBS5)

# dynamic libraries should have executable file mode
FILEMODE= 755

.KEEP_STATE:

all clean clobber: links FRC
	@cd $(MACH); pwd; $(MAKE) $@

install: links $(MACHLIBS) $(ROOTLIBS)

#
# NOTE:  all of these processor specific links have been hardcoded
#        into this Makefile.  This is because the libbc library
#     	 is for the Binary Compatiblity Package.  The BCP is
#	 only relevent for Sun4 machines.  Future architectures will
#	 not use this package!!!
#
links:
	-$(RM) sparc/inc/machine 
	$(SYMLINK) sparc sparc/inc/machine 
	-$(RM) inc/include/machine
	$(SYMLINK) sun4 inc/include/machine 
	-$(RM) mach
	$(SYMLINK) sparc mach
	-$(RM) sparc/inc/sun4
	$(SYMLINK) sparc sparc/inc/sun4
	
# include MACH-specific library targets
include ../Makefile.mach

