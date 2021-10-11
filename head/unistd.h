/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _UNISTD_H
#define	_UNISTD_H

#pragma ident	"@(#)unistd.h	1.33	95/08/28 SMI"	/* SVr4.0 1.26	*/

#include <sys/feature_tests.h>

#include <sys/types.h>
#include <sys/unistd.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Symbolic constants for the "access" routine: */
#define	R_OK	4	/* Test for Read permission */
#define	W_OK	2	/* Test for Write permission */
#define	X_OK	1	/* Test for eXecute permission */
#define	F_OK	0	/* Test for existence of File */

#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
#define	F_ULOCK	0	/* Unlock a previously locked region */
#define	F_LOCK	1	/* Lock a region for exclusive use */
#define	F_TLOCK	2	/* Test and lock a region for exclusive use */
#define	F_TEST	3	/* Test a region for other processes locks */
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */

/* Symbolic constants for the "lseek" routine: */

#ifndef	SEEK_SET
#define	SEEK_SET	0	/* Set file pointer to "offset" */
#endif

#ifndef	SEEK_CUR
#define	SEEK_CUR	1	/* Set file pointer to current plus "offset" */
#endif

#ifndef	SEEK_END
#define	SEEK_END	2	/* Set file pointer to EOF plus "offset" */
#endif

#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
/* Path names: */
#define	GF_PATH	"/etc/group"	/* Path name of the "group" file */
#define	PF_PATH	"/etc/passwd"	/* Path name of the "passwd" file */
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */

/*
 * compile-time symbolic constants,
 * Support does not mean the feature is enabled.
 * Use pathconf/sysconf to obtain actual configuration value.
 */
#define	_POSIX_FSYNC			1
#define	_POSIX_JOB_CONTROL		1
#define	_POSIX_MAPPED_FILES		1
#define	_POSIX_MEMLOCK			1
#define	_POSIX_MEMLOCK_RANGE		1
#define	_POSIX_MEMORY_PROTECTION	1
#define	_POSIX_REALTIME_SIGNALS		1
#define	_POSIX_SAVED_IDS		1
#define	_POSIX_SYNCHRONIZED_IO		1
#define	_POSIX_TIMERS			1
/*
 * POSIX.4a compile-time symbolic constants.
 */
#define	_POSIX_THREAD_SAFE_FUNCTIONS	1
#define	_POSIX_THREADS			1
#define	_POSIX_THREAD_ATTR_STACKADDR	1
#define	_POSIX_THREAD_ATTR_STACKSIZE	1
#define	_POSIX_THREAD_PROCESS_SHARED	1
#define	_POSIX_THREAD_PRIORITY_SCHEDULING	1

#ifndef _POSIX_VDISABLE
#define	_POSIX_VDISABLE		0
#endif

#ifndef	NULL
#define	NULL	0
#endif

#define	STDIN_FILENO	0
#define	STDOUT_FILENO	1
#define	STDERR_FILENO	2

#if defined(__STDC__)

extern int access(const char *, int);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int acct(const char *);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern unsigned alarm(unsigned);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int brk(void *);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern int chdir(const char *);
extern int chown(const char *, uid_t, gid_t);
#if !defined(_POSIX_C_SOURCE) || defined(__EXTENSIONS__)
extern int chroot(const char *);
#endif /* !defined(_POSIX_C_SOURCE) || defined(__EXTENSIONS__) */
extern int close(int);
#if (defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4)) || \
	defined(__EXTENSIONS__)
extern size_t confstr(int, char *, size_t);
extern char *crypt(const char *, const char *);
#endif /* (defined(XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4))... */
#if !defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) || \
	defined(__EXTENSIONS__)
extern char *ctermid(char *);
#endif /* (!defined(_POSIX_C_SOURCE) && (_XOPEN_VERSION - 0 != 4))... */
extern char *cuserid(char *);
extern int dup(int);
extern int dup2(int, int);
#if (defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4)) || \
	defined(__EXTENSIONS__)
extern void encrypt(char *, int);
#endif /* (defined(XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4))... */
extern int execl(const char *, const char *, ...);
extern int execle(const char *, const char *, ...);
extern int execlp(const char *, const char *, ...);
extern int execv(const char *, char *const *);
extern int execve(const char *, char *const *, char *const *);
extern int execvp(const char *, char *const *);
extern void _exit(int);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int fattach(int, const char *);
extern int fchdir(int);
extern int fchown(int, uid_t, gid_t);
extern int fchroot(int);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) || \
	(_POSIX_C_SOURCE > 2) || defined(__EXTENSIONS__)
