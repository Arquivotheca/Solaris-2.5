/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)hat_refmod.c	1.19	94/11/09 SMI"

/*
 * The following routines implement the hat layer's
 * recording of the referenced and modified bits.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/debug.h>
#include <sys/kmem.h>

/*
 * Note, usage of cmn_err requires you not hold any hat layer locks.
 */
#include <sys/cmn_err.h>

#include <vm/as.h>
#include <vm/hat.h>

static kmutex_t	hat_statlock;	/* protects all hat statistics data */
struct hrmstat *hrm_memlist;	/* tracks memory alloced for hrm_blist blocks */
struct hrmstat **hrm_hashtab;	/* hash table for finding blocks quickly */
struct hrmstat *hrm_blist;
int hrm_blist_incr = HRM_BLIST_INCR;
int hrm_blist_lowater = HRM_BLIST_INCR/2;
int hrm_blist_num = 0;
int hrm_blist_total = 0;
int hrm_mlockinited = 0;
int hrm_allocfailmsg = 0;	/* print a message when allocations fail */
int hrm_allocfail = 0;
int hrm_sws;			/* swsmon is active */

static struct hrmstat	*hrm_balloc(void);
static int	hrm_init(void);
static void	hrm_link(struct hrmstat *);
static void	hrm_setbits(struct hrmstat *, caddr_t, u_int);
static void	hrm_copybits(struct hrmstat *, caddr_t, int, caddr_t, int);
static void	hrm_hashout(struct hrmstat *);
static int	hrm_getbits(struct hrmstat *, caddr_t, int);

#define	hrm_hash(as, addr) \
	(HRM_HASHMASK & \
	(((u_int)(addr) >> HRM_BASESHIFT) ^ ((u_int)(as) >> 2)))

#define	hrm_match(hrm, as, addr) \
	(((hrm)->hrm_as == (as) && \
	((hrm)->hrm_base == ((u_int)(addr) & HRM_BASEMASK))) ? 1 : 0)

/*
 * Start the statistics gathering for an address space.
 * Return -1 if we can't do it, otherwise return an opaque
 * identifier to be used when querying for the gathered statistics.
 * The identifier is an unused bit in a_vbits.
 * Bit 0 is reserved for swsmon.
 */
int
hat_startstat(as)
	struct as *as;
{
	register u_int nbits;		/* number of bits */
	register u_int bn;		/* bit number */
	register u_int id;		/* new vbit, identifier */
	register u_int vbits;		/* used vbits of address space */

	/*
	 * Initialize global data, if needed.
	 */
	if (hrm_init() == -1)
		return (-1);

	/*
	 * If the refmod saving memory allocator runs out, print
	 * a warning message about how to fix it, see comment at
	 * the beginning of hat_setstat.
	 */
	if (hrm_allocfailmsg) {
		cmn_err(CE_WARN,
		    "hrm_balloc failures occured, increase hrm_blist_incr");
		hrm_allocfailmsg = 0;
	}

	/*
	 * Verify that a buffer of statistics blocks exists
	 * and allocate more, if needed.
	 */
	hrm_getblk(0, as);

	/*
	 * Find a unused id in the given address space.
	 */
	hat_enter(as);
	vbits = as->a_vbits;
	nbits = sizeof (as->a_vbits) * NBBY;
	for (bn = 1, id = 2; bn < (nbits - 1); bn++, id <<= 1)
		if ((id & vbits) == 0)
			break;
	if (bn >= (nbits - 1)) {
		hat_exit(as);
		return (-1);
	}
	as->a_vbits |= id;
	hat_exit(as);
	return (id);
}

/*
 * Record referenced and modified information for an address space.
 * Rmbits is a word containing the referenced bit in bit position 1
 * and the modified bit in bit position 0.
 * Returns 0 for success, 1 for failure.  Failures only happen
 * when there are no more blocks to hold statistics bits.
 * Failure in this case is not deemed unacceptable.
 * For current informational uses, one can rerun any program using
 * this facility after modifying the hrm_blist_incr to be a larger
 * amount so that a larger buffer of blocks will be maintained.
 */
int
hat_setstat(as, addr, rmbits)
	struct as	*as;
	caddr_t		addr;
	u_int		rmbits;
{
	register struct hrmstat	*hrm;
	u_int		vbits, newbits, nb;
	int		h;

	if (!rmbits)
		return (0);

	/*
	 * Initialize global data, if needed.
	 */
	if (hrm_init() == -1)
		return (-1);

	/*
	 * Verify that a buffer of statistics blocks exists
	 * and allocate more, if needed.
	 */
	hrm_getblk(0, as);

	mutex_enter(&hat_statlock);

	/*
	 * Search the hash list for the as and addr we are looking for
	 * and set the ref and mod bits in every block that matches.
	 */
	vbits = 0;
	h = hrm_hash(as, addr);
	for (hrm = hrm_hashtab[h]; hrm; hrm = hrm->hrm_hnext) {
		if (hrm_match(hrm, as, addr)) {
			hrm_setbits(hrm, addr, rmbits);
			vbits |= hrm->hrm_id;
		}
	}

	/*
	 * If we didn't find a block for all of the enabled
	 * vpages bits, then allocate and initialize a block
	 * for each bit that was not found.
	 */
	if (vbits != as->a_vbits) {
		newbits = vbits ^ as->a_vbits;
		while (newbits) {
			if (ffs(newbits))
				nb = 1 << (ffs(newbits)-1);
			hrm = (struct hrmstat *)hrm_balloc();
			if (hrm == NULL) {
				hrm_allocfailmsg = 1;
				hrm_allocfail++;
				mutex_exit(&hat_statlock);
				return (1);
			}
			hrm->hrm_as = as;
			hrm->hrm_base = (u_int)addr & HRM_BASEMASK;
			hrm->hrm_id = nb;
			hrm_link(hrm);
			hrm_setbits(hrm, addr, rmbits);
			newbits &= ~nb;
		}
	}
	mutex_exit(&hat_statlock);
	return (0);
}

#ifdef notdef

/*
 * the following code is essentially the same as hat_getstatby,
 * it was used by the program swsmon, but swsmon is no
 * longer supported, thus it has been ifdef'd out.
 */

/*
 * Return collected statistics about an address space.
 * If clearflag is set, atomically read and zero the bits.
 *
 * Fill in the data array supplied with the referenced and
 * modified bits collected for address range [addr ... addr + len]
 * in address space, as, uniquely identified by id.
 * Kernel only interface, can't fault on destination data array.
 *
 * The most efficient way to use this routine is to pass a data
 * array, address and length that is the same size as the statistics
 * block being or modulo that size, second best is to use address
 * argument address is rounded down to a four page boundary; and the
 * length rounded up to next a four page boundary.  This allows a quick
 * bcopy of the data array since a character holds four pages work of ref
 * and mod bits. Any address and length should work, it just will take more
 * cycles to massage the bits.
 *
 */
