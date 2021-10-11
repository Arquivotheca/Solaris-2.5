#
# devices/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.21	95/07/17 SMI"
#
#	This makefile populates the minimal set of configuration
#	independent special nodes and links required to boot an
#	initial system. Configuration specific devices (such as
#	disks) are not created.
#

#
#	Include common rules.
#
include $(SRC)/Makefile.master

#
#	Define the module and object file sets.
#
MKNOD		= mknod

FILEMODE	= 644
OWNER		= root
GROUP		= sys

ROOTDEV		= $(ROOT)/dev
ROOTDEVSAD	= $(ROOTDEV)/sad

DEVICES		= devices
ROOTDEVICES	= $(ROOT)/$(DEVICES)
DEVPSEUDO	= $(DEVICES)/pseudo
ROOTPSEUDO	= $(ROOT)/$(DEVPSEUDO)

#
#	If not root, only create the directories.
#
PSEUDO_DEVS	=
$(CH)PSEUDO_DEVS= pseudo_devices
DEV_LINKS	=
$(CH)DEV_LINKS	= dev_links

#
#	Nothing to do for all, clean, or clobber; nothing to track.
#
.KEEP_STATE:

all clean clobber:

#
#	Default build targets.
#
$(ROOTDEVICES) $(ROOTDEV):
	$(INS.dir)

$(ROOTPSEUDO):	$(ROOTDEVICES)
	$(INS.dir)

$(ROOTDEVSAD):	$(ROOTDEV)
	$(INS.dir)

#
#	Creat installation directories and (if ROOT) create the device
#	nodes and links.
#
install: $(ROOTPSEUDO) $(ROOTDEV) $(ROOTDEVSAD) $(PSEUDO_DEVS) $(DEV_LINKS)

