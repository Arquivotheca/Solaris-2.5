#
#ident "@(#)Makefile	1.3	95/02/14 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# stand/lib/Makefile

include ../../Makefile.master

SUBDIRS = $(MACH)

all install clean clobber lint: $(SUBDIRS)

all	:=	TARGET = alllibs
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
lint	:=	TARGET = lint
install	:=	TARGET = install

UTSDIR= $(SRC)/uts

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

CSPATHS	= ./$(MACH) ./fs ./common
i386_CSINCS=  -I$(UTSDIR)/common  -I$(UTSDIR)/i86 \
 	  -I$(UTSDIR)/i86pc  	  -I$(ROOT)/usr/include \
 	  $(ENVCPPFLAGS1) $(ENVCPPFLAGS2) $(ENVCPPFLAGS3) $(ENVCPPFLAGS4)
sparc_CSINCS= -I$(UTSDIR)/common -I$(UTSDIR)/sparc \
	-I$(UTSDIR)/sparc/$(ARCHVERS) \
	-I$(UTSDIR)/sun -I$(UTSDIR)/sunmmu -I$(UTSDIR)/sfmmu \
	-I$(UTSDIR)/srmmu -I$(UTSDIR)/sun4c \
	-I$(UTSDIR)/sun4m -I$(UTSDIR)/sun4d -I$(UTSDIR)/sun4u \
	-I$(ROOT)/usr/include \
	$(ENVCPPFLAGS1) $(ENVCPPFLAGS2) $(ENVCPPFLAGS3) $(ENVCPPFLAGS4)
CSINCS	= $($(MACH)_CSINCS)
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

FRC:
