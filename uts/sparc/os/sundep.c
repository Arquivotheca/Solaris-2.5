/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)sundep.c	1.85	95/02/27 SMI"	/* from SunOS-5.0 1.1 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <sys/class.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <sys/archsystm.h>

#include <sys/reboot.h>
#include <sys/uadmin.h>

#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/session.h>
#include <sys/ucontext.h>

#include <sys/dnlc.h>
#include <sys/var.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>
#include <sys/thread.h>
#include <sys/vtrace.h>
#include <sys/consdev.h>
#include <sys/frame.h>
#include <sys/stack.h>
#include <sys/swap.h>
#include <sys/vmparam.h>
#include <sys/cpuvar.h>

#include <sys/privregs.h>

#include <vm/hat.h>
#include <vm/anon.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>

#include <sys/exec.h>
#include <sys/acct.h>
#include <sys/modctl.h>
#include <sys/tuneable.h>

#include <c2/audit.h>

#include <sys/trap.h>
#include <sys/sunddi.h>
#include <sys/bootconf.h>
#include <sys/memlist.h>
#include <sys/dumphdr.h>
#include <sys/systeminfo.h>

extern void prom_printf(char *fmt, ...);
extern void prom_enter_mon(void);
extern void prom_panic();

#ifdef DEBUG
extern int mutex_critical_verifier;
#endif

extern int vac;
extern u_int shm_alignment;

/*
 * Compare the version of boot that boot says it is against
 * the version of boot the kernel expects.
 *
 * XXX	There should be no need to use promif routines here.
 */
int
check_boot_version(int boots_version)
{
	if (boots_version == BO_VERSION)
		return (0);

	prom_printf("Wrong boot interface - kernel needs v%d found v%d\n",
	    BO_VERSION, boots_version);
	prom_panic("halting");
	/*NOTREACHED*/
}

/*
 * Count the number of available pages and the number of
 * chunks in the list of available memory.
 */
void
size_physavail(
	struct memlist	*physavail,
	u_int		*npages,
	int		*memblocks)
{
	*npages = 0;
	*memblocks = 0;

	for (; physavail; physavail = physavail->next) {
		*npages += (u_int)(physavail->size >> PAGESHIFT);
		(*memblocks)++;
	}
}

/*
 * Returns the max contiguous physical memory present in the
 * memlist "physavail".
 */
u_longlong_t
get_max_phys_size(
	struct memlist	*physavail)
{
	u_longlong_t	max_size = 0;

	for (; physavail; physavail = physavail->next) {
		if (physavail->size > max_size)
			max_size = physavail->size;
	}

	return (max_size);
}


/*
 * Copy boot's physavail list deducting memory at "start"
 * for "size" bytes.
 */
int
copy_physavail(
	struct memlist	*src,
	struct memlist	**dstp,
	u_int		start,
	u_int		size)
{
	struct memlist *dst, *prev;
	u_int end1;
	int deducted = 0;

	dst = *dstp;
	prev = dst;
	end1 = start + size;

	for (; src; src = src->next) {
		u_longlong_t addr, lsize, end2;

		addr = src->address;
		lsize = src->size;
		end2 = addr + lsize;

		if ((size != 0) && start >= addr && end1 <= end2) {
			/* deducted range in this chunk */
			deducted = 1;
			if (start == addr) {
				/* abuts start of chunk */
				if (end1 == end2)
					/* is equal to the chunk */
					continue;
				dst->address = end1;
				dst->size = lsize - size;
			} else if (end1 == end2) {
				/* abuts end of chunk */
				dst->address = addr;
				dst->size = lsize - size;
			} else {
				/* in the middle of the chunk */
				dst->address = addr;
				dst->size = start - addr;
				dst->next = 0;
				if (prev == dst) {
					dst->prev = 0;
					dst++;
				} else {
					dst->prev = prev;
					prev->next = dst;
					dst++;
					prev++;
				}
				dst->address = end1;
				dst->size = end2 - end1;
			}
			dst->next = 0;
			if (prev == dst) {
				dst->prev = 0;
				dst++;
			} else {
				dst->prev = prev;
				prev->next = dst;
				dst++;
				prev++;
			}
		} else {
			dst->address = src->address;
			dst->size = src->size;
			dst->next = 0;
			if (prev == dst) {
				dst->prev = 0;
				dst++;
			} else {
				dst->prev = prev;
				prev->next = dst;
				dst++;
				prev++;
			}
		}
	}

	*dstp = dst;
	return (deducted);
}

struct vnode prom_ppages;
static int address64_in_memlist(struct memlist *mp, u_longlong_t a, u_int len);

/*
 * Find the pages allocated by the prom by diffing the original
 * phys_avail list and the current list.  In the difference, the
 * pages not locked belong to the PROM.  (The kernel has already locked
 * and removed all the pages it has allocated from the freelist, this
 * routine removes the remaining "free" pages that really belong to the
 * PROM and hashs them in on the 'prom_pages' vnode.)
 */
void
fix_prom_pages(struct memlist *orig, struct memlist *new)
{
	struct memlist *list, *nlist;

	nlist = new;
	for (list = orig; list; list = list->next) {

		u_longlong_t pa, end;
		u_int pfnum;
		page_t *pp;

		if (list->address == nlist->address &&
		    list->size == nlist->size) {
			nlist = nlist->next ? nlist->next : nlist;
			continue;
		}

		/*
		 * Loop through the old list looking to
		 * see if each page is still in the new one.
		 * If a page is not in the new list then we
		 * check to see if it locked permanently.
		 * If so, the kernel allocated and owns it.
		 * If not, then the prom must own it. We
		 * remove any pages found to owned by the prom
		 * from the freelist.
		 */
		end = list->address + list->size;
		for (pa = list->address; pa < end; pa += PAGESIZE) {

			if (address64_in_memlist(new, pa, PAGESIZE))
				continue;

			pfnum = (u_int)(pa >> PAGESHIFT);
			if ((pp = page_numtopp_nolock(pfnum)) == NULL)
				cmn_err(CE_PANIC, "missing pfnum %x", pfnum);

			if (se_nolock(&pp->p_selock)) {
				/*
				 * Ahhh yes, a prom page,
				 * suck it off the freelist,
				 * lock it, and hashin on prom_pages vp.
				 */
				if (page_trylock(pp, SE_EXCL) == 0)
					cmn_err(CE_PANIC, "prom page locked");

				(void) page_reclaim(pp, NULL);
				/*
				 * XXX	vnode offsets on the prom_ppages vnode
				 *	are page numbers (gack) for >32 bit
				 *	physical memory machines.
				 */
				(void) page_hashin(pp, &prom_ppages, pfnum,
				    NULL);

				page_downgrade(pp);
			}
		}
		nlist = nlist->next ? nlist->next : nlist;
	}
}

/*
 * Find the page number of the highest installed physical
 * page and the number of pages installed (one cannot be
 * calculated from the other because memory isn't necessarily
 * contiguous).
 */
void
installed_top_size(
	struct memlist *list,	/* pointer to start of installed list */
	int *topp,		/* return ptr for top value */
	int *sumpagesp)		/* return prt for sum of installed pages */
{
	int top = 0;
	int sumpages = 0;
	int highp;		/* high page in a chunk */

	for (; list; list = list->next) {
		highp = (list->address + list->size - 1) >> PAGESHIFT;
		if (top < highp)
			top = highp;
		sumpages += (u_int)(list->size >> PAGESHIFT);
	}

	*topp = top;
	*sumpagesp = sumpages;
}

/*
 * Check a memory list to see elements are
 * expansion memory.
 */
int
check_memexp(
	struct memlist	*list,
	u_int		memexp_start)
{
	int memexp_flag = 0;

	for (; list; list = list->next) {
		if (list->address >= memexp_start) {
			memexp_flag++;
			break;
		}
	}

	return (memexp_flag);
}

/*
 * Copy a memory list.  Used in startup() to copy boot's
 * memory lists to the kernel.
 */
void
copy_memlist(
	struct memlist	*src,
	struct memlist	**dstp)
{
	struct memlist *dst, *prev;

	dst = *dstp;
	prev = dst;

	for (; src; src = src->next) {
		dst->address = src->address;
		dst->size = src->size;
		dst->next = 0;
		if (prev == dst) {
			dst->prev = 0;
			dst++;
		} else {
			dst->prev = prev;
			prev->next = dst;
			dst++;
			prev++;
		}
	}

	*dstp = dst;
}

/*
 * Determine if a 32-bit address is in a memlist...
 */
int
address_in_memlist(struct memlist *mp, caddr_t va, u_int len)
{
	register u_int x = (u_int)va;

	while (mp != 0)	 {
		if ((x >= mp->address) &&
		    (x + len <= (mp->address + mp->size)))
			return (1);	 /* TRUE */
		mp = mp->next;
	}
	return (0);	/* FALSE */
}

/*
 * Determine if a 64-bit address is in a memlist...
 */
static int
address64_in_memlist(struct memlist *mp, u_longlong_t a, u_int len)
{
	while (mp != 0)	 {
		if ((a >= mp->address) &&
		    (a + len <= (mp->address + mp->size)))
			return (1);	 /* TRUE */
		mp = mp->next;
	}
	return (0);	/* FALSE */
}


/*
 * Kernel setup code, called from startup().
 */
void
kern_setup1(void)
{
	proc_t *pp;

	pp = &p0;

	proc_sched = pp;

	/*
	 * Initialize process 0 data structures
	 */
	pp->p_stat = SRUN;
	pp->p_flag = SLOAD | SSYS | SLOCK | SULOAD;

	pp->p_pidp = &pid0;
	pp->p_pgidp = &pid0;
	pp->p_sessp = &session0;
	pp->p_tlist = &t0;
	pid0.pid_pglink = pp;

	/*
	 * Make sure that the mutex fixup assumptions haven't been violated
	 */
	ASSERT(mutex_critical_verifier == 0);

	/*
	 * We assume that the u-area is zeroed out.
	 */
	u.u_cmask = (mode_t)CMASK;
	mutex_init(&u.u_flock, "u flist lock", MUTEX_DEFAULT, NULL);

	/*
	 * Set up default resource limits.
	 */
	bcopy((caddr_t)rlimits, (caddr_t)u.u_rlimit,
	    sizeof (struct rlimit) * RLIM_NLIMITS);

	thread_init();		/* init thread_free list */
#ifdef LATER
	hrtinit();		/* init hires timer free list */
	itinit();		/* init interval timer free list */
#endif
	pid_init();		/* initialize pid (proc) table */

	/*
	 * Determine pages_pp_maximum, the number of currently available
	 * pages (availrmem) that can't be `locked'. If not set by
	 * the user, we set it to 10% of the currently available memory.
	 * But we also insist that it be greater than tune.t_minarmem;
	 * otherwise a process could lock down a lot of memory, get swapped
	 * out, and never have enough to get swapped back in.
	 */
	if (pages_pp_maximum <= MAX(tune.t_minarmem+100, availrmem/10))
		pages_pp_maximum = MAX(tune.t_minarmem+100, availrmem/10);

	/*
	 * Set the system wide, processor-specific flags to be
	 * passed to userland via the aux vector. (Switch
	 * on any handy kernel optimizations at the same time.)
	 */
	bind_hwcap();
}

/*
 * Second stage setup - some startup-like things cannot
 * be done until after the root has been mounted.
 */
int
kern_setup2(void)
{
	consconfig();
	return (0);
}

static char *initname = "/etc/init";

static struct  bootcode {
	char    letter;
	u_int   bit;
} bootcode[] = {	/* See reboot.h */
	'a',	RB_ASKNAME,
	's',	RB_SINGLE,
	'i',	RB_INITNAME,
	'h',	RB_HALT,
	'b',	RB_NOBOOTRC,
	'd',	RB_DEBUG,
	'w',	RB_WRITABLE,
	'G',    RB_GDB,
	'r',    RB_RECONFIG,
	'c',    RB_CONFIG,
	'v',	RB_VERBOSE,
	'f',	RB_FLUSHCACHE,
	0,	0,
};

char kern_bootargs[256];		/* Max of all OBP_MAXPATHLEN's */

/*
 * Parse the boot line to determine boot flags .
 */
void
bootflags(void)
{
	register char *cp;
	register int i;

	if (BOP_GETPROP(bootops, "boot-args", kern_bootargs) != 0) {
		cp = (char *)0;
		boothowto |= RB_ASKNAME;
	} else {
		cp = kern_bootargs;

		while (*cp && *cp != ' ')
			cp++;

		while (*cp && *cp != '-')
			cp++;

		if (*cp && *cp++ == '-')
			do {
				for (i = 0; bootcode[i].letter; i++) {
					if (*cp == bootcode[i].letter) {
						boothowto |= bootcode[i].bit;
						break;
					}
				}
				cp++;
			} while (bootcode[i].letter && *cp);
	}

	if (boothowto & RB_INITNAME) {
		/*
		 * XXX	This is a bit broken - shouldn't we
		 *	really be using the initpath[] above?
		 */
		while (*cp && *cp != ' ' && *cp != '\t')
			cp++;
		initname = cp;
	}

	if (boothowto & RB_HALT) {
		prom_printf("kernel halted by -h flag\n");
		prom_enter_mon();
	}

	if (boothowto & RB_GDB) {
		extern int gdbon;
		gdbon = 1;
	}
}


/*
 * MP initialization.
 */
void
mp_init(void)
{
	extern void start_other_cpus(int);	/* should be in a header file */

	/*
	 * Start other CPUs, if any, now.
	 */
	start_other_cpus(0);
}

/*
 * Start the initial user process.
 * The program [initname] is invoked with one argument
 * containing the boot flags.
 */
void
icode(void)
{
	struct execa *ap;
	char *ucp, **uap, *arg0, *arg1;
	register proc_t *p = ttoproc(curthread);
	static char pathbuf[128];
	int i, error = 0;
	klwp_t *lwp = ttolwp(curthread);
	extern void lwp_rtt();

	/*
	 * Allocate user address space and stack segment
	 */
	p->p_cstime = p->p_stime = p->p_cutime = p->p_utime = 0;
	proc_init = ttoproc(curthread);
	p->p_as = as_alloc();
	(void) hat_setup(p->p_as, HAT_ALLOC);
	(void) as_map(p->p_as, (caddr_t)(USRSTACK - PAGESIZE),
	    PAGESIZE, segvn_create, zfod_argsp);

	/*
	 * Construct the boot flag argument.
	 */
	ucp = (char *)USRSTACK;
	(void) subyte(--ucp, '\0');		/* trailing null byte */

	/*
	 * XXX - should we also handle "-i" ?
	 */
	if (boothowto & RB_SINGLE)
		(void) subyte(--ucp, 's');
	if (boothowto & RB_NOBOOTRC)
		(void) subyte(--ucp, 'b');
	if (boothowto & RB_RECONFIG)
		(void) subyte(--ucp, 'r');
	if (boothowto & RB_VERBOSE)
		(void) subyte(--ucp, 'v');
	if (boothowto & RB_FLUSHCACHE)
		(void) subyte(--ucp, 'f');
	(void) subyte(--ucp, '-');		/* leading hyphen */
	arg1 = ucp;

	/*
	 * Build a pathname.
	 */
	(void) strcpy(pathbuf, initname);

	/*
	 * Move out the file name (also arg 0).
	 */
	for (i = 0; pathbuf[i]; i++);		/* size the name */
	for (; i >= 0; i--)
		(void) subyte(--ucp, pathbuf[i]);
	arg0 = ucp;

	/*
	 * Move out the arg pointers.
	 */
	uap = (char **)((int)ucp & ~(NBPW-1));
	(void) suword((int *)--uap, 0);	/* terminator */
	(void) suword((int *)--uap, (int)arg1);
	(void) suword((int *)--uap, (int)arg0);

	/*
	 * Point at the arguments.
	 */
	lwp->lwp_ap = lwp->lwp_arg;
	ap = (struct execa *)lwp->lwp_ap;
	ap->fname = arg0;
	ap->argp = uap;
	ap->envp = 0;
	curthread->t_sysnum = SYS_execve;

	init_mstate(curthread, LMS_SYSTEM);

	/*
	 * Now let exec do the hard work.
	 */
	if (error = exece(ap, (rval_t *)NULL)) {
		printf("Can't invoke %s, error %d\n", initname, error);
		cmn_err(CE_PANIC, "icode");
	}

	lwp_rtt();
}

/*
 * Load a procedure into a thread.
 */
int
thread_load(
	kthread_id_t	t,
	void		(*start)(),
	caddr_t		arg,
	int		len)
{
	struct rwindow *rwin;
	caddr_t sp;
	int framesz;
	caddr_t argp;
	void thread_start();
	label_t ljb;

	/*
	 * Push a "c" call frame onto the stack to represent
	 * the caller of "start".
	 */
	sp = t->t_stk;
	if (len > 0) {
		/*
		 * the object that arg points at is copied into the
		 * caller's frame.
		 */
		framesz = SA(len);
		sp -= framesz;
		if (sp < t->t_swap) 	/* stack grows down */
			return (-1);
		argp = sp + SA(MINFRAME);
		if (on_fault(&ljb)) {
			no_fault();
			return (-1);
		}
		copyout_noerr(arg, argp, len);
		no_fault();
		arg = (void *)argp;
	}
	/*
	 * store arg and len into the frames input register save area.
	 * these are then transfered to the first 2 output registers by
	 * thread_start() in swtch.s.
	 */
	rwin = (struct rwindow *)sp;
	rwin->rw_in[0] = (int)arg;
	rwin->rw_in[1] = len;
	rwin->rw_in[6] = 0;
	rwin->rw_in[7] = (int)start;
	/*
	 * initialize thread to resume at thread_start().
	 */
	t->t_pc = (u_int)thread_start - 8;
	t->t_sp = (u_int)sp;

	return (0);
}

/*
 * Condition variable used while waiting for virtual address
 * to become available in the kernel's address space during exec.
 */
static kcondvar_t args_cv;

/*
 * Allocate a chunk of anon memory and initialize the uarg structure
 * to point at it.  This chunk will eventually be remapped to USRSTACK.
 * Size is rounded up to be a multiple of PAGESIZE.
 */
static int
alloc_hunk(
	struct as	*as,
	u_int		size,
	struct uarg	*args)
{
	struct segvn_crargs crargs;
	struct anon_map *amp;
	struct anon **anonp;
	caddr_t base;
	u_int len, anon_size;
	u_int nsize;
	int error;
	u_int mapsize;
	int vcolor;
	int adjust_size;

	base = (caddr_t)ARGSBASE;	/* start of virtual arg space area */
	len = NCARGS;

	nsize = roundup(size, PAGESIZE);

	/*
	 * Reserve swap space, allocate and intialize the anon_map.
	 *
	 * Note:  anonmap_alloc() is not used as a performance optimization
	 * since it initializes the the various mutexes in the anon_map
	 * structure.  This also assumes that adaptive mutexes are of type
	 * 0 (yuk!).
	 *
	 * Allocate bigger anon map than needed to preload extra stack pages.
	 */
	if (anon_resv(nsize) == 0)
		return (ENOMEM);
	amp = (struct anon_map *)kmem_zalloc(sizeof (*amp), KM_SLEEP);
	mapsize = nsize + roundup(SSIZE, PAGESIZE);
	anon_size = btop(mapsize) * sizeof (struct anon *);
	anonp = (struct anon **)kmem_zalloc(anon_size, KM_SLEEP);
	amp->refcnt = 1;
	amp->size = mapsize;
	amp->anon = anonp;
	if (vac) {
		/*
		 * try to choose a base address that aligns with the same
		 * virtual color as the user stack.
		 * XXX should use addr_to_vcolor once it becomes machine
		 * independent.
		 */
		vcolor = ((USRSTACK - nsize) & (shm_alignment - 1))
				>> MMU_PAGESHIFT;
		adjust_size = mmu_ptob(vcolor);
		if ((NCARGS - adjust_size) >= nsize)
		base = (caddr_t)((u_int)base + adjust_size);
	}


	TRACE_5(TR_FAC_VM, TR_ANON_EXEC, "anon exec:%u %u %u %u %x",
	    amp, 0, 0, nsize, 1);

	as_rangelock(as);		/* serialize access to as ranges */

	while (as_gap(as, nsize, &base, &len, AH_LO, NULL)) {
		/*
		 * Need to wait for virtual address space to be freed,
		 * so release hold on "claimgap" and wakeup any other
		 * blocked threads.
		 */
		as_rangewait(as, &args_cv);
	}

	/*
	 * Now setup the arguments for segvn_create and map the segment
	 * into the specified address space.
	 */
	crargs.vp = NULL;
	crargs.offset = mapsize - nsize;
	crargs.cred = NULL;
	crargs.type = MAP_SHARED;
	crargs.maxprot = PROT_ALL;
	crargs.prot = PROT_ALL;
	if (as == &kas) {
		crargs.maxprot &= ~PROT_USER;
		crargs.prot &= ~PROT_USER;
	}
	crargs.flags = 0;
	crargs.amp = amp;
	if (error = as_map(as, base, nsize, segvn_create, (caddr_t)&crargs)) {
		anon_unresv(nsize);
		as_rangeunlock(as);
		kmem_free((caddr_t)amp, sizeof (*amp));
		kmem_free((caddr_t)anonp, anon_size);
		TRACE_5(TR_FAC_VM, TR_ANON_EXEC, "anon exec:%u %u %u %u %u",
		    amp, 0, 0, nsize, 0);
		return (error);
	}
	as_rangeunlock(as);
	(void) as_fault(as->a_hat, as, base, nsize, F_INVAL, S_WRITE);

	/*
	 * Initialize arg structure.
	 */
	args->as = as;
	args->hunk_base = base;
	args->amp = amp;
	args->hunk_size = nsize;
	return (0);
}

/*
 * Discard a hunk of memory that was allocated by alloc_hunk().
 */
static void
free_hunk(struct anon_map *amp, u_int size)
{
	ASSERT(amp->refcnt == 1);

	anon_free(amp->anon, amp->size);
	anon_unresv(size);
	kmem_free((caddr_t)amp->anon, btop(amp->size) * sizeof (struct anon *));
	kmem_free((caddr_t)amp, sizeof (*amp));
	TRACE_5(TR_FAC_VM, TR_ANON_EXEC, "anon exec:%u %u %u %u %u",
	    amp, 0, 0, size, 0);
}

/*
 * Map a hunk of memory to another address.
 */
static int
map_hunk(
	struct as	*as,
	struct anon_map	*amp,
	caddr_t		addr,
	u_int		size)
{
	struct segvn_crargs crargs;
	int error;

	crargs.vp = NULL;
	crargs.offset = 0;
	crargs.cred = NULL;
	crargs.type = MAP_PRIVATE;
	crargs.maxprot = PROT_ALL;
	crargs.prot = PROT_ALL;
	crargs.flags = 0;
	crargs.amp = amp;

	/*
	 * Remap anon pages containing the arguments.
	 */
	if (error = as_map(as, addr, size, segvn_create, (caddr_t)&crargs))
		return (error);
	return (0);
}

/*
 * Calculate the amount of space used by auxiliary vector strings.
 */
static void
exec_auxsize(
	struct execa	*uap,
	struct uarg	*args,
	struct intpdata	*idata,
	int		**aux)
{
	size_t l;

	if ((aux == 0) || (*aux == 0))
		return;

	/*
	 * for AT_SUN_PLATFORM
	 */
	**aux = AT_SUN_PLATFORM;
	l = strlen(platform) + 1;

	/*
	 * "nc" is the size of all the information block info, i.e.
	 * argv[] and environ[] strings and auxiliary information.
	 *
	 * preserve "nc" size previously calculated by exec_argsize()
	 */
	l = (l + NBPW - 1) & ~(NBPW - 1);
	args->nc += l;
}

/*
 * Calculate the amount of space used by *argv[], and *environ[].
 */
static void
exec_argsize(
	struct execa	*uap,
	struct uarg	*args,
	struct intpdata	*idata)
{
	char **argvp;
	char **envp;
	caddr_t str;
	int i, j, l;

	argvp = uap->argp;
	envp = uap->envp;

	i = 0;
	l = 0;
	/*
	 * Calculate size of the program interpreting the file
	 * referenced by argv[0].
	 */
	if (idata && idata->intp_name) {
		argvp++;	/* ignore argv[0] */
		l += strlen(idata->intp_name);
		if (idata->intp_arg) {
			l += strlen(idata->intp_arg);
			i++;
		}
		if (args->fname != NULL)
			l += strlen(args->fname);
		else
			l += ustrlen(uap->fname);
		i += 2;
	}

	/*
	 * Calculate the size of "argvp".
	 */
	while ((str = (caddr_t)fuword_noerr((int *)argvp++)) != NULL) {
		l += ustrlen(str);
		i++;
	}

	/*
	 * Calculate the size of "envp".
	 */
	j = 0;
	if (envp) {
		while ((str = (caddr_t)fuword_noerr((int *)envp++)) != NULL) {
			l += ustrlen(str);
			j++;
		}
	}
	args->na = i + j;
	args->ne = j;

	/*
	 * "na" is the number of argv[] and environ[] pointers while
	 * "ne" is the just the number of environ[] pointers.  "nc" is
	 * the number of characters in argv[] and environ[] strings.
	 *
	 * "i + j" is the number of bytes used to terminate the strings
	 * since they are not included by strlen.
	 */
	l = l + (i + j);
	l = (l + NBPW - 1) & ~(NBPW - 1);
	args->nc = l;
}

static int fastbuildstack(struct execa *, struct uarg *,
    struct intpdata *idata, u_int size, int **aux);

/*
 * Move exec arguments and environment variables to the new user's
 * stack.
 *
 * NOTE:  If a -1 is returned as an error, it implies that the old
 * address space is either partially or completely destroyed and
 * it is the callers responsiblity to send a SIGKILL to the process.
 */
