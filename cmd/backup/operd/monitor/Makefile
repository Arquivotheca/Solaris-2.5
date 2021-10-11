#
#ident  "@(#)Makefile 1.15     93/10/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/backup/operd/monitor/Makefile
#

include ../../Makefile.backup

OPERMON=	opermon

HSMPROG=	$(OPERMON)
PRODUCT=	$(HSMPROG)

OBJECTS=	commands.o help.o message.o main.o regex.o screen.o
SRCS=		$(OBJECTS:.o=.c)

LOCAL=		.
GENERAL=	../../include
CPPFLAGS=	-I$(LOCAL) -I$(GENERAL) $(USG) $(ANSI) \
			-DHSMROOT=\"$(HSMROOT)\" $(CPPFLAGS.master)
LIBDUMP=	../../lib/$(DUMPLIB)
HSMLDLIBS=	$(LDLIBS.cmd) $(LIBDUMP) -lsocket -lnsl \
		-lcurses -ltermlib -lintl

CLOBBERFILES=	$(PRODUCT) $(DEBUGPRODUCTS)

FILEMODE=	0555

.KEEP_STATE:

all hsm:	$(PRODUCT) 

$(HSMPROG):	$(OBJECTS) $$(LIBDUMP)
	$(LINK.c) -o $@ $(OBJECTS) $(HSMLDLIBS)
	$(POST_PROCESS)

$$(LIBDUMP): FRC
	$(LIBDIR)

FRC:

install hsminstall:	hsm $(HSMROOTPKGSBINPROG)

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS) $(DEBUGOBJS)

include ../../Makefile.targ