#
# I don't know a better way to do this because of the way make deals with
# `:' character, in names, escaped or otherwise...
#
# I consider it a bug that the entries for le and ie are in this list.
#
# le and ie, although clone, streams drivers reflect the physical hardware
# configuration of the machine.
#
# NOTE: Changes to this file should reflect in the prototype files of SUNWcsd
#	package.
#
pseudo_devices: $(MACH)_pseudo_devices
	@-$(RM) $(ROOTPSEUDO)/cn@0:console
	$(MKNOD) $(ROOTPSEUDO)/cn@0:console c 0 0
	$(CHMOD) 0620 $(ROOTPSEUDO)/cn@0:console
	$(CHOWN) root $(ROOTPSEUDO)/cn@0:console
	$(CHGRP) tty $(ROOTPSEUDO)/cn@0:console
	@-$(RM) $(ROOTPSEUDO)/cn@0:syscon
	$(MKNOD) $(ROOTPSEUDO)/cn@0:syscon c 0 0
	$(CHMOD) 0620 $(ROOTPSEUDO)/cn@0:syscon
	$(CHOWN) root $(ROOTPSEUDO)/cn@0:syscon
	$(CHGRP) tty $(ROOTPSEUDO)/cn@0:syscon
	@-$(RM) $(ROOTPSEUDO)/cn@0:systty
	$(MKNOD) $(ROOTPSEUDO)/cn@0:systty c 0 0
	$(CHMOD) 0620 $(ROOTPSEUDO)/cn@0:systty
	$(CHOWN) root $(ROOTPSEUDO)/cn@0:systty
	$(CHGRP) tty $(ROOTPSEUDO)/cn@0:systty
	@-$(RM) $(ROOTPSEUDO)/clone@0:ip
	$(MKNOD) $(ROOTPSEUDO)/clone@0:ip c 11 3
	$(CHMOD) 0660 $(ROOTPSEUDO)/clone@0:ip
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:ip
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:ip
	@-$(RM) $(ROOTPSEUDO)/clone@0:udp
	$(MKNOD) $(ROOTPSEUDO)/clone@0:udp c 11 41
	$(CHMOD) 0666 $(ROOTPSEUDO)/clone@0:udp
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:udp
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:udp
	@-$(RM) $(ROOTPSEUDO)/ksyms@0:ksyms
	$(MKNOD) $(ROOTPSEUDO)/ksyms@0:ksyms c 72 0
	$(CHMOD) 0666 $(ROOTPSEUDO)/ksyms@0:ksyms
	$(CHOWN) root $(ROOTPSEUDO)/ksyms@0:ksyms
	$(CHGRP) sys $(ROOTPSEUDO)/ksyms@0:ksyms
	@-$(RM) $(ROOTPSEUDO)/log@0:conslog
	$(MKNOD) $(ROOTPSEUDO)/log@0:conslog c 21 0
	$(CHMOD) 0666 $(ROOTPSEUDO)/log@0:conslog
	$(CHOWN) root $(ROOTPSEUDO)/log@0:conslog
	$(CHGRP) sys $(ROOTPSEUDO)/log@0:conslog
	@-$(RM) $(ROOTPSEUDO)/log@0:log
	$(MKNOD) $(ROOTPSEUDO)/log@0:log c 21 5
	$(CHMOD) 0640 $(ROOTPSEUDO)/log@0:log
	$(CHOWN) root $(ROOTPSEUDO)/log@0:log
	$(CHGRP) sys $(ROOTPSEUDO)/log@0:log
	@-$(RM) $(ROOTPSEUDO)/mm@0:kmem
	$(MKNOD) $(ROOTPSEUDO)/mm@0:kmem c 13 1
	$(CHMOD) 0640 $(ROOTPSEUDO)/mm@0:kmem
	$(CHOWN) root $(ROOTPSEUDO)/mm@0:kmem
	$(CHGRP) sys $(ROOTPSEUDO)/mm@0:kmem
	@-$(RM) $(ROOTPSEUDO)/mm@0:mem
	$(MKNOD) $(ROOTPSEUDO)/mm@0:mem c 13 0
	$(CHMOD) 0640 $(ROOTPSEUDO)/mm@0:mem
	$(CHOWN) root $(ROOTPSEUDO)/mm@0:mem
	$(CHGRP) sys $(ROOTPSEUDO)/mm@0:mem
	@-$(RM) $(ROOTPSEUDO)/mm@0:null
	$(MKNOD) $(ROOTPSEUDO)/mm@0:null c 13 2
	$(CHMOD) 0666 $(ROOTPSEUDO)/mm@0:null
	$(CHOWN) root $(ROOTPSEUDO)/mm@0:null
	$(CHGRP) sys $(ROOTPSEUDO)/mm@0:null
	@-$(RM) $(ROOTPSEUDO)/mm@0:zero
	$(MKNOD) $(ROOTPSEUDO)/mm@0:zero c 13 12
	$(CHMOD) 0666 $(ROOTPSEUDO)/mm@0:zero
	$(CHOWN) root $(ROOTPSEUDO)/mm@0:zero
	$(CHGRP) sys $(ROOTPSEUDO)/mm@0:zero
	@-$(RM) $(ROOTPSEUDO)/sad@0:admin
	$(MKNOD) $(ROOTPSEUDO)/sad@0:admin c 12 1
	$(CHMOD) 0600 $(ROOTPSEUDO)/sad@0:admin
	$(CHOWN) root $(ROOTPSEUDO)/sad@0:admin
	$(CHGRP) sys $(ROOTPSEUDO)/sad@0:admin
	@-$(RM) $(ROOTPSEUDO)/sad@0:user
	$(MKNOD) $(ROOTPSEUDO)/sad@0:user c 12 0
	$(CHMOD) 0666 $(ROOTPSEUDO)/sad@0:user
	$(CHOWN) root $(ROOTPSEUDO)/sad@0:user
	$(CHGRP) sys $(ROOTPSEUDO)/sad@0:user
	@-$(RM) $(ROOTPSEUDO)/sy@0:tty
	$(MKNOD) $(ROOTPSEUDO)/sy@0:tty c 22 0
	$(CHMOD) 0666 $(ROOTPSEUDO)/sy@0:tty
	$(CHOWN) root $(ROOTPSEUDO)/sy@0:tty
	$(CHGRP) tty $(ROOTPSEUDO)/sy@0:tty
	@-$(RM) $(ROOTPSEUDO)/wc@0:wscons
	$(MKNOD) $(ROOTPSEUDO)/wc@0:wscons c 15 0
	$(CHMOD) 0600 $(ROOTPSEUDO)/wc@0:wscons
	$(CHOWN) root $(ROOTPSEUDO)/wc@0:wscons
	$(CHGRP) sys $(ROOTPSEUDO)/wc@0:wscons

