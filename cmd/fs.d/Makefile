#
#ident	"@(#)Makefile	1.39	95/01/04 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/fs.d/Makefile

# The filesystem independent utilities clri, fsdb, dcopy, labelit, and mkfs
# are all built from the source file switchout.c. They are all then links
# to the same object. This is accomplished by:
#	1) building clri from switchout.c (had to choose one)
#	2) installing it in the target directory
#	3) linking the others to clri.
# In a similar manner, ncheck is linked to ff.

STATPROG=	mount umount
PROG=		$(STATPROG) fsck df volcopy ff
XPG4PROG=	df
SPPROG=		clri
MNTTAB=		mnttab
FSTYPE=		fs

# install rule for static versions
# deliberately ahead of a similar rule in ../Makefile.cmd
#
ROOT=		/proto
$(ROOT)/sbin/%: %.static
	$(INS.rename)

include ../Makefile.cmd

SUBDIR1= pcfs lofs
SUBDIR2= fd nfs hsfs proc ufs tmpfs cachefs autofs
i386_SUBDIRS= s5fs
SUBDIRS= $(SUBDIR1) $(SUBDIR2) $($(MACH)_SUBDIRS)

CLOBBERFILES += $(STATIC)

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint
_msg:=          TARGET= catalog

SBINF=		umount mount
USRSBINF=	umount mount df clri fsck volcopy ff
USRSBINCLRI=	dcopy fsdb labelit mkfs
USRSBINFF=	ncheck

ETC2SBIN=	umount mount
ETC2USRSBIN=	clri fsdb mkfs fsck labelit dcopy volcopy ff ncheck
USRBIN2USRSBIN=	df

ROOTSBINF=		$(SBINF:%=$(ROOTSBIN)/%)
ROOTUSRSBINF=		$(USRSBINF:%=$(ROOTUSRSBIN)/%)
ROOTUSRSBINCLRI=	$(USRSBINCLRI:%=$(ROOTUSRSBIN)/%)
ROOTUSRSBINFF=		$(USRSBINFF:%=$(ROOTUSRSBIN)/%)
ROOTETCMNTTAB=		$(MNTTAB:%=$(ROOTETC)/%)
ROOTFSTYPE=		$(ROOTETC)/default/$(FSTYPE)
SYMETC2SBIN=		$(ETC2SBIN:%=$(ROOTETC)/%)
SYMETC2USRSBIN	=	$(ETC2USRSBIN:%=$(ROOTETC)/%)
SYMUSRBIN2USRSBIN=	$(USRBIN2USRSBIN:%=$(ROOTBIN)/%)
SYMDEVNM=		$(ROOTUSRSBIN)/devnm

%.xpg4.o := CPPFLAGS += -DXPG4
$(XPG4) := LDLIBS += -lintl -lcmd
$(STATIC) $(SPPROG) volcopy mount umount df fsck :=	LDLIBS += -lintl
$(SPPROG) fsck mount mount.static df ff volcopy :=	LDLIBS += -lcmd

$(ROOTETCMNTTAB) := FILEMODE = 644
$(ROOTETCMNTTAB) := OWNER = root
$(ROOTETCMNTTAB) := GROUP = root
$(ROOTFSTYPE)	 := FILEMODE = 444

# for messaging catalog
#
POFILE= fs.d.po
POFILES1= $(PROG:%=%.po) switchout.po
POFILES2= $(SUBDIR2:%=%/%.po)
POFILES= $(POFILES1) $(POFILES2)
volcopy.po :=   XGETFLAGS += -a -x volcopy.xcl


# build rule for xpg4 objects
%.xpg4.o: %.c
	$(COMPILE.c) -o $@ $<

.KEEP_STATE:

# This is too intense when building the whole world.
# .PARALLEL:	$(SUBDIRS)

all:		$(SUBDIRS) .WAIT all_local 

_msg: $(SUBDIR2) $(POFILES1)
	$(RM) $(POFILE)
	cat $(POFILES) > $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	cp $(POFILE) $(MSGDOMAIN)

all_local:	$(PROG) $(XPG4PROG) $(STATIC) $(SPPROG) $(MNTTAB) $(FSTYPE) 

df df.xpg4 ff volcopy: deffs.o $$(@F).o
		$(LINK.c) -o $@ $@.o deffs.o $(LDLIBS)
		$(POST_PROCESS)

fsck: fsck.o deffs.o preenlib.o
	$(LINK.c) -o $@ fsck.o deffs.o preenlib.o $(LDLIBS) -ldl
	$(POST_PROCESS)

mount mount.static: deffs.o mount.o fslib.o
		$(LINK.c) -o $@ mount.o deffs.o fslib.o $(LDLIBS)
		$(POST_PROCESS)

umount umount.static: umount.o fslib.o
		$(LINK.c) -o $@ umount.o fslib.o $(LDLIBS)
		$(POST_PROCESS)

$(SPPROG):	switchout.o deffs.o
	$(LINK.c) -o $@ switchout.o deffs.o $(LDLIBS)
	$(POST_PROCESS)

install: $(SUBDIRS) .WAIT install_local

install_local:	all_local $(ROOTSBINF) $(ROOTUSRSBINF) $(ROOTUSRSBINCLRI) \
		$(ROOTUSRSBINFF) $(ROOTETCMNTTAB) $(ROOTFSTYPE) $(ROOTXPG4PROG) \
		$(SYMETC2SBIN) $(SYMETC2USRSBIN) $(SYMUSRBIN2USRSBIN) \
		$(SYMDEVNM)

$(ROOTETC)/default/%:%
		$(INS.file)

# Links from /etc to /sbin such as /etc/mount -> ../sbin/mount
$(SYMETC2SBIN):
	-$(RM) $@; $(SYMLINK) ../sbin/$(@F) $@

# Links from /etc to /usr/sbin such as /etc/clri -> ../usr/sbin/clri
$(SYMETC2USRSBIN):
	-$(RM) $@; $(SYMLINK) ../usr/sbin/$(@F) $@

# Links from /usr/bin to /usr/sbin such as /usr/bin/df -> ../sbin/df
$(SYMUSRBIN2USRSBIN):
	-$(RM) $@; $(SYMLINK) ../sbin/$(@F) $@

# Symlink from devnm to df in /usr/sbin
$(SYMDEVNM):
	-$(RM) $@; $(SYMLINK) ./df $@

# Multiple names for switchout (clri, dcopy, fsdb, labelit, mkfs)
$(ROOTUSRSBINCLRI):	$(ROOTUSRSBIN)/clri
	-$(RM) $@; $(SYMLINK) ./clri $@

$(MNTTAB):
	touch $(MNTTAB)

$(FSTYPE):
	@$(RM) $@; $(ECHO) "LOCAL=ufs" >$@

# Multiple names for ff (ncheck)
$(ROOTUSRSBINFF):	$(ROOTUSRSBIN)/ff
	-$(RM) $@; $(SYMLINK) ./ff $@

clean: $(SUBDIRS) .WAIT clean_local

clean_local:

clobber: $(SUBDIRS) .WAIT clobber_local

clobber_local:	clean_local
	$(RM) $(PROG) $(SPPROG) $(MNTTAB) $(FSTYPE) $(CLOBBERFILES)

lint:

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:
