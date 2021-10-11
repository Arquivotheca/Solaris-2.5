/*
 * Copyright (c) 1987, 1988-1991 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)vm_machdep.c 1.38     95/01/30 SMI"

/* Merged from SVR4 1.5 && SunOS 4.1.1 sun4/vm_machdep.c 1.19 */

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
#include <sys/debug.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_vn.h>
#include <vm/page.h>
#include <vm/seg_kmem.h>

#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/cpu.h>
#include <sys/vmparam.h>
#include <sys/memerr.h>

#include <vm/hat_sunm.h>

/*
 * Handle a pagefault.
 */
faultcode_t
pagefault(
	register caddr_t	addr,
	register enum fault_type type,
	register enum seg_rw	rw,
	register int		iskernel)
{
	register struct as *as;
	register struct proc *p;
	register faultcode_t res;
	struct pte pte;
	caddr_t base;
	u_int len;
	int err;

	if (!good_addr(addr))
		return (FC_NOMAP);

	if (iskernel) {
		as = &kas;
	} else {
		p = curproc;
		as = p->p_as;
	}

	/*
	 * Setting up context in sunm_fault is not sufficient
	 * because pagefault() is called via trap from the register
	 * window overflow and underflow handlers which bypass
	 * sunm_fault.
	 */
	mutex_enter(&sunm_mutex);
	if (iskernel == 0 && mmu_getctx()->c_as != as) {
		(void) sunm_setup(as, HAT_ALLOC);
		curthread->t_mmuctx = 0;
	}
	mutex_exit(&sunm_mutex);

#ifdef	DEBUG
	if (addr < (caddr_t)KERNELBASE) {
		mmu_getpte(addr, &pte);
		ASSERT(!(iskernel == 0 && pte_valid(&pte) &&
		    (pte.pg_prot == KW || pte.pg_prot == KR)));
	}
#endif	/* DEBUG */

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
		if (addr < base || addr >= base + len) {	/* stack seg? */
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
		cmn_err(CE_PANIC, "pagefault as_gap");
	}

	err = as_map(as, base, len, segvn_create, zfod_argsp);
	as_rangeunlock(as);
	if (err)
		return (FC_MAKE_ERR(err));

	return (as_fault(as->a_hat, as, addr, 1, F_INVAL, rw));
}

/*ARGSUSED*/
void
map_addr(addrp, len, off, align)
	caddr_t *addrp;
	register u_int len;
	off_t off;
	int align;
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
void
map_addr_proc(
	caddr_t		*addrp,
	register u_int	len,
	off_t		off,
	int		align,
	struct proc 	*p)
{
	register struct as *as = p->p_as;
	register caddr_t addr;
	caddr_t base;
	u_int slen;

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
	len += 2 * PAGESIZE;

	if (vac && align)
		len += shm_alignment;

