/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)pid.c	1.27	95/02/16 SMI"	/* from SVr4.0 1.10 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <sys/tuneable.h>
#include <sys/var.h>
#include <sys/cred.h>
#include <sys/systm.h>
#include <sys/prsystm.h>
#include <sys/vnode.h>
#include <sys/session.h>
#include <sys/cpuvar.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <c2/audit.h>

/* directory entries for /proc */
union procent {
	proc_t *pe_proc;
	union procent *pe_next;
};

struct pid pid0 = {
	0,		/* pid_prinactive */
	1,		/* pid_pgorphaned */
	0,		/* pid_padding	*/
	0,		/* pid_prslot	*/
	0,		/* pid_id	*/
	NULL,		/* pid_pglink	*/
	NULL,		/* pid_link	*/
	3		/* pid_ref	*/
};

static int pid_hashlen = 4;	/* desired average hash chain length */
static int pid_hashsz;		/* number of buckets in the hash table */

#define	HASHPID(pid)	(pidhash[((pid)&(pid_hashsz-1))])

extern u_int nproc;
static void	upcount_init(void);

kmutex_t	pidlock;	/* global process lock */
kmutex_t	pr_pidlock;	/* /proc global process lock */
kcondvar_t	*pr_pid_cv;	/* for /proc, one per process slot */

static kmutex_t	pidlinklock;
static struct pid **pidhash;
static pid_t minpid;
static pid_t mpid;
static union procent *procdir;
static union procent *procentfree;

static struct pid *
pid_lookup(pid_t pid)
{
	struct pid *pidp;

	ASSERT(MUTEX_HELD(&pidlinklock));

	for (pidp = HASHPID(pid); pidp; pidp = pidp->pid_link) {
		if (pidp->pid_id == pid) {
			ASSERT(pidp->pid_ref > 0);
			break;
		}
	}
	return (pidp);
}

void
pid_setmin(void)
{
	minpid = mpid + 1;
}

/*
 * This function assigns a pid for use in a fork request.  It allocates
 * a pid structure, tries to find an empty slot in the proc table,
 * and selects the process id.
 *
 * pid_assign() returns the new pid on success, -1 on failure.
 */
pid_t
pid_assign(proc_t *prp)
{
	struct pid *pidp;
	union procent *pep;
	pid_t newpid;

	pidp = kmem_zalloc(sizeof (struct pid), KM_SLEEP);

	mutex_enter(&pidlinklock);
	if ((pep = procentfree) == NULL) {
		/*
		 * ran out of /proc directory entries
		 */
		mutex_exit(&pidlinklock);
		kmem_free(pidp, sizeof (struct pid));
		return (-1);
	}
	procentfree = pep->pe_next;
	pep->pe_proc = prp;
	prp->p_pidp = pidp;

	/*
	 * Allocate a pid
	 */
	do  {
		newpid = (++mpid == MAXPID ? mpid = minpid : mpid);
	} while (pid_lookup(newpid));

	/*
	 * Put pid into the pid hash table.
	 */
	pidp->pid_link = HASHPID(newpid);
	HASHPID(newpid) = pidp;
	pidp->pid_ref = 1;
	pidp->pid_id = newpid;
	pidp->pid_prslot = pep - procdir;
	mutex_exit(&pidlinklock);

	return (newpid);
}

/*
 * decrement the reference count for pid
 */
int
pid_rele(struct pid *pidp)
{
	struct pid **pidpp;

	mutex_enter(&pidlinklock);
	ASSERT(pidp != &pid0);

	pidpp = &HASHPID(pidp->pid_id);
	for (;;) {
		ASSERT(*pidpp != NULL);
		if (*pidpp == pidp)
			break;
		pidpp = &(*pidpp)->pid_link;
	}

	*pidpp = pidp->pid_link;
	mutex_exit(&pidlinklock);

	kmem_free(pidp, sizeof (*pidp));
	return (0);
}

void
proc_entry_free(struct pid *pidp)
{
	mutex_enter(&pidlinklock);
	pidp->pid_prinactive = 1;
	procdir[pidp->pid_prslot].pe_next = procentfree;
	procentfree = &procdir[pidp->pid_prslot];
	mutex_exit(&pidlinklock);
}

