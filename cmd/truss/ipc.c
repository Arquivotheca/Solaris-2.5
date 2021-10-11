/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ipc.c	1.13	93/08/18 SMI"	/* SVr4.0 1.2	*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/termio.h>

#include <signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include "pcontrol.h"
#include "ramdata.h"
#include "proto.h"

/*
 * Routines related to interprocess communication
 * among the truss processes which are controlling
 * multiple traced processes.
 *
 * This module is carefully coded to contain only read-only data.
 * All read/write data is defined in ramdata.c (see also ramdata.h).
 */

/*
 * Function prototypes for static routines in this module.
 */
#if	defined(__STDC__)

static	void	Ecritical( int );
static	void	Xcritical( int );
static	void	UnFlush();

#else	/* defined(__STDC__) */

static	void	Ecritical();
static	void	Xcritical();
static	void	UnFlush();

#endif	/* defined(__STDC__) */

void
Flush()		/* ensure everyone keeps out of each other's way */
{		/* while writing lines of trace output		 */

	/* except for regions bounded by Eserialize()/Xserialize(), */
	/* this is the only place anywhere in the program */
	/* where a write() to the trace output file takes place */
	/* so here is where we detect errors writing to the output */

	register FILE * fp = stdout;

	if (fp->_ptr == fp->_base)
		return;

	errno = 0;

	Ecritical(0);
	if (interrupt)
		UnFlush();
	else
		(void) fflush(fp);
	if (slowmode)
		(void) ioctl(fileno(fp), TCSBRK, 1);
	Xcritical(0);

	if (ferror(fp) && errno)	/* error on write(), probably EPIPE */
		interrupt = TRUE;		/* post an interrupt */
}

static void
UnFlush()	/* avoid writing what is in the stdout buffer */
{
	register FILE * fp = stdout;

	fp->_cnt -= (fp->_ptr - fp->_base);	/* this is filthy */
	fp->_ptr = fp->_base;
}

/*
 * Eserialize() and Xserialize() are used to bracket
 * a region which may produce large amounts of output,
 * such as showargs()/dumpargs().
 */

void
Eserialize()
{
	/* serialize output */
	Ecritical(0);
}

void
Xserialize()
{
	(void) fflush(stdout);
	if (slowmode)
		(void) ioctl(fileno(stdout), TCSBRK, 1);
	Xcritical(0);
}

static void	/* enter critical region --- */
Ecritical(num)	/* wait on mutex, lock out other processes */
	int num;	/* which mutex */
{
	int rv = _lwp_mutex_lock(&Cp->mutex[num]);

	if (rv != 0) {
		char mnum[2];
		mnum[0] = '0' + num;
		mnum[1] = '\0';
		errno = rv;
		perror(command);
		errmsg("cannot grab mutex #", mnum);
	}
}

static void	/* exit critical region --- */
Xcritical(num)	/* release other processes waiting on mutex */
	int num;	/* which mutex */
{
	int rv = _lwp_mutex_unlock(&Cp->mutex[num]);

	if (rv != 0) {
		char mnum[2];
		mnum[0] = '0' + num;
		mnum[1] = '\0';
		errno = rv;
		perror(command);
		errmsg("cannot release mutex #", mnum);
	}
}

void
procadd(spid)	/* add process to list of those being traced */
	register pid_t spid;
{
	register int i;
	register int j = -1;

	if (Cp == NULL)
		return;

	Ecritical(1);
	for (i = 0; i < sizeof(Cp->tpid)/sizeof(Cp->tpid[0]); i++) {
		if (Cp->tpid[i] == 0) {
			if (j == -1)	/* remember first vacant slot */
				j = i;
			if (Cp->spid[i] == 0)	/* this slot is better */
				break;
		}
	}
	if (i < sizeof(Cp->tpid)/sizeof(Cp->tpid[0]))
		j = i;
	if (j >= 0) {
		Cp->tpid[j] = getpid();
		Cp->spid[j] = spid;
	}
	Xcritical(1);
}

