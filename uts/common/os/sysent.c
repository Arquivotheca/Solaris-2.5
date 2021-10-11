/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sysent.c	1.68	95/07/14 SMI"	/* SVr4.0 1.51	*/

#include "sys/param.h"
#include "sys/types.h"
#include "sys/systm.h"

/*
 * This table is the switch used to transfer to the appropriate
 * routine for processing a system call.  Each row contains the
 * number of arguments expected, a switch that tells systrap()
 * in trap.c whether a setjmp() is not necessary, and a pointer
 * to the routine.
 */

int	access();
int	alarm();
int	auditsys();
int	brk();
int	chdir();
int	chmod();
int	chown();
int	chroot();
int	close();
int	creat();
int	dup();
int	exec();
int	exece();
int	fcntl();
longlong_t	fork();
longlong_t	fork1();
int	fstat();
int	fdsync();
longlong_t	getgid();
longlong_t	getpid();
longlong_t	getuid();
int	gtime();
int	gtty();
#if defined(i386)
int	hrtsys();
#endif /* defined(i386) */
int	ioctl();
int	kill();
int	link();
off_t	lseek();
int	mknod();
int	mount();
int	nice();
int	nullsys();
int	open();
int	pause();
int	profil();
int	pread();
int	pwrite();
int	read();
int	rename();
void	rexit();
int	semsys();
int	setgid();
int	setpgrp();
int	setuid();
int	shmsys();
int	ssig();
int	sigprocmask();
int	sigsuspend();
int	sigaltstack();
int	sigaction();
int	sigpending();
int	sigtimedwait();
int	setcontext();
int	stat();
int	stime();
int	stty();
int	syssync();
int	sysacct();
int	times();
off_t	ulimit();
int	getrlimit();
int	setrlimit();
int	umask();
int	umount();
int	unlink();
int	utime();
int	utssys();
int	wait();
int	write();
int	readv();
int	writev();
int	rmdir();
int	mkdir();
int	getdents();
int	statfs();
int	fstatfs();
int	sysfs();
int	getmsg();
int	poll();
int	putmsg();
int	uadmin();
int	lstat();
int	symlink();
int	readlink();
int	setgroups();
int	getgroups();
int	fchdir();
int	fchown();
int	fchmod();
int	statvfs();
int	fstatvfs();
#ifndef _NO_LONGLONG
offset_t	llseek();
#endif /* !_NO_LONGLONG */

#ifdef i386
int	sysi86();
#endif

int	acl();
int	facl();
int	priocntlsys();
int	waitsys();
int	sigsendsys();
int	mincore();
int	smmap();
int	mprotect();
int	munmap();
longlong_t	vfork();
int	xstat();
int	lxstat();
int	fxstat();
int	xmknod();
int	uname();
int	lchown();
int	getpmsg();
int	putpmsg();
int	memcntl();
int	sysconfig();
int	adjtime();
int	systeminfo();
int	setegid();
int	seteuid();

int	setreuid();
int	setregid();

int	syslwp_create();
int	syslwp_suspend();
void	syslwp_exit();
int	syslwp_continue();
int	lwp_info();
int	lwp_kill();
int	lwp_self();
int	yield();
int	lwp_wait();
int	lwp_setprivate();
int	lwp_getprivate();

int	lwp_mutex_lock();
int	lwp_mutex_unlock();
int	lwp_sema_v();
int	lwp_sema_p();
int	lwp_cond_wait();
int	lwp_cond_signal();
int	lwp_cond_broadcast();
int	lwp_alarm();		/* will be EOL'ed in a post-2.5 release */

int	pathconf();
int	fpathconf();
int	processor_bind();
int	processor_info();
int	p_online();

/*
 *	System Calls used by the ASLWP.
 */
int signotifywait();
int lwp_sigredirect();

/*
 *	POSIX .4 system calls *
 */
