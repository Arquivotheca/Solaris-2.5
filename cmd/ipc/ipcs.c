/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ipcs.c	1.14	95/02/24 SMI"	/* SVr4.0  1.12.2.6	*/ 

/*
 * ipcs - IPC status
 *
 * Examine and print certain things about
 * message queues, semaphores and shared memory.
 *
 * As of SVR4, IPC information is obtained via msgctl, semctl and shmctl
 * to the extent possible.  /dev/kmem is used only to obtain configuration
 * information and to determine the IPC identifiers present in the system.
 * This change ensures that the information in each msgid_ds, semid_ds or
 * shmid_ds data structure that we obtain is complete and consistent.
 * For example, the shm_nattch field of a shmid_ds data structure is
 * only guaranteed to be meaningful when obtained via shmctl; when read
 * directly from /dev/kmem, it may contain garbage.
 * If the user supplies an alternate corefile (using -C), no attempt is
 * made to obtain information using msgctl/semctl/shmctl.
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/vnode.h> 
#include <sys/param.h>
#include <sys/var.h>
#include <kvm.h>
#include <nlist.h>
#include <sys/elf.h>
#include <fcntl.h>
#include <time.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define		SYS5	0
#define		SYS3	1

#define	TIME	0
#define	MSG	1
#define	SEM	2
#define	SHM	3
#define	MSGINFO	4
#define	SEMINFO	5
#define	SHMINFO	6

/*
 * Given an index into an IPC table (message table, shared memory
 * table) determine the corresponding IPC
 * identifier (msgid, shmid).  This requires knowledge of
 * the table size, the corresponding ipc_perm structure (for the
 * sequence number contained therein) and the undocumented method
 * by which the kernel assigns new identifiers.
 */
#define IPC_ID(tblsize, index, permp)	((index) + (tblsize)*(permp)->seq)
/*
 * Semaphore id's are generated as 2 16 bit values, the index and sequence
 * number.
 */
#define SEMA_SEQ_SHIFT	16
#define SEM_ID(index, permp)	(((permp)->seq << SEMA_SEQ_SHIFT) + index)

struct nlist nl[] = {		/* name list entries for IPC facilities */
	{"time"},
	{"msgque"},
	{"sema"},
	{"shmem"},
	{"msginfo"},
	{"seminfo"},
	{"shminfo"},
	{NULL}
};

char	chdr[] = "T     ID     KEY        MODE       OWNER    GROUP",
				/* common header format */
	chdr2[] = "  CREATOR   CGROUP",
				/* c option header format */
	*name = NULL,		/* name list file */
	*mem = NULL,		/* memory file */
	opts[] = "abcmopqstC:N:X";/* allowable options for getopt */
extern char	*optarg;	/* arg pointer for getopt */
int		bflg,		/* biggest size:
					segsz on m; qbytes on q; nsems on s */
		cflg,		/* creator's login and group names */
		mflg,		/* shared memory status */
		oflg,		/* outstanding data:
					nattch on m; cbytes, qnum on q */
		pflg,		/* process id's: lrpid, lspid on q;
					cpid, lpid on m */
		qflg,		/* message queue status */
		sflg,		/* semaphore status */
		tflg,		/* times: atime, ctime, dtime on m;
					ctime, rtime, stime on q;
					ctime, otime on s */
		Cflg,		/* user supplied corefile */
		Nflg,		/* user supplied namelist */

		err;		/* option error count */
extern int	optind;		/* option index for getopt */
kvm_t *kd;

union semun {
	int val;
	struct semid_ds *buf;
	ushort *array;
} semarg;

