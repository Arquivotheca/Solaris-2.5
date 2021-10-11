#
# ident	"@(#)Makefile	1.16	95/01/13 SMI"
#
# Copyright (c) 1990,1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/fs.d/nfs/lockd/Makefile

FSTYPE=		nfs
TYPEPROG=	lockd
ATTMK=		$(TYPEPROG)

include		../../Makefile.fstype

OBJS=		lockd.o
SRCS=		$(OBJS:%.o=%.c)
LDLIBS +=	-lnsl -lintl

$(TYPEPROG):	$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

lint:		$(SRCS) lint_SRCS

clean:
		$(RM) $(OBJS)

# Build the .o's with the following CFLAGS for best results with
# Purify.  Remove the .o's before trying to build a regular
# (non-Purify) lockd.
# CFLAGS = -g $($(MACH)_CFLAGS) -DPURIFY

lockd.pure:	$(OBJS)
		purify $(LINK.c) -o $@ $(OBJS) $(LDLIBS)