int	clock_gettime();
int	clock_settime();
int	clock_getres();
int	timer_create();
int	timer_delete();
int	timer_settime();
int	timer_gettime();
int	timer_getoverrun();
int	nanosleep();
int	sigqueue();

/*
 *	++++++++++++++++++++++++
 *	++  SunOS4.1 Buyback  ++
 *	++++++++++++++++++++++++
 *
 *	fchroot, utimes, vhangup, gettimeofday
 */

int	fchroot();
int	utimes();
int	vhangup();
int	gettimeofday();
int	getitimer();
int	setitimer();
#ifdef	TRACE
int	vtrace();
#endif

#ifdef MEGA
int	uexch();
#endif /* MEGA */

int	modctl();

longlong_t loadable_syscall();
longlong_t syscall_ap();

longlong_t indir();

typedef longlong_t (*llfcn_t)();	/* for casting one-word returns */

/*
 * Sysent initialization macros.
 * 	These take the name string of the system call even though that isn't
 *	currently used in the sysent entry.  This might be useful someday.
 *
 * Initialization macro for system calls which take their args in the C style.
 * These system calls return the longlong_t return value and must call
 * set_errno() to return an error.  For SPARC, narg must be at most six.
 * For more args, use SYSENT_AP().
 */
#define	SYSENT_C(name, call, narg)	\
	{ (narg), 0, NULL, NULL, (call) }

#define	SYSENT_CI(name, call, narg)	\
	{ (narg), 0, NULL, NULL, (llfcn_t)(call) }

/*
 * Initialization macro for system calls which take their args in the standard
 * Unix style of a pointer to the arg structure and a pointer to the rval_t.
 */
#define	SYSENT_AP(name, call, narg)	\
	{ (narg), 0, (call), NULL, syscall_ap }

/*
 * Initialization macro for loadable system calls.
 */
#define	SYSENT_LOADABLE()	\
	{ 0, SE_LOADABLE, (int (*)())nosys, NULL, loadable_syscall }

struct sysent nosys_ent = SYSENT_C("nosys", nosys, 0);

