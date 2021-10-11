/*
 *
 *	Copyright (c) 1990 - 1991, 1993 by Sun Microsystems, Inc.
 *
 *	Generic interfaces for dynamically selectable modules.
 */

#pragma ident   "@(#)module_asm.s 1.4     94/11/20 SMI"

#include <sys/asm_linkage.h>
#include <sys/machparam.h>
#include <sys/mmu.h>

#if defined(lint)

void
srmmu_noop(void)
{}

/* ARGSUSED */ 
void
turn_cache_on(int cpuid)
{}

/* ARGSUSED */
void
pac_flushall()
{}

#ifdef OBSOLETE

/* ARGSUSED */ 
void
turn_cache_off(int cpuid)
{}

/* ARGSUSED */ 
void
turn_mxcc_on(int cpuid)
{}

/* ARGSUSED */ 
void
turn_mxcc_off(int cpuid)
{}

void
cache_flushall(void)
{}

#endif OBSOLETE
 
#else	/* lint */

#define	REVEC(name, r)				\
	sethi	%hi(v_/**/name), r		; \
	ld	[r+%lo(v_/**/name)], r		; \
	jmp	r				; \
	nop

	ENTRY(srmmu_noop)
	retl
	nop
	SET_SIZE(srmmu_noop)

	ENTRY_NP(turn_cache_on)		! enable the cache
	REVEC(turn_cache_on, %g1)
	SET_SIZE(turn_cache_on)

	!
	! blow away ptags
	!
	ENTRY(pac_flushall)
	REVEC(pac_flushall, %g1)
	SET_SIZE(pac_flushall)
 
#ifdef OBSOLETE
	ENTRY_NP(turn_cache_off)	! disable the cache
	REVEC(turn_cache_off, %g1)
	SET_SIZE(turn_cache_off)

	ENTRY_NP(turn_mxcc_on)		! enable the cache
	REVEC(turn_mxcc_on, %g1)	! enable E-$ for viking
	SET_SIZE(turn_mxcc_on)

	ENTRY_NP(turn_mxcc_off)	! disable the cache
	REVEC(turn_mxcc_off, %g1)	! disable E-$ for viking
	SET_SIZE(turn_mxcc_off)

!
! Routines for flushing TLB and VAC (if it exists) for the particular 
! module.
!
	ENTRY(cache_flushall)
	REVEC(cache_flushall, %o5)
	SET_SIZE(cache_flushall)

 
#endif  /* lint */

#ifdef NOTYET
#ifdef	lint
void
vik_pac_pageflush(pfnum)
	u_int pfnum;
{ }

#else	/* lint */
	ENTRY_NP(vik_pac_pageflush)
	! Flush page from Viking I and D Caches.
	! This routine is invoked for Viking/No MXCC machines.
	! This routine will only ever be run on a UP machine.
	! We need not splhi since we are in the process of setting
	! up a translation for the page and we already hold the
	! hat_mutex.
	!
	! setup to check dcache tags
	!
	! %g1 virtual address of alias to trigger flush
	! %o4 & %o5 ptag of line
	! %o3 ptag to flush
	! %o2 current line
	! %o1 current set
	! %o0 tag address
	!
	! We use pp_base as an alias for forcing replacement of dirty lines. 
	! cannnot use physical addresses for
	! an alias because then we have to use the MMU bypass ASI (ASI_MEM)
	! and to indicate that it is a cacheable access (it has to be because
	! we are forcing line replacement) we have to set the Viking MMU
	! control register Alternate Cacheable (AC) bit. Doing this seems to
	! cause an almost immediate watchdog. 

1:
	mov	%o0, %o3		! The physical page number
	set	pp_base, %g1		! Form alias for flushing
	ld	[%g1], %g1
	andn	%g1, MMU_PAGEOFFSET, %g1

	clr	%o1			! set = 0
	clr	%o2			! line = 0

