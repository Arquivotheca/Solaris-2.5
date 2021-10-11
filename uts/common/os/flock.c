
/*
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident   "@(#)flock.c 1.6     95/07/01 SMI"

#include <sys/flock_impl.h>
#include <rpc/types.h>
#include <netinet/in.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/rpc.h>
#include <sys/vfs.h>
#include <sys/t_lock.h>		/* for <sys/callb.h> */
#include <sys/callb.h>
#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <nfs/rnode.h>

/*
 * The following four variables are for statistics purposes and they are
 * not protected by locks. They may not be accurate but will at least be
 * close to the actual value.
 */

int	flk_lock_allocs;
int	flk_lock_frees;
int 	edge_allocs;
int	edge_frees;
int 	flk_proc_vertex_allocs;
int 	flk_proc_edge_allocs;
int	flk_proc_vertex_frees;
int	flk_proc_edge_frees;

#ifdef DEBUG
int check_debug = 0;
#define	CHECK_ACTIVE_LOCKS(gp)	if (check_debug) \
					check_active_locks(gp);
#define	CHECK_SLEEPING_LOCKS(gp)	if (check_debug) \
						check_sleeping_locks(gp);
#define	CHECK_OWNER_LOCKS(gp, pid, sysid, vp) 	\
		if (check_debug)	\
			check_owner_locks(gp, pid, sysid, vp);
#else

#define	CHECK_ACTIVE_LOCKS(gp)
#define	CHECK_SLEEPING_LOCKS(gp)
#define	CHECK_OWNER_LOCKS(gp, pid, sysid, vp)

#endif /* DEBUG */

struct kmem_cache	*flk_edge_cache;

graph_t	*lock_graph[HASH_SIZE];
proc_graph_t	pgraph;

/*
 * Do we allow lock manager requests or not.  Protected by flock_lock.
 * Each graph has a copy of this status variable, which is protected by the
 * graph's mutex.
 */
flk_lockmgr_status_t flk_lockmgr_status = FLK_LOCKMGR_UP;

static void create_flock(lock_descriptor_t *, flock_t *);
static lock_descriptor_t	*flk_get_lock(void);
static void	flk_free_lock(lock_descriptor_t	*lock);
static void	flk_get_first_blocking_lock(lock_descriptor_t *request);
static int flk_process_request(lock_descriptor_t *);
static int flk_execute_request(lock_descriptor_t *);
static int flk_add_edge(lock_descriptor_t *, lock_descriptor_t *, int, int);
static edge_t *flk_get_edge(void);
static int flk_wait_execute_request(lock_descriptor_t *);
static int flk_relation(lock_descriptor_t *, lock_descriptor_t *);
static void flk_insert_active_lock(lock_descriptor_t *);
static void flk_delete_active_lock(lock_descriptor_t *, int);
static void flk_cancel_sleeping_lock(lock_descriptor_t *, int);
static void flk_insert_sleeping_lock(lock_descriptor_t *);
static void flk_graph_uncolor(graph_t *);
static void flk_wakeup(lock_descriptor_t *, int);
static void flk_free_edge(edge_t *);
static void flk_recompute_dependencies(lock_descriptor_t *,
			lock_descriptor_t **,  int, int);
static int flk_find_barriers(lock_descriptor_t *);
static void flk_update_barriers(lock_descriptor_t *);
static int flk_color_reachables(lock_descriptor_t *);
static int flk_canceled(lock_descriptor_t *);
static void flk_delete_locks_by_sysid(lock_descriptor_t *);
static void report_blocker(lock_descriptor_t *, lock_descriptor_t *);
static void wait_for_lock(lock_descriptor_t *);
static void wakeup_sleeping_lockmgr_locks(void);
static void unlock_lockmgr_granted(void);

#ifdef DEBUG
static int check_sleeping_locks(graph_t *);
static int check_active_locks(graph_t *);
static int no_path(lock_descriptor_t *, lock_descriptor_t *);
static void path(lock_descriptor_t *, lock_descriptor_t *);
static void check_owner_locks(graph_t *, pid_t, long, vnode_t *);
static int level_one_path(lock_descriptor_t *, lock_descriptor_t *);
static int level_two_path(lock_descriptor_t *, lock_descriptor_t *, int);
#endif

/*	proc_graph function definitons */
static int flk_check_deadlock(lock_descriptor_t *);
static void flk_proc_graph_uncolor(void);
static proc_vertex_t *flk_get_proc_vertex(lock_descriptor_t *);
static proc_edge_t *flk_get_proc_edge(void);
static void flk_proc_release(proc_vertex_t *);
static void flk_free_proc_edge(proc_edge_t *);
static void flk_update_proc_graph(edge_t *, int);



/*
 * Routine called from fs_frlock in fs/fs_subr.c
 */

int
reclock(vnode_t *vp,
	flock_t *lckdat,
		int cmd,
		int flag,
		off_t offset)
{
	lock_descriptor_t	stack_lock_request;
	lock_descriptor_t	*lock_request;
	int error = 0;
	graph_t	*gp;

	/*
	 * Check access permissions
	 */

	if ((cmd & SETFLCK) &&
		((lckdat->l_type == F_RDLCK && (flag & FREAD) == 0) ||
		(lckdat->l_type == F_WRLCK && (flag & FWRITE) == 0)))
			return (EBADF);

	/*
	 * for query and unlock we use the stack_lock_request
	 */

	if ((lckdat->l_type == F_UNLCK) ||
			!((cmd & INOFLCK) || (cmd & SETFLCK))) {
		lock_request = &stack_lock_request;
		(void) bzero((caddr_t)lock_request,
				sizeof (lock_descriptor_t));

		/*
		 * following is added to make the assertions in
		 * flk_execute_request() to pass through
		 */

		lock_request->l_edge.edge_in_next = &lock_request->l_edge;
		lock_request->l_edge.edge_in_prev = &lock_request->l_edge;
		lock_request->l_edge.edge_adj_next = &lock_request->l_edge;
		lock_request->l_edge.edge_adj_prev = &lock_request->l_edge;
	} else {
		lock_request = flk_get_lock();
	}
	lock_request->l_state = 0;
	lock_request->l_vnode = vp;

	/*
	 * Convert the request range into the canonical start and end
	 * values.  The NLM protocol supports locking over the entire
	 * 32-bit range, so there's no range checking for remote requests,
	 * but we still need to verify that local requests obey the rules.
	 */
	if (cmd & RCMDLCK) {
		ASSERT(lckdat->l_whence == 0);
		lock_request->l_start = lckdat->l_start;
		lock_request->l_end = (lckdat->l_len == 0) ? MAX_U_OFF_T :
			lckdat->l_start + lckdat->l_len - 1;
	} else {
		/* check the validity of the lock range */
		error = flk_convert_lock_data(vp, lckdat,
			&lock_request->l_start, &lock_request->l_end,
			offset);
		if (error) {
			goto done;
		}
		error = flk_check_lock_data(lock_request->l_start,
					    lock_request->l_end);
		if (error) {
			goto done;
		}
	}

	ASSERT(lock_request->l_end >= lock_request->l_start);

	lock_request->l_type = lckdat->l_type;
	if (cmd & INOFLCK)
		lock_request->l_state |= IO_LOCK;
	if (cmd & SLPFLCK)
		lock_request->l_state |= WILLING_TO_SLEEP_LOCK;
	if (cmd & RCMDLCK)
		lock_request->l_state |= LOCKMGR_LOCK;
	if (!((cmd & SETFLCK) || (cmd & INOFLCK))) {
		if (lock_request->l_type == F_RDLCK ||
			lock_request->l_type == F_WRLCK)
			lock_request->l_state |= QUERY_LOCK;
	}
	lock_request->l_flock = (*lckdat);

	/*
	 * We are ready for processing the request
	 */
	mutex_enter(&flock_lock);

	/*
	 * Bail out if this is a lock manager request and the lock manager
	 * is not supposed to be running.
	 */
	if (flk_lockmgr_status != FLK_LOCKMGR_UP &&
	    IS_LOCKMGR(lock_request)) {
		error = ENOLCK;
		mutex_exit(&flock_lock);
		goto done;
	}

	gp = lock_graph[HASH_INDEX(vp)];
	if (gp == NULL) {
		gp = kmem_zalloc(sizeof (graph_t), KM_SLEEP);

		/* Initialise the graph */

		gp->active_locks.l_next = gp->active_locks.l_prev =
					(lock_descriptor_t *)ACTIVE_HEAD(gp);
		gp->sleeping_locks.l_next = gp->sleeping_locks.l_prev =
					(lock_descriptor_t *)SLEEPING_HEAD(gp);
		lock_graph[HASH_INDEX(vp)] = gp;
		gp->index = HASH_INDEX(vp);
		mutex_init(&gp->gp_mutex, "per graph mutex", MUTEX_DEFAULT,
								NULL);
		gp->lockmgr_status = flk_lockmgr_status;
	}

	mutex_exit(&flock_lock);

	/*
	 * We drop rwlock here otherwise this might end up causing a
	 * deadlock if this IOLOCK sleeps. (bugid # 1183392).
	 */

	if (IS_IO_LOCK(lock_request)) {
		VOP_RWUNLOCK(vp,
			(lock_request->l_type == F_RDLCK) ? 0 : 1);
	}
	mutex_enter(&gp->gp_mutex);

	lock_request->l_state |= REFERENCED_LOCK;
	lock_request->l_graph = gp;

	switch (lock_request->l_type) {
	case F_RDLCK:
	case F_WRLCK:
		if (IS_QUERY_LOCK(lock_request)) {
			flk_get_first_blocking_lock(lock_request);
			(*lckdat) = lock_request->l_flock;
			break;
		}

		/* process the request now */

		error = flk_process_request(lock_request);
		break;
	case F_UNLCK:

		/* unlock request will not block so execute it immediately */

		if (IS_LOCKMGR(lock_request) &&
		    flk_canceled(lock_request)) {
			error = 0;
		} else {
			error = flk_execute_request(lock_request);
		}
		break;

	case F_UNLKSYS:
		/*
		 * Recovery mechanism to release lock manager locks when
		 * NFS client crashes and restart. NFS server will clear
		 * old locks and grant new locks.
		 */
		flk_delete_locks_by_sysid(lock_request);
		lock_request->l_state &= ~REFERENCED_LOCK;
		flk_free_lock(lock_request);
		mutex_exit(&gp->gp_mutex);
		return (0);

	default:
		error = EINVAL;
		break;
	}

	/*
	 * Now that we have seen the status of locks in the system for
	 * this vnode we acquire the rwlock if it is an IO_LOCK.
	 */

	if (IS_IO_LOCK(lock_request)) {
		VOP_RWLOCK(vp,
			(lock_request->l_type == F_RDLCK) ? 0 : 1);
		if (!error) {
			lckdat->l_type = F_UNLCK;

			/*
			 * This wake up is needed otherwise
			 * if IO_LOCK has slept the dependents on this
			 * will not be woken up at all. (bugid # 1185482).
			 */

			flk_wakeup(lock_request, 1);
			flk_free_lock(lock_request);
		}
		/*
		 * else if error had occurred either flk_process_request()
		 * has returned EDEADLK in which case there will be no
		 * dependents for this lock or EINTR from flk_wait_execute_
		 * request() in which case flk_cancel_sleeping_lock()
		 * would have been done. same is true with EBADF.
		 */
	}


	lock_request->l_state &= ~REFERENCED_LOCK;

	if ((error && lock_request != &stack_lock_request) ||
	    IS_DELETED(lock_request))
		flk_free_lock(lock_request);
	mutex_exit(&gp->gp_mutex);
	return (error);

done:
	if (lock_request != &stack_lock_request)
		flk_free_lock(lock_request);
	return (error);
}