struct sysent sysent[NSYSCALL] =
{
	/*  0 */ SYSENT_C("indir",		indir,		1),
	/*  1 */ SYSENT_CI("exit",	(int (*)())rexit,	1),
	/*  2 */ SYSENT_C("fork",		fork,		0),
	/*  3 */ SYSENT_CI("read",		read,		3),
	/*  4 */ SYSENT_CI("write",		write,		3),
	/*  5 */ SYSENT_CI("open",		open,		3),
	/*  6 */ SYSENT_CI("close",		close,		1),
	/*  7 */ SYSENT_CI("wait",		wait,		0),
	/*  8 */ SYSENT_CI("creat",		creat,		2),
	/*  9 */ SYSENT_CI("link",		link,		2),
	/* 10 */ SYSENT_CI("unlink",		unlink,		1),
	/* 11 */ SYSENT_AP("exec",		exec,		2),
	/* 12 */ SYSENT_CI("chdir",		chdir,		1),
	/* 13 */ SYSENT_CI("time",		gtime,		0),
	/* 14 */ SYSENT_CI("mknod",		mknod,		3),
	/* 15 */ SYSENT_CI("chmod",		chmod,		2),
	/* 16 */ SYSENT_CI("chown",		chown,		3),
	/* 17 */ SYSENT_CI("brk",		brk,		1),
	/* 18 */ SYSENT_CI("stat",		stat,		2),
	/* 19 */ SYSENT_CI("lseek",		lseek,		3),
	/* 20 */ SYSENT_C("getpid",		getpid,		0),
	/* 21 */ SYSENT_AP("mount",		mount,		6),
	/* 22 */ SYSENT_CI("umount",		umount,		1),
	/* 23 */ SYSENT_CI("setuid",		setuid,		1),
	/* 24 */ SYSENT_C("getuid",		getuid,		0),
	/* 25 */ SYSENT_CI("stime",		stime,		1),
	/* 26 */ SYSENT_LOADABLE(),			/* (was ptrace) */
	/* 27 */ SYSENT_CI("alarm",		alarm,		1),
	/* 28 */ SYSENT_CI("fstat",		fstat,		2),
	/* 29 */ SYSENT_CI("pause",		pause,		0),
	/* 30 */ SYSENT_CI("utime",		utime,		2),
	/* 31 */ SYSENT_AP("stty",		stty,		2),
	/* 32 */ SYSENT_AP("gtty",		gtty,		2),
	/* 33 */ SYSENT_CI("access",		access,		2),
	/* 34 */ SYSENT_CI("nice",		nice,		1),
	/* 35 */ SYSENT_CI("statfs",		statfs,		4),
	/* 36 */ SYSENT_CI("sync",		syssync,	0),
	/* 37 */ SYSENT_CI("kill",		kill,		2),
	/* 38 */ SYSENT_CI("fstatfs",		fstatfs,	4),
	/* 39 */ SYSENT_CI("setpgrp",		setpgrp,	3),
	/* 40 */ SYSENT_LOADABLE(),			/* (was cxenix) */
	/* 41 */ SYSENT_CI("dup",		dup,		1),
	/* 42 */ SYSENT_LOADABLE(),			/* (was pipe ) */
	/* 43 */ SYSENT_CI("times",		times,		1),
	/* 44 */ SYSENT_CI("prof",		profil,		4),
	/* 45 */ SYSENT_LOADABLE(),			/* (was proc lock) */
	/* 46 */ SYSENT_CI("setgid",		setgid,		1),
	/* 47 */ SYSENT_C("getgid",		getgid,		0),
	/* 48 */ SYSENT_CI("sig",		ssig,		2),
	/* 49 */ SYSENT_LOADABLE(),			/* (was msgsys) */
#ifdef i386
	/* 50 */ SYSENT_AP("sysi86", 		sysi86,		4),
				/* i386-specific syscall call */
#else
	/* 50 */ SYSENT_LOADABLE(),			/* (was sys3b) */
#endif /* i386 */
	/* 51 */ SYSENT_LOADABLE(),			/* sysacct */
	/* 52 */ SYSENT_LOADABLE(),			/* shmsys */
	/* 53 */ SYSENT_LOADABLE(),			/* semsys */
	/* 54 */ SYSENT_AP("ioctl",		ioctl,		3),
	/* 55 */ SYSENT_AP("uadmin",		uadmin,		3),
#ifdef MEGA
	/* 56 */ SYSENT_AP("uexch",		uexch,		3),
#else
	/* 56 */ SYSENT_LOADABLE(),
#endif /* MEGA */
	/* 57 */ SYSENT_AP("utssys",		utssys,		4),
	/* 58 */ SYSENT_CI("fdsync",		fdsync,		2),
	/* 59 */ SYSENT_AP("exece",		exece,		3),
	/* 60 */ SYSENT_CI("umask",		umask,		1),
	/* 61 */ SYSENT_CI("chroot",		chroot,		1),
	/* 62 */ SYSENT_CI("fcntl",		fcntl,		3),
	/* 63 */ SYSENT_CI("ulimit",		ulimit,		2),

	/*
	 * The following 6 entries were reserved for the UNIX PC.
	 */
	/* 64 */ SYSENT_LOADABLE(),
	/* 65 */ SYSENT_LOADABLE(),
	/* 66 */ SYSENT_LOADABLE(),
	/* 67 */ SYSENT_LOADABLE(),		/* file locking call */
	/* 68 */ SYSENT_LOADABLE(),		/* local system calls */
	/* 69 */ SYSENT_LOADABLE(),		/* inode open */

