/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)hwbcopy.c	1.8	95/01/16 SMI"

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/pte.h>
#include <sys/kmem.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <sys/sysmacros.h>

/*
 * This hwbcopy breaks in the presence of non-l3 ptes.
 */
#ifdef notdef

/* #define	BCSTAT */

#include <sys/bcopy_if.h>

extern longlong_t	xdb_cc_ssar_get(void);
extern void		xdb_cc_ssar_set(longlong_t src);
extern longlong_t	xdb_cc_sdar_get(void);
extern void		xdb_cc_sdar_set(longlong_t src);
extern u_int		disable_traps(void);
extern void		enable_traps(u_int psr_value);
extern void 		debug_enter(char *msg);

#define	SIGN_CHECK(card64)	((u_int)(card64 >> 32) & (1U << 31))

/*
 * Optional DEBUG if you're paranoid...
 */

/* #define	HWBCOPY_DEBUG */

#ifdef HWBCOPY_DEBUG
static int ldebug = 0;
static int bcbz = 0;
#define	dprint if (ldebug) printf
#endif

/*
 * (physical) block scan, fill, & copy primitives
 * these are a bit sensitive because they operate with traps disabled
 */
void
hwbc_scan(u_int blks, pa_t src)
{
	ASSERT(mxcc);

	/*
	 * Yes, these outer while() loops are necessary.
	 */
	while (blks > 0) {
		u_int psr = disable_traps();
		for (; blks > 0; blks--) {
			xdb_cc_ssar_set(src);
			src += BLOCK_SIZE;
		}
		while (SIGN_CHECK(xdb_cc_ssar_get()) == 0) { }
		enable_traps(psr);
	}
}

void
hwbc_fill(u_int blks, pa_t dest, pa_t pattern)
{
	ASSERT(mxcc);

	while (blks > 0) {
		u_int psr = disable_traps();
		xdb_cc_ssar_set(pattern);
		for (; blks > 0; blks--) {
			xdb_cc_sdar_set(dest);
			dest += BLOCK_SIZE;
		}
		while (SIGN_CHECK(xdb_cc_sdar_get()) == 0) { }
		enable_traps(psr);
	}
}

void
hwbc_copy(u_int blks, pa_t src, pa_t dest)
{
	ASSERT(mxcc);

	while (blks > 0) {
		u_int psr = disable_traps();
		for (; blks > 0; blks--) {
			xdb_cc_ssar_set(src);
			xdb_cc_sdar_set(dest);
			src += BLOCK_SIZE;
			dest += BLOCK_SIZE;
		}
		while (SIGN_CHECK(xdb_cc_ssar_get()) == 0) { }
		while (SIGN_CHECK(xdb_cc_sdar_get()) == 0) { }
		enable_traps(psr);
	}
}

static char hwbc_zero_buf[2*BLOCK_SIZE] = { 0, };
static pa_t hwbc_zeroes = 0;

/*
 * physical page/block - scan, fill, zero, & copy primitives
 */
void
hwpage_scan(u_int pfn)
{
	u_int blks = BLOCKS_PER_PAGE;
	pa_t src = PFN_ENLARGE(pfn);

	hwbc_scan(blks, src);
}

void
hwpage_zero(u_int pfn)
{
	u_int blks = BLOCKS_PER_PAGE;
	pa_t dest = PFN_ENLARGE(pfn);
	pa_t pattern = hwbc_zeroes;

	hwbc_fill(blks, dest, pattern);
}

void
hwpage_fill(u_int pfn, pa_t pattern)
{
	u_int blks = BLOCKS_PER_PAGE;
	pa_t dest = PFN_ENLARGE(pfn);

	hwbc_fill(blks, dest, pattern);
}

void
hwpage_copy(u_int spfn, u_int dpfn)
{
	u_int blks = BLOCKS_PER_PAGE;
	pa_t src = PFN_ENLARGE(spfn);
	pa_t dest = PFN_ENLARGE(dpfn);

	hwbc_copy(blks, src, dest);
}

