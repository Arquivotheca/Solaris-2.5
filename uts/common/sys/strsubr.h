/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_STRSUBR_H
#define	_SYS_STRSUBR_H

#pragma ident	"@(#)strsubr.h	1.67	95/09/26 SMI"	/* SVr4.0 1.17	*/

/*
 * WARNING:
 * Everything in this file is private, belonging to the
 * STREAMS subsystem.  The only guarantee made about the
 * contents of this file is that if you include it, your
 * code will not port to the next release.
 */
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/session.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * In general, the STREAMS locks are disjoint; they are only held
 * locally, and not simulataneously by a thread.  However, module
 * code, including at the stream head, requires some locks to be
 * acquired in order for its safety.
 *	1. Stream level claim.  This prevents the value of q_next
 *		from changing while module code is executing.
 *	2. Queue level claim.  This prevents the value of q_ptr
 *		from changing while put or service code is executing.
 *		In addition, it provides for queue single-threading
 *		for QPAIR and PERQ MT-safe modules.
 *	3. Stream head lock.  May be held by the stream head module
 *		to implement a read/write/open/close monitor.
 *	   Note: that the only types of twisted stream supported are
 *	   the pipe and transports which have read and write service
 *	   procedures on both sides of the twist.
 *	4. Queue lock.  May be acquired by utility routines on
 *		behalf of a module.
 */

/*
 * In general, sd_lock protects the consistency of the stdata
 * structure.  Additionally, it is used with sd_monitor
 * to implement an open/close monitor.  In particular, it protects
 * the following fields:
 *	sd_iocblk
 *	sd_flag
 *	sd_iocid
 *	sd_iocwait
 *	sd_sidp
 *	sd_pgidp
 *	sd_wroff
 *	sd_rerror
 *	sd_werror
 *	sd_pushcnt
 *	sd_sigflags
 *	sd_siglist
 *	sd_pollist
 *	sd_mark
 *	sd_closetime
 *	sd_wakeq
 *	sd_uiordq
 *	sd_uiowrq
 *
 * The following fields are modified only by the allocator, which
 * has exclusive access to them at that time:
 *	sd_wrq
 *	sd_strtab
 *
 * The following field is protected by the overlying filesystem
 * code, guaranteeing single-threading of opens:
 *	sd_vnode
 *
 * If unsafe_driver is needed, it should be acquired before
 *	Stream-level locks such as sd_lock are acquired.  Stream-
 *	level locks should be acquired before any queue-level locks
 *	are acquired.
 *
 * The stream head write queue lock(sd_wrq) is used to protect the
 * fields qn_maxpsz and qn_minpsz because freezestr() which is
 * necessary for strqset() only gets the queue lock.
 */

/*
 * Header for a stream: interface to rest of system.
 */
typedef struct stdata {
	struct queue *sd_wrq;		/* write queue */
	struct msgb *sd_iocblk;		/* return block for ioctl */
	struct vnode *sd_vnode;		/* pointer to associated vnode */
	struct streamtab *sd_strtab;	/* pointer to streamtab for stream */
	long sd_flag;			/* state/flags */
	long sd_iocid;			/* ioctl id */
	struct pid *sd_sidp;		/* controlling session info */
	struct pid *sd_pgidp;		/* controlling process group info */
	ushort dummy;			/* XXX UNUSED */
	ushort sd_wroff;		/* write offset */
	int sd_rerror;			/* read error to set u.u_error */
	int sd_werror;			/* write error to set u.u_error */
	int sd_pushcnt;			/* number of pushes done on stream */
	int sd_sigflags;		/* logical OR of all siglist events */
	struct strsig *sd_siglist;	/* pid linked list to rcv SIGPOLL sig */
	struct pollhead sd_pollist;	/* list of all pollers to wake up */
	struct msgb *sd_mark;		/* "marked" message on read queue */
	int sd_closetime;		/* time to wait to drain q in close */
	kmutex_t sd_lock;		/* protect head consistency */
	kcondvar_t sd_monitor;		/* open/close/push/pop monitor */
	kcondvar_t sd_iocmonitor;	/* ioctl single-threading */
	long	sd_qn_minpsz;		/* These two fields are a performance */
	long	sd_qn_maxpsz;		/* enhancements, cache the values in */
					/* the stream head so we don't have */
					/* to ask the module below the stream */
					/* head to get this information. */
	struct stdata *sd_mate;		/* pointer to twisted stream mate */
	kthread_id_t sd_freezer;	/* thread that froze stream */
	kmutex_t	sd_reflock;	/* Protects sd_refcnt */
	long		sd_refcnt;	/* number of claimstr */
	struct stdata *sd_next;		/* next in list of all stream heads */
	struct stdata *sd_prev;		/* prev in list of all stream heads */
	long sd_wakeq;			/* strwakeq()'s copy of sd_flag */
	struct queue *sd_struiordq;	/* sync barrier struio() read queue */
	struct queue *sd_struiowrq;	/* sync barrier struio() write queue */
	char sd_struiodnak;		/* defer NAK of M_IOCTL by rput() */
	struct msgb *sd_struionak;	/* pointer M_IOCTL mblk(s) to NAK */
#ifdef C2_AUDIT
	caddr_t sd_t_audit_data;
#endif	 /* C2_AUDIT */
	struct vnode *sd_vnfifo;	/* fifo vnode held */
} stdata_t;