/*
 * Initialize the flk_edge_cache data structure
 */

void
flk_init()
{
	flk_edge_cache = kmem_cache_create("flk_edges",
			sizeof (struct edge), 0, NULL, NULL, NULL);
	if (flk_edge_cache == NULL) {
		cmn_err(CE_PANIC, "Couldn't creat flk_edge_cache\n");
	}
}

/*
 * Get a lock_descriptor structure with initialisation of edge lists.
 */

static lock_descriptor_t *
flk_get_lock()
{
	lock_descriptor_t	*l;

	l = kmem_zalloc(sizeof (lock_descriptor_t), KM_SLEEP);

	cv_init(&l->l_cv, "flock_lock", CV_DRIVER, NULL);
	l->l_edge.edge_in_next = &l->l_edge;
	l->l_edge.edge_in_prev = &l->l_edge;
	l->l_edge.edge_adj_next = &l->l_edge;
	l->l_edge.edge_adj_prev = &l->l_edge;
	l->pvertex = -1;
	flk_lock_allocs++;
	return (l);
}

/*
 * Free a lock_descriptor structure. Just sets the DELETED_LOCK flag
 * when some thread has a reference to it as in reclock().
 */

static void
flk_free_lock(lock_descriptor_t	*lock)
{
	if (IS_REFERENCED(lock)) {
		lock->l_state |= DELETED_LOCK;
		return;
	}
	flk_lock_frees++;
	kmem_free((void *)lock, sizeof (lock_descriptor_t));
}

/*
 * Routine that checks whether there are any blocking locks in the system.
 *
 * The policy followed is if a write lock is sleeping we don't allow read
 * locks before this write lock even though there may not be any active
 * locks corresponding to the read locks' region.
 *
 * flk_add_edge() function adds an edge between l1 and l2 iff there
 * is no path between l1 and l2. This is done to have a "minimum
 * storage representation" of the dependency graph.
 *
 * Another property of the graph is since only the new request throws
 * edges to the existing locks in the graph, the graph is always topologically
 * ordered.
 */

static int
flk_process_request(lock_descriptor_t *request)
{
	graph_t	*gp = request->l_graph;
	lock_descriptor_t *lock;
	int	request_blocked_by_active = 0;
	int request_blocked_by_granted = 0;
	vnode_t	*vp = request->l_vnode;
	int	error = 0;
	int request_will_wait = 0;
	int found_covering_lock = 0;
	lock_descriptor_t *covered_by = NULL;

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	request_will_wait = IS_WILLING_TO_SLEEP(request);

	/*
	 * check active locks
	 */

	SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp);


	if (lock) {
		do {
			if (BLOCKS(lock, request)) {
				if (!request_will_wait)
					return (EAGAIN);
				request_blocked_by_active = TRUE;
				break;
			}
			/*
			 * Grant lock if it is for the same owner holding active
			 * lock that covers the request.
			 */

			if (SAME_OWNER(lock, request) &&
					COVERS(lock, request) &&
						(request->l_type == F_RDLCK))
				return (flk_execute_request(request));
			lock = lock->l_next;
		} while (lock->l_vnode == vp);
	}

	if (!request_blocked_by_active) {
			lock_descriptor_t *lk[1];
			lock_descriptor_t *first_glock = NULL;
		/*
		 * Shall we grant this?! NO!!
		 * What about those locks that was just granted and still
		 * in sleep queue. Those threads are woken up and so locks
		 * are almost active.
		 */
		SET_LOCK_TO_FIRST_SLEEP_VP(gp, lock, vp);
		if (lock) {
			do {
				if (BLOCKS(lock, request) &&
					IS_GRANTED(lock)) {
						request_blocked_by_granted = 1;
				}
				lock = lock->l_next;
			} while ((lock->l_vnode == vp));
			first_glock = lock->l_prev;
			ASSERT(first_glock->l_vnode == vp);
		}

		if (request_blocked_by_granted)
			goto block;
		lk[0] = request;
		request->l_state |= RECOMPUTE_LOCK;
		SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp);
		if (lock) {
			do {
				flk_recompute_dependencies(lock, lk, 1, 0);
				lock = lock->l_next;
			} while (lock->l_vnode == vp);
		}
		lock = first_glock;
		if (lock) {
			do {
				if (IS_GRANTED(lock)) {
				flk_recompute_dependencies(lock, lk, 1, 0);
				}
				lock = lock->l_prev;
			} while ((lock->l_vnode == vp));
		}
		request->l_state &= ~RECOMPUTE_LOCK;
		if (!NO_DEPENDENTS(request) && flk_check_deadlock(request))
			return (EDEADLK);
		return (flk_execute_request(request));
	}

block:
	if (request_will_wait)
		flk_graph_uncolor(gp);

	/* check sleeping locks */

	SET_LOCK_TO_FIRST_SLEEP_VP(gp, lock, vp);

	/*
	 * If we find a sleeping write lock that is a superset of the
	 * region wanted by request we can be assured that by adding an
	 * edge to this write lock we have paths to all locks in the
	 * graph that blocks the request except in one case and that is why
	 * another check for SAME_OWNER in the loop below. The exception
	 * case is when this process that owns the sleeping write lock 'l1'
	 * has other locks l2, l3, l4 that are in the system and arrived
	 * before l1. l1 does not have path to these locks as they are from
	 * same process. We break when we find a second covering sleeping
	 * lock l5 owned by a process different from that owning l1, because
	 * there cannot be any of l2, l3, l4 etc., arrived before l5, and if
	 * it has l1 would have produced a deadlock already.
	 */

	if (lock) {
		do {
			if (BLOCKS(lock, request)) {
				if (!request_will_wait)
					return (EAGAIN);
				if (COVERS(lock, request) &&
						lock->l_type == F_WRLCK) {
					if (found_covering_lock &&
					    !SAME_OWNER(lock, covered_by)) {
						found_covering_lock++;
						break;
					}
					found_covering_lock = 1;
					covered_by = lock;
				}
				if (found_covering_lock &&
					!SAME_OWNER(lock, covered_by)) {
					lock = lock->l_next;
					continue;
				}
				if ((error = flk_add_edge(request, lock,
						!found_covering_lock, 0)))
					return (error);
			}
			lock = lock->l_next;
		} while (lock->l_vnode == vp);
	}

/*
 * found_covering_lock == 2 iff at this point 'request' has paths
 * to all locks that blocks 'request'. found_covering_lock == 1 iff at this
 * point 'request' has paths to all locks that blocks 'request' whose owners
 * are not same as the one that covers 'request' (covered_by above) and
 * we can have locks whose owner is same as covered_by in the active list.
 */

	if (request_blocked_by_active &&
			found_covering_lock != 2) {
		SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp);
		ASSERT(lock != NULL);
		do {
			if (BLOCKS(lock, request)) {
				if (found_covering_lock &&
					!SAME_OWNER(lock, covered_by)) {
					lock = lock->l_next;
					continue;
				}
				if ((error = flk_add_edge(request, lock,
							CHECK_CYCLE, 0)))
					return (error);
			}
			lock = lock->l_next;
		} while (lock->l_vnode == vp);
	}

	if (NOT_BLOCKED(request)) {
		/*
		 * request not dependent on any other locks
		 * so execute this request
		 */
		return (flk_execute_request(request));
	} else {
		/*
		 * check for deadlock
		 */
		if (flk_check_deadlock(request))
			return (EDEADLK);
		/*
		 * this thread has to sleep
		 */
		return (flk_wait_execute_request(request));
	}
}

/*
 * The actual execution of the request in the simple case is only to
 * insert the 'request' in the list of active locks if it is not an
 * UNLOCK.
 * We have to consider the existing active locks' relation to
 * this 'request' if they are owned by same process. flk_relation() does
 * this job and sees to that the dependency graph information is maintained
 * properly.
 */

static int
flk_execute_request(lock_descriptor_t *request)
{
	graph_t	*gp = request->l_graph;
	vnode_t	*vp = request->l_vnode;
	lock_descriptor_t	*lock, *lock1;
	int done_searching = 0;

	CHECK_SLEEPING_LOCKS(gp);
	CHECK_ACTIVE_LOCKS(gp);

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	ASSERT(NOT_BLOCKED(request));

	/* IO_LOCK requests are only to check status */

	if (IS_IO_LOCK(request))
		return (0);

	SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp);

	if (lock == NULL && request->l_type == F_UNLCK)
		return (0);
	if (lock == NULL) {
		flk_insert_active_lock(request);
		return (0);
	}

	do {
		lock1 = lock->l_next;
		if (SAME_OWNER(request, lock)) {
			done_searching = flk_relation(lock, request);
		}
		lock = lock1;
	} while (lock->l_vnode == vp && !done_searching);

	/*
	 * insert in active queue
	 */

	if (request->l_type != F_UNLCK)
		flk_insert_active_lock(request);

	return (0);
}

/*
 * 'request' is blocked by some one therefore we put it into sleep queue.
 */

static int
flk_wait_execute_request(lock_descriptor_t *request)
{
	graph_t	*gp = request->l_graph;
	callb_cpr_t *cprp;		/* CPR info from callback */
	callb_cpr_t *(*cb_proc)(void *); /* callback function */
	void *cb_args;			/* callback arguments */

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	ASSERT(IS_WILLING_TO_SLEEP(request));

	flk_insert_sleeping_lock(request);

	if (gp->lockmgr_status != FLK_LOCKMGR_UP &&
	    IS_LOCKMGR(request)) {
		flk_cancel_sleeping_lock(request, 1);
		return (ENOLCK);
	}

	if (IS_LOCKMGR(request) && (l_callback(&request->l_flock) != NULL)) {
		/*
		 * To make sure the shutdown code works correctly, either
		 * the callback must happen after putting the lock on the
		 * sleep list, or we must check the shutdown status after
		 * returning from the callback (and before sleeping).  At
		 * least for now, we'll use the first option.  If a
		 * shutdown or signal or whatever happened while the graph
		 * mutex was dropped, that will be detected by
		 * wait_for_lock().
		 */
		cb_proc = l_callback(&request->l_flock);
		cb_args = l_cbp(&request->l_flock);
		mutex_exit(&gp->gp_mutex);
		cprp = (*cb_proc)(cb_args);
		mutex_enter(&gp->gp_mutex);

		mutex_enter(cprp->cc_lockp);
		CALLB_CPR_SAFE_BEGIN(cprp);
		mutex_exit(cprp->cc_lockp);
		wait_for_lock(request);
		mutex_enter(cprp->cc_lockp);
		CALLB_CPR_SAFE_END(cprp, cprp->cc_lockp);
		mutex_exit(cprp->cc_lockp);
	} else {
		wait_for_lock(request);
	}

	/*
	 * If the lock manager is shutting down, return an error
	 * that will encourage the client to retransmit.
	 */
	if (gp->lockmgr_status != FLK_LOCKMGR_UP &&
	    IS_LOCKMGR(request) && !IS_GRANTED(request)) {
		flk_cancel_sleeping_lock(request, 1);
		return (ENOLCK);
	}

	if (IS_INTERRUPTED(request)) {
		/* we got a signal, or act like we did */
		flk_cancel_sleeping_lock(request, 1);
		return (EINTR);
	}

	/* Cancelled if some other thread has closed the file */

	if (IS_CANCELLED(request)) {
		flk_cancel_sleeping_lock(request, 1);
		return (EBADF);
	}

	request->l_state &= ~GRANTED_LOCK;
	REMOVE_SLEEP_QUEUE(request);
	return (flk_execute_request(request));
}