	/* 70 */ SYSENT_LOADABLE(),		/* (was advfs) */
	/* 71 */ SYSENT_LOADABLE(),		/* (was unadvfs) */
	/* 72 */ SYSENT_LOADABLE(),		/* (was notused) */
	/* 73 */ SYSENT_LOADABLE(),		/* (was notused) */
	/* 74 */ SYSENT_LOADABLE(),		/* (was rfstart) */
	/* 75 */ SYSENT_LOADABLE(),		/* (was sigret (SunOS)) */
	/* 76 */ SYSENT_LOADABLE(),		/* (was rdebug) */
	/* 77 */ SYSENT_LOADABLE(),		/* (was rfstop) */
	/* 78 */ SYSENT_LOADABLE(),		/* (was rfsys) */
	/* 79 */ SYSENT_CI("rmdir",		rmdir,		1),
	/* 80 */ SYSENT_CI("mkdir",		mkdir,		2),
	/* 81 */ SYSENT_CI("getdents",		getdents,	3),
	/* 82 */ SYSENT_LOADABLE(),		/* (was libattach) */
	/* 83 */ SYSENT_LOADABLE(),		/* (was libdetach) */
	/* 84 */ SYSENT_CI("sysfs",		sysfs,		3),
	/* 85 */ SYSENT_AP("getmsg",		getmsg,		4),
	/* 86 */ SYSENT_AP("putmsg",		putmsg,		4),
	/* 87 */ SYSENT_AP("poll",		poll,		3),
	/* 88 */ SYSENT_CI("lstat",		lstat,		2),
	/* 89 */ SYSENT_CI("symlink",		symlink,	2),
	/* 90 */ SYSENT_CI("readlink",		readlink,	3),
	/* 91 */ SYSENT_CI("setgroups",		setgroups,	2),
	/* 92 */ SYSENT_CI("getgroups",		getgroups,	2),
	/* 93 */ SYSENT_CI("fchmod",		fchmod,		2),
	/* 94 */ SYSENT_CI("fchown",		fchown,		3),
	/* 95 */ SYSENT_CI("sigprocmask",	sigprocmask,	3),
	/* 96 */ SYSENT_CI("sigsuspend",	sigsuspend,	1),
	/* 97 */ SYSENT_CI("sigaltstack ",	sigaltstack,	2),
	/* 98 */ SYSENT_CI("sigaction",		sigaction,	3),
	/* 99 */ SYSENT_CI("sigpending",	sigpending,	2),

