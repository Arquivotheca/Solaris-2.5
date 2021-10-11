/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_STREAM_H
#define	_SYS_STREAM_H

#pragma ident	"@(#)stream.h	1.56	94/09/28 SMI"	/* SVr4.0 11.44	*/

/*
 * For source compatibility
 */
#include <sys/vnode.h>
#include <sys/poll.h>
#include <sys/strmdep.h>
#include <sys/cred.h>
#include <sys/t_lock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Both the queue and the data block structures have embedded
 * mutexes: q_lock and db_lock.  In general, these mutexes protect
 * the consistency of the data structure.  In addition, q_lock
 * is used, with the stream head condition variable, to implement
 * a monitor at the stream head for blocking reads and writes.
 * Neither of these locks should be acquired by module or driver code.
 *
 * In particular, q_lock protects the following fields:
 *	q_first
 *	q_last
 *	q_link
 *	q_count
 *	q_bandp
 *	q_nband
 *
 * q_next is protected by stream head locking; this links the
 * stream together, and should be thought of as part of the stream
 * data structure, not the queue.  This is also true for q_strshare,
 * which is a back-reference to the stream-level locking.
 *
 * The remaining fields are either modified only by the allocator,
 * which has exclusive access to them at that time, or are not to
 * be touched by anything but the MT synchronization primitives
 * (i.e. q_wait).
 *
 * q_draining is special - it is protected by SQLOCK(q->syncq).
 */

/*
 * Data queue
 */
typedef struct	queue {
	struct	qinit	*q_qinfo;	/* procs and limits for queue */
	struct	msgb	*q_first;	/* first data block */
	struct	msgb	*q_last;	/* last data block */
	struct	queue	*q_next;	/* Q of next stream */
	struct	queue	*q_link;	/* to next Q for scheduling */
	void		*q_ptr;		/* to private data structure */
	ulong		q_count;	/* number of bytes on Q */
	ulong		q_flag;		/* queue state */
	long		q_minpsz;	/* min packet size accepted by */
					/* this module */
	long		q_maxpsz;	/* max packet size accepted by */
					/* this module */
	ulong		q_hiwat;	/* queue high water mark */
	ulong		q_lowat;	/* queue low water mark */
	struct qband	*q_bandp;	/* separate flow information */
	kmutex_t	q_lock;		/* protect data queue */
	struct stdata 	*q_stream;	/* for head locks */
	struct	syncq	*q_syncq;	/* sync queue object */
	unsigned char	q_nband;	/* number of priority bands > 0 */
	kcondvar_t	q_wait;		/* read/write sleeps */
	kcondvar_t	q_sync;		/* open/close sleeps */
	struct	queue	*q_nfsrv;	/* next q fwd with service routine */
	struct	queue	*q_nbsrv;	/* next q back with service routine */
	ushort		q_draining;	/* locked by SQLOCK - drain_syncq */
	short		q_struiot;	/* stream uio type for struio() */
} queue_t;

/*
 * Queue flags
 */
#define	QENAB	0x001			/* Queue is already enabled to run */
#define	QWANTR	0x002			/* Someone wants to read Q */
#define	QWANTW	0x004			/* Someone wants to write Q */
#define	QFULL	0x008			/* Q is considered full */
#define	QREADR	0x010			/* This is the reader (first) Q */
#define	QUSE	0x020			/* This queue in use (allocation) */
#define	QNOENB	0x040			/* Don't enable Q via putq */
#define	QOLD	0x080			/* Pre-SVR4 open/close interface */
#define	QBACK	0x100			/* queue has been back-enabled */
#define	QHLIST	0x200			/* sd_wrq is on "scanqhead" */
#define	QUNSAFE	0x400			/* for quick check of unsafe */
#define	QPAIR	0x800			/* per queue-pair syncq */
#define	QPERQ 	0x1000			/* per queue-instance syncq */
#define	QPERMOD	0x2000			/* per module syncq */
#define	QMTSAFE	0x4000			/* stream module is MT-safe */
#define	QMTOUTPERIM 0x8000		/* Has outer perimeter */
#define	QMT_TYPEMASK	(QUNSAFE|QPAIR|QPERQ|QPERMOD|QMTSAFE|QMTOUTPERIM)
					/* all MT type flags */
