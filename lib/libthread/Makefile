#
#ident	"@(#)Makefile 1.11 94/09/14 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libthread/Makefile
#
LIBTHREADDIR=.
LIB_PIC= libthread_pic.a
LIBRARY = libthread.a
VERS = .1

all :=		TARGET=all
install :=	TARGET=install
clean :=	TARGET=clean
clobber :=	TARGET=clobber

ROOTPICSDIR=	$(ROOT)/usr/lib/pics
ROOTPICS=	$(LIB_PIC:%=$(ROOTPICSDIR)/%)

# include common library definitions
include ../Makefile.lib

CPPFLAGS= $(INCLIST) $(CPPFLAGS.master) 

# do not install libthread.a. set LIBS to be equal to only DYNLIB
LIBS = $(DYNLIB)



#
# This does not need to be set here, Makefile.master sets MACH to sparc.
#
#MACH=sparc

.KEEP_STATE:

all: $(MACHLIBS) 

install: all $(ROOTLIBS) $(ROOTLINKS)

$(ROOTLIBDIR):
	$(INS.dir)

clean clobber: FRC
	@cd $(MACH); pwd; $(MAKE) $@

ARCHITECTURES = $(MACH)

CSDIR   = .
CSDIRS  = sparc i386 common inc sys
CSPATHS = $(CSDIRS:%=$(CSDIR)/%)
CSINCS  = $(CSPATHS:%=-I%)
CSCOPE  = cscope
CTAGS   = ctags

.PRECIOUS: cscope.out

cscope.out: cscope.files FRC
	${CSCOPE} -b -f `pwd`/cscope.out

cscope.files: FRC
	@-$(RM) cscope.files
	echo "$(CSINCS)" > cscope.files
	find $(CSPATHS) -name SCCS -prune -o \
		-type d -name '.del-*' -prune -o -type f \
		\( -name '*.[csh]' -o -name 'Makefile*' -o -name '*.il' \) \
		  -print >> cscope.files
	@wc -l cscope.files

tags: tags.list FRC
	${CTAGS} -wt `cat tags.list`

tags.list: cscope.files
	@$(RM) tags.list
	grep '\.c$$' cscope.files > tags.list
	grep '\.s$$' cscope.files >> tags.list
	grep '\.h$$' cscope.files >> tags.list

FRC:

# install rules for lib_pic.a - not invoked thru the install target
# these exist only if sometime you do want to build libthread_pic.a
$(ROOTPICSDIR):
	$(INS.dir)

$(ROOTPICSDIR)/%: $(MACH)/%
	$(INS.file)

$(MACH)/$(LIB_PIC): FRC
	@cd $(MACH); pwd; $(MAKE) $(LIB_PIC)

# include MACH-specific library targets
include ../Makefile.mach