/*
 * stdata flag field defines
 */
#define	IOCWAIT		0x00000001	/* Someone wants to do ioctl */
#define	RSLEEP		0x00000002	/* Someone wants to read/recv msg */
#define	WSLEEP		0x00000004	/* Someone wants to write */
#define	STRPRI		0x00000008	/* An M_PCPROTO is at stream head */
#define	STRHUP		0x00000010	/* Device has vanished */
#define	STWOPEN		0x00000020	/* waiting for 1st open */
#define	STPLEX		0x00000040	/* stream is being multiplexed */
#define	STRISTTY	0x00000080	/* stream is a terminal */
#define	RMSGDIS		0x00000100	/* read msg discard */
#define	RMSGNODIS	0x00000200	/* read msg no discard */
#define	STRDERR		0x00000400	/* fatal read error from M_ERROR */
#define	STRPOLL		0x00000800	/* used to optimize poll calls */
#define	STRDERRNONPERSIST 0x00001000	/* nonpersistent read errors */
#define	STWRERRNONPERSIST 0x00002000	/* nonpersistent write errors */
#define	STRCLOSE	0x00004000	/* wait for a close to complete */
#define	SNDMREAD	0x00008000	/* used for read notification */
#define	OLDNDELAY	0x00010000	/* use old TTY semantics for */
					/* NDELAY reads and writes */
#define	STRSNDZERO	0x00040000	/* send 0-length msg. down pipe/FIFO */
#define	STRTOSTOP	0x00080000	/* block background writes */
#define	RDPROTDAT	0x00100000	/* read M_[PC]PROTO contents as data */
#define	RDPROTDIS	0x00200000	/* discard M_[PC]PROTO blocks and */
					/* retain data blocks */
#define	STRMOUNT	0x00400000	/* stream is mounted */
#define	STRSIGPIPE	0x00800000	/* send SIGPIPE on write errors */
#define	STRDELIM	0x01000000	/* generate delimited messages */
#define	STWRERR		0x02000000	/* fatal write error from M_ERROR */
#define	STRPLUMB	0x08000000	/* push/pop pending */
#define	STR4TIME	0x10000000	/* used with timeout kstr_msg_poll */
#define	STREOPENFAIL	0x20000000	/* indicates if re-open has failed */
#define	STRMATE		0x40000000	/* this stream is a mate */
#define	STRHASLINKS	0x80000000	/* I_LINKs under this stream */


/*
 * Each queue points to a sync queue (the inner perimeter) which keeps
 * track of the number of threads that are inside a given queue (sq_count)
 * and also is used to implement the asynchronous putnext
 * (by queueing messages if the queue can not be entered.)
 *
 * Messages are queued on sq_head/sq_tail including deferred qwriter(INNER)
 * messages. The sq_ehad/sq_tail list is a singly-linked list with
 * b_queue recording the queue and b_prev recording the function to
 * be called (either the put procedure or a qwriter callback function.)
 *
 * In addition a module writer can declare that the module has an outer
 * perimeter (by setting D_MTOUTPERIM) in which case all inner perimeter
 * syncq's for the module point (through sq_outer) to an outer perimeter
 * syncq. The outer perimeter consists of the doubly linked list (sq_onext and
 * sq_oprev) linking all the inner perimeter syncq's with out outer perimeter
 * syncq. This is used to implement qwriter(OUTER) (an asynchronous way of
 * getting exclusive access at the outer perimeter) and outer_enter/exit
 * which are used by the framework to acquire exclusive access to the outer
 * perimeter during open and close of modules that have set D_MTOUTPERIM.
 *
 * In the inner perimeter case sq_save is available for use by machine
 * dependent code. sq_head/sq_tail are used to queue deferred messages on
 * the inner perimeter syncqs and to queue become_writer requests on the
 * outer perimeter syncqs.
 *
 * Note: machine dependent optmized versions of putnext may depend
 * on the order of sq_flags and sq_count (so that they can e.g.
 * read these two fields in a single load instruction.)
 *
 */