#define	QINSERVICE 0x10000		/* service routine executing */
#define	QWCLOSE	0x20000			/* will not be enabled */
#define	QEND	0x40000			/* last queue in stream */
#define	QWANTWSYNC 0x80000		/* Streamhead wants to write Q */
#define	QSYNCSTR 0x100000		/* Q supports Synchronous STREAMS */
#define	QISDRV 0x200000			/* the Queue is attached to a driver */

/*
 * Structure that describes the separate information
 * for each priority band in the queue.
 */
typedef struct qband {
	struct qband	*qb_next;	/* next band's info */
	ulong		qb_count;	/* number of bytes in band */
	struct msgb	*qb_first;	/* beginning of band's data */
	struct msgb	*qb_last;	/* end of band's data */
	ulong		qb_hiwat;	/* high water mark for band */
	ulong		qb_lowat;	/* low water mark for band */
	ulong		qb_flag;	/* see below */
} qband_t;

/*
 * qband flags
 */
#define	QB_FULL		0x01		/* band is considered full */
#define	QB_WANTW	0x02		/* Someone wants to write to band */
#define	QB_BACK		0x04		/* queue has been back-enabled */

/*
 * Maximum number of bands.
 */
#define	NBAND	256

/*
 * Fields that can be manipulated through strqset() and strqget().
 */
typedef enum qfields {
	QHIWAT	= 0,		/* q_hiwat or qb_hiwat */
	QLOWAT	= 1,		/* q_lowat or qb_lowat */
	QMAXPSZ	= 2,		/* q_maxpsz */
	QMINPSZ	= 3,		/* q_minpsz */
	QCOUNT	= 4,		/* q_count or qb_count */
	QFIRST	= 5,		/* q_first or qb_first */
	QLAST	= 6,		/* q_last or qb_last */
	QFLAG	= 7,		/* q_flag or qb_flag */
	QSTRUIOT = 8,		/* q_struiot */
	QBAD	= 9
} qfields_t;

/*
 * Module information structure
 */
struct module_info {
	ushort	mi_idnum;		/* module id number */
	char 	*mi_idname;		/* module name */
	long	mi_minpsz;		/* min packet size accepted */
	long	mi_maxpsz;		/* max packet size accepted */
	ulong	mi_hiwat;		/* hi-water mark */
	ulong 	mi_lowat;		/* lo-water mark */
};

/*
 * queue information structure (with Synchronous STREAMS extensions)
 */
struct	qinit {
	int	(*qi_putp)();		/* put procedure */
	int	(*qi_srvp)();		/* service procedure */
	int	(*qi_qopen)();		/* called on startup */
	int	(*qi_qclose)();		/* called on finish */
	int	(*qi_qadmin)();		/* for future use */
	struct module_info *qi_minfo;	/* module information structure */
	struct module_stat *qi_mstat;	/* module statistics structure */
	int	(*qi_rwp)();		/* r/w procedure */
	int	(*qi_infop)();		/* information procedure */
	int	qi_struiot;		/* stream uio type for struio() */
};

/*
 * Values for qi_struiot and q_struiot:
 */
#define	STRUIOT_NONE		-1	/* doesn't support struio() */
#define	STRUIOT_DONTCARE	0	/* use current uiomove() (default) */
#define	STRUIOT_STANDARD	1	/* use standard uiomove() */
#define	STRUIOT_IP		2	/* use IP uioipcopy{in,out}() */

/*
 * Streamtab (used in cdevsw and fmodsw to point to module or driver)
 */

struct streamtab {
	struct qinit *st_rdinit;
	struct qinit *st_wrinit;
	struct qinit *st_muxrinit;
	struct qinit *st_muxwinit;
};

/*
 * Structure sent to mux drivers to indicate a link.
 */

struct linkblk {
	queue_t *l_qtop;	/* lowest level write queue of upper stream */
				/* (set to NULL for persistent links) */
	queue_t *l_qbot;	/* highest level write queue of lower stream */
	int	l_index;	/* index for lower stream. */
};

/*
 * Esballoc data buffer freeing routine
 */
typedef struct free_rtn {
	void (*free_func)();
	char *free_arg;
	struct	free_rtn	*free_next;
	int free_flags;
} frtn_t;

/*
 *  Data block descriptor
 */