main(argc, argv)
int	argc;	/* arg count */
char	**argv;	/* arg vector */
{
	register int	i,	/* loop control */
			o,	/* option flag */
			n,	/* table size */
			id;	/* IPC identifier */
	time_t		time;	/* date in memory file */
	struct shmid_ds	mds;	/* shared memory data structure */
	struct shminfo shminfo;	/* shared memory information structure */
	struct shmid_ds *pshm;	/* pointer to shared memory */
	struct msqid_ds	qds;	/* message queue data structure */
	struct msginfo msginfo;	/* message information structure */
	struct msqid_ds	*pmsgque; /* pointer to message queue */
	struct semid_ds	sds;	/* semaphore data structure */
	struct seminfo seminfo;	/* semaphore information structure */
	struct semid_ds *psem; /* pointer to semaphore data structures */
	struct var v;		/* tunable parameters for kernel */
	void hp();
	void tp();
	int reade();

	/* Go through the options and set flags. */
	while ((o = getopt(argc, argv, opts)) != EOF)
		switch (o) {
		case 'a':
			bflg = cflg = oflg = pflg = tflg = 1;
			break;
		case 'b':
			bflg = 1;
			break;
		case 'c':
			cflg = 1;
			break;
		case 'C':
			mem = optarg;
			Cflg = 1;
			break;
		case 'm':
			mflg = 1;
			break;
		case 'N':
			name = optarg;
			Nflg = 1;
			break;
		case 'o':
			oflg = 1;
			break;
		case 'p':
			pflg = 1;
			break;
		case 'q':
			qflg = 1;
			break;
		case 's':
			sflg = 1;
			break;
		case 't':
			tflg = 1;
			break;
		case '?':
			err++;
			break;
		}
	if (err || (optind < argc)) {
		fprintf(stderr,
			"usage:  ipcs [-abcmopqstX] [-C corefile] [-N namelist]\n");
		exit(1);
	}

	/*
	 * If the user supplied either the corefile or namelist then
	 * reset the uid/gid to the user invoking this command.
	 */
	if (Cflg || Nflg) {
		setuid(getuid());
		setgid(getgid());
	}

	if ((mflg + qflg + sflg) == 0)
		mflg = qflg = sflg = 1;

	/* Check out namelist and memory files. */
	if ((kd = kvm_open(name, mem, NULL, O_RDONLY, argv[0])) == NULL) {
		exit(1);
	}
	
	if (kvm_nlist(kd, nl) == -1) {
		perror("ipcs: can't read namelist");
		exit(1);
	}
		
	if (nl[TIME].n_value == 0) {
		perror("ipcs:  no namelist");
		exit(1);
	}
	reade(kd, (long)nl[TIME].n_value, &time, sizeof(time));
	printf("IPC status from %s as of %s", mem ? mem : "<running system>",
		ctime(&time));

	/* Print Message Queue status report. */
	if (qflg) {
		unsigned long addr;

		if (nl[MSG].n_value && nl[MSGINFO].n_value &&
		    reade(kd, (long)nl[MSGINFO].n_value, &msginfo,
			  sizeof(msginfo)) &&
		    reade(kd, (long)nl[MSG].n_value, &pmsgque,
			  sizeof(struct msqid_ds *)) && pmsgque != 0) {
			printf("%s%s%s%s%s%s\nMessage Queues:\n", chdr,
			       cflg ? chdr2 : "",
			       oflg ? " CBYTES  QNUM" : "",
			       bflg ? " QBYTES" : "",
			       pflg ? " LSPID LRPID" : "",
			       tflg ?
			       "   STIME    RTIME    CTIME " : "");
			n = msginfo.msgmni;
		} else {
			printf("Message Queue facility not in system.\n");
			n = 0;
		}

		for (i = 0, addr = (unsigned long)pmsgque;
		     i < n;
		     i++, addr += sizeof(qds)) {
			reade(kd, addr, &qds, sizeof(qds));

			if ((qds.msg_perm.mode & IPC_ALLOC) == 0)
				continue;
			id = IPC_ID(n, i, &qds.msg_perm);
			if (!Cflg && msgctl(id, IPC_STAT, &qds) < 0)
				continue;
			hp('q', "SRrw-rw-rw-", &qds.msg_perm, id,SYS5,SYS5);
			if (oflg)
				printf("%7u%6u", qds.msg_cbytes, qds.msg_qnum);
			if (bflg)
				printf("%7u", qds.msg_qbytes);
			if (pflg)
				printf("%6u%6u", qds.msg_lspid, qds.msg_lrpid);
			if (tflg) {
				tp(qds.msg_stime);
				tp(qds.msg_rtime);
				tp(qds.msg_ctime);
			}
			printf("\n");
		}
	}

	/* Print Shared Memory status report. */
	if (mflg) {
		unsigned long addr;

		if (nl[SHM].n_value && nl[SHMINFO].n_value &&
		    reade(kd, (long)nl[SHMINFO].n_value, &shminfo,
			  sizeof(shminfo)) &&
		    reade(kd, (long)nl[SHM].n_value, &pshm,
			  sizeof(struct shmid_ds *)) && pshm != 0) {
			if (oflg || bflg || tflg || !qflg || !nl[MSG].n_value)
				printf("%s%s%s%s%s%s\n", chdr,
				       cflg ? chdr2 : "",
				       oflg ? " NATTCH" : "",
				       bflg ? "  SEGSZ" : "",
				       pflg ? "  CPID  LPID" : "",
				       tflg ? "   ATIME    DTIME    CTIME " : "");
			printf("Shared Memory:\n");
			n = shminfo.shmmni;

			/* Seek to actual shared memory data structures */
		} else {
			printf("Shared Memory facility not in system.\n");
			n = 0;
		}
		
		for (i = 0, addr = (unsigned long)pshm;
		     i < n;
		     i++, addr += sizeof(mds)) {
			reade(kd, addr, &mds, sizeof(mds));
			if ((mds.shm_perm.mode & IPC_ALLOC) == 0)
				continue;
			id = IPC_ID(n, i, &mds.shm_perm);
			if (!Cflg && shmctl(id, IPC_STAT, &mds) < 0)
				continue;
			hp('m', "DCrw-rw-rw-", &mds.shm_perm, id,SYS5,SYS5);
			if (oflg)
				printf("%7u", mds.shm_nattch);
			if (bflg)
				printf("%7d", mds.shm_segsz);
			if (pflg)
				printf("%6u%6u", mds.shm_cpid, mds.shm_lpid);
			if (tflg) {
				tp(mds.shm_atime);
				tp(mds.shm_dtime);
				tp(mds.shm_ctime);
			}
			printf("\n");
		}

	}

	/* Print Semaphore facility status. */
	if (sflg) {
		unsigned long addr;

		if (nl[SEM].n_value && nl[SEMINFO].n_value &&
		    reade(kd, (long)nl[SEMINFO].n_value, &seminfo,
			  sizeof(seminfo)) &&
		    reade(kd, (long)nl[SEM].n_value, &psem,
			  sizeof(long)) && psem != 0) {
			if (bflg || tflg || (!qflg || !nl[MSG].n_value) &&
			    (!mflg || !nl[SHM].n_value))
				printf("%s%s%s%s\n", chdr,
				       cflg ? chdr2 : "",
				       bflg ? " NSEMS" : "",
				       tflg ? "   OTIME    CTIME " : "");
			printf("Semaphores:\n");
			n = seminfo.semmni;

			/* Seek to actual semaphore set data structures */
		} else {
			printf("Semaphore facility not in system.\n");
			n = 0;
		}

		for (i = 0, addr = (unsigned long)psem;
		     i < n;
		     i++, addr += sizeof(sds)) {
			reade(kd, addr, &sds, sizeof(sds));
			if ((sds.sem_perm.mode & IPC_ALLOC) == 0)
				continue;
			id = SEM_ID(i, &sds.sem_perm);
			semarg.buf = &sds;
			if (!Cflg && semctl(id, 0, IPC_STAT, semarg) < 0)
				continue;
			hp('s', "--ra-ra-ra-", &sds.sem_perm, id, SYS5, SYS5);
			if (bflg)
				printf("%6u", sds.sem_nsems);
			if (tflg) {
				tp(sds.sem_otime);
				tp(sds.sem_ctime);
			}
			printf("\n");
		}
	}
	exit(0);
	/*NOTREACHED*/
}