void
hwblk_zero(u_int blks, pa_t dest)
{
	hwbc_fill(blks, dest, (pa_t) hwbc_zeroes);
}


/*
 * initialize 'hwbc_zeroes' to a hardware block address for 'hwbc_zero_buf',
 * has to deal with alignment issues; also one-time check some invariants.
 */
void
hwbc_init(void)
{
	u_int mask = ~(BLOCK_SIZE - 1);	/* mask for blk alignment */
	char *base = (char *)((int) &hwbc_zero_buf[BLOCK_SIZE] & mask);
	u_int residue = (u_int) base & MMU_STD_FIRSTMASK;
	u_int hwbc_page;

	if (mxcc == 0) {
		defhwbcopy = hwbcopy = 0;
		cmn_err(CE_CONT,
			"?hwbc_init: not enabled on non-mxcc machine.\n");
		return;
	}

	ASSERT(MMU_STD_FIRSTSHIFT == 12);
	ASSERT(MMU_STD_FIRSTSHIFT == PAGESHIFT);
	ASSERT(MMU_STD_FIRSTMASK == ((1 << MMU_STD_FIRSTSHIFT) - 1));
	ASSERT(sizeof (pa_t) == 8);

	hwbc_page = hat_getkpfnum(base);
	hwbc_zeroes = (1ULL << BC_CACHE_SHIFT);	/* ssar&sdar Cacheable bit */
	hwbc_zeroes |= residue;
	hwbc_zeroes |= PFN_ENLARGE(hwbc_page);


#ifdef BCSTAT
	mutex_init(&bcstat.lock, "bcstat_lock", MUTEX_DEFAULT, DEFAULT_WT);
#endif
	ASSERT(hwbcopy == 0);	/* We shouldn't be using hwbcopy */
				/* until now */
	hwbcopy = defhwbcopy;	/* now turn it on */

#ifdef	HWBCOPY_DEBUG
	{
		static void hwbc_selftest(void);
		hwbc_selftest();
	}
#endif	/* HWBCOPY_DEBUG */
}


/* ---------------------------------------------------------------------- */

/*
 * For now, for sun4m, the hw bcopy set of routines are prefixed with hw_
 * so that they are not used by default.  In the future, when we have time
 * to analyze the performance and gain a better understanding of hw bcopy
 * performance, we can either make these routines the default or selectively
 * call them.
 */

/*
 * Minimum threshold of BLOCK_SIZE byte transfers before engaging hwbcopy.
 * set hwbcopy to 0 to disable hwbcopy.  Likely trys are 4, 8, 32, etc...
 * I don't suggest setting it to < 3.  hwbcopy must be 0 until
 * hwbc_init() is called.
 */
int	defhwbcopy =   	1;	/* default for now is on */
int	hwbcopy =	0;	/* This value must be 0 until */
				/* hwbc_init() is called */
#ifdef BCSTAT
bcopy_stat_t bcstat;
int bcstaton = 0x0;
#endif


/*
 * ----------------------------------------------------------------------
 *
 * bcopy() divides the area to copy into 3 logical address sections:
 *
 * 1. "from" up to beginning of a BLOCK_SIZE boundary.
 *    The size of this region is >= 0 && < BLOCK_SIZE.
 * 2. 1 or more BLOCK_SIZE blocks - blks;
 * 3. Remaining bytes up to "from" + "count".  Again, the size of this region
 *    is >= 0 && < BLOCK_SIZE.
 *
 * "from" side:					"to" side:
 * ---- from					---- to
 * .						.
 * .						.
 * ----		first BLOCK_SIZE byte boundary	----
 * .						.
 * .						.
 * .						.
 * .						.
 * .						.
 * ----		end of last BLOCK_SIZE byte boundary	----
 * .						.
 * .						.
 * ---- end					---- end
 *
 *
 * ---------------------------------------------------------------------- */

