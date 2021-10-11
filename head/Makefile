#
#ident	"@(#)Makefile	1.66	95/03/01 SMI"
#
# Copyright (c) 1989,1993,1994, by Sun Microsystems, Inc.
# All rights reserved.
#
#
# head/Makefile
#
# include global definitions
include ../Makefile.master

HDRS=	\
aio.h	     alloca.h     ar.h         archives.h   assert.h     cpio.h       \
crypt.h      ctype.h      deflt.h      devmgmt.h    dial.h       dirent.h     \
dlfcn.h      elf.h        errno.h      euc.h        fatal.h      fcntl.h      \
float.h      fmtmsg.h     fnmatch.h    ftw.h        getwidth.h   glob.h       \
grp.h        iconv.h      ieeefp.h     \
langinfo.h   lastlog.h    libelf.h     libgenIO.h   limits.h     linenum.h    \
link.h       listen.h     locale.h     macros.h     malloc.h     memory.h     \
mon.h        monetary.h   mp.h         mqueue.h     nan.h        ndbm.h       \
netconfig.h  netdb.h      netdir.h     nl_types.h   nlist.h      note.h       \
nsswitch.h   nss_common.h nss_dbdefs.h nss_netdir.h pfmt.h       pkgdev.h     \
pkginfo.h    pkglocs.h    pkgstrct.h   pkgtrans.h   poll.h       \
prof.h       pw.h         pwd.h        re_comp.h	\
regex.h      regexp.h     resolv.h     \
rje.h        \
rmmount.h    sac.h        sched.h      search.h     semaphore.h  setjmp.h     \
sgtty.h      shadow.h     siginfo.h    \
signal.h     std.h        stdarg.h     stddef.h     stdio.h      \
stdlib.h     storclass.h  string.h     strings.h    \
stropts.h    sum.h        synch.h      \
sysexits.h   syslog.h     syms.h       table.h      \
tar.h        termio.h     termios.h    pthread.h    thread.h     time.h	\
tiuser.h     \
tzfile.h     \
ucontext.h   ulimit.h     unistd.h     userdefs.h   ustat.h      utime.h      \
utmp.h       utmpx.h      valtools.h   values.h     varargs.h    wait.h       \
wchar.h	     wctype.h     widec.h      wordexp.h    thread_db.h 	      \
proc_service.h

ARPAHDRS= ftp.h	 inet.h		nameser.h	telnet.h	tftp.h

# rpcsvc headers which are just headers (not derived from a .x file)
RPCSVC_SRC_HDRS = \
bootparam.h      dbm.h             nis_db.h          nis_tags.h \
nislib.h         yp_prot.h         ypclnt.h          yppasswd.h \
ypupd.h          rpc_sztypes.h

# rpcsvc headers which are generated from .x files
RPCSVC_GEN_HDRS = \
bootparam_prot.h mount.h           nfs_prot.h \
nis.h            nis_cache.h       nis_callback.h    nlm_prot.h \
rex.h            rquota.h          rstat.h           rusers.h \
rwall.h          spray.h           autofs_prot.h \
nispasswd.h      ufs_prot.h        nfs_acl.h

RPCGENFLAGS = -C -h
rpcsvc/nispasswd.h :=	RPCGENFLAGS += -M

# rpcsvc rpcgen source (.x files)
#
# yp.x is an attempt at codifying what was hand coded in RPCL. 
# Unfortunately it doesn't quite work. (The handcoded stuff isn't
# expressable in RPCL) this is due to the fact that YP was written
# before rpcgen existed.  Hence, yp_prot.h cannot be derived from yp.x
#
# There is no `.h' for nis_object.x because it is included by nis.x and
# the resulting .h is nis.h.

RPCSVCPROTS = \
$(RPCSVC_GEN_HDRS:%.h=%.x)        nis_object.x       yp.x

RPCSVCHDRS= $(RPCSVC_SRC_HDRS) $(RPCSVC_GEN_HDRS)

PROTOHDRS=   dumprestore.h routed.h rwhod.h timed.h

SECURITYHDRS=   ia_switch.h ia_appl.h ia_schemes.h

ROOTHDRS= $(HDRS:%=$(ROOT)/usr/include/%)  \
	$(ARPAHDRS:%=$(ROOT)/usr/include/arpa/%)  \
	$(RPCSVCHDRS:%=$(ROOT)/usr/include/rpcsvc/%)  \
	$(RPCSVCPROTS:%=$(ROOT)/usr/include/rpcsvc/%)  \
	$(PROTOHDRS:%=$(ROOT)/usr/include/protocols/%)  \
	$(SECURITYHDRS:%=$(ROOT)/usr/include/security/%)

DIRS= arpa rpcsvc protocols security
ROOTDIRS= $(DIRS:%=$(ROOT)/usr/include/%)

SED=	sed

# check files really don't exist
#
# should do something with the rpcsvc headers

arpa/%.check:		arpa/%.h
	$(DOT_H_CHECK)

rpcsvc/%.check:		rpcsvc/%.h
	$(DOT_H_CHECK)

rpcsvc/%.check:		rpcsvc/%.x
	$(DOT_X_CHECK)

protocols/%.check:	protocols/%.h
	$(DOT_H_CHECK)

security/%.check:	security/%.h
	$(DOT_H_CHECK)

# Note that the derived headers (rpcgen) are not checked at this time.  These
# need work at the source level and rpcgen itself has a bug which causes a
# cstyle violation.  Furthermore, there seems to be good reasons for the
# generated headers to not pass all of the hdrchk rules.
#
# Add the following to the CHECKHDRS list to activate the .x checks:
#	$(RPCSVCPROTS:%.x=rpcsvc/%.check) \
#
CHECKHDRS= $(HDRS:%.h=%.check)  \
	$(ARPAHDRS:%.h=arpa/%.check)  \
	$(RPCSVC_SRC_HDRS:%.h=rpcsvc/%.check) \
	$(PROTOHDRS:%.h=protocols/%.check) \
	$(SECURITYHDRS:%.h=security/%.check)

# headers which won't quite meet the standards...
#
# assert.h is required by ansi-c to *not* be idempotent (section 4.1.2).
# Hence the trailing guard is not the last thing in the file nor can it
# be without playing silly games.

assert.check	:= HDRCHK_TAIL = | grep -v "end guard wrong" | true

# install rules

$(ROOT)/usr/include/security/%: security/%
	$(INS.file)

$(ROOT)/usr/include/protocols/%: protocols/%
	$(INS.file)

$(ROOT)/usr/include/rpcsvc/%: rpcsvc/%
	$(INS.file)

$(ROOT)/usr/include/arpa/%: arpa/%
	$(INS.file)

$(ROOT)/usr/include/%: %
	$(INS.file)

.KEEP_STATE:

.PARALLEL:	$(ROOTHDRS) $(CHECKHDRS)

install_h: $(ROOTDIRS) .WAIT $(ROOTHDRS)

check:	$(CHECKHDRS)

clean clobber:
	cd rpcsvc ; $(RM) $(RPCSVC_GEN_HDRS)

$(ROOTDIRS):
	$(INS.dir)

rpcsvc/%.h:	rpcsvc/%.x
	$(RPCGEN) $(RPCGENFLAGS) $< -o $@

rpcsvc/nis.h:	rpcsvc/nis.x
	$(RPCGEN) $(RPCGENFLAGS) rpcsvc/nis.x |\
	$(SED) -e '/EDIT_START/,$$ d' > $@
