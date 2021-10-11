/*
 *	Copyright (c) 1990 - 1991, 1993 by Sun Microsystems, Inc.
 *
 * assembly code support for spitfire modules
 */

#pragma ident	"@(#)module_spitfire_asm.s	1.19	95/08/04 SMI"

#if defined(lint)
#include <sys/types.h>
#else	/* lint */
#include "assym.s"
#endif	/* lint */

#include <sys/machparam.h>
#include <sys/asm_linkage.h>
#include <sys/spitasi.h>
#include <sys/trap.h>

#include <sys/spitregs.h>

#if defined(lint)

/* ARGSUSED */
void
spitfire_cache_init(void)
{}

#else	/* lint */

	.seg	".data"
sf_cache_panic1:
	.ascii	"spitfire_cache_init: not implemented"
	.byte	0
sf_cache_panic2:
	.ascii	"spitfire_vac_flush: not implemented"
	.byte	0
	.align	4
	.seg	".text"

	ENTRY_NP(spitfire_cache_init)
	sethi	%hi(sf_cache_panic1), %o0
	call	panic
	 or	%o0, %lo(sf_cache_panic1), %o0
	retl
	 nop
	SET_SIZE(spitfire_cache_init)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
void
spitfire_vac_flush(caddr_t va, int sz)
{}

#else	/* lint */

	ENTRY(spitfire_vac_flush)
	sethi	%hi(sf_cache_panic2), %o0
	call	panic
	 or	%o0, %lo(sf_cache_panic2), %o0
	retl
	 nop
	SET_SIZE(spitfire_vac_flush)

#endif	/* lint */

/*
 * Spitfire Cache Operators
 */

#if defined (lint)

/* ARGSUSED */
void
spitfire_icache_flushpage(caddr_t vaddr)
{
}

/* ARGSUSED */
void
spitfire_cache_debug_init()
{}

/* ARGSUSED */
void
spitfire_dcache_flushpage(int pfnum, int vcolor)
{
}

/* ARGSUSED */
void
spitfire_dcache_flushpage_tl1(int pfnum, int vcolor)
{
}

/* ARGSUSED */
void
spitfire_cache_flushall_tl1(void)
{
}

/* ARGSUSED */
void
spitfire_reenable_caches_tl1()
{}

/* ARGSUSED */
void
spitfire_cache_nop()
{}

#else	/* lint */

/* 
 * spitfire_cache_flushall_tl1
 * 	Flush the entire e-$ using displacement flush
 *	Assume we have a contigous mappings of e-$ size starting from
 *	KERNELBASE to etext that is mapped read-only.
 */
	ENTRY(spitfire_cache_flushall_tl1)

	set	ecache_size, %g1
	ld	[%g1], %g1
	set	KERNELBASE, %g2
	set	ecache_linesize, %g3
	ld	[%g3], %g3

	b	2f
	nop

1:
	ld	[%g1+%g2], %g0		! start reading from KERNELBASE
					!                      + ecache_size
2:
	subcc	%g1, %g3, %g1
	bcc,a	1b
	nop
	

	retry
	SET_SIZE(spitfire_cache_flushall_tl1)
 
/*
 * spitfire_icache_flushpage:
 *	Flush 1 page of the I-$ starting at vaddr
 * 	%o0 vaddr
 *	%o1 bytes to be flushed 
 */

	ENTRY(spitfire_icache_flushpage)

	set	MMU_PAGESIZE, %o1

	srlx	%o0, MMU_PAGESHIFT, %o0			! Go to begining of
	sllx	%o0, MMU_PAGESHIFT, %o0			! page
	
	membar	#StoreStore				! Ensure the stores
							! are globally visible
1:
	flush	%o0
	sub	%o1, ICACHE_FLUSHSZ, %o1		! bytes = bytes-8 
	brnz,a,pt %o1, 1b
	add	%o0, ICACHE_FLUSHSZ, %o0		! vaddr = vaddr+8

	retl
	nop
	SET_SIZE(spitfire_icache_flushpage)
/*
 * spitfire_dcache_flush(pfnum, color)
 *	Flush 1 8k page of the D-$ with physical page = pfnum
 *	Algorithm:
 *		The spitfire dcache is a 16k direct mapped virtual indexed,
 *		physically tagged cache.  Given the pfnum we read all cache
 *		lines for the corresponding page in the cache (determined by
 *		the color).  Each cache line is compared with
 *		the tag created from the pfnum. If the tags match we flush
 *		the line.
 *	NOTE:	Any changes to this code probably need to be reflected in
 *		sfmmu_tlbcache_flushpage_tl1.
 */
	.seg	".data"
	.align	8
	.global	dflush_type
