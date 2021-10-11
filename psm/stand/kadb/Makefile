#
#ident	"@(#)Makefile	1.26	95/02/01 SMI"
#
# Copyright (c) 1995, by Sun Microsystems, Inc.
#
# psm/stand/kadb/Makefile
#

# All the real work gets done in kernel-architecture directories,
# and that's where the targets wind up. This makefile just gets us there.

SUBDIRS = $(MACH)

TOPDIR= ../../..

all		:=	TARGET= all
install		:=	TARGET= install
clean		:=	TARGET= clean
clobber		:=	TARGET= clobber
lint		:=	TARGET= lint
clean.lint	:=	TARGET= clean

.KEEP_STATE:

all install lint clean.lint: $(SUBDIRS)

#
# Since the subdirs only clobber the platform specific macros, clobber the
# platform independent ones here.
#
clean clobber: $(SUBDIRS)
	@cd $(TOPDIR)/uts/adb/$(MACH); pwd; $(MAKE) $(TARGET)

#
# The cscope.out file is made in the current directory and spans
# all architectures.
# Things to note:
#	1. We use relative names for cscope and tags.
#	2. We *don't* remove the old cscope.out file, because cscope is
#	   smart enough to only build what has changed.  It can be
#	   confused, however, if files are renamed or removed, so it may
#	   be necessary to manually remove cscope.out if a lot of
#	   reorganization has occured.
#	3. We deliberately avoid names that include '.del' in their names
#	   as these are usually files that have been 'deleted' by nsefile.
#
CSDIR	= .

CSDIRS	= $(TOPDIR)/cmd/adb/sparc $(TOPDIR)/cmd/adb/common \
	$(TOPDIR)/stand/lib $(TOPDIR)/stand/sys \
	$(TOPDIR)/psm/promif $(TOPDIR)/uts/i86/promif \
	$(SUBDIRS-sparc) $(SUBDIRS-i386) common

CSPATHS	= $(CSDIRS:%=$(CSDIR)/%)

CSINCS	= $(CSPATHS:%=-I%) \
	  $(SUBDIRS-sparc:%=-I../../uts/%) \
	  -I$(TOPDIR)/uts/sun \
	  -I$(TOPDIR)/uts/sunmmu \
	  -I$(TOPDIR)/uts/srmmu \
	  -I$(TOPDIR)/uts/i86pc \
	  -I$(TOPDIR)/uts/i86 \
	  $(SUBDIRS-i386:%=-I$(TOPDIR)/uts/%) \
	  -I$(TOPDIR)/uts/common \
	  -I$(ROOT)/usr/include \
	  $(ENVCPPFLAGS1) $(ENVCPPFLAGS2) $(ENVCPPFLAGS3) $(ENVCPPFLAGS4)

CSCOPE	= cscope
CTAGS	= ctags

cscope.out: cscope.files FRC
	${CSCOPE} -b -f `pwd`/$@

cscope.files:   FRC
	@$(RM) -f cscope.files
	echo $(CSINCS) > cscope.files
	find $(CSPATHS) -name SCCS -prune -o \
	    \( -type d -name '.del*' \) -prune -o -type f \
	    \( -name '*.[chs]' -o -name 'Makefile*' -o -name '*.il' \) \
	    -a ! -name '.del*' -print >> cscope.files
	@wc -l cscope.files

#
# Create a tags data base, similar to above.
# Since assembler files now contain C fragments for lint,
# the lint fragments will allow ctags to "work" on assembler.
#
# Things to note:
#	1. We put plain files before headers, and .c before .s (because
#	   ctags doesn't understand assembly comments).
#	2. We *don't* sort the output of find, as we want functions in
#	   architecture directories to take precedence over those in
#	   sun, and those over those in common.
#
tags: cscope.files
	$(CTAGS) -wt `sed 1d cscope.files`

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