dcache_top:

	set	VIK_PTAGADDR, %o0	! ptagaddr = VIK_PTAGADDR |
	sll	%o1, 5, %o4		!  set << 5 | line << 26
	or	%o4, %o0, %o0
	sll	%o2, 26, %o4
	or	%o4, %o0, %o0
	ldda	[%o0]ASI_DCT, %o4	! load ptag (%o5 = paddr %o4 = tag info)
	cmp	%o5, %o3		! match?
	bne	next_dline		! no, check next line in same set
	nop

	set	VIK_PTAGVALID, %o5
	btst	%o5, %o4		! line valid?
	bz	next_dline		! no, check next line
	nop

	set	VIK_PTAGDIRTY, %o5
	btst	%o5, %o4		! line dirty?
	bz	next_dline		! no, just kill valid bit
	nop

	!
	! Force writeback of all lines in this set by doing 8 loads
	! to it.  The 1st 4 will fill the set, the next 4 will replace
	! the previous 4.  The result will be 4 clean lines.  Then invalidate
	! all the lines in case the lines we loaded shouldn't be cached.
	!
	sll	%o2, 26, %o4
	xor	%o0, %o4, %o0		! reset ptagaddr to line 0
	sll	%o1, 5, %o4		! form alias for current set in %o4
	add	%o4, %g1, %o4
	clr	%o2			! use %o2 as counter

2:
	ld	[%o4], %o5
	set	MMU_PAGESIZE, %o5	! Same offset, 8 different pages
	add	%o4, %o5, %o4
	cmp	%o2, 7
	bne,a	2b
	inc	%o2			! DELAY

3:
	ba	next_dset
	nop

next_dline:
	cmp	%o2, 3			! last line?
	bne,a	dcache_top
	inc	%o2			! DELAY
next_dset:
	clr	%o2			! reset line #
	cmp	%o1, 127		! last set?
	bne,a	dcache_top
	inc	%o1			! DELAY

	/* 
	 *  There is no need to flush the Icache, since it is
	 *  always consistent with memory.
	 */
	retl
	nop
	SET_SIZE(vik_pac_pageflush)

#endif	/* lint */
#endif NOTYET


#ifdef	lint
/*ARGSUSED*/
void
vik_mxcc_pageflush(pfnum)
	u_int pfnum;
{}

#else	/* lint */

	ENTRY_NP(vik_mxcc_pageflush)		! flush page from MXCC
	/*
	 *  %o0 has the PFN of the page to flush.
	 *  The algorithm uses the MXCC's stream copy registers
	 *  to read the data and write it back to memory.  This will cause the
	 *  MXCC to issue a CWI to all other caches.
	 *  Note that we must do this for every subblock, not just every
	 *  line in the MXCC.
	 *  Also note that this will only flush paddrs's with PA<35:32> = 0.
	 *  Full 36-bit flushing is left as an exercise to the reader.
	 */

	set	MXCC_STRM_SRC, %o4
	set	MXCC_STRM_DEST, %o5
	mov	1 << 4, %o2			! enable Cacheability (bit 36)
	sll	%o0, MMU_PAGESHIFT, %o3		

1:
	/* Read the data into the Stream Data Register */
	stda	%o2, [%o4]ASI_MXCC	
	/* and write it back from whence it came */
	stda	%o2, [%o5]ASI_MXCC	

	add	%o3, MXCC_STRM_SIZE, %o3	! should this be a variable?
	andcc	%o3, MMU_PAGEOFFSET, %g0	! done?
	bnz	1b
	nop

	retl
	clr	%o0
	
	SET_SIZE(vik_mxcc_pageflush)
#endif OBSOLETE

#endif lint

/*
 * Get processor state register (XXX ported from sparc/ml/sparc_subr.s. Put
 * it here since we have no better place to keep it yet)
 */
 
#if defined(lint)
 
greg_t
getpsr(void)
{ return (0); }
 
#else	/* lint */
 
	ENTRY(getpsr)
	retl
	mov	%psr, %o0
	SET_SIZE(getpsr)
 
#endif	/* lint */
