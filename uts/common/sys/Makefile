#
#ident	"@(#)Makefile	1.101	95/03/02 SMI"
#
# Copyright (c) 1989-1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# uts/common/sys/Makefile
#	This makefile installs all common system header files.
#
# include global definitions
include ../../../Makefile.master

#
#	Note that the following headers are present in the kernel but
#	neither installed or shipped as part of the product:
#		unix_bb_info.h:		Private interface to kcov
#

HDRS=	\
acl.h            acct.h           aio.h            aio_impl.h	\
aio_req.h        ascii.h	\
attr.h           autoconf.h       auxv.h			\
bitmap.h         buf.h            bufmod.h	\
bustypes.h       byteorder.h      callb.h          callo.h	\
cdio.h           cdump.h          class.h          cmn_err.h	\
condvar.h        conf.h           consdev.h        core.h	\
cpuvar.h         cred.h           crtctl.h         ddi.h	\
ddi_impldefs.h   ddi_implfuncs.h  ddidmareq.h	\
ddimapreq.h      ddipropdefs.h    dditypes.h	\
debug.h          dedump.h         des.h            devops.h	\
dirent.h         disp.h           dki_lkinfo.h     dki_lock.h	\
dl.h             dlpi.h           door.h           door_data.h	\
dnlc.h           elf.h	\
elf_386.h        elf_M32.h        elf_SPARC.h      elf_notes.h	\
elftypes.h       err.h	\
errno.h          ertyp.h          esunddi.h        ethernet.h	\
euc.h            eucioctl.h       exec.h           exechdr.h	\
fault.h         \
fbio.h           fbuf.h           fcntl.h          fdio.h	\
feature_tests.h  file.h           filio.h          flock.h	\
flock_impl.h     fsid.h           fstyp.h          gprof.h	\
hwconf.h         ia.h             iapriocntl.h	\
instance.h       ioccom.h         ioctl.h          ioreq.h	\
ipc.h            ipd_extern.h     ipd_ioctl.h      ipd_sys.h	\
isa_defs.h       jioctl.h	\
kbd.h            kbdreg.h         kbio.h           klwp.h	\
kmem.h           kmem_impl.h      kobj.h           kobj_impl.h	\
kstat.h          kstr.h           ksyms.h          ksynch.h	\
kvtopdata.h      ldterm.h         lihdr.h          link.h	\
llc1.h		\
lock.h           lockfs.h         locking.h	\
log.h            logindmux.h      lwp.h            map.h	\
mem.h            memlist.h	\
mkdev.h          mman.h           mntent.h         mnttab.h	\
modctl.h         mode.h           mount.h          msacct.h	\
msg.h            msgbuf.h         msio.h           mtio.h	\
mutex.h		\
netconfig.h      nexusdefs.h      note.h           open.h	\
openpromio.h     param.h          pathconf.h	   pci.h	\
pathname.h       pcmcia.h         pctypes.h        pem.h        \
pfmod.h          pirec.h          poll.h	\
ppp_chap.h       ppp_diag.h       ppp_extern.h     ppp_ioctl.h  \
ppp_lqm.h        ppp_pap.h        ppp_sys.h                     \
priocntl.h       prioinherit.h	\
proc.h           processor.h      procset.h        proctl.h	\
protosw.h        prsystm.h        ptem.h	\
ptms.h           ptrace.h         ptyvar.h         reboot.h	\
resource.h       rlioctl.h        rt.h             rtpriocntl.h	\
rwlock.h         sad.h            seg_drv.h        select.h	\
sem.h		 semaphore.h      ser_sync.h       session.h    \
shm.h            siginfo.h        signal.h         sleepq.h	\
sobject.h        socket.h	\
socketvar.h      sockio.h         sockmod.h        sservice.h   \
statfs.h	\
statvfs.h        stermio.h        stream.h         strlog.h	\
strmdep.h        stropts.h        strredir.h       strstat.h	\
strsubr.h        strsun.h         strtty.h         sunddi.h	\
suntty.h         swap.h           synch.h          syscall.h	\
sysconf.h        sysconfig.h      sysinfo.h        syslog.h	\
sysmacros.h      systeminfo.h     systm.h          t_kuser.h	\
t_lock.h         tblock.h         telioctl.h       termio.h        \
termios.h	\
termiox.h        thread.h         ticlts.h         ticots.h	\
ticotsord.h      tihdr.h          time.h           timeb.h	\
timer.h          times.h          timod.h          tirdwr.h	\
tiuser.h         ts.h             tspriocntl.h     ttcompat.h	\
ttold.h          tty.h            ttychars.h       ttydev.h	\
tuneable.h       turnstile.h      types.h          uadmin.h	\
uio.h            ulimit.h         un.h             unistd.h	\
user.h           ustat.h          utime.h	\
utsname.h        utssys.h         var.h            varargs.h	\
visual_io.h      vfs.h            vfstab.h         vjcomp.h 	\
vm.h             vmmac.h          vmmeter.h        vmsystm.h	\
vnode.h          vol.h            vtrace.h         vuid_event.h	\
vuid_queue.h     vuid_state.h     vuid_store.h     wait.h	\
winlockio.h      dklabel.h        vtoc.h           tl.h		\
tnf_com.h        tnf_writer.h     tnf_probe.h      tnf.h