/*
 * This routine adds an edge between from and to because from depends
 * to. If asked to check for deadlock it checks whether there are any
 * reachable locks from "from_lock" that is owned by the same process
 * as "from_lock".
 * NOTE: It is the caller's responsibility to make sure that the color
 * of the graph is consistent between the calls to flk_add_edge as done
 * in flk_process_request. This routine does not color and check for
 * deadlock explicitly.
 */

static int
flk_add_edge(lock_descriptor_t *from_lock, lock_descriptor_t *to_lock,
			int check_cycle, int update_graph)
{
	edge_t	*edge;
	edge_t	*ep;
	lock_descriptor_t	*vertex;
	lock_descriptor_t *vertex_stack;

	STACK_INIT(vertex_stack);

	/*
	 * if to vertex already has mark_color just return
	 * don't add an edge as it is reachable from from vertex
	 * before itself.
	 */

	if (COLORED(to_lock))
		return (0);

	edge = flk_get_edge();

	/*
	 * set the from and to vertex
	 */

	edge->from_vertex = from_lock;
	edge->to_vertex = to_lock;

	/*
	 * put in adjacency list of from vertex
	 */

	from_lock->l_edge.edge_adj_next->edge_adj_prev = edge;
	edge->edge_adj_next = from_lock->l_edge.edge_adj_next;
	edge->edge_adj_prev = &from_lock->l_edge;
	from_lock->l_edge.edge_adj_next = edge;

	/*
	 * put in in list of to vertex
	 */

	to_lock->l_edge.edge_in_next->edge_in_prev = edge;
	edge->edge_in_next = to_lock->l_edge.edge_in_next;
	to_lock->l_edge.edge_in_next = edge;
	edge->edge_in_prev = &to_lock->l_edge;


	if (update_graph) {
		flk_update_proc_graph(edge, 0);
		return (0);
	}
	if (!check_cycle) {
		return (0);
	}

	STACK_PUSH(vertex_stack, from_lock, l_stack);

	while ((vertex = STACK_TOP(vertex_stack)) != NULL) {

		STACK_POP(vertex_stack, l_stack);

		for (ep = FIRST_ADJ(vertex);
			ep != HEAD(vertex);
				ep = NEXT_ADJ(ep)) {
			if (COLORED(ep->to_vertex))
				continue;
			COLOR(ep->to_vertex);
			if (SAME_OWNER(ep->to_vertex, from_lock))
				goto dead_lock;
			STACK_PUSH(vertex_stack, ep->to_vertex, l_stack);
		}
	}
	return (0);

dead_lock:

	/*
	 * remove all edges
	 */

	ep = FIRST_ADJ(from_lock);

	while (ep != HEAD(from_lock)) {
		IN_LIST_REMOVE(ep);
		from_lock->l_sedge = NEXT_ADJ(ep);
		ADJ_LIST_REMOVE(ep);
		flk_free_edge(ep);
		ep = from_lock->l_sedge;
	}
	return (EDEADLK);
}

/*
 * Get an edge structure for representing the dependency between two locks.
 */

static edge_t *
flk_get_edge()
{
	edge_t	*ep;

	ASSERT(flk_edge_cache != NULL);

	ep = kmem_cache_alloc(flk_edge_cache, KM_SLEEP);
	edge_allocs++;
	return (ep);
}

/*
 * Free the edge structure.
 */

static void
flk_free_edge(edge_t *ep)
{
	edge_frees++;
	kmem_cache_free(flk_edge_cache, (void *)ep);
}

/*
 * Check the relationship of request with lock and perform the
 * recomputation of dependencies, break lock if required, and return
 * 1 if request cannot have any more relationship with the next
 * active locks.
 * The 'lock' and 'request' are compared and in case of overlap we
 * delete the 'lock' and form new locks to represent the non-overlapped
 * portion of original 'lock'. This function has side effects such as
 * 'lock' will be freed, new locks will be added to the active list.
 */

static int
flk_relation(lock_descriptor_t *lock, lock_descriptor_t *request)
{
	int lock_effect;
	lock_descriptor_t *lock1, *lock2;
	lock_descriptor_t *topology[3];
	int nvertex = 0;
	int i;
	edge_t	*ep;
	graph_t	*gp = (lock->l_graph);


	CHECK_SLEEPING_LOCKS(gp);
	CHECK_ACTIVE_LOCKS(gp);

	ASSERT(MUTEX_HELD(&gp->gp_mutex));

	topology[0] = topology[1] = topology[2] = NULL;

	if (request->l_type == F_UNLCK)
		lock_effect = UNLOCK;
	else if (request->l_type == F_RDLCK &&
			lock->l_type == F_WRLCK)
		lock_effect = DOWNGRADE;
	else if (request->l_type == F_WRLCK &&
			lock->l_type == F_RDLCK)
		lock_effect = UPGRADE;
	else
		lock_effect = STAY_SAME;

	if (lock->l_end < request->l_start) {
		if (lock->l_end == request->l_start - 1 &&
				lock_effect == STAY_SAME) {
			topology[0] = request;
			request->l_start = lock->l_start;
			nvertex = 1;
			goto recompute;
		} else {
			return (0);
		}
	}

	if (lock->l_start > request->l_end) {
		if (request->l_end == lock->l_start - 1 &&
					lock_effect == STAY_SAME) {
			topology[0] = request;
			request->l_end = lock->l_end;
			nvertex = 1;
			goto recompute;
		} else {
			return (1);
		}
	}

	if (request->l_end < lock->l_end) {
		if (request->l_start > lock->l_start) {
			if (lock_effect == STAY_SAME) {
				request->l_start = lock->l_start;
				request->l_end = lock->l_end;
				topology[0] = request;
				nvertex = 1;
			} else {
				lock1 = flk_get_lock();
				lock2 = flk_get_lock();
				COPY(lock1, lock);
				COPY(lock2, lock);
				lock1->l_start = lock->l_start;
				lock1->l_end = request->l_start - 1;
				lock2->l_start = request->l_end + 1;
				lock2->l_end = lock->l_end;
				topology[0] = lock1;
				topology[1] = lock2;
				topology[2] = request;
				nvertex = 3;
			}
		} else if (request->l_start < lock->l_start) {
			if (lock_effect == STAY_SAME) {
				request->l_end = lock->l_end;
				topology[0] = request;
				nvertex = 1;
			} else {
				lock1 = flk_get_lock();
				COPY(lock1, lock);
				lock1->l_start = request->l_end + 1;
				topology[0] = lock1;
				topology[1] = request;
				nvertex = 2;
			}
		} else  {
			if (lock_effect == STAY_SAME) {
				request->l_start = lock->l_start;
				request->l_end = lock->l_end;
				topology[0] = request;
				nvertex = 1;
			} else {
				lock1 = flk_get_lock();
				COPY(lock1, lock);
				lock1->l_start = request->l_end + 1;
				topology[0] = lock1;
				topology[1] = request;
				nvertex = 2;
			}
		}
	} else if (request->l_end > lock->l_end) {
		if (request->l_start > lock->l_start)  {
			if (lock_effect == STAY_SAME) {
				request->l_start = lock->l_start;
				topology[0] = request;
				nvertex = 1;
			} else {
				lock1 = flk_get_lock();
				COPY(lock1, lock);
				lock1->l_end = request->l_start - 1;
				topology[0] = lock1;
				topology[1] = request;
				nvertex = 2;
			}
		} else if (request->l_start < lock->l_start)  {
			topology[0] = request;
			nvertex = 1;
		} else {
			topology[0] = request;
			nvertex = 1;
		}
	} else {
		if (request->l_start > lock->l_start) {
			if (lock_effect == STAY_SAME) {
				request->l_start = lock->l_start;
				topology[0] = request;
				nvertex = 1;
			} else {
				lock1 = flk_get_lock();
				COPY(lock1, lock);
				lock1->l_end = request->l_start - 1;
				topology[0] = lock1;
				topology[1] = request;
				nvertex = 2;
			}
		} else if (request->l_start < lock->l_start) {
			topology[0] = request;
			nvertex = 1;
		} else {
			if (lock_effect !=  UNLOCK) {
				topology[0] = request;
				nvertex = 1;
			} else {
				flk_delete_active_lock(lock, 0);
				flk_wakeup(lock, 1);
				flk_free_lock(lock);
				CHECK_SLEEPING_LOCKS(gp);
				CHECK_ACTIVE_LOCKS(gp);
				return (1);
			}
		}
	}

recompute:

	/*
	 * For unlock we don't send the 'request' to for recomputing
	 * dependencies because no lock will add an edge to this.
	 */

	if (lock_effect == UNLOCK) {
		topology[nvertex-1] = NULL;
		nvertex--;
	}
	for (i = 0; i < nvertex; i++) {
		topology[i]->l_state |= RECOMPUTE_LOCK;
		topology[i]->l_color = NO_COLOR;
	}

	ASSERT(FIRST_ADJ(lock) == HEAD(lock));

	/*
	 * we remove the adjacent edges for all vertices' to this vertex
	 * 'lock'.
	 */

	ep = FIRST_IN(lock);
	while (ep != HEAD(lock)) {
		ADJ_LIST_REMOVE(ep);
		ep = NEXT_IN(ep);
	}

	flk_delete_active_lock(lock, 0);

	/* We are ready for recomputing the dependencies now */

	flk_recompute_dependencies(lock, topology, nvertex, 1);

	for (i = 0; i < nvertex; i++) {
		topology[i]->l_state &= ~RECOMPUTE_LOCK;
		topology[i]->l_color = NO_COLOR;
	}


	if (lock_effect == UNLOCK) {
		nvertex++;
	}
	for (i = 0; i < nvertex - 1; i++) {
		flk_insert_active_lock(topology[i]);
	}


	if (lock_effect == DOWNGRADE || lock_effect == UNLOCK) {
		flk_wakeup(lock, 0);
	} else {
		ep = FIRST_IN(lock);
		while (ep != HEAD(lock)) {
			lock->l_sedge = NEXT_IN(ep);
			IN_LIST_REMOVE(ep);
			flk_update_proc_graph(ep, 1);
			flk_free_edge(ep);
			ep = lock->l_sedge;
		}
	}
	flk_free_lock(lock);

	CHECK_SLEEPING_LOCKS(gp);
	CHECK_ACTIVE_LOCKS(gp);
	return (0);
}

/*
 * Insert a lock into the active queue.
 */

