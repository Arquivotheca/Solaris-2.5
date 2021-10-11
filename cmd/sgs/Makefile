#
#ident	"@(#)Makefile	1.25	95/05/25 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/sgs/Makefile
#

include		../Makefile.cmd

SUBDIRS-common=	libconv		\
		liblddbg	\
		.WAIT		\
		libld		\
		libldmake	\
		libldstab	\
		.WAIT		\
		ld		\
		ldd		\
		lddstub		\
		rtld		\
		pvs		\
		ar		\
		dis		\
		dump		\
		error		\
		gprof		\
		lex		\
		lorder		\
		m4		\
		mcs		\
		nm		\
		prof		\
		ranlib		\
		size		\
		strip		\
		symorder	\
		tsort		\
		unifdef		\
		yacc

MSGSUBDIRS= ar m4 nm strip tsort yacc
SUBDIRS-i386=
SUBDIRS-sparc=	rtld.4.x


SUBDIRS=	$(SUBDIRS-common) $(SUBDIRS-$(MACH))

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
delete :=	TARGET= delete
lint :=		TARGET= lint
_msg :=		TARGET= catalog


# for messaging catalog
#
POFILE= sgs.po
POFILES= $(MSGSUBDIRS:%=%/%.po)

.KEEP_STATE:

.PARALLEL:	$(SUBDIRS)

all install:	native-add .WAIT $(SUBDIRS)

$(POFILE): $(MSGSUBDIRS)
	$(RM) $(POFILE)
	cat $(POFILES) > $(POFILE)

# built from lib/Makefile
install_lib:	FRC
		@cd lex; pwd; $(MAKE) $@
		@cd yacc; pwd; $(MAKE) $@

lint:		$(SUBDIRS)

delete \
clean clobber:	native-clobber .WAIT $(SUBDIRS)

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)


# Integration of ld and ld.so.1 in some developement cycles requires that both
# of these modules be built using the new ld.  This `native' target allows us
# to build a local ld which will then be used to build the delivered version of
# itself and ld.so.1.  Once this new functionality appears in the standard ld
# this target can be disabled.

native-add:	native-proto FRC
		@cd libconv/$(MACH); pwd; $(MAKE)
		@cd liblddbg/$(MACH); pwd; $(MAKE) native
		@cd libld/$(MACH); pwd; $(MAKE) native
		@cd ld/$(MACH); pwd; $(MAKE) native

native-clobber:
		rm -rf proto/$(MACH)

native-proto:
		-@mkdir -p proto/$(MACH)

FRC:

include ../Makefile.targ
