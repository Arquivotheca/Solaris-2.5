/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)vm_machdep.c 1.24     95/07/12 SMI"

/*
 * UNIX machine dependent virtual memory support.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/cpuvar.h>
#include <sys/disp.h>
#include <sys/vm.h>
#include <sys/mman.h>
#include <sys/vnode.h>
#include <sys/cred.h>
#include <sys/exec.h>
#include <sys/exechdr.h>
#include <sys/cmn_err.h>
#include <sys/vmsystm.h>
#include <sys/bitmap.h>
#include <sys/debug.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_vn.h>
#include <vm/page.h>
#include <vm/seg_kmem.h>

#include <sys/cpu.h>
#include <sys/vm_machparam.h>
#include <sys/elf_SPARC.h>
#include <sys/machsystm.h>

#include <vm/hat_sfmmu.h>
#include <sys/bt.h>


extern	u_int	shm_alignment;	/* VAC address consistency modulus */

extern int vac_size;
extern int vac_shift;

#ifdef  DEBUG
#define	COLOR_STATS
#endif  DEBUG


/*
 * Handle a pagefault.
 */
faultcode_t
pagefault(addr, type, rw, iskernel)
	register caddr_t addr;
	register enum fault_type type;
	register enum seg_rw rw;
	register int iskernel;
{
	register struct as *as;
	register struct proc *p;
	register faultcode_t res;
	caddr_t base;
	u_int len;
	int err;

	if (iskernel) {
		as = &kas;
	} else {
		p = curproc;
		as = p->p_as;
	}

	/*
	 * Dispatch pagefault.
	 */
	res = as_fault(as->a_hat, as, addr, 1, type, rw);

	/*
	 * If this isn't a potential unmapped hole in the user's
	 * UNIX data or stack segments, just return status info.
	 */
	if (!(res == FC_NOMAP && iskernel == 0))
		return (res);

	/*
	 * Check to see if we happened to faulted on a currently unmapped
	 * part of the UNIX data or stack segments.  If so, create a zfod
	 * mapping there and then try calling the fault routine again.
	 */
	base = p->p_brkbase;
	len = p->p_brksize;

	if (addr < base || addr >= base + len) {		/* data seg? */
		base = (caddr_t)((caddr_t)USRSTACK - p->p_stksize);
		len = p->p_stksize;
		if (addr < base || addr >= (caddr_t)USRSTACK) {	/* stack seg? */
			/* not in either UNIX data or stack segments */
			return (FC_NOMAP);
		}
	}

	/* the rest of this function implements a 3.X 4.X 5.X compatibility */
	/* This code is probably not needed anymore */

	/* expand the gap to the page boundaries on each side */
	len = (((u_int)base + len + PAGEOFFSET) & PAGEMASK) -
	    ((u_int)base & PAGEMASK);
	base = (caddr_t)((u_int)base & PAGEMASK);

	as_rangelock(as);
	if (as_gap(as, NBPG, &base, &len, AH_CONTAIN, addr) != 0) {
		/*
		 * Since we already got an FC_NOMAP return code from
		 * as_fault, there must be a hole at `addr'.  Therefore,
		 * as_gap should never fail here.
		 */
		panic("pagefault as_gap");
	}

	err = as_map(as, base, len, segvn_create, zfod_argsp);
	as_rangeunlock(as);
	if (err)
		return (FC_MAKE_ERR(err));

	return (as_fault(as->a_hat, as, addr, 1, F_INVAL, rw));
}

/*ARGSUSED*/
void
map_addr(caddr_t *addrp, u_int len, off_t off, int align)
{
	map_addr_proc(addrp, len, off, align, curproc);
}

/*
 * map_addr_proc() is the routine called when the system is to
 * chose an address for the user.  We will pick an address
 * range which is just below the current stack limit.  The
 * algorithm used for cache consistency on machines with virtual
 * address caches is such that offset 0 in the vnode is always
 * on a shm_alignment'ed aligned address.  Unfortunately, this
 * means that vnodes which are demand paged will not be mapped
 * cache consistently with the executable images.  When the
 * cache alignment for a given object is inconsistent, the
 * lower level code must manage the translations so that this
 * is not seen here (at the cost of efficiency, of course).
 *
 * addrp is a value/result parameter.
 *	On input it is a hint from the user to be used in a completely
 *	machine dependent fashion.  We decide to completely ignore this hint.
 *
 *	On output it is NULL if no address can be found in the current
 *	processes address space or else an address that is currently
 *	not mapped for len bytes with a page of red zone on either side.
 *	If align is true, then the selected address will obey the alignment
 *	constraints of a vac machine based on the given off value.
 */