static void
flk_insert_active_lock(lock_descriptor_t *new_lock)
{
	graph_t	*gp = new_lock->l_graph;
	vnode_t	*vp = new_lock->l_vnode;
	lock_descriptor_t *first_lock, *lock;

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp);
	first_lock = lock;

	if (first_lock != NULL) {
		for (; (lock->l_vnode == vp &&
			lock->l_start < new_lock->l_start); lock = lock->l_next)
			;
	} else {
		lock = ACTIVE_HEAD(gp);
	}

	lock->l_prev->l_next = new_lock;
	new_lock->l_next = lock;
	new_lock->l_prev = lock->l_prev;
	lock->l_prev = new_lock;

	if (first_lock == NULL || (new_lock->l_start <= first_lock->l_start)) {
		vp->v_filocks = (struct filock *) new_lock;
	}
	new_lock->l_state |= ACTIVE_LOCK;

	CHECK_ACTIVE_LOCKS(gp);
	CHECK_SLEEPING_LOCKS(gp);
}

/*
 * Delete the active lock : Performs two functions depending on the
 * value of second parameter. One is to remove from the active lists
 * only and other is to both remove and free the lock.
 */

static void
flk_delete_active_lock(lock_descriptor_t *lock, int free_lock)
{
	vnode_t *vp = lock->l_vnode;
	graph_t	*gp = lock->l_graph;

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	if (free_lock)
		ASSERT(NO_DEPENDENTS(lock));
	ASSERT(NOT_BLOCKED(lock));
	ASSERT(IS_ACTIVE(lock));
	ASSERT(!IS_SLEEPING(lock));
	ASSERT(!IS_GRANTED(lock));

	ASSERT((vp->v_filocks != NULL));

	if (vp->v_filocks == (struct filock *)lock) {
		vp->v_filocks = (struct filock *)
				((lock->l_next->l_vnode == vp) ? lock->l_next :
								NULL);
	}
	lock->l_next->l_prev = lock->l_prev;
	lock->l_prev->l_next = lock->l_next;
	lock->l_next = lock->l_prev = NULL;
	lock->l_state &= ~ACTIVE_LOCK;

	if (free_lock)
		flk_free_lock(lock);
	CHECK_ACTIVE_LOCKS(gp);
	CHECK_SLEEPING_LOCKS(gp);
}

/*
 * Insert into the sleep queue.
 */

static void
flk_insert_sleeping_lock(lock_descriptor_t *request)
{
	graph_t *gp = request->l_graph;
	vnode_t	*vp = request->l_vnode;
	lock_descriptor_t	*lock;

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	ASSERT(!IS_ACTIVE(request));
	ASSERT(!IS_SLEEPING(request));
	ASSERT(!NOT_BLOCKED(request));
	ASSERT(!IS_GRANTED(request));

	for (lock = gp->sleeping_locks.l_next; (lock != &gp->sleeping_locks &&
		lock->l_vnode < vp); lock = lock->l_next)
		;

	lock->l_prev->l_next = request;
	request->l_prev = lock->l_prev;
	lock->l_prev = request;
	request->l_next = lock;
	request->l_state |= SLEEPING_LOCK;
}

/*
 * Cancelling a sleeping lock implies removing a vertex from the
 * dependency graph and therefore we should recompute the dependencies
 * of all vertices that have a path  to this vertex, w.r.t. all
 * vertices reachable from this vertex.
 */

static void
flk_cancel_sleeping_lock(lock_descriptor_t *request, int remove_from_queue)
{
	graph_t	*gp = request->l_graph;
	vnode_t *vp = request->l_vnode;
	lock_descriptor_t **topology = NULL;
	edge_t	*ep;
	lock_descriptor_t *vertex, *lock;
	int nvertex = 0;
	int i;
	lock_descriptor_t *vertex_stack;

	STACK_INIT(vertex_stack);

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	/*
	 * count number of vertex pointers that has to be allocated
	 * All vertices that are reachable from request.
	 */

	STACK_PUSH(vertex_stack, request, l_stack);

	while ((vertex = STACK_TOP(vertex_stack)) != NULL) {
		STACK_POP(vertex_stack, l_stack);
		for (ep = FIRST_ADJ(vertex); ep != HEAD(vertex);
					ep = NEXT_ADJ(ep)) {
			if (IS_RECOMPUTE(ep->to_vertex))
				continue;
			ep->to_vertex->l_state |= RECOMPUTE_LOCK;
			STACK_PUSH(vertex_stack, ep->to_vertex, l_stack);
			nvertex++;
		}
	}

	/*
	 * allocate memory for holding the vertex pointers
	 */

	if (nvertex) {
		topology = kmem_zalloc(nvertex * sizeof (lock_descriptor_t *),
						KM_SLEEP);
	}

	/*
	 * one more pass to actually store the vertices in the
	 * allocated array.
	 * We first check sleeping locks and then active locks
	 * so that topology array will be in a topological
	 * order.
	 */

	nvertex = 0;
	SET_LOCK_TO_FIRST_SLEEP_VP(gp, lock, vp);

	if (lock) {
		do {
			if (IS_RECOMPUTE(lock)) {
				lock->l_index = nvertex;
				topology[nvertex++] = lock;
			}
			lock->l_color = NO_COLOR;
			lock = lock->l_next;
		} while (lock->l_vnode == vp);
	}

	SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp);

	if (lock) {
		do {
			if (IS_RECOMPUTE(lock)) {
				lock->l_index = nvertex;
				topology[nvertex++] = lock;
			}
			lock->l_color = NO_COLOR;
			lock = lock->l_next;
		} while (lock->l_vnode == vp);
	}

	/*
	 * remove in and out edges of request
	 * They are freed after updating proc_graph below.
	 */

	for (ep = FIRST_IN(request); ep != HEAD(request); ep = NEXT_IN(ep)) {
		ADJ_LIST_REMOVE(ep);
	}


	if (remove_from_queue)
		REMOVE_SLEEP_QUEUE(request);

	/* we are ready to recompute */

	flk_recompute_dependencies(request, topology, nvertex, 1);

	ep = FIRST_ADJ(request);
	while (ep != HEAD(request)) {
		IN_LIST_REMOVE(ep);
		request->l_sedge = NEXT_ADJ(ep);
		ADJ_LIST_REMOVE(ep);
		flk_update_proc_graph(ep, 1);
		flk_free_edge(ep);
		ep = request->l_sedge;
	}


	/*
	 * unset the RECOMPUTE flag in those vertices
	 */

	for (i = 0; i < nvertex; i++) {
		topology[i]->l_state &= ~RECOMPUTE_LOCK;
	}

	/*
	 * free the topology
	 */
	if (nvertex)
		kmem_free((void *)topology,
			(nvertex * sizeof (lock_descriptor_t *)));
	/*
	 * Possibility of some locks unblocked now
	 */

	flk_wakeup(request, 0);

	/*
	 * we expect to have a correctly recomputed graph  now.
	 */
	flk_free_lock(request);
	CHECK_SLEEPING_LOCKS(gp);
	CHECK_ACTIVE_LOCKS(gp);

}

/*
 * Uncoloring the graph is simply to increment the mark value of the graph
 * And only when wrap round takes place will we color all vertices in
 * the graph explicitly.
 */

static void
flk_graph_uncolor(graph_t *gp)
{
	lock_descriptor_t *lock;

	if (gp->mark == UINT_MAX) {
		gp->mark = 1;
	for (lock = ACTIVE_HEAD(gp)->l_next; lock != ACTIVE_HEAD(gp);
					lock = lock->l_next)
			lock->l_color  = 0;

	for (lock = SLEEPING_HEAD(gp)->l_next; lock != SLEEPING_HEAD(gp);
					lock = lock->l_next)
			lock->l_color  = 0;
	} else {
		gp->mark++;
	}
}

/*
 * Wake up locks that are blocked on the given lock.
 */

static void
flk_wakeup(lock_descriptor_t *lock, int adj_list_remove)
{
	edge_t	*ep;
	graph_t	*gp = lock->l_graph;
	lock_descriptor_t	*lck;

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	if (NO_DEPENDENTS(lock))
		return;
	ep = FIRST_IN(lock);
	do {
		/*
		 * delete the edge from the adjacency list
		 * of from vertex. if no more adjacent edges
		 * for this vertex wake this process.
		 */
		lck = ep->from_vertex;
		if (adj_list_remove)
			ADJ_LIST_REMOVE(ep);
		flk_update_proc_graph(ep, 1);
		if (NOT_BLOCKED(lck)) {
			GRANT_WAKEUP(lck);
		}
		lock->l_sedge = NEXT_IN(ep);
		IN_LIST_REMOVE(ep);
		flk_free_edge(ep);
		ep = lock->l_sedge;
	} while (ep != HEAD(lock));
	ASSERT(NO_DEPENDENTS(lock));
}

/*
 * The dependents of request, is checked for its dependency against the
 * locks in topology (called topology because the array is and should be in
 * topological order for this algorithm, if not in topological order the
 * inner loop below might add more edges than necessary. Topological ordering
 * of vertices satisfies the property that all edges will be from left to
 * right i.e., topology[i] can have an edge to  topology[j], iff i<j)
 * If lock l1 in the dependent set of request is dependent (blocked by)
 * on lock l2 in topology but does not have a path to it, we add an edge
 * in the inner loop below.
 *
 * We don't want to add an edge between l1 and l2 if there exists
 * already a path from l1 to l2, so care has to be taken for those vertices
 * that  have two paths to 'request'. These vertices are referred to here
 * as barrier locks.
 *
 * The barriers has to be found (those vertex that originally had two paths
 * to request) because otherwise we may end up adding edges unnecessarily
 * to vertices in topology, and thus barrier vertices can have an edge
 * to a vertex in topology as well a path to it.
 */

static void
flk_recompute_dependencies(lock_descriptor_t *request,
		lock_descriptor_t **topology,
			int nvertex, int update_graph)
{
	lock_descriptor_t *vertex, *lock;
	graph_t	*gp = request->l_graph;
	int i, count;
	int barrier_found = 0;
	edge_t	*ep;
	lock_descriptor_t *vertex_stack;

	STACK_INIT(vertex_stack);

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	if (nvertex == 0)
		return;
	flk_graph_uncolor(request->l_graph);
	barrier_found = flk_find_barriers(request);
	request->l_state |= RECOMPUTE_DONE;

	STACK_PUSH(vertex_stack, request, l_stack);
	request->l_sedge = FIRST_IN(request);


	while ((vertex = STACK_TOP(vertex_stack)) != NULL) {
		if (vertex->l_state & RECOMPUTE_DONE) {
			count = 0;
			goto next_in_edge;
		}
		if (IS_BARRIER(vertex)) {
			/* decrement the barrier count */
			if (vertex->l_index) {
				vertex->l_index--;
				/* this guy will be pushed again anyway ? */
				STACK_POP(vertex_stack, l_stack);
				if (vertex->l_index == 0)  {
				/*
				 * barrier is over we can recompute
				 * dependencies for this lock in the
				 * next stack pop
				 */
					vertex->l_state &= ~BARRIER_LOCK;
				}
				continue;
			}
		}
		vertex->l_state |= RECOMPUTE_DONE;
		flk_graph_uncolor(gp);
		count = flk_color_reachables(vertex);
		for (i = 0; i < nvertex; i++) {
			lock = topology[i];
			if (COLORED(lock))
				continue;
			if (BLOCKS(lock, vertex)) {
				flk_add_edge(vertex, lock, NO_CHECK_CYCLE,
						update_graph);
				COLOR(lock);
				count++;
				count += flk_color_reachables(lock);
			}

		}

next_in_edge:
		if (count == nvertex ||
				vertex->l_sedge == HEAD(vertex)) {
			/* prune the tree below this */
			STACK_POP(vertex_stack, l_stack);
			vertex->l_state &= ~RECOMPUTE_DONE;
			/* update the barrier locks below this! */
			if (vertex->l_sedge != HEAD(vertex) && barrier_found) {
				flk_graph_uncolor(gp);
				flk_update_barriers(vertex);
			}
			continue;
		}

		ep = vertex->l_sedge;
		lock = ep->from_vertex;
		STACK_PUSH(vertex_stack, lock, l_stack);
		lock->l_sedge = FIRST_IN(lock);
		vertex->l_sedge = NEXT_IN(ep);
	}

}