	/*
	 * Look for a large enough hole starting below the stack limit.
	 * After finding it, use the upper part.  Addition of PAGESIZE is
	 * for the redzone as described above.
	 */
	if (as_gap(as, len, &base, &slen, AH_HI, (caddr_t)NULL) == 0) {
		addr = base + slen - len + PAGESIZE;

		if (vac && align) {
			caddr_t as_addr = addr;

			/*
			 * Round address DOWN to shm_alignment,
			 * add the offset, and if this address is less
			 * than the original address, add shm_alignment.
			 */
			addr = (caddr_t)((u_int)addr & (~(shm_alignment - 1)));
			addr += off & (shm_alignment - 1);
			if (addr < as_addr)
				addr += shm_alignment;

			ASSERT(addr <= (as_addr + shm_alignment));
			ASSERT(((u_int)addr & (shm_alignment - 1)) ==
				((u_int)off & (shm_alignment - 1)));
		}
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
int
valid_va_range(
	register caddr_t	*basep,
	register u_int		*lenp,
	register u_int		minlen,
	register int		dir)
{
	register caddr_t hi, lo;

#ifdef MMU_3LEVEL
	/* three level mmu has no hole */
	if (mmu_3level)
		if (hi - lo < minlen)
			return (0);
		else
			return (1);
#endif

	lo = *basep;
	hi = lo + *lenp;

	if (lo < hole_start) {
		if (hi > hole_start)
			if (hi < hole_end)
				hi = hole_start;
			else
				/* lo < hole_start && hi >= hole_end */
				if (dir == AH_LO) {
					if (hole_start - lo >= minlen)
						hi = hole_start;
					else if (hi - hole_end >= minlen)
						lo = hole_end;
					else
						return (0);
				} else {
					if (hi - hole_end >= minlen)
						lo = hole_end;
					else if (hole_start - lo >= minlen)
						hi = hole_start;
					else
						return (0);
				}
	} else {
		/* lo >= hole_start */
		if (hi < hole_end)
			return (0);
		if (lo < hole_end)
			lo = hole_end;
	}

	if (hi - lo < minlen)
		return (0);

	*basep = lo;
	*lenp = hi - lo;
	return (1);
}

/*
 * Determine whether [addr, addr+len] are valid user addresses.
 */
int
valid_usr_range(register caddr_t addr, register u_int len)
{
	register caddr_t eaddr = addr + len;

	if (eaddr <= addr || addr >= (caddr_t)USRSTACK ||
	    eaddr > (caddr_t)USRSTACK)
		return (0);

#ifdef MMU_3LEVEL
	if (mmu_3level)
		return (1);
#endif /* MMU_3LEVEL */

	/*
	 * Determine if the address range falls
	 * within the hole in a 2 level MMU.
	 */
	if ((addr >= hole_start && addr < hole_end) ||
	    (eaddr >= hole_start && eaddr < hole_end))
		return (0);

	return (1);
}

#ifdef notdef
/*
 * Move pages from one kernel virtual address to another.
 * Both addresses are assumed to reside in the Sysmap,
 * and size must be a multiple of PAGESIZE.
 */
void
pagemove(from, to, size)
	register caddr_t from, to;
	int size;
{
	register struct pte *fpte;

	if ((size % PAGESIZE) != 0)
		cmn_err(CE_PANIC, "pagemove");

	for (fpte = &Sysmap[mmu_btop((int)from - SYSBASE)]; size > 0;
	    size -= PAGESIZE, from += PAGESIZE,
	    to += PAGESIZE, fpte += CLSIZE) {
		u_int tpf;

		/*
		 * We save the old page frame info and unmap
		 * the old address "from" before we set up the
		 * new mapping to new address "to" to avoid
		 * VAC conflicts
		 */
		tpf = MAKE_PFNUM(fpte);
		segkmem_mapout(&kvseg, (caddr_t)from, PAGESIZE);
		segkmem_mapin(&kvseg, (caddr_t)to, PAGESIZE,
		    PROT_READ | PROT_WRITE, tpf, 0);
	}
}
#endif /* notdef */

#ifdef notdef
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
#endif	/* notdef */

#define	ONBPG 		2048	/* old page size */
#define	ONBSG 		32768	/* old segment size */

/*
 * Routine used to check to see if an a.out can be executed
 * by the current machine/architecture.
 */
int
chkaout(struct exdata *exp)
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

#ifdef notdef
/*
 * Return the size of the text segment.
 */
long
getts(exp)
	struct exec *exp;
{

	return ((long)clrnd(btoc(exp->a_text)));
}
#endif	/* notdef */

#ifdef notdef
/*
 * Return the size of the data segment.
 */
long
getds(exp)
	struct exec *exp;
{

	return ((long)clrnd(btoc(exp->a_data + exp->a_bss)));
}
#endif	/* notdef */

/*
 * Return the load memory address for the data segment.
 */
caddr_t
getdmem(struct exec *exp)
{
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

/*
 * Give up this page, which contains the address that caused the
 * parity error.  While we are at it, see if the parity error is permanent
 * If it is, then lock the page so that it will be never be used again (at
 * least until we reboot).
 * XXX: This routine is identical to the one in sun4/vm/vm_machdep.c;
 * they should be moved into a common file.
 */
void
page_giveup(
	caddr_t		 addr,
	struct pte	*ppte,
	struct page	*pp)
{
	int remove_page;

	ASSERT(se_assert(&pp->p_selock));

	/*
	 * Check to see if we get another parity error when we access
	 * this address again.
	 */
	remove_page = parerr_reset(addr, ppte);

	/* Remove mappings */
	hat_pageunload(pp);

	/*
	 * Destroy the association of this page with its data.
	 * If retry failed (returned -1), then tell VN_DISPOSE to not
	 * free or unlock the page to keep it from being used again.
	 * This will let the machine stay up for now, without the
	 * parity error affecting future processes.
	 */
	/* LINTED */
	VN_DISPOSE(pp, B_INVAL, (remove_page == -1), kcred);

	/* Tell what we did. */
	printf("page %x %s service.\n", ptob(ppte->pg_pfnum),
		remove_page == -1 ? "marked out of" :
			"back in");
}

void
getexinfo(
	struct exdata	*edp_in,
	struct exdata	*edp_out,
	int		*pagetext,
	int		*pagedata)
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
 * Return 1 if the page frame is onboard memory, else 0.
 */
int
pf_is_memory(u_int pf)
{
	return ((pf & PGT_MASK) == PGT_OBMEM);
}

static page_t	*page_freelist;
static page_t	*page_cachelist;
static kmutex_t	freelist_lock;
static kmutex_t	cachelist_lock;

/*
 * Just called this because all the other silly platforms us
 * this name.  Here, it just needs to set up the mutexes.
 */
void
page_coloring_init()
{
	mutex_init(&freelist_lock, "page_freelist", MUTEX_DEFAULT, NULL);
	mutex_init(&cachelist_lock, "page_cahcelist", MUTEX_DEFAULT, NULL);
}

/* ARGSUSED */
page_t *
page_get_freelist(
	struct vnode *vp,
	u_int off,
	struct as *as,
	caddr_t vaddr,
	u_int flags)
{
	page_t		*pp, *first_pp;


	mutex_enter(&freelist_lock);
	/* LINTED */
	if (pp = page_freelist) {
		ASSERT(pp->p_free);
		ASSERT(pp->p_age);
		ASSERT(pp->p_vnode == NULL);
		ASSERT(pp->p_hash == NULL);
		ASSERT(pp->p_offset == -1);
		first_pp = pp;
		/*
		 * Walk down the freelist
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
		if (pp != NULL) {
			page_sub(&page_freelist, pp);
			if ((pp->p_free == 0) ||
			    (pp->p_age == 0)) {
				cmn_err(CE_PANIC,
				    "free page is not. pp %x", pp);
			}
			mutex_exit(&freelist_lock);
			return (pp);
		}
	}
	mutex_exit(&freelist_lock);
	return (pp);
}

/* ARGSUSED */
page_t *
page_get_cachelist(
	struct vnode *vp,
	u_int off,
	struct as *as,
	caddr_t vaddr,
	u_int flags)
{
	page_t		*pp;
	page_t		*first_pp;

	mutex_enter(&cachelist_lock);
	if ((pp = page_cachelist) != NULL) {
		first_pp = pp;
		ASSERT(pp->p_vnode);
		ASSERT(pp->p_age == 0);
		while (!page_trylock(pp, SE_EXCL)) {
			pp = pp->p_next;
			if (pp == first_pp) {
				/*
				 * Searched the complete list!
				 * And all of them (might only
				 * be one) are locked. This can
				 * happen because these pages
				 * can also be found via the
				 * hash list.  When they are,
				 * they are locked first, then
				 * removed.  We give up and let
				 * the other thread run.
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
			 * found and locked a page.
			 * pull it off the list.
			 */
			ASSERT(se_excl_assert(&pp->p_selock));
			page_sub(&page_cachelist, pp);
		}
	}
	mutex_exit(&cachelist_lock);
	return (pp);
}

void
page_list_add(int list, page_t *pp, int where)
{
	kmutex_t	*lm;
	page_t		**ppp;

	ASSERT(se_excl_assert(&pp->p_selock));
	ASSERT(pp->p_free == 1);
	ASSERT(pp->p_mapping == NULL);

	if (list == PG_FREE_LIST) {
		ASSERT(pp->p_age == 1);
		ppp = &page_freelist;
		lm = &freelist_lock;
	} else {
		ASSERT(pp->p_vnode != NULL);
		ppp = &page_cachelist;
		lm = &cachelist_lock;
	}

	mutex_enter(lm);
	page_add(ppp, pp);
	if (where == PG_LIST_TAIL) {
		*ppp = (*ppp)->p_next;
	}
	mutex_exit(lm);

	/*
	 * It is up to the caller to unlock the page!
	 */
	ASSERT(se_excl_assert(&pp->p_selock));
}

void
page_list_sub(int list, page_t *pp)
{
	kmutex_t	*lm;
	page_t		**ppp;

	ASSERT(se_excl_assert(&pp->p_selock));
	ASSERT(pp->p_free == 1);

	if (list == PG_FREE_LIST) {
		ppp = &page_freelist;
		lm = &freelist_lock;
	} else {
		ppp = &page_cachelist;
		lm = &cachelist_lock;
	}

	mutex_enter(lm);
	page_sub(ppp, pp);
	mutex_exit(lm);
}
