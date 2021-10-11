/*
 * @(#)audit_event.c 2.29 92/03/04 SMI; SunOS CMW
 * @(#)audit_event.c 4.2.1.2 91/05/08 SMI; SunOS BSM
 *
 * This file contains the audit event table used to control the production
 * of audit records for each system call.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/systeminfo.h>	/* for sysinfo auditing */
#include <sys/utsname.h>	/* for sysinfo auditing */
#include <kerberos/krb.h>	/* for sysinfo auditing */
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/modctl.h>		/* for modctl auditing */
#include <sys/vnode.h>
#include <sys/user.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/file.h>		/* for accept */
#include <sys/ptrace.h>		/* for ptrace */
#include <sys/pathname.h>	/* for symlink */
#include <sys/uio.h>		/* for symlink */
#include <sys/utssys.h>		/* for fuser */
#include <c2/audit.h>
#include <c2/audit_kernel.h>
#include <c2/audit_kevents.h>
#include <c2/audit_record.h>
#include <sys/procset.h>
#include <nfs/mount.h>
#include <sys/param.h>

#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <sys/socket.h>
#include <netinet/in.h>


extern kmutex_t  au_stat_lock;
static au_event_t	aui_null(au_event_t);
static au_event_t	aui_open(au_event_t);
static au_event_t	aui_msgsys(au_event_t);
static au_event_t	aui_shmsys(au_event_t);
static au_event_t	aui_semsys(au_event_t);
static au_event_t	aui_utssys(au_event_t);
static au_event_t	aui_fcntl(au_event_t);
static au_event_t	aui_execv(au_event_t);
static au_event_t	aui_execve(au_event_t);
static au_event_t	aui_memcntl(au_event_t);
static au_event_t	aui_auditsys(au_event_t);
static au_event_t	aui_modctl(au_event_t);
#if 0
static au_event_t	aui_rfssys(au_event_t);
static au_event_t	aui_sysinfo(au_event_t);
#endif

static void	aus_null(struct t_audit_data *);
static void	aus_acct(struct t_audit_data *);
static void	aus_chown(struct t_audit_data *);
static void	aus_fchown(struct t_audit_data *);
static void	aus_lchown(struct t_audit_data *);
static void	aus_chmod(struct t_audit_data *);
static void	aus_fchmod(struct t_audit_data *);
static void	aus_fcntl(struct t_audit_data *);
static void	aus_mkdir(struct t_audit_data *);
static void	aus_mknod(struct t_audit_data *);
static void	aus_mount(struct t_audit_data *);
static void	aus_msgsys(struct t_audit_data *);
static void	aus_semsys(struct t_audit_data *);
static void	aus_close(struct t_audit_data *);
static void	aus_fstatfs(struct t_audit_data *);
static void	aus_setgid(struct t_audit_data *);
static void	aus_setuid(struct t_audit_data *);
static void	aus_shmsys(struct t_audit_data *);
static void	aus_symlink(struct t_audit_data *);
static void	aus_ioctl(struct t_audit_data *);
static void	aus_memcntl(struct t_audit_data *);
static void	aus_mmap(struct t_audit_data *);
static void	aus_munmap(struct t_audit_data *);
static void	aus_priocntlsys(struct t_audit_data *);
static void	aus_setegid(struct t_audit_data *);
static void	aus_setgroups(struct t_audit_data *);
static void	aus_seteuid(struct t_audit_data *);
static void	aus_putmsg(struct t_audit_data *);
static void	aus_putpmsg(struct t_audit_data *);
static void	aus_getmsg(struct t_audit_data *);
static void	aus_getpmsg(struct t_audit_data *);
static void	aus_auditsys(struct t_audit_data *);
static void	aus_sysinfo(struct t_audit_data *);
static void	aus_modctl(struct t_audit_data *);
static void	aus_kill(struct t_audit_data *);
#if 0
static void	aus_setregid(struct t_audit_data *);
static void	aus_xmknod(struct t_audit_data *);
static void	aus_setreuid(struct t_audit_data *);
#endif

static void	auf_null(struct t_audit_data *, int, rval_t *);
static void	auf_mknod(struct t_audit_data *, int, rval_t *);
static void	auf_msgsys(struct t_audit_data *, int, rval_t *);
static void	auf_semsys(struct t_audit_data *, int, rval_t *);
static void	auf_shmsys(struct t_audit_data *, int, rval_t *);
static void	auf_symlink(struct t_audit_data *, int, rval_t *);
#if 0
static void	auf_open(struct t_audit_data *, int, rval_t *);
static void	auf_xmknod(struct t_audit_data *, int, rval_t *);
#endif


#ifdef	VPIX
static au_event_t	aui_vpixsys(au_event_t);
#endif	/* VPIX */

/*
 * This table contains mapping information for converting system call numbers
 * to audit event IDs. In several cases it is necessary to map a single system
 * call to several events.
 */