/*
**	reade - read with error exit
*/

reade(kd, addr, b, s)
kvm_t *kd;
unsigned long addr;
void	*b;	/* buffer address */
size_t	s;	/* size */
{
	if (kvm_read(kd, addr, (char *)b, s) != s) {
		perror("ipcs:  read error");
		exit(1);
	}
	return (1);
}

/*
**	hp - common header print
*/

void
hp(type, modesp, permp, slot, slots, sys3)
char				type,	/* facility type */
				*modesp;/* ptr to mode replacement characters */
register struct ipc_perm	*permp;	/* ptr to permission structure */
int				slot,	/* facility slot number */
                                slots;	/* # of facility slots */
int				sys3;	/* system 5 vs. system 3 */
{
	register int		i;	/* loop control */
	register struct group	*g;	/* ptr to group group entry */
	register struct passwd	*u;	/* ptr to user passwd entry */

        if (sys3){
		printf("%c%s%s", type, "    x	  ", "xenix    ");
	}
	else {
		printf("%c%7d%s%#8.8x ", type, slot,
			permp->key ? " " : " 0x", permp->key);
	}
	for (i = 02000; i; modesp++, i >>= 1)
		printf("%c", ((int)permp->mode & i) ? *modesp : '-');
	if ((u = getpwuid(permp->uid)) == NULL)
		printf("%9d", permp->uid);
	else
		printf("%9.8s", u->pw_name);
	if ((g = getgrgid(permp->gid)) == NULL)
		printf("%9d", permp->gid);
	else
		printf("%9.8s", g->gr_name);
	if (cflg) {
		if ((u = getpwuid(permp->cuid)) == NULL)
			printf("%9d", permp->cuid);
		else
			printf("%9.8s", u->pw_name);
		if ((g = getgrgid(permp->cgid)) == NULL)
			printf("%9d", permp->cgid);
		else
			printf("%9.8s", g->gr_name);
	}
}

/*
**	tp - time entry printer
*/

void
tp(time)
time_t	time;	/* time to be displayed */
{
	register struct tm *t;	/* ptr to converted time */

	if (time) {
		t = localtime(&time);
		printf(" %2d:%2.2d:%2.2d", t->tm_hour, t->tm_min, t->tm_sec);
	} else
		printf(" no-entry");
}