/*
 * the db_ref field of the datab is protected by the mutex strmsglock.
 * this field should not be modified without holding this lock.
 * the other fields are set only at allocation time, when the
 * allocator has exclusive access.
 *
 * NOTE - for performance reasons, it is important that the size of
 * msgb + datab be a multiple of sizeof(double). See "stream.c".
 */
typedef struct datab {
	struct free_rtn	*db_frtnp;
	unsigned char	*db_base;
	unsigned char	*db_lim;
	unsigned char	db_ref;
	unsigned char	db_type;
	unsigned char	db_refmin;
	unsigned char	db_struioflag;
	void		*db_cache;	/* kernel memory cache descriptor */
	unsigned char	*db_struiobase;
	unsigned char	*db_struiolim;
	unsigned char	*db_struioptr;
	union {
		unsigned char data[8];
		/*
		 * Union used for future extensions (pointer to data ?).
		 */
	} db_struioun;
} dblk_t;

/*
 * Message block descriptor
 */
typedef struct	msgb {
	struct	msgb	*b_next;
	struct  msgb	*b_prev;
	struct	msgb	*b_cont;
	unsigned char	*b_rptr;
	unsigned char	*b_wptr;
	struct datab 	*b_datap;
	unsigned char	b_band;
	unsigned short	b_flag;
	queue_t		*b_queue;	/* for sync queues */
} mblk_t;

/*
 * db_struioflag values:
 */
#define	STRUIO_SPEC	0x01		/* struio{get,put}() special mblk */
#define	STRUIO_DONE	0x02		/* struio done (could be partial) */
#define	STRUIO_IP	0x04		/* IP checksum stored in db_struioun */


/*
 * Message flags.  These are interpreted by the stream head.
 */
#define	MSGMARK		0x01		/* last byte of message is "marked" */
#define	MSGNOLOOP	0x02		/* don't loop message around to */
					/* write side of stream */
#define	MSGDELIM	0x04		/* message is delimited */
#define	MSGNOGET	0x08		/* getq does not return message */

/*
 * Streams message types.
 */

/*
 * Data and protocol messages (regular and priority)
 */
#define	M_DATA		0x00		/* regular data */
#define	M_PROTO		0x01		/* protocol control */

/*
 * Control messages (regular and priority)
 */
#define	M_BREAK		0x08		/* line break */
#define	M_PASSFP	0x09		/* pass file pointer */
#define	M_EVENT		0x0a		/* post an event to an event queue */
#define	M_SIG		0x0b		/* generate process signal */
#define	M_DELAY		0x0c		/* real-time xmit delay (1 param) */
#define	M_CTL		0x0d		/* device-specific control message */
#define	M_IOCTL		0x0e		/* ioctl; set/get params */
#define	M_SETOPTS	0x10		/* set various stream head options */
#define	M_RSE		0x11		/* reserved for RSE use only */
#ifdef _VPIX
#define	M_VPIXINT	0x12		/* send pseudorupt to vpix process */
#endif

/*
 * Control messages (high priority; go to head of queue)
 */
#define	M_IOCACK	0x81		/* acknowledge ioctl */
#define	M_IOCNAK	0x82		/* negative ioctl acknowledge */
#define	M_PCPROTO	0x83		/* priority proto message */
#define	M_PCSIG		0x84		/* generate process signal */
#define	M_READ		0x85		/* generate read notification */
#define	M_FLUSH		0x86		/* flush your queues */
#define	M_STOP		0x87		/* stop transmission immediately */
#define	M_START		0x88		/* restart transmission after stop */
#define	M_HANGUP	0x89		/* line disconnect */
#define	M_ERROR		0x8a		/* fatal error used to set u.u_error */
#define	M_COPYIN	0x8b		/* request to copyin data */
#define	M_COPYOUT	0x8c		/* request to copyout data */
#define	M_IOCDATA	0x8d		/* response to M_COPYIN and M_COPYOUT */
#define	M_PCRSE		0x8e		/* reserved for RSE use only */
#define	M_STOPI		0x8f		/* stop reception immediately */
#define	M_STARTI	0x90		/* restart reception after stop */
#define	M_PCEVENT	0x91		/* post an event to an event queue */
/* XXX */
#define	M_UNHANGUP	0x92		/* line reconnect, sigh */

/*
 * Queue message class definitions.
 */
