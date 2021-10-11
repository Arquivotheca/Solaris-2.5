/*
 * Copyright (c) 1994,1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */


#ifndef _SYS_FLOCK_IMPL_H
#define	_SYS_FLOCK_IMPL_H

#pragma ident	"@(#)flock_impl.h	1.13	95/02/08 SMI"
/* SVr4.0 11.11 */

#include <sys/types.h>
#include <sys/fcntl.h>		/* flock definition */
#include <sys/file.h>		/* FREAD etc */
#include <sys/flock.h>		/* RCMD etc */
#include <sys/kmem.h>
#include <sys/user.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/vnode.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct	edge {
	struct	edge	*edge_adj_next;	/* adjacency list next */
	struct	edge	*edge_adj_prev; /* adjacency list prev */
	struct	edge	*edge_in_next;	/* incoming edges list next */
	struct	edge	*edge_in_prev;	/* incoming edges list prev */
	struct 	lock_descriptor	*from_vertex;	/* edge emanating from lock */
	struct 	lock_descriptor	*to_vertex;	/* edge pointing to lock */
};


typedef	struct	edge	edge_t;

struct lock_descriptor {
	struct	lock_descriptor	*l_next;	/* next active/sleep lock */
	struct	lock_descriptor	*l_prev;	/* previous active/sleep lock */
	struct	edge		l_edge;		/* edge for adj and in lists */
	struct	lock_descriptor	*l_stack;	/* for stack operations */
	struct	lock_descriptor	*l_stack1;	/* for stack operations */
	struct 	lock_descriptor *l_dstack;	/* stack for debug functions */
	struct	edge		*l_sedge;	/* start edge for graph alg. */
			int	l_index; 	/* used for barrier count */
		struct	graph	*l_graph;	/* graph this belongs to */
		vnode_t		*l_vnode;	/* vnode being locked */
			int	l_type;		/* type of lock */
			int	l_state;	/* state described below */
			u_off_t	l_start;	/* start offset */
			u_off_t	l_end;		/* end offset */
		struct	flock	l_flock;	/* original flock request */
			int	l_color;	/* color used for graph alg */
		kcondvar_t	l_cv;		/* wait condition for lock */
		int		pvertex;	/* index to proc vertex */
};


typedef struct 	lock_descriptor	lock_descriptor_t;

/*
 * Each graph holds locking information for some number of vnodes.  The
 * active and sleeping lists are circular, with a dummy head element.
 *
 * The lockmgr_status field is a copy of flk_lockmgr_status.  The per-graph
 * copies are used to synchronize lock requests with shutdown requests.
 * The global copy is used to initialize the per-graph field when a new
 * graph is created.
 */

struct	graph {
	kmutex_t	gp_mutex;	/* mutex for this graph */
	struct	lock_descriptor	active_locks;
	struct	lock_descriptor	sleeping_locks;
	int index;	/* index of this graph into the hash table */
	int mark;	/* used for coloring the graph */
	flk_lockmgr_status_t lockmgr_status;
};

typedef	struct	graph	graph_t;

/* flags defining state  of locks */

#define	ACTIVE_LOCK		0x0001	/* in active queue */
#define	SLEEPING_LOCK		0x0002	/* in sleep queue */
#define	IO_LOCK			0x0004	/* is an IO lock */
#define	REFERENCED_LOCK		0x0008	/* referenced some where */
#define	QUERY_LOCK		0x0010	/* querying about lock */
#define	WILLING_TO_SLEEP_LOCK	0x0020	/* lock can be put in sleep queue */
#define	RECOMPUTE_LOCK		0x0040	/* used for recomputing dependencies */
#define	RECOMPUTE_DONE		0x0080	/* used for recomputing dependencies */
#define	BARRIER_LOCK		0x0100	/* used for recomputing dependencies */
#define	GRANTED_LOCK		0x0200	/* granted but still in sleep queue */
#define	CANCELLED_LOCK		0x0400	/* cancelled will be thrown out */
#define	DELETED_LOCK		0x0800	/* deleted - free at earliest */
#define	INTERRUPTED_LOCK	0x1000	/* pretend signal */
#define	LOCKMGR_LOCK		0x2000	/* remote lock (server-side) */

#define	HASH_SIZE	32
#define	HASH_SHIFT	(HASH_SIZE - 1)
#define	HASH_INDEX(vp)	(((int)vp >> 7) & HASH_SHIFT)

/* extern definitions */

extern struct graph	*lock_graph[HASH_SIZE];
extern kmutex_t flock_lock;
extern struct kmem_cache *flk_edge_cache;
extern flk_lockmgr_status_t flk_lockmgr_status;

#define	SAME_OWNER(lock1, lock2)	\
	(((lock1)->l_flock.l_pid == (lock2)->l_flock.l_pid) && \
		((lock1)->l_flock.l_sysid == (lock2)->l_flock.l_sysid))

/* flags used for readability in flock.c */