void
pid_exit(proc_t *prp)
{
	struct pid *pidp;

	ASSERT(MUTEX_HELD(&pidlock));

	/*
	 * Exit process group.  If it is NULL, it's because fork failed
	 * before calling pgjoin().
	 */
	ASSERT(prp->p_pgidp != NULL || prp->p_stat == SIDL);
	if (prp->p_pgidp != NULL)
		pgexit(prp);

	(void) SESS_RELE(prp->p_sessp);

	pidp = prp->p_pidp;

	proc_entry_free(pidp);

#ifdef C2_AUDIT
	if (audit_active)
		audit_pfree(prp);
#endif

	if (practive == prp) {
		practive = prp->p_next;
	}

	if (prp->p_next) {
		prp->p_next->p_prev = prp->p_prev;
	}
	if (prp->p_prev) {
		prp->p_prev->p_next = prp->p_next;
	}

	PID_RELE(pidp);

	mutex_destroy(&prp->p_lock);
	mutex_destroy(&prp->p_crlock);
	kmem_free(prp, sizeof (*prp));
	nproc--;
}

/*
 * find a process given its process ID
 */
proc_t *
prfind(pid_t pid)
{
	struct pid *pidp;

	ASSERT(MUTEX_HELD(&pidlock));

	mutex_enter(&pidlinklock);
	pidp = pid_lookup(pid);
	mutex_exit(&pidlinklock);
	if (pidp != NULL && pidp->pid_prinactive == 0)
		return (procdir[pidp->pid_prslot].pe_proc);
	return (NULL);
}

/*
 * return the list of processes in whose process group ID is 'pgid',
 * or NULL, if no such process group
 */

proc_t *
pgfind(pid_t pgid)
{
	struct pid *pidp;

	ASSERT(MUTEX_HELD(&pidlock));

	mutex_enter(&pidlinklock);
	pidp = pid_lookup(pgid);
	mutex_exit(&pidlinklock);
	if (pidp != NULL)
		return (pidp->pid_pglink);
	return (NULL);
}

void
pid_init(void)
{
	int i;

	pid_hashsz = 1 << highbit(v.v_proc / pid_hashlen);

	mutex_init(&pidlinklock, "pidlink", MUTEX_DEFAULT, NULL);
	mutex_init(&pr_pidlock, "pr_pidlock", MUTEX_DEFAULT, NULL);

	pidhash = (struct pid **)
	    kmem_zalloc(sizeof (struct pid *)*pid_hashsz, KM_NOSLEEP);

	procdir = (union procent *)
	    kmem_alloc(sizeof (union procent)*v.v_proc, KM_NOSLEEP);

	pr_pid_cv = (kcondvar_t *)
	    kmem_alloc(sizeof (kcondvar_t)*v.v_proc, KM_NOSLEEP);

	if (pidhash == NULL || procdir == NULL || pr_pid_cv == NULL)
		cmn_err(CE_PANIC, "Could not allocate space for pid tables\n");

	nproc = 1;
	practive = proc_sched;
	proc_sched->p_next = NULL;
	procdir[0].pe_proc = proc_sched;

	procentfree = &procdir[1];
	for (i = 1; i < v.v_proc - 1; i++)
		procdir[i].pe_next = &procdir[i+1];
	procdir[i].pe_next = NULL;

	for (i = 0; i < v.v_proc; i++)
		cv_init(&pr_pid_cv[i], "pr_pidlock cv", CV_DEFAULT, NULL);

	HASHPID(0) = &pid0;

	upcount_init();

}

proc_t *
pid_entry(int slot)
{
	union procent *pep;
	proc_t *prp;

	ASSERT(MUTEX_HELD(&pidlock));
	ASSERT(slot >= 0 && slot < v.v_proc);

	pep = procdir[slot].pe_next;
	if (pep >= procdir && pep < &procdir[v.v_proc])
		return (NULL);
	prp = procdir[slot].pe_proc;
	if (prp != 0 && prp->p_stat == SIDL)
		return (NULL);
	return (prp);
}

/*
 * Send the specified signal to all processes whose process group ID is
 * equal to 'pgid'
 */

void
signal(pid_t pgid, int sig)
{
	struct pid *pidp;
	proc_t *prp;

	mutex_enter(&pidlock);
	mutex_enter(&pidlinklock);
	if (pgid == 0 || (pidp = pid_lookup(pgid)) == NULL) {
		mutex_exit(&pidlinklock);
		mutex_exit(&pidlock);
		return;
	}
	mutex_exit(&pidlinklock);
	for (prp = pidp->pid_pglink; prp; prp = prp->p_pglink) {
		mutex_enter(&prp->p_lock);
		sigtoproc(prp, NULL, sig, 0);
		mutex_exit(&prp->p_lock);
	}
	mutex_exit(&pidlock);
}

/*
 * Send the specified signal to the specified process
 */