void
hat_getstat(as, addr, len, id, datap, clearflag)
	struct as	*as;
	caddr_t		addr;
	u_int		len;
	u_int		id;
	caddr_t		datap;
	int		clearflag;
{
	struct hrmstat	*hrm;
	int		h;			/* hash index */
	int		np;			/* number of pages */
	int		n;			/* number of pages, temp */
	int		doff;			/* offset into datap */
	caddr_t		a;

	hat_sync(as, addr, len, clearflag);

	/* allocate more statistics blocks if needed */
	hrm_getblk(0, as);

	if (datap == NULL)
		return;

	np = btop(len);
	n = (np * 2) / NBBY;
	n += ((np * 2) % NBBY) ? 1 : 0;
	bzero(datap, n);

	mutex_enter(&hat_statlock);
	if (hrm_hashtab == NULL) {
		/* can happen when victim process exits */
		mutex_exit(&hat_statlock);
		return;
	}
	/*
	 * Do a good quick transfer of information for requesters
	 * passing aligned buffers and addresses, otherwise just
	 * do something that works.
	 */
	if ((((u_int)addr & ~HRM_BASEMASK) == 0) &&
	    ((n & (HRM_BYTES-1)) == 0)) {		/* fully aligned */

		doff = 0;
		for (a = addr;
		    (np > 0) && (a < addr + len);
		    a += HRM_PAGES * MMU_PAGESIZE) {

			h = hrm_hash(as, a);
			for (hrm = hrm_hashtab[h]; hrm; hrm = hrm->hrm_hnext) {
				if (hrm->hrm_as == as &&
				    hrm->hrm_base == (u_int)a &&
				    id == hrm->hrm_id) {
					hrm_copybits(hrm, a,
					    (np >= HRM_PAGES ? HRM_PAGES : np),
					    &datap[doff], clearflag);
					break;
				}
			}
			doff += HRM_BYTES;
			np -= HRM_PAGES;
		}

	} else if ((((int)addr & HRM_PGOFFMASK) == 0) &&
	    ((np & HRM_PGBYTEMASK) == 0)) {		/* partially aligned */

		doff = 0;
		a = (caddr_t)((int)addr & MMU_PAGEMASK);
		while (np > 0 && a < addr + len) {
			int nb;		/* number of bytes */

			h = hrm_hash(as, a);
			n = (HRM_PAGES - (((u_int)a & HRM_PAGEMASK) >>
				MMU_PAGESHIFT));
			if (n > np)
				n = np;
			ASSERT((n % HRM_PGPERBYTE) == 0);
			nb = n/HRM_PGPERBYTE;
			for (hrm = hrm_hashtab[h]; hrm; hrm = hrm->hrm_hnext) {
				if (hrm->hrm_as == as &&
				    hrm->hrm_base ==
					((u_int)a & HRM_BASEMASK) &&
				    id == hrm->hrm_id) {
					hrm_copybits(hrm, a, n,
					    &datap[doff], clearflag);
					break;
				}
			}

			doff += nb;
			np -= n;
			a += n * MMU_PAGESIZE;
		}

	} else {					/* grunt */

		register u_int bo;	/* byte offset */
		register int bp, spb;	/* bits position, shift */
		register int bits, i;

		bp = 0;
		a = (caddr_t)((int)addr & MMU_PAGEMASK);
		while (np > 0 && a < addr + len) {

			h = hrm_hash(as, a);
			n = (HRM_PAGES - (((u_int)a & HRM_PAGEMASK) >>
				MMU_PAGESHIFT));
			if (n > np)
				n = np;

			for (hrm = hrm_hashtab[h]; hrm; hrm = hrm->hrm_hnext) {
				if (hrm->hrm_as == as &&
				    hrm->hrm_base ==
					((u_int)a & HRM_BASEMASK) &&
				    id == hrm->hrm_id) {

					for (i = 0; i < n; i++) {
						bits = hrm_getbits(hrm,
							a + (i * MMU_PAGESIZE),
							clearflag);
						bo = bp / HRM_PGPERBYTE;
						spb = (3 - (bp & 3)) * 2;
						datap[bo] |= (bits << spb);
						bp++;
					}

					break;
				}
			}
			if (hrm == NULL)
				bp += n;
			np -= n;
			a += n * MMU_PAGESIZE;
		}
	}
	mutex_exit(&hat_statlock);
}

#endif / notdef */

/*
 * Free the resources used to maintain the referenced and modified
 * statistics for the virtual page view of an address space
 * identified by id.
 */