void
hw_bcopy(caddr_t from, caddr_t to, size_t count)
{
	register int resid;

	if (!mxcc || hwbcopy <= 0)
		goto swcopy;

	BC_STAT_SMALLCALLER(count);

	if (count < (hwbcopy << BLOCK_SIZE_SHIFT))
		goto swcopy;

	/*
	 * "from" and "to" must be aligned on BLOCK_SIZE byte boundaries
	 */
	if (((u_int)from ^ (u_int)to) & BLOCK_MASK)
		goto swcopy;

	if (resid = ((u_int)from & BLOCK_MASK)) {	/* copy part 1 */
		resid = BLOCK_SIZE - resid;
		count -= resid;
		SWBCOPY(from, to, resid);
		from += resid;
		to += resid;
	}

	/*
	 * Copy section 2
	 */
	resid = count & ~BLOCK_MASK;	/* bytes to copy in part 2 */
	count &= BLOCK_MASK;		/* calculate remainder for part 3 */

	while (resid > 0) {
		register int bcnt;
		pa_t phys_saddr, phys_daddr;

		va_to_pa64(from, &phys_saddr);
		if (phys_saddr == (pa_t) -1) {
			/*
			 * Oh well, we're not mapped.  Sorry.
			 */
			count += resid;
			goto swcopy;
		}

		va_to_pa64(to, &phys_daddr);
		if (phys_daddr == (pa_t) -1) {
			count += resid;
			goto swcopy;
		}

		bcnt = MIN((PAGESIZE - ((u_int) from & PAGEOFFSET)),
				(PAGESIZE - ((u_int) to & PAGEOFFSET)));
		if (resid < bcnt)
			bcnt = resid;
		/*
		 * Mark this page as Referenced and Modified and
		 * call the HW routine
		 */
		*to = *from;
		hwbc_copy((bcnt >> BLOCK_SIZE_SHIFT), phys_saddr, phys_daddr);
		*to = *from;

		from += bcnt;
		to += bcnt;
		resid -= bcnt;
	}

	if (count)
swcopy:
		SWBCOPY(from, to, count);	/* copy part 3 */
}


void
hw_bzero(caddr_t addr, size_t count)
{
	register int resid;

	if (!mxcc || hwbcopy <= 0)
		goto swzero;

	BC_STAT_SMALLCALLER(count);

	if (count < (hwbcopy << BLOCK_SIZE_SHIFT))
		goto swzero;

	if (resid = ((u_int)addr & BLOCK_MASK)) {	/* zero part 1 */
		resid = BLOCK_SIZE - resid;
		count -= resid;
		SWBZERO(addr, resid);
		addr += resid;
	}

	resid = count & ~BLOCK_MASK;	/* bytes to zero in part 2 */
	count &= BLOCK_MASK;		/* calculate remainder for part 3 */

	/*
	 * Section 2
	 */
	while (resid > 0) {
		register int bcnt;
		register caddr_t nxp;
		pa_t phys_addr;

		va_to_pa64(addr, &phys_addr);
		if (phys_addr == (pa_t) -1) {
			count += resid;
			goto swzero;
		}

		nxp = BC_ROUNDUP2(addr, PAGESIZE);
		bcnt = MIN(resid, (nxp - addr));
		/*
		 * Mark this page as Referenced and Modified and call
		 * the HW routine;
		 */
		*addr = 0;
		hwbc_fill((bcnt >> BLOCK_SIZE_SHIFT), phys_addr,
				(pa_t) hwbc_zeroes);
		*addr = 0;

		addr += bcnt;
		resid -= bcnt;
	}

	if (count)
swzero:
		SWBZERO(addr, count);
}

/*
 * Copy a block of storage, returning an error code if `from' or
 * `to' takes a kernel pagefault which cannot be resolved.
 * Returns errno value on pagefault error, 0 if all ok
 *
 * int
 * kcopy(from, to, count)
 *	caddr_t from, to;
 *	size_t count;
 */

int
hw_kcopy(caddr_t from, caddr_t to, size_t count)
{
	label_t ljb;

	if (!mxcc || hwbcopy <= 0)
dosoftw:	return (SWKCOPY(from, to, count));

	if (count < (hwbcopy << BLOCK_SIZE_SHIFT))
		goto dosoftw;

	if (((u_int)from ^ (u_int)to) & BLOCK_MASK)
		goto dosoftw;

	if (on_fault(&ljb))
		return (EFAULT);
	hw_bcopy(from, to, count);
	no_fault();
	return (0);
}

