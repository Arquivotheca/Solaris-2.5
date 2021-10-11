#
#ident	"@(#)Makefile	1.14	95/02/14 SMI"
#
# Copyright (c) 1991-1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# psm/stand/boot/Makefile
#

SUBDIRS = $(MACH)

all		:=	TARGET= all
install		:=	TARGET= install
clean		:=	TARGET= clean
clobber		:=	TARGET= clobber
lint		:=	TARGET= lint

.KEEP_STATE:

all install lint clean: $(SUBDIRS)

clobber: $(SUBDIRS)
	$(RM) *cscope*
#
# The cscope.out file is made in the current directory and spans all
# architectures and boot fs types.
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
UTSDIR=		../../../uts
STANDLIBDIR=	../../../stand/lib
STANDSYSDIR=	../../../stand/sys
i386_PROMDIR=	$(UTSDIR)/i86/promif
sparc_PROMDIR=	../../promif
PROMDIRS=	$(sparc_PROMDIR) $(i386_PROMDIR)
CSSUBDIRS= $(SUBDIRS)
CSDIR	= .
CSDIRS	= $(STANDLIBDIR) $(STANDSYSDIR) $(PROMDIRS) $(CSSUBDIRS) common
CSPATHS	= $(CSDIRS:%=$(CSDIR)/%)
CSINCS	= $(CSPATHS:%=-I%) \
 	  -I$(UTSDIR)/sun4c \
	  -I$(UTSDIR)/sun4u \
	  -I$(UTSDIR)/sunmmu \
	  -I$(UTSDIR)/sfmmu \
	  -I$(UTSDIR)/sparc/v7 \
 	  -I$(UTSDIR)/sparc/v9 \
	  -I$(UTSDIR)/sparc \
 	  -I$(UTSDIR)/sun \
 	  -I$(UTSDIR)/common \
 	  -I$(UTSDIR)/i86 \
 	  -I$(UTSDIR)/i86pc \
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
	    \( -name '*.[chs]' -o -name 'Makefile*' -o \
	    -name 'mapfile*' -o -name '*.il' \) \
	    -a ! -name '.del*' -print >> cscope.files
	@wc -l cscope.files

#
# Create a tags data base, similar to above.
# Since many assembler files now contain C fragments for lint,
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
	${CTAGS} -wt `sed 1d cscope.files`

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