/*
 * Color all reachable vertices from vertex that belongs to topology (here
 * those that have RECOMPUTE_LOCK set in their state) and yet uncolored.
 *
 * Note: we need to use a different stack_link l_stack1 because this is
 * called from flk_recompute_dependencies() that already uses a stack with
 * l_stack as stack_link.
 */

static int
flk_color_reachables(lock_descriptor_t *vertex)
{
	lock_descriptor_t *ver, *lock;
	int count;
	edge_t	*ep;
	lock_descriptor_t *vertex_stack;

	STACK_INIT(vertex_stack);

	STACK_PUSH(vertex_stack, vertex, l_stack1);
	count = 0;
	while ((ver = STACK_TOP(vertex_stack)) != NULL) {

		STACK_POP(vertex_stack, l_stack1);
		for (ep = FIRST_ADJ(ver); ep != HEAD(ver);
					ep = NEXT_ADJ(ep)) {
			lock = ep->to_vertex;
			if (COLORED(lock))
				continue;
			COLOR(lock);
			if (IS_RECOMPUTE(lock))
				count++;
			STACK_PUSH(vertex_stack, lock, l_stack1);
		}

	}
	return (count);
}

/*
 * Called from flk_recompute_dependencies() this routine decrements
 * the barrier count of barrier vertices that are reachable from lock.
 */

static void
flk_update_barriers(lock_descriptor_t *lock)
{
	lock_descriptor_t *vertex, *lck;
	edge_t	*ep;
	lock_descriptor_t *vertex_stack;

	STACK_INIT(vertex_stack);

	STACK_PUSH(vertex_stack, lock, l_stack1);

	while ((vertex = STACK_TOP(vertex_stack)) != NULL) {
		STACK_POP(vertex_stack, l_stack1);
		for (ep = FIRST_IN(vertex); ep != HEAD(vertex);
						ep = NEXT_IN(ep)) {
			lck = ep->from_vertex;
			if (COLORED(lck)) {
				if (IS_BARRIER(lck)) {
					ASSERT(lck->l_index > 0);
					lck->l_index--;
					if (lck->l_index == 0)
						lck->l_state &= ~BARRIER_LOCK;
				}
				continue;
			}
			COLOR(lck);
			if (IS_BARRIER(lck)) {
				ASSERT(lck->l_index > 0);
				lck->l_index--;
				if (lck->l_index == 0)
					lck->l_state &= ~BARRIER_LOCK;
			}
			STACK_PUSH(vertex_stack, lck, l_stack1);
		}
	}
}

/*
 * Finds all vertices that are reachable from 'lock' more than once and
 * mark them as barrier vertices and increment their barrier count.
 * The barrier count is one minus the total number of paths from lock
 * to that vertex.
 */

static int
flk_find_barriers(lock_descriptor_t *lock)
{
	lock_descriptor_t *vertex, *lck;
	int found = 0;
	edge_t	*ep;
	lock_descriptor_t *vertex_stack;

	STACK_INIT(vertex_stack);

	STACK_PUSH(vertex_stack, lock, l_stack1);

	while ((vertex = STACK_TOP(vertex_stack)) != NULL) {
		STACK_POP(vertex_stack, l_stack1);
		for (ep = FIRST_IN(vertex); ep != HEAD(vertex);
						ep = NEXT_IN(ep)) {
			lck = ep->from_vertex;
			if (COLORED(lck)) {
				/* this is a barrier */
				lck->l_state |= BARRIER_LOCK;
				/* index will have barrier count */
				lck->l_index++;
				if (!found)
					found = 1;
				continue;
			}
			COLOR(lck);
			lck->l_index = 0;
			STACK_PUSH(vertex_stack, lck, l_stack1);
		}
	}
	return (found);
}

/*
 * Finds the first lock that is mainly responsible for blocking this
 * request.  If there is no such lock, request->l_flock.l_type is set to
 * F_UNLCK.  Otherwise, request->l_flock is filled in with the particulars
 * of the blocking lock.
 *
 * Note: It is possible a request is blocked by a sleeping lock because
 * of the fairness policy used in flk_process_request() to construct the
 * dependencies. (see comments before flk_process_request()).
 */

static void
flk_get_first_blocking_lock(lock_descriptor_t *request)
{
	graph_t	*gp = request->l_graph;
	vnode_t *vp = request->l_vnode;
	lock_descriptor_t *lock, *blocker;

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	blocker = NULL;
	SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp);

	if (lock) {
		do {
			if (BLOCKS(lock, request)) {
				blocker = lock;
				break;
			}
			lock = lock->l_next;
		} while (lock->l_vnode == vp);
	}

	if (blocker == NULL) {
		SET_LOCK_TO_FIRST_SLEEP_VP(gp, lock, vp);
		if (lock) {
			do {
				if (BLOCKS(lock, request)) {
					blocker = lock;
					break;
				}
				lock = lock->l_next;
			} while (lock->l_vnode == vp);
		}
	}
	if (blocker) {
		report_blocker(blocker, request);
	} else
		request->l_flock.l_type = F_UNLCK;
}

/*
 * Determine whether there are any locks for the given vnode with a remote
 * sysid.  Returns zero if not, non-zero if there are.
 *
 * Note that the return value from this function is potentially invalid
 * once it has been returned.  The caller is responsible for providing its
 * own synchronization mechanism to ensure that the return value is useful
 * (e.g., see nfs_lockcompletion()).
 */

int
flk_has_remote_locks(vnode_t *vp)
{
	lock_descriptor_t *lock;
	int result = 0;
	graph_t *gp;

	mutex_enter(&flock_lock);
	gp = lock_graph[HASH_INDEX(vp)];
	mutex_exit(&flock_lock);
	if (gp == NULL) {
		return (0);
	}

	mutex_enter(&gp->gp_mutex);

	SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp);

	if (lock) {
		while (lock->l_vnode == vp) {
			if (IS_REMOTE(lock)) {
				result = 1;
				goto done;
			}
			lock = lock->l_next;
		}
	}

	SET_LOCK_TO_FIRST_SLEEP_VP(gp, lock, vp);

	if (lock) {
		while (lock->l_vnode == vp) {
			if (IS_REMOTE(lock)) {
				result = 1;
				goto done;
			}
			lock = lock->l_next;
		}
	}

done:
	mutex_exit(&gp->gp_mutex);
	return (result);
}

/*
 * Determine if there are any locks on the given vfs.
 * Returns zero if not, non-zero if there are.
 *
 * This routine has the same synchronization issues as
 * flk_has_remote_locks.
 */

int
flk_vfs_has_locks(struct vfs *vfsp)
{
	int		has_locks = 0;
	lock_descriptor_t	*lock;
	graph_t 	*gp;
	int		i;

	for (i = 0; i < HASH_SIZE && !has_locks; i++) {
		mutex_enter(&flock_lock);
		gp = lock_graph[i];
		mutex_exit(&flock_lock);
		if (gp == NULL) {
			continue;
		}

		mutex_enter(&gp->gp_mutex);
		for (lock = ACTIVE_HEAD(gp)->l_next;
		    lock != ACTIVE_HEAD(gp) && !has_locks;
		    lock = lock->l_next) {
			if (lock->l_vnode->v_vfsp == vfsp) {
				has_locks = 1;
			}
		}

		for (lock = SLEEPING_HEAD(gp)->l_next;
		    lock != SLEEPING_HEAD(gp) && !has_locks;
		    lock = lock->l_next) {
			if (lock->l_vnode->v_vfsp == vfsp) {
				has_locks = 1;
			}
		}
		mutex_exit(&gp->gp_mutex);
	}

	return (has_locks);
}

/*
 * Determine if there are any locks owned by the given sysid.
 * Returns zero if not, non-zero if there are.  Note that this return code
 * could be derived from flk_get_{sleeping,active}_locks, but this routine
 * avoids all the memory allocations of those routines.
 *
 * This routine has the same synchronization issues as
 * flk_has_remote_locks.
 */

int
flk_sysid_has_locks(long sysid)
{
	int		has_locks = 0;
	lock_descriptor_t	*lock;
	graph_t 	*gp;
	int		i;

	for (i = 0; i < HASH_SIZE && !has_locks; i++) {
		mutex_enter(&flock_lock);
		gp = lock_graph[i];
		mutex_exit(&flock_lock);
		if (gp == NULL) {
			continue;
		}

		mutex_enter(&gp->gp_mutex);
		for (lock = ACTIVE_HEAD(gp)->l_next;
		    lock != ACTIVE_HEAD(gp) && !has_locks;
		    lock = lock->l_next) {
			if (lock->l_flock.l_sysid == sysid) {
				has_locks = 1;
			}
		}

		for (lock = SLEEPING_HEAD(gp)->l_next;
		    lock != SLEEPING_HEAD(gp) && !has_locks;
		    lock = lock->l_next) {
			if (lock->l_flock.l_sysid == sysid) {
				has_locks = 1;
			}
		}
		mutex_exit(&gp->gp_mutex);
	}

	return (has_locks);
}

/*
 * Delete all locks in the system that belongs to the sysid of the request.
 */

static void
flk_delete_locks_by_sysid(lock_descriptor_t *request)
{
	long	sysid  = request->l_flock.l_sysid;
	lock_descriptor_t *lock, *nlock;
	graph_t	*gp;
	int i;

	ASSERT(MUTEX_HELD(&request->l_graph->gp_mutex));
	ASSERT(sysid != 0);

	mutex_exit(&request->l_graph->gp_mutex);

	for (i = 0; i < HASH_SIZE; i++) {
		mutex_enter(&flock_lock);
		gp = lock_graph[i];
		mutex_exit(&flock_lock);

		if (gp == NULL)
			continue;

		mutex_enter(&gp->gp_mutex);

		/* signal sleeping requests so that they bail out */
		lock = SLEEPING_HEAD(gp)->l_next;
		while (lock != SLEEPING_HEAD(gp)) {
			nlock = lock->l_next;
			if (lock->l_flock.l_sysid == sysid) {
				INTERRUPT_WAKEUP(lock);
			}
			lock = nlock;
		}

		/* delete active locks */
		lock = ACTIVE_HEAD(gp)->l_next;
		while (lock != ACTIVE_HEAD(gp)) {
			nlock = lock->l_next;
			if (lock->l_flock.l_sysid == sysid) {
				flk_delete_active_lock(lock, 0);
				flk_wakeup(lock, 1);
				flk_free_lock(lock);
			}
			lock = nlock;
		}
		mutex_exit(&gp->gp_mutex);
	}

	mutex_enter(&request->l_graph->gp_mutex);

}