struct audit_s2e audit_s2e[] =
{
/*
----------	---------- 	----------	----------
INITIAL		AUDIT		START		SYSTEM
PROCESSING	EVENT		PROCESSING	CALL
----------	----------	----------	-----------
	FINISH		EVENT
	PROCESSING	CONTROL
----------------------------------------------------------
*/
aui_null,	AUE_NULL,	aus_null,	/* 0 unused (indirect) */
		auf_null,	0,
aui_null,	AUE_EXIT,	aus_null,	/* 1 exit */
		auf_null,	S2E_NPT,
aui_null,	AUE_FORK,	aus_null,	/* 2 fork */
		auf_null,	0,
aui_null,	AUE_NULL,	aus_null,	/* 3 read */
		auf_null,	0,
aui_null,	AUE_NULL,	aus_null,	/* 4 write */
		auf_null,	0,
aui_open,	AUE_OPEN,	aus_null,	/* 5 open */
		auf_null,	S2E_SP,
aui_null,	AUE_CLOSE,	aus_close,	/* 6 close */
		auf_null,	0,
aui_null,	AUE_NULL,	aus_null,	/* 7 wait */
		auf_null,	0,
aui_null,	AUE_CREAT,	aus_null,	/* 8 create */
		auf_null,	S2E_SP,
aui_null,	AUE_LINK,	aus_null,	/* 9 link */
		auf_null,	0,
aui_null,	AUE_UNLINK,	aus_null,	/* 10 unlink */
		auf_null,	0,
aui_execv,	AUE_EXEC,   aus_null,    /* 11 exec */
		auf_null,   S2E_MLD,
aui_null,	AUE_CHDIR,  aus_null,    /* 12 chdir */
		auf_null,  S2E_SP,
aui_null,	AUE_NULL,   aus_null,    /* 13 time */
		auf_null,   0,
aui_null,	AUE_MKNOD,  aus_mknod,   /* 14 mknod */
		auf_mknod,  0,
aui_null,	AUE_CHMOD,  aus_chmod,   /* 15 chmod */
		auf_null,   0,
aui_null,	AUE_CHOWN,  aus_chown,   /* 16 chown */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 17 brk */
		auf_null,   0,
aui_null,	AUE_STAT,   aus_null,    /* 18 stat */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 19 lseek */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 20 getpid */
		auf_null,   0,
aui_null,	AUE_MOUNT,  aus_mount,    /* 21 mount */
		auf_null,   0,
aui_null,	AUE_UMOUNT, aus_null,    /* 22 umount */
		auf_null,   0,
aui_null,	AUE_SETUID, aus_setuid,  /* 23 setuid */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 24 getuid */
		auf_null,   0,
aui_null,	AUE_STIME,  aus_null,    /* 25 stime */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 26 (was ptrace) */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 27 alarm */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 28 fstat */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 29 pause */
		auf_null,   0,
aui_null,	AUE_UTIME,  aus_null,    /* 30 utime */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 31 stty (TIOCSETP-audit?) */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 32 gtty */
		auf_null,   0,
aui_null,	AUE_ACCESS, aus_null,    /* 33 access */
		auf_null,   0,
aui_null,	AUE_NICE,   aus_null,    /* 34 nice */
		auf_null,   0,
aui_null,	AUE_STATFS, aus_null,    /* 35 statfs */
		auf_null,   0,
aui_null,	AUE_NULL,   aus_null,    /* 36 sync */
		auf_null,   0,
aui_null,	AUE_KILL,   aus_kill,    /* 37 kill */
		auf_null,   0,
aui_null,    AUE_FSTATFS,   aus_fstatfs, /* 38 fstatfs */
		auf_null,   0,
aui_null,    AUE_SETPGRP,   aus_null,    /* 39 setpgrp */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 40 (was cxenix) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 41 dup */
		auf_null,   0,
aui_null,    AUE_PIPE,   aus_null,    /* 42 pipe */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 43 times */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 44 profil */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 45 nosys (was proc lock) */
		auf_null,   0,
aui_null,    AUE_SETGID, aus_setgid, /* 46 setgid */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 47 getgid */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 48 sig */
		auf_null,   0,
aui_msgsys,  AUE_MSGSYS, aus_msgsys,  /* 49 IPC message */
		auf_msgsys, 0,
aui_null,    AUE_NULL,   aus_null,    /* 50 nosys (was sys3b) */
		auf_null,   0,
aui_null,    AUE_ACCT,   aus_acct,    /* 51 acct */
		auf_null,   0,
aui_shmsys,  AUE_SHMSYS, aus_shmsys,  /* 52 shared memory */
		auf_shmsys, 0,
aui_semsys,  AUE_SEMSYS, aus_semsys,  /* 53 IPC semaphores */
		auf_semsys, 0,
aui_null,    AUE_IOCTL,  aus_ioctl,   /* 54 ioctl */
		auf_null,  0,
aui_null,    AUE_NULL,   aus_null,    /* 55 uadmin */
		auf_null,  0,
#ifdef MEGA
aui_null,    AUE_NULL,   aus_null,    /* 56 uexch */
		auf_null,   0,
#else
aui_null,    AUE_NULL,   aus_null,    /* 56 uexch */
		auf_null,   0,
#endif
aui_utssys,  AUE_FUSERS, aus_null,    /* 57 utssys */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 58 fsync */
		auf_null,   0,
aui_execve,  AUE_EXECVE, aus_null,    /* 59 exece */
		auf_null,   S2E_MLD,
aui_null,    AUE_NULL,   aus_null,    /* 60 umask */
		auf_null,   0,
aui_null,    AUE_CHROOT, aus_null,    /* 61 chroot */
		auf_null, S2E_SP,
aui_fcntl,   AUE_FCNTL,  aus_fcntl,   /* 62 fcntl */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 63 ulimit */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 64 nosys */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 65 nosys */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 66 nosys */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 67 nosys (file locking call) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 68 nosys (local system calls) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 69 nosys (inode open) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 70 nosys (was advfs) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 71 nosys (was unadvfs) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 72 nosys (was notused) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 73 nosys (was notused) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 74 nosys (was rfstart) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 75 nosys (was sigret (SunOS)) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 76 nosys (was rdebug) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 77 nosys (was rfstop) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 78 nosys (was rfssys) */
		auf_null,   0,
aui_null,    AUE_RMDIR,  aus_null,    /* 79 rmdir */
		auf_null,   0,
aui_null,    AUE_MKDIR,  aus_mkdir,   /* 80 mkdir */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,   /* 81 getdents */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 82 nosys (was libattach) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 83 nosys (was libdetach) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 84 sysfs */
		auf_null,   0,
aui_null,    AUE_GETMSG, aus_getmsg,    /* 85 getmsg */
		auf_null,   0,
aui_null,    AUE_PUTMSG,   aus_putmsg,    /* 86 putmsg */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 87 poll */
		auf_null,   0,
aui_null,    AUE_LSTAT,  aus_null,    /* 88 lstat */
		auf_null,   0,
aui_null,    AUE_SYMLINK, aus_symlink, /* 89 symlink */
		auf_symlink,   0,
aui_null,    AUE_READLINK, aus_null,   /* 90 readlink */
		auf_null,   0,
aui_null,    AUE_SETGROUPS, aus_setgroups,  /* 91 setgroups */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 92 getgroups */
		auf_null,   0,
aui_null,    AUE_FCHMOD, aus_fchmod,  /* 93 fchmod */
		auf_null,   0,
aui_null,    AUE_FCHOWN, aus_fchown,  /* 94 fchown */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 95 sigprocmask */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 96 sigsuspend */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 97 sigaltstack */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 98 sigaction */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 99 sigpending */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 100 setcontext */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 101 nosys (was evsys) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 102 nosys (was evtrapret) */
		auf_null,   0,
aui_null,    AUE_STATVFS, aus_null,    /* 103 statvfs */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 104 fstatvfs */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 105 nosys */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 106 (was nfssys; now loadable) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 107 waitset */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 108 sigsendset */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 109 nosys (was hrtsys) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 110 nosys (was acancel) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 111 nosys (was async) */
		auf_null,   0,
aui_null,    AUE_PRIOCNTLSYS, aus_priocntlsys, /* 112 priocntlsys */
		auf_null,   0,
aui_null,    AUE_PATHCONF, aus_null,   /* 113 pathconf */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 114 mincore */
		auf_null,   0,
aui_null,    AUE_MMAP,   aus_mmap,    /* 115 mmap */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 116 mprotect */
		auf_null,   0,
aui_null,    AUE_MUNMAP, aus_munmap,    /* 117 munmap */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 118 fpathconf */
		auf_null,   0,
aui_null,    AUE_VFORK,  aus_null,    /* 119 vfork */
		auf_null,   0,
aui_null,    AUE_FCHDIR, aus_null,    /* 120 fchdir */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 121 readv */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 122 writev */
		auf_null,   0,
aui_null,    AUE_XSTAT,  aus_null,    /* 123 xstat (expanded stat) */
		auf_null,   0,
aui_null,    AUE_LXSTAT, aus_null,    /* 124 lxstat (expanded sym stat) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 125 fxstat */
		auf_null,   0,
aui_null,    AUE_XMKNOD, aus_null,    /* 126 xmknod */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 127 nosys (was clocal) */
		auf_null,   0,
aui_null,    AUE_SETRLIMIT, aus_null,  /* 128 setrlimit */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 129 getrlimit */
		auf_null,   0,
aui_null,    AUE_LCHOWN, aus_lchown,  /* 130 lchown */
		auf_null,   0,
aui_memcntl, AUE_MEMCNTL, aus_memcntl, /* 131 memcntl */
		auf_null,   0,
aui_null,    AUE_GETPMSG, aus_getpmsg, /* 132 getpmsg */
		auf_null,   0,
aui_null,    AUE_PUTPMSG, aus_putpmsg, /* 133 putpmsg */
		auf_null,   0,
aui_null,    AUE_RENAME, aus_null,    /* 134 rename */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 135 uname */
		auf_null,   0,
aui_null,    AUE_SETEGID, aus_setegid, /* 136 setegid */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 137 sysconfig */
		auf_null,   0,
aui_null,    AUE_ADJTIME, aus_null,    /* 138 adjtime */
		auf_null,   0,
aui_null,    AUE_SYSINFO, aus_sysinfo, /* 139 systeminfo */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 140 reserved */
		auf_null,   0,
aui_null,    AUE_SETEUID, aus_seteuid,    /* 141 seteuid */
		auf_null,   0,
#ifdef TRACE
aui_null,    AUE_VTRACE, aus_null,    /* 142 vtrace */
		auf_null,   0,
#else  TRACE
aui_null,    AUE_NULL,   aus_null,    /* 142 vtrace */
		auf_null,   0,
#endif TRACE
aui_null,    AUE_FORK1,  aus_null,    /* 143 fork1 */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 144 sigwait */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 145 lwp_info */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 146 yield */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 147 = lwp_sema_p */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 148 lwp_sema_v */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 149 nosys (reserved) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 150 nosys (reserved) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 151 nosys (reserved) */
		auf_null,   0,
aui_modctl,  AUE_MODCTL, aus_modctl,  /* 152 modctl */
		auf_null, 0,
aui_null,    AUE_FCHROOT, aus_null,    /* 153 fchroot */
		auf_null,   0,
aui_null,    AUE_UTIMES, aus_null,    /* 154 utimes */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 155 vhangup */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 156 gettimeofday */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 157 getitimer */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 158 setitimer */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 159 lwp_create */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 160 lwp_exit */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 161 lwp_stop */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 162 lwp_continue */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 163 lwp_kill */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 164 lwp_get_id */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 165 lwp_setprivate */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 166 lwp_getprivate */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 167 lwp_wait */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 168 lwp_mutex_unlock  */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 169 lwp_mutex_lock */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 170 lwp_cond_wait */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 171 lwp_cond_signal */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 172 lwp_cond_broadcast */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 173 pread */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 174 pwrite */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 175 llseek */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 176 nosys (loadable syscalls) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 177 nosys (loadable syscalls) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 178 nosys (loadable syscalls) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 179 nosys (loadable syscalls) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 180 nosys (loadable syscalls) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 181 nosys (loadable syscalls) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 182 nosys (loadable syscalls) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 183 nosys (loadable syscalls) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 184 nosys (loadable syscalls) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 185 nosys (loadable syscalls) */
		auf_null,   0,
aui_auditsys, AUE_AUDITSYS, aus_auditsys,    /* 186 auditsys  */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 187 processor_bind */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 188 processor_info */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 189 p_online */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 190 sigqueue */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 191 clock_gettime */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 192 clock_settime */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 193 clock_getres */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 194 timer_create */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 195 timer_delete */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 196 timer_settime */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 197 timer_gettime */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 198 timer_getoverrun */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 199 nanosleep */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 200 nosys (future expansion) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 201 nosys (future expansion) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 202 nosys (future expansion) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 203 nosys (future expansion) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 204 nosys (future expansion) */
		auf_null,   0,
aui_null,    AUE_NULL,   aus_null,    /* 205 nosys (future expansion) */
		auf_null,   0,
};

u_int num_syscall = sizeof (audit_s2e) / sizeof (struct audit_s2e);


/* null start function */
/*ARGSUSED*/
static void
aus_null(tad)
	struct t_audit_data *tad;
{
}