struct syncq {
	kmutex_t	sq_lock;	/* atomic access to syncq */
	double		sq_save;	/* used by machine dependent, */
					/* optimized putnext */
	u_short		sq_count;	/* # threads inside */
	u_short		sq_flags;	/* state and some type info */
	mblk_t		*sq_head;	/* queue of deferred messages */
	mblk_t		*sq_tail;	/* queue of deferred messages */
	u_long		sq_type;	/* type (concurrency) of syncq */

	kcondvar_t 	sq_wait;	/* block on this sync queue */
	kcondvar_t 	sq_exitwait;	/* waiting for thread to leave the */
					/* inner perimeter */
	/*
	 * Handling synchronous callbacks such as qtimeout and qbufcall
	 */
	u_short		sq_callbflags;	/* flags for callback synchronization */
	int		sq_cancelid;	/* id of callback being cancelled */
	struct callbparams *sq_callbpend;	/* Pending callbacks */

	/*
	 * Links forming an outer perimeter from one outer syncq and
	 * a set of inner sync queues.
	 */
	struct syncq	*sq_outer;	/* Pointer to outer perimeter */
	struct syncq	*sq_onext;	/* Linked list of syncq's making */
	struct syncq	*sq_oprev;	/* up the outer perimeter. */
};
typedef struct syncq syncq_t;

/*
 * sync queue state flags
 */
#define	SQ_EXCL		0x0001		/* exclusive access to inner */
					/*	perimeter */
#define	SQ_BLOCKED	0x0002		/* qprocsoff */
#define	SQ_FROZEN	0x0004		/* freezestr */
#define	SQ_WRITER	0x0008		/* qwriter(OUTER) pending or running */
#define	SQ_QUEUED	0x0010		/* messages on syncq */
#define	SQ_WANTWAKEUP	0x0020		/* do cv_broadcast on sq_wait */
#define	SQ_WANTEXWAKEUP	0x0040		/* do cv_broadcast on sq_exitwait */

/*
 * If any of these flags are set it is not possible for a thread to
 * enter a put or service procedure. Instead it must either block
 * or put the message on the syncq.
 */
#define	SQ_GOAWAY	(SQ_EXCL|SQ_BLOCKED|SQ_FROZEN|SQ_WRITER|\
			SQ_QUEUED)
/*
 * If any of these flags are set it not possible to drain the syncq
 */
#define	SQ_STAYAWAY	(SQ_BLOCKED|SQ_FROZEN|SQ_WRITER)

/*
 * Syncq types (stored in sq_type)
 * The SQ_TYPES_IN_FLAGS (unsafe + ciput) are also stored in sq_flags
 * for performance reasons. Thus these type values have to be in the low
 * 16 bits and not conflict with the sq_flags values above.
 *
 * Notes:
 *  - putnext() and put() assume that the put procedures have the highest
 *    degreee of concurrency. Thus if any of the SQ_CI* are set then SQ_CIPUT
 *    has to be set. This restriction can be lifted by adding code to putnext
 *    and put that check that sq_count == 0 like entersq does.
 *  - putnext() and put() does currently not handle !SQ_COPUT
 *  - Can not implement SQ_CIOC due to qprocsoff deadlock for D_MTPERMOD
 *    (SQ_CIOC would allow multiple threads to enter the close procedure
 *    which would cause a deadlock in qprocsoff.)
 *  - In order to implement !SQ_COCB outer_enter has to be fixed so that
 *    the callback can be cancelled while cv_waiting in outer_enter.
 *
 * All the SQ_CO flags are set when there is no outer perimeter.
 */