FSHDRS=	\
autofs.h	autofs_prot.h					\
cachefs_dir.h	cachefs_filegrp.h		cachefs_fs.h	\
cachefs_fscache.h	cachefs_log.h				\
fifonode.h      hsfs_isospec.h  hsfs_node.h     hsfs_rrip.h	\
hsfs_spec.h     hsfs_susp.h     lofs_info.h     lofs_node.h	\
namenode.h      pc_dir.h					\
pc_fs.h         pc_label.h      pc_node.h       		\
snode.h         swapnode.h      tmp.h				\
tmpnode.h       ufs_fs.h        ufs_fsdir.h     ufs_inode.h	\
ufs_mount.h     ufs_quota.h     ufs_lockfs.h    ufs_filio.h	\
ufs_trans.h	ufs_acl.h	ufs_panic.h	ufs_prot.h	\
s5_fblk.h	s5_fs.h		s5_fsdir.h	s5_inode.h	\
s5_lockfs.h	s5_mount.h

SCSIHDRS=  \
scsi.h			scsi_address.h		scsi_ctl.h  \
scsi_params.h		scsi_pkt.h		scsi_resource.h  \
scsi_types.h

SCSICONFHDRS=		autoconf.h		device.h

SCSIGENHDRS = \
commands.h	inquiry.h	mode.h		status.h	\
dad_mode.h	message.h	sense.h

SCSIIMPLHDRS= \
commands.h	mode.h		sense.h		status.h	types.h \
inquiry.h	pkt_wrapper.h	services.h	transport.h	uscsi.h

# SVVS headers and other excluded headers
XHDRS=          lo.h    tidg.h  tivc.h  tmux.h  cpr.h

# check files really don't exist

fs/%.check:	fs/%.h
	$(DOT_H_CHECK)

scsi/%.check:           scsi/%.h
	$(DOT_H_CHECK)
 
scsi/conf/%.check:      scsi/conf/%.h
	$(DOT_H_CHECK)
 
scsi/generic/%.check:   scsi/generic/%.h
	$(DOT_H_CHECK)
 
scsi/impl/%.check:      scsi/impl/%.h
	$(DOT_H_CHECK)


CHECKHDRS=	$(HDRS:%.h=%.check) \
		$(FSHDRS:%.h=fs/%.check) \
		$(XHDRS:%.h=%.check) \
		$(SCSIHDRS:%.h=scsi/%.check) \
		$(SCSICONHDRS:%.h=scsi/conf/%.check) \
		$(SCSIIMPLHDRS:%.h=scsi/impl/%.check)

ROOTDIR=	$(ROOT)/usr/include/sys
ROOTDIRS=	$(ROOTDIR) \
		$(ROOTDIR)/fs \
		$(ROOTDIR)/scsi \
		$(ROOTDIR)/scsi/conf \
		$(ROOTDIR)/scsi/generic \
		$(ROOTDIR)/scsi/impl
 
