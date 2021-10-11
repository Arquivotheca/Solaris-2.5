/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_MSG_H
#define	_SYS_MSG_H

#pragma ident	"@(#)msg.h	1.21	94/05/02 SMI"	/* SVr4.0 11.14	*/

#include <sys/ipc.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * IPC Message Facility.
 */

/*
 * Implementation Constants.
 */

/*
 * Permission Definitions.
 */

#define	MSG_R	0400	/* read permission */
#define	MSG_W	0200	/* write permission */

/*
 * ipc_perm Mode Definitions.
 */

#define	MSG_RWAIT	01000	/* a reader is waiting for a message */
#define	MSG_WWAIT	02000	/* a writer is waiting to send */

/*
 * Message Operation Flags.
 */

#define	MSG_NOERROR	010000	/* no error if big message */

/*
 * There is one msg queue id data structure for each q in the system.
 */

/*
 * Applications that read /dev/mem must be built like the kernel. A new
 * symbol "_KMEMUSER" is defined for this purpose.
 */

#if defined(_KERNEL) || defined(_KMEMUSER)

#include <sys/t_lock.h>

/* expanded msqid_ds structure */

struct msqid_ds {
	struct ipc_perm msg_perm;	/* operation permission struct */
	struct msg	*msg_first;	/* ptr to first message on q */
	struct msg	*msg_last;	/* ptr to last message on q */
	ulong		msg_cbytes;	/* current # bytes on q */
	ulong		msg_qnum;	/* # of messages on q */
	ulong		msg_qbytes;	/* max # of bytes on q */
	pid_t		msg_lspid;	/* pid of last msgsnd */
	pid_t		msg_lrpid;	/* pid of last msgrcv */
	time_t		msg_stime;	/* last msgsnd time */
	long		msg_pad1;	/* reserved for time_t expansion */
	time_t		msg_rtime;	/* last msgrcv time */
	long		msg_pad2;	/* time_t expansion */
	time_t		msg_ctime;	/* last change time */
	long		msg_pad3;	/* time expansion */
	kcondvar_t	msg_cv;
	kcondvar_t	msg_qnum_cv;
	long		msg_pad4[3];	/* reserve area */
};

/* SVR3 structure */

struct o_msqid_ds {
	struct o_ipc_perm msg_perm;	/* operation permission struct */
	struct msg	*msg_first;	/* ptr to first message on q */
	struct msg	*msg_last;	/* ptr to last message on q */
	ushort_t	msg_cbytes;	/* current # bytes on q */
	ushort_t	msg_qnum;	/* # of messages on q */
	ushort_t	msg_qbytes;	/* max # of bytes on q */
	o_pid_t		msg_lspid;	/* pid of last msgsnd */
	o_pid_t		msg_lrpid;	/* pid of last msgrcv */
	time_t		msg_stime;	/* last msgsnd time */
	time_t		msg_rtime;	/* last msgrcv time */
	time_t		msg_ctime;	/* last change time */
};

/*
 * There is one msg structure for each message that may be in the system.
 */

struct msg {
	struct msg	*msg_next;	/* ptr to next message on q */
	long		msg_type;	/* message type */
	ushort_t	msg_ts;		/* message text size */
	short		msg_spot;	/* message text map address */
};
#else	/* user definition */

/* this maps to the kernel struct msgid_ds */

typedef unsigned long msgqnum_t;
typedef unsigned long msglen_t;

struct msqid_ds {
	struct ipc_perm	msg_perm;	/* operation permission struct */
	struct msg	*msg_first;	/* ptr to first message on q */
	struct msg	*msg_last;	/* ptr to last message on q */
	ulong_t		msg_cbytes;	/* current # bytes on q */
	msgqnum_t	msg_qnum;	/* # of messages on q */
	msglen_t	msg_qbytes;	/* max # of bytes on q */
	pid_t		msg_lspid;	/* pid of last msgsnd */
	pid_t		msg_lrpid;	/* pid of last msgrcv */
	time_t		msg_stime;	/* last msgsnd time */
	long		msg_pad1;	/* reserved for time_t expansion */
	time_t		msg_rtime;	/* last msgrcv time */
	long		msg_pad2;	/* time_t expansion */
	time_t		msg_ctime;	/* last change time */
	long		msg_pad3;	/* time_t expansion */
	short		msg_cv;
	short		msg_qnum_cv;
	long		msg_pad4[3];	/* reserve area */
};