int
hw_copyin(caddr_t uaddr, caddr_t kaddr, size_t count)
{
	label_t ljb;

	if (!mxcc || hwbcopy <= 0)
dosoftw:	return (SWCOPYIN(uaddr, kaddr, count));

	if (count < (hwbcopy << BLOCK_SIZE_SHIFT))
		goto dosoftw;

	if (((u_int)uaddr ^ (u_int)kaddr) & BLOCK_MASK)
		goto dosoftw;

	if ((u_int)uaddr >= KERNELBASE)
		return (-1);

	if (on_fault(&ljb))
		return (-1);
	hw_bcopy(uaddr, kaddr, count);
	no_fault();
	return (0);
}

int
hw_copyout(caddr_t kaddr, caddr_t uaddr, size_t count)
{
	label_t ljb;

	if (!mxcc || hwbcopy <= 0)
dosoftw:	return (SWCOPYOUT(kaddr, uaddr, count));

	if (count < (hwbcopy << BLOCK_SIZE_SHIFT))
		goto dosoftw;

	if (((u_int)kaddr ^ (u_int)uaddr) & BLOCK_MASK)
		goto dosoftw;

	if ((u_int)uaddr >= KERNELBASE)
		return (-1);

	if (on_fault(&ljb))
		return (-1);
	hw_bcopy(kaddr, uaddr, count);
	no_fault();
	return (0);
}

int
hw_xcopyin(caddr_t uaddr, caddr_t kaddr, size_t count)
{
	label_t ljb;

	if (!mxcc || hwbcopy <= 0)
dosoftw:	return (SWXCOPYIN(uaddr, kaddr, count));

	if (count < (hwbcopy << BLOCK_SIZE_SHIFT))
		goto dosoftw;

	if (((u_int)uaddr ^ (u_int)kaddr) & BLOCK_MASK)
		goto dosoftw;

	if ((u_int)uaddr >= KERNELBASE)
		return (EFAULT);

	if (on_fault(&ljb))
		return (EFAULT);
	hw_bcopy(uaddr, kaddr, count);
	no_fault();
	return (0);
}


int
hw_xcopyout(caddr_t kaddr, caddr_t uaddr, size_t count)
{
	label_t ljb;

	if (!mxcc || hwbcopy <= 0)
dosoftw:	return (SWXCOPYOUT(kaddr, uaddr, count));

	if (count < (hwbcopy << BLOCK_SIZE_SHIFT))
		goto dosoftw;

	if (((u_int)kaddr ^ (u_int)uaddr) & BLOCK_MASK)
		goto dosoftw;

	if ((u_int)uaddr >= KERNELBASE)
		return (EFAULT);

	if (on_fault(&ljb))
		return (EFAULT);
	hw_bcopy(kaddr, uaddr, count);
	no_fault();
	return (0);
}


#ifdef BCSTAT
void
insertcaller(callerl_t *clist, caddr_t callerp)
{
	u_int ndx = (u_int) callerp % CALLERLSIZE;

	if (clist[ndx].caller == NULL)
		clist[ndx].caller = callerp;
	else if (clist[ndx].caller != callerp) {
		for (ndx = 0; ndx < CALLERLSIZE; ndx++) {
			if (clist[ndx].caller == NULL) {
				clist[ndx].caller = callerp;
				break;
			}
			if (clist[ndx].caller == callerp)
				break;
		}
	}
	if (ndx == CALLERLSIZE)
		return;			/* no room for this entry */
	clist[ndx].count++;
}



void
bc_statsize(histo_t *histop, int count)
{
	int ndx = count / PAGESIZE;
	if (ndx > 3)
		ndx = 3;
	mutex_enter(&bcstat.lock);
	histop->bucket[ndx]++;
	if (count < PAGESIZE) {
		ndx = count / BLOCK_SIZE;
		if (ndx > 15)
			ndx = 15;
		histop->sbucket[ndx]++;
	}
	mutex_exit(&bcstat.lock);
}

