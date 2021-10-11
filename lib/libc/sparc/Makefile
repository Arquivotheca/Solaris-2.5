#
#pragma ident	"@(#)Makefile	1.105	95/09/12 SMI"
#
# Copyright (c) 1993-1995, by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/libc/sparc/Makefile
#
# This builds all objects needed for libc, including those derived
# from source under ../port.  crt0.o and friends belong to the compiler.
#
LIBRARY= libc.a
LIB_PIC= libc_pic.a
VERS=	 .1
M4=	 m4
CPP=	 /usr/lib/cpp

# objects are grouped by source directory, except for the few objects
# requiring special treatment in the ordering of the library.  These
# few are found in the FIRSTOBJS and LASTOBJS macros.

# local objects
#
STRETS= stret1.o stret2.o stret4.o

CRTOBJS= \
$(STRETS)	_ftou.o		_div.o		multiply.o	_rem.o \
umultiply.o	mul64.o		divrem64.o	hwmuldiv.o

DYNOBJS=

FPOBJS= \
finite.o	fp_data.o	fpgetmask.o	fpgetrnd.o	fpgetsticky.o \
fpsetrnd.o	fpsetsticky.o	isnand.o	isnanf.o \
logb.o		scalb.o 	sigfpe.o \
_Q_add.o	_Q_cmp.o	_Q_cmpe.o	_Q_div.o	_Q_dtoq.o \
_Q_fcc.o	_Q_get_rp_rd.o	_Q_itoq.o	_Q_mul.o	_Q_neg.o \
_Q_qtod.o	_Q_qtoi.o	_Q_qtos.o	_Q_qtou.o	_Q_sqrt.o \
_Q_stoq.o	_Q_sub.o	_Q_utoq.o	_Qfaddsub.o	_Qfcompare.o \
_Qfdiv.o	_Qfmul.o	_Qfpack.o	_Qfunpack.o	_Qfutility.o \
__floatprint.o	__flt_decim.o	__flt_rounds.o	__gconvert.o	__tbl_10_b.o \
__tbl_10_h.o	__tbl_10_s.o	__tbl_2_b.o	__tbl_2_h.o	__tbl_2_s.o \
__tbl_baselg.o	__tbl_fdq.o	__tbl_ntens.o	__tbl_tens.o	__x_power.o \
_base_il.o	_base_il4.o	_base_sup.o	_sprintf_sup.o	_unpack_dble.o \
decimal_bin.o	double_decim.o	econvert.o	file_decim.o	func_decim.o \
gconvert.o	ieee_globals.o	pack_float.o	qeconvert.o \
seconvert.o	string_decim.o	unpack_float.o \
_Q_ulltoq.o	_Q_lltoq.o	_Q_funpackllong.o

GENOBJS= \
_getsp.o      _hidden_dgettext.o	  abs.o         alloca.o      atof.o  \
ecvt.o        getctxt.o     gethrtime.o   ladd.o        lconstants.o  \
ldivide.o     lexp10.o      llog10.o      lmul.o        lshiftl.o  \
lsign.o       lsub.o        ltostr.o      makectxt.o    memcmp.o  \
memcpy.o      memmove.o	    memset.o      setjmp.o \
siginfolst.o  sigsetjmp.o   sparc_data.o  strcmp.o      strcpy.o  \
strlen.o      strncmp.o     strncpy.o     strtod.o      swapctxt.o \
_xregs_clrptr.o

GENOBJS_R= \
errno.o       sh_locks.o    tsdalloc.o

LWP_MULK= \
_lwp_mutex_unlock.o

