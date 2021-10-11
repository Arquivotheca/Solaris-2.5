/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)msgsys.c	1.10	93/10/30 SMI"	/* SVr4.0 1.6.1.7	*/
#ifdef __STDC__
	#pragma weak msgctl = _msgctl
	#pragma weak msgget = _msgget
	#pragma weak msgrcv = _msgrcv
	#pragma weak msgsnd = _msgsnd
#endif
#include	"synonyms.h"
#include	<sys/types.h>
#include	<sys/ipc.h>
#include	<sys/msg.h>
#include	<sys/syscall.h>

#define	MSGGET	0
#define	MSGCTL	1
#define	MSGRCV	2
#define	MSGSND	3

extern long syscall();

int
msgget(key, msgflg)
key_t key;
int msgflg;
{
	return (syscall(SYS_msgsys, MSGGET, key, msgflg));
}

int
msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
	return (syscall(SYS_msgsys, MSGCTL, msqid, cmd, buf));
}

int
msgrcv(msqid, msgp, msgsz, msgtyp, msgflg)
int msqid;
void *msgp;
size_t msgsz;
long msgtyp;
int msgflg;
{
	return (syscall(SYS_msgsys, MSGRCV, msqid, msgp, msgsz, msgtyp,
			msgflg));
}

int
msgsnd(msqid, msgp, msgsz, msgflg)
int msqid;
const void *msgp;
size_t msgsz;
int msgflg;
{
	return (syscall(SYS_msgsys, MSGSND, msqid, msgp, msgsz, msgflg));
}
