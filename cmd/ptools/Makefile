#
#ident	"@(#)Makefile	1.1	94/11/10 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/ptools/Makefile
#
# include global definitions
include ../Makefile.cmd

SUBDIRS = libproc \
	pcred	\
	pfiles	\
	pflags	\
	pldd	\
	pmap	\
	prun	\
	psig	\
	pstack	\
	pstop	\
	ptime	\
	ptree	\
	pwait	\
	pwdx

all	:=	TARGET = all
install	:=	TARGET = install
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
lint	:=	TARGET = lint

.KEEP_STATE:

all install clean clobber lint: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