#define	SQ_UNSAFE	0x0080		/* An unsafe syncq */
#define	SQ_CIPUT	0x0100		/* Concurrent inner put proc */
#define	SQ_CISVC	0x0200		/* Concurrent inner svc proc */
#define	SQ_CIOC		0x0400		/* Concurrent inner open/close */
#define	SQ_CICB		0x0800		/* Concurrent inner callback */
#define	SQ_COPUT	0x1000		/* Concurrent outer put proc */
#define	SQ_COSVC	0x2000		/* Concurrent outer svc proc */
#define	SQ_COOC		0x4000		/* Concurrent outer open/close */
#define	SQ_COCB		0x8000		/* Concurrent outer callback */

/* Types also kept in sq_flags for performance */
#define	SQ_TYPES_IN_FLAGS	(SQ_UNSAFE|SQ_CIPUT)

#define	SQ_CI		(SQ_CIPUT|SQ_CISVC|SQ_CIOC|SQ_CICB)
#define	SQ_CO		(SQ_COPUT|SQ_COSVC|SQ_COOC|SQ_COCB)
#define	SQ_TYPEMASK	(SQ_CI|SQ_CO|SQ_UNSAFE)

/*
 * Flag combinations passed to entersq and leavesq to specify the type
 * of entry point.
 */
#define	SQ_PUT		(SQ_CIPUT|SQ_COPUT)
#define	SQ_SVC		(SQ_CISVC|SQ_COSVC)
#define	SQ_OPENCLOSE	(SQ_CIOC|SQ_COOC)
#define	SQ_CALLBACK	(SQ_CICB|SQ_COCB)

/*
 * Asynchronous callback qun*** flag.
 * The mechanism these flags are used in is one where callbacks enter
 * the perimeter thanks to framework support. To use this mechanism
 * the q* and qun* flavours of the callback routines must be used.
 * eg qtimeout and quntimeout. The synchonization provided by the flags
 * avoids deadlocks between blocking qun* routines and the perimeter
 * lock.
 */
#define	SQ_CALLB_BYPASSED	0x01		/* bypassed callback fn */

/*
 * Cancel callback mask.
 * The mask expands as the number of cancelable callback types grows
 * Note - separate callback flag because different callbacks have
 * overlapping id space.
 */
#define	SQ_CALLB_CANCEL_MASK	(SQ_CANCEL_TOUT|SQ_CANCEL_BUFCALL)

#define	SQ_CANCEL_TOUT		0x02		/* cancel timeout request */
#define	SQ_CANCEL_BUFCALL	0x04		/* cancel bufcall request */

typedef struct callbparams {
	syncq_t *sq;
	void (*fun)();
	caddr_t arg;
	int	id;
	u_long	flags;
	struct callbparams *next;
} callbparams_t;

typedef struct strbufcall {
	void		(*bc_func)();
	long		bc_arg;
	int		bc_size;
	char		bc_unsafe;
	u_short		bc_id;
	struct strbufcall *bc_next;
	kthread_id_t	bc_executor;
} strbufcall_t;

/*
 * Structure of list of processes to be sent SIGSEL signal
 * on request, or for processes sleeping on select().  The valid
 * SIGSEL events are defined in stropts.h, and the valid select()
 * events are defined in select.h.
 */
typedef struct strsig {
	struct pid	*ss_pidp;
	long		ss_events;
	struct strsig	*ss_next;
} strsig_t;

/*
 * Since all of these events are fairly rare, we allocate them all
 * from a single cache to save memory.
 */
typedef struct strevent {
	union {
		callbparams_t	c;
		strbufcall_t	b;
		strsig_t	s;
	} un;
} strevent_t;

/*
 * bufcall list
 */
struct bclist {
	strbufcall_t	*bc_head;
	strbufcall_t	*bc_tail;
};

/*
 * Structure used to track mux links and unlinks.
 */
struct mux_node {
	long		 mn_imaj;	/* internal major device number */
	ushort		 mn_indegree;	/* number of incoming edges */
	struct mux_node *mn_originp;	/* where we came from during search */
	struct mux_edge *mn_startp;	/* where search left off in mn_outp */
	struct mux_edge *mn_outp;	/* list of outgoing edges */
	uint		 mn_flags;	/* see below */
};

/*
 * Flags for mux_nodes.
 */
#define	VISITED	1

/*
 * Edge structure - a list of these is hung off the
 * mux_node to represent the outgoing edges.
 */
