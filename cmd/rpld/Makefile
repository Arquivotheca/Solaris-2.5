#
#ident	"@(#)Makefile	1.7	94/07/27 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/rpld/Makefile
#

PROG=	rpld

CONFILE=	rpld.conf

OBJS= 	rpld.o llcsetup.o debug.o sighandler.o params.o \
	clientop.o rplin.o rplout.o

LIBOBJS=	dl_open.o dl_bind.o dl_snd.o dl_rcv.o dl_addr.o dl_close.o \
	 	dl_unbind.o dl_attach.o dl_info.o dl_error.o

LIBDLA= 	libdla.a

LIBS= 		$(LIBDLA) -lsocket

AR=		ar -r

CLOBBERFILES=	$(LIBDLA)

include ../Makefile.cmd

$(ROOTETC)/$(CONFILE):=	FILEMODE = 644
$(ROOTETC)/$(CONFILE):=	OWNER = root
$(ROOTETC)/$(CONFILE):=	GROUP = sys

CPPFLAGS +=	-I.

.KEEP_STATE:

all:	$(PROG)

$(PROG): 	$(OBJS) $(LIBDLA)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS) $(LIBS)

$(LIBDLA): 	$(LIBOBJS)
	$(AR) $(LIBDLA) $?

install:	all $(ROOTUSRSBINPROG) $(ROOTETC)/$(CONFILE)

clean:
	$(RM) $(OBJS) $(LIBOBJS)

include ../Makefile.targ
