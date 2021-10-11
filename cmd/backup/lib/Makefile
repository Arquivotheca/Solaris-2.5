#
#ident	"@(#)Makefile	1.23	95/01/09 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/backup/lib/Makefile
#
LIBRARY=	libdump.a

include ../Makefile.backup

XFILE=		operator.x
RPC_HDR=	operator.h
RPC_CLNT=	operator_clnt.c
RPC_XDR=	operator_xdr.c
HDRS=		../include/$(RPC_HDR)
RSRC=		$(HDRS) $(RPC_CLNT) $(RPC_XDR)

YFILE=		getdate.y
YSRC=		getdate.c

CLOBBERFILES=	$(RSRC) $(YSRC) $(GLIB)

OBJECTS=	$(RPC_CLNT:%.c=%.o) $(RPC_XDR:%.c=%.o) operator_lib.o \
		config.o rmtlib.o myrcmd.o $(YSRC:%.c=%.o) \
		dblib_update.o dbserv_xdr.o handles.o byteorder.o

# include library definitions
include ../../../lib/Makefile.lib

GENERAL=	../include
CPPFLAGS=	-I$(GENERAL) $(USG) $(ANSI) \
			-DHSMROOT=\"$(HSMROOT)\" $(CPPFLAGS.master)
LINTOUT=	lint.out
CLEANFILES=	$(OBJECTS) $(LINTOUT) $(LINTLIB) $(DEBUGS) $(HDRS)

# support for -g library
GLIB=		libdump_g.a
DEBUGS=		$(OBJECTS:%=.debug/%)
$(GLIB):=	AROBJS = $(DEBUGS)
$(GLIB):=	DIR = .debug
$(GLIB):=	CFLAGS= -g -xs -DDEBUG -DYYDEBUG ${SBFLAGS}

.KEEP_STATE:

all hsm:	$(LIBS)

debug:	$(GLIB)

.debug:
	-@mkdir -p $@

.debug/%.o: %.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

$(GLIB): .debug $$(DEBUGS)
	$(BUILD.AR)
	$(POST_PROCESS_A)

#  Rules to generate derived rpcgen files from operator.x spec file.

$(HDRS):	$(XFILE)
	$(RM) $@
	$(RPCGEN) -h $(XFILE) > $@

$(RPC_CLNT):	$(XFILE)
	$(RM) $@
	$(RPCGEN) -l $(XFILE) | sed -e '1,$$s/	memset/	(void) memset/' > $@

$(RPC_XDR):	$(XFILE)
	$(RM) $@
	$(RPCGEN) -c $(XFILE) > $@

$(OBJS):	$(HDRS)

rpcgen:		$(RSRC)

install hsminstall:	all

lint:		$(LINTLIB)

# include library targets
include ../../../lib/Makefile.targ