dflush_type:
	.word	FLUSHPAGE_TYPE
	.seg	".text"

	ENTRY(spitfire_dcache_flushpage)
	/*
	 * %o0 = pfnum, %o1 = color
	 */
	DCACHE_FLUSH(%o0, %o1, %o2, %o3, %o4)
	retl
	nop
	SET_SIZE(spitfire_dcache_flushpage)

	ENTRY_NP(spitfire_dcache_flushpage_tl1)
	/*
	 * %g1 = pfnum, %g2 = color
	 */
	DCACHE_FLUSH(%g1, %g2, %g3, %g4, %g5)
	retry
	nop
	SET_SIZE(spitfire_dcache_flushpage_tl1)
/*
 * spitfire_cache_debug_init
 *	Initialize the various caches.
 *	Since in the real running system, the $es should be initialized
 *	by the PROM, this is really here for debugging purposes in case
 *	we want to selectively turn on/off each $. It is not expected 
 *	anyone would call this routine in a perfect real world.
 *	Algorithm :
 *		We would read the LSU register. If any of the I/D cache
 *		is not turned on, we would turn it on/off according to
 *		the variables use_ic, use_dc and initialize accordingly
 *
 *	%l0 - LSU register or modified LSU
 *	%l1 - Scratch
 *	%i0 - Scratch, holds use_ic/use_dc
 *	%o0 - Flag indicating a store to LSU needed 
 *   XXX - What about E-$. 
 */

	ENTRY(spitfire_cache_debug_init)
	save	%sp, -SA(MINFRAME), %sp

	ldxa	[%g0]ASI_LSU, %l0		! read LSU
	and	%l0, 0x1, %l1			! check IC bit
	brnz	%l1, 2f				! Already enabled
	nop
	
	set	use_ic, %i0			! Check use_ic bit
	ldub	[%i0], %i0		
	brz	%i0, 2f				! use_ic = 0, don't mess it 
	nop					! Else initialize it 

	mov	%g0, %l1			! Line = 0;
1:
	sllx	%l1, 5, %l1		
	stxa	%g0, [%l1]ASI_IC_TAG 		! Store it back
	stxa	%g0, [%l1]ASI_IC_NEXT
	membar	#Sync
	srlx	%l1, 5, %l1
	add	%l1, 1, %l1
	cmp	%l1, 0x200			! Total of 512 tags
	bne	1b
	nop

	or	%l0, 0x1, %l0			! Turn On I bit
	mov	1, %o0
	
2:	
	and	%l0, 0x2, %l1			! Check DC bit
	brnz	%l1, 4f				! Already enabled
	nop

	set	use_dc, %i0			! Check use_dc bit
	ldub	[%i0], %i0		
	brz	%i0, 4f				! use_dc = 0, don't mess it 
	nop					! Else initialize it 


	mov	%g0, %l1			! Line = 0;
3:
	sllx	%l1, 5, %l1		
	stxa	%g0, [%l1]ASI_DC_TAG 		! Store it back
	membar	#Sync
	srlx	%l1, 5, %l1
	add	%l1, 1, %l1
	cmp	%l1, 0x200			! Total of 512 tags
	bne	3b
	nop
	or	%l0, 0x2, %l0			! Turn On D bit
	mov	1, %o0				! Set flag to do store to LSU
	
4:
	tst	%o0				! Changed LSU bits?
	beq	5f
	nop

	stxa	%l0, [%g0]ASI_LSU		! Store to LSU
	membar	#Sync
5:
	ret
	restore
	SET_SIZE(spitfire_cache_debug_init)

/*
 * spitfire_reenable_caches_tl1 - reenable I and D $
 *
 */
	ENTRY(spitfire_reenable_caches_tl1)
	ldxa	[%g0]ASI_LSU, %g1		! read LSU
	or	%g1, 0x3, %g1			! Turn On IC and DC bits
	stxa	%g1, [%g0]ASI_LSU		! Store to LSU
	membar	#Sync
	retry
	SET_SIZE(spitfire_reenable_caches_tl1)
	
/*
 * spitfire_cache_nop - Do nothing. 
 *                      For non-yet-implemented routines to point to. 
 */

	ENTRY(spitfire_cache_nop)
	retl
	nop
	SET_SIZE(spitfire_cache_nop)

#endif /* lint */