#define	QNORM		0x00		/* normal priority messages */
#define	QPCTL		0x80		/* high priority cntrl messages */

/*
 *  IOCTL structure - this structure is the format of the M_IOCTL message type.
 */

struct iocblk {
	int 	ioc_cmd;		/* ioctl command type */
	cred_t	*ioc_cr;		/* full credentials */
	uint	ioc_id;			/* ioctl id */
	uint	ioc_count;		/* count of bytes in data field */
	int	ioc_error;		/* error code */
	int	ioc_rval;		/* return value  */
	long	ioc_filler[4];		/* reserved for future use */
};

#define	ioc_uid ioc_cr->cr_uid
#define	ioc_gid ioc_cr->cr_gid

/*
 * structure for the M_COPYIN and M_COPYOUT message types.
 */

struct copyreq {
	int	cq_cmd;			/* ioctl command (from ioc_cmd) */
	cred_t	*cq_cr;			/* full credentials */
	uint	cq_id;			/* ioctl id (from ioc_id) */
	caddr_t	cq_addr;		/* address to copy data to/from */
	uint	cq_size;		/* number of bytes to copy */
	int	cq_flag;		/* see below */
	mblk_t *cq_private;		/* private state information */
	long	cq_filler[4];		/* reserved for future use */
};

#define	cq_uid cq_cr->cr_uid
#define	cq_gid cq_cr->cr_gid

/* cq_flag values */

#define	STRCANON	0x01		/* b_cont data block contains */
					/* canonical format specifier */
#define	RECOPY		0x02		/* perform I_STR copyin again, */
					/* this time using canonical */
					/* format specifier */

/*
 * structure for the M_IOCDATA message type.
 */

struct copyresp {
	int	cp_cmd;			/* ioctl command (from ioc_cmd) */
	cred_t	*cp_cr;			/* full credentials */
	uint	cp_id;			/* ioctl id (from ioc_id) */
	caddr_t	cp_rval;		/* status of request: 0 -> success */
					/* 		non-zero -> failure */
	uint	cp_pad1;
	int	cp_pad2;
	mblk_t *cp_private;		/* private state information */
	long	cp_filler[4];
};

#define	cp_uid cp_cr->cr_uid
#define	cp_gid cp_cr->cr_gid

/*
 * Options structure for M_SETOPTS message.  This is sent upstream
 * by a module or driver to set stream head options.
 */

struct stroptions {
	ulong	so_flags;		/* options to set */
	short	so_readopt;		/* read option */
	ushort	so_wroff;		/* write offset */
	long	so_minpsz;		/* minimum read packet size */
	long	so_maxpsz;		/* maximum read packet size */
	ulong	so_hiwat;		/* read queue high water mark */
	ulong	so_lowat;		/* read queue low water mark */
	unsigned char so_band;		/* band for water marks */
	ushort	so_erropt;		/* error option */
};

/* flags for stream options set message */

#define	SO_ALL		0x003f	/* set all old options */
#define	SO_READOPT	0x0001	/* set read option */
#define	SO_WROFF	0x0002	/* set write offset */
#define	SO_MINPSZ	0x0004	/* set min packet size */
#define	SO_MAXPSZ	0x0008	/* set max packet size */
#define	SO_HIWAT	0x0010	/* set high water mark */
#define	SO_LOWAT	0x0020	/* set low water mark */
#define	SO_MREADON	0x0040	/* set read notification ON */
#define	SO_MREADOFF	0x0080	/* set read notification OFF */
#define	SO_NDELON	0x0100	/* old TTY semantics for NDELAY reads/writes */
#define	SO_NDELOFF	0x0200	/* STREAMS semantics for NDELAY reads/writes */
#define	SO_ISTTY	0x0400	/* the stream is acting as a terminal */
#define	SO_ISNTTY	0x0800	/* the stream is not acting as a terminal */
#define	SO_TOSTOP	0x1000	/* stop on background writes to this stream */
#define	SO_TONSTOP	0x2000	/* do not stop on background writes to stream */
#define	SO_BAND		0x4000	/* water marks affect band */
#define	SO_DELIM	0x8000	/* messages are delimited */
#define	SO_NODELIM	0x010000	/* turn off delimiters */
#define	SO_STRHOLD	0x020000	/* No longer implemented */
#define	SO_ERROPT	0x040000	/* set error option */