extern int fdatasync(int);
#endif /* (!defined(_POSIX_C_SOURCE) && ! defined(_XOPEN_SOURCE))... */
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int fdetach(const char *);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern pid_t fork(void);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern pid_t fork1(void);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern long fpathconf(int, int);
#if !defined(_POSIX_C_SOURCE) || (_POSIX_C_SOURCE > 2) || \
	defined(__EXTENSIONS__)
extern int fsync(int);
#endif /* !defined(_POSIX_C_SOURCE) || (_POSIX_C_SOURCE > 2)... */
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	(_POSIX_C_SOURCE > 2) || defined(__EXTENSIONS__)
extern int ftruncate(int, off_t);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern char *getcwd(char *, size_t);
extern gid_t getegid(void);
extern uid_t geteuid(void);
extern gid_t getgid(void);
extern int getgroups(int, gid_t *);
extern char *getlogin(void);
#if (defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4)) || \
	defined(__EXTENSIONS__)
extern int  getopt(int, char *const *, const char *);
extern char *optarg;
extern int  opterr, optind, optopt;
extern char *getpass(const char *);
#endif /* (defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4))... */
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern pid_t getpgid(pid_t);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern pid_t getpid(void);
extern pid_t getppid(void);
extern pid_t getpgrp(void);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
char *gettxt(const char *, const char *);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern pid_t getsid(pid_t);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern uid_t getuid(void);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int ioctl(int, int, ...);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern int isatty(int);
extern int link(const char *, const char *);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int lchown(const char *, uid_t, gid_t);
extern offset_t llseek(int, offset_t, int);
extern int lockf(int, int, long);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern off_t lseek(int, off_t, int);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int mincore(caddr_t, size_t, char *);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
#if !defined(_POSIX_C_SOURCE) || defined(__EXTENSIONS__)
extern int nice(int);
#endif /* !defined(_POSIX_C_SOURCE) || defined(__EXTENSIONS__) */
extern long pathconf(const char *, int);
extern int pause(void);
extern int pipe(int *);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern ssize_t pread(int, void *, size_t, off_t);
extern void profil(unsigned short *, unsigned int, unsigned int, unsigned int);
extern int ptrace(int, pid_t, int, int);
extern ssize_t pwrite(int, const void *, size_t, off_t);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern ssize_t read(int, void *, unsigned);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int readlink(const char *, void *, int);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
#if (!defined(_POSIX_C_SOURCE) && (_XOPEN_VERSION - 0 != 4)) || \
	defined(__EXTENSIONS__)
extern int rename(const char *, const char *);
#endif /* (!defined(_POSIX_C_SOURCE) && (_XOPEN_VERSION - 0 != 4))... */
extern int rmdir(const char *);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern void *sbrk(int);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern int setgid(gid_t);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int setegid(gid_t);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int setgroups(int, const gid_t *);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern int setpgid(pid_t, pid_t);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern pid_t setpgrp(void);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern pid_t setsid(void);
extern int setuid(uid_t);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int seteuid(uid_t);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern unsigned sleep(unsigned);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int stime(const time_t *);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
#if defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4)
/* __EXTENSIONS__ makes the SVID Third Edition prototype in stdlib.h visible */
extern void swab(const void *, void *, ssize_t);
#endif /* defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4) */
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int symlink(const char *, const char *);
extern void sync(void);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern long sysconf(int);
extern pid_t tcgetpgrp(int);
extern int tcsetpgrp(int, pid_t);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int truncate(const char *, off_t);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern char *ttyname(int);
extern int unlink(const char *);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern pid_t vfork(void);
extern void vhangup(void);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
extern ssize_t write(int, const void *, unsigned);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern void yield(void);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */

#else