/*
 * Search for a sleeping lock manager lock which matches exactly this lock
 * request; if one is found, fake a signal to cancel it.
 *
 * Return 1 if a matching lock was found, 0 otherwise.
 */

static int
flk_canceled(lock_descriptor_t *request)
{
	lock_descriptor_t *lock, *nlock;
	graph_t *gp = request->l_graph;
	vnode_t *vp = request->l_vnode;

	ASSERT(MUTEX_HELD(&gp->gp_mutex));
	ASSERT(IS_LOCKMGR(request));
	SET_LOCK_TO_FIRST_SLEEP_VP(gp, lock, vp);

	if (lock) {
		while (lock->l_vnode == vp) {
			nlock = lock->l_next;
			if (SAME_OWNER(lock, request) &&
				lock->l_start == request->l_start &&
					lock->l_end == request->l_end) {
				INTERRUPT_WAKEUP(lock);
				return (1);
			}
			lock = nlock;
		}
	}
	return (0);
}

/*
 * Remove all the locks for the vnode belonging to the given pid and sysid.
 */

int
cleanlocks(vnode_t *vp, pid_t pid, sysid_t sysid)
{
	graph_t	*gp;
	lock_descriptor_t *lock, *nlock;
	lock_descriptor_t *link_stack;

	STACK_INIT(link_stack);

	mutex_enter(&flock_lock);
	gp = lock_graph[HASH_INDEX(vp)];
	mutex_exit(&flock_lock);

	if (gp == NULL)
		return (0);
	mutex_enter(&gp->gp_mutex);

	CHECK_SLEEPING_LOCKS(gp);
	CHECK_ACTIVE_LOCKS(gp);

	SET_LOCK_TO_FIRST_SLEEP_VP(gp, lock, vp);

	if (lock) {
		do {
			nlock = lock->l_next;
			if ((lock->l_flock.l_pid == pid ||
					pid == IGN_PID) &&
				lock->l_flock.l_sysid == sysid) {
				CANCEL_WAKEUP(lock);
			}
			lock = nlock;
		} while (lock->l_vnode == vp);
	}

	SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp);

	if (lock) {
		do {
			nlock = lock->l_next;
			if ((lock->l_flock.l_pid == pid ||
					pid == IGN_PID) &&
				lock->l_flock.l_sysid == sysid) {
				flk_delete_active_lock(lock, 0);
				STACK_PUSH(link_stack, lock, l_stack);
			}
			lock = nlock;
		} while (lock->l_vnode == vp);
	}

	while ((lock = STACK_TOP(link_stack)) != NULL) {
		STACK_POP(link_stack, l_stack);
		flk_wakeup(lock, 1);
		flk_free_lock(lock);
	}

	CHECK_SLEEPING_LOCKS(gp);
	CHECK_ACTIVE_LOCKS(gp);
	CHECK_OWNER_LOCKS(gp, pid, sysid, vp);
	mutex_exit(&gp->gp_mutex);
	return (0);
}

/*
 * Called from 'fs' read and write routines for files that have mandatory
 * locking enabled.
 */

int
chklock(vp, iomode, offset, len, fmode)
	register struct vnode	*vp;
	int 			iomode;
	off_t 			offset;
	int 			len;
	int 			fmode;
{
	register int	i;
	struct flock 	bf;
	int 		error = 0;

	bf.l_type = (iomode & FWRITE) ? F_WRLCK : F_RDLCK;
	bf.l_whence = 0;
	bf.l_start = offset;
	bf.l_len = len;
	bf.l_pid = curproc->p_pid;
	bf.l_sysid = 0;
	i = (fmode & (FNDELAY|FNONBLOCK)) ? INOFLCK : INOFLCK|SLPFLCK;
	if ((i = reclock(vp, &bf, i, 0, offset)) != 0 ||
	    bf.l_type != F_UNLCK)
		error = i ? i : EAGAIN;
	return (error);
}

/*
 * convoff - converts the given data (start, whence) to the
 * given whence.
 */
int
convoff(vp, lckdat, whence, offset)
	struct vnode 	*vp;
	struct flock 	*lckdat;
	int 		whence;
	off_t 		offset;
{
	int 		error;
	struct vattr 	vattr;

	if ((lckdat->l_whence == 2) || (whence == 2)) {
		vattr.va_mask = AT_SIZE;
		if (error = VOP_GETATTR(vp, &vattr, 0, CRED()))
			return (error);
	}

	switch (lckdat->l_whence) {
	case 1:
		lckdat->l_start += offset;
		break;
	case 2:
		lckdat->l_start += vattr.va_size;
		/* FALLTHRU */
	case 0:
		break;
	default:
		return (EINVAL);
	}

	if (lckdat->l_start < 0)
		return (EINVAL);

	switch (whence) {
	case 1:
		lckdat->l_start -= offset;
		break;
	case 2:
		lckdat->l_start -= vattr.va_size;
		/* FALLTHRU */
	case 0:
		break;
	default:
		return (EINVAL);
	}

	lckdat->l_whence = (short)whence;
	return (0);
}


/* 	proc_graph function definitions */

/*
 * Function checks for deadlock due to the new 'lock'. If deadlock found
 * edges of this lock are freed and returned.
 */

static int
flk_check_deadlock(lock_descriptor_t *lock)
{
	proc_vertex_t	*start_vertex, *pvertex;
	proc_vertex_t *dvertex;
	proc_edge_t *pep, *ppep;
	edge_t	*ep, *nep;
	proc_vertex_t *process_stack;

	STACK_INIT(process_stack);

	mutex_enter(&flock_lock);
	start_vertex = flk_get_proc_vertex(lock);
	ASSERT(start_vertex != NULL);

	/* construct the edges from this process to other processes */

	ep = FIRST_ADJ(lock);
	while (ep != HEAD(lock)) {
		proc_vertex_t *adj_proc;

		adj_proc = flk_get_proc_vertex(ep->to_vertex);
		for (pep = start_vertex->edge; pep != NULL; pep = pep->next) {
			if (pep->to_proc == adj_proc) {
				ASSERT(pep->refcount);
				pep->refcount++;
				break;
			}
		}
		if (pep == NULL) {
			pep = flk_get_proc_edge();
			pep->to_proc = adj_proc;
			pep->refcount = 1;
			adj_proc->incount++;
			pep->next = start_vertex->edge;
			start_vertex->edge = pep;
		}
		ep = NEXT_ADJ(ep);
	}

	ep = FIRST_IN(lock);

	while (ep != HEAD(lock)) {
		proc_vertex_t *in_proc;

		in_proc = flk_get_proc_vertex(ep->from_vertex);

		for (pep = in_proc->edge; pep != NULL; pep = pep->next) {
			if (pep->to_proc == start_vertex) {
				ASSERT(pep->refcount);
				pep->refcount++;
				break;
			}
		}
		if (pep == NULL) {
			pep = flk_get_proc_edge();
			pep->to_proc = start_vertex;
			pep->refcount = 1;
			start_vertex->incount++;
			pep->next = in_proc->edge;
			in_proc->edge = pep;
		}
		ep = NEXT_IN(ep);
	}

	if (start_vertex->incount == 0) {
		mutex_exit(&flock_lock);
		return (0);
	}

	flk_proc_graph_uncolor();

	start_vertex->p_sedge = start_vertex->edge;

	STACK_PUSH(process_stack, start_vertex, p_stack);

	while ((pvertex = STACK_TOP(process_stack)) != NULL) {
		for (pep = pvertex->p_sedge; pep != NULL; pep = pep->next) {
			dvertex = pep->to_proc;
			if (!PROC_ARRIVED(dvertex)) {
				STACK_PUSH(process_stack, dvertex, p_stack);
				dvertex->p_sedge = dvertex->edge;
				PROC_ARRIVE(pvertex);
				pvertex->p_sedge = pep->next;
				break;
			}
			if (!PROC_DEPARTED(dvertex))
				goto deadlock;
		}
		if (pep == NULL) {
			PROC_DEPART(pvertex);
			STACK_POP(process_stack, p_stack);
		}
	}
	mutex_exit(&flock_lock);
	return (0);

deadlock:

	/* we remove all lock edges and proc edges */

	ep = FIRST_ADJ(lock);
	while (ep != HEAD(lock)) {
		proc_vertex_t *adj_proc;
		adj_proc = flk_get_proc_vertex(ep->to_vertex);
		nep = NEXT_ADJ(ep);
		IN_LIST_REMOVE(ep);
		ADJ_LIST_REMOVE(ep);
		flk_free_edge(ep);
		ppep = start_vertex->edge;
		for (pep = start_vertex->edge; pep != NULL; ppep = pep,
						pep = ppep->next) {
			if (pep->to_proc == adj_proc) {
				pep->refcount--;
				if (pep->refcount == 0) {
					if (pep == ppep) {
						start_vertex->edge = pep->next;
					} else {
						ppep->next = pep->next;
					}
					adj_proc->incount--;
					flk_proc_release(adj_proc);
					flk_free_proc_edge(pep);
				}
				break;
			}
		}
		ep = nep;
	}
	ep = FIRST_IN(lock);
	while (ep != HEAD(lock)) {
		proc_vertex_t *in_proc;
		in_proc = flk_get_proc_vertex(ep->from_vertex);
		nep = NEXT_IN(ep);
		IN_LIST_REMOVE(ep);
		ADJ_LIST_REMOVE(ep);
		flk_free_edge(ep);
		ppep = in_proc->edge;
		for (pep = in_proc->edge; pep != NULL; ppep = pep,
						pep = ppep->next) {
			if (pep->to_proc == start_vertex) {
				pep->refcount--;
				if (pep->refcount == 0) {
					if (pep == ppep) {
						in_proc->edge = pep->next;
					} else {
						ppep->next = pep->next;
					}
					start_vertex->incount--;
					flk_proc_release(in_proc);
					flk_free_proc_edge(pep);
				}
				break;
			}
		}
		ep = nep;
	}
	flk_proc_release(start_vertex);
	mutex_exit(&flock_lock);
	return (1);
}

/*
 * Get a proc vertex. If lock's pvertex value gets a correct proc vertex
 * from the list we return that, otherwise we allocate one. If necessary
 * we grow the list of vertices also.
 */

proc_vertex_t *
flk_get_proc_vertex(lock_descriptor_t *lock)
{
	int i;
	proc_vertex_t	*pv;
	proc_vertex_t	**palloc;

	ASSERT(MUTEX_HELD(&flock_lock));
	if (lock->pvertex != -1) {
		ASSERT(lock->pvertex >= 0);
		pv = pgraph.proc[lock->pvertex];
		if (pv != NULL && PROC_SAME_OWNER(lock, pv)) {
			return (pv);
		}
	}
	for (i = 0; i < pgraph.gcount; i++) {
		pv = pgraph.proc[i];
		if (pv != NULL && PROC_SAME_OWNER(lock, pv)) {
			lock->pvertex = pv->index = i;
			return (pv);
		}
	}
	pv = kmem_zalloc(sizeof (struct proc_vertex), KM_SLEEP);
	pv->pid = lock->l_flock.l_pid;
	pv->sysid = lock->l_flock.l_sysid;
	flk_proc_vertex_allocs++;
	if (pgraph.free != 0) {
		for (i = 0; i < pgraph.gcount; i++) {
			if (pgraph.proc[i] == NULL) {
				pgraph.proc[i] = pv;
				lock->pvertex = pv->index = i;
				pgraph.free--;
				return (pv);
			}
		}
	}
	palloc = kmem_zalloc((pgraph.gcount + PROC_CHUNK) *
				sizeof (proc_vertex_t *), KM_SLEEP);

	if (pgraph.proc) {
		bcopy((caddr_t) pgraph.proc, (caddr_t) palloc,
			(size_t)(pgraph.gcount * sizeof (proc_vertex_t *)));

		kmem_free(pgraph.proc,
			(pgraph.gcount * sizeof (proc_vertex_t *)));
	}
	pgraph.proc = palloc;
	pgraph.free += (PROC_CHUNK - 1);
	pv->index = lock->pvertex = pgraph.gcount;
	pgraph.gcount += PROC_CHUNK;
	pgraph.proc[pv->index] = pv;
	return (pv);
}

