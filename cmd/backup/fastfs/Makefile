#
#ident  "@(#)Makefile 1.4     93/10/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/backup/fastfs/Makefile
#

include ../Makefile.backup

FASTFS=		fastfs

HSMPROG=	$(FASTFS)
PRODUCT=	$(HSMPROG)

OBJECTS=	fastfs.o
SRCS=		$(OBJECTS:.o=.c)

GENERAL=	../include
CPPFLAGS=	-I$(GENERAL) -DPARTIAL $(USG) $(ANSI) \
			-DHSMROOT=\"$(HSMROOT)\" $(CPPFLAGS.master)
HSMLDLIBS +=	-lintl

CLOBBERFILES=	$(PRODUCT) $(DEBUGPRODUCTS)

FILEMODE=	0555

.KEEP_STATE:

all hsm:	$(PRODUCT) 

$(HSMPROG):	$(OBJECTS)
	$(LINK.c) -o $@ $(OBJECTS) $(HSMLDLIBS)
	$(POST_PROCESS)

FRC:

install hsminstall:	hsm $(HSMROOTPKGSBINPROG)

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS) $(DEBUGOBJS)

include ../Makefile.targ
