#
#ident	"@(#)Makefile	1.24	94/09/14 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libc/Makefile
#
LIBRARY= libc.a
VERS = .1
LIB_PIC= libc_pic.a

# values deliverables
VALUES = values-Xa.o values-Xc.o values-Xs.o values-Xt.o values-xpg4.o
INCLIST= -Iinc
ROOTCCSLIB= $(ROOT)/usr/ccs/lib
ROOTVALUES= $(VALUES:%=$(ROOTCCSLIB)/%)
ROOTPICSDIR=	$(ROOT)/usr/lib/pics
ROOTPICS=	$(LIB_PIC:%=$(ROOTPICSDIR)/%)

# include common library definitions
include ../Makefile.lib

#
# message catalogue file
#
TEXT_DOMAIN= SUNW_OST_OSLIB


CPPFLAGS= $(INCLIST) $(CPPFLAGS.master)
LIBS += $(DYNLIB) $(LINTLIB)

# definitions for install_h target
HDRS=	getxby_door.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKDIRS=	$(HDRS:%.h=%.check)

# install rule for install_h target
$(ROOTHDRDIR)/%: port/gen/%
	$(INS.file)

LINTSRC= $(LINTLIB:%.ln=%)
ROOTLINTDIR= $(ROOTLIBDIR)
ROOTLINT= $(LINTSRC:%=$(ROOTLINTDIR)/%)

$(ROOTLIBDIR)/$(DYNLIB):=	FILEMODE = 755

# build rule for VALUES
%.o: port/gen/%.c
	$(COMPILE.c) $<
	$(POST_PROCESS_O)

.KEEP_STATE:

all: values $(TXTS) $(MACHLIBS) $(MACH)/$(LIB_PIC) $(MACHPLIB)

values: $(VALUES)

install: all insvalues $(ROOTLIBS) $(ROOTLINKS) $(ROOTPICSDIR) \
	$(ROOTPICS) $(ROOTPLIB) $(ROOTLINT)

install_h: $(ROOTHDRS)

check:	$(CHECKHDRS)

insvalues: values $(ROOTVALUES)

# install rule for VALUES
$(ROOTCCSLIB)/%: %
	$(INS.file)

# install rules for lib_pic.a
$(ROOTPICSDIR):
	$(INS.dir)

# install rule for lint library target
$(ROOTLINTDIR)/%: port/%
	$(INS.file)

$(LINTLIB):
	@cd $(MACH); pwd; $(MAKE) $@

$(ROOTPICSDIR)/%: $(MACH)/%
	$(INS.file)

$(MACH)/$(LIB_PIC): FRC
	@cd $(MACH); pwd; $(MAKE) $(LIB_PIC)

clean clobber: FRC
	@cd $(MACH); pwd; $(MAKE) $@


links:
	-$(RM) $(MACH)
	$(SYMLINK) `mach` $(MACH)

_msg:	$(MSGDOMAIN) catalog

catalog:
	sh ./makelibccatalog.sh $(MSGDOMAIN)

$(MSGDOMAIN):
	$(INS.dir)

ARCHITECTURES = $(MACH)
 
CSDIR   = .
CSDIRS  = sparc i386 port inc
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
# include MACH-specific library targets
include ../Makefile.mach