void
prsignal(struct pid *pidp, int sig)
{
	if (!(pidp->pid_prinactive))
		psignal(procdir[pidp->pid_prslot].pe_proc, sig);
}

#include <sys/sunddi.h>

/*
 * DDI/DKI interfaces for drivers to send signals to processes
 */

/*
 * obtain an opaque reference to a process for signaling
 */
void *
proc_ref(void)
{
	struct pid *pidp;

	mutex_enter(&pidlock);
	pidp = curproc->p_pidp;
	PID_HOLD(pidp);
	mutex_exit(&pidlock);

	return (pidp);
}

/*
 * release a reference to a process
 * - a process can exit even if a driver has a reference to it
 * - one proc_unref for every proc_ref
 */
void
proc_unref(void *pref)
{
	mutex_enter(&pidlock);
	PID_RELE((struct pid *)pref);
	mutex_exit(&pidlock);
}

/*
 * send a signal to a process
 *
 * - send the process the signal
 * - if the process went away, return a -1
 * - if the process is still there return 0
 */
int
proc_signal(void *pref, int sig)
{
	struct pid *pidp = pref;

	prsignal(pidp, sig);
	return (pidp->pid_prinactive ? -1 : 0);
}


static struct upcount	**upc_hash;	/* a boot time allocated array */
static u_long		upc_hashmask;
#define	UPC_HASH(x)	((u_long)(x) & upc_hashmask)

/*
 * Get us off the ground.  Called once at boot.
 */
void
upcount_init(void)
{
	u_long	upc_hashsize;

	/*
	 * An entry per MB of memory is our current guess
	 */
	upc_hashsize = (1 << highbit(physmem >> 8));
	upc_hashmask = upc_hashsize - 1;
	upc_hash = (struct upcount **)kmem_zalloc(
	    upc_hashsize * sizeof (struct upcount *), KM_NOSLEEP);
	if (upc_hash == NULL) {
		cmn_err(CE_PANIC, "could not alocate space fpr upc hash\n");
	}
}

/*
 * Increment the number of processes associated with a given uid.
 */
void
upcount_inc(uid_t uid)
{
	struct upcount	**upc, **hupc;
	struct upcount	*new;

	ASSERT(MUTEX_HELD(&pidlock));
	new = NULL;
	hupc = &upc_hash[UPC_HASH(uid)];
top:
	upc = hupc;
	while ((*upc) != NULL) {
		if ((*upc)->up_uid == uid) {
			(*upc)->up_count++;
			if (new) {
				/*
				 * did not need `new' afterall.
				 */
				kmem_free(new, sizeof (*new));
			}
			return;
		}
		upc = &(*upc)->up_next;
	}

	/*
	 * There is no entry for this uid.
	 * Allocate one.  If we have to drop pidlock, check
	 * again.
	 */
	if (new == NULL) {
		new = (struct upcount *)kmem_alloc(sizeof (*new), KM_NOSLEEP);
		if (new == NULL) {
			mutex_exit(&pidlock);
			new = (struct upcount *)kmem_alloc(sizeof (*new),
			    KM_SLEEP);
			mutex_enter(&pidlock);
			goto top;
		}
	}


	/*
	 * On the assumption that a new user is going to do some
	 * more forks, put the new upcount structure on the front.
	 */
	upc = hupc;

	new->up_uid = uid;
	new->up_count = 1;
	new->up_next = *upc;

	*upc = new;
}

/*
 * Decrement the number of processes a given uid has.
 */
void
upcount_dec(uid_t uid)
{
	struct	upcount **upc;
	struct	upcount *done;

	ASSERT(MUTEX_HELD(&pidlock));

	upc = &upc_hash[UPC_HASH(uid)];
	while ((*upc) != NULL) {
		if ((*upc)->up_uid == uid) {
			(*upc)->up_count--;
			if ((*upc)->up_count == 0) {
				done = *upc;
				*upc = (*upc)->up_next;
				kmem_free(done, sizeof (*done));
			}
			return;
		}
		upc = &(*upc)->up_next;
	}
	cmn_err(CE_PANIC, "decr_upcount-off the end");
}

/*
 * Returns the number of processes a uid has.
 * Non-existent uid's are assumed to have no processes.
 */
int
upcount_get(uid_t uid)
{
	struct	upcount *upc;

	ASSERT(MUTEX_HELD(&pidlock));

	upc = upc_hash[UPC_HASH(uid)];
	while (upc != NULL) {
		if (upc->up_uid == uid) {
			return (upc->up_count);
		}
		upc = upc->up_next;
	}
	return (0);
}
