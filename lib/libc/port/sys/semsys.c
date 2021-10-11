/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)semsys.c	1.11	94/09/06 SMI"	/* SVr4.0 1.6.1.6	*/

#ifdef __STDC__
	#pragma weak semctl = _semctl
	#pragma weak semget = _semget
	#pragma weak semop = _semop
#endif
#include	"synonyms.h"
#include	<sys/types.h>
#include	<sys/ipc.h>
#include	<sys/sem.h>
#include	<sys/syscall.h>

#define	SEMCTL	0
#define	SEMGET	1
#define	SEMOP	2

extern long syscall();

union semun {
	int val;
	struct semid_ds *buf;
	ushort *array;
};

#ifdef __STDC__

	#include <stdarg.h>

	/*
	 * XXX The code in common/os/semsys.c expects a struct containing the
	 * XXX "value" of the semun argument, but the compiler passes a pointer
	 * XXX to it, since it is a union.  So, we convert here and pass the
	 * XXX value, but to keep the naive user from being penalized for the
	 * XXX counterintuitive behaviour of the compiler, we ignore the union
	 * XXX if it will not be used by the system call (to protect the caller
	 * XXX from SIGSEGVs.  e.g. semctl(semid, semnum, cmd, NULL);  which
	 * XXX would otherwise always result in a segmentation violation)
	 * XXX We do this partly for consistency, since the ICL port did it
	 */

	int
	semctl(int semid, int semnum, int cmd, ...)
	{
		int arg;
		va_list ap;

		switch(cmd) {
		case GETVAL:
		case GETPID:
		case GETZCNT:
		case GETNCNT:
		case IPC_RMID:
			arg = 0;
			break;
		default:
			va_start(ap,cmd);
			arg = va_arg(ap,union semun).val;
			va_end(ap);
			break;
		}

		return(syscall(SYS_semsys, SEMCTL, semid, semnum, cmd, arg));
	}

#else	/* pre-ANSI version */

	int
	semctl(semid, semnum, cmd, arg)
	int semid, semnum, cmd;
	union semun arg;
	{
		int argval;

		switch(cmd) {
		case GETVAL:
		case GETPID:
		case GETZCNT:
		case IPC_RMID:
			argval = 0;
			break;
		default:
			argval = arg.val;
			break;
		}
		return(syscall(SYS_semsys, SEMCTL, semid, semnum, cmd, argval));
	}

#endif	/* __STDC__ */

int
semget(key, nsems, semflg)
key_t key;
int nsems, semflg;
{
	return(syscall(SYS_semsys, SEMGET, key, nsems, semflg));
}

int
semop(semid, sops, nsops)
int semid;
struct sembuf *sops;
unsigned int nsops;
{
	return(syscall(SYS_semsys, SEMOP, semid, sops, nsops));
}