void
hat_freestat(as, id)
	struct as *as;
	int id;
{
	struct hrmstat *hrm, *prev_ahrm;

	hat_enter(as);
	if (id == NULL)
		as->a_vbits = 0;
	else
		as->a_vbits &= ~id;

	if (id == HRM_SWSMONID)
		hrm_sws = 0;

	if ((hrm = as->a_hrm) == NULL) {
		hat_exit(as);
		return;
	}
	hat_exit(as);

	mutex_enter(&hat_statlock);
	if (hrm_hashtab == NULL) {
		/* can't happen? */
		mutex_exit(&hat_statlock);
		return;
	}
	for (prev_ahrm = NULL; hrm; hrm = hrm->hrm_anext) {
		if ((id == hrm->hrm_id) || (id == NULL)) {

			hrm_hashout(hrm);
			hrm->hrm_hnext = hrm_blist;
			hrm_blist = hrm;
			hrm_blist_num++;

			if (prev_ahrm == NULL)
				as->a_hrm = hrm->hrm_anext;
			else
				prev_ahrm->hrm_anext = hrm->hrm_anext;

		} else
			prev_ahrm = hrm;
	}

	/*
	 * If all statistics blocks are free,
	 * return the memory to the system.
	 */
	if ((hrm_blist_num == hrm_blist_total) && !hrm_sws) {
		/* zero the block list since we are giving back its memory */
		hrm_blist = NULL;
		hrm_blist_num = 0;
		hrm_blist_total = 0;
		while (hrm_memlist) {
			hrm = hrm_memlist;
			hrm_memlist = hrm->hrm_hnext;
			kmem_free(hrm, hrm->hrm_base);
		}
		ASSERT(hrm_memlist == NULL);
		kmem_free(hrm_hashtab, HRM_HASHSIZE * sizeof (char *));
		hrm_hashtab = NULL;
	}
	mutex_exit(&hat_statlock);
}

/*
 * Initialize any global state for the statistics handling.
 * Hrm_lock protects the globally allocted memory:
 *	hrm_memlist and hrm_hashtab.
 */
int
hrm_init()
{
	/*
	 * Alloacte the hashtable if it doesn't exist yet.
	 */
	mutex_enter(&hat_statlock);
	if (hrm_hashtab == NULL) {
		hrm_hashtab = (struct hrmstat **)
			kmem_zalloc(HRM_HASHSIZE * sizeof (char *), 0);
		if (hrm_hashtab == NULL) {
			cmn_err(CE_WARN, "Can't allocate hrm hash table\n");
			mutex_exit(&hat_statlock);
			return (-1);
		}
	}
	mutex_exit(&hat_statlock);
	return (0);
}

/*
 * Grab memory for statistics gathering of the hat layer.
 * Not a static routine because we allow swsmon to bump
 * up the buffer of statistics blocks.
 */
/* ARGSUSED */
void
hrm_getblk(chunk, as)
	int chunk;
	struct as *as;
{
	struct hrmstat *hrm, *l;
	register int i;

	mutex_enter(&hat_statlock);
	if ((hrm_blist == NULL) ||
	    (hrm_blist_num <= hrm_blist_lowater) ||
	    chunk) {
		mutex_exit(&hat_statlock);

		if (chunk) {
			hrm_sws = 1;
			i = chunk;
		} else
			i = hrm_blist_incr;

		hrm = (struct hrmstat *)kmem_zalloc(
				sizeof (struct hrmstat) * i, 0);
		/*
		 * If the allocation fails give up silently,
		 * we do not actually need the blocks immediately.
		 */
		if (hrm == NULL)
			return;
		hrm->hrm_base = sizeof (struct hrmstat) * i;

		/*
		 * thread the allocated blocks onto a freelist
		 * using the first block to hold information for
		 * freeing them all later
		 */
		mutex_enter(&hat_statlock);
		hrm->hrm_hnext = hrm_memlist;
		hrm_memlist = hrm;

		hrm_blist_total += (hrm_blist_incr - 1);
		for (i = 1; i < hrm_blist_incr; i++) {
			l = &hrm[i];
			l->hrm_hnext = hrm_blist;
			hrm_blist = l;
			hrm_blist_num++;
		}
	}
	mutex_exit(&hat_statlock);
}

static void
hrm_hashin(hrm)
	register struct hrmstat	*hrm;
{
	struct hrmstat	*list;
	int 		h;

	ASSERT(MUTEX_HELD(&hat_statlock));
	h = hrm_hash(hrm->hrm_as, hrm->hrm_base);
	list = hrm_hashtab[h];

	if (list == (struct hrmstat *)NULL) {
		hrm_hashtab[h] = hrm;
		hrm->hrm_hnext = NULL;
	} else {
		hrm->hrm_hnext = list;
		hrm_hashtab[h] = hrm;
	}
}


