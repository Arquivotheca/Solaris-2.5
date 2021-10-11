/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 *		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *
 *
 *
 *		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *	Copyright (c) 1986,1987,1988,1989,1990,1991,1994, Sun Microsystems, Inc.
 *	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 */

#ifndef _NFS_LM_SERVER_H
#define	_NFS_LM_SERVER_H

#pragma ident	"@(#)lm_server.h	1.11	95/06/15 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The lock manager server code is divided into three files:
 *	lm_server.c ----------- generic server code
 *	lm_nlm_server.c ------- NLMv1-3 protocol specific code
 *	lm_nlm4_server.c ------ NLMv4 protcol specific code
 *
 * N.B. the code in lm_nlm_server.c and lm_nlm4_server.c is nearly
 * identical.  Any changes made to one file should also be made to
 * the corresponding code in the other file.  The main reason these
 * haven't been combined is that the responses made to the client
 * must be done at the same level of the protocol that was used
 * by the client when making the request.  The easiest way to make
 * this information available at the point where the server is
 * responding is to call a different routine for NLMv4.
 *
 * There is also a header file lm_server.h which holds the common
 * definitions used by the "server" files.
 *
 * The file lm_subr.c contains two protocol specific routines.  These
 * are only used for debugging purposes, so they have been left in
 * lm_subr.c.
 */

/*
 * The lock manager server code can shut down and then restart (e.g., if
 * lockd is killed and restarted).  While the server is down or shutting
 * down, no requests will be responded to.  Once the server is down, it can
 * be restarted, but not while it is still shutting down.  If a thread
 * needs to wait for the server status to change, it can wait on
 * lm_stat_cv.  This status information is protected by lm_lck.
 */
typedef enum {LM_UP, LM_SHUTTING_DOWN, LM_DOWN} lm_server_status_t;
#ifdef _KERNEL
extern lm_server_status_t lm_server_status;
#endif /* _KERNEL */

/*
 * For each vnode a list of granted shares is maintained.
 */
struct lm_share {
	struct lm_sysid *sysid;
	struct netobj oh;
	int mode;
	int access;
	struct lm_share *next;
};

/*
 * The list is used for keeping vnodes active as long as NFS-locks or NFS-shares
 * exist on them. Each vnode in the list is missing one VN_RELE.
 * If an lm_vnode is free, it should have both a zero reference count and a
 * null vnode pointer.  (A non-free lm_vnode can have a zero reference
 * count, e.g., if there is an active lock for the file but no lock manager
 * threads are using the lm_vnode for the file.)  When an lm_vnode is
 * marked free, its memory is not immediately freed, but free lm_vnode's
 * can be garbage collected later.
 *
 * lm_vnodes_lock protects the integrity of the lm_vnodes list itself.
 * lm_lck protects the fields `fh', `vp', `count', and the `shares' list.
 * To obtain both locks, they must be acquired in the order:
 *
 *		lm_vnodes_lock > lm_lck
 *
 * and released in the opposite order to prevent deadlocking.
 */
struct lm_vnode {
	struct vnode *vp;
	int count;
	struct lm_share *shares;
	struct lm_vnode *next;
	nfs_fhandle fh;			/* contains fh storage */
};

#ifdef _KERNEL
extern struct lm_vnode *lm_vnodes;
extern struct kmem_cache *lm_vnode_cache;

/*
 * This lock protects the lm_vnodes list for both reads and writes.
 * Perhaps it should be a readers/writer lock to improve MT performance.
 */
extern kmutex_t lm_vnodes_lock;

/* Number of entries in the lm_vnodes list.  Protected by lm_vnodes_lock. */
extern unsigned int lm_vnode_len;
#endif /* _KERNEL */

/*
 * Argument passed to local blocking lock callback routine
 * (lm_block_callback).  This structure allows calling thread
 * to communicate enough info to callback routine to answer
 * a lock request or transmit a lock_res with a status of blocked
 * backed to the client, as appropriate.
 */
typedef struct lm_blockinfo {
	vnode_t *vp;
	int blocked;
	struct lm_sysid *ls;
	int callback;
	union {
		struct nlm_res *nr;
		struct nlm4_res *nr4;
	} unr;
} lm_blockinfo_t;