/*ARGSUSED*/
void
map_addr_proc(caddr_t *addrp, u_int len, off_t off, int align, struct proc *p)
{
	register struct as *as = p->p_as;
	register caddr_t addr;
	caddr_t base;
	u_int slen;
	u_int align_amount;

	base = p->p_brkbase;
	slen = (caddr_t)USRSTACK - base
	    - ((u.u_rlimit[RLIMIT_STACK].rlim_cur + PAGEOFFSET) & PAGEMASK);
	len = (len + PAGEOFFSET) & PAGEMASK;

	/*
	 * Redzone for each side of the request. This is done to leave
	 * one page unmapped between segments. This is not required, but
	 * it's useful for the user because if their program strays across
	 * a segment boundary, it will catch a fault immediately making
	 * debugging a little easier.
	 */
	len += (2 * PAGESIZE);

	/*
	 *  If the request is larger than the size of a particular
	 *  mmu level, then we use that level to map the request.
	 *  But this requires that both the virtual and the physical
	 *  addresses be aligned with respect to that level, so we
	 *  do the virtual bit of nastiness here.
	 */
	if (len >= MMU_PAGESIZE4M) {  /* 4MB mappings */
		align_amount = MMU_PAGESIZE4M;
	} else if (len >= MMU_PAGESIZE512K) { /* 512KB mappings */
		align_amount = MMU_PAGESIZE512K;
	} else if (len >= MMU_PAGESIZE64K) { /* 64KB mappings */
		align_amount = MMU_PAGESIZE64K;
	} else  {
		/*
		 * Align virtual addresses on a 64K boundary to ensure
		 * that ELF shared libraries are mapped with the appropriate
		 * alignment constraints by the run-time linker.
		 */
		align_amount = ELF_SPARC_MAXPGSZ;
	}

#ifdef VAC
	if (vac && align)
		if (align_amount < shm_alignment)
			align_amount = shm_alignment;
#endif

	len += align_amount;

	/*
	 * Look for a large enough hole starting below the stack limit.
	 * After finding it, use the upper part.  Addition of PAGESIZE is
	 * for the redzone as described above.
	 */
	if (as_gap(as, len, &base, &slen, AH_HI, (caddr_t)NULL) == 0) {
		caddr_t as_addr;

		addr = base + slen - len + PAGESIZE;
		as_addr = addr;
		/*
		 * Round address DOWN to the alignment amount,
		 * add the offset, and if this address is less
		 * than the original address, add alignment amount.
		 */
		addr = (caddr_t)((u_int)addr & (~(align_amount - 1)));
		addr += off & (align_amount - 1);
		if (addr < as_addr)
			addr += align_amount;

		ASSERT(addr <= (as_addr + align_amount));
		ASSERT(((u_int)addr & (align_amount - 1)) ==
			((u_int)off & (align_amount - 1)));
		*addrp = addr;
	} else {
		*addrp = ((caddr_t)NULL);	/* no more virtual space */
	}
}

/*
 * Determine whether [base, base+len] contains a mapable range of
 * addresses at least minlen long. base and len are adjusted if
 * required to provide a mapable range.
 */
/* ARGSUSED */
int
valid_va_range(caddr_t *basep, u_int *lenp, u_int minlen, int dir)
{
	register caddr_t hi, lo;

	lo = *basep;
	hi = lo + *lenp;

	/*
	 * If hi rolled over the top, try cutting back.
	 */
	if (hi < lo) {
		if (0 - (u_int)lo + (u_int)hi < minlen)
			return (0);
		if (0 - (u_int)lo < minlen)
			return (0);
		*lenp = 0 - (u_int)lo;
	} else if (hi - lo < minlen)
		return (0);
	return (1);
}

/*
 * Determine whether [addr, addr+len] are valid user addresses.
 */
int
valid_usr_range(caddr_t addr, u_int len)
{
	register caddr_t eaddr = addr + len;

	if (eaddr <= addr || addr >= (caddr_t)USRSTACK ||
	    eaddr > (caddr_t)USRSTACK)
		return (0);
	return (1);
}

/*
 * Check for valid program size
 */