sparc_pseudo_devices:
	@-$(RM) $(ROOTPSEUDO)/clone@0:ie
	$(MKNOD) $(ROOTPSEUDO)/clone@0:ie c 11 63
	$(CHMOD) 0600 $(ROOTPSEUDO)/clone@0:ie
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:ie
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:ie
	@-$(RM) $(ROOTPSEUDO)/clone@0:le
	$(MKNOD) $(ROOTPSEUDO)/clone@0:le c 11 40
	$(CHMOD) 0600 $(ROOTPSEUDO)/clone@0:le
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:le
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:le
	@-$(RM) $(ROOTPSEUDO)/clone@0:qe
	$(MKNOD) $(ROOTPSEUDO)/clone@0:qe c 11 104
	$(CHMOD) 0600 $(ROOTPSEUDO)/clone@0:qe
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:qe
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:qe
	@-$(RM) $(ROOTPSEUDO)/clone@0:be
	$(MKNOD) $(ROOTPSEUDO)/clone@0:be c 11 106
	$(CHMOD) 0600 $(ROOTPSEUDO)/clone@0:be
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:be
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:be
	@-$(RM) $(ROOTPSEUDO)/clone@0:hme
	$(MKNOD) $(ROOTPSEUDO)/clone@0:hme c 11 7
	$(CHMOD) 0600 $(ROOTPSEUDO)/clone@0:hme
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:hme
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:hme
	@-$(RM) $(ROOTPSEUDO)/openeepr@0:openprom
	$(MKNOD) $(ROOTPSEUDO)/openeepr@0:openprom c 38 0
	$(CHMOD) 0640 $(ROOTPSEUDO)/openeepr@0:openprom
	$(CHOWN) root $(ROOTPSEUDO)/openeepr@0:openprom
	$(CHGRP) sys $(ROOTPSEUDO)/openeepr@0:openprom

i386_pseudo_devices:
	@-$(RM) $(ROOTPSEUDO)/clone@0:smc
	$(MKNOD) $(ROOTPSEUDO)/clone@0:smc c 11 105
	$(CHMOD) 0600 $(ROOTPSEUDO)/clone@0:smc
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:smc
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:smc
	@-$(RM) $(ROOTPSEUDO)/clone@0:el
	$(MKNOD) $(ROOTPSEUDO)/clone@0:el c 11 119
	$(CHMOD) 0600 $(ROOTPSEUDO)/clone@0:el
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:el
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:el
	@-$(RM) $(ROOTPSEUDO)/clone@0:elx
	$(MKNOD) $(ROOTPSEUDO)/clone@0:elx c 11 109
	$(CHMOD) 0600 $(ROOTPSEUDO)/clone@0:elx
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:elx
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:elx
	@-$(RM) $(ROOTPSEUDO)/clone@0:tr
	$(MKNOD) $(ROOTPSEUDO)/clone@0:tr c 11 107
	$(CHMOD) 0600 $(ROOTPSEUDO)/clone@0:tr
	$(CHOWN) root $(ROOTPSEUDO)/clone@0:tr
	$(CHGRP) sys $(ROOTPSEUDO)/clone@0:tr

