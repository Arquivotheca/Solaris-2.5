/*
 * Copyright (c) 1990-1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)kalloc.c	1.11	95/03/07 SMI"

#include <sys/t_lock.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/mman.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/cpu.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg_kmem.h>

/*
 * Global Data:
 */

/*
 * Kernel No-cache list allocations
 */

struct knclist {
	struct knclist	*knc_next;	/* next in a list */
	struct page	*knc_pp;	/* page list for us */
	u_long		knc_addr;	/* base kernel virtual */
	u_long		knc_size;	/* size of space (bytes) */
	int		knc_nmapent;	/* number of map entries in map */
	struct map	*knc_map;	/* map we're using */
	char 		knc_mname[8];	/* name of this map */
};

static int kncnbytes = 0;
static struct knclist *knclist;
static kmutex_t	knc_lock;		/* mutex protecting knclist data */
static int kcadebug = 0;
static int kcused, kctotasked, kctotused, kncused, knctotasked, knctotused;

void
kncinit()
{
	mutex_init(&knc_lock, "knc_lock", MUTEX_DEFAULT, DEFAULT_WT);
}

/*
 * Allocate from the system, aligned on a specific boundary.
 *
 * It is assumed that alignment, if non-zero, is a simple power of two.
 *
 * It is also assumed that the power of two alignment is
 * less than or equal to the system's pagesize.
 */


caddr_t
kalloca(u_int nbytes, int align, int nocache, int cansleep)
{
	u_long addr, raddr, alloc, *saddr;

	if (kcadebug) {
		printf("kalloca: request is %d%%%d nc=%d slp=%d\n", nbytes,
		    align, nocache, cansleep);
	}

	/*
	 * Validate and fiddle with alignment
	 */

	if (align < 8)
		align = 8;
	if (align & (align - 1))
		return ((caddr_t)0);

	/*
	 * Round up the allocation to an alignment boundary,
	 * and add in padding for both alignment and storage
	 * of the 'real' address and length.
	 */

	alloc = roundup(nbytes, align) + 8 + align;

	/*
	 * Allocate the requested space from the desired space
	 */

	raddr = addr = (u_long)
	    kmem_alloc(alloc, (cansleep)? KM_SLEEP : KM_NOSLEEP);

	if (addr) {
		mutex_enter(&knc_lock);
		if (nocache) {
			knctotasked += nbytes;
			knctotused += alloc;
			kncused += alloc;
		} else {
			kctotasked += nbytes;
			kctotused += alloc;
			kcused += alloc;
		}
		mutex_exit(&knc_lock);
		if (addr & (align - 1)) {
			addr = roundup(addr, align);
			if (addr - raddr < 8)
				addr = roundup(addr, align);
		} else
			addr += align;

		saddr = (u_long *) addr;
		saddr[-1] = raddr;
		saddr[-2] = alloc;
	}

	if (kcadebug) {
		printf("kalloca: %d%%%d from %s got %x.%x returns %x\n",
		    nbytes, align, (nocache)? "kncmap" : "heap", raddr,
		    raddr + alloc, addr);
	}

	return ((caddr_t)addr);
}

void
kfreea(caddr_t addr, int nocache)
{
	u_long *saddr, raddr;
	u_int nbytes;

	saddr = (u_long *) (((u_long) addr) & ~0x3);
	raddr = saddr[-1];
	nbytes = (u_int) saddr[-2];
	if (kcadebug) {
		printf("kfreea: freeing %x (real %x.%x) from %s area\n",
		    addr, raddr, raddr + nbytes, (nocache)? "kncmap" : "heap");
	}
	mutex_enter(&knc_lock);
	if (nocache) {
		kncused -= nbytes;
	} else {
		kcused -= nbytes;
	}
	mutex_exit(&knc_lock);

	kmem_free((caddr_t)raddr, nbytes);
}

static struct {
	u_int   runouts;
	u_int   sleeps;
	u_int   align;
	u_int   unalign;
} kncstats;