SYSOBJS= \
__clock_timer.o               __sigrt.o \
_nfssys.o      _rename.o      _sigaction.o  \
access.o       acl.o          facl.o  \
acct.o         adjtime.o      alarm.o        chdir.o        chmod.o  \
chown.o        chroot.o       close.o        creat.o        door.o  \
dup.o	\
execve.o       exit.o         fchdir.o       fchmod.o       fchown.o  \
fcntl.o        fork.o         fork1.o        fpathconf.o    fstat.o \
fstatfs.o      fstatvfs.o     fdsync.o                      getdents.o \
getegid.o      geteuid.o      getgid.o       getgroups.o    getmsg.o  \
getpmsg.o      getppid.o      getrlimit.o    getuid.o       gtty.o  \
ioctl.o        kaio.o         lchown.o       link.o         llseek.o \
lseek.o        lstat.o        memcntl.o      mincore.o      mkdir.o  \
mknod.o        mmap.o         modctl.o       mount.o        mprotect.o \
munmap.o       nice.o         open.o         pathconf.o     pause.o  \
pipe.o         poll.o         priocntlset.o  profil.o  \
pread.o        ptrace.o       pwrite.o \
putmsg.o       putpmsg.o      read.o         readlink.o  \
readv.o        rmdir.o        sbrk.o         seteguid.o  \
setgid.o       setgroups.o    setpgrp.o      setrlimit.o    setsid.o  \
setuid.o       sigaction.o    sigaltstk.o                   sigpending.o  \
sigprocmsk.o   sigsendset.o   sigsuspend.o   stat.o         statfs.o  \
statvfs.o      stime.o        stty.o         symlink.o      sync.o  \
syscall.o      sysconfig.o    sysfs.o        sysinfo.o      syssun.o  \
time.o         times.o        uadmin.o       ucontext.o     ulimit.o  \
umask.o        umount.o       uname.o        unlink.o       ustat.o  \
utime.o        utssys.o       vfork.o        wait.o         waitid.o  \
write.o        writev.o       yield.o \
utimes.o       fchroot.o      vhangup.o      gettimeofday.o getitimer.o \
setitimer.o    syslwp.o	      lock.o	     sigwait.o	    signotifywait.o \
machlwp.o  $(LWP_MULK)

# objects from source under ../port
# the last two objects in PORTGEN come from derived C source
# DYP added for libc routines that are ifdef'd YP.
#
PORTGEN= \
addsev.o \
_ctype.o        _env_data.o	_loc_data.o     _locale.o       _set_tab.o \
_xftw.o         a64l.o          abort.o                         assert.o  \
                                atoi.o          atol.o          bcmp.o	\
bcopy.o		bsearch.o	bzero.o	\
calloc.o                                        catgets.o       catopen.o  \
cfgetispeed.o   cfgetospeed.o   cfree.o         cfsetispeed.o   cfsetospeed.o  \
cftime.o        clock.o         closedir.o \
collate.o \
crypt.o 	csetlen.o \
confstr.o \
ctime.o         ctime_r.o	ctypefcns.o     div.o		drand48.o  \
dup2.o          env_data.o \
		euclen.o        execvp.o        fattach.o       fdetach.o  \
ffs.o           fmtmsg.o        frexp.o         ftime.o		ftok.o     \
ftw.o           gcvt.o          getcwd.o        getdate.o       getdate_err.o  \
getdtblsize.o	\
getenv.o        getgrnam.o      gethostid.o	gethostname.o	gethz.o \
getlogin.o  \
getmntent.o     getnetgrent.o   getopt.o        getpagesize.o	getpw.o \
getpwnam.o	getrusage.o \
getspent.o      getsubopt.o     gettxt.o        getusershell.o \
getwidth.o \
getut.o         getutx.o        getvfsent.o     getwd.o		\
getxby_door.o	gtxt.o \
hsearch.o       iconv.o		import_def.o    index.o		\
initgroups.o	insque.o        isastream.o     isatty.o        killpg.o \
l64a.o  \
lckpwdf.o       ldexp.o  	lfind.o \
lfmt.o		lfmt_log.o \
libcollate.o \
loadtab.o \
localeconv.o    lsearch.o  \
madvise.o       malloc.o        mbstowcs.o      mbtowc.o        memalign.o \
memccpy.o       memchr.o                                        \
mkdev.o         mkfifo.o        mktemp.o        mkstemp.o       mlock.o \
mlockall.o  \
modf.o          modff.o         mon.o           msync.o         munlock.o  \
munlockall.o    ndbm.o          nftw.o          nl_langinfo.o   \
opendir.o       opt_data.o      perror.o   \
pfmt.o		pfmt_data.o	pfmt_print.o \
plock.o		priocntl.o      psiginfo.o 	putspent.o \
psignal.o       pt.o            putenv.o        putpwent.o      qsort.o  \
raise.o         rand.o          random.o        readdir.o       realpath.o \
reboot.o	regex.o	\
rename.o        rewinddir.o     rindex.o	scrwidth.o	seekdir.o \
select.o	setlabel.o \
setlocale.o	setpriority.o	setreid.o \
sigflag.o	siglist.o	signal.o \
sigsend.o       sigsetops.o     sleep.o         ssignal.o  \
str2id.o        str2sig.o       strcat.o        strchr.o        settimeofday.o \
strcspn.o       strdup.o        strerror.o      strftime.o  \
stricmp.o       strncat.o                                       strpbrk.o  \
strrchr.o       strsignal.o     strspn.o        strstr.o        strtok.o  \
strtol.o        strtoul.o       strxfrm.o       strxfrm.xpg4.o \
swab.o          swapctl.o  \
sysconf.o       syslog.o        tcdrain.o       tcflow.o        tcflush.o  \
tcgetattr.o     tcgetpgrp.o     tcgetsid.o      tcsendbreak.o   tcsetattr.o  \
tcsetpgrp.o     tell.o          telldir.o       tfind.o         time_comm.o  \
time_data.o     time_gdata.o    tolower.o       toupper.o       truncate.o  \
tsearch.o       ttyname.o       ttyslot.o       ualarm.o	usleep.o \
valloc.o        waitpid.o  \
vlfmt.o		vpfmt.o \
wait3.o		wait4.o \
wcstombs.o      wctomb.o \
errlst.o        new_list.o      \
nss_common.o    nss_dbdefs.o	nss_deffinder.o	nsparse.o	\
wisprint.o      xpg4.o		xgetwidth.o     wdata.o         yp_stubs.o  \
atoll.o		lltostr.o	_ftoull.o \
_ftoll.o	llabs.o		lldiv.o		strtoll.o \
strtoull.o