ROOTHDRS= $(HDRS:%=$(ROOTDIR)/%)
ROOTFSHDRS= $(FSHDRS:%=$(ROOTDIR)/fs/%)
ROOTSCSIHDRS= $(SCSIHDRS:%=$(ROOTDIR)/scsi/%)
ROOTSCSICONFHDRS= $(SCSICONFHDRS:%=$(ROOTDIR)/scsi/conf/%)
ROOTSCSIGENHDRS= $(SCSIGENHDRS:%=$(ROOTDIR)/scsi/generic/%)
ROOTSCSIIMPLHDRS= $(SCSIIMPLHDRS:%=$(ROOTDIR)/scsi/impl/%)
ROOTXHDRS= $(XHDRS:%=$(ROOTDIR)/%)

ROOTSCSIHDRS= $(SCSIHDRS:%=$(ROOTDIR)/scsi/%)
ROOTSCSICONFHDRS= $(SCSICONFHDRS:%=$(ROOTDIR)/scsi/conf/%)
ROOTSCSIGENHDRS= $(SCSIGENHDRS:%=$(ROOTDIR)/scsi/generic/%)
ROOTSCSIIMPLHDRS= $(SCSIIMPLHDRS:%=$(ROOTDIR)/scsi/impl/%)

# install rules
$(ROOTDIR)/%: %
	$(INS.file)

$(ROOTDIR)/fs/%: fs/%
	$(INS.file)

$(ROOTDIR)/scsi/%: scsi/%
	$(INS.file)
 
$(ROOTDIR)/scsi/conf/%: scsi/conf/%
	$(INS.file)
 
$(ROOTDIR)/scsi/generic/%: scsi/generic/%
	$(INS.file)
 
$(ROOTDIR)/scsi/impl/%: scsi/impl/%
	$(INS.file)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS) $(ROOTHDRS) $(ROOTFSHDRS) $(ROOTSCSIHDRS) \
		$(ROOTSCSICONFHDRS) $(ROOTSCSIGENHDRS) $(ROOTSCSIIMPLHDRS) 

install_h: $(ROOTDIRS) .WAIT $(ROOTHDRS) $(ROOTFSHDRS) $(ROOTSCSIHDRS) \
		$(ROOTSCSICONFHDRS) $(ROOTSCSIGENHDRS) $(ROOTSCSIIMPLHDRS) \
		svvs_h

svvs_h: FRC
	@if [ -f lo.h  ]; then \
		$(MAKE) install_X_h; \
	else \
		true; \
	fi

install_X_h: $(ROOTXHDRS)

$(ROOTDIRS):
	$(INS.dir)

# Special rules for several PPP headers for which the source is shared by the
# UNBUNDLED 4.x product and the BUNDLED 5.x product.  Once the 4.x product is
# uninterseting, this special case should be removed.

$(ROOTDIR)/ppp_extern.h:	ppp_extern.h
	$(RM) $@
	unifdef -U_SunOS4 ppp_extern.h > $@
	$(CH)chmod $(FILEMODE) $@
	$(CH)$(CHGRP) $(GROUP) $@
	$(CH)$(CHOWN) $(OWNER) $@

$(ROOTDIR)/ppp_ioctl.h:		ppp_ioctl.h
	$(RM) $@
	unifdef -U_SunOS4 ppp_ioctl.h > $@
	$(CH)chmod $(FILEMODE) $@
	$(CH)$(CHGRP) $(GROUP) $@
	$(CH)$(CHOWN) $(OWNER) $@

$(ROOTDIR)/ppp_sys.h:		ppp_sys.h
	$(RM) $@
	unifdef -U_SunOS4 ppp_sys.h > $@
	$(CH)chmod $(FILEMODE) $@
	$(CH)$(CHGRP) $(GROUP) $@
	$(CH)$(CHOWN) $(OWNER) $@

check:	$(CHECKHDRS)

FRC:
