
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)shm.c	1.57	95/10/25 SMI"

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

/*
 * Inter-Process Communication Shared Memory Facility.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/kmem.h>
#include <sys/user.h>
#include <sys/ipc.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/shm.h>
#include <sys/tuneable.h>
#include <sys/vm.h>
#include <sys/mman.h>
#include <sys/swap.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/vtrace.h>

#include <vm/hat.h>
#include <vm/seg.h>
#include <vm/as.h>
#include <vm/seg_vn.h>
#include <vm/anon.h>
#include <vm/page.h>
#include <vm/vpage.h>


#ifndef sun
extern struct shmid_ds	shmem[];	/* shared memory headers */
#endif /* !sun */

static int kshmdt(proc_t *pp, caddr_t addr);
static int shmem_lock(struct anon_map *amp);
static void shmem_unlock(struct anon_map *amp, uint lck);
static void sa_add(struct proc *pp, caddr_t addr, size_t len,
	struct anon_map *amp, struct as *as);
static void shm_rm_amp(struct anon_map *amp, uint lckflag);

/* changes for ISM */
#include <vm/spt.h>

/* hook in /etc/system - only for internal testing purpose */
int share_page_table;
int ism_off;

/*
 * Protects all shm data except amp's
 */
static kmutex_t shm_lock;
static kmutex_t *shmem_locks;

/*
 * The following variables shminfo_* are there so that the
 * elements of the data structure shminfo can be tuned
 * (if necessary) using the /etc/system file syntax for
 * tuning of integer data types.
 */
int shminfo_shmmax = 1048576;	/* max shared memory segment size */
int shminfo_shmmin = 1;		/* min shared memory segment size */
int shminfo_shmmni = 100;	/* # of shared memory identifiers */
int shminfo_shmseg = 6;		/* segments per process		  */

/*
 * Argument vectors for the various flavors of shmsys().
 */

#define	SHMAT	0
#define	SHMCTL	1
#define	SHMDT	2
#define	SHMGET	3

struct shmsysa {
	int		opcode;
};

struct shmata {
	int		opcode;
	int		shmid;
	caddr_t		addr;
	int		flag;
};

struct shmctla {
	int		opcode;
	int		shmid;
	int		cmd;
	struct shmid_ds	*arg;
};

struct shmdta {
	int		opcode;
	caddr_t		addr;
};

struct shmgeta {
	int		opcode;
	key_t		key;
	int		size;
	int		shmflg;
};

#include <sys/modctl.h>
#include <sys/syscall.h>

static int shmsys(struct shmsysa *, rval_t *);

static struct sysent ipcshm_sysent = {
	4,
	SE_NOUNLOAD,
	shmsys
};

/*
 * Module linkage information for the kernel.
 */
static struct modlsys modlsys = {
	&mod_syscallops, "System V shared memory", &ipcshm_sysent
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlsys, NULL
};

char _depends_on[] = "misc/ipc";	/* ipcaccess, ipcget */


int
_init(void)
{
	int retval;
	int i;
	u_longlong_t mavail;

	/*
	 * shminfo_* are inited above to default values
	 * These values can be tuned if need be using the
	 * integer tuning capabilities in the /etc/system file.
	 */
	shminfo.shmmax = shminfo_shmmax;
	shminfo.shmmin = shminfo_shmmin;
	shminfo.shmmni = shminfo_shmmni;
	shminfo.shmseg = shminfo_shmseg;


	/*
	 * 1213443: u.u_nshmseg is now defined as a short.
	 */
	if (shminfo.shmseg >= SHRT_MAX) {
		cmn_err(CE_NOTE, "shminfo.shmseg limited to %d", SHRT_MAX);
		shminfo.shmseg = SHRT_MAX;
	}

	/*
	 * Don't use more than 25% of the available kernel memory
	 */
	mavail = kmem_maxavail() / 4;
	if ((u_longlong_t)shminfo.shmmni * sizeof (struct shmid_ds) +
	    (u_longlong_t)shminfo.shmmni * sizeof (kmutex_t) > mavail) {
		cmn_err(CE_WARN,
		    "shmsys: can't load module, too much memory requested");
		return (ENOMEM);
	}

	mutex_init(&shm_lock, "Sys V shared mem", MUTEX_DEFAULT, NULL);

	ASSERT(shmem == NULL);
	ASSERT(shmem_locks == NULL);
	shmem = kmem_zalloc(shminfo.shmmni * sizeof (struct shmid_ds),
		KM_SLEEP);
	shmem_locks = kmem_zalloc(shminfo.shmmni * sizeof (kmutex_t), KM_SLEEP);

	for (i = 0; i < shminfo.shmmni; i++)
		mutex_init(&shmem_locks[i], "shmem locks", MUTEX_DEFAULT, NULL);

	if ((retval = mod_install(&modlinkage)) == 0)
		return (0);

	for (i = 0; i < shminfo.shmmni; i++)
		mutex_destroy(&shmem_locks[i]);

	kmem_free(shmem, shminfo.shmmni * sizeof (struct shmid_ds));
	kmem_free(shmem_locks, shminfo.shmmni * sizeof (kmutex_t));

	shmem = NULL;
	shmem_locks = NULL;

	mutex_destroy(&shm_lock);

	return (retval);
}

int
_fini(void)
{
	/*
	 * shm segment lives after the process that created it is gone.
	 * There are many ways that shm segment can be implicitly used
	 * even when the shm id is removed!  Shared memory segment may hold
	 * important system resources like swap space and anon-maps...
	 * Furthermore, processes can fork with as_map(),
	 * as_unmap(), and async_io can be done to the shared memory segment.
	 * We need a global counting scheme (outside of shmsys code) to keep
	 * track of all these implicit references.  When ALL these references
	 * are gone, we can then safely allow unloading of the shmsys module.
	 *
	 * Before that scheme is available and tested for all known problems,
	 * we simply return EBUSY.
	 */
	return (EBUSY);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Convert user supplied shmid into a ptr to the associated
 * shared memory header.
 */
static kmutex_t *
shmconv_lock(int s, struct shmid_ds **spp)
{
	struct shmid_ds *sp;	/* ptr to associated header */
	int	index;
	kmutex_t	*lock;

	if (s < 0)
		return (NULL);

	index = s % shminfo.shmmni;
	lock = &shmem_locks[index];
	sp = &shmem[index];
	mutex_enter(&shm_lock);
	mutex_enter(lock);
	if (!(sp->shm_perm.mode & IPC_ALLOC) ||
	    s / shminfo.shmmni != sp->shm_perm.seq) {
		mutex_exit(lock);
		mutex_exit(&shm_lock);
		return (NULL);
	}
	mutex_exit(&shm_lock);
	*spp = sp;
	return (lock);
}

/*
 * Shmat (attach shared segment) system call.
 */
static int
shmat(struct shmata *uap, rval_t *rvp)
{
	struct shmid_ds *sp;	/* shared memory header ptr */
	caddr_t	addr;
	uint	size;
	int	error = 0;
	proc_t *pp = curproc;
	struct segvn_crargs	crargs;	/* segvn create arguments */
	kmutex_t	*lock;

	if ((lock = shmconv_lock(uap->shmid, &sp)) == NULL) {
		return (EINVAL);
	}
	if (error = ipcaccess(&sp->shm_perm, SHM_R, CRED()))
		goto errret;
	if (spt_on(uap->flag) &&
	    (error = ipcaccess(&sp->shm_perm, SHM_W, CRED())))
		goto errret;
	if ((uap->flag & SHM_RDONLY) == 0 &&
	    (error = ipcaccess(&sp->shm_perm, SHM_W, CRED())))
		goto errret;

	mutex_enter(&pp->p_lock);
	if (PTOU(pp)->u_nshmseg >= shminfo.shmseg) {
		error = EMFILE;
		mutex_exit(&pp->p_lock);
		goto errret;
	}
	mutex_exit(&pp->p_lock);

	if (ism_off)
		uap->flag = uap->flag & ~SHM_SHARE_MMU;

	mutex_enter(&sp->shm_amp->lock);
	size = sp->shm_amp->size;
	mutex_exit(&sp->shm_amp->lock);
	addr = uap->addr;

	as_rangelock(pp->p_as);

	if (spt_on(uap->flag)) {
		/*
		 * Handle ISM
		 */
		uint    share_size, ssize, n;
		struct	sptshm_data ssd;

		n = num_hw_pagesizes();
		if (n < 2) { /* take care of sun4/4c/4e */
			as_rangeunlock(pp->p_as);
			error = EINVAL;
			goto errret;
		}
		share_size = get_hw_pagesize(n - 1);
		if (share_size == 0) {
			as_rangeunlock(pp->p_as);
			error = EINVAL;
			goto errret;
		}
		size = roundup(size, share_size);
		if (addr == 0) {
			/*
			 * Add in another share_size so we know there
			 * is a share_size aligned address in the segment
			 * returned by map_addr()
			 */
			ssize = size + share_size;
			map_addr(&addr, ssize, (off_t)0, 1);
			if (addr == NULL) {
				as_rangeunlock(pp->p_as);
				error = ENOMEM;
				goto errret;
			}
			addr = (caddr_t)roundup((u_int)addr, share_size);
		} else {
			/* Use the user-supplied attach address */
			caddr_t base;
			uint len;

			/*
			 * Check that the address range
			 *  1) is properly aligned
			 *  2) is correct in unix terms
			 *  3) is within an unmapped address segment
			 */
			base = addr;
			len = size;		/* use spt aligned size */
			/* XXX - in SunOS, is sp->shm_segsz */
			if (((uint)base & (share_size - 1)) ||
			    (valid_usr_range(base, len) == 0) ||
			    as_gap(pp->p_as, len, &base, &len,
				AH_LO, (caddr_t)NULL) != 0) {
				error = EINVAL;
				as_rangeunlock(pp->p_as);
				goto errret;
			}
		}
		if (!isspt(sp)) {
			struct as *sptas;

			error = sptcreate(size, &sptas, sp->shm_amp);
			if (error) {
				as_rangeunlock(pp->p_as);
				goto errret;
			}
			sp->shm_sptas = sptas;
		}
		ssd.sptas = sp->shm_sptas;
		ssd.amp = sp->shm_amp;
		error = as_map(pp->p_as, addr, size,
				segspt_shmattach, (caddr_t)&ssd);
	} else {
		/*
		 * Normal case.
		 */
		if (addr == 0) {
			/* Let the system pick the attach address */
			map_addr(&addr, size, (off_t)0, 1);
			if (addr == NULL) {
				as_rangeunlock(pp->p_as);
				error = ENOMEM;
				goto errret;
			}
		} else {
			/* Use the user-supplied attach address */
			caddr_t base;
			uint len;

			if (uap->flag & SHM_RND)
				addr = (caddr_t)((ulong)addr & ~(SHMLBA - 1));
			/*
			 * Check that the address range
			 *  1) is properly aligned
			 *  2) is correct in unix terms
			 *  3) is within an unmapped address segment
			 */
			base = addr;
			len = size;		/* use aligned size */
			/* XXX - in SunOS, is sp->shm_segsz */
			if (((uint)base & PAGEOFFSET) ||
			    (valid_usr_range(base, len) == 0) ||
			    as_gap(pp->p_as, len, &base, &len,
				AH_LO, (caddr_t)NULL) != 0) {
				error = EINVAL;
				as_rangeunlock(pp->p_as);
				goto errret;
			}
		}

		/* Initialize the create arguments and map the segment */
		crargs = *(struct segvn_crargs *)zfod_argsp;
		crargs.offset = 0;
		crargs.type = MAP_SHARED;
		crargs.amp = sp->shm_amp;
		crargs.prot = (uap->flag & SHM_RDONLY) ?
				(PROT_ALL & ~PROT_WRITE) : PROT_ALL;
		crargs.maxprot = crargs.prot;
		crargs.flags = 0;

		error = as_map(pp->p_as, addr, size, /* XXX - sp->shm_segsz */
				segvn_create, (caddr_t)&crargs);
	}

	as_rangeunlock(pp->p_as);
	if (error)
		goto errret;

	/* record shmem range for the detach */

	sa_add(pp, addr, (size_t)size, sp->shm_amp, sp->shm_sptas);

	mutex_enter(&sp->shm_amp->lock);
	sp->shm_amp->refcnt++;		/* keep amp until shmdt and IPC_RMID */
	rvp->r_val1 = (int)addr;
	sp->shm_atime = hrestime.tv_sec;
	sp->shm_lpid = pp->p_pid;
	mutex_exit(&sp->shm_amp->lock);
errret:

	mutex_exit(lock);
	return (error);
}


/*
 * Shmctl system call.
 */
/* ARGSUSED */
static int
shmctl(struct shmctla *uap, rval_t *rvp)
{
	struct shmid_ds		*sp;	/* shared memory header ptr */
	struct o_shmid_ds	ods;	/* hold area for SVR3 IPC_O_SET */
	struct shmid_ds		ds;	/* hold area for SVR4 IPC_SET */
	int			error = 0;
	struct cred 		*cr;
	int			cnt;
	kmutex_t		*lock;

	if ((lock = shmconv_lock(uap->shmid, &sp)) == NULL) {
		return (EINVAL);
	}

	cr = CRED();
	switch (uap->cmd) {

	/* Remove shared memory identifier. */
	case IPC_O_RMID:
	case IPC_RMID:
		if (cr->cr_uid != sp->shm_perm.uid &&
		    cr->cr_uid != sp->shm_perm.cuid &&
		    !suser(cr)) {
			error = EPERM;
			break;
		}
		if (((int)(++(sp->shm_perm.seq)*shminfo.shmmni + (sp - shmem)))
		    < 0)
			sp->shm_perm.seq = 0;

		/*
		 * When we created the shared memory segment,
		 * we set the refcnt to 2. When we shmat (and shmfork)
		 * we bump the refcnt.  We decrement it in
		 * kshmdt (called from shmdt and shmexit),
		 * and in IPC_RMID.  Thus we use a refcnt
		 * of 1 to mean that there are no more references.
		 * We do this so that the anon_map will not
		 * go away until we are ready, even if a process
		 * munmaps it's shared memory.
		 */
		mutex_enter(&sp->shm_amp->lock);
		cnt = --sp->shm_amp->refcnt;
		mutex_exit(&sp->shm_amp->lock);
		if (cnt == 1) {		/* if no attachments */
			if (isspt(sp))
				sptdestroy(sp->shm_sptas, sp->shm_amp);
			shm_rm_amp(sp->shm_amp, sp->shm_lkcnt);
		}
		sp->shm_lkcnt = 0;
		sp->shm_segsz = 0;
		sp->shm_amp = NULL;
		sp->shm_sptas = NULL;
		sp->shm_perm.mode = 0;
		error = 0;
		break;

	/* Set ownership and permissions. */
	case IPC_O_SET:
		if (cr->cr_uid != sp->shm_perm.uid &&
		    cr->cr_uid != sp->shm_perm.cuid &&
		    !suser(cr)) {
			error = EPERM;
			break;
		}
		if (copyin((caddr_t)uap->arg, (caddr_t)&ods, sizeof (ods))) {
			error = EFAULT;
			break;
		}
		if (ods.shm_perm.uid > MAXUID || ods.shm_perm.gid > MAXUID) {
			error = EINVAL;
			break;
		}
		sp->shm_perm.uid = ods.shm_perm.uid;
		sp->shm_perm.gid = ods.shm_perm.gid;
		sp->shm_perm.mode =
		    (ods.shm_perm.mode & 0777) | (sp->shm_perm.mode & ~0777);
		sp->shm_ctime = hrestime.tv_sec;
		break;

	case IPC_SET:
		if (cr->cr_uid != sp->shm_perm.uid &&
		    cr->cr_uid != sp->shm_perm.cuid &&
		    !suser(cr)) {
			error = EPERM;
			break;
		}
		if (copyin((caddr_t)uap->arg, (caddr_t)&ds, sizeof (ds))) {
			error = EFAULT;
			break;
		}
		if (ds.shm_perm.uid < (uid_t)0 || ds.shm_perm.uid > MAXUID ||
		    ds.shm_perm.gid < (gid_t)0 || ds.shm_perm.gid > MAXUID) {
			error = EINVAL;
			break;
		}
		sp->shm_perm.uid = ds.shm_perm.uid;
		sp->shm_perm.gid = ds.shm_perm.gid;
		sp->shm_perm.mode =
		    (ds.shm_perm.mode & 0777) | (sp->shm_perm.mode & ~0777);
		sp->shm_ctime = hrestime.tv_sec;
		break;

	/* Get shared memory data structure. */
	case IPC_O_STAT:
		if (error = ipcaccess(&sp->shm_perm, SHM_R, cr))
			break;

		/*
		 * We set refcnt to 2 in shmget.
		 * It is bumped twice for every attach.
		 */
		mutex_enter(&sp->shm_amp->lock);
		sp->shm_nattch = (sp->shm_amp->refcnt >> 1) - 1;
		mutex_exit(&sp->shm_amp->lock);
		sp->shm_cnattch = sp->shm_nattch;

		/*
		 * copy expanded shmid_ds struct to SVR3 o_shmid_ds.
		 * The o_shmid_ds data structure supports SVR3 applications.
		 * EFT applications use struct shmid_ds.
		 */
		if ((unsigned)sp->shm_perm.uid > USHRT_MAX ||
		    (unsigned)sp->shm_perm.gid > USHRT_MAX ||
		    (unsigned)sp->shm_perm.cuid > USHRT_MAX ||
		    (unsigned)sp->shm_perm.cgid > USHRT_MAX ||
		    (unsigned)sp->shm_perm.seq > USHRT_MAX ||
		    (unsigned)sp->shm_lpid > SHRT_MAX ||
		    (unsigned)sp->shm_cpid > SHRT_MAX ||
		    (unsigned)sp->shm_nattch > USHRT_MAX ||
		    (unsigned)sp->shm_cnattch > USHRT_MAX) {
			error = EOVERFLOW;
			break;
		}
		ods.shm_perm.uid = (o_uid_t)sp->shm_perm.uid;
		ods.shm_perm.gid = (o_gid_t)sp->shm_perm.gid;
		ods.shm_perm.cuid = (o_uid_t)sp->shm_perm.cuid;
		ods.shm_perm.cgid = (o_gid_t)sp->shm_perm.cgid;
		ods.shm_perm.mode = (o_mode_t)sp->shm_perm.mode;
		ods.shm_perm.seq = (ushort)sp->shm_perm.seq;
		ods.shm_perm.key = sp->shm_perm.key;
		ods.shm_segsz = sp->shm_segsz;
		ods.shm_amp = NULL;	/* kernel addr */
		ods.shm_lkcnt = sp->shm_lkcnt;
		ods.shm_pad[0] = 0; 	/* initialize SVR3 reserve pad */
		ods.shm_pad[1] = 0;
		ods.shm_lpid = (o_pid_t)sp->shm_lpid;
		ods.shm_cpid = (o_pid_t)sp->shm_cpid;
		ods.shm_nattch = (ushort)sp->shm_nattch;
		ods.shm_cnattch = (ushort)sp->shm_cnattch;
		ods.shm_atime = sp->shm_atime;
		ods.shm_dtime = sp->shm_dtime;
		ods.shm_ctime = sp->shm_ctime;

		if (copyout((caddr_t)&ods, (caddr_t)uap->arg, sizeof (ods)))
			error = EFAULT;
		break;

	case IPC_STAT:
		if (error = ipcaccess(&sp->shm_perm, SHM_R, cr))
			break;

		/*
		 * We set refcnt to 2 in shmget.
		 * It is bumped twice for every attach.
		 */
		mutex_enter(&sp->shm_amp->lock);
		sp->shm_nattch = (sp->shm_amp->refcnt >> 1) - 1;
		mutex_exit(&sp->shm_amp->lock);
		sp->shm_cnattch = sp->shm_nattch;

		if (copyout((caddr_t)sp, (caddr_t)uap->arg, sizeof (*sp)))
			error = EFAULT;
		break;

	/* Lock segment in memory */
	case SHM_LOCK:
		if (!suser(cr)) {
			error = EPERM;
			break;
		}
		if (!isspt(sp) && (sp->shm_lkcnt++ == 0)) {
			if (error = shmem_lock(sp->shm_amp)) {
			    mutex_enter(&sp->shm_amp->lock);
			    cmn_err(CE_NOTE,
				"shmctl - couldn't lock %d pages into memory",
			    sp->shm_amp->size);
			    mutex_exit(&sp->shm_amp->lock);
			    error = ENOMEM;
			    sp->shm_lkcnt--;
			    shmem_unlock(sp->shm_amp, 0);
			}
		}
		break;

	/* Unlock segment */
	case SHM_UNLOCK:
		if (!suser(cr)) {
			error = EPERM;
			break;
		}
		if (!isspt(sp)) {
			if (sp->shm_lkcnt && (--sp->shm_lkcnt == 0)) {
				shmem_unlock(sp->shm_amp, 1);
			}
		}
		break;

	default:
		error = EINVAL;
		break;
	}
	mutex_exit(lock);

	return (error);
}

/*
 * Detach shared memory segment.
 */
/*ARGSUSED1*/
static int
shmdt(struct shmdta *uap, rval_t *rvp)
{
	proc_t	*pp = curproc;
	int	rc;

	rc = kshmdt(pp, uap->addr);

	return (rc);
}

static int
kshmdt(proc_t *pp, caddr_t addr)
{
	struct shmid_ds	*sp;
	struct anon_map	*amp;
	segacct_t 	*sap, **sapp;
	struct as 	*as;
	kmutex_t	*lock;
	int		len, cnt;

	/*
	 * Is addr a shared memory segment?
	 */
	mutex_enter(&pp->p_lock);

	for (sapp = (segacct_t **)&pp->p_segacct; (sap = *sapp) != NULL;
	    sapp = &sap->sa_next)
		if (sap->sa_addr == addr)
			break;
	if (sap == NULL)  {
		mutex_exit(&pp->p_lock);
		return (EINVAL);
	}

	as = sap->sa_as;
	len = sap->sa_len;
	amp = sap->sa_amp;

	*sapp = sap->sa_next;

	kmem_free(sap, sizeof (segacct_t));

	ASSERT(PTOU(pp)->u_nshmseg > 0);
	PTOU(pp)->u_nshmseg--;

	mutex_exit(&pp->p_lock);

	(void) as_unmap(pp->p_as, addr, len);

	/*
	 * We increment refcnt for every shmat
	 * (and shmfork) and decrement for every
	 * detach (shmdt and shmexit).
	 * If the refcnt is now 1, there are no
	 * more references, and the IPC_RMID has
	 * been done.
	 */
	mutex_enter(&amp->lock);
	cnt = --amp->refcnt;
	mutex_exit(&amp->lock);
	if (cnt == 1) {
		if (as) 	/* isspt(sp) */
			sptdestroy(as, amp);
		shm_rm_amp(amp, 0);
		return (0);
	}

	/*
	 * Find shmem anon_map ptr in system-wide table.
	 * If not found, IPC_RMID has already been done.
	 */

	for (sp = shmem, lock = shmem_locks; sp < &shmem[shminfo.shmmni];
	    sp++, lock++) {
		mutex_enter(lock);
		if (sp->shm_amp == amp) {
			sp->shm_dtime = hrestime.tv_sec;
			sp->shm_lpid = pp->p_pid;
			mutex_exit(lock);
			break;
		}
		mutex_exit(lock);
	}

	return (0);
}

/*
 * Shmget (create new shmem) system call.
 */
static int
shmget(struct shmgeta *uap, rval_t *rvp)
{
	struct shmid_ds	*sp;		/* shared memory header ptr */
	uint		npages; 	/* how many pages */
	int		s;		/* ipcget status */
	int		size = uap->size;
	int		error = 0;
	int		index;
	kmutex_t	*lock;

	mutex_enter(&shm_lock);
	if (error = ipcget(uap->key, uap->shmflg, (struct ipc_perm *)shmem,
	    shminfo.shmmni, sizeof (*sp), &s, (struct ipc_perm **)&sp)) {
		mutex_exit(&shm_lock);
		return (error);
	}
	index = sp - shmem;
	lock = &shmem_locks[index];
	mutex_enter(lock);
	mutex_exit(&shm_lock);

	if (s) {
		/*
		 * This is a new shared memory segment.
		 * Allocate an anon_map structure and anon array and
		 * finish initialization.
		 */
		if (size < shminfo.shmmin || size > shminfo.shmmax) {
			sp->shm_perm.mode = 0;
			mutex_exit(lock);
			return (EINVAL);
		}

		/*
		 * Fail if we cannot get anon space.
		 */
		if (anon_resv((uint)size) == 0) {
			sp->shm_perm.mode = 0;
			mutex_exit(lock);
			return (ENOMEM);
		}

		/*
		 * Get number of pages required by this segment (round up).
		 */
		npages = btopr(size);

		sp->shm_amp = (struct anon_map *)
		    kmem_zalloc(sizeof (struct anon_map), KM_SLEEP);
		mutex_enter(&sp->shm_amp->lock);
		sp->shm_amp->anon = (struct anon **)
		    kmem_zalloc(npages * sizeof (struct anon *), KM_SLEEP);
		sp->shm_amp->swresv = sp->shm_amp->size = ptob(npages);
		/*
		 * We set the refcnt to 2 so that the anon_map
		 * will stay around even if we IPC_RMID
		 * and as_unmap (instead of shmdt) the shm.
		 * In that case we catch this in kshmdt,
		 * and free up the anon_map there.
		 */
		sp->shm_amp->refcnt = 2;
		mutex_exit(&sp->shm_amp->lock);

		/*
		 * Store the original user's requested size, in bytes,
		 * rather than the page-aligned size.  The former is
		 * used for IPC_STAT and shmget() lookups.  The latter
		 * is saved in the anon_map structure and is used for
		 * calls to the vm layer.
		 */

		sp->shm_segsz = size;
		sp->shm_atime = sp->shm_dtime = 0;
		sp->shm_ctime = hrestime.tv_sec;
		sp->shm_lpid = 0;
		sp->shm_cpid = curproc->p_pid;

		/* initialize reserve area */
		{
			int i;

			for (i = 0; i < 4; i++)
				sp->shm_perm.pad[i] = 0;
			sp->shm_pad1 = 0;
			sp->shm_pad2 = 0;
			sp->shm_pad3 = 0;
			for (i = 0; i < 4; i++)
				sp->shm_pad4[i] = 0;
		}
	} else {
		/*
		 * Found an existing segment.  Check size
		 */
		if (size && size > sp->shm_segsz) {
			mutex_exit(lock);
			return (EINVAL);
		}
	}

	rvp->r_val1 = sp->shm_perm.seq * shminfo.shmmni + (sp - shmem);
	mutex_exit(lock);
	return (0);
}

/*
 * System entry point for shmat, shmctl, shmdt, and shmget system calls.
 */

static int
shmsys(struct shmsysa *uap, rval_t *rvp)
{
	int error;

	switch (uap->opcode) {
	case SHMAT:
		error = shmat((struct shmata *)uap, rvp);
		break;
	case SHMCTL:
		error = shmctl((struct shmctla *)uap, rvp);
		break;
	case SHMDT:
		error = shmdt((struct shmdta *)uap, rvp);
		break;
	case SHMGET:
		error = shmget((struct shmgeta *)uap, rvp);
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

/*
 * add this record to the segacct list.
 */
static void
sa_add(
	struct proc *pp,
	caddr_t addr,
	size_t len,
	struct anon_map *amp,
	struct as 	*as)
{
	segacct_t *nsap, **sapp;

	nsap = kmem_alloc(sizeof (segacct_t), KM_SLEEP);

	nsap->sa_addr = addr;
	nsap->sa_len  = len;
	nsap->sa_amp  = amp;
	nsap->sa_as   = as;

	/* add this to the sorted list */
	mutex_enter(&pp->p_lock);
	sapp = (segacct_t **)&pp->p_segacct;
	while ((*sapp != NULL) && ((*sapp)->sa_addr < addr))
		sapp = &((*sapp)->sa_next);

	ASSERT((*sapp == NULL) || ((*sapp)->sa_addr >= addr));
	nsap->sa_next = *sapp;
	*sapp = nsap;

	PTOU(pp)->u_nshmseg++;
	mutex_exit(&pp->p_lock);
}

/*
 * Duplicate parents segacct records in child.
 */
void
shmfork(struct proc *ppp,	/* parent proc pointer */
	struct proc *cpp)	/* childs proc pointer */
{
	segacct_t *sap;

	/*
	 * We are the only lwp running in the parent so nobody can
	 * mess with our p_segacct list.  Thus it is safe to traverse
	 * the list without holding p_lock.  This is essential because
	 * we can't hold p_lock during a KM_SLEEP allocation.
	 */
	PTOU(cpp)->u_nshmseg = 0;
	sap = (segacct_t *)ppp->p_segacct;
	while (sap != NULL) {
		sa_add(cpp, sap->sa_addr, sap->sa_len, sap->sa_amp, sap->sa_as);
		/* increment for every shmat */
		mutex_enter(&sap->sa_amp->lock);
		sap->sa_amp->refcnt++;
		mutex_exit(&sap->sa_amp->lock);
		sap = sap->sa_next;
	}
}

/*
 * Detach shared memory segments from process doing exit.
 */
void
shmexit(struct proc *pp)
{
	/*
	 *  We don't need to grap the p_lock here; all
	 *  other threads are defunct and this process is
	 *  exiting
	 */
	while (pp->p_segacct != NULL)
		(void) kshmdt(pp, ((segacct_t *)pp->p_segacct)->sa_addr);
	ASSERT(PTOU(pp)->u_nshmseg == 0);
}

/*
 * At this time pages should be in memory, so just lock them.
 */

static void
lock_again(uint npages, struct anon_map *amp)
{
	struct anon **app;
	struct page *pp;
	struct vnode *vp;
	uint off;

	mutex_enter(&amp->lock);
	app = amp->anon;

	for (; npages != 0; app++, npages--) {

		swap_xlate(*app, &vp, &off);

		pp = page_lookup(vp, off, SE_SHARED);
		if (pp == NULL)
			cmn_err(CE_PANIC, "lock_again: page not in the system");
		(void) page_pp_lock(pp, 0, 0);
		page_unlock(pp);
	}
	mutex_exit(&amp->lock);
}

/* check if this segment is already locked. */
/*ARGSUSED*/
static int
check_locked(struct as *as, struct segvn_data *svd, uint npages)
{
	struct vpage *vpp = svd->vpage;
	uint i;
	if (svd->vpage == NULL)
		return (0);		/* unlocked */

	SEGVN_LOCK_ENTER(as, &svd->lock, RW_READER);
	for (i = 0; i < npages; i++) {
		if ((vpp++)->vp_pplock != 1) {
			SEGVN_LOCK_EXIT(as, &svd->lock);
			return (1);	/* partially locked */
		}
	}
	SEGVN_LOCK_EXIT(as, &svd->lock);
	return (2);			/* locked */
}



/*
 * Attach the share memory segment to process
 * address space and lock the pages.
 */

static int
shmem_lock(struct anon_map *amp)
{
	uint npages = btopr(amp->size);
	struct seg *seg;
	struct as *as;
	struct segvn_crargs crargs;
	struct segvn_data *svd;
	proc_t *p = curproc;
	caddr_t addr;
	uint lckflag, error, ret;

	as = p->p_as;
	AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
	/* check if shared memory is already attached */
	for (seg = AS_SEGP(as, as->a_segs); seg != NULL;
	    seg = AS_SEGP(as, seg->s_next)) {
		svd = (struct segvn_data *)seg->s_data;
		if ((seg->s_ops == &segvn_ops) && (svd->amp == amp) &&
		    (amp->size == seg->s_size)) {
			switch (ret = check_locked(as, svd, npages)) {
			case 0:			/* unlocked */
				AS_LOCK_EXIT(as, &as->a_lock);
				if ((error = as_ctl(as, seg->s_base,
				    seg->s_size, MC_LOCK, 0,
				    (caddr_t)NULL, (ulong *)NULL,
				    (size_t)NULL)) == 0)
					lock_again(npages, amp);
				(void) as_ctl(as, seg->s_base, seg->s_size,
				    MC_UNLOCK, 0, (caddr_t)NULL,
				    (ulong *)NULL, (size_t)NULL);
				return (error);
			case 1:			/* partially locked */
				break;
			case 2:			/* locked */
				AS_LOCK_EXIT(as, &as->a_lock);
				lock_again(npages, amp);
				return (0);
			default:
				cmn_err(CE_WARN, "shmem_lock: deflt %d", ret);
				break;
			}
		}
	}
	AS_LOCK_EXIT(as, &as->a_lock);

	/* attach shm segment to our address space */

	as_rangelock(as);
	map_addr(&addr, amp->size, (off_t)0, 1);
	if (addr == NULL) {
		as_rangeunlock(as);
		return (ENOMEM);
	}

	/* Initialize the create arguments and map the segment */
	crargs = *(struct segvn_crargs *)zfod_argsp;	/* structure copy */
	crargs.offset = 0;
	crargs.type = MAP_SHARED;
	crargs.amp = amp;
	crargs.maxprot = PROT_ALL;
	crargs.flags = 0;
	crargs.prot =  PROT_ALL;

	mutex_enter(&as->a_contents);
	if (!as->a_paglck) {
		lckflag = 1;
		as->a_paglck = 1;
	}
	mutex_exit(&as->a_contents);
	error = as_map(as, addr, amp->size, segvn_create,
			(caddr_t)&crargs);
	as_rangeunlock(as);
	if (!error) {
		lock_again(npages, amp);
		(void) as_unmap(as, addr, amp->size);
	}
	if (lckflag) {
		mutex_enter(&as->a_contents);
		as->a_paglck = 0;
		mutex_exit(&as->a_contents);
	}
	return (error);
}


/* Unlock shared memory */

static void
shmem_unlock(struct anon_map *amp, uint lck)
{
	struct anon **app = amp->anon;
	uint npages = btopr(amp->size);
	struct vnode *vp;
	struct page *pp;
	uint off;

	for (; npages != 0; app++, npages--) {
		if (*app == NULL) {
			if (lck)
				cmn_err(CE_PANIC, "shmem_unlock: null app");
			continue;
		}
		swap_xlate(*app, &vp, &off);
		pp = page_lookup(vp, off, SE_SHARED);
		if (pp == NULL) {
			if (lck)
				cmn_err(CE_PANIC,
				    "shmem_unlock: page not in the system");
			continue;
		}
		if (pp->p_lckcnt) {
			page_pp_unlock(pp, 0, 0);
		}
		page_unlock(pp);
	}
}

/*
 * We call this routine when we have
 * removed all references to this amp.
 * This means all shmdt's and the
 * IPC_RMID have been done.
 */
static void
shm_rm_amp(struct anon_map *amp, uint lckflag)
{
	/*
	 * If we are finally deleting the
	 * shared memory, and if no one did
	 * the SHM_UNLOCK, we must do it now.
	 */
	shmem_unlock(amp, lckflag);

	/*
	 * Free up the anon_map.
	 */
	anon_free(amp->anon, amp->size);
	anon_unresv(amp->swresv);
	kmem_free(amp->anon, btopr(amp->size) * sizeof (struct anon *));
	kmem_free(amp, sizeof (struct anon_map));
}