static void
hrm_hashout(hrm)
	struct hrmstat	*hrm;
{
	struct hrmstat	*list, **prev_hrm;
	int		h;

	ASSERT(MUTEX_HELD(&hat_statlock));
	h = hrm_hash(hrm->hrm_as, hrm->hrm_base);
	list = hrm_hashtab[h];
	prev_hrm = (struct hrmstat **)&hrm_hashtab[h];

	while (list) {
		if (list == hrm) {
			*prev_hrm = list->hrm_hnext;
			return;
		}
		prev_hrm = &list->hrm_hnext;
		list = list->hrm_hnext;
	}
}



/*
 * Link a statistic block into an address space and also put it
 * on the hash list for future references.
 */
static void
hrm_link(hrm)
	register struct hrmstat	*hrm;
{
	register struct as *as = hrm->hrm_as;

	ASSERT(MUTEX_HELD(&hat_statlock));
	hrm->hrm_anext = as->a_hrm;
	as->a_hrm = hrm;
	hrm_hashin(hrm);
}

/*
 * Allocate a block for statistics keeping.
 * Returns NULL if blocks are unavailable.
 */
static struct hrmstat *
hrm_balloc()
{
	register struct hrmstat *hrm;

	ASSERT(MUTEX_HELD(&hat_statlock));

	hrm = hrm_blist;
	if (hrm != NULL) {
		hrm_blist = hrm->hrm_hnext;
		hrm_blist_num--;
		hrm->hrm_hnext = NULL;
	}
	return (hrm);
}

/*
 * Set the ref and mod bits for addr within statistics block hrm.
 */
static void
hrm_setbits(hrm, addr, bits)
	struct hrmstat *hrm;
	caddr_t addr;
	u_int bits;
{
	register u_int po, bo, spb;
	register u_int nbits;

	po = ((u_int)addr & HRM_BASEOFFSET) >> MMU_PAGESHIFT; 	/* pg offset */
	bo = po / (NBBY / 2);			/* which byte in bit array */
	spb = (3 - (po & 3)) * 2;		/* shift position within byte */
	nbits = bits << spb;			/* bit mask */
	hrm->hrm_bits[bo] |= nbits;
}

/*
 * Get the ref and mod bits for addr within statistics block hrm;
 * used for unaligned requests.
 */
static int
hrm_getbits(hrm, addr, clearflag)
	struct hrmstat *hrm;
	caddr_t addr;
	int clearflag;
{
	register u_int po, bo, spb;
	register u_int bits;

	po = ((u_int)addr & HRM_BASEOFFSET) >> MMU_PAGESHIFT; 	/* pg offset */
	bo = po / (NBBY / 2);			/* which byte in bit array */
	spb = (3 - (po & 3)) * 2;		/* shift position within byte */
	bits = (hrm->hrm_bits[bo] >> spb) & 3;
	if (clearflag)
		hrm->hrm_bits[bo] &= ~(3 << spb);
	return (bits);
}

/*
 * Copy ref and mod bits from a statistics block.
 * 	hrm is the statistics block to read from
 * 	addr is used in figuring the offset, and is rounded to a page boundary
 *	len is the len in pages
 *	p is pointer to the destination buffer
 *	clearflag indicates whether or not to clear the bits after copying
 */
static void
hrm_copybits(hrm, addr, len, p, clearflag)
	struct hrmstat	*hrm;
	caddr_t		addr;
	int 		len;
	caddr_t		p;
	int		clearflag;
{
	register u_int bo;

	if ((hrm->hrm_base == (u_int)addr) &&
	    (len == HRM_PAGES)) {
		bcopy((caddr_t)&hrm->hrm_bits[0], p, HRM_BYTES);
		if (clearflag)
			bzero((caddr_t)&hrm->hrm_bits[0], (size_t)HRM_BYTES);
	} else if ((((u_int)addr & HRM_PGOFFMASK) == 0) &&
			((len & HRM_PGBYTEMASK) == 0)) {
		bo = btop(addr - hrm->hrm_base)/HRM_PGPERBYTE;
		bcopy((caddr_t)&hrm->hrm_bits[bo], p, HRM_PGPERBYTE);
		if (clearflag)
			bzero((caddr_t)&hrm->hrm_bits[bo], HRM_PGPERBYTE);
	} else
		printf("copybits called with unaligned operands\n");
}