PORTGEN_R= \
getgrnam_r.o	getpwnam_r.o	getspent_r.o	\
rand_r.o        readdir_r.o

# GENTXTS = portable source unused because of sparc-specific source, see TXTS
GENTXTS= \
abs.c     atof.c    ecvt.c     memcmp.c   memcpy.c  memmove.c	memset.c  \
strcmp.c  strcpy.c  strlen.c  strncmp.c  strncpy.c  strtod.c

PORTPRINT= \
doprnt.o dostrfmon.o \
fprintf.o  printf.o  sprintf.o strfmon.o \
vfprintf.o  vprintf.o  vsprintf.o \
snprintf.o vsnprintf.o

PORTSTDIO= \
_filbuf.o   _findbuf.o  _flsbuf.o   _wrtchk.o   clearerr.o  \
ctermid.o   cuserid.o   data.o      doscan.o    fdopen.o    feof.o  \
ferror.o    fgetc.o     fgets.o     fileno.o    flush.o     fopen.o  \
fpos.o      fputc.o     fputs.o     fread.o     fseek.o     ftell.o  \
fwrite.o    getc.o      getchar.o   getpass.o   gets.o      getw.o  \
popen.o     putc.o      putchar.o   puts.o      putw.o  \
rewind.o    scanf.o     setbuffer.o \
setbuf.o    setvbuf.o   system.o    tempnam.o  \
tmpfile.o   tmpnam.o    ungetc.o

PORTSTDIO_R= ctermid_r.o   flockf.o    tmpnam_r.o

STUBOBJS= \
cond_broadcast.o cond_destroy.o \
cond_init.o     cond_signal.o   cond_timedwait.o \
cond_wait.o     mutex_destroy.o mutex_held.o    mutex_init.o    mutex_lock.o \
mutex_trylock.o mutex_unlock.o  pthr_stub.o	rw_rdlock.o \
rw_tryrdlock.o	rw_trywrlock.o  rw_unlock.o     rw_wrlock.o \
rwlock_init.o	rw_read_held.o  rw_write_held.o sema_held.o \
sema_init.o     sema_post.o     sema_trywait.o  sema_wait.o \
thr_continue.o  thr_create.o    thr_exit.o      thr_getconcurrency.o \
thr_getprio.o   thr_getspecific.o  thr_init.o   thr_join.o   \
thr_keycreate.o thr_kill.o      thr_main.o      thr_self.o   \
thr_setconcurrency.o thr_setprio.o thr_setspecific.o thr_sigsetmask.o \
thr_suspend.o   thr_yield.o     thr_errnop.o	thr_stksegment.o \
thr_min_stack.o

