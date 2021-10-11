/*
 * Copyright (c) 1988-1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)sunm_cache.s	1.13	94/06/09 SMI"	/* from SunOS 4.1 */ 

#include <sys/asm_linkage.h>
#include <sys/machparam.h>
#include <sys/pte.h>
#include <sys/mmu.h>
#include <sys/enable.h>

/*
 * change all of the text and data pages
 * that are used by prog to be cacheable
 */

#if defined(lint)

/* ARGSUSED */
void
sunm_cache_prog(u_long start, u_long end)
{}

#else	/* lint */

	ENTRY(sunm_cache_prog)
	save	%sp, -SA(MINFRAME), %sp
	set	PG_NC, %l0		! the no cache bit
	set	pagesize, %l1		! pagesize increment
	ld	[%l1], %l1
1:
	call	getpgmap		! get pte
	mov	%i0, %o0		! address

	andn	%o0, %l0, %o1		! clear the nocache bit
	call	setpgmap		! write the new pte
	mov	%i0, %o0		! address

	add	%i0, %l1, %i0		! add pagesize to get next pte
	cmp	%i0, %i1
	bleu	1b			! go do the next page
	nop

	ret
	restore
	SET_SIZE(sunm_cache_prog)

#endif	/* lint */

/*
 * sunm_turnon_cache
 *	write all the tags to zero
 *	turn on the cache via the system enable register
 */

#if defined(lint)

/* ARGSUSED */
void
sunm_turnon_cache(void)
{}

#else	/* lint */

	ENTRY(sunm_turnon_cache)
	set     CACHE_TAGS, %o0		! address of cache tags in CTL space
	set	vac_nlines, %o1		! number of lines to initialize
	ld	[%o1], %o1
	set	vac_linesize, %o2	! offsets for by 4 loop unroll
	ld	[%o2], %o2
	add     %o2, %o2, %o3		! linesize * 2
	add	%o2, %o3, %o4		! linesize * 3
	add	%o2, %o4, %o5		! linesize * 4
1:
	sta     %g0, [%o0]ASI_CTL       ! write tags to zero
	sta     %g0, [%o0 + %o2]ASI_CTL ! offset (1 * linesize)
	sta     %g0, [%o0 + %o3]ASI_CTL ! offset (2 * linesize)
	sta     %g0, [%o0 + %o4]ASI_CTL ! offset (3 * linesize)
	subcc   %o1, 4, %o1             ! done yet?
	bg      1b
	add     %o0, %o5, %o0		! next cache tags address

	set     ENABLEREG, %o2          ! address of real version in hardware
	lduba   [%o2]ASI_CTL, %g1	! get current value
	bset	ENA_CACHE, %g1		! enable cache bit
	retl
	stba	%g1, [%o2]ASI_CTL	! write new value
	SET_SIZE(sunm_turnon_cache)

#endif	/* lint */

#define	GET(val, p, d) \
	sethi %hi(val), p; \
	ld [p + %lo(val)], d;

/*
 * Flush a range of addresses.
 * 
 * Needed for workaround on sun4c's with V0 OBP's
 * where the cache is not consistent with I/O.
 */

#if defined(lint)

/*ARGSUSED*/
void
sunm_vac_flush(caddr_t v, u_int nbytes)
{}

#else	/* lint */

	ENTRY(sunm_vac_flush)
	set     ENABLEREG, %o2          ! address of real version in hardware
	lduba   [%o2]ASI_CTL, %g1	! get current value
	btst	ENA_CACHE, %g1		! check enable cache bit
	bz	2f			! cache off, return
	GET(vac_size, %g1, %o3)		! get VAC size
	GET(vac_linesize, %g1, %o2)	! get line size
	sub	%o2, 1, %o4		! convert to mask (assumes power of 2)
	add	%o0, %o1, %o1		! add start to length
	andn	%o0, %o4, %o0		! round down start
	add	%o4, %o1, %o1		! round up end
	andn	%o1, %o4, %o1		! and mask off
	sub	%o1, %o0, %o1		! and subtract start
	cmp	%o1, %o3		! if (nbytes > vac_size)
	bgu,a	1f			! ...
	mov	%o3, %o1		!	nbytes = vac_size
1:
	subcc	%o1, %o2, %o1
	bg	1b
	sta	%g0, [%o0 + %o1]ASI_FCP

2:
	retl
	nop
	SET_SIZE(sunm_vac_flush)

#endif	/* lint */