int
exec_args(
	struct execa	*uap,
	struct uarg	*args,
	struct intpdata	*idata,
	int		**aux)
{
	int error;
	label_t ljb;
	u_int size;
	int hunk_allocated = 0;

	if (uap->argp == NULL)
		return (0);

	if (on_fault(&ljb)) {
		/*
		 * Free hunk, if allocated.
		 * XXX - Will this work?
		 */
		if (hunk_allocated) {
			(void) as_unmap(args->as, args->hunk_base,
			    args->hunk_size);
		}
		no_fault();
		return (EFAULT);
	}

	exec_argsize(uap, args, idata);
	exec_auxsize(uap, args, idata, aux);
	size = SA(args->nc + (args->na + 4) * NBPW) + args->auxsize;
	if (roundup(size + sizeof (struct rwindow), PAGESIZE) > NCARGS) {
		no_fault();
		return (E2BIG);
	}

	/*
	 * Allocate one chunk of anon memory that'll hold
	 * argv[], environ[], and their strings in the kernel's
	 * address space.
	 */
	error = alloc_hunk(&kas, size + sizeof (struct rwindow), args);
	if (error == 0) {
		hunk_allocated = 1;
		error = fastbuildstack(uap, args, idata, size, aux);
		if (error)
			error = -1;
	}
	no_fault();
	return (error);
}

/*
 * Copy the user arguments into a temporary area in the old user's
 * address space before invalidating the remaining segments.
 *
 * 			User Stack
 *	argvp_end ---->	-----------------	-  HIGH ADDRS ----
 *			|	NULL	|	|		|
 *			-----------------	|		|
 *			|		|	|		|
 * 			|   		|	|		|
 *			|    ARGUMENT	|	|		|
 *			|    STRINGS	|	|		|
 *			|		|	strbuflen	|
 *  			|		|	|		|
 *  			|		|	|		|
 *  			|		|	|		|
 * 			|		|	|		|
 * 			|		|	|		|
 *			----------------- <----- strp		|
 *			|    AUX	|			size
 *			|    VECTOR	|			|
 *			-----------------			|
 *			|    NULL	|    ^			|
 *			-----------------    |			|
 *			|		|-----			|
 *	env[] -------->	-----------------			|
 *			|    NULL	|	^		|
 *			-----------------	|		|
 *			|		|--------   ^		|
 *			-----------------	    |		|
 *			|		|------------		|
 *	argv[] ------->	-----------------			|
 *			|    argc	|			|
 *	argvp -------->	-----------------			--
 *			| struct rwindow|
 *			-----------------	   LOW ADDRS
 *
 */
