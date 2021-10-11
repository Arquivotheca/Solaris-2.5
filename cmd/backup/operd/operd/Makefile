#
#ident  "@(#)Makefile 1.11     93/10/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/backup/operd/monitor/Makefile
#

include ../../Makefile.backup

OPERD=		rpc.operd

HSMPROG=	$(OPERD)
PRODUCT=	$(HSMPROG)

OBJECTS=	operd.o init.o forward.o message.o
SRCS=		$(OBJECTS:.o=.c)

LOCAL=		.
GENERAL=	../../include
CPPFLAGS=	-I$(LOCAL) -I$(GENERAL) $(USG) $(ANSI) \
			-DHSMROOT=\"$(HSMROOT)\" $(CPPFLAGS.master)
LIBDUMP=	../../lib/$(DUMPLIB)
HSMLDLIBS=	$(LDLIBS.cmd) $(LIBDUMP) -lsocket -lnsl -lintl

CLOBBERFILES=	$(PRODUCT) $(DEBUGPRODUCTS)

FILEMODE=	0555

.KEEP_STATE:

all hsm:	$(PRODUCT) 

$(HSMPROG):	$(OBJECTS) $$(LIBDUMP)
	$(LINK.c) -o $@ $(OBJECTS) $(HSMLDLIBS)
	$(POST_PROCESS)

$$(LIBDUMP): FRC
	$(DO_LIBDIR)

FRC:

install hsminstall:	hsm $(HSMROOTPKGLIBPROG)

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS) $(DEBUGOBJS)

include ../../Makefile.targ
