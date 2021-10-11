#pragma ident	"@(#)msgsys.c	1.9	93/08/18 SMI" 

#include	<syscall.h>
#include 	<varargs.h>
#include	<sys/types.h>
#include	<sys/ipc.h>
#include	<sys/msg.h>


/* msgsys dispatch argument */
#define	MSGGET	0
#define	MSGCTL	1
#define	MSGRCV	2
#define	MSGSND	3


msgget(key, msgflg)
key_t key;
int msgflg;
{
	return(_syscall(SYS_msgsys, MSGGET, key, msgflg));
}

msgctl(msqid, cmd, buf)
int msqid, cmd;
struct msqid_ds *buf;
{
	return(_syscall(SYS_msgsys, MSGCTL, msqid, cmd, buf));
}

msgrcv(msqid, msgp, msgsz, msgtyp, msgflg)
int msqid;
struct msgbuf *msgp;
int msgsz;
long msgtyp;
int msgflg;
{
	return(_syscall(SYS_msgsys, MSGRCV, msqid, msgp, msgsz, msgtyp, msgflg));
}

msgsnd(msqid, msgp, msgsz, msgflg)
int msqid;
struct msgbuf *msgp;
int msgsz, msgflg;
{
	return(_syscall(SYS_msgsys, MSGSND, msqid, msgp, msgsz, msgflg));
}


msgsys(sysnum, va_alist)
int sysnum;
va_dcl
{
	va_list ap;
	key_t key;
	int msgflg;
	int msgflag;
	int msqid, cmd;
	struct msqid_ds *buf;
	struct msgbuf *msgp;
	int msgsz;
	long msgtyp;


	va_start(ap);
	switch (sysnum) {
	case MSGGET:
		key=va_arg(ap, key_t);
		msgflag=va_arg(ap, int);
		return(msgget(key, msgflag));
	case MSGCTL:
		msqid=va_arg(ap, int);
		cmd=va_arg(ap, int);
		buf=va_arg(ap, struct msqid_ds *);
		return(msgctl(msqid, cmd, buf));
	case MSGRCV:
		msqid=va_arg(ap, int);
		msgp=va_arg(ap, struct msgbuf *);
		msgsz=va_arg(ap, int);
		msgtyp=va_arg(ap, long);
		msgflg=va_arg(ap, int);
		return(msgrcv(msqid, msgp, msgsz, msgtyp, msgflg));
	case MSGSND:
		msqid=va_arg(ap, int);
		msgp=va_arg(ap, struct msgbuf *);
		msgsz=va_arg(ap, int);
		msgflg=va_arg(ap, int);
		return(msgsnd(msqid, msgp, msgsz, msgflg));
	}
}