/*ARGSUSED*/
chksize(ts, ds, ss)
	unsigned ts, ds, ss;
{
	/*
	 * Most of the checking is done by the as layer routines, we
	 * simply check the resource limits for data and stack here.
	 */
	if (ctob(ds) > u.u_rlimit[RLIMIT_DATA].rlim_cur ||
	    ctob(ss) > u.u_rlimit[RLIMIT_STACK].rlim_cur) {
		return (ENOMEM);
	}

	return (0);
}

/*
 * Routine used to check to see if an a.out can be executed
 * by the current machine/architecture.
 */
chkaout(exp)
	struct exdata *exp;
{

	if (exp->ux_mach == M_SPARC)
		return (0);
	else
		return (ENOEXEC);
}

/*
 * The following functions return information about an a.out
 * which is used when a program is executed.
 */

/*
 * Return the size of the text segment.
 */
long
getts(exp)
	struct exec *exp;
{

	return ((long)clrnd(btoc(exp->a_text)));
}

/*
 * Return the size of the data segment.
 */
long
getds(exp)
	struct exec *exp;
{

	return ((long)clrnd(btoc(exp->a_data + exp->a_bss)));
}

/*
 * Return the load memory address for the data segment.
 */
caddr_t
getdmem(exp)
	struct exec *exp;
{
	/*
	 * XXX - Sparc Reference Hack approaching
	 * Remember that we are loading
	 * 8k executables into a 4k machine
	 * DATA_ALIGN == 2 * NBPG
	 */
	if (exp->a_text)
		return ((caddr_t)(roundup(USRTEXT + exp->a_text, DATA_ALIGN)));
	else
		return ((caddr_t)USRTEXT);
}

/*
 * Return the starting disk address for the data segment.
 */
u_int
getdfile(struct exec *exp)
{

	if (exp->a_magic == ZMAGIC)
		return (exp->a_text);
	else
		return (sizeof (struct exec) + exp->a_text);
}

/*
 * Return the load memory address for the text segment.
 */

/*ARGSUSED*/
caddr_t
gettmem(struct exec *exp)
{

	return ((caddr_t)USRTEXT);
}

/*
 * Return the file byte offset for the text segment.
 */
u_int
gettfile(struct exec *exp)
{

	if (exp->a_magic == ZMAGIC)
		return (0);
	else
		return (sizeof (struct exec));
}

void
getexinfo(edp_in, edp_out, pagetext, pagedata)
	struct exdata *edp_in, *edp_out;
	int *pagetext;
	int *pagedata;
{
	*edp_out = *edp_in;	/* structure copy */

	if ((edp_in->ux_mag == ZMAGIC) &&
	    ((edp_in->vp->v_flag & VNOMAP) == 0)) {
		*pagetext = 1;
		*pagedata = 1;
	} else {
		*pagetext = 0;
		*pagedata = 0;
	}
}

/*
 * initialized by page_coloring_init()
 */
static u_int page_colors = 0;
static u_int page_colors_mask = 0;
static u_int vac_colors = 0;
static u_int vac_colors_mask = 0;

extern int do_pg_coloring;
extern int do_virtual_coloring;

/*
 * page freelist and cachelist are hashed into bins based on color
 */
#define	PAGE_COLORS_MAX	512
static	page_t *page_freelists[PAGE_COLORS_MAX];
static	page_t *page_cachelists[PAGE_COLORS_MAX];

/*
 * There are at most 512 colors/bins.  Spread them out under a
 * couple of locks.  There are mutexes for both the page freelist
 * and the page cachelist.
 */
#define	PC_SHIFT	(4)
#define	NPC_MUTEX	(PAGE_COLORS_MAX/(1 << PC_SHIFT))

static kmutex_t	fpc_mutex[NPC_MUTEX];
static kmutex_t	cpc_mutex[NPC_MUTEX];

#ifdef	COLOR_STATS

#define	COLOR_STATS_INC(x) (x)++;
#define	COLOR_STATS_DEC(x) (x)--;

static	u_int	pf_size[PAGE_COLORS_MAX];
static	u_int	pc_size[PAGE_COLORS_MAX];

static	u_int	sys_nak_bins[PAGE_COLORS_MAX];
static	u_int	sys_req_bins[PAGE_COLORS_MAX];

#else	COLOR_STATS

#define	COLOR_STATS_INC(x)
#define	COLOR_STATS_DEC(x)

#endif	COLOR_STATS


#define	PP_2_BIN(pp)		((pp->p_pagenum) & page_colors_mask)

#define	PC_BIN_MUTEX(bin, list)	((list == PG_FREE_LIST)? \
	&fpc_mutex[(((bin)>>PC_SHIFT)&(NPC_MUTEX-1))] :	\
	&cpc_mutex[(((bin)>>PC_SHIFT)&(NPC_MUTEX-1))])

/*
 * Function to get an ecache color bin: F(as, cnt, vcolor).
 * the goal of this function is to:
 * - to spread a processes' physical pages across the entire ecache to
 *	maximize its use.
 * - to minimize vac flushes caused when we reuse a physical page on a
 *	different vac color than it was previously used.
 * - to prevent all processes to use the same exact colors and trash each
 *	other.
 *
 * cnt is a bin ptr kept on a per as basis.  As we page_create we increment
 * the ptr so we spread out the physical pages to cover the entire ecache.
 * The virtual color is made a subset of the physical color in order to
 * in minimize virtual cache flushing.
 * We add in the as to spread out different as.  This happens when we
 * initialize the start count value.
 * sizeof(struct as) is 60 so we shift by 3 to get into the bit range
 * that will tend to change.  For example, on spitfire based machines
 * (vcshft == 1) contigous as are spread bu ~6 bins.
 * vcshft provides for proper virtual color alignment.
 * In theory cnt should be updated using cas only but if we are off by one
 * or 2 it is no big deal.
 * We also keep a start value which is used to randomize on what bin we
 * start counting when it is time to start another loop. This avoids
 * contigous allocations of ecache size to point to the same bin.
 * Why 3? Seems work ok. Better than 7 or anything larger.
 */
#define	PGCLR_LOOPFACTOR 3

#define	AS_2_BIN(as, cnt, vcolor, bin)					\
	cnt = as_color_bin(as);						\
	/* make sure physical color aligns with vac color */		\
	while ((cnt & vac_colors_mask) != vcolor) {			\
		cnt++;							\
	}								\
	bin = cnt = cnt & page_colors_mask;				\
	/* update per as page coloring fields */			\
	cnt = (cnt + 1) & page_colors_mask;				\
	if (cnt == (as_color_start(as) & page_colors_mask)) {		\
		cnt = as_color_start(as) = as_color_start(as) +		\
			PGCLR_LOOPFACTOR; 				\
	}								\
	as_color_bin(as) = cnt & page_colors_mask;			\
	ASSERT(bin <= page_colors_mask);


u_int
get_color_start(struct as *as)
{
	return ((((u_int)as >> 3) << (vac_shift - MMU_PAGESHIFT)) &&
		page_colors_mask);
}

/*
 * Take a particular page off of whatever freelist the page is claimed to be on.
 */
void
page_list_sub(int list, page_t *pp)
{
	u_int		bin;
	kmutex_t	*pcm;
	page_t		**ppp;

	ASSERT(se_excl_assert(&pp->p_selock));
	ASSERT(pp->p_free == 1);

	bin = PP_2_BIN(pp);
	pcm = PC_BIN_MUTEX(bin, list);

	if (list == PG_FREE_LIST) {
		ppp = &page_freelists[bin];
		COLOR_STATS_DEC(pf_size[bin]);
		ASSERT(pp->p_age == 1);


	} else {
		ppp = &page_cachelists[bin];
		COLOR_STATS_DEC(pc_size[bin]);
		ASSERT(pp->p_age == 0);
	}


	mutex_enter(pcm);
	page_sub(ppp, pp);
	mutex_exit(pcm);
}

void
page_list_add(int list, page_t *pp, int where)
{
	page_t		**ppp;
	kmutex_t	*pcm;
	u_int		bin;
	u_int		*pc_stats;

	ASSERT(se_excl_assert(&pp->p_selock));
	ASSERT(pp->p_free == 1);
	ASSERT(pp->p_mapping == NULL);

	bin = PP_2_BIN(pp);
	pcm = PC_BIN_MUTEX(bin, list);

	if (list == PG_FREE_LIST) {
		ASSERT(pp->p_age == 1);
		/* LINTED */
		ASSERT(pc_stats = &pf_size[bin]);  /* the `=' is correct */
		ppp = &page_freelists[bin];

	} else {
		ASSERT(pp->p_vnode);
		ASSERT((pp->p_offset & 0xfff) == 0);
		/* LINTED */
		ASSERT(pc_stats = &pc_size[bin]);  /* the `=' is correct */
		ppp = &page_cachelists[bin];
	}

	mutex_enter(pcm);
	COLOR_STATS_INC(*pc_stats);
	page_add(ppp, pp);

	if (where == PG_LIST_TAIL) {
		*ppp = (*ppp)->p_next;
	}
	mutex_exit(pcm);

	/*
	 * It is up to the caller to unlock the page!
	 */
	ASSERT(se_excl_assert(&pp->p_selock));
}


