#pragma ident	"@(#)semsys.c	1.11	94/01/19 SMI" 

#include	<syscall.h>
#include	<varargs.h>
#include	<sys/types.h>
#include	<sys/ipc.h>
#include	<sys/sem.h>

/* semsys dispatch argument */
#define SEMCTL  0
#define SEMGET  1
#define SEMOP   2

/*VARARGS3*/
semctl(semid, semnum, cmd, arg)
int semid, cmd;
int semnum;
union semun arg;
{
	switch (cmd) {

	case IPC_STAT:
	case IPC_SET:
		cmd += 10;
		/* fall-through */
	case SETVAL:
	case GETALL:
	case SETALL:
		return(_syscall(SYS_semsys,SEMCTL,semid,semnum,cmd,arg.val));

	case IPC_RMID:
		cmd += 10;
		/* fall-through */
	default:
		return(_syscall(SYS_semsys,SEMCTL,semid,semnum,cmd,0));
	}
}

semget(key, nsems, semflg)
key_t key;
int nsems, semflg;
{
	return(_syscall(SYS_semsys, SEMGET, key, nsems, semflg));
}

semop(semid, sops, nsops)
int semid;
struct sembuf (*sops)[];
int nsops;
{
	return(_syscall(SYS_semsys, SEMOP, semid, sops, nsops));
}

semsys(sysnum, va_alist)
int sysnum;
va_dcl
{
	va_list ap;
	int semid, cmd;
	int semnum, val;
	union semun arg;
	key_t key;
	int nsems, semflg;
	struct sembuf *sops;
	int nsops;

	va_start(ap);
	switch (sysnum) {
	case SEMCTL:
		semid=va_arg(ap, int);
		semnum=va_arg(ap, int);
		cmd=va_arg(ap, int);
		val=va_arg(ap, int);
		if ((cmd == IPC_STAT) || (cmd == IPC_SET) || (cmd == IPC_RMID))
			cmd += 10;
		return(_syscall(SYS_semsys, SEMCTL, semid, semnum, cmd, val));
	case SEMGET:
		key=va_arg(ap, key_t);
		nsems=va_arg(ap, int);
		semflg=va_arg(ap, int);
		return(semget(key, nsems, semflg));
	case SEMOP:
		semid=va_arg(ap, int);
		sops=va_arg(ap, struct sembuf *);
		nsops=va_arg(ap, int);
		return(semop(semid, sops, nsops));
	}
}