	/* 100 */ SYSENT_AP("setcontext",	setcontext,	2),
	/* 101 */ SYSENT_LOADABLE(),	/* (was evsys) */
	/* 102 */ SYSENT_LOADABLE(),	/* (was evtrapret) */
	/* 103 */ SYSENT_CI("statvfs",		statvfs,	2),
	/* 104 */ SYSENT_CI("fstatvfs",		fstatvfs,	2),
	/* 105 */ SYSENT_LOADABLE(),
	/* 106 */ SYSENT_LOADABLE(),		/* nfssys */
	/* 107 */ SYSENT_CI("waitset",		waitsys,	4),
	/* 108 */ SYSENT_CI("sigsendset",	sigsendsys,	2),
#if defined(i386)
	/* 109 */ SYSENT_AP("hrtsys",		hrtsys,		5),
#else
	/* 109 */ SYSENT_LOADABLE(),
#endif /* defined(i386) */
	/* 110 */ SYSENT_LOADABLE(),	/* was acancel */
	/* 111 */ SYSENT_LOADABLE(),	/* was async */
	/* 112 */ SYSENT_AP("priocntlsys",	priocntlsys,	4),
	/* 113 */ SYSENT_CI("pathconf",		pathconf,	2),
	/* 114 */ SYSENT_CI("mincore",		mincore,	3),
	/* 115 */ SYSENT_CI("mmap",		smmap,		6),
	/* 116 */ SYSENT_CI("mprotect",		mprotect,	3),
	/* 117 */ SYSENT_CI("munmap",		munmap,		2),
	/* 118 */ SYSENT_CI("fpathconf",	fpathconf,	2),
	/* 119 */ SYSENT_C("vfork",		vfork,		0),
	/* 120 */ SYSENT_CI("fchdir",		fchdir,		1),
	/* 121 */ SYSENT_CI("readv",		readv,		3),
	/* 122 */ SYSENT_CI("writev",		writev,		3),
	/* 123 */ SYSENT_CI("xstat",		xstat,		3),
	/* 124 */ SYSENT_CI("lxstat",		lxstat,		3),
	/* 125 */ SYSENT_CI("fxstat",		fxstat,		3),
	/* 126 */ SYSENT_CI("xmknod",		xmknod,		4),
	/* 127 */ SYSENT_LOADABLE(),		/* was clocal */
	/* 128 */ SYSENT_CI("setrlimit",	setrlimit,	2),
	/* 129 */ SYSENT_CI("getrlimit",	getrlimit,	2),
	/* 130 */ SYSENT_CI("lchown",		lchown,		3),
	/* 131 */ SYSENT_CI("memcntl",		memcntl,	6),
	/* 132 */ SYSENT_AP("getpmsg",		getpmsg,	5),
	/* 133 */ SYSENT_AP("putpmsg",		putpmsg,	5),
	/* 134 */ SYSENT_CI("rename",		rename,		2),
	/* 135 */ SYSENT_CI("uname",		uname,		1),
	/* 136 */ SYSENT_CI("setegid",		setegid,	1),
	/* 137 */ SYSENT_CI("sysconfig",	sysconfig,	1),
	/* 138 */ SYSENT_CI("adjtime",		adjtime,	2),
	/* 139 */ SYSENT_CI("systeminfo",	systeminfo,	3),
	/* 140 */ SYSENT_LOADABLE(),		/* reserved */
	/* 141 */ SYSENT_CI("seteuid",		seteuid,	1),
#ifdef	TRACE
	/* 142 */ SYSENT_AP("vtrace",		vtrace,		3),
#else
	/* 142 */ SYSENT_LOADABLE(),		/* vtrace */
#endif	/* TRACE */
	/* 143 */ SYSENT_C("fork1",		fork1,		0),
	/* 144 */ SYSENT_CI("sigtimedwait",	sigtimedwait,	3),
	/* 145 */ SYSENT_CI("lwp_info",		lwp_info,	1),
	/* 146 */ SYSENT_CI("yield",		yield,		0),
	/* 147 */ SYSENT_CI("lwp_sema_p",	lwp_sema_p,	1),
	/* 148 */ SYSENT_CI("lwp_sema_v",	lwp_sema_v,	1),
	/* 149 */ SYSENT_LOADABLE(),		/* reserved */
	/* 150 */ SYSENT_LOADABLE(),		/* reserved */
	/* 151 */ SYSENT_LOADABLE(),		/* reserved */
	/* 152 */ SYSENT_AP("modctl",		modctl,		4),
	/* 153 */ SYSENT_CI("fchroot",		fchroot,	1),
	/* 154 */ SYSENT_CI("utimes",		utimes,		2),
	/* 155 */ SYSENT_CI("vhangup",		vhangup,	0),
	/* 156 */ SYSENT_CI("gettimeofday",	gettimeofday,	1),
	/* 157 */ SYSENT_AP("getitimer",	getitimer,	2),
	/* 158 */ SYSENT_AP("setitimer",	setitimer,	3),
	/* 159 */ SYSENT_CI("lwp_create",	syslwp_create,	3),
	/* 160 */ SYSENT_CI("lwp_exit",	(int (*)())syslwp_exit,	0),
	/* 161 */ SYSENT_CI("lwp_stop",		syslwp_suspend,	1),
	/* 162 */ SYSENT_CI("lwp_continue",	syslwp_continue, 1),
	/* 163 */ SYSENT_CI("lwp_kill",		lwp_kill,	2),
	/* 164 */ SYSENT_CI("lwp_get_id",	lwp_self,	0),
	/* 165 */ SYSENT_CI("lwp_setprivate",	lwp_setprivate,	1),
	/* 166 */ SYSENT_CI("lwp_getprivate",	lwp_getprivate,	0),
	/* 167 */ SYSENT_CI("lwp_wait",		lwp_wait,	2),
	/* 168 */ SYSENT_CI("lwp_mutex_unlock",	lwp_mutex_unlock,	1),
	/* 169 */ SYSENT_CI("lwp_mutex_lock",	lwp_mutex_lock,		1),
	/* 170 */ SYSENT_CI("lwp_cond_wait",	lwp_cond_wait,		3),
	/* 171 */ SYSENT_CI("lwp_cond_signal",	lwp_cond_signal,	1),
	/* 172 */ SYSENT_CI("lwp_cond_broadcast", lwp_cond_broadcast,	1),
	/* 173 */ SYSENT_CI("pread",		pread,		4),
	/* 174 */ SYSENT_CI("pwrite ",		pwrite,		4),
			/* Note that long long offset counts is two arguments */

#ifdef _NO_LONGLONG
	/* 175 */ SYSENT_CI("lseek",	lseek,	3),
#else
	/* 175 */ SYSENT_C("llseek",	llseek,	4),
#endif _NO_LONGLONG