/*
 * When a bin is empty, and we can't satisfy a color request correctly,
 * we scan.  If we assume that the programs have reasonable spatial
 * behavior, then it will not be a good idea to use the adjacent color.
 * Using the adjacent color would result in virtually adjacent addresses
 * mapping into the same spot in the cache.  So, if we stumble across
 * an empty bin, skip a bunch before looking.  After the first skip,
 * then just look one bin at a time so we don't miss our cache on
 * every look. Be sure to check every bin.  Page_create() will panic
 * if we miss a page.
 *
 * This also explains the `<=' in the for loops in both page_get_freelist()
 * and page_get_cachelist().  Since we checked the target bin, skipped
 * a bunch, then continued one a time, we wind up checking the target bin
 * twice to make sure we get all of them bins.
 */
#define	BIN_STEP	20

/*
 * Find the `best' page on the freelist for this (vp,off) (as,vaddr) pair.
 *
 * Does its own locking and accounting.
 * If PG_MATCH_COLOR is set, then NULL will be returned if there are no
 * pages of the proper color even if there are pages of a different color.
 *
 * Finds a page, removes it, THEN locks it.
 */
/*ARGSUSED*/
page_t *
page_get_freelist(
	struct vnode *vp,
	u_int off,
	struct as *as,
	caddr_t vaddr,
	u_int flags)
{
	u_int		bin;
	kmutex_t	*pcm;
	int		i;
	page_t		*pp, *first_pp;
	u_int		bin_marker;
	int		colorcnt;

	/*
	 * Only hold one freelist lock at a time, that way we
	 * can start anywhere and not have to worry about lock
	 * ordering.
	 */
	AS_2_BIN(as, colorcnt, addr_to_vcolor(vaddr), bin);
	ASSERT(bin <= page_colors_mask);

	for (i = 0; i <= page_colors; i++) {
		COLOR_STATS_INC(sys_req_bins[bin]);
		if (page_freelists[bin]) {
			pcm = PC_BIN_MUTEX(bin, PG_FREE_LIST);
			mutex_enter(pcm);
			/* LINTED */
			if (pp = page_freelists[bin]) {
				/*
				 * These were set before the page
				 * was put on the free list,
				 * they must still be set.
				 */
				ASSERT(pp->p_free);
				ASSERT(pp->p_age);
				ASSERT(pp->p_vnode == NULL);
				ASSERT(pp->p_hash == NULL);
				ASSERT(pp->p_offset == -1);
				first_pp = pp;
				/*
				 * Walk down the hash chain
				 */
				while (!page_trylock(pp, SE_EXCL)) {
					pp = pp->p_next;

					ASSERT(pp->p_free);
					ASSERT(pp->p_age);
					ASSERT(pp->p_vnode == NULL);
					ASSERT(pp->p_hash == NULL);
					ASSERT(pp->p_offset == -1);

					if (pp == first_pp) {
						pp = NULL;
						break;
					}
				}

				if (pp) {
					COLOR_STATS_DEC(pf_size[bin]);
					page_sub(&page_freelists[bin], pp);

					if ((pp->p_free == 0) ||
					    (pp->p_age == 0)) {
						cmn_err(CE_PANIC,
						    "free page is not. pp %x",
						    pp, pcm);
					}
					mutex_exit(pcm);
					return (pp);
				}
			}
			mutex_exit(pcm);
		}

		/*
		 * Wow! The bin was empty.
		 */
		COLOR_STATS_INC(sys_nak_bins[bin]);
		if (flags & PG_MATCH_COLOR) {
			break;
		}
		if (i == 0) {
			bin = (bin + BIN_STEP) & page_colors_mask;
			bin_marker = bin;
		} else {
			bin = (bin +  vac_colors) & page_colors_mask;
			if (bin == bin_marker) {
				bin = (bin + 1) & page_colors_mask;
				bin_marker = bin;
			}
		}
	}
	return (NULL);
}