struct mux_edge {
	struct mux_node	*me_nodep;	/* edge leads to this node */
	struct mux_edge	*me_nextp;	/* next edge */
	int		 me_muxid;	/* id of link */
};

/*
 * Queue info
 *
 * XXX -- The syncq is included here to reduce memory fragmentation
 * for kernel memory allocators that only allocate in sizes that are
 * powers of two. If the kernel memory allocator changes this should
 * be revisited.
 */
typedef struct queinfo {
	struct queue	qu_rqueue;	/* read queue - must be first */
	struct queue	qu_wqueue;	/* write queue - must be second */
	struct syncq	qu_syncq;	/* syncq - must be third */
} queinfo_t;

/*
 * Multiplexed streams info
 */
typedef struct linkinfo {
	struct linkblk	li_lblk;	/* must be first */
	struct file	*li_fpdown;	/* file pointer for lower stream */
	struct linkinfo	*li_next;	/* next in list */
	struct linkinfo *li_prev;	/* previous in list */
} linkinfo_t;

/*
 * List of syncq's used by freeezestr/unfreezestr
 */
typedef struct syncql {
	struct syncql	*sql_next;
	syncq_t		*sql_sq;
} syncql_t;

typedef struct sqlist {
	syncql_t	*sqlist_head;
	int		sqlist_size;		/* structure size in bytes */
	int		sqlist_index;		/* next free entry in array */
	syncql_t	sqlist_array[4];	/* 4 or more entries */
} sqlist_t;

/* Per-device and per-module structure */
typedef struct perdm {
	syncq_t			*dm_sq;
	struct streamtab	*dm_str;
} perdm_t;

/*
 * Miscellaneous parameters and flags.
 */

/*
 * amount of time to hold small messages in strwrite hoping to to
 * able to append more data from a subsequent write.  one tick min.
 */
#define	STRSCANP	((10*HZ+999)/1000)	/* 10 ms in ticks */

/*
 * Finding related queues
 */
#define	STREAM(q)	((q)->q_stream)
#define	SQ(rq)		((syncq_t *)((rq) + 2))

/*
 * Locking macros
 */
#define	QLOCK(q)	(&(q)->q_lock)
#define	SQLOCK(sq)	(&(sq)->sq_lock)

#define	CLAIM_QNEXT_LOCK(stp)	mutex_enter(&(stp)->sd_lock)
#define	RELEASE_QNEXT_LOCK(stp)	mutex_exit(&(stp)->sd_lock)

/*
 * Default timeout in milliseconds for ioctls and close
 */
#define	STRTIMOUT 15000

/*
 * Flag values for stream io
 */
#define	WRITEWAIT	0x1	/* waiting for write event */
#define	READWAIT	0x2	/* waiting for read event */
#define	NOINTR		0x4	/* error is not to be set for signal */
#define	GETWAIT		0x8	/* waiting for getmsg event */

/*
 * These flags need to be unique for stream io name space
 * and copy modes name space.  These flags allow strwaitq
 * and strdoioctl to proceed as if signals or errors on the stream
 * head have not occurred; i.e. they will be detected by some other
 * means.
 * STR_NOSIG does not allow signals to interrupt the call
 * STR_NOERROR does not allow stream head read, write or hup errors to
 * affect the call.  When used with strdoioctl(), if a previous ioctl
 * is pending and times out, STR_NOERROR will cause strdoioctl() to not
 * return ETIME. If, however, the requested ioctl times out, ETIME
 * will be returned (use ic_timout instead)
 */
#define	STR_NOSIG	0x10	/* Ignore signals during strdoioctl/strwaitq */
#define	STR_NOERROR	0x20	/* Ignore errors during strdoioctl/strwaitq */

/*
 * Copy modes for tty and I_STR ioctls
 */
#define	U_TO_K 	01			/* User to Kernel */
#define	K_TO_K  02			/* Kernel to Kernel */

/*
 * canonical structure definitions
 */

