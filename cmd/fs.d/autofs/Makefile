#
#ident	"@(#)Makefile	1.16	95/07/25 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/fs.d/autofs/Makefile

FSTYPE=		autofs
AUTO=		automount
MOUNT=		mount
LIBPROG=	$(AUTO) $(MOUNT)
TYPEPROG=	automountd

LINKINSTALL=	$(ROOTUSRSBIN)/$(AUTO)
LINKVALUE=	../lib/fs/$(FSTYPE)/$(AUTO)
MAPS=		auto_master auto_home
MAPINSTALL=	$(MAPS:%=$(ROOTETC)/%)
OTHERINSTALL=	$(MAPINSTALL) $(LINKINSTALL);

include		../Makefile.fstype

$(MAPINSTALL)	:= OWNER= root
$(MAPINSTALL)	:= FILEMODE= 0644

XFILE=		$(ROOT)/usr/include/rpcsvc/autofs_prot.x

COMMON=		ns_generic.o ns_files.o ns_nisplus.o ns_nis.o \
		auto_mnttab.o auto_subr.o fslib.o

AUTOOBJS=	automount.o $(COMMON)

MOUNTOBJS=	mount.o fslib.o

TYPEOBJS=	autod_main.o autod_xdr.o key_generic.o \
		autod_mount.o autod_nfs.o nfs_cast.o ns_fn.o \
		$(COMMON)

POFILE= autofs.po
GREP= egrep
SED= sed

$(AUTO) := 	LDLIBS += -lnsl -lintl
$(MOUNT) :=	LDLIBS += -lintl
$(TYPEPROG) :=	LDLIBS += -lrpcsvc -lsocket -lnsl -lintl -ldl -lthread

CFLAGS +=	-v
fslib.o :=	CFLAGS += -o $@

CPPFLAGS=	-I.. $(CPPFLAGS.master) -D_REENTRANT
OBJS=		$(AUTOOBJS) $(MOUNTOBJS) $(TYPEOBJS)
AUTOSRCS=	$(AUTOOBJS:%.o=%.c)
MOUNTSRCS=	$(MOUNTOBJS:%.o=%.c)
TYPESRCS=	$(TYPEOBJS:%.o=%.c)
SRCS=		$(OBJS:%.o=%.c)

$(AUTO):	$(AUTOOBJS)
		$(LINK.c) -o $@ $(AUTOOBJS) $(LDLIBS)
		$(POST_PROCESS)

$(MOUNT):	$(MOUNTOBJS)
		$(LINK.c) -o $@ $(MOUNTOBJS) $(LDLIBS)
		$(POST_PROCESS)

$(TYPEPROG):	$(TYPEOBJS)
		$(LINK.c) -o $@ $(TYPEOBJS) $(LDLIBS)
		$(POST_PROCESS)

$(AUTOOBJS):	automount.h
$(TYPEOBJS):	automount.h autofs_prot.h

autod_xdr.c:	$(XFILE)
		$(RPCGEN) -C -c $(XFILE) -o $@

autofs_prot.h:	$(XFILE)
		$(RPCGEN) -C -h $(XFILE) -o $@

$(ROOTUSRSBIN)/$(AUTO):
		$(RM) $@; $(SYMLINK) $(LINKVALUE) $@

catalog: $(POFILE)

$(POFILE):
	$(RM) messages.po
	$(SED) -e 's/pr_msg/gettext/' `$(GREP) -l "pr_msg|gettext" *.[ch]` | \
	$(XGETTEXT) $(XGETFLAGS) -
	$(SED) -e '/^# msg/d' -e '/^domain/d' < messages.po > $@
	$(RM) messages.po

lint:
	$(LINT.c) $(AUTOSRCS)
	$(LINT.c) $(MOUNTSRCS)
	$(LINT.c) $(TYPESRCS)

clean:
	$(RM) $(OBJS) autofs_prot.h autod_xdr.c mountxdr.c

fslib.o: ../fslib.c
	$(COMPILE.c) ../fslib.c