/*
 * Structure for M_EVENT and M_PCEVENT messages.  This is sent upstream
 * by a module or driver to have the stream head generate a call to the
 * General Events subsystem.  It is also contained in the first M_DATA
 * block of an M_IOCTL message for the I_STREV and I_UNSTREV ioctls.
 */
struct str_evmsg {
	long		 sv_event;	/* the event (module-specific) */
	vnode_t		*sv_vp;		/* vnode pointer of event queue */
	long		 sv_eid;	/* same as ev_eid */
	long		 sv_evpri;	/* same as ev_pri */
	long		 sv_flags;	/* same as ev_flags */
	uid_t		 sv_uid;	/* user id of posting process */
	pid_t		 sv_pid;	/* process id of posting process */
	hostid_t	 sv_hostid;	/* host id of posting process */
	long		 sv_pad[4];	/* reserved for future use */
};

/*
 * Structure for rw (read/write) procedure calls. A pointer
 * to a struiod_t is passed as a parameter to the rwnext() call.
 *
 * Note: DEF_IOV_MAX is defined and used as it is in "fs/vncalls.c"
 *	 as there isn't a formal definition of IOV_MAX ???
 */
#define	DEF_IOV_MAX	16

typedef struct struiod {
	mblk_t		*d_mp;		/* pointer to mblk (chain) */
	uio_t		d_uio;		/* uio info */
	iovec_t d_iov[DEF_IOV_MAX];	/* iov referenced by uio */
} struiod_t;

/*
 * Structure for infomation procedure calls.
 */
typedef struct infod {
	unsigned char	d_cmd;		/* info info request command */
	unsigned char	d_res;		/* info info command results */
	int		d_bytes;	/* mblk(s) byte count */
	int		d_count;	/* count of mblk(s) */
	uio_t		*d_uiop;	/* pointer to uio struct */
} infod_t;
/*
 * Values for d_cmd & d_res.
 */
#define	INFOD_FIRSTBYTES	0x02	/* return msgbsize() of first mblk */
#define	INFOD_BYTES		0x04	/* return msgbsize() of all mblk(s) */
#define	INFOD_COUNT		0x08	/* return count of mblk(s) */
#define	INFOD_COPYOUT		0x10	/* copyout any M_DATA mblk(s) */

/*
 * Miscellaneous parameters and flags.
 */

/*
 * New code for two-byte M_ERROR message.
 */
#ifdef _KERNEL
#define	NOERROR	((unsigned char)-1)
#endif	/* _KERNEL */

/*
 * Values for stream flag in open to indicate module open, clone open;
 * return value for failure.
 */
#define	MODOPEN 	0x1		/* open as a module */
#define	CLONEOPEN	0x2		/* clone open; pick own minor dev */
#define	OPENFAIL	-1		/* returned for open failure */

/*
 * Priority definitions for block allocation.
 */
#define	BPRI_LO		1
#define	BPRI_MED	2
#define	BPRI_HI		3

/*
 * Value for packet size that denotes infinity
 */
#define	INFPSZ		-1

/*
 * Flags for flushq()
 */
#define	FLUSHALL	1	/* flush all messages */
#define	FLUSHDATA	0	/* don't flush control messages */

/*
 * Flag for transparent ioctls
 */
#define	TRANSPARENT	(unsigned int)(-1)

/*
 * Sleep priorities for stream io
 */
#define	STIPRI	PZERO+3
#define	STOPRI	PZERO+3

/*
 * Stream head default high/low water marks
 */
#define	STRHIGH 5120
#define	STRLOW	1024

/*
 * Block allocation parameters
 */
#define	MAXIOCBSZ	1024		/* max ioctl data block size */

/*
 * qwriter perimeter types
 */
#define	PERIM_INNER	1		/* The inner perimeter */
#define	PERIM_OUTER	2		/* The outer perimeter */

/*
 * Definitions of Streams macros and function interfaces.
 */

/*
 * Definition of spl function needed to provide critical region protection
 * for streams drivers and modules.
 * This is only to be used for MT unsafe drivers and it will be removed
 * in a future release.
 */
#define	splstr() spltty()

/*
 * canenable - check if queue can be enabled by putq().
 */
#define	canenable(q)	!((q)->q_flag & QNOENB)

/*
 * tracing macros
 */