/*
 * This is a list of pending granted calls for blocked lock requests for
 * which the server has granted the lock, sent a granted call to the client,
 * but the client has not yet responded to the call.  The following is a
 * scenario requiring a work around that uses this list.
 *   1. Client submits LOCK request, which blocks.
 *   2. Server sends GRANTED call.
 *   3. Client process unblocks, and client sends GRANTED response.
 *   4. GRANTED response is lost.
 *   5. Client does some processing and releases the lock.
 *   6. Some other process obtains the lock.
 *   7. Client decides it needs to get the same lock.  It submits a new LOCK
 *      request, which blocks.
 *   8. Server retransmits GRANTED call.
 *   9. Client fails to recognize that the GRANTED call is a retransmission,
 *      so it incorrectly thinks it has the lock.
 * This list allows the server to keep track of pending granted calls.
 * In the above scenario step 5, the server is able to search through
 * the list and cancel the retransmission of granted messages.
 *
 * Once a blocked lock request is granted, the server adds one of these
 * entries to the list and sends the granted call on to the client.  In
 * the normal case, the client will respond to this call and the server
 * will remove the entry from the list.  However, if the response is lost
 * for some reason, recovery will be necessary.  Incoming requests are
 * compared against this list.  If a match is found, the following table
 * describes what happens depending on the NLM command received.  A match
 * is determined by having the same sysid, pid and at least some part
 * of the blocked lock region in common.
 *
 * Command		Action
 * unlock (same or	assume the client got the granted call and the
 * overlapping region)	response was lost.  Cancel the retransmission
 *			loop and process the unlock request
 *
 * lock (same region	assume the client never got the granted call
 * non blocking)	gave up, and came back later with a non blocking
 *			request.  Cancel the retransmission loop and
 *			process this request.  Because we think the
 *			client already has the lock, we will grant the
 *			request.
 *
 * lock (same region	Assume the client never got the granted call, and
 * blocking)		retransmitted its request.  Cancel the retransmisson
 *			loop and proccess this request instead.  This
 *			time it won't block because the lock has already
 *			been granted.
 *
 * lock (for over-	There can be several things going on here:
 * lapping region)	1. the client could be multi-threaded,  2. it
 *			could have received the granted call and the
 *			response got lost, or 3. the granted call could
 *			have been lost and the client gave up and is
 *			trying a new lock.  There is no way to distinguish
 *			between these cases.  The retransmission loop
 *			will be canceled and this request will be processed.
 *
 * The general solution is that if a match is found on the lm_block
 * list, cancel the retransmission loop and process the request.
 *
 * The lm_block list is protected by lm_lck.  It is also NULL terminated.
 */

struct lm_block {
	bool_t	lmb_cancelled;		/* cancel retransmission flag */
	struct flock	*lmb_flk;
	struct lm_vnode	*lmb_vn;
	struct lm_block *lmb_next;
};
typedef struct lm_block lm_block_t;
#ifdef _KERNEL
extern lm_block_t *lm_blocks;
#endif /* _KERNEL */


/*
 * value for lmb_cancelled when the the request should be canceled
 */
#define	LM_BLOCK_CANCELLED	1

#ifdef DEBUG

/*
 * Testing hooks.
 *
 * lm_gc_sysids		if enabled, free as many lm_sysid's as possible before
 * 			every outgoing and incoming request.
 */

#ifdef _KERNEL
extern int lm_gc_sysids;
#endif /* _KERNEL */

#endif /* DEBUG */

/* function prototypes for functions found in lm_server.c */
#ifdef _KERNEL
/*
 * Globals and functions used by the lock manager.  Except for
 * functions exported in modstubs.s, these should all be treated as
 * private to the lock manager.
 */

void lm_sm_client(struct lm_sysid *ls, struct lm_sysid *me);
void lm_sm_server(struct lm_sysid *ls, struct lm_sysid *me);
void lm_rel_vnode(struct lm_vnode *);
void lm_rel_share(struct lm_vnode *lv, struct lm_sysid *ls, struct netobj *oh);
void lm_unlock_client(struct lm_sysid *);
void lm_relock_server(char *server);
void lm_reclaim_lock(struct vnode *, struct flock *);
int nlm_dispatch_enter(register SVCXPRT *xprt);
void nlm_dispatch_exit();

/*
 * the dispatch routine for versions 1-3 of the NLM protocol
 */
void lm_nlm_dispatch(register struct svc_req *, register SVCXPRT *);

/*
 * the lock reclaim routine for versions 1-3 of the NLM protocol
 */
void lm_nlm_reclaim(struct vnode *vp, struct flock *flkp);

/*
 * the dispatch routine for version 4 of the NLM protocol
 */
void lm_nlm4_dispatch(register struct svc_req *, register SVCXPRT *);

/*
 * the lock reclaim routine for version 4 of the NLM protocol
 */
void lm_nlm4_reclaim(struct vnode *vp, struct flock *flkp);

/*
 * Routines to operate on the lm_block list
 */
void lm_add_block(lm_block_t *);
void lm_remove_block(lm_block_t *);
void lm_release_blocks(sysid_t);
int  lm_block_cmp(lm_block_t *, struct flock *, struct lm_vnode *);
void lm_cancel_granted_rxmit(struct flock *, struct lm_vnode *);
#endif /* _KERNEL */

/*
 * Server NLM dispatcher table.
 * Indexed by procedure number.
 *
 */
#define	LM_IGNORED	-1		/* field contents are ignored */

struct lm_nlm_disp {
	void (*proc)();		/* proc to call */
	int callback;		/* proc to call back to. */
	xdrproc_t xdrargs;	/* xdr routine to get args */
	xdrproc_t xdrres;	/* xdr routine to put results */
};

#ifdef	__cplusplus
}
#endif

#endif /* _NFS_LM_SERVER_H */
