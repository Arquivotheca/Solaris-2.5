#
# @(#)Makefile 1.21 92/08/10 SMI
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/vgrind/Makefile
#

#
# These are the objects associated with the overall vgrind command.
#
VFONTEDPR=	vfontedpr
RETEST= 	retest
MACROS=		tmac.vgrind
LANGDEFS=	vgrindefs
CSHPROG=	vgrind

#
# These macros captures objects that ultimately will be installed in
# (respectively) /usr/bin, /usr/lib, and /usr/share/lib.
#
# Note also that retest is used strictly as a test program and is never
# installed.  We omit it here, so that the NSE doesn't spend cycles
# on it when acquiring and reconciling.
#
PROG= 		$(CSHPROG)
LIBPROG= 	$(VFONTEDPR) $(LANGDEFS)
TMACPROG=	$(MACROS)

VFONTEDPROBJS=	vfontedpr.o vgrindefs.o regexp.o
RETESTOBJS=	retest.o regexp.o

RETESTSRC=	$(RETESTOBJS:%.o=%.c)

OBJS= $(VFONTEDPROBJS) $(RETESTOBJS)
SRCS= $(OBJS:%.o=%.c)

#
# We can get away simply with omitting TMACPROGS to protect
# tmac.vgrind, since it's the only entry in that macro.
#
CLOBBERFILES=	$(LIBPROG) $(RETEST)

include ../Makefile.cmd

#
# Message catalog
#
POFILES= $(OBJS:%.o=%.po)
POFILE= vgrind.po

#
# Abbreviation for future use.
#
ROOTTMAC= $(ROOT)/usr/share/lib/tmac

#
# Override macro definitions from Makefile.cmd.  Necessary because
# we're building targets for multiple destinations.
#
ROOTLIBPROG= $(LIBPROG:%=$(ROOT)/usr/lib/%)
ROOTTMACPROG= $(TMACPROG:%=$(ROOTTMAC)/%)

#
# Conditional assignments pertinent to installation.
#
$(ROOTLIB)/$(LANGDEFS) 	:= FILEMODE= $(LIBFILEMODE)
$(ROOTTMACPROG) 	:= FILEMODE= 0644

#
# Conditional assignments for building LIBPROG
#
$(LIBPROG)	:= LDLIBS += -lintl

#
# The standard set of rules doesn't know about installing into
# subdirectories of /usr/share/lib, so we have to roll our own.
#
$(ROOTTMAC)/%: %
	$(INS.file)

.KEEP_STATE:

#
# retest appears here only in source form; see comment above for PROG.
#
all:	$(PROG) $(LIBPROG) $(TMACPROG) $(RETESTSRC)

#
# message catalog
#
$(POFILE): $(POFILES)
	rm -f $(POFILE)
	echo 'msgid         "vgrind: %s option must have argument"' > $(POFILE)
	echo 'msgstr' >> $@
	cat $(POFILES)  >> $@

$(VFONTEDPR): $(VFONTEDPROBJS)
	$(LINK.c) -o $@ $(VFONTEDPROBJS) $(LDLIBS)

$(LANGDEFS):	$(LANGDEFS).src
	$(CP) $? $@

$(RETEST): $(RETESTOBJS)
	$(LINK.c) -o $@ $(RETESTOBJS) $(LDLIBS)

#
# XXX:	There should be a .csh rule in default.mk.  Until there is, we
# do it by hand.
#
$(CSHPROG): $(CSHPROG).csh
	$(RM) $@
	sed -e "s/TEXT_DOMAIN/${TEXT_DOMAIN}/g" $(CSHPROG).csh > $@
	chmod +x $@



#
# We add all as a dependent to make sure that the install pattern
# matching rules see everything they should.  (This is a safety net.)
#
# XXX:	ROOTTMAC shouldn't appear as a dependent; it's here as a
# 	bandaid(TM) until /usr/lib/tmac becomes a symlink to
#	/usr/share/lib/tmac.
#
install: all $(ROOTTMAC) $(ROOTPROG) $(ROOTLIBPROG) $(ROOTTMACPROG)

# XXX: see above.
$(ROOTTMAC):
	$(INS.dir)

clean:
	$(RM) $(OBJS)

#
# Don't worry about linting retest.
#
lint:=	SRCS = $(VFONTEDPROBJS:%.o=%.c)
lint: lint_SRCS

include ../Makefile.targ