#define	NO_COLOR	0	/* vertex is not colored */
#define	NO_CHECK_CYCLE	0	/* don't mark vertex's in flk_add_edge */
#define	CHECK_CYCLE	1	/* mark vertex's in flk_add_edge */


#define	COLORED(vertex)		((vertex)->l_color == (vertex)->l_graph->mark)
#define	COLOR(vertex)		((vertex)->l_color = (vertex)->l_graph->mark)

/*
 * stack data structure and operations
 */

#define	STACK_INIT(stack)	((stack) = NULL)
#define	STACK_PUSH(stack, ptr, stack_link)	(ptr)->stack_link = (stack),\
				(stack) = (ptr)
#define	STACK_POP(stack, stack_link)	(stack) = (stack)->stack_link
#define	STACK_TOP(stack)	(stack)
#define	STACK_EMPTY(stack)	((stack) == NULL)


#define	ACTIVE_HEAD(gp)	(&(gp)->active_locks)

#define	SLEEPING_HEAD(gp)	(&(gp)->sleeping_locks)

#define	SET_LOCK_TO_FIRST_ACTIVE_VP(gp, lock, vp) \
{ \
	(lock) = (lock_descriptor_t *) vp->v_filocks;	\
}

#define	SET_LOCK_TO_FIRST_SLEEP_VP(gp, lock, vp) \
{ \
for ((lock) = SLEEPING_HEAD((gp))->l_next; ((lock) != SLEEPING_HEAD((gp)) && \
			(lock)->l_vnode != (vp)); (lock) = (lock)->l_next) \
			; \
(lock) = ((lock) == SLEEPING_HEAD((gp))) ? NULL : (lock); \
}

#define	OVERLAP(lock1, lock2) \
	(((lock1)->l_start <= (lock2)->l_start && \
		(lock2)->l_start <= (lock1)->l_end) || \
	((lock2)->l_start <= (lock1)->l_start && \
		(lock1)->l_start <= (lock2)->l_end))

#define	IS_QUERY_LOCK(lock)	((lock)->l_state & QUERY_LOCK)
#define	IS_RECOMPUTE(lock)	((lock)->l_state & RECOMPUTE_LOCK)
#define	IS_SLEEPING(lock)	((lock)->l_state & SLEEPING_LOCK)
#define	IS_BARRIER(lock)	((lock)->l_state & BARRIER_LOCK)
#define	IS_GRANTED(lock)	((lock)->l_state & GRANTED_LOCK)
#define	IS_CANCELLED(lock)	((lock)->l_state & CANCELLED_LOCK)
#define	IS_DELETED(lock)	((lock)->l_state & DELETED_LOCK)
#define	IS_REFERENCED(lock)	((lock)->l_state & REFERENCED_LOCK)
#define	IS_IO_LOCK(lock)	((lock)->l_state & IO_LOCK)
#define	IS_WILLING_TO_SLEEP(lock)	\
		((lock)->l_state & WILLING_TO_SLEEP_LOCK)
#define	IS_ACTIVE(lock)	((lock)->l_state & ACTIVE_LOCK)
#define	IS_INTERRUPTED(lock)	((lock)->l_state & INTERRUPTED_LOCK)
#define	IS_LOCKMGR(lock)	((lock)->l_state & LOCKMGR_LOCK)

/*
 * "local" requests don't involve the NFS lock manager in any way.
 * "remote" requests can be on the server (requests from a remote client),
 * in which case they should be associated with a local vnode (UFS, tmpfs,
 * etc.).  These requests are flagged with LOCKMGR_LOCK and are made using
 * kernel service threads.  Remote requests can also be on an NFS client,
 * because the NFS lock manager uses local locking for some of its
 * bookkeeping.  These requests are made by regular user processes.
 */
#define	IS_LOCAL(lock)	((lock)->l_flock.l_sysid == 0)
#define	IS_REMOTE(lock)	(! IS_LOCAL(lock))

#define	BLOCKS(lock1, lock2)	(!SAME_OWNER((lock1), (lock2)) && \
					(((lock1)->l_type == F_WRLCK) || \
					((lock2)->l_type == F_WRLCK)) && \
					OVERLAP((lock1), (lock2)))

#define	COVERS(lock1, lock2)	\
		(((lock1)->l_start <= (lock2)->l_start) && \
			((lock1)->l_end >= (lock2)->l_end))

#define	IN_LIST_REMOVE(ep)	\
	{ \
	(ep)->edge_in_next->edge_in_prev = (ep)->edge_in_prev; \
	(ep)->edge_in_prev->edge_in_next = (ep)->edge_in_next; \
	}

#define	ADJ_LIST_REMOVE(ep)	\
	{ \
	(ep)->edge_adj_next->edge_adj_prev = (ep)->edge_adj_prev; \
	(ep)->edge_adj_prev->edge_adj_next = (ep)->edge_adj_next; \
	}

#define	NOT_BLOCKED(lock)	\
	((lock)->l_edge.edge_adj_next == &(lock)->l_edge && !IS_GRANTED(lock))