void
procdel()	/* delete process from list of those being traced */
{
	register int i;
	register pid_t tpid;

	if (Cp == NULL)
		return;

	tpid = getpid();

	Ecritical(1);
	for (i = 0; i < sizeof(Cp->tpid)/sizeof(Cp->tpid[0]); i++) {
		if (Cp->tpid[i] == tpid) {
			Cp->tpid[i] = 0;
			break;
		}
	}
	Xcritical(1);
}

int				/* check for open of /proc/nnnnn file */
checkproc(Pr, path, err)	/* return TRUE iff process opened its own */
	register process_t *Pr;	/* else inform controlling truss process */
	register char * path;
	int err;
{
	register int pid;
	register int i;
	register char *fname;
	CONST char * dirname;
	char * next;
	char * sp;
	int rc = FALSE;		/* assume not self-open */

	if ((sp = strrchr(path, '/')) == NULL) {	/* last component */
		fname = path;
		dirname = ".";
	} else {
		*sp = '\0';
		fname = sp + 1;
		dirname = path;
	}

	if ((pid = strtol(fname, &next, 10)) < 0  /* filename not a number */
	 || *next != '\0'		/* filename not a number */
	 || !isprocdir(Pr, dirname)	/* file not in a /proc directory */
	 || pid == getpid()		/* process opened truss's /proc file */
	 || pid == 0) {			/* process opened process 0 */
		if (sp != NULL)
			*sp = '/';
		return rc;
	}

	if (sp != NULL)		/* restore the embedded '/' */
		*sp = '/';

	/* process did open a /proc file --- */

	if (pid == Pr->upid)	/* process opened its own /proc file */
		rc = TRUE;
	else {			/* send signal to controlling truss process */
		for (i = 0; i < sizeof(Cp->tpid)/sizeof(Cp->tpid[0]); i++) {
			if (Cp->spid[i] == pid) {
				pid = Cp->tpid[i];
				break;
			}
		}
		if (i >= sizeof(Cp->tpid)/sizeof(Cp->tpid[0]))
			err = 0;	/* don't attempt retry of open() */
		else {	/* wait for controlling process to terminate */
			(void) sighold(SIGALRM);
			while (pid && Cp->tpid[i] == pid) {
				if (kill(pid, SIGUSR1) == -1)
					break;
				timeout = TRUE;
				(void) alarm(1);
				(void) sigpause(SIGALRM);
				(void) sighold(SIGALRM);
				(void) alarm(0);
			}
			(void) sigrelse(SIGALRM);
			timeout = FALSE;
			Ecritical(1);
			if (Cp->tpid[i] == 0)
				Cp->spid[i] = 0;
			Xcritical(1);
		}
	}

	if (err) {	/* prepare to reissue the open() system call */
		UnFlush();	/* don't print the failed open() */
		if (rc && !cflag && prismember(&trace,SYS_open)) { /* last gasp */
			(void) sysentry(Pr);
			(void) printf("%s%s\n", pname, sys_string);
			sys_leng = 0;
			*sys_string = '\0';
		}
#if sparc
		if (sys_indirect) {
			Pr->REG[R_G1] = SYS_syscall;
			Pr->REG[R_O0] = SYS_open;
			for (i = 0; i < 5; i++)
				Pr->REG[R_O1+i] = sys_args[i];
		} else {
			Pr->REG[R_G1] = SYS_open;
			for (i = 0; i < 6; i++)
				Pr->REG[R_O0+i] = sys_args[i];
		}
		Pr->REG[R_nPC] = Pr->REG[R_PC];
#else
		(void) Psetsysnum(Pr, SYS_open);
#endif
		Pr->REG[R_PC] -= sizeof(syscall_t);
		(void) Pputregs(Pr);
	}

	return rc;
}