/* acct start function */
/*ARGSUSED*/
static void
aus_acct(tad)
	struct t_audit_data *tad;
{
	register struct a {
		char *fname;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	if (uap->fname == NULL)
		au_uwrite(au_to_arg(1, "accounting off", (u_long)0));
}

/* chown start function */
/*ARGSUSED*/
static void
aus_chown(tad)
	struct t_audit_data *tad;
{
	register struct a {
		char	*fname;
		int	uid;
		int	gid;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(2, "new file uid", (u_long)uap->uid));
	au_uwrite(au_to_arg(3, "new file gid", (u_long)uap->gid));
}

/* fchown start function */
/*ARGSUSED*/
static void
aus_fchown(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int fd;
		int uid;
		int gid;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	struct file  *fp;
	struct vnode *vp;
	struct f_audit_data *fad;

	au_uwrite(au_to_arg(2, "new file uid", (u_long)uap->uid));
	au_uwrite(au_to_arg(3, "new file gid", (u_long)uap->gid));

		/*
		 * convert file pointer to file descriptor
		 *   Note: fd ref count incremented here.
		 */
	if ((fp = GETF(uap->fd)) == NULL)
		return;

		/* get path from file struct here */
	fad = (struct f_audit_data *) F2A(fp);
	if (fad->fad_lpbuf) {
		au_uwrite(au_to_path(fad->fad_path, fad->fad_pathlen));
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
			/* include attributes */
		vp = fp->f_vnode;
		audit_attributes(vp);
	} else {
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
		au_uwrite(au_to_arg(1, "no path: fd", (u_long)(uap->fd)));
	}
}

/*ARGSUSED*/
static void
aus_lchown(tad)
	struct t_audit_data *tad;
{
	register struct a {
		char	*fname;
		int	uid;
		int	gid;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(2, "new file uid", (u_long)uap->uid));
	au_uwrite(au_to_arg(3, "new file gid", (u_long)uap->gid));
}

/* chmod start function */
/*ARGSUSED*/
static void
aus_chmod(tad)
	struct t_audit_data *tad;
{
	register struct a {
		char	*fname;
		int	fmode;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(2, "new file mode", (u_long)(uap->fmode&07777)));
}

/* chmod start function */
/*ARGSUSED*/
static void
aus_fchmod(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int fd;
		int fmode;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	struct file  *fp;
	struct vnode *vp;
	struct f_audit_data *fad;

	au_uwrite(au_to_arg(2, "new file mode", (u_long)(uap->fmode&07777)));

		/*
		 * convert file pointer to file descriptor
		 *   Note: fd ref count incremented here.
		 */
	if ((fp = GETF(uap->fd)) == NULL)
		return;

		/* get path from file struct here */
	fad = (struct f_audit_data *) F2A(fp);
	if (fad->fad_lpbuf) {
		au_uwrite(au_to_path(fad->fad_path, fad->fad_pathlen));
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
			/* include attributes */
		vp = fp->f_vnode;
		audit_attributes(vp);
	} else {
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
		au_uwrite(au_to_arg(1, "no path: fd", (u_long)(uap->fd)));
	}
}


/* null function */
/*ARGSUSED*/
static void
auf_null(tad, error, rval)
	struct t_audit_data *tad;
	int	error;
	rval_t	*rval;
{
}

/* null function */
static au_event_t
aui_null(au_event_t e)
{
	return (e);
}

/* convert open to appropriate event */
static au_event_t
aui_open(au_event_t e)
{
	register int fm;
	register struct a {
		char *fnamep;
		int fmode;
		int cmode;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	fm = uap->fmode;

	if (fm & O_WRONLY)
		e = AUE_OPEN_W;
	else if (fm & O_RDWR)
		e = AUE_OPEN_RW;
	else
		e = AUE_OPEN_R;

	if (fm & O_CREAT)
		e += 1;
	if (fm & O_TRUNC)
		e += 2;

	return (e);
}

/* msgsys */
static au_event_t
aui_msgsys(au_event_t e)
{
	register int fm;

	register struct a {
		unsigned	id;	/* function code id */
		int		*ap;	/* arg pointer for recvmsg */
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	register struct b {
		int	msgid,
			cmd;
		struct msqid_ds *buf;
	} *uap1 = (struct b *) &ttolwp(curthread)->lwp_ap[1];

	fm = uap->id;
	switch (fm) {
	case 0:		/* msgget */
		e = AUE_MSGGET;
		break;
	case 1:		/* msgctl */
		fm = uap1->cmd;
		switch (fm) {
		case IPC_RMID:
			e = AUE_MSGCTL_RMID;
			break;
		case IPC_SET:
			e = AUE_MSGCTL_SET;
			break;
		case IPC_STAT:
			e = AUE_MSGCTL_STAT;
			break;
		default:
			e = AUE_MSGCTL;
			break;
		}
		break;
	case 2:		/* msgrcv */
		e = AUE_MSGRCV;
		break;
	case 3:		/* msgsnd */
		e = AUE_MSGSND;
		break;
	default:	/* illegal system call */
		e = AUE_NULL;
		break;
	}

	return (e);
}


/* shmsys */
static au_event_t
aui_shmsys(au_event_t e)
{
	register int fm;

	struct a {		/* shmsys */
		unsigned id;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	struct b {		/* ctrl */
		int	shmid;
		int	cmd;
		struct shmid_ds *arg;
	} *uap1 = (struct b *) &ttolwp(curthread)->lwp_ap[1];

	fm = uap->id;
	switch (fm) {
	case 0:		/* shmat */
		e = AUE_SHMAT;
		break;
	case 1:		/* shmctl */
		fm = uap1->cmd;
		switch (fm) {
		case IPC_RMID:
			e = AUE_SHMCTL_RMID;
			break;
		case IPC_SET:
			e = AUE_SHMCTL_SET;
			break;
		case IPC_STAT:
			e = AUE_SHMCTL_STAT;
			break;
		default:
			e = AUE_SHMCTL;
			break;
		}
		break;
	case 2:		/* shmdt */
		e = AUE_SHMDT;
		break;
	case 3:		/* shmget */
		e = AUE_SHMGET;
		break;
	default:	/* illegal system call */
		e = AUE_NULL;
		break;
	}

	return (e);
}


/* semsys */
static au_event_t
aui_semsys(au_event_t e)
{
	register int fm;
	struct a {		/* semsys */
		unsigned id;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	struct b {		/* ctrl */
		int	semid;
		uint	semnum;
		uint	cmd;
		int	arg;
	} *uap1 = (struct b *) &ttolwp(curthread)->lwp_ap[1];

	fm = uap->id;
	switch (fm) {
	case 0:		/* semctl */
		fm = uap1->cmd;
		switch (fm) {
		case IPC_RMID:
			e = AUE_SEMCTL_RMID;
			break;
		case IPC_SET:
			e = AUE_SEMCTL_SET;
			break;
		case IPC_STAT:
			e = AUE_SEMCTL_STAT;
			break;
		case GETNCNT:
			e = AUE_SEMCTL_GETNCNT;
			break;
		case GETPID:
			e = AUE_SEMCTL_GETPID;
			break;
		case GETVAL:
			e = AUE_SEMCTL_GETVAL;
			break;
		case GETALL:
			e = AUE_SEMCTL_GETALL;
			break;
		case GETZCNT:
			e = AUE_SEMCTL_GETZCNT;
			break;
		case SETVAL:
			e = AUE_SEMCTL_SETVAL;
			break;
		case SETALL:
			e = AUE_SEMCTL_SETALL;
			break;
		default:
			e = AUE_SEMCTL;
			break;
		}
		break;
	case 1:		/* semget */
		e = AUE_SEMGET;
		break;
	case 2:		/* semop */
		e = AUE_SEMOP;
		break;
	default:	/* illegal system call */
		e = AUE_NULL;
		break;
	}

	return (e);
}

/* utssys - uname(2), ustat(2), fusers(2) */
static au_event_t
aui_utssys(au_event_t e)
{
	struct a {
		union {
			char *cbuf;
			struct stat *ubuf;
		} ub;
		union {
			int mv;		/* for USTAT */
			int flags;	/* for FUSERS */
		} un;
		int type;
		char *outbp;		/* for FUSERS */
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	if (uap->type == UTS_FUSERS)
		return (e);
	else
		return ((au_event_t) AUE_NULL);
}

static au_event_t
aui_fcntl(au_event_t e)
{
	register struct a {
		int	fdes;
		int	cmd;
		int	arg;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	switch (uap->cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
	case F_RGETLK:
	case F_RSETLK:
	case F_RSETLKW:
		break;
	case F_SETFL:
	case F_GETFL:
	case F_GETFD:
		break;
	default:
		e = (au_event_t) AUE_NULL;
		break;
	}
	return ((au_event_t) e);
}

/* null function for now */
static au_event_t
aui_execv(au_event_t e)
{
	return (e);
}

#ifdef NOTYET
static au_event_t
aui_cmwsys(au_event_t e)
{
	register struct a {
		int	code;
		int	a1;
		int	a2;
		int	a3;
		int	a4;
		int	a5;
		int	a6;
		int	a7;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	e = AUE_NULL;

	switch (uap->code) {
	case BSM_GETAUID:
		e = AUE_GETAUID;
		break;
	case BSM_SETAUID:
		e = AUE_SETAUID;
		break;
	case BSM_GETAUDIT:
		e = AUE_GETAUDIT;
		break;
	case BSM_SETAUDIT:
		e = AUE_SETAUDIT;
		break;
	case BSM_GETUSERAUDIT:
		e = AUE_GETUSERAUDIT;
		break;
	case BSM_SETUSERAUDIT:
		e = AUE_SETUSERAUDIT;
		break;
	case BSM_AUDITSVC:
		e = AUE_AUDITSVC;
		break;
	case BSM_AUDITUSER:
		e = AUE_AUDITUSER;
		break;
	case BSM_AUDITON:
		e = AUE_AUDITON;
		break;
	case BSM_AUDITCTL:
		switch (uap->a1) {
		case A_GETTERMID:
			e = AUE_AUDITON_GTERMID;
			break;
		case A_SETTERMID:
			e = AUE_AUDITON_STERMID;
			break;
		case A_GETPOLICY:
			e = AUE_AUDITON_GPOLICY;
			break;
		case A_SETPOLICY:
			e = AUE_AUDITON_SPOLICY;
			break;
		case A_GETKMASK:
			e = AUE_AUDITON_GESTATE;
			break;
		case A_SETKMASK:
			e = AUE_AUDITON_SESTATE;
			break;
		case A_GETQCTRL:
			e = AUE_AUDITON_GQCTRL;
			break;
		case A_SETQCTRL:
			e = AUE_AUDITON_SQCTRL;
			break;
		}
		break;
	case BSM_GETKERNSTATE:
		e = AUE_GETKERNSTATE;
		break;
	case BSM_SETKERNSTATE:
		e = AUE_SETKERNSTATE;
		break;
	case BSM_GETPORTAUDIT:
		e = AUE_GETPORTAUDIT;
		break;
	case BSM_AUDITSTAT:
		e = AUE_AUDITSTAT;
		break;
	default:
		return (AUE_NULL);
	}

	return (e);
}
#endif

/* null function for now */
static au_event_t
aui_execve(au_event_t e)
{
	return (e);
}

/*ARGSUSED*/
static void
aus_fcntl(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int	fd;
		int	cmd;
		int	arg;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	struct file  *fp;
	struct vnode *vp;
	struct f_audit_data *fad;

	au_uwrite(au_to_arg(2, "cmd", (u_long)uap->cmd));

		/*
		 * convert file pointer to file descriptor
		 *   Note: fd ref count incremented here.
		 */
	if ((fp = GETF(uap->fd)) == NULL)
		return;

	/* get path from file struct here */
	fad = (struct f_audit_data *) F2A(fp);
	if (fad->fad_lpbuf) {
		au_uwrite(au_to_path(fad->fad_path, fad->fad_pathlen));
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
			/* include attributes */
		vp = fp->f_vnode;
		audit_attributes(vp);
	} else {
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
		au_uwrite(au_to_arg(1, "no path: fd", (u_long)(uap->fd)));
	}

}

#ifdef NOTYET
/*ARGSUSED*/
static void
aus_bsmsys(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int	code;
		int	a1;
		int	a2;
		int	a3;
		int	a4;
		int	a5;
		int	a6;
		int	a7;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	struct file *fp;
	register struct f_audit_data *fad;
	struct vnode *vp;
	register int  fd;
	long policy;

	switch (tad->tad_event) {
	case AUE_SETAUID:
	case AUE_SETUSERAUDIT:
		au_uwrite(au_to_arg(1, "auditID", (u_long) uap->a1));
		break;
	case AUE_AUDITSVC:
			/*
			 * convert file pointer to file descriptor
			 *   Note: fd ref count incremented here.
			 */
		if ((fp = GETF(uap->a1)) == NULL)
			return;

			/* get path from file struct here */
		fad = (struct f_audit_data *) F2A(fp);
		if (fad->fad_lpbuf) {
			au_uwrite(au_to_path(fad->fad_path, fad->fad_pathlen));
				/* decrement file descriptor reference count */
			RELEASEF(uap->a1);
				/* include attributes */
			vp = fp->f_vnode;
			audit_attributes(vp);
		} else {
				/* decrement file descriptor reference count */
			RELEASEF(uap->a1);
			au_uwrite(au_to_arg(1, "no path: fd",
				(u_long)(uap->a1)));
		}
		au_uwrite(au_to_arg(2, "limit", (u_long)uap->a2));
		break;
	case AUE_AUDITON:
		au_uwrite(au_to_arg(1, "new audit state", (u_long)uap->a1));
		break;
	case AUE_AUDIT_SPOLICY:	/* use of privilege */
		if (copyin((caddr_t)uap->a2, (caddr_t)&policy, sizeof (policy)))
			return;
		au_uwrite(au_to_arg(1, "policy", (u_long)policy));
		break;
	case AUE_GETAUID:		/* use of privilege */
	case AUE_GETAUDIT:		/* use of privilege */
	case AUE_GETUSERAUDIT:		/* use of privilege */
	case AUE_AUDITON_GTERMID:	/* use of privilege */
	case AUE_AUDITON_GPOLICY:	/* use of privilege */
	case AUE_AUDITON_GESTATE:	/* use of privilege */
	case AUE_AUDITON_GQCTRL:	/* use of privilege */
	case AUE_GETKERNSTATE:		/* use of privilege */
	case AUE_AUDITSTAT:		/* use of privilege */
	case AUE_SETAUDIT:		/* use of privilege */
	case AUE_AUDITON_STERMID:	/* use of privilege */
	case AUE_AUDITON_SESTATE:	/* use of privilege */
	case AUE_AUDITON_SQCTRL:	/* use of privilege */
	case AUE_SETKERNSTATE:		/* use of privilege */
	case AUE_GETPORTAUDIT:		/* use of privilege */
		break;
	}
}
#endif NOTYET

/*ARGSUSED*/
static void
aus_kill(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int	pid;
		int	signo;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	struct proc *p;

	au_uwrite(au_to_arg(2, "signal", (u_long)uap->signo));
	if (uap->pid > 0) {
		mutex_enter(&pidlock);
		if ((p = prfind(uap->pid)) == (struct proc *)0) {
			mutex_exit(&pidlock);
			return;
		}
		mutex_enter(&p->p_lock);	/* so process doesn't go away */
		mutex_exit(&pidlock);
		au_uwrite(au_to_process(p));
#ifdef	SunOS_CMW
		au_uwrite(au_to_slabel(&p->p_cred->cr_slabel));
#endif	/* SunOS_CMW */
		mutex_exit(&p->p_lock);
	}
	else
		au_uwrite(au_to_arg(1, "process", (u_long)uap->pid));
}

/*ARGSUSED*/
static void
aus_mkdir(tad)
	struct t_audit_data *tad;
{
	struct a {
		char	*dirnamep;
		int	dmode;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	au_uwrite(au_to_arg(2, "mode", (u_long)uap->dmode));
}

/*ARGSUSED*/
static void
aus_mknod(tad)
	struct t_audit_data *tad;
{
	struct a {
		char    *pnamep;
		int	fmode;
		int	dev;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	au_uwrite(au_to_arg(2, "mode", (u_long)uap->fmode));
	au_uwrite(au_to_arg(3, "dev", (u_long)uap->dev));
}

/*ARGSUSED*/
static void
auf_mknod(tad, error, rval)
	struct t_audit_data *tad;
	int	error;
	rval_t	*rval;
{
	struct a {
		char    *pnamep;
		int	fmode;
		int	dev;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	char  path[MAXPATHLEN];
	u_int len;
	u_int size;
	char *src;
	char *ptr, *base;
	struct p_audit_data *pad = (struct p_audit_data *)P2A(curproc);

		/* no error, then already path token in audit record */
	if (error != EPERM)
		return;
		/* not auditing this event, nothing then to do */
	if (tad->tad_flag == 0)
		return;
		/* get path string */
	if (copyinstr((caddr_t)uap->pnamep, path, MAXPATHLEN, &len))
		return;
		/* if length 0, then do nothing */
	if (len == 0)
		return;

	/* absolute or relative paths? */
	mutex_enter(&pad->pad_lock);
	if (path[0] == '/') {
		size  = pad->pad_cwrd->cwrd_rootlen;
		src   = pad->pad_cwrd->cwrd_root;
	} else {
		size  = pad->pad_cwrd->cwrd_dirlen;
		src   = pad->pad_cwrd->cwrd_dir;
	}
		/* space for two strings (first null becomes a '/') */
	AS_INC(as_memused, size+len);
	base = ptr = (char *)kmem_alloc((u_int)(size+len), KM_SLEEP);
	bcopy(src, base, size-1);
	mutex_exit(&pad->pad_lock);
	ptr += size-1;
	*ptr++ = '/';
	bcopy(path, ptr, len);
	au_uwrite(au_to_path(base, size+len));
	AS_DEC(as_memused, size+len);
	kmem_free(base, size+len);
}

#if 0
/*ARGSUSED*/
static void
aus_xmknod(tad)
	struct t_audit_data *tad;
{
	struct a {
		int	version;
		char    *fname;
		mode_t	fmode;
		dev_t	dev;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	au_uwrite(au_to_arg(2, "mode", (u_long)uap->fmode));
	au_uwrite(au_to_arg(3, "dev", (u_long)uap->dev));
}
#endif

#if 0
/*ARGSUSED*/
static void
auf_xmknod(tad, error, rval)
	t_audit_data_t *tad;
	int	error;
	rval_t	*rval;
{
	struct a {
		int	version;
		char    *fname;
		mode_t   fmode;
		dev_t    dev;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	char  path[MAXPATHLEN];
	u_int len;
	u_int size;
	char *src;
	char *ptr, *base;
	struct p_audit_data *pad = (p_audit_data_t *) P2A(curproc);

		/* no error, then already path token in audit record */
	if (error != EPERM)
		return;
		/* not auditing this event, nothing then to do */
	if (tad->tad_flag == 0)
		return;
		/* get path string */
	if (copyinstr((caddr_t)uap->fname, path, MAXPATHLEN, &len))
		return;
		/* if length 0, then do nothing */
	if (len == 0)
		return;

		/* absolute or relative paths? */
	mutex_enter(&pad->pad_lock);
	if (path[0] == '/') {
		size  = pad->pad_cwrd->cwrd_rootlen;
		src   = pad->pad_cwrd->cwrd_root;
	} else {
		size  = pad->pad_cwrd->cwrd_dirlen;
		src   = pad->pad_cwrd->cwrd_dir;
	}
		/* space for two strings (first null becomes a '/') */
	AS_INC(as_memused, size+len);
	base = ptr = (char *)kmem_alloc((u_int)(size+len), KM_SLEEP);
	bcopy(src, base, size-1);
	mutex_exit(&pad->pad_lock);
	ptr += size-1;
	*ptr++ = '/';
	bcopy(path, ptr, len);
	au_uwrite(au_to_path(base, size+len));
	AS_DEC(as_memused, size+len);
	kmem_free(base, size+len);
}
#endif

/*ARGSUSED*/
static void
aus_mount(tad)

	struct t_audit_data *tad;

{	/* AUS_START */

	struct a {
		char    *spec;
		char    *dir;
		int	flags;
		char    *fstype;
		char 	*dataptr;
		int	datalen;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	struct a *mounta;
	struct nfs_args nfsargs;
	char hostname[MAXNAMELEN];
	char fstype[MAXNAMELEN];
	size_t len;

	mounta = (struct a *) uap;
	bzero(fstype, MAXNAMELEN);
	if (copyinstr((caddr_t)uap->fstype, (caddr_t) fstype,
		MAXNAMELEN, &len)) {
			return;
	}
	au_uwrite(au_to_arg(3, "flags", (u_long) mounta->flags));
	au_uwrite(au_to_text(fstype));

	if (strncmp(fstype, "nfs", 4) == 0) {
		if (copyin((caddr_t)mounta->dataptr, (caddr_t) &nfsargs,
			sizeof (struct nfs_args))) {
				debug_enter((char *)NULL);
				return;
		}
		bzero(hostname, MAXNAMELEN);
		if (copyinstr((caddr_t) nfsargs.hostname, (caddr_t) hostname,
			MAXNAMELEN, &len)) {
				return;
		}
		au_uwrite(au_to_text(hostname));
		au_uwrite(au_to_arg(3, "internal flags",
			(u_long) nfsargs.flags));
	}

}	/* AUS_MOUNT */


static void
aus_msgsys(tad)
	struct t_audit_data *tad;
{
	struct b {
		int	msgid,
			cmd;
		struct msqid_ds *buf;
	} *uap1 = (struct b *) &ttolwp(curthread)->lwp_ap[1];

	switch (tad->tad_event) {
	case AUE_MSGGET:		/* msgget */
		break;
	case AUE_MSGCTL:		/* msgctl */
	case AUE_MSGCTL_RMID:		/* msgctl */
	case AUE_MSGCTL_STAT:		/* msgctl */
	case AUE_MSGRCV:		/* msgrcv */
	case AUE_MSGSND:		/* msgsnd */
		au_uwrite(au_to_arg(1, "msg ID", (u_long) uap1->msgid));
		break;
	case AUE_MSGCTL_SET:		/* msgctl */
		au_uwrite(au_to_arg(1, "msg ID", (u_long) uap1->msgid));
		break;
	}
}

/*ARGSUSED*/
static void
auf_msgsys(tad, error, rval)
	struct t_audit_data *tad;
	int	error;
	rval_t	*rval;
{
#ifdef NOTYET
	register struct msqid_ds *qp;
	extern struct msginfo msginfo;
	int id;

	if (error)
		return;
	if (tad->tad_event == AUE_MSGGET) {
		id = (int)rval->r_val1;
		qp = &msgque[id % msginfo.msgmni];
		au_uwrite(au_to_ipc(AT_IPC_MSG, id));
		au_uwrite(au_to_ipc_perm(&(qp->msg_perm)));
	}
#endif
}

static void
aus_semsys(tad)
	struct t_audit_data *tad;
{
	struct b {		/* ctrl */
		int	semid;
		uint	semnum;
		uint	cmd;
		int	arg;
	} *uap1 = (struct b *) &ttolwp(curthread)->lwp_ap[1];

	switch (tad->tad_event) {
	case AUE_SEMCTL_RMID:
	case AUE_SEMCTL_STAT:
	case AUE_SEMCTL_GETNCNT:
	case AUE_SEMCTL_GETPID:
	case AUE_SEMCTL_GETVAL:
	case AUE_SEMCTL_GETALL:
	case AUE_SEMCTL_GETZCNT:
	case AUE_SEMCTL_SETVAL:
	case AUE_SEMCTL_SETALL:
	case AUE_SEMCTL:
	case AUE_SEMOP:
		au_uwrite(au_to_arg(1, "sem ID", (u_long)uap1->semid));
		break;
	case AUE_SEMCTL_SET:
		au_uwrite(au_to_arg(1, "sem ID", (u_long)uap1->semid));
		/* put out msqid_ds into audit record */
		break;
	case AUE_SEMGET:
		break;
	}
}

/*ARGSUSED*/
static void
auf_semsys(tad, error, rval)
	struct t_audit_data *tad;
	int	error;
	rval_t	*rval;
{
#ifdef NOTYET
	register struct semid_ds *sp;
	extern struct seminfo seminfo;
	int id;

	if (error)
		return;
	if (tad->tad_event == AUE_SEMGET) {
		id = (int)rval->r_val1;
		sp = &sema[id % seminfo.semmni];
		au_uwrite(au_to_ipc(AT_IPC_SEM, id));
		au_uwrite(au_to_ipc_perm(&(sp->sem_perm)));
	}
#endif
}

/*ARGSUSED*/
static void
aus_close(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int	i;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	register int fd = uap->i;
	struct file *fp;
	struct f_audit_data *fad;
	register struct vnode *vp;
	struct vattr attr;

	attr.va_mask = 0;
	au_uwrite(au_to_arg(1, "fd", (u_long)fd));

		/*
		 * convert file pointer to file descriptor
		 *   Note: fd ref count incremented here.
		 */
	if ((fp = GETF(fd)) == NULL)
		return;

	fad = (struct f_audit_data *) F2A(fp);
	if (fad->fad_lpbuf) {
		au_uwrite(au_to_path(fad->fad_path, fad->fad_pathlen));
			/* decrement file descriptor reference count */
		RELEASEF(fd);
		if ((vp = fp->f_vnode) != NULL) {
			if (VOP_GETATTR(vp, &attr, 0, CRED()) == 0) {
				au_uwrite(au_to_attr(&attr));
			}
		}
	} else {
			/* decrement file descriptor reference count */
		RELEASEF(fd);
	}
}

#if 0
/*ARGSUSED*/
static void
auf_open(tad, error, rval)
	struct t_audit_data *tad;
	int	error;
	rval_t	*rval;
{
	/* system will panic in kmem_alloc if tad info is not cleared */
	tad->tad_pathlen = 0;
	tad->tad_path	= (caddr_t) 0;
	tad->tad_vn	= (struct vnode *) 0;

}
#endif

/*ARGSUSED*/
static void
aus_fstatfs(tad)
	struct t_audit_data *tad;
{
	struct a {
		int fd;
		struct statfs *buf;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	struct file  *fp;
	struct vnode *vp;
	struct f_audit_data *fad;

		/*
		 * convert file pointer to file descriptor
		 *   Note: fd ref count incremented here.
		 */
	if ((fp = GETF(uap->fd)) == NULL)
		return;

		/* get path from file struct here */
	fad = (struct f_audit_data *) F2A(fp);
	if (fad->fad_lpbuf) {
		au_uwrite(au_to_path(fad->fad_path, fad->fad_pathlen));
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
			/* include attributes */
		vp = fp->f_vnode;
		audit_attributes(vp);
	} else {
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
		au_uwrite(au_to_arg(1, "no path: fd", (u_long)(uap->fd)));
	}
}

#ifdef NOTYET
/*ARGSUSED*/
static void
aus_setpgrp(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int	pid;
		int	pgrp;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	register struct proc *p;

		/* current process? */
	if (uap->pid == 0)
		(return);

	mutex_enter(&pidlock);
	p = prfind(uap->pid);
	mutex_enter(&p->p_lock);	/* so process doesn't go away */
	mutex_exit(&pidlock);
	if (p == NULL || p->p_as == &kas) {
		mutex_exit(&p->p_lock);
		return;
	}
	au_uwrite(au_to_process(p));
	mutex_exit(&p->p_lock);
	au_uwrite(au_to_arg(2, "pgrp", (u_long) uap->pgrp));
}
#endif

#if 0
/*ARGSUSED*/
static void
aus_setregid(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int	 rgid;
		int	 egid;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "rgid", (u_long)uap->rgid));
	au_uwrite(au_to_arg(2, "egid", (u_long)uap->egid));
}
#endif

/*ARGSUSED*/
static void
aus_setgid(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int gid;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "gid", (u_long)uap->gid));
}


#if 0
/*ARGSUSED*/
static void
aus_setreuid(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int	ruid;
		int	euid;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "ruid", (u_long)uap->ruid));
	au_uwrite(au_to_arg(2, "euid", (u_long)uap->euid));
}
#endif

/*ARGSUSED*/
static void
aus_setuid(tad)
	struct t_audit_data *tad;
{
	register struct a {
		int	uid;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "uid", (u_long)uap->uid));
}

/*ARGSUSED*/
static void
aus_shmsys(tad)
	struct t_audit_data *tad;
{
	register struct b {
		int	id,
			cmd;
		struct shmid_ds *buf;
	} *uap1 = (struct b *) &ttolwp(curthread)->lwp_ap[1];

	switch (tad->tad_event) {
	case AUE_SHMGET:			/* shmget */
		break;
	case AUE_SHMCTL:			/* shmctl */
	case AUE_SHMCTL_RMID:		/* shmctl */
	case AUE_SHMCTL_STAT:		/* shmctl */
	case AUE_SHMCTL_SET:		/* shmctl */
		au_uwrite(au_to_arg(1, "shmid", (u_long) uap1->id));
		/* put out msqid_ds into audit record */
		break;
	case AUE_SHMDT:		/* shmdt */
		au_uwrite(au_to_arg(1, "shmaddr", (u_long) uap1->id));
		break;
	case AUE_SHMAT:		/* shmat */
		au_uwrite(au_to_arg(1, "shmid", (u_long) uap1->id));
		au_uwrite(au_to_arg(2, "shmaddr", (u_long) uap1->cmd));
		break;
	}
}

/*ARGSUSED*/
static void
auf_shmsys(tad, error, rval)
	struct t_audit_data *tad;
	int error;
	rval_t *rval;
{
#ifdef NOTYET
	register struct shmid_ds *sp;
	extern struct shminfo shminfo;
	int id;

	if (error)
		return;
	if (tad->tad_event == AUE_SHMGET) {
		id = (int)rval->r_val1;
		sp = &shmem[id % shminfo.shmmni];
		au_uwrite(au_to_ipc(AT_IPC_SHM, id));
		au_uwrite(au_to_ipc_perm(&(sp->shm_perm)));
	}
#endif
}

/*ARGSUSED*/
static void
aus_symlink(tad)
	struct t_audit_data *tad;
{
	register struct a {
		char    *target;
		char    *linkname;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	struct pathname tpn;

	if (pn_get(uap->target, UIO_USERSPACE, &tpn))
		return;
	au_uwrite(au_to_text(tpn.pn_path));
	pn_free(&tpn);
	tad->tad_ctrl |= PAD_SAVPATH;
}

/*
 * auf symlink inherits the symlink path from the initial lookuppn
 * it now must redo the lookup to get the new vnode assosiated with
 * the symlink and record the attributes
 */

/*ARGSUSED*/
static void
auf_symlink(tad, error, rval)
	struct t_audit_data *tad;
	int error;
	rval_t *rval;
{
	vnode_t	*vp;
	struct pathname path;
	char *link_path;

	tad->tad_ctrl = PAD_NOPATH;
	if ((!error) && (tad->tad_pathlen)) {
		link_path = kmem_zalloc(MAXPATHLEN, KM_SLEEP);
		bcopy(tad->tad_path, link_path, tad->tad_pathlen);
		path.pn_buf = link_path;
		path.pn_path = link_path;
		path.pn_pathlen = (u_int) tad->tad_pathlen;
		lookuppn(&path, NO_FOLLOW, NULLVPP, &vp);
		if (vp != NULL) {
			audit_attributes(vp);
			VN_RELE(vp);
		}
		pn_free(&path);
	}

	if (tad->tad_pathlen) {
		dprintf(2, ("auf_symlink: %x %x\n",
		tad->tad_pathlen, tad->tad_path));
		call_debug(2);
		AS_DEC(as_memused, tad->tad_pathlen);
		kmem_free(tad->tad_path, tad->tad_pathlen);
		tad->tad_pathlen = 0;
		tad->tad_path = (caddr_t) 0;
		tad->tad_vn = (struct vnode *) 0;
	}
}


/*ARGSUSED*/
static void
aus_ioctl(tad)
	struct t_audit_data *tad;
{
	struct a {
		int fd;
		int cmd;
		caddr_t cmarg;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	struct file *fp;
	struct vnode *vp;
	struct f_audit_data *fad;


		/*
		 * convert file pointer to file descriptor
		 *   Note: fd ref count incremented here.
		 */
	if ((fp = GETF(uap->fd)) == NULL) {
		au_uwrite(au_to_arg(1, "fd", (u_long) uap->fd));
		au_uwrite(au_to_arg(2, "cmd", (u_long) uap->cmd));
		au_uwrite(au_to_arg(3, "arg", (u_long) uap->cmarg));
		return;
	}

	/* get path from file struct here */
	fad = (struct f_audit_data *) F2A(fp);
	if (fad->fad_lpbuf) {
		au_uwrite(au_to_path(fad->fad_path, fad->fad_pathlen));
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
			/* include attributes */
		vp = fp->f_vnode;
		audit_attributes(vp);
	} else {
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
		au_uwrite(au_to_arg(1, "no path: fd", (u_long) (uap->fd)));
	}
	au_uwrite(au_to_arg(2, "cmd", (u_long) uap->cmd));
	au_uwrite(au_to_arg(3, "arg", (u_long) uap->cmarg));
}

#if 0
/* rfs system calls are not being audited at this time */
/*ARGSUSED*/
static au_event_t
aui_rfssys(au_event_t e)
{
	return (AUE_NULL);
}
#endif

/*
 * null function for memcntl for now. We might want to limit memcntl()
 * auditing to commands: MC_LOCKAS, MC_LOCK, MC_UNLOCKAS, MC_UNLOCK which
 * require superuser privileges.
 */
static au_event_t
aui_memcntl(au_event_t e)
{
	return (e);
}

/*ARGSUSED*/
static void
aus_memcntl(tad)
	struct t_audit_data *tad;
{
	struct a {
		caddr_t addr;
		size_t  len;
		int	cmd;
		caddr_t arg;
		int	attr;
		int	mask;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "base", (u_long) uap->addr));
	au_uwrite(au_to_arg(2, "len", (u_long) uap->len));
	au_uwrite(au_to_arg(3, "cmd", (u_long) uap->cmd));
	au_uwrite(au_to_arg(4, "arg", (u_long) uap->arg));
	au_uwrite(au_to_arg(5, "attr", (u_long) uap->attr));
	au_uwrite(au_to_arg(6, "mask", (u_long) uap->mask));
}

/*ARGSUSED*/
static void
aus_mmap(tad)

	struct t_audit_data *tad;

{	/* AUS_MMAP */

	register struct a {
		caddr_t addr;
		int len;
		int prot;
		int flags;
		int fd;
		off_t pos;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;
	struct file *fp;
	register struct f_audit_data *fad;
	register struct vnode *vp;

		/*
		 * convert file pointer to file descriptor
		 *   Note: fd ref count incremented here.
		 */
	if ((fp = GETF(uap->fd)) == NULL)
		return;

	fad = (struct f_audit_data *) F2A(fp);
	if (fad->fad_lpbuf) {
		au_uwrite(au_to_path(fad->fad_path, fad->fad_pathlen));
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
			/* include attributes */
		vp = (struct vnode *)fp->f_vnode;
		audit_attributes(vp);
	} else {
			/* decrement file descriptor reference count */
		RELEASEF(uap->fd);
		au_uwrite(au_to_arg(1, "no path: fd", (u_long) uap->fd));
	}

}	/* AUS_MMAP */




/*ARGSUSED*/
static void
aus_munmap(tad)

	struct t_audit_data *tad;

{	/* AUS_MUNMAP */

	struct a {
		caddr_t addr;
		int len;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "addr", (u_long) uap->addr));
	au_uwrite(au_to_arg(2, "len", (u_long) uap->len));

}	/* AUS_MUNMAP */







/*ARGSUSED*/
static void
aus_priocntlsys(tad)

	struct t_audit_data *tad;

{	/* AUS_PRIOCNTLSYS */

	register struct a {
		int pc_version;
		procset_t *psp;
		int	cmd;
		caddr_t	arg;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "pc_version", (u_long) uap->pc_version));
	au_uwrite(au_to_arg(3, "cmd", (u_long) uap->cmd));

}	/* AUS_PRIOCNTLSYS */


/*ARGSUSED*/
static void
aus_setegid(tad)

	struct t_audit_data *tad;

{	/* AUS_SETEGID */

	register struct a {
		int gid;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "gid", (u_long) uap->gid));


}	/* AUS_SETEGID */




/*ARGSUSED*/
static void
aus_setgroups(tad)

	struct t_audit_data *tad;

{	/* AUS_SETGROUPS */

	register struct a {
		u_int   gidsetsize;
		gid_t   *gidset;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	int i;
	gid_t gidsetsize;
	gid_t gid_list[NGROUPS_MAX_DEFAULT];

	gidsetsize = uap->gidsetsize;
	if ((gidsetsize > NGROUPS_MAX_DEFAULT) || (gidsetsize < 0))
		return;
	if (gidsetsize != 0) {
		if (copyin((caddr_t) uap->gidset, (caddr_t) gid_list,
			gidsetsize * sizeof (gid_t)))
			return;
		for (i = 0; i < gidsetsize; i++)
			au_uwrite(au_to_arg(
				1, "setgroups", (u_long) gid_list[i]));
	} else
		au_uwrite(au_to_arg(1, "setgroups", (u_long) 0));

}	/* AUS_SETGROUPS */





/*ARGSUSED*/
static void
aus_seteuid(tad)

	struct t_audit_data *tad;

{	/* AUS_SETEUID */

	register struct a {
		int uid;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "gid", (u_long) uap->uid));

}	/* AUS_SETEUID */




/*ARGSUSED*/
static void
aus_putmsg(tad)

	struct t_audit_data *tad;

{	/* AUS_PUTMSG */

	register struct a {
		int fdes;
		struct strbuf *ctl;
		struct strbuf *data;
		int pri;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "fd", (u_long) uap->fdes));
	au_uwrite(au_to_arg(4, "pri", (u_long) uap->pri));

}	/* AUS_PUTMSG */





/*ARGSUSED*/
static void
aus_putpmsg(tad)

	struct t_audit_data *tad;

{	/* AUS_PUTPMSG */

	register struct a {
		int fdes;
		struct strbuf *ctl;
		struct strbuf *data;
		int pri;
		int flags;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "fd", (u_long) uap->fdes));
	au_uwrite(au_to_arg(4, "pri", (u_long) uap->pri));
	au_uwrite(au_to_arg(5, "flags", (u_long) uap->flags));

}	/* AUS_PUTPMSG */






/*ARGSUSED*/
static void
aus_getmsg(tad)

	struct t_audit_data *tad;

{	/* AUS_GETMSG */

	register struct a {
		int fdes;
		struct strbuf *ctl;
		struct strbuf *data;
		int pri;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "fd", (u_long) uap->fdes));
	au_uwrite(au_to_arg(4, "pri", (u_long) uap->pri));

}	/* AUS_GETMSG */





/*ARGSUSED*/
static void
aus_getpmsg(tad)

	struct t_audit_data *tad;

{	/* AUS_GETPMSG */

	register struct a {
		int fdes;
		struct strbuf *ctl;
		struct strbuf *data;
		int pri;
		int flags;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	au_uwrite(au_to_arg(1, "fd", (u_long) uap->fdes));

}	/* AUS_GETPMSG */







static au_event_t
aui_auditsys(au_event_t e)
{	/* AUI_AUDITSYS */

	struct a {
		int code;
		int a1;
		int a2;
		int a3;
		int a4;
		int a5;
		int a6;
		int a7;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	switch (uap->code) {

	case BSM_GETAUID:
		e = AUE_GETAUID;
		break;
	case BSM_SETAUID:
		e = AUE_SETAUID;
		break;
	case BSM_GETAUDIT:
		e = AUE_GETAUDIT;
		break;
	case BSM_SETAUDIT:
		e = AUE_SETAUDIT;
		break;
	case BSM_AUDIT:
		e = AUE_AUDIT;
		break;
	case BSM_AUDITSVC:
		e = AUE_AUDITSVC;
		break;
	case BSM_GETPORTAUDIT:
		e = AUE_GETPORTAUDIT;
		break;
	case BSM_AUDITON:
	case BSM_AUDITCTL:

		switch (uap->a1) {

		case A_GETPOLICY:
			e = AUE_AUDITON_GPOLICY;
			break;
		case A_SETPOLICY:
			e = AUE_AUDITON_SPOLICY;
			break;
		case A_GETKMASK:
			e = AUE_AUDITON_GETKMASK;
			break;
		case A_SETKMASK:
			e = AUE_AUDITON_SETKMASK;
			break;
		case A_GETQCTRL:
			e = AUE_AUDITON_GQCTRL;
			break;
		case A_SETQCTRL:
			e = AUE_AUDITON_SQCTRL;
			break;
		case A_GETCWD:
			e = AUE_AUDITON_GETCWD;
			break;
		case A_GETCAR:
			e = AUE_AUDITON_GETCAR;
			break;
		case A_GETSTAT:
			e = AUE_AUDITON_GETSTAT;
			break;
		case A_SETSTAT:
			e = AUE_AUDITON_SETSTAT;
			break;
		case A_SETUMASK:
			e = AUE_AUDITON_SETUMASK;
			break;
		case A_SETSMASK:
			e = AUE_AUDITON_SETSMASK;
			break;
		case A_GETCOND:
			e = AUE_AUDITON_GETCOND;
			break;
		case A_SETCOND:
			e = AUE_AUDITON_SETCOND;
			break;
		case A_GETCLASS:
			e = AUE_AUDITON_GETCLASS;
			break;
		case A_SETCLASS:
			e = AUE_AUDITON_SETCLASS;
			break;
		default:
			e = AUE_NULL;
			break;
		}
		break;
	default:
		e = AUE_NULL;
		break;
	}

	return (e);




}	/* AUI_AUDITSYS */







static void
aus_auditsys(tad)

	struct t_audit_data *tad;

{	/* AUS_AUDITSYS */

	register struct a {
		int code;
		int a1;		/* argument or decode command for auditctl */
		int a2;
		int a3;
		int a4;
		int a5;
		int a6;
		int a7;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	struct file *fp;
	register struct f_audit_data *fad;
	struct vnode *vp;
	long policy;
	struct au_qctrl qctrl;
	auditinfo_t ainfo;
	au_evclass_map_t event;
	au_mask_t mask;
	int auditstate;


	switch (tad->tad_event) {
	case AUE_SETAUID:
		au_uwrite(au_to_arg(2, "setauid", (u_long) uap->a1));
		break;
	case AUE_SETAUDIT:
		if (copyin((caddr_t) uap->a1, (caddr_t) &ainfo,
			sizeof (auditinfo_t))) {
				return;
		}
		au_uwrite(au_to_arg((char) 1, "setaudit:auid",
			(u_long) ainfo.ai_auid));
		au_uwrite(au_to_arg((char) 1, "setaudit:port",
			(u_long) ainfo.ai_termid.port));
		au_uwrite(au_to_arg((char) 1, "setaudit:machine",
			(u_long) ainfo.ai_termid.machine));
		au_uwrite(au_to_arg((char) 1, "setaudit:as_success",
			(u_long) ainfo.ai_mask.as_success));
		au_uwrite(au_to_arg((char) 1, "setaudit:as_failure",
			(u_long) ainfo.ai_mask.as_failure));
		au_uwrite(au_to_arg((char) 1, "setaudit:asid",
			(u_long) ainfo.ai_asid));
		break;
	case AUE_AUDITSVC:
		/*
		 * convert file pointer to file descriptor
		 * Note: fd ref count incremented here
		 */
		if ((fp = GETF(uap->a1)) == NULL)
			return;
		fad = (struct f_audit_data *) F2A(fp);
		if (fad->fad_lpbuf) {
			au_uwrite(au_to_path(fad->fad_path, fad->fad_pathlen));
			/* decrement file descriptor ref count */
			RELEASEF(uap->a1);
			vp = fp->f_vnode;	/* include vnode attributes */
			audit_attributes(vp);
		} else {
			RELEASEF(uap->a1);	/* dec file desc ref cnt */
			au_uwrite(au_to_arg(2, "no path: fd",
				(u_long) (uap->a1)));
		}
		au_uwrite(au_to_arg(3, "limit", (u_long) uap->a2));
		break;
	case AUE_AUDITON_SETKMASK:
		if (copyin((caddr_t) uap->a2, (caddr_t) &mask,
			sizeof (au_mask_t)))
				return;
		au_uwrite(au_to_arg(
			2, "setkmask:as_success", (u_long) mask.as_success));
		au_uwrite(au_to_arg(
			2, "setkmask:as_failure", (u_long) mask.as_failure));
		break;
	case AUE_AUDITON_SPOLICY:
		if (copyin((caddr_t) uap->a2, (caddr_t) &policy, sizeof (int)))
			return;
		au_uwrite(au_to_arg(3, "setpolicy", (u_long) policy));
		break;
	case AUE_AUDITON_SQCTRL:
		if (copyin((caddr_t) uap->a2, (caddr_t) &qctrl,
			sizeof (struct au_qctrl)))
				return;
		au_uwrite(au_to_arg(
			3, "setqctrl:aq_hiwater", (u_long) qctrl.aq_hiwater));
		au_uwrite(au_to_arg(
			3, "setqctrl:aq_lowater", (u_long) qctrl.aq_lowater));
		au_uwrite(au_to_arg(
			3, "setqctrl:aq_bufsz", (u_long) qctrl.aq_bufsz));
		au_uwrite(au_to_arg(
			3, "setqctrl:aq_delay", (u_long) qctrl.aq_delay));
		break;
	case AUE_AUDITON_SETUMASK:
		if (copyin((caddr_t) uap->a2, (caddr_t) &ainfo,
			sizeof (struct auditinfo)))
				return;
		au_uwrite(au_to_arg(3, "setumask:as_success",
			(u_long) ainfo.ai_mask.as_success));
		au_uwrite(au_to_arg(3, "setumask:as_failure",
			(u_long) ainfo.ai_mask.as_failure));
		break;
	case AUE_AUDITON_SETSMASK:
		if (copyin((caddr_t) uap->a2, (caddr_t) &ainfo,
			sizeof (struct auditinfo)))
				return;
		au_uwrite(au_to_arg(3, "setsmask:as_success",
			(u_long) ainfo.ai_mask.as_success));
		au_uwrite(au_to_arg(3, "setsmask:as_failure",
			(u_long) ainfo.ai_mask.as_failure));
		break;
	case AUE_AUDITON_SETCOND:
		if (copyin((caddr_t) uap->a2, (caddr_t) &auditstate,
			sizeof (int)))
			return;
		au_uwrite(au_to_arg(3, "setcond", (u_long) auditstate));
		break;
	case AUE_AUDITON_SETCLASS:
		if (copyin((caddr_t) uap->a2, (caddr_t) &event,
			sizeof (au_evclass_map_t)))
			return;
		au_uwrite(au_to_arg(
			2, "setclass:ec_event", (u_long) event.ec_number));
		au_uwrite(au_to_arg(
			3, "setclass:ec_class", (u_long) event.ec_class));
		break;
	case AUE_GETAUID:
	case AUE_GETAUDIT:
	case AUE_AUDIT:
	case AUE_GETPORTAUDIT:
	case AUE_AUDITON_GPOLICY:
	case AUE_AUDITON_GQCTRL:
	case AUE_AUDITON_GETKMASK:
	case AUE_AUDITON_GETCWD:
	case AUE_AUDITON_GETCAR:
	case AUE_AUDITON_GETSTAT:
	case AUE_AUDITON_SETSTAT:
	case AUE_AUDITON_GETCOND:
	case AUE_AUDITON_GETCLASS:
		break;
	default:
		break;
	}

}	/* AUS_AUDITSYS */


#if 0
/* only audit privileged operations for systeminfo(2) system call */
static au_event_t
aui_sysinfo(au_event_t e)
{
	struct a {
		int command;
		char *buf;
		long count;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	switch (uap->command) {
	case SI_SET_HOSTNAME:
	case SI_SET_SRPC_DOMAIN:
	case SI_SET_KERB_REALM:
		e = AUE_SYSINFO;
		break;
	default:
		e = AUE_NULL;
		break;
	}
	return (e);
}
#endif

/*ARGSUSED*/
static void
aus_sysinfo(tad)
	struct t_audit_data *tad;
{
	struct a {
		int command;
		char *buf;
		long count;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	size_t len;

	au_uwrite(au_to_arg(1, "cmd", (u_long) uap->command));

	switch (uap->command) {
	case SI_SET_HOSTNAME:
	{
		char name[SYS_NMLN];

		if (!suser(CRED()))
			return;

		if (copyinstr(uap->buf, name, SYS_NMLN, &len))
			return;

		/*
		 * Must be non-NULL string and string
		 * must be less than SYS_NMLN chars.
		 */
		if (len < 2 || (len == SYS_NMLN && name[SYS_NMLN-1] != '\0'))
			return;

		au_uwrite(au_to_text(name));
		break;
	}

	case SI_SET_SRPC_DOMAIN:
	{
		char name[SYS_NMLN];

		if (!suser(CRED()))
			return;

		if (copyinstr(uap->buf, name, SYS_NMLN, &len))
			return;

		/*
		 * If string passed in is longer than length
		 * allowed for domain name, fail.
		 */
		if (len == SYS_NMLN && name[SYS_NMLN-1] != '\0')
			return;

		au_uwrite(au_to_text(name));
		break;
	}

	case SI_SET_KERB_REALM:
	{
		char name[REALM_SZ];

		if (!suser(CRED()))
			return;

		if (copyinstr(uap->buf, name, REALM_SZ, &len))
			return;

		/*
		 * If string passed in is longer than length
		 * allowed for domain name, fail.
		 */
		if (len == REALM_SZ && name[REALM_SZ-1] != '\0')
			return;

		au_uwrite(au_to_text(name));
		break;
	}

	default:
		break;
	}
}

static au_event_t
aui_modctl(au_event_t e)
{
	struct a {
		int cmd;
	} *uap = (struct a *) ttolwp(curthread)->lwp_ap;

	switch (uap->cmd) {
	case MODLOAD:
		e = AUE_MODLOAD;
		break;
	case MODUNLOAD:
		e = AUE_MODUNLOAD;
		break;
	case MODCONFIG:
		e = AUE_MODCONFIG;
		break;
	case MODADDMAJBIND:
		e = AUE_MODADDMAJ;
		break;
	default:
		e = AUE_NULL;
		break;
	}
	return (e);
}

/*ARGSUSED*/
static void
aus_modctl(tad)
	struct t_audit_data *tad;
{
	struct modloada {
		int cmd;
		int use_path;
		char *filename;
	} *mla;
	extern char *default_path;
	char *filenamep;

	struct modunloada {
		int cmd;
		int id;
	} *mula;

	struct modconfiga {
		int cmd;
		int subcmd;
		int *data;
	} *mcf;
	struct modconfig mc;
	char *drvname;
	int   i;
	struct aliases alias;
	struct aliases *ap;
	char name[256];
	char *ddi_major_to_name();


	switch (tad->tad_event) {
	case AUE_MODLOAD:
		mla = (struct modloada *) ttolwp(curthread)->lwp_ap;

			/* space to hold path */
		filenamep = kmem_zalloc(MOD_MAXPATH, KM_SLEEP);
			/* get string */
		if (copyinstr(mla->filename, filenamep, MOD_MAXPATH, 0)) {
				/* free allocated path */
			kmem_free(filenamep, MOD_MAXPATH);
			return;
		}
			/* ensure it's null terminated */
		filenamep[MOD_MAXPATH - 1] = 0;

		if (mla->use_path)
			au_uwrite(au_to_text(default_path));
		au_uwrite(au_to_text(filenamep));

			/* release temporary memory */
		kmem_free(filenamep, MOD_MAXPATH);
		break;

	case AUE_MODUNLOAD:
		mula = (struct modunloada *) ttolwp(curthread)->lwp_ap;
		au_uwrite(au_to_arg(1, "id", (u_long) mula->id));
		break;

	case AUE_MODCONFIG:
		mcf = (struct modconfiga *) ttolwp(curthread)->lwp_ap;
			/* sanitize buffer */
		bzero((caddr_t)&mc, sizeof (struct modconfig));
			/* get user arguments */
		if (copyin((caddr_t)(mcf->data), (caddr_t)&mc,
		    sizeof (struct modconfig)) != 0)
			return;
		if (mc.rootdir[0] != NULL) {
				/* safety */
			mc.rootdir[255] = '0';
			au_uwrite(au_to_text(mc.rootdir));
		}
		else
			au_uwrite(au_to_text("no rootdir"));
		if (mc.drvname[0] != NULL) {
				/* safety */
			mc.drvname[255] = '\0';
			au_uwrite(au_to_text(mc.drvname));
		}
		else
			au_uwrite(au_to_text("no drvname"));
		break;
	case AUE_MODADDMAJ:
		mcf = (struct modconfiga *) ttolwp(curthread)->lwp_ap;
			/* sanitize buffer */
		bzero((caddr_t)&mc, sizeof (struct modconfig));
			/* get user arguments */
		if (copyin((caddr_t)(mcf->data), (caddr_t)&mc,
		    sizeof (struct modconfig)) != 0) {
			return;
		}
		if ((drvname = ddi_major_to_name(mc.major)) != NULL &&
			strncmp(drvname, mc.drvname, 256) != 0) {
				/* safety */
			if (mc.drvname[0] != NULL) {
				mc.drvname[255] = '\0';
				au_uwrite(au_to_text(mc.drvname));
			}
				/* drvname != NULL from test above */
			au_uwrite(au_to_text(drvname));
			return;
		}
			/* print out aliases */
		if (mc.rootdir[0] != NULL) {
				/* safety */
			mc.rootdir[255] = '0';
			au_uwrite(au_to_text(mc.rootdir));
		}
		else
			au_uwrite(au_to_text("no rootdir"));
		if (mc.drvname[0] != NULL) {
				/* safety */
			mc.drvname[255] = '\0';
			au_uwrite(au_to_text(mc.drvname));
		}
		else
			au_uwrite(au_to_text("no drvname"));
		au_uwrite(au_to_arg(5, "", (u_long) mc.num_aliases));
		ap = mc.ap;
		for (i = 0; i < mc.num_aliases; i++) {
			bzero((caddr_t)&alias, sizeof (struct aliases));
			if (copyin((caddr_t)ap, (caddr_t)&alias,
			    sizeof (struct aliases)) != 0)
				return;
			if (copyin(alias.a_name, (caddr_t)name,
			    alias.a_len) != 0)
				return;
			name[255] = '\0';
			au_uwrite(au_to_text(name));
			ap = alias.a_next;
		}
		break;
	default:
		break;
	}
}

#ifdef	VPIX
/* null function */
/*ARGSUSED*/
static au_event_t
aui_vpix(au_event_t e)
{
	return (AUE_NULL);
}
#endif	/* VPIX */

au_state_t audit_ets[MAX_KEVENTS];
int naevent = sizeof (audit_ets) / sizeof (au_state_t);
