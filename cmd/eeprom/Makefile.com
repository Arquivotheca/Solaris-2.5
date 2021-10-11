#
#ident	"@(#)Makefile.com	1.7	94/10/14 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/eeprom/Makefile.com
#

include $(SRCDIR)/../Makefile.cmd
include $(SRCDIR)/../../Makefile.psm

PROG		= eeprom

FILEMODE	= 02555
DIRMODE		= 755
OWNER		= bin
GROUP		= sys

#
# program implementation supports openprom machines.  identical versions
# are installed in /usr/platform for each machine type
# because (at this point in time) we have no guarantee that a common version
# will be available for all potential sparc machines (eg: ICL, solbourne ,...).
#
# The identical binary is installed several times (rather than linking them
# together) because they will be in separate packages.
#
# Now that it should be obvious that little (if anything) was gained from
# this `fix-impl' implementation style, maybe somebody will unroll this in
# distinct, small and simpler versions for each PROM type.
#
OBJS	= openprom.o loadlogo.o error.o
LINT_OBJS	= $(OBJS:%.o=%.ln)
SOURCES		= openprom.c loadlogo.c error.c

.PARALLEL: $(OBJS)

%.o:	%.c
	$(COMPILE.c) -o $@ $<

%.o:	$(SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<

%.ln:	%.c
	$(LINT.c) -c $@ $<

%.ln:	$(SRCDIR)/%.c
	$(LINT.c) -c $@ $<