dev_links: $(MACH)_dev_links
	@-$(RM) $(ROOTDEV)/console
	$(SYMLINK) ../$(DEVPSEUDO)/cn@0:console $(ROOTDEV)/console;
	@-$(RM) $(ROOTDEV)/syscon
	$(SYMLINK) ../$(DEVPSEUDO)/cn@0:syscon $(ROOTDEV)/syscon;
	@-$(RM) $(ROOTDEV)/systty
	$(SYMLINK) ../$(DEVPSEUDO)/cn@0:systty $(ROOTDEV)/systty;
	@-$(RM) $(ROOTDEV)/ip
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:ip $(ROOTDEV)/ip
	@-$(RM) $(ROOTDEV)/udp
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:udp $(ROOTDEV)/udp
	@-$(RM) $(ROOTDEV)/ksyms
	$(SYMLINK) ../$(DEVPSEUDO)/ksyms@0:ksyms $(ROOTDEV)/ksyms
	@-$(RM) $(ROOTDEV)/conslog
	$(SYMLINK) ../$(DEVPSEUDO)/log@0:conslog $(ROOTDEV)/conslog
	@-$(RM) $(ROOTDEV)/log
	$(SYMLINK) ../$(DEVPSEUDO)/log@0:log $(ROOTDEV)/log
	@-$(RM) $(ROOTDEV)/kmem
	$(SYMLINK) ../$(DEVPSEUDO)/mm@0:kmem $(ROOTDEV)/kmem;
	@-$(RM) $(ROOTDEV)/mem
	$(SYMLINK) ../$(DEVPSEUDO)/mm@0:mem $(ROOTDEV)/mem;
	@-$(RM) $(ROOTDEV)/null
	$(SYMLINK) ../$(DEVPSEUDO)/mm@0:null $(ROOTDEV)/null;
	@-$(RM) $(ROOTDEV)/zero
	$(SYMLINK) ../$(DEVPSEUDO)/mm@0:zero $(ROOTDEV)/zero;
	@-$(RM) $(ROOTDEVSAD)/admin
	$(SYMLINK) ../../$(DEVPSEUDO)/sad@0:admin $(ROOTDEVSAD)/admin;
	@-$(RM) $(ROOTDEVSAD)/user
	$(SYMLINK) ../../$(DEVPSEUDO)/sad@0:user $(ROOTDEVSAD)/user;
	@-$(RM) $(ROOTDEV)/tty
	$(SYMLINK) ../$(DEVPSEUDO)/sy@0:tty $(ROOTDEV)/tty;

sparc_dev_links:
	@-$(RM) $(ROOTDEV)/be
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:be $(ROOTDEV)/be
	@-$(RM) $(ROOTDEV)/ie
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:ie $(ROOTDEV)/ie
	@-$(RM) $(ROOTDEV)/le
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:le $(ROOTDEV)/le
	@-$(RM) $(ROOTDEV)/qe
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:qe $(ROOTDEV)/qe
	@-$(RM) $(ROOTDEV)/hme
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:hme $(ROOTDEV)/hme
	@-$(RM) $(ROOTDEV)/openprom
	$(SYMLINK) ../$(DEVPSEUDO)/openeepr@0:openprom $(ROOTDEV)/openprom;

i386_dev_links:
	@-$(RM) $(ROOTDEV)/smc
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:smc $(ROOTDEV)/smc
	@-$(RM) $(ROOTDEV)/spx
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:sp $(ROOTDEV)/spx
	@-$(RM) $(ROOTDEV)/el
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:el $(ROOTDEV)/el
	@-$(RM) $(ROOTDEV)/elx
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:elx $(ROOTDEV)/elx
	@-$(RM) $(ROOTDEV)/tr
	$(SYMLINK) ../$(DEVPSEUDO)/clone@0:tr $(ROOTDEV)/tr
