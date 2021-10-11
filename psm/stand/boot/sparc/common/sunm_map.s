/*
 * Copyright (c) 1988-1991 Sun Microsystems, Inc.
 */

#pragma ident	"@(#)sunm_map.s	1.8	94/12/10 SMI" /* from SunOS 4.1 */ 

/*
 * Memory Mapping for SunMMU architectures
 *
 * The following subroutines accept any address in the mappable range
 * (256 megs).  They access the map for the current context register.  They
 * assume that currently we are running in supervisor state.
 */

#include <sys/asm_linkage.h>
#include <sys/mmu.h>

#define	CONTEXTBASE	0x30000000	/* context reg */
#define	SEGMENTADDRBITS 0xFFFC0000	/* segment map virtual address mask */
#ifndef ASI_RM
#define	ASI_RM		0x6		/* region map on sun4's */
#endif

#if defined(lint)

/*
 * Set the pme for address v using the software pte given.
 */
/* ARGSUSED */
void
setpgmap(caddr_t v, long pme)
{}

#else	/* lint */

	ENTRY(setpgmap)
	andn	%o0, 0x3, %o2		! align to word boundary
	retl
	sta	%o1,[%o2]ASI_PM		! write page map entry
	SET_SIZE(setpgmap)

#endif	/* lint */

#if defined(lint)

/*
 * Read the page map entry for the given address v
 * and return it in a form suitable for software use.
 */
/* ARGSUSED */
long
getpgmap(caddr_t v)
{ return (0L); }

#else	/* lint */

	ENTRY(getpgmap)
	andn	%o0,0x3,%o1		! align to word boundary
	retl
	lda	[%o1]ASI_PM,%o0		! read page map entry
	SET_SIZE(getpgmap)

#endif	/* lint */

#if defined(lint)

/*
 * Set the segment map entry for segno to pm.
 */
/* ARGSUSED */
void 
setsegmap(u_int segno, u_short pm)
{}

#else	/* lint */

	ENTRY(setsegmap)
	set	SEGMENTADDRBITS, %o2
	and	%o0, %o2, %o0		! get relevant segment address bits
	retl
	stha	%o1,[%o0]ASI_SM		! write segment entry
	SET_SIZE(setsegmap)

#endif	/* lint */

#if defined(lint)

/*
 * Get the segment map entry for the given virtual address
 */
/*ARGSUSED*/
u_int
getsegmap(caddr_t vaddr)
{ return (0); }

#else	/* lint */

	ENTRY(getsegmap)
	andn    %o0, 0x1, %o0           ! align to halfword boundary
	lduha	[%o0]ASI_SM,%o0		! read segment entry
	sethi	%hi(segmask), %o2	! need to mask bits due to bug in cobra
	ld      [%o2 + %lo(segmask)], %o2
	retl
	and     %o0, %o2, %o0
	SET_SIZE(getsegmap)

#endif	/* lint */

#if defined(lint)

/*
 * Set the current [user] context number to uc.
 */
/* ARGSUSED */
void
setcontext(int uc)
{}

#else	/* lint */

	ENTRY(setcontext)
	set     CONTEXTBASE, %o1
	retl
	stba    %o0, [%o1]ASI_CTL       ! write the context register
	SET_SIZE(setcontext)

#endif	/* lint */

#if defined(lint)

/*
 * Return the 16 bit region map entry for the given region number.
 */
/*ARGSUSED*/
u_int
map_getrgnmap(caddr_t v)
{ return (0); }

#else	/* lint */

	ENTRY(map_getrgnmap)
	andn	%o0, 0x1, %o0		! align to halfword boundary
	or	%o0, 0x2, %o0
	lduha	[%o0]ASI_RM, %o0	! read region number
	retl
	srl	%o0, 0x8, %o0
	SET_SIZE(map_getrgnmap)

#endif	/* lint */

#if defined(lint)

/*
 * Set the segment map entry for segno to pm.
 */
/* ARGSUSED */
void
map_setrgnmap(caddr_t v, u_int pm)
{}

#else	/* lint */

	ENTRY(map_setrgnmap)
	andn	%o0, 0x1, %o0		! align to halfword boundary
	or	%o0, 0x2, %o0
	sll	%o1, 0x8, %o1
	retl
	stha	%o1, [%o0]ASI_RM	! write region entry
	SET_SIZE(map_setrgnmap)

#endif	/* lint */
