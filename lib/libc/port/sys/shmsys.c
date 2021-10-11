/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)shmsys.c	1.9	93/10/30 SMI"	/* SVr4.0 1.7.1.5	*/

#ifdef __STDC__
	#pragma weak shmat = _shmat
	#pragma weak shmctl = _shmctl
	#pragma weak shmdt = _shmdt
	#pragma weak shmget = _shmget
#endif
#include	"synonyms.h"
#include	<sys/types.h>
#include	<sys/ipc.h>
#include	<sys/shm.h>
#include	<sys/syscall.h>


#define	SHMAT	0
#define	SHMCTL	1
#define	SHMDT	2
#define	SHMGET	3

extern long syscall();

VOID *
shmat(shmid, shmaddr, shmflg)
int shmid;
const VOID *shmaddr;
int shmflg;
{
	return ((char *)syscall(SYS_shmsys, SHMAT, shmid, shmaddr, shmflg));
}

int
shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
	return (syscall(SYS_shmsys, SHMCTL, shmid, cmd, buf));
}

int
shmdt(shmaddr)
char *shmaddr;
{
	return (syscall(SYS_shmsys, SHMDT, shmaddr));
}

int
shmget(key, size, shmflg)
key_t key;
int size, shmflg;
{
	return (syscall(SYS_shmsys, SHMGET, key, size, shmflg));
}