PORTSYS= \
execl.o     execle.o  execv.o   fsync.o  lockf.o           msgsys.o  \
nfssys.o    p_online.o  processor_bind.o    processor_info.o  semsys.o  \
shmsys.o    lwp.o       lwp_cond.o	posix_sigwait.o

PORTREGEX= \
regcomp.o	regexec.o	regerror.o	regdosba.o	regdosub.o \
glob.o		fnmatch.o	wordexp.o	fexecve.o	m_cclass.o \
m_mbstow.o	m_wcstom.o	mbrtowc.o	regcol.o	iswctype.o \
wschr.o		wslen.o		trwctype.o

# XXX - Note: the lists of FIRSTOBJS and LASTOBJS are taken directly
#	from the original AT&T source makefile. This ordering might
#	not have made any sense for a 3b2 and it is even less likely
#	to make sense for SunOS/sparc. The lists are maintained (for
#	now) to maintain the mechanism. One should also consider that
#	the ordering which might optimize search time for a static
#	archive probably has no relationship to the ordering which
#	will minimize memory usage for a dynamic library.
FIRSTOBJS= atexit.o
REENTRANTOBJS= $(PORTGEN_R) $(PORTSTDIO_R) $(GENOBJS_R) $(STUBOBJS)
MOSTOBJS= $(CRTOBJS) $(PORTREGEX) $(DYNOBJS) $(GENOBJS) $(FPOBJS) $(SYSOBJS) \
	$(PORTGEN) $(PORTPRINT) $(PORTSTDIO) $(PORTSYS) \
	 $(REENTRANTOBJS)
LASTOBJS= fpsetmask.o kill.o getpid.o cerror.o cerror64.o cuexit.o

OBJECTS= $(FIRSTOBJS) $(MOSTOBJS) $(LASTOBJS)


# libc build rules
objs/%.o profs/%.o pics/%.o: crt/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: crt/%.s
	$(BUILD.s)
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: gen/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: gen/%.s
	$(BUILD.s)
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: fp/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: fp/%.s
	$(BUILD.s)
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: sys/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: sys/%.s
	$(BUILD.s)
	$(POST_PROCESS_O)

#
# libc build rules for objects built from "portable" source in ../port
objs/%.o profs/%.o pics/%.o %.o: ../port/gen/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: ../port/print/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

# regex rules.  Note: remove -Dwslen, etc when libw is integrated into libc
objs/%.o profs/%.o pics/%.o: ../port/regex/%.c
	$(COMPILE.c) -DM_I18N_MB -DI18N -Diswupper=__iswupper -Dwslen=__wslen \
	-Dtowlower=__towlower -Dtowupper=__towupper -Diswlower=__iswlower \
	-Diswdigit=__iswdigit -Diswalpha=__iswalpha -Dwschr=__wschr \
	-Dwctype=___wctype -Diswctype=___iswctype -I../port/regex \
	-I../port/gen -o $@ $<
	$(POST_PROCESS_O)


objs/%.o profs/%.o pics/%.o: ../port/stdio/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: ../port/stubs/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: ../port/sys/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

# include common library definitions
include ../../Makefile.lib

CPPFLAGS  = -D_REENTRANT -Dsparc -Iinc -I../inc $(RTLDINC) $(CPPFLAGS.master)
ASDEFS    = -D__STDC__ -D_ASM
$(PROFS) := ASDEFS += -DPROF
ASDEFS   += $(CPPFLAGS)
ASFLAGS   = -P $(ASDEFS)
DYNFLAGS += -e __rtboot -Wl,-f/usr/platform/\$$PLATFORM/lib/$(DYNLIBPSR)
LDLIBS   += -ldl

BUILD.s=  $(AS) $(ASFLAGS) $< -o $@

# libc method of building an archive, using AT&T ordering
BUILD.AR= $(RM) $@ ; \
	$(AR) q $@ $(FIRSTOBJS:%=$(DIR)/%) && \
	$(AR) q $@ `$(LORDER) $(MOSTOBJS:%=$(DIR)/%)| $(TSORT)` && \
	$(AR) q $@ $(LASTOBJS:%=$(DIR)/%)

# extra files for the clean target
CLEANFILES= \
assym.s crt/_div.s crt/_rem.s crt/_rtld.s	\
../port/gen/errlst.c ../port/gen/new_list.c