/*
 * Return collected statistics about an address space.
 * If clearflag is set, atomically read and zero the bits.
 *
 * Fill in the data array supplied with the referenced and
 * modified bits collected for address range [addr ... addr + len]
 * in address space, as, uniquely identified by id.
 * The destination is a byte array.  We fill in three bits per byte:
 * referenced, modified, and hwmapped bits.
 * Kernel only interface, can't fault on destination data array.
 *
 */
void
hat_getstatby(as, addr, len, id, datap, clearflag)
	struct as	*as;
	caddr_t		addr;
	u_int		len;
	u_int		id;
	char 		*datap;
	int		clearflag;
{
	int		np;		/* number of pages */
	caddr_t		a;
	register char 	*dp;

	np = btop(len);
	bzero(datap, np);

	hat_sync(as, addr, len, clearflag);

	/* allocate more statistics blocks if needed */
	hrm_getblk(0, as);

	mutex_enter(&hat_statlock);
	if (hrm_hashtab == NULL) {
		/* can happen when victim process exits */
		mutex_exit(&hat_statlock);
		return;
	}
	dp = datap;
	a = (caddr_t)((int)addr & MMU_PAGEMASK);
	while (a < addr + len) {
		struct hrmstat	*hrm;
		int	n;		/* number of pages, temp */
		int	h;		/* hash index */
		register u_int po;

		h = hrm_hash(as, a);
		n = (HRM_PAGES - (((u_int)a & HRM_PAGEMASK) >> MMU_PAGESHIFT));
		if (n > np)
			n = np;
		po = ((u_int)a & HRM_BASEOFFSET) >> MMU_PAGESHIFT;

		for (hrm = hrm_hashtab[h]; hrm; hrm = hrm->hrm_hnext) {
			if (hrm->hrm_as == as &&
			    hrm->hrm_base == ((u_int)a & HRM_BASEMASK) &&
			    id == hrm->hrm_id) {
				register int i, nr;
				register u_int bo, spb;

				/*
				 * Extract leading unaligned bits.
				 */
				i = 0;
				while (i < n && (po & 3)) {
					bo = po / (NBBY / 2);
					spb = (3 - (po & 3)) * 2;
					*dp++ |= (hrm->hrm_bits[bo] >> spb) & 3;
					if (clearflag)
						hrm->hrm_bits[bo] &= ~(3<<spb);
					po++;
					i++;
				}
				/*
				 * Extract aligned bits.
				 */
				nr = n/4*4;
				bo = po / (NBBY / 2);
				while (i < nr) {
					register int bits = hrm->hrm_bits[bo];
					*dp++ |= (bits >> 6) & 3;
					*dp++ |= (bits >> 4) & 3;
					*dp++ |= (bits >> 2) & 3;
					*dp++ |= (bits >> 0) & 3;
					if (clearflag)
						hrm->hrm_bits[bo] = 0;
					bo++;
					po += 4;
					i += 4;
				}
				/*
				 * Extract trailing unaligned bits.
				 */
				while (i < n) {
					bo = po / (NBBY / 2);
					spb = (3 - (po & 3)) * 2;
					*dp++ |= (hrm->hrm_bits[bo] >> spb) & 3;
					if (clearflag)
						hrm->hrm_bits[bo] &= ~(3<<spb);
					po++;
					i++;
				}

				break;
			}
		}
		if (hrm == NULL)
			dp += n;
		np -= n;
		a += n * MMU_PAGESIZE;
	}
	mutex_exit(&hat_statlock);
}