#ifdef TRACE
#define	QNAME(q)	((q)->q_qinfo->qi_minfo->mi_idname)
#endif

/*
 * Test if data block type is one of the data messages (i.e. not a control
 * message).
 */
#define	datamsg(type) \
		((type) == M_DATA || \
		    (type) == M_PROTO || \
		    (type) == M_PCPROTO || \
		    (type) == M_DELAY)

/*
 * Extract queue class of message block.
 */
#define	queclass(bp) (((bp)->b_datap->db_type >= QPCTL) ? QPCTL : QNORM)

/*
 * Align address on next lower word boundary.
 */
#define	straln(a)	(caddr_t)((long)(a) & ~(sizeof (int)-1))

/*
 * Find the max size of data block.
 */
#define	bpsize(bp) ((unsigned int)(bp->b_datap->db_lim - bp->b_datap->db_base))

/*
 * declarations of common routines
 */

#ifdef _KERNEL

/*
 * This one declaration is for a routine fifo_getinfo, streamio is
 * the only consumer. The function is in fifofs_vnops.c
 */
extern mblk_t *allocb(int, uint);
extern mblk_t *esballoc(unsigned char *, int, int, frtn_t *);
extern int testb(int, uint);
extern int bufcall(uint, int, void (*)(), long);
extern int esbbcall(int, void (*)(), long);
extern void freeb(struct msgb *);
extern void freemsg(mblk_t *);
extern mblk_t *dupb(mblk_t *);
extern mblk_t *dupmsg(mblk_t *);
extern mblk_t *copyb(mblk_t *);
extern mblk_t *copymsg(mblk_t *);
extern void linkb(mblk_t *, mblk_t *);
extern mblk_t *unlinkb(mblk_t *);
extern mblk_t *rmvb(mblk_t *, mblk_t *);
extern int pullupmsg(struct msgb *, int);
extern mblk_t *msgpullup(struct msgb *, int);
extern int adjmsg(struct msgb *, int);
extern int msgdsize(struct msgb *);
extern mblk_t *getq(queue_t *);
extern void rmvq(queue_t *, mblk_t *);
extern void flushq(queue_t *, int);
extern void flushband(queue_t *, unsigned char, int);
extern int canput(queue_t *);
extern int bcanput(queue_t *, unsigned char);
extern int canputnext(queue_t *);
extern int bcanputnext(queue_t *, unsigned char);
extern int putq(queue_t *, mblk_t *);
extern int putbq(queue_t *, mblk_t *);
extern int insq(queue_t *, mblk_t *, mblk_t *);
extern void put(queue_t *, mblk_t *);
extern int putnext(queue_t *, mblk_t *);

extern int putctl(queue_t *, int);
extern int putctl1(queue_t *, int, int);
extern int putnextctl(queue_t *, int);
extern int putnextctl1(queue_t *, int, int);
extern queue_t *backq(queue_t *);
extern void qreply(queue_t *, mblk_t *);
extern void qenable(queue_t *);
extern int qsize(queue_t *);
extern void noenable(queue_t *);
extern void enableok(queue_t *);
extern int strqset(queue_t *, qfields_t, unsigned char, long);
extern int strqget(queue_t *, qfields_t, unsigned char, long *);
extern void unbufcall(int);
extern void qprocson(queue_t *);
extern void qprocsoff(queue_t *);
extern void freezestr(queue_t *);
extern void unfreezestr(queue_t *);
extern void qwait(queue_t *);
extern int qwait_sig(queue_t *);
extern void qwriter(queue_t *q, mblk_t *mp, void (*fun)(), int perim);
extern int qtimeout(queue_t *q, void (*fun)(), caddr_t arg, long tim);
extern int qbufcall(queue_t *, uint, int, void (*)(), long);
extern int quntimeout(queue_t *q, int id);
extern void qunbufcall(queue_t *q, int id);
extern int strwakeq(queue_t *, int);
extern int struioput(queue_t *, mblk_t *, struiod_t *);
extern int struioget(queue_t *, mblk_t *, struiod_t *);
extern int rwnext(queue_t *, struiod_t *);
extern int infonext(queue_t *, infod_t *);
extern int isuioq(queue_t *);

#endif /* _KERNEL */

/*
 * shared or externally configured data structures
 */
extern int nstrpush;			/* maximum number of pushes allowed */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_STREAM_H */