#define	GRANT_WAKEUP(lock)	\
	{	\
		(lock)->l_state |= GRANTED_LOCK; \
		cv_signal(&(lock)->l_cv); \
	}

#define	CANCEL_WAKEUP(lock)	\
	{ \
		(lock)->l_state |= CANCELLED_LOCK; \
		cv_signal(&(lock)->l_cv); \
	}

#define	INTERRUPT_WAKEUP(lock)	\
	{ \
		(lock)->l_state |= INTERRUPTED_LOCK; \
		cv_signal(&(lock)->l_cv); \
	}

#define	REMOVE_SLEEP_QUEUE(lock)	\
	{ \
	ASSERT(IS_SLEEPING(lock)); \
	(lock)->l_state &= ~SLEEPING_LOCK; \
	(lock)->l_next->l_prev = (lock)->l_prev; \
	(lock)->l_prev->l_next = (lock)->l_next; \
	(lock)->l_next = (lock)->l_prev = (lock_descriptor_t *) NULL; \
	}

#define	NO_DEPENDENTS(lock)	\
	((lock)->l_edge.edge_in_next == &(lock)->l_edge)

#define	GRANT(lock)	((lock)->l_state |= GRANTED_LOCK)
#define	FIRST_IN(lock)	((lock)->l_edge.edge_in_next)
#define	FIRST_ADJ(lock)	((lock)->l_edge.edge_adj_next)
#define	HEAD(lock)	(&(lock)->l_edge)
#define	NEXT_ADJ(ep)	((ep)->edge_adj_next)
#define	NEXT_IN(ep)	((ep)->edge_in_next)
#define	IN_ADJ_INIT(lock)	\
{	\
(lock)->l_edge.edge_adj_next = (lock)->l_edge.edge_adj_prev = &(lock)->l_edge; \
(lock)->l_edge.edge_in_next = (lock)->l_edge.edge_in_prev = &(lock)->l_edge; \
}

#define	COPY(lock1, lock2)	\
{	\
(lock1)->l_graph = (lock2)->l_graph; \
(lock1)->l_vnode = (lock2)->l_vnode; \
(lock1)->l_type = (lock2)->l_type; \
(lock1)->l_start = (lock2)->l_start; \
(lock1)->l_end = (lock2)->l_end; \
(lock1)->l_flock = (lock2)->l_flock; \
(lock1)->pvertex = (lock2)->pvertex; \
}

/* Indicates the effect of executing a request on the existing locks */

#define	UNLOCK		0x1	/* request unlocks the existing lock */
#define	DOWNGRADE	0x2	/* request downgrades the existing lock */
#define	UPGRADE		0x3	/* request upgrades the existing lock */
#define	STAY_SAME	0x4	/* request type is same as existing lock */


/*	proc graph definitions	*/

/*
 * Proc graph is the global process graph that maintains information
 * about the dependencies between processes. An edge is added between two
 * processes represented by proc_vertex's A and B, iff there exists l1
 * owned by process A in any of the lock_graph's dependent on l2
 * (thus having an edge to l2) owned by process B.
 */
struct proc_vertex {
	pid_t	pid;	/* pid of the process */
	long	sysid;	/* sysid of the process */
	struct proc_edge	*edge;	/* adajcent edges of this process */
	int incount;		/* Number of inedges to this process */
	struct proc_edge *p_sedge;	/* used for implementing stack alg. */
	struct proc_vertex	*p_stack;	/* used for stack alg. */
	int atime;	/* used for cycle detection algorithm */
	int dtime;	/* used for cycle detection algorithm */
	int index;	/* index into the  array of proc_graph vertices */
};

typedef	struct proc_vertex proc_vertex_t;

struct proc_edge {
	struct proc_edge	*next;	/* next edge in adjacency list */
	int  refcount;			/* reference count of this edge */
	struct proc_vertex	*to_proc;	/* process this points to */
};

typedef struct proc_edge proc_edge_t;


#define	PROC_CHUNK	100

struct proc_graph {
	struct proc_vertex **proc;	/* list of proc_vertexes */
	int gcount;		/* list size */
	int free;		/* number of free slots in the list */
	int mark;		/* used for graph coloring */
};

typedef struct proc_graph proc_graph_t;

extern	struct proc_graph	pgraph;

#define	PROC_SAME_OWNER(lock, pvertex)	\
	(((lock)->l_flock.l_pid == (pvertex)->pid) && \
		((lock)->l_flock.l_sysid == (pvertex)->sysid))

#define	PROC_ARRIVE(pvertex)	((pvertex)->atime = pgraph.mark)
#define	PROC_DEPART(pvertex)	((pvertex)->dtime = pgraph.mark)
#define	PROC_ARRIVED(pvertex)	((pvertex)->atime == pgraph.mark)
#define	PROC_DEPARTED(pvertex)  ((pvertex)->dtime == pgraph.mark)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FLOCK_IMPL_H */