#define	STRLINK		"lli"
#define	STRIOCTL	"iiil"
#define	STRPEEK		"iiliill"
#define	STRFDINSERT	"iiliillii"
#define	O_STRRECVFD	"lssc8"
#define	STRRECVFD	"lllc8"
#define	STRNAME		"c0"
#define	STRINT		"i"
#define	STRTERMIO	"ssssc12"
#define	STRTERMCB	"c6"
#define	STRSGTTYB	"c4i"
#define	STRTERMIOS	"llllc20"
#define	STRLIST		"il"
#define	STRSEV		"issllc1"
#define	STRGEV		"ili"
#define	STREVENT	"lssllliil"
#define	STRLONG		"l"
#define	STRBANDINFO	"ci"

#define	STRPIDT		"l"

/*
 * Tables we reference during open(2) processing.
 */
#define	CDEVSW	0
#define	FMODSW	1

/*
 * Mux defines.
 */
#define	LINKNORMAL	0x01		/* normal mux link */
#define	LINKPERSIST	0x02		/* persistent mux link */
#define	LINKTYPEMASK	0x03		/* bitmask of all link types */
#define	LINKCLOSE	0x04		/* unlink from strclose */
#define	LINKIOCTL	0x08		/* unlink from strioctl */
#define	LINKNOEDGE	0x10		/* no edge to remove from graph */

/*
 * Definitions of Streams macros and function interfaces.
 */

/*
 * free_rtn flag definitions.
 */
#define	STRFREE_UNSAFE		0x1	/* unsafe driver */
#define	STRFREE_DEFCALLBACK	0x2	/* deferred free_func callback */

/*
 *  Queue scheduling macros
 */
#define	setqsched()	qrunflag = 1	/* set up queue scheduler */
#define	qready()	qrunflag	/* test if queues are ready to run */

/*
 * Macros dealing with mux_nodes.
 */
#define	MUX_VISIT(X)	((X)->mn_flags |= VISITED)
#define	MUX_CLEAR(X)	((X)->mn_flags &= (~VISITED)); \
			((X)->mn_originp = NULL)
#define	MUX_DIDVISIT(X)	((X)->mn_flags & VISITED)


/*
 * Twisted stream macros
 */
#define	STRMATED(X)	((X)->sd_flag & STRMATE)
#define	SETMATED(X, Y)  ((X)->sd_flag |= STRMATE); \
			((X)->sd_mate = (Y)); \
			((Y)->sd_flag |= STRMATE); \
			((Y)->sd_mate = (X))
#define	SETUNMATED(X, Y) STRLOCKMATES(X); \
			((X)->sd_flag &= ~STRMATE); \
			((Y)->sd_flag &= ~STRMATE); \
			((X)->sd_mate = NULL); \
			((Y)->sd_mate = NULL); \
			mutex_exit(&((X)->sd_lock)); \
			mutex_exit(&((Y)->sd_lock));
#define	STRLOCKMATES(X)	if (&((X)->sd_lock) > &(((X)->sd_mate)->sd_lock)) { \
				mutex_enter(&((X)->sd_lock)); \
				mutex_enter(&(((X)->sd_mate)->sd_lock));  \
			} else {  \
				mutex_enter(&(((X)->sd_mate)->sd_lock)); \
				mutex_enter(&((X)->sd_lock)); \
			}
#define	STRUNLOCKMATES(X)	mutex_exit(&((X)->sd_lock)); \
			mutex_exit(&(((X)->sd_mate)->sd_lock))

/*
 * Declarations of private routines.
 */
struct strioctl;
struct strbuf;
struct uio;
struct proc;

extern int strdoioctl(struct stdata *, struct strioctl *, mblk_t *,
	int, char *, cred_t *, int *);
extern void strsendsig(struct strsig *, int, long);
extern void str_sendsig(vnode_t *, int, long);
extern void strevpost(struct stdata *, int, long);
extern void strdrpost(mblk_t *);
extern void strhup(struct stdata *);
extern int qattach(queue_t *, dev_t *, int, int, int, cred_t *);
extern int qreopen(queue_t *, dev_t *, int, cred_t *);
extern void qdetach(queue_t *, int, int, cred_t *);
extern void strtime(struct stdata *);
extern void enterq(queue_t *);
extern void leaveq(queue_t *);
extern void str2time(struct stdata *);
extern void str3time(struct stdata *);
extern int putiocd(mblk_t *, mblk_t *, caddr_t, int, char *);
extern int getiocd(mblk_t *, caddr_t, int, char *);
extern struct linkinfo *alloclink(queue_t *, queue_t *, struct file *);
extern void lbfree(struct linkinfo *);
extern int linkcycle(stdata_t *, stdata_t *);
extern struct linkinfo *findlinks(stdata_t *, int, int);
extern queue_t *getendq(queue_t *);
extern int mlink(vnode_t *, int, int, cred_t *, int *);
extern int munlink(struct stdata *, struct linkinfo *, int, cred_t *, int *);
extern int munlinkall(struct stdata *, int, cred_t *, int *);
extern int mux_addedge(stdata_t *, stdata_t *, int);
extern void mux_rmvedge(stdata_t *, int);
extern int devflg_to_qflag(u_long, u_long *, u_long *);
extern void setq(queue_t *, struct qinit *, struct qinit *, struct streamtab *,
	perdm_t *, u_long, u_long);