extern int access();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int acct();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern unsigned alarm();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int brk();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern int chdir();
extern int chown();
#if !defined(_POSIX_C_SOURCE) && ! defined(_XOPEN_SOURCE)
extern int chroot();
#endif /* !defined(_POSIX_C_SOURCE) && ! defined(_XOPEN_SOURCE) */
extern int close();
#if defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4)
extern size_t confstr();
extern  char *crypt();
#endif /* defined(XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4) */
extern char *ctermid();
#ifdef _REENTRANT
extern char *ctermid_r(char *);
#endif /* _REENTRANT */
extern char *cuserid();
extern int dup();
extern int dup2();
#if defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4)
extern  void    encrypt();
#endif /* defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4) */
extern int execl();
extern int execle();
extern int execlp();
extern int execv();
extern int execve();
extern int execvp();
extern void _exit();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int fattach();
extern int fchdir();
extern int fchown();
extern int fchroot();
extern int fdetach();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern int fork();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int fork1();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern long fpathconf();
#if !defined(_POSIX_C_SOURCE)
extern int fsync();
#endif /* !defined(_POSIX_C_SOURCE) */
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int ftruncate();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern char *getcwd();
extern int getegid();
extern int geteuid();
extern int getgid();
extern int getgroups();
extern char *getlogin();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int getpgid();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern int getpid();
extern int getppid();
extern int getpgrp();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int getsid();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
char *gettxt();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern int getuid();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int ioctl();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern int isatty();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int lchown();
extern offset_t llseek();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern int link();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int lockf();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern long lseek();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int mincore();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
#if !defined(_POSIX_C_SOURCE)
extern int nice();
#endif /* !defined(_POSIX_C_SOURCE) */
#if defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4)
extern  char *optarg;
extern  int  opterr, optind, optopt;
#endif /* defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4) */
extern long pathconf();
extern int pause();
extern int pipe();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern ssize_t pread();
extern void profil();
extern int ptrace();
extern ssize_t pwrite();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern ssize_t read();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int readlink();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern int rmdir();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern void *sbrk();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern int setgid();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int setegid();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int setgroups();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern int setpgid();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int setpgrp();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern int setsid();
extern int setuid();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int seteuid();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern unsigned sleep();
#if defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4)
extern void swab();
#endif /* defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4) */
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int stime();
extern int symlink();
extern void sync();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern long sysconf();
extern int tcgetpgrp();
extern int tcsetpgrp();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern int truncate();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */
extern char *ttyname();
extern int unlink();
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int vfork();
extern void vhangup();
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) ... */
extern ssize_t write();
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern void yield();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE */

#endif

/*
 * This atrocity is necessary on sparc because registers modified
 * by the child get propagated back to the parent via the window
 * save/restore mechanism.
 */
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
#if defined(sparc) || defined(__sparc)
#pragma unknown_control_flow(vfork)
#endif
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */


/*
 * getlogin_r() & ttyname_r() prototypes are defined here.
 */
#if	defined(__EXTENSIONS__) || defined(_REENTRANT) || \
	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

#if	defined(__STDC__)

#if	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

#ifdef __PRAGMA_REDEFINE_EXTNAME
extern int getlogin_r(char *, int);
extern int ttyname_r(int, char *, size_t);
#pragma redefine_extname getlogin_r __posix_getlogin_r
#pragma redefine_extname ttyname_r __posix_ttyname_r
#else  /* __PRAGMA_REDEFINE_EXTNAME */

static int
getlogin_r(char *__name, int __len)
{
	extern int __posix_getlogin_r(char *, int);
	return (__posix_getlogin_r(__name, __len));
}
static int
ttyname_r(int __fildes, char *__buf, size_t __size)
{
	extern int __posix_ttyname_r(int, char *, size_t);
	return (__posix_ttyname_r(__fildes, __buf, __size));
}
#endif /* __PRAGMA_REDEFINE_EXTNAME */

#else  /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

extern char *getlogin_r(char *, int);
extern char *ttyname_r(int, char *, int);

#endif  /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

#else  /* __STDC__ */

#if	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

#ifdef __PRAGMA_REDEFINE_EXTNAME
extern int getlogin_r();
extern int ttyname_r();
#pragma redefine_extname getlogin_r __posix_getlogin_r
#pragma redefine_extname ttyname_r __posix_ttyname_r
#else  /* __PRAGMA_REDEFINE_EXTNAME */

static int
getlogin_r(__name, __len)
	char *__name;
	int __len;
{
	extern int __posix_getlogin_r();
	return (__posix_getlogin_r(__name, __len));
}
static int
ttyname_r(__fildes, __buf, __size)
	int __fildes;
	char *__buf;
	size_t __size;
{
	extern int __posix_ttyname_r();
	return (__posix_ttyname_r(__fildes, __buf, __size));
}
#endif /* __PRAGMA_REDEFINE_EXTNAME */

#else  /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

extern char *getlogin_r();
extern char *ttyname_r();

#endif /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

#endif /* __STDC__ */

#endif /* defined(__EXTENSIONS__) || (__STDC__ == 0 ... */

#ifdef	__cplusplus
}
#endif

#endif /* _UNISTD_H */
