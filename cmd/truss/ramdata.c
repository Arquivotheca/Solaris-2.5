 /*
  * Copyright (c) 1992 Sun Microsystems, Inc.  All Rights Reserved.
  */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident "@(#)ramdata.c	1.12	94/01/17 SMI"	/* SVr4.0 1.3	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include "pcontrol.h"
#include "ramdata.h"
#include "proto.h"

/* ramdata.c -- read/write data definitions for process management */

char *	command = NULL;		/* name of command ("truss") */
int	length = 0;		/* length of printf() output so far */
pid_t	child = 0;		/* pid of fork()ed child process */
char	pname[32] = "";		/* formatted pid of controlled process */
int	interrupt = FALSE;	/* interrupt signal was received */
int	sigusr1 = FALSE;	/* received SIGUSR1 (release process) */
pid_t	created = 0;		/* if process was created, its process id */
int	Errno = 0;		/* errno for controlled process's syscall */
int	Rval1 = 0;		/* rval1 (%r0) for syscall */
int	Rval2 = 0;		/* rval2 (%r1) for syscall */
uid_t	Euid = 0;		/* truss's effective uid */
uid_t	Egid = 0;		/* truss's effective gid */
uid_t	Ruid = 0;		/* truss's real uid */
uid_t	Rgid = 0;		/* truss's real gid */
prcred_t credentials = { 0 };	/* traced process credentials */
int	istty = FALSE;		/* TRUE iff output is a tty */
int	wasptraced = FALSE;	/* TRUE iff process is being ptrace()d */

int	Fflag = FALSE;		/* option flags from getopt() */
int	qflag = FALSE;
int	fflag = FALSE;
int	cflag = FALSE;
int	aflag = FALSE;
int	eflag = FALSE;
int	iflag = FALSE;
int	lflag = FALSE;
int	tflag = FALSE;
int	pflag = FALSE;
int	sflag = FALSE;
int	mflag = FALSE;
int	oflag = FALSE;
int	vflag = FALSE;
int	xflag = FALSE;

sysset_t trace = { 0 };		/* sys calls to trace */
sysset_t traceeven = { 0 };	/* sys calls to trace even if not reported */
sysset_t verbose = { 0 };	/* sys calls to be verbose about */
sysset_t rawout = { 0 };	/* sys calls to show in raw mode */
sigset_t signals = { 0 };	/* signals to trace */
fltset_t faults = { 0 };	/* faults to trace */

fileset_t readfd = { 0 };	/* read() file descriptors to dump */
fileset_t writefd = { 0 };	/* write() file descriptors to dump */

struct counts * Cp = NULL;	/* for counting: malloc() or shared memory */
timestruc_t sysbegin = { 0 };	/* initial value of stime */
timestruc_t syslast = { 0 };	/* most recent value of stime */
timestruc_t usrbegin = { 0 };	/* initial value of utime */
timestruc_t usrlast = { 0 };	/* most recent value of utime */

pid_t	ancestor = 0;		/* top-level parent process id */
int	descendent = FALSE;	/* TRUE iff descendent of top level */

int	sys_args[9] = { 0 };	/* the arguments to last syscall */
int	sys_nargs = -1;		/* number of arguments to last syscall */
int	sys_indirect = FALSE;	/* if TRUE, this is an indirect system call */

char	sys_name[12] = "sys#ddd";/* name of unknown system call */
char	raw_sig_name[20] = "SIGxxxxx"; /* name of known signal */
char	sig_name[12] = "SIG#dd";/* name of unknown signal */
char	flt_name[12] = "FLT#dd";/* name of unknown fault */

char *	sys_path = NULL;	/* first pathname given to syscall */
unsigned sys_psize = 0;		/* sizeof(*sys_path) */
int	sys_valid = FALSE;	/* pathname was fetched and is valid */

char *	sys_string = NULL;	/* buffer for formatted syscall string */
unsigned sys_ssize = 0;		/* sizeof(*sys_string) */
unsigned sys_leng = 0;		/* strlen(sys_string) */

char	exec_string[80];	/* copy of sys_string for exec() only */

char *	str_buffer = NULL;	/* fetchstring() buffer */
unsigned str_bsize = 0;		/* sizeof(*str_buffer) */

char iob_buf[2*IOBSIZE+8] = { 0 }; /* where prt_iob() leaves its stuff */

char	code_buf[100] = { 0 };	/* for symbolic arguments, e.g., ioctl codes */

int	ngrab = 0;		/* number of pid's to grab */
pid_t	grab[MAXGRAB] = { 0 };	/* process id's to grab */
char *	grabdir[MAXGRAB] = { 0 }; /* path prefix for grabbed process */

process_t	Proc = { 0 };	/* the process structure */
process_t *	PR = NULL;	/* pointer to same (for abend()) */

int	debugflag = FALSE;	/* for debugging */
char *	procdir = "/proc";	/* default PROC directory */
sigset_t psigs = { 0 };		/* pending signals (used by Psyscall()) */

int	recur = 0;		/* show_strioctl() -- to prevent recursion */

int	no_inherit = FALSE;	/* set TRUE iff ioctl(PIOC[RS]FORK) failed */
int	timeout = FALSE;	/* set TRUE by SIGALRM catchers */

long	pagesize = 0;		/* bytes per page; should be per-process */

int	exit_called = FALSE;	/* _exit() syscall was seen */
int	slowmode = FALSE;	/* always wait for tty output to drain */

sysset_t syshang = { 0 };	/* sys calls to make process hang */
sigset_t sighang = { 0 };	/* signals to make process hang */
fltset_t flthang = { 0 };	/* faults to make process hang */
int	Tflag = FALSE;
int	Sflag = FALSE;
int	Mflag = FALSE;