# the TXTS macro is used for NSE bootstrapping.
TXTS=	inc/SYS.h inc/PIC.h $(GENTXTS:%=../port/gen/%)

# list of C source for lint
SRCS= \
gen/atof.c        gen/getctxt.c     gen/ecvt.c	\
gen/lconstants.c  gen/ldivide.c     gen/lexp10.c      gen/llog10.c  \
gen/lmul.c        gen/ltostr.c      gen/makectxt.c    \
gen/siginfolst.c  gen/sigsetjmp.c   gen/strtod.c  \
gen/swapctxt.c  \
sys/sigaction.c  sys/_signal.c  \
$(PORTGEN:%.o=../port/gen/%.c)      $(PORTPRINT:%.o=../port/print/%.c)  \
$(PORTSTDIO:%.o=../port/stdio/%.c)  $(PORTSYS:%.o=../port/sys/%.c)  \
$(PORTREGEX:%.o=../port/regex/%.c) \
../port/gen/atexit.c

# conditional assignments
$(DYNLIB) $(LIB_PIC):= DYNOBJS = _rtbootld.o
pics/%.o:=  ASFLAGS += -K pic

# Files in fp subdirectory which need base.il inline template
IL=	_base_sup.o	decimal_bin.o	double_decim.o	__floatprint.o \
	__gconvert.o	__tbl_fdq.o	__x_power.o
$(IL:%=objs/%) $(IL:%=pics/%) $(IL:%=profs/%) := CFLAGS += fp/base.il

objs/mul64.o pics/mul64.o profs/mul64.o := CFLAGS += crt/mul64.il

.KEEP_STATE:

all: $(TXTS) $(LIBS) $(PLIB) $(LIB_PIC)

$(LINTLIB):= SRCS=../port/llib-lc
$(LINTLIB):= LINTFLAGS=-nvx
$(LINTLIB):= TARGET_ARCH=

# object files that depend on inline template.
$(IL:%=objs/%) $(IL:%=pics/%) $(IL:%=profs/%): fp/base.il
objs/mul64.o pics/mul64.o profs/mul64.o: crt/mul64.il

# include common library targets
include ../../Makefile.targ

$(LIB_PIC) := DIR = pics
$(LIB_PIC): pics $$(PICS)
	$(BUILD.AR)
	$(POST_PROCESS_A)


# special cases
$(STRETS:%=objs/%) \
$(STRETS:%=profs/%) \
$(STRETS:%=pics/%): crt/stret.s
	$(AS) $(ASFLAGS) -DSTRET$(@F:stret%.o=%) crt/stret.s -o $@
	$(POST_PROCESS_O)

crt/_div.s:	crt/divrem.m4.s
	$(M4) -DANSWER=quotient crt/divrem.m4.s > $@

crt/_rem.s:	crt/divrem.m4.s
	$(M4) -DANSWER=remainder crt/divrem.m4.s > $@

crt/_rtbootld.s:	crt/_rtboot.s crt/_rtld.c
	$(CC) $(CPPFLAGS) -O -S crt/_rtld.c -o crt/_rtld.s
	$(CAT) crt/_rtboot.s crt/_rtld.s > $@
	$(RM) crt/_rtld.s

$(LWP_MULK:%=objs/%) \
$(LWP_MULK:%=profs/%) \
$(LWP_MULK:%=pics/%): assym.s

assym.s: genassym
	./genassym > $@

# XXX	A hack.  Perhaps this should be 'CPPFLAGS.native' and
#	live in Makefile.master

CPPFLAGS.genassym = \
$(ENVCPPFLAGS1) $(ENVCPPFLAGS2) $(ENVCPPFLAGS3) $(ENVCPPFLAGS4)

genassym: genassym.c
	$(NATIVECC) -Dsparc -Iinc $(CPPFLAGS.genassym) -o $@ genassym.c

# derived C source and related explicit dependencies
#
../port/gen/errlst.c + \
../port/gen/new_list.c: ../port/gen/errlist ../port/gen/errlist.awk
	cd ../port/gen; pwd; $(AWK) -f errlist.awk < errlist

objs/errlst.o \
pics/errlst.o \
profs/errlst.o: ../port/gen/errlst.c

objs/new_list.o \
pics/new_list.o \
profs/new_list.o: ../port/gen/new_list.c