/*
 * Find the `best' page on the cachelist for this (vp,off) (as,vaddr) pair.
 *
 * Does its own locking.
 * If PG_MATCH_COLOR is set, then NULL will be returned if there are no
 * pages of the proper color even if there are pages of a different color.
 * Otherwise, scan the bins for ones with pages.  For each bin with pages,
 * try to lock one of them.  If no page can be locked, try the
 * next bin.  Return NULL if a page can not be found and locked.
 *
 * Finds a pages, TRYs to lock it, then removes it.
 */
/*ARGSUSED*/
page_t *
page_get_cachelist(
	struct vnode *vp,
	u_int off,
	struct as *as,
	caddr_t vaddr,
	u_int flags)
{
	kmutex_t	*pcm;
	int		i;
	page_t		*pp;
	page_t		*first_pp;
	int		bin;
	u_int		bin_marker;
	int		colorcnt;

	/*
	 * Only hold one cachelist lock at a time, that way we
	 * can start anywhere and not have to worry about lock
	 * ordering.
	 */
	AS_2_BIN(as, colorcnt, addr_to_vcolor(vaddr), bin);
	ASSERT(bin <= page_colors_mask);

	for (i = 0; i <= page_colors; i++) {
		COLOR_STATS_INC(sys_req_bins[bin]);
		if (page_cachelists[bin]) {
			pcm = PC_BIN_MUTEX(bin, PG_CACHE_LIST);
			mutex_enter(pcm);
			/* LINTED */
			if (pp = page_cachelists[bin]) {
				first_pp = pp;
				ASSERT(pp->p_vnode);
				ASSERT(pp->p_age == 0);
				while (!page_trylock(pp, SE_EXCL)) {
					pp = pp->p_next;
					if (pp == first_pp) {
						/*
						 * We have searched the
						 * complete list!
						 * And all of them (might
						 * only be one) are locked.
						 * This can happen since
						 * these pages can also be
						 * found via the hash list.
						 * When found via the hash
						 * list, they are locked
						 * first, then removed.
						 * We give up to let the
						 * other thread run.
						 */
						pp = NULL;
						break;
					}
					ASSERT(pp->p_vnode);
					ASSERT(pp->p_free);
					ASSERT(pp->p_age == 0);
				}

				if (pp) {
					/*
					 * Found and locked a page.
					 * Pull it off the list.
					 */
					COLOR_STATS_DEC(pc_size[bin]);
					page_sub(&page_cachelists[bin], pp);
					mutex_exit(pcm);
					ASSERT(pp->p_vnode);
					ASSERT(pp->p_age == 0);
					return (pp);
				}
			}
			mutex_exit(pcm);
		}
		COLOR_STATS_INC(sys_nak_bins[bin]);
		/*
		 * Wow! The bin was empty or no page could be locked.
		 * If only the proper bin is to be checked, get out
		 * now.
		 */
		if (flags & PG_MATCH_COLOR) {
			break;
		}
		if (i == 0) {
			bin = (bin + BIN_STEP) & page_colors_mask;
			bin_marker = bin;
		} else {
			bin = (bin +  vac_colors) & page_colors_mask;
			if (bin == bin_marker) {
				bin = (bin + 1) & page_colors_mask;
				bin_marker = bin;
			}
		}
		bin &= page_colors_mask;
	}

	return (NULL);
}

/*
 * page_coloring_init()
 * called once at startup from kphysm_init() -- before memialloc()
 * is invoked to do the 1st page_free()/page_freelist_add().
 *
 * initializes page_colors and page_colors_mask
 * based on the cache size of the boot CPU.
 *
 */
void
page_coloring_init()
{
	u_int i;
	char buffer[100];

	extern int ecache_size;			/* from obp properties */

	if (do_pg_coloring == 0)
		return;

	page_colors = ecache_size / MMU_PAGESIZE;
	page_colors_mask = page_colors - 1;

	vac_colors = vac_size / MMU_PAGESIZE;
	vac_colors_mask = vac_colors -1;

	for (i = 0; i < NPC_MUTEX; i++) {
		(void) sprintf(buffer, "fpc lock %d", i);
		mutex_init(&fpc_mutex[i], buffer, MUTEX_DEFAULT, NULL);

		(void) sprintf(buffer, "cpc lock %d", i);
		mutex_init(&cpc_mutex[i], buffer, MUTEX_DEFAULT, NULL);
	}


}