/*
 * Allocate a proc edge.
 */

proc_edge_t *
flk_get_proc_edge()
{
	proc_edge_t *pep;

	pep = kmem_zalloc(sizeof (proc_edge_t), KM_SLEEP);
	flk_proc_edge_allocs++;
	return (pep);
}

/*
 * Free the proc edge. Called whenever its reference count goes to zero.
 */

static void
flk_free_proc_edge(proc_edge_t *pep)
{
	ASSERT(pep->refcount == 0);
	kmem_free((void *)pep, sizeof (proc_edge_t));
	flk_proc_edge_frees++;
}

/*
 * Color the graph explicitly done only when the mark value hits max value.
 */

static void
flk_proc_graph_uncolor()
{
	int i;

	if (pgraph.mark == UINT_MAX) {
		for (i = 0; i < pgraph.gcount; i++)
			if (pgraph.proc[i] != NULL) {
				pgraph.proc[i]->atime = 0;
				pgraph.proc[i]->dtime = 0;
			}
		pgraph.mark = 1;
	} else {
		pgraph.mark++;
	}
}

/*
 * Release the proc vertex iff both there are no in edges and out edges
 */

static void
flk_proc_release(proc_vertex_t *proc)
{
	ASSERT(MUTEX_HELD(&flock_lock));
	if (proc->edge == NULL && proc->incount == 0) {
		pgraph.proc[proc->index] = NULL;
		pgraph.free++;
		kmem_free(proc, sizeof (proc_vertex_t));
		flk_proc_vertex_frees++;
	}
}

/*
 * Updates process graph to reflect change in a lock_graph.
 * Note: We should call this function only after we have a correctly
 * recomputed lock graph. Otherwise we might miss a deadlock detection.
 * eg: in function flk_relation() we call this function after flk_recompute_
 * dependencies() otherwise if a process tries to lock a vnode hashed
 * into another graph it might sleep for ever.
 */

static void
flk_update_proc_graph(edge_t *ep, int delete)
{
	proc_vertex_t *toproc, *fromproc;
	proc_edge_t *pep, *prevpep;

	mutex_enter(&flock_lock);
	toproc = flk_get_proc_vertex(ep->to_vertex);
	fromproc = flk_get_proc_vertex(ep->from_vertex);

	if (!delete)
		goto add;
	pep = prevpep = fromproc->edge;

	ASSERT(pep != NULL);
	while (pep != NULL) {
		if (pep->to_proc == toproc) {
			ASSERT(pep->refcount > 0);
			pep->refcount--;
			if (pep->refcount == 0) {
				if (pep == prevpep) {
					fromproc->edge = pep->next;
				} else {
					prevpep->next = pep->next;
				}
				toproc->incount--;
				flk_proc_release(toproc);
				flk_free_proc_edge(pep);
			}
			break;
		}
		prevpep = pep;
		pep = pep->next;
	}
	flk_proc_release(fromproc);
	mutex_exit(&flock_lock);
	return;
add:

	pep = fromproc->edge;

	while (pep != NULL) {
		if (pep->to_proc == toproc) {
			ASSERT(pep->refcount > 0);
			pep->refcount++;
			break;
		}
		pep = pep->next;
	}
	if (pep == NULL) {
		pep = flk_get_proc_edge();
		pep->to_proc = toproc;
		pep->refcount = 1;
		toproc->incount++;
		pep->next = fromproc->edge;
		fromproc->edge = pep;
	}
	mutex_exit(&flock_lock);
}

/*
 * Set the control status for lock manager requests.
 *
 * Note that when this routine is called with FLK_WAKEUP_SLEEPERS, there
 * may be locks requests that have gotten started but not finished.  In
 * particular, there may be blocking requests that are in the callback code
 * before sleeping (so they're not holding the lock for the graph).  If
 * such a thread reacquires the graph's lock (to go to sleep) after
 * flk_lockmgr_status is set to a non-up value, it will notice the status
 * and bail out.  If the request gets granted before the thread can check
 * flk_lockmgr_status, let it continue normally.  It will get flushed when
 * we are called with FLK_LOCKMGR_DOWN.
 */

void
flk_set_lockmgr_status(flk_lockmgr_status_t status)
{
	int i;
	graph_t *gp;

	mutex_enter(&flock_lock);
	flk_lockmgr_status = status;
	mutex_exit(&flock_lock);

	/*
	 * If the lock manager is coming back up, all that's needed is to
	 * propagate this information to the graphs.  If the lock manager
	 * is going down, additional action is required, and each graph's
	 * copy of the state is updated atomically with this other action.
	 */
	switch (status) {
	case FLK_LOCKMGR_UP:
		for (i = 0; i < HASH_SIZE; i++) {
			mutex_enter(&flock_lock);
			gp = lock_graph[i];
			mutex_exit(&flock_lock);
			if (gp == NULL)
				continue;
			mutex_enter(&gp->gp_mutex);
			gp->lockmgr_status = status;
			mutex_exit(&gp->gp_mutex);
		}
		break;
	case FLK_WAKEUP_SLEEPERS:
		wakeup_sleeping_lockmgr_locks();
		break;
	case FLK_LOCKMGR_DOWN:
		unlock_lockmgr_granted();
		break;
	default:
		panic("flk_set_lockmgr_status: bad status (%d)", status);
		break;
	}
}

/*
 * This routine returns all the locks associated with a particular owner
 * ({sysid, pid}) that are active or sleeping.  If pid == NOPID, the pid is
 * ignored and only the sysid is considered in determining a match.
 *
 * A list containing the vnode pointer and an flock structure
 * describing the lock is returned.  Each element in the list is
 * dynammically allocated and must be freed by the caller.  The
 * last item in the list is denoted by a NULL value in the ll_next
 * field.
 *
 * The vnode pointers returned are held.  The caller is responsible
 * for releasing these.  Note that the returned list is only a snapshot of
 * the current lock information, and that it is a snapshot of a moving
 * target (only one graph is locked at a time).
 */

locklist_t *
get_lock_list(int list_type, long sysid, pid_t pid)
{
	lock_descriptor_t	*lock;
	lock_descriptor_t	*graph_head;
	locklist_t		listhead;
	locklist_t		*llheadp;
	locklist_t		*llp;
	locklist_t		*lltp;
	graph_t			*gp;
	int			i;

	ASSERT(list_type == ACTIVE_LOCK || list_type == SLEEPING_LOCK);

	/*
	 * Get a pointer to something to use as a list head while building
	 * the rest of the list.
	 */
	llheadp = &listhead;
	lltp = llheadp;
	llheadp->ll_next = (locklist_t *) NULL;

	for (i = 0; i < HASH_SIZE; i++) {
		mutex_enter(&flock_lock);
		gp = lock_graph[i];
		mutex_exit(&flock_lock);
		if (gp == NULL) {
			continue;
		}

		mutex_enter(&gp->gp_mutex);
		graph_head = (list_type == ACTIVE_LOCK) ?
			ACTIVE_HEAD(gp) : SLEEPING_HEAD(gp);
		for (lock = graph_head->l_next;
		    lock != graph_head;
		    lock = lock->l_next) {
			if (lock->l_flock.l_sysid == sysid &&
			    ((pid == NOPID) || (lock->l_flock.l_pid == pid))) {
				/*
				 * A matching lock was found.  Allocate
				 * space for a new locklist entry and fill
				 * it in.
				 */
				llp = kmem_alloc(sizeof (locklist_t), KM_SLEEP);
				lltp->ll_next = llp;
				VN_HOLD(lock->l_vnode);
				llp->ll_vp = lock->l_vnode;
				create_flock(lock, &(llp->ll_flock));
				llp->ll_next = (locklist_t *) NULL;
				lltp = llp;
			}
		}
		mutex_exit(&gp->gp_mutex);
	}

	llp = llheadp->ll_next;
	return (llp);
}

/*
 * These two functions are simply interfaces to get_lock_list.  They return
 * a list of sleeping or active locks for the given sysid and pid.  See
 * get_lock_list for details.
 */

locklist_t *
flk_get_sleeping_locks(long sysid, pid_t pid)
{
	return (get_lock_list(SLEEPING_LOCK, sysid, pid));
}

locklist_t *
flk_get_active_locks(long sysid, pid_t pid)
{
	return (get_lock_list(ACTIVE_LOCK, sysid, pid));
}

/*
 * Find all sleeping lock manager requests and poke them.
 */

static void
wakeup_sleeping_lockmgr_locks()
{
	lock_descriptor_t *lock;
	lock_descriptor_t *nlock = NULL; /* next lock */
	int i;
	graph_t *gp;

	for (i = 0; i < HASH_SIZE; i++) {
		mutex_enter(&flock_lock);
		gp = lock_graph[i];
		mutex_exit(&flock_lock);
		if (gp == NULL) {
			continue;
		}

		mutex_enter(&gp->gp_mutex);
		gp->lockmgr_status = FLK_WAKEUP_SLEEPERS;
		for (lock = SLEEPING_HEAD(gp)->l_next;
		    lock != SLEEPING_HEAD(gp);
		    lock = nlock) {
			nlock = lock->l_next;
			if (IS_LOCKMGR(lock)) {
				INTERRUPT_WAKEUP(lock);
			}
		}
		mutex_exit(&gp->gp_mutex);
	}
}

/*
 * Find all active (granted) lock manager locks and release them.
 */

static void
unlock_lockmgr_granted()
{
	lock_descriptor_t *lock;
	lock_descriptor_t *nlock = NULL; /* next lock */
	int i;
	graph_t *gp;

	for (i = 0; i < HASH_SIZE; i++) {
		mutex_enter(&flock_lock);
		gp = lock_graph[i];
		mutex_exit(&flock_lock);
		if (gp == NULL) {
			continue;
		}

		mutex_enter(&gp->gp_mutex);
		gp->lockmgr_status = FLK_LOCKMGR_DOWN;
		for (lock = ACTIVE_HEAD(gp)->l_next;
		    lock != ACTIVE_HEAD(gp);
		    lock = nlock) {
			nlock = lock->l_next;
			if (IS_LOCKMGR(lock)) {
				ASSERT(IS_ACTIVE(lock));
				flk_delete_active_lock(lock, 0);
				flk_wakeup(lock, 1);
				flk_free_lock(lock);
			}
		}
		mutex_exit(&gp->gp_mutex);
	}
}

/*
 * Wait until a lock is granted, cancelled, or interrupted.
 */