static int
fastbuildstack(
	struct execa	*uap,
	struct uarg	*args,
	struct intpdata	*idata,
	u_int		size,
	int		**aux)
{
	u_int *argvp;
	u_int argvp_end, usrsp, len;
	u_int strp, ostrp;
	caddr_t ps_strp, addr;
	int ps_len, argc, ne;
	int strbuflen, error;
	struct as *as;
	struct anon_map *amp;
	u_int args_size;
	proc_t *pp = ttoproc(curthread);
	klwp_id_t lwp = ttolwp(curthread);
	struct user *up = PTOU(pp);
	u_int mapsize;
	char *uargp;
	char *uenvp;

#ifdef sparc
	/*
	 * Make sure user register windows are empty before
	 * attempting to make a new stack.
	 */
	flush_user_windows();
#endif

	argc = args->na - args->ne;
	argvp_end = (u_int)(args->hunk_base + args->hunk_size);
	strbuflen = args->nc + NBPW;
	strp = argvp_end - strbuflen;

	/* for /proc */
	up->u_argc = argc;
	up->u_argv = (char **)((u_int)USRSTACK - size + NBPW);
	up->u_envp = up->u_argv + argc + 1;

	argvp = (u_int *)(argvp_end - size);
	usrsp = (u_int)USRSTACK - strbuflen;

	*argvp++ = argc;
	ps_strp = (caddr_t)strp;	/* remember for u_psargs */
	ostrp = strp;

	/*
	 * Copy interpreter's name and argument to argv[0] and argv[1].
	 */
	if (idata && idata->intp_name) {
		(void) knstrcpy((caddr_t)strp, idata->intp_name, &len);
		uap->argp++;			/* ignore argv[0] */
		*argvp++ = usrsp;
		strp += len;
		usrsp += len;

		/* argv[1] or argv[2] */
		if (idata->intp_arg) {
			(void) knstrcpy((caddr_t)strp, idata->intp_arg, &len);
			*argvp++ = usrsp;
			strp += len;
			usrsp += len;
			argc--;
		}
		if (args->fname != NULL)
			(void) knstrcpy((caddr_t)strp, args->fname, &len);
		else
			(void) copyinstr_noerr((caddr_t)strp, uap->fname, &len);
		*argvp++ = usrsp;
		strp += len;
		usrsp += len;
		argc -= 2;
	}

	/*
	 * Copy exec arguments to stack.
	 */
	while (argc-- > 0) {
		uargp = (caddr_t)fuword_noerr((int *)uap->argp++);
		(void) copyinstr_noerr((caddr_t)strp, uargp, &len);
		*argvp++ = usrsp;
		strp += len;
		usrsp += len;
	}
	*argvp++ = 0;		/* NULL terminate argv pointers */

	/*
	 * Copy arguments to u.u_psargs.
	 */
	ps_len = min((caddr_t)strp - ps_strp, PSARGSZ);
	(void) bzero(up->u_psargs, PSARGSZ);
	if (ps_len > 0) {
		(void) bcopy(ps_strp, up->u_psargs, ps_len);
		ps_strp = &up->u_psargs[--ps_len];
		*ps_strp = '\0';		/* NULL terminate the string */
		while (--ps_strp >= up->u_psargs) {
			if (*ps_strp == '\0')
				*ps_strp = ' ';
		}
	}

	/*
	 * Copy environ variables to stack.
	 */
	ne = args->ne;
	while (ne-- > 0) {
		uenvp = (caddr_t)fuword_noerr((int *)uap->envp++);
		(void) copyinstr_noerr((caddr_t)strp, uenvp, &len);
		*argvp++ = usrsp;
		strp += len;
		usrsp += len;
	}
	*argvp++ = 0;		/* NULL terminate env pointers */

	/*
	 * copy auxiliary vector information to stack
	 */
	if (aux && *aux) {
		if (**aux == AT_SUN_PLATFORM) {
			(int)(*aux)++;
			(void) knstrcpy((caddr_t)strp, platform, &len);
			**aux = usrsp;
			(int)(*aux)++;
			strp += len;
			usrsp += len;
		}
	}

	/*
	 * Determine the location of the "aux" vector.
	 */
	args->stackend = (caddr_t)(USRSTACK - (argvp_end - (u_int)argvp));
	if (ostrp - (u_int)argvp < args->auxsize) {
		cmn_err(CE_PANIC,
		    "fastbuildstack: arg strings may be overwritten");
	}

#ifdef  C2_AUDIT
	if (audit_active)
		audit_exec(USRSTACK, argvp_end, size, args->na, args->ne);
#endif

	/*
	 * At this point, we are committed to the new image!
	 * Release virtual memory resource of old process, and
	 * initialize the virtual memory of the new process.
	 */
	relvm(pp);
	pp->p_brkbase = NULL;
	pp->p_brksize = 0;
	pp->p_stksize = 0;

	lwp->lwp_pcb.pcb_xregstat = XREGNONE;
	lwptoregs(lwp)->r_sp =
		(u_int)USRSTACK - (size + sizeof (struct rwindow));

	/*
	 * Allocate an address space and setup its context.
	 */
	as = pp->p_as = as_alloc();
	(void) hat_setup(as, HAT_ALLOC);

	amp = args->amp;
	args_size = args->hunk_size;
	mapsize = amp->size;


	/*
	 * Now unmap the segment which contains the arguments.  This
	 * will only free up the segment since the anon_map reference
	 * count is 2.
	 *
	 * NOTE:  The segment has to be unmapped before mapping in the
	 * stack in order to prevent overlapping segments if both are
	 * in the same address space.
	 */
	ASSERT(amp->refcnt == 2);

	error = as_unmap(args->as, args->hunk_base, args_size);
	as_rangebroadcast(args->as, &args_cv);

	ASSERT(amp->refcnt == 1);

	/*
	 * Map args into the user's stack.
	 */
	if (error == 0) {
		addr = (caddr_t)USRSTACK - mapsize;
		error = map_hunk(as, amp, addr, mapsize);
		pp->p_stksize = mapsize;
	}

	/*
	 * Now, discard anon map and its associated structures.
	 */
	free_hunk(amp, args_size);

	/*
	 * Ensure that stack pages have writable translations by
	 * forcing a minor fault.
	 */
	if (error == 0) {
		(void) as_fault(as->a_hat, as, addr, mapsize,
		    F_INVAL, S_WRITE);
	}
	return (error);
}


/*
 * Machine-dependent portion of dump-checking;
 * mark all pages for dumping; moved here since
 * it was the same for all Sun architectures.
 */
void
dump_allpages_machdep(void)
{
	u_int	i, j;
	struct memlist	*pmem;

	for (pmem = phys_install; pmem; pmem = pmem->next) {
		i = pmem->address >> PAGESHIFT;
		j = i + (pmem->size >> PAGESHIFT);
		for (; i < j; i++)
			dump_addpage(i);
	}
}