void
bc_stattype(caddr_t src, caddr_t dest, int count, int destonly)
{
	union ptpe ptpe;

	if (srmmu_xlate(dest, NULL, &ptpe, NULL))
	    bc_statsize(ptpe.pte.Cacheable ? &bcstat.dest_mem : &bcstat.dest_io,
		count);

	if (destonly)	/* for bzero */
		return;
	if (srmmu_xlate(src, NULL, &ptpe, NULL))
	    bc_statsize(ptpe.pte.Cacheable ? &bcstat.src_mem : &bcstat.src_io,
		count);
}

#endif /* BCSTAT */


#ifdef HWBCOPY_DEBUG


#include <sys/kmem.h>

static u_int
hwbc_mem_pfn(caddr_t p)
{
	u_int cacheable = (1 << (BC_CACHE_SHIFT - MMU_STD_FIRSTSHIFT));
	u_int pfn = cacheable | hat_getkpfnum(p);
	/* assume the page is locked down! */
	return (pfn);
}

static void
hwbc_selftest(void)
{
	u_int one_page = BLOCKS_PER_PAGE * BLOCK_SIZE;
	caddr_t chunk = kmem_alloc(3 * one_page, KM_NOSLEEP);
	caddr_t from_buf = (caddr_t)
		(((u_int) chunk + one_page) & ~MMU_STD_FIRSTMASK);
	caddr_t to_buf = from_buf + one_page;
	u_int spfn = hwbc_mem_pfn(from_buf);
	u_int dpfn = hwbc_mem_pfn(to_buf);

	int i;

	for (i = 0; i < one_page; i++) {
		from_buf[i] = (i & 0xff);
	}

	cmn_err(CE_CONT, "?page_copy self-test: ");
	hwpage_copy(spfn, dpfn);
	cmn_err(CE_CONT, "?verifying... ");

	for (i = 0; i < one_page; i++) {
		u_char byte = to_buf[i];
		if (byte != (i & 0xff)) {
			debug_enter("page_copy bug?");
		}
	}

	cmn_err(CE_CONT, "?done\n");

	cmn_err(CE_CONT, "?page_zero self-test: ");
	hwpage_zero(dpfn);
	cmn_err(CE_CONT, "?verifying... ");

	for (i = 0; i < one_page; i++) {
		u_char byte = to_buf[i];
		if (byte != 0) {
			debug_enter("page_zero bug?");
		}
	}

	cmn_err(CE_CONT, "?done\n");

	cmn_err(CE_CONT, "?page_scan self-test: ");
	hwpage_scan(spfn);
	cmn_err(CE_CONT, "?done\n");

	kmem_free(chunk, 3 * one_page);
}


check_copy(caddr_t from, caddr_t to, int count, int lineno)
{
	caddr_t f, t;
	int c;
	int err = 0;

	for (f = from, t = to, c = count; c > 0; f++, t++, c--) {
		if (*f != *t) {
			if (++err < 20)
				dprint(
			"bcopy(%x, %x, %d): failure at %x -> %x diff %d\n",
				from, to, count, f, t, f - from);
		}
	}
	if (err)
		dprint("bcopy: %d total errors at line %d\n", err, lineno);
	if (err && ldebug > 1)
		debug_enter("stopping");
	return (err);
}

check_zero(caddr_t addr, int count, int lineno)
{
	caddr_t a;
	int c;
	int err = 0;

	for (a = addr, c = count; c > 0; a++, c--) {
		if (*a != 0) {
			if (++err < 20)
				dprint("bzero(%x, %d): failure at %x diff %d\n",
				addr, count, a, a - addr);
		}
	}
	if (err)
		dprint("bzero: %d total errors at line %d\n", err, lineno);
	if (err && ldebug > 1)
		debug_enter("bzero");
	return (err);
}
#endif	/* HWBCOPY_DEBUG */

#endif	/* notdef */
