#
#ident	"@(#)Makefile	1.24	95/07/25 SMI"	/* 	*/
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/fs.d/nfs/mount/Makefile

FSTYPE=		nfs
LIBPROG=	mount
ATTMK=		$(PROG)

# duplicate ROOTETCFSTYPE value needed for installation rule
ROOTETCFSTYPE = $(ROOT)/etc/fs/nfs
$(ROOTETCFSTYPE)/%: %.ps
	$(INS.rename)

include		../../Makefile.fstype

OBJS=	$(LIBPROG).o fslib.o
SRCS=	$(OBJS:%.o=%.c)

#
# Message catalog
#
POFILE= mount.po

LDLIBS += -lrpcsvc -lnsl -lintl
CPPFLAGS += -I../.. -o $@

# `PS[ps]' is meant to imply "partially static" because
# the version of mount_nfs(1) that goes in $(ROOT)/etc/fs/nfs
# is dynamic wrt ld.so, but static wrt all others.
PS =		$(LIBPROG:%=%.ps)
$(PS) := LDLIBS = $(LDLIBS.cmd) $(NEEDU) -Bdynamic -ldl -Bstatic -lrpcsvc -lsocket -lnsl -lintl -lc

ROOTPSPROG	= $(LIBPROG:%=$(ROOTETCFSTYPE)/%)
CLOBBERFILES	+= $(LIBPROG) $(PS)

# We need to make these become externally available so
# that /etc/lib/nss_files.so.1 (ps) can find them at run time.
NEEDU=		-u __ctype \
		-u fgets \
		-u isspace \
		-u strcasecmp \
		-u __nss2herrno

.KEEP_STATE:

all:	$(LIBPROG) $(PS)

$(LIBPROG):	$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

$(PS):		$(OBJS)
		LD_OPTIONS=-I/etc/lib/ld.so.1 $(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

#
# message catalog
#
catalog: $(POFILE)

$(POFILE): $(SRCS)
	$(RM) $@
	$(COMPILE.cpp) $(SRCS)   > $(POFILE).i
	$(XGETTEXT)     $(XGETFLAGS) $(POFILE).i
	sed "/^domain/d"        messages.po     > $@
	$(RM) $(POFILE).i messages.po

install:	$(LIBPROG) $(PS) $(ROOTPSPROG)

lint:	lint_SRCS

clean:     
	$(RM) $(OBJS)

fslib.o: ../../fslib.c
	$(COMPILE.c) ../../fslib.c
