/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)sunm_map_prog.c	1.8	94/11/17 SMI"

#include <sys/types.h>
#include <sys/pte.h>
#include <sys/mmu.h>
#include <sys/promif.h>
#include <sys/openprom.h>

extern int cache_state;
extern int mmu_3level;
extern u_int map_getrgnmap(caddr_t);
extern void map_setrgnmap(caddr_t, u_int);

#define	MAGIC_PHYS	(4*1024*1024)
/*
 * For sparc/sun platforms, the top_bootmem is the same as MAGIC_PHYS
 * and is not changed.
 */
caddr_t magic_phys = (caddr_t) MAGIC_PHYS;
caddr_t top_bootmem = (caddr_t) MAGIC_PHYS;

/*
 * /boot will be using region 0 something else seems to have used region 1
 * so will start allocating from region 2. The PROM uses region 0xfe.
 */
/* FIX ME!  This #define should be fixed to use the kernels */
#undef	SMGRP_INVALID
#define	SMGRP_INVALID	255

#define	REG_LOWATER	2		/* first region we'll use */
#define	REG_HIWATER	0xfd		/* last region we could use */

u_int reg_eof = REG_LOWATER;

/*
 * We know the Monitor uses 15 pmgs starting at the last entry - 1
 */
#define	SEG_HIWATER	(PMGRP_INVALID-15)

/*
 * ...and that the first 4MB uses 16 PMG's, so our V0
 * allocator can tickle the rest of them outside these bounds.
 */
#define	SEG_LOWATER	(0x10)

static u_short seg_eof = (u_short)SEG_LOWATER;

/*
 * This routine should only be needed by sun4's and V0 sun4c's
 *
 * This routine will map in a segment indicated by vaddr with
 * the (hopefully) unused segment entry given by seg_eof.
 * It maps the pages linearly in the segment.
 * Warning:
 * Really low-tech hardware stuff here.
 * Not for the faint of heart.
 */

void
map_child(caddr_t v, caddr_t p)
{
	u_short seg;
	u_long pte;
	extern u_int pageshift;
	static int first_time = 1;
	static void invalidate_mmu_maps();

	if (first_time) {
		first_time = 0;
		invalidate_mmu_maps();
	}

	if (mmu_3level) {
		u_int reg = map_getrgnmap(v);
		if (reg == SMGRP_INVALID) {
			map_setrgnmap(v, reg_eof);
			reg_eof++;
		}
	}

	seg = getsegmap(v);
	if (seg < PMGRP_INVALID && seg > SEG_HIWATER)
		prom_panic("map_child: segment out of range");

	if (seg < SEG_LOWATER || seg == PMGRP_INVALID) {
		setsegmap(v, seg_eof);
		seg_eof++;
	}

	if (seg_eof >= SEG_HIWATER)
		prom_panic("map_child: too many segments");

	/*
	 * this should map the pages linearly in the seg
	 */
	pte = (u_int)p >> pageshift;

	/*
	 * Now froggle up a PTE to give to the standalone.  We
	 * always set the Valid, Supervisor Writeable, Onboard Mem bits.
	 *
	 * However, the 'cacheable' bit needs more thought.
	 *
	 * sun4c cache rules:
	 *
	 *	For sun4c machines we always give uncached memory, since
	 *	the V2 PROMs that we're trying to emulate here do this too.
	 *
	 * sun4 cache rules:
	 *
	 *	We currently give pages marked cacheable, though this policy
	 *	may change as we put more effort into stabilising sun4's -
	 *	particularly those with writeback VACs..
	 *
	 *	There is an undocumented boot flag to enable this
	 *	cacheing to be disabled if it causes problems.
	 *
	 * sun.. cache rules:
	 *
	 *	Later machines use the PROM exclusively, so the decision can
	 *	be punted to the PROM for those machines.
	 */
	pte |= PG_V|PG_W|PG_S|PGT_OBMEM;
	if (!cache_state || prom_getversion == 0)
		pte |= PG_NC;

	setpgmap(v, pte);
}

/*
 * The base virtual address of the region in which the
 * monitor lives.
 */
#define	SUNMON_REGBASE	(SUNMON_START & 0xff000000)

/*
 * The idea here is to invalidate any existing mappings between
 * KERNELBASE and SUNMON_START.  map_child() is going to use the state
 * of the mmu maps to keep track of what it's done, so we need a
 * clean slate to start with.  In the case of the three-level mmu
 * things are a little trickier because mappings may exist is regions
 * other than the current ones used for addresses between KERNELBASE and
 * SUNMON_START.  So, we invalidate the segment map entry in all regions
 * that we might expect to reuse.  Note that since the monitor is already
 * mapped we can't mess with its region, otherwise the prom becomes
 * unmapped and bad things happen.
 */
static void
invalidate_mmu_maps()
{
	u_int addr, reg;
	u_short pmgrp, smgrp;

	if (mmu_3level) {
		for (reg = REG_LOWATER; reg <= REG_HIWATER; reg++) {
			for (addr = KERNELBASE; addr < SUNMON_REGBASE;
			    addr += PMGRPSIZE) {
				map_setrgnmap((caddr_t) addr, reg);
				setsegmap((caddr_t) addr, PMGRP_INVALID);
			}
		}

		for (addr = SUNMON_REGBASE; addr < SUNMON_START;
		    addr += PMGRPSIZE)
			setsegmap((caddr_t) addr, PMGRP_INVALID);

		/*
		 * the first loop left valid segment map group numbers
		 * in the region map, let's make those invalid
		 */
		for (addr = KERNELBASE; addr < SUNMON_REGBASE;
		    addr += SMGRPSIZE)
			map_setrgnmap((caddr_t) addr, SMGRP_INVALID);
	} else {
		for (addr = KERNELBASE; addr < SUNMON_START; addr += PMGRPSIZE)
			setsegmap((caddr_t) addr, PMGRP_INVALID);
	}
}

/*
 * A crufty debugging routine.
 *
 * XXX	Only works on sunmmu machines ..
 */
void
dump_mmu(void)
{
	char seg;
	caddr_t i;

	prom_printf("Segment table:\n");
	for (i = (caddr_t)0xf0000000;
	    i - 1 < (caddr_t)0xffffffff; i += (256*1024)) {
		seg = getsegmap(i);
		if (seg != -1)
			prom_printf("\t%x: %x ", i, seg);
	}
	prom_printf("\n");
}
