#
#ident	"@(#)Makefile.com	1.1	94/11/10 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/ptools/Makefile.com
#

ROOTPROCBIN =		$(ROOT)/usr/proc/bin
ROOTPROCLIB =		$(ROOT)/usr/proc/lib

ROOTPROCBINPROG =	$(PROG:%=$(ROOTPROCBIN)/%)
ROOTPROCLIBLIB =	$(LIBS:%=$(ROOTPROCLIB)/%)

$(ROOTPROCBIN)/%: %
	$(INS.file)

$(ROOTPROCLIB)/%: %
	$(INS.file)
