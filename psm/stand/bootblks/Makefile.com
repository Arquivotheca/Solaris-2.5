#
#ident	"@(#)Makefile.com	1.3	94/12/22 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
#
# psm/stand/bootblks/Makefile.com
#
TOPDIR = ../../../$(BASEDIR)

include $(TOPDIR)/Makefile.master
include $(TOPDIR)/Makefile.psm

STANDDIR	= $(TOPDIR)/stand
PSMSTANDDIR	= $(TOPDIR)/psm/stand

SYSHDRDIR	= $(STANDDIR)
SYSLIBDIR	= $(STANDDIR)/lib/$(MACH)

PSMSYSHDRDIR	= $(PSMSTANDDIR)
PSMNAMELIBDIR	= $(PSMSTANDDIR)/lib/names/$(MACH)
PSMPROMLIBDIR	= $(PSMSTANDDIR)/lib/promif/$(MACH)

#
# 'bootblk' is the basic target we build - in many flavours
#
PROG		= bootblk

#
# Used to convert Forth source to isa-independent FCode.
#
TOKENIZE	= tokenize

#
# Common install modes and owners
#
FILEMODE	= 444
DIRMODE		= 755
OWNER		= root
GROUP		= sys

#
# Lint rules (adapted from Makefile.uts)
#
LHEAD		= ( $(ECHO) "\n$@";
LTAIL		= ) | grep -v "pointer cast may result in improper alignment"
LINT_DEFS	+= -Dlint

#
# For building lint objects
#
LINTFLAGS.c	= -nsxum
LINT.c		= $(LINT) $(LINTFLAGS.c) $(LINT_DEFS) $(CPPFLAGS) -c
LINT.s		= $(LINT) $(LINTFLAGS.s) $(LINT_DEFS) $(CPPFLAGS) -c

#
# For building lint libraries
#
LINTFLAGS.lib	= -nsxum
LINT.lib	= $(LINT) $(LINTFLAGS.lib) $(LINT_DEFS) $(CPPFLAGS)

#
# For complete pass 2 cross-checks
#
LINTFLAGS.2	= -nsxm
LINT.2		= $(LINT) $(LINTFLAGS.2) $(LINT_DEFS) $(CPPFLAGS)
