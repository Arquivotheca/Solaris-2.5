!
!	Copyright (c) 1994 by Sun Microsystems, Inc.
!

#pragma ident "@(#)map.s	1.17	94/10/18 SMI"

/*
 * Additional memory mapping routines for use by standalone debugger,
 * setpgmap(), getpgmap() are taken from the boot code.
 */

#include "assym.s"
#include <sys/param.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/enable.h>
#include <sys/cpu.h>
#include <sys/eeprom.h>
#include <sys/asm_linkage.h>
#include <sys/privregs.h>
#include <sys/debug/debug.h>

	.seg	".text"
	.align	4

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

#endif	/* lint */

#if defined(lint)

/*
 * Get the machine type in the ID prom.
 */
/* ARGSUSED */
u_char
getmachinetype(void)
{ return (u_char)(0); }

#else	/* lint */

	ENTRY(getmachinetype)
	set	IDPROM_ADDR+1, %o0
	retl
	ldub	[%o0], %o0

#endif	/* lint */

#if defined(lint)

/*
 * Flush a page from the cache.
 *
 */
/* ARGSUSED */
int
vac_pageflush(caddr_t vaddr)
{ return (0); }

#else	/* lint */

	ENTRY(vac_pageflush)
	srl	%o0, PGSHIFT, %o0	! mask off low bits
	sll	%o0, PGSHIFT, %o0
	set	MMU_PAGESIZE, %o1	! compute NLINES=PAGESIZE/LINESIZE
	set	vac_linesize, %o2	!  ...number of lines to flush
	ld	[%o2], %o2
	sll	%o2, 2, %o5		! VAC_LINESIZE * 4
0:
	srl	%o2, 1, %o2		! we "know" LINESIZE is a power of 2
	tst	%o2			!  so just shift until LINESIZE
	bnz,a	0b			!  goes to zero
	srl	%o1, 1, %o1
	mov	16, %o2			! offsets for by 4 loop unroll
	mov	32, %o3
	mov	48, %o4
1:					! flush a 4 line chunk of the cache
	sta	%g0, [%o0]ASI_FCP	! offset 0
	sta	%g0, [%o0 + %o2]ASI_FCP	! offset 16
	sta	%g0, [%o0 + %o3]ASI_FCP	! offset 32
	sta	%g0, [%o0 + %o4]ASI_FCP	! offset 48
	subcc	%o1, 4, %o1		! decrement count
	bg	1b			! done yet?
	add	%o0, %o5, %o0		! generate next match address
2:
	retl
	nop
#endif	/* lint */


#if defined(lint)

/*
 * Initialize the cache, write all tags to zero
 *
 */
/* ARGSUSED */
int
vac_init()
{ return (0); }

#else	/* lint */

	ENTRY(vac_init)
	set	CACHE_TAGS, %o0		! address of cache tags in CTL space
	set	vac_size, %o1		! compute NLINES=SIZE/LINESIZE
	ld	[%o1], %o1		!  ...number of lines to initialize
	set	vac_linesize, %o2
	ld	[%o2], %o2
	sll	%o2, 2, %o5		! VAC_LINESIZE * 4
0:
	srl	%o2, 1, %o2		! we "know" LINESIZE is a power of 2
	tst	%o2			!  so just shift until LINESIZE
	bnz,a	0b			!  goes to zero
	srl	%o1, 1, %o1
	mov	16, %o2			! offsets for by 4 loop unroll
	mov	32, %o3
	mov	48, %o4
1:
	sta	%g0, [%o0]ASI_CTL	! write tags to zero
	sta	%g0, [%o0 + %o2]ASI_CTL	! offset 16
	sta	%g0, [%o0 + %o3]ASI_CTL	! offset 32
	sta	%g0, [%o0 + %o4]ASI_CTL	! offset 48
	subcc	%o1, 4, %o1		! done yet?
	bg	1b
	add	%o0, %o5, %o0		! next cache tags address

	retl
	nop

#endif	/* lint */
