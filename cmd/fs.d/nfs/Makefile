#
#ident	"@(#)Makefile	1.15	93/04/07 SMI"	/* 	*/
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/fs.d/nfs/Makefile
#
#  cmd/fs.d/nfs is the directory of all nfs specific commands
#  whose executable reside in $(INSDIR1) and $(INSDIR2).
#

SUBDIR1=	exportfs nfsd rquotad share unshare \
		lockd statd nfsstat mountd dfshares \
		nfsfind
SUBDIR2=	umount showmount mount dfmounts 
SUBDIRS=	$(SUBDIR1) $(SUBDIR2)

# for messaing catalog files
#
POFILES=        $(SUBDIR2:%=%/%.po)
POFILE=         nfs.po

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint
catalog:=       TARGET= catalog

.KEEP_STATE:

.PARALLEL:	$(SUBDIRS)

all install clean clobber lint: $(SUBDIRS)

catalog: $(SUBDIR2)
	$(RM) $(POFILE)
	cat $(POFILES) > $(POFILE)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