extern int strmakemsg(struct strbuf *, int, struct uio *, struct stdata *,
	long, mblk_t **);
extern int strgetmsg(vnode_t *, struct strbuf *, struct strbuf *, u_char *,
	int *, int, rval_t *);
extern int strputmsg(vnode_t *, struct strbuf *, struct strbuf *, u_char,
	int flag, int fmode);
extern int strsyncplumb(struct stdata *, int, int);
extern struct streamtab	*fifo_getinfo(void);
extern int stropen(struct vnode *, dev_t *, int, cred_t *);
extern int strclose(struct vnode *, int, cred_t *);
extern int strpoll(register struct stdata *, short, int, short *,
	struct pollhead **);
extern void strclean(struct vnode *);
extern void str_cn_clean();	/* XXX hook for consoles signal cleanup */
extern int strwrite(struct vnode *, struct uio *, cred_t *);
extern int strread(struct vnode *, struct uio *, cred_t *);
extern int strioctl(struct vnode *, int, int, int, int, cred_t *, int *);
extern int getiocseqno(void);
extern int strwaitbuf(int, int);
extern void strunbcall(int, kthread_id_t);
extern int strwaitq(struct stdata *, int, off_t, int, int *);
extern int strctty(struct proc *, struct stdata *);
extern void stralloctty(sess_t *, struct stdata *);
extern void strfreectty(struct stdata *);
extern struct stdata *shalloc(queue_t *);
extern void shfree(struct stdata *s);
extern queue_t *allocq(void);
extern void freeq(queue_t *);
extern qband_t *allocband(void);
extern void freeband(qband_t *);
extern void queuerun(void);
extern void runqueues(void);
extern int findmod(char *);
extern void adjfmtp(char **, mblk_t *, int);
extern int str2num(char **);
extern void setqback(queue_t *, unsigned char);
extern int strcopyin(caddr_t, caddr_t, unsigned int, char *, int);
extern int strcopyout(caddr_t, caddr_t, unsigned int, char *, int);
extern void strsignal(struct stdata *, int, long);
extern int str_cv_wait(kcondvar_t *, kmutex_t *, long, int);
extern void strscan(void);
extern void runbuffcalls(void);
extern int rmv_qp(queue_t **, queue_t **qtail, queue_t *qp);
extern void disable_svc(queue_t *);
extern void remove_runlist(queue_t *);
extern void wait_svc(queue_t *);
extern void freemsg_flush(mblk_t *);
extern void backenable(queue_t *, int);
extern void set_qend(queue_t *);
extern int strgeterr(stdata_t *, long);
extern void qenable_locked(queue_t *);

extern void strblock(queue_t *);
extern void strunblock(queue_t *);
extern int qclaimed(queue_t *);
extern int straccess(struct stdata *, enum jcaccess);
extern int findmodbyindex(int);
extern int findmodbyname(char *);

extern void entersq(syncq_t *, int entrypoint);
extern void leavesq(syncq_t *, int entrypoint);
extern void claimq(queue_t *);
extern void releaseq(queue_t *);
extern void claimstr(queue_t *);
extern void releasestr(queue_t *);
extern void removeq(queue_t *, int unsafe);
extern void insertq(struct stdata *, queue_t *, int unsafe);
extern void blockq(queue_t *);
extern void unblockq(queue_t *);
extern void fill_syncq(syncq_t *, queue_t *, mblk_t *, void (*fun)());
extern void drain_syncq(syncq_t *);
extern void flush_syncq(syncq_t *, queue_t *);