/*
 * There is one msg structure for each message that may be in the system.
 * XXX - a user version of this should not be necessary, but is being kept
 * XXX - for compatibility.
 */

struct msg {
	struct msg	*msg_next;	/* ptr to next message on q */
	long		msg_type;	/* message type */
	ushort_t	msg_ts;		/* message text size */
	short		msg_spot;	/* message text map address */
};
#endif  /* defined(_KERNEL) */

/*
 * User message buffer template for msgsnd and msgrecv system calls.
 */

#ifdef _KERNEL
struct ipcmsgbuf {
#else
struct msgbuf {
#endif /* _KERNEL */
#if defined(_XOPEN_SOURCE)
	long	_mtype;		/* message type */
	char	_mtext[1];	/* message text */
#else
	long	mtype;		/* message type */
	char	mtext[1];	/* message text */
#endif
};

/*
 * Message information structure.
 */

struct msginfo {
	int		msgmap;	/* # of entries in msg map */
	int		msgmax;	/* max message size */
	int		msgmnb;	/* max # bytes on queue */
	int		msgmni;	/* # of message queue identifiers */
	int		msgssz;	/* msg segment size (should be word size */
				/* multiple) */
	int		msgtql;	/* # of system message headers */
	ushort_t	msgseg;	/* # of msg segments (MUST BE < 32768) */
};

/*
 * We have to be able to lock a message queue since we can
 * sleep during message processing due to a page fault in
 * copyin/copyout or iomove.  We cannot add anything to the
 * msqid_ds structure since this is used in user programs
 * and any change would break object file compatibility.
 * Therefore, we allocate a parallel array, msglock, which
 * is used to lock a message queue.  The array is defined
 * in the msg master file.  The following macro takes a
 * pointer to a message queue and returns a pointer to the
 * lock entry.  The argument must be a pointer to a msgqid
 * structure.
 */

#define	MSGLOCK(X)	&msglock[X - msgque]

#if !defined(_KERNEL)
#if defined(__STDC__)
int msgctl(int, int, struct msqid_ds *);
int msgget(key_t, int);
int msgrcv(int, void *, size_t, long, int);
int msgsnd(int, const void *, size_t, int);
#else /* __STDC __ */
int msgctl();
int msgget();
int msgrcv();
int msgsnd();
#endif /* __STDC __ */
#endif /* ! _KERNEL */

#ifdef _KERNEL

/*
 *	Configuration Parameters
 * These parameters are tuned by editing the system configuration file.
 * The following lines establish the default values.
 */
#ifndef	MSGPOOL
#define	MSGPOOL	8	/* size, in kilobytes, of message pool */
#endif
#ifndef	MSGMNB
#define	MSGMNB	2048	/* default max number of bytes on a queue */
#endif
#ifndef	MSGMNI
#define	MSGMNI	50	/* number of message queue identifiers */
#endif
#ifndef	MSGTQL
#define	MSGTQL	50	/* # of system message headers */
#endif

/* The following parameters are assumed not to require tuning */
#ifndef	MSGMAP
#define	MSGMAP	100	/* number of entries in msg map */
#endif
#ifndef	MSGMAX
#define	MSGMAX	(MSGPOOL * 1024)	/* max message size (in bytes) */
#endif
#ifndef	MSGSSZ
#define	MSGSSZ	8	/* msg segment size (should be word size multiple) */
#endif
#define	MSGSEG	((MSGPOOL * 1024) / MSGSSZ) /* # segments (MUST BE < 32768) */

#ifdef sun

struct msglock {
	char msglock_lock;
	kcondvar_t msglock_cv;
};

/*
 * Defined in space.c, allocated/initialized in msg.c
 */
extern caddr_t		msg;		/* base address of message buffer */
extern struct map	*msgmap;	/* msg allocation map */
extern struct msg	*msgh;		/* message headers */
extern struct msqid_ds	*msgque;	/* msg queue headers */
extern struct msglock	*msglock; 	/* locks for the message queues */
extern struct msg	*msgfp;		/* ptr to head of free header list */
extern struct msginfo	msginfo;	/* message parameters */

#endif /* sun */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MSG_H */