	/* 176 */ SYSENT_LOADABLE(),	/* inst_sync */
	/* 177 */ SYSENT_LOADABLE(),
	/* 178 */ SYSENT_LOADABLE(),
	/* 179 */ SYSENT_LOADABLE(),
	/* 180 */ SYSENT_LOADABLE(),	/* kaio */
	/* 181 */ SYSENT_LOADABLE(),
	/* 182 */ SYSENT_LOADABLE(),
	/* 183 */ SYSENT_LOADABLE(),
	/* 184 */ SYSENT_LOADABLE(),	/* tsolsys */
	/* 185 */ SYSENT_AP("acl",		acl,		4),
	/* 186 */ SYSENT_AP("auditsys",	auditsys, 2),		/* c2audit */
	/* 187 */ SYSENT_CI("processor_bind",	processor_bind,	4),
	/* 188 */ SYSENT_CI("processor_info",	processor_info,	2),
	/* 189 */ SYSENT_CI("p_online",		p_online,	2),
	/* 190 */ SYSENT_CI("sigqueue",		sigqueue,	3),
	/* 191 */ SYSENT_CI("clock_gettime",	clock_gettime,	2),
	/* 192 */ SYSENT_CI("clock_settime",	clock_settime,	2),
	/* 193 */ SYSENT_CI("clock_getres",	clock_getres,	2),
	/* 194 */ SYSENT_AP("timer_create",	timer_create,	3),
	/* 195 */ SYSENT_AP("timer_delete",	timer_delete,	1),
	/* 196 */ SYSENT_AP("timer_settime",	timer_settime,	4),
	/* 197 */ SYSENT_AP("timer_gettime",	timer_gettime,	2),
	/* 198 */ SYSENT_AP("timer_getoverrun",	timer_getoverrun, 1),
	/* 199 */ SYSENT_CI("nanosleep",	nanosleep,	2),
	/* 200 */ SYSENT_AP("facl",		facl,		4),
	/* 201 */ SYSENT_LOADABLE(),	/* door */
	/* 202 */ SYSENT_CI("setreuid",		setreuid,	2),
	/* 203 */ SYSENT_CI("setregid",		setregid,	2),
	/* 204 */ SYSENT_LOADABLE(),
	/* 205 */ SYSENT_LOADABLE(),
	/* 206 */ SYSENT_LOADABLE(),
	/* 207 */ SYSENT_LOADABLE(),
	/* 208 */ SYSENT_LOADABLE(),
	/* 209 */ SYSENT_LOADABLE(),
	/* 210 */ SYSENT_CI("signotifywait",	signotifywait, 0),
	/* 211 */ SYSENT_CI("lwp_sigredirect",	lwp_sigredirect, 2),
	/* 212 */ SYSENT_CI("lwp_alarm",	lwp_alarm, 1)
};

/*
 * Space allocated and initialized in mod_setup().
 */
char **syscallnames;