static void
wait_for_lock(lock_descriptor_t *request)
{
	graph_t *gp = request->l_graph;

	ASSERT(MUTEX_HELD(&gp->gp_mutex));

	while (!(IS_GRANTED(request)) && !(IS_CANCELLED(request)) &&
	    !(IS_INTERRUPTED(request))) {
		if (!cv_wait_sig(&request->l_cv, &gp->gp_mutex)) {
			request->l_state |= INTERRUPTED_LOCK;
		}
	}
}

/*
 * Create an flock structure from the existing lock information
 *
 * This routine is used to create flock structures for the lock manager
 * to use in a reclaim request.  Since the lock was orginated on this
 * host, it must be conforming to UNIX semantics, so no checking is
 * done to make sure it falls within the lower half of the 32-bit range.
 */

static void
create_flock(lock_descriptor_t *lp, flock_t *flp)
{
	ASSERT(lp->l_end == MAX_U_OFF_T || lp->l_end <= MAXEND);
	ASSERT(lp->l_end >= lp->l_start);

	flp->l_type = lp->l_type;
	flp->l_whence = 0;
	flp->l_start = lp->l_start;
	flp->l_len = (lp->l_end == MAX_U_OFF_T) ? 0 :
		(lp->l_end - lp->l_start + 1);
	flp->l_sysid = lp->l_flock.l_sysid;
	flp->l_pid = lp->l_flock.l_pid;
}

/*
 * Convert flock_t data describing a lock range into unsigned long starting
 * and ending points, which are put into lock_request.  Returns 0 or an
 * errno value.
 */

int
flk_convert_lock_data(vnode_t *vp, flock_t *flp,
	u_off_t *start, u_off_t *end, off_t offset)
{
	struct vattr	vattr;
	int	error;

	/*
	 * Determine the starting point of the request
	 */
	switch (flp->l_whence) {
	case 0:		/* SEEK_SET */
		*start = (u_off_t) flp->l_start;
		break;
	case 1:		/* SEEK_CUR */
		*start = (u_off_t) (flp->l_start + offset);
		break;
	case 2:		/* SEEK_END */
		vattr.va_mask = AT_SIZE;
		if (error = VOP_GETATTR(vp, &vattr, 0, CRED()))
			return (error);
		*start = (u_off_t) (flp->l_start + vattr.va_size);
		break;
	default:
		return (EINVAL);
	}

	/*
	 * Determine the range covered by the request.
	 */
	if (flp->l_len == 0)
		*end = MAX_U_OFF_T;
	else if (flp->l_len > 0)
		*end = (u_off_t) (*start + flp->l_len - 1);
	else {
		/*
		 * Negative length; why do we even allow this ?
		 * Because this allows easy specification of
		 * the last n bytes of the file.
		 */
		*end = *start;
		*start += (u_off_t) flp->l_len;
		(*start)++;
	}
	return (0);
}

/*
 * Check the validity of lock data.  This can used by the NFS
 * frlock routines to check data before contacting the server.  The
 * server must support semantics that aren't as restrictive as
 * the UNIX API, so the NFS client is required to check.
 * Note that MAXEND is the max positive value of a 32 bit signed
 * number as required by the UNIX API.
 */

int
flk_check_lock_data(u_off_t start, u_off_t  end)
{
	if ((start > MAXEND) ||
		((end > MAXEND) && (end != MAX_U_OFF_T))) {
		return (EINVAL);
	}
	if (start > end) {
	    return (EINVAL);
	}
	return (0);
}

/*
 * Fill in request->l_flock with information about the lock blocking the
 * request.  The complexity here is that lock manager requests are allowed
 * to see into the upper part of the 32-bit address range, whereas local
 * requests are only allowed to see signed values.
 *
 * What should be done when "blocker" is a lock manager lock that uses the
 * upper portion of the 32-bit range, but "request" is local?  Since the
 * request has already been determined to have been blocked by the blocker,
 * at least some portion of "blocker" must be in the range of the request,
 * or the request extends to the end of file.  For the first case, the
 * portion in the lower range is returned with the indication that it goes
 * "to EOF."  For the second case, the last byte of the lower range is
 * returned with the indication that it goes "to EOF."
 */

static void
report_blocker(lock_descriptor_t *blocker, lock_descriptor_t *request)
{
	flock_t *flrp;			/* l_flock portion of request */

	ASSERT(blocker != NULL);

	flrp = &request->l_flock;
	flrp->l_whence = 0;
	flrp->l_type = blocker->l_type;
	flrp->l_pid = blocker->l_flock.l_pid;
	flrp->l_sysid = blocker->l_flock.l_sysid;

	if (IS_LOCKMGR(request)) {
		flrp->l_start = blocker->l_start;
		if (blocker->l_end == MAX_U_OFF_T)
			flrp->l_len = 0;
		else
			flrp->l_len = blocker->l_end - blocker->l_start + 1;
	} else {
		if (blocker->l_start > MAXEND) {
			flrp->l_start = MAXEND;
			flrp->l_len = 0;
		} else {
			flrp->l_start = blocker->l_start;
			if (blocker->l_end >= MAXEND)
				flrp->l_len = 0;
			else
				flrp->l_len = blocker->l_end -
					blocker->l_start + 1;
		}
	}
}

#ifdef DEBUG
static int
check_active_locks(graph_t *gp)
{
	lock_descriptor_t *lock, *lock1;
	edge_t	*ep;

	for (lock = ACTIVE_HEAD(gp)->l_next; lock != ACTIVE_HEAD(gp);
						lock = lock->l_next) {
		ASSERT(IS_ACTIVE(lock));
		ASSERT(!IS_SLEEPING(lock));
		ASSERT(!IS_GRANTED(lock));
		ASSERT(NOT_BLOCKED(lock));
		ASSERT(!IS_BARRIER(lock));

		ep = FIRST_IN(lock);

		while (ep != HEAD(lock)) {
			ASSERT(IS_SLEEPING(ep->from_vertex));
			ASSERT(!NOT_BLOCKED(ep->from_vertex));
			ASSERT(!IS_GRANTED(ep->from_vertex));
			ASSERT(!IS_ACTIVE(ep->from_vertex));
			ep = NEXT_IN(ep);
		}

		for (lock1 = lock->l_next; lock1 != ACTIVE_HEAD(gp);
					lock1 = lock1->l_next) {
			if (lock1->l_vnode == lock->l_vnode) {
			if (BLOCKS(lock1, lock)) {
		cmn_err(CE_PANIC, "active lock %x blocks %x \n", lock1, lock);
			} else if (BLOCKS(lock, lock1)) {
		cmn_err(CE_PANIC, "active lock %x blocks %x \n", lock, lock1);
			}
			}
		}
	}
	return (0);
}

static int
check_sleeping_locks(graph_t *gp)
{
	lock_descriptor_t *lock1, *lock2;
	edge_t *ep;
	for (lock1 = SLEEPING_HEAD(gp)->l_next; lock1 != SLEEPING_HEAD(gp);
				lock1 = lock1->l_next) {
				ASSERT(!IS_BARRIER(lock1));
	for (lock2 = lock1->l_next; lock2 != SLEEPING_HEAD(gp);
				lock2 = lock2->l_next) {
		if (lock1->l_vnode == lock2->l_vnode) {
			if (BLOCKS(lock2, lock1)) {
				ASSERT(!IS_GRANTED(lock1));
				ASSERT(!NOT_BLOCKED(lock1));
				path(lock1, lock2);
			}
		}
	}

	for (lock2 = ACTIVE_HEAD(gp)->l_next; lock2 != ACTIVE_HEAD(gp);
					lock2 = lock2->l_next) {
				ASSERT(!IS_BARRIER(lock1));
		if (lock1->l_vnode == lock2->l_vnode) {
			if (BLOCKS(lock2, lock1)) {
				ASSERT(!IS_GRANTED(lock1));
				ASSERT(!NOT_BLOCKED(lock1));
				path(lock1, lock2);
			}
		}
	}
	ep = FIRST_ADJ(lock1);
	while (ep != HEAD(lock1)) {
		ASSERT(BLOCKS(ep->to_vertex, lock1));
		ep = NEXT_ADJ(ep);
	}
	}
	return (0);
}

static int
level_two_path(lock_descriptor_t *lock1, lock_descriptor_t *lock2, int no_path)
{
	edge_t	*ep;
	lock_descriptor_t	*vertex;
	lock_descriptor_t *vertex_stack;

	STACK_INIT(vertex_stack);

	flk_graph_uncolor(lock1->l_graph);
	ep = FIRST_ADJ(lock1);
	ASSERT(ep != HEAD(lock1));
	while (ep != HEAD(lock1)) {
		if (no_path)
			ASSERT(ep->to_vertex != lock2);
		STACK_PUSH(vertex_stack, ep->to_vertex, l_dstack);
		COLOR(ep->to_vertex);
		ep = NEXT_ADJ(ep);
	}

	while ((vertex = STACK_TOP(vertex_stack)) != NULL) {
		STACK_POP(vertex_stack, l_dstack);
		for (ep = FIRST_ADJ(vertex); ep != HEAD(vertex);
						ep = NEXT_ADJ(ep)) {
			if (COLORED(ep->to_vertex))
				continue;
			COLOR(ep->to_vertex);
			if (ep->to_vertex == lock2)
				return (1);

			STACK_PUSH(vertex_stack, ep->to_vertex, l_dstack);
		}
	}
	return (0);
}

static void
check_owner_locks(graph_t *gp, pid_t pid, long sysid, vnode_t *vp)
{
	lock_descriptor_t *lock;

	SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp);

	if (lock) {
		while (lock != ACTIVE_HEAD(gp) && (lock->l_vnode == vp)) {
			if (lock->l_flock.l_pid == pid &&
				lock->l_flock.l_sysid == sysid)
cmn_err(CE_PANIC, "owner pid %d 's lock %x in active queue %x \n", pid, lock);
			lock = lock->l_next;
		}
	}
	SET_LOCK_TO_FIRST_SLEEP_VP(gp, lock, vp);

	if (lock) {
		while (lock != SLEEPING_HEAD(gp) && (lock->l_vnode == vp)) {
			if (lock->l_flock.l_pid == pid &&
				lock->l_flock.l_sysid == sysid)
cmn_err(CE_PANIC, "owner pid %d 's lock %x in sleep queue %x \n", pid, lock);
			lock = lock->l_next;
		}
	}
}

static int
level_one_path(lock_descriptor_t *lock1, lock_descriptor_t *lock2)
{
	edge_t *ep = FIRST_ADJ(lock1);

	while (ep != HEAD(lock1)) {
		if (ep->to_vertex == lock2)
			return (1);
		else
			ep = NEXT_ADJ(ep);
	}
	return (0);
}

static int
no_path(lock_descriptor_t *lock1, lock_descriptor_t *lock2)
{
	return (!level_two_path(lock1, lock2, 1));
}

static void
path(lock_descriptor_t *lock1, lock_descriptor_t *lock2)
{
	if (level_one_path(lock1, lock2)) {
		if (level_two_path(lock1, lock2, 0) != 0) {
			cmn_err(CE_WARN,
				"one edge one path from lock1 %x lock2 %x\n",
				lock1, lock2);
		}
	} else if (no_path(lock1, lock2)) {
		cmn_err(CE_PANIC, "No path from  lock1 %x to lock2 %x \n",
			lock1, lock2);
	}
}
#endif /* DEBUG */
