#
#ident	"@(#)Makefile	1.4	91/05/04 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# ucblib/libucb/Makefile
#

LIBRARY= libucb.a
VERS = .1

# include common library definitions
include ../../lib/Makefile.lib

LIBS= $(LIBRARY) $(DYNLIB) $(LINTLIB)

ROOTLIBDIR= $(ROOT)/usr/ucblib

LINTSRC= $(LINTLIB:%.ln=%)
ROOTLINTDIR= $(ROOTLIBDIR)
ROOTLINT= $(LINTSRC:%=$(ROOTLINTDIR)/%)

# install rule for lint library target
$(ROOTLINTDIR)/%: $(MACH)/%
	$(INS.file)

.KEEP_STATE:

all: $(TXTS) $(MACHLIBS)

$(LINTLIB):
	@cd $(MACH); pwd; $(MAKE) $@

install: $(ROOTLIBS) $(ROOTLINT) $(ROOTLINKS)

clean clobber: FRC
	@cd $(MACH); pwd; $(MAKE) $@

links:
	-$(RM) $(MACH)
	$(SYMLINK) `mach` $(MACH)

# include MACH-specific library targets
include ../../lib/Makefile.mach