extern void outer_enter(syncq_t *outer, u_long flags);
extern void outer_exit(syncq_t *outer);
extern void qwriter_inner(queue_t *q, mblk_t *mp, void (*func)());
extern void qwriter_outer(queue_t *q, mblk_t *mp, void (*func)());

extern callbparams_t *callbparams_alloc(syncq_t *sq, void (*fun)(),
					caddr_t arg);
extern void callbparams_free(syncq_t *sq, callbparams_t *params);
extern void callbparams_free_id(syncq_t *sq, int id, long flag);
extern void qcallbwrapper(callbparams_t *params);

extern mblk_t *esballoca(unsigned char *, int, int, frtn_t *);
extern int do_sendfp(struct stdata *, struct file *, struct cred *);
extern int qprocsareon(queue_t *);
extern int frozenstr(queue_t *);
extern int xmsgsize(mblk_t *);

extern void putnext_from_unsafe(queue_t *, mblk_t *);
extern int  putnext_to_unsafe(queue_t *, mblk_t *, u_long, u_long, syncq_t *);
extern void putnext_tail(syncq_t *, mblk_t *, u_long, u_long);

extern int str_mate(queue_t *, queue_t *);
extern queue_t *strvp2wq(vnode_t *);
extern vnode_t *strq2vp(queue_t *);
extern mblk_t *allocb_wait(int, uint, uint, int *);
void strpollwakeup(vnode_t *, short);
extern int putnextctl_wait(queue_t *, int);
int prn_putnextctl_wait(queue_t *, int, mblk_t *(*)());
int prn_putctl_wait(queue_t *, int, mblk_t *(*)());

/*
 * shared or externally configured data structures
 */
extern int strmsgsz;			/* maximum stream message size */
extern int strctlsz;			/* maximum size of ctl message */
extern int nstrpush;			/* maximum number of pushes allowed */

extern queue_t	*qhead;		/* head of runnable services list */
extern queue_t	*qtail;		/* tail of runnable services list */
extern kmutex_t	service_queue;	/* protects qhead and qtail */

extern struct kmem_cache *strsig_cache;
extern struct kmem_cache *bufcall_cache;
extern struct kmem_cache *callbparams_cache;

extern perdm_t *permod_syncq;
extern perdm_t *perdev_syncq;

/*
 * Note: Use of these macros are restricted to kernel/unix.
 * All modules/drivers should include sys/ddi.h.
 *
 * Finding related queues
 */
#define		OTHERQ(q)	((q)->q_flag&QREADR? (q)+1: (q)-1)
#define		WR(q)		((q)->q_flag&QREADR? (q)+1: (q))
#define		RD(q)		((q)->q_flag&QREADR? (q): (q)-1)
#define		SAMESTR(q)	(!((q)->q_flag & QEND))

/*
 * Old, unsafe version of SAMESTR
 */
#define		O_SAMESTR(q)	(((q)->q_next) && \
	(((q)->q_flag&QREADR) == ((q)->q_next->q_flag&QREADR)))



/*
 * Important that sizeof (MH) be a multiple of sizeof (double)
 * so that the buffer which follows is doubleword aligned.
 */
typedef struct mh_s {
	mblk_t	mh_mblk;
	dblk_t	mh_dblk;
} MH, *MHP;

#define	MH_MAX_CACHE	9536
#define	MH_ALIGN	8
#define	MH_ALIGN_SHIFT	3

#define	MH_SET_BAND_FLAG(addr, bandflag)	*((int *)(addr)) = bandflag;
#define	MH_GET_BAND_FLAG(addr)			*((int *)(addr))

#ifdef _BIG_ENDIAN
#define	MH_SET_REF_TYPE_REFMIN_UIOFLAG(addr, ref, type, refmin, uioflag) \
	*((int *)(addr)) = \
	    ((ref) << 24) | ((type) << 16) | ((refmin) << 8) | (uioflag);
#else
#define	MH_SET_REF_TYPE_REFMIN_UIOFLAG(addr, ref, type, refmin, uioflag) \
	*((int *)(addr)) = \
	    (ref) | ((type) << 8) | ((refmin) << 16) | ((uioflag) << 24);
#endif

#ifdef _KERNEL

extern struct kmem_cache *mh_table[MH_MAX_CACHE / MH_ALIGN];
extern void mh_constructor(void *, size_t);

#endif /* _KERNEL */


#ifdef	__cplusplus
}
#endif


#endif	/* _SYS_STRSUBR_H */
