/*
 *	Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident	"@(#)copy.s	1.38	94/11/21 SMI"

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/asm_linkage.h>
#include <sys/vtrace.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/machthread.h>
#include <sys/clock.h>
#include <sys/bcopy_if.h>

#if defined(lint)

#else	/* lint */
#include "assym.s"
#endif	/* lint */

/*
 * The following routines have been moved to sparc/v7/ml/sparc_subr.s
 * subyte, suword, susword, fubyte, fuword, fusword,
 * subyte_noerr, suword_noerr, susword_noerr, fubyte_noerr, fuword_noerr,
 * fusword_noerr
 */

#ifdef BCOPY_BUF
/*
 * limit use of the bcopy buffer to transfers of at least this size
 * we can't use the bcopy hardware unless the underlying source and
 * destination pages are locked into memory (since we pass physical
 * addresses to the hardware and the pages can be stolen if they're
 * not locked. On an MP machine, it would be too expensive to ensure
 * this (due to potential lock contention), so we only use the code
 * in locked_pgcopy.
 *
 * See Bug Report 1159882:
 * If MXCC present, use block copy when:
 *     src and dest are page aligned;  length >= 1 page
 *     src and/or dest are not cacheABLE

 * Disabling interrupts prevents rescheduling locally, and blocks
 * cross processor interrupts necessary for demapping.  So
 * page stealing can not happen during mxcc block copies.
 * Interrupts are disabled/enabled on a page by page basis.
 */

#define	BCPY_BLKSZ		0x20
#define	BCOPY_LIMIT     	0x100

#define	PTE_CACHEABLEMASK	0x80	/* XXX */
#endif

#ifdef USE_HW_BCOPY
#define	bcopy		bcopy_asm
#define	bzero		bzero_asm
#define	kcopy		kcopy_asm
#define	copyin		copyin_asm
#define	copyout		copyout_asm
#define	xcopyin		xcopyin_asm
#define	xcopyout	xcopyout_asm
#endif

#ifdef TRACE

	.section	".text"
TR_bcopy_start:
	.asciz "bcopy_start:caller %K src %x dest %x size %d"
	.align 4
TR_kcopy_start:
	.asciz "kcopy_start:caller %K src %x dest %x size %d"
	.align 4
TR_pgcopy_start:
	.asciz "pgcopy_start:caller %K src %x dest %x size %d"
	.align 4
TR_copyout_start:
	.asciz "copyout_start:caller %K src %x dest %x size %d"
	.align 4
TR_copyin_start:
	.asciz "copyin_start:caller %K src %x dest %x size %d"
	.align 4

TR_copyout_noerr_start:
	.asciz "copyout_noerr_start:caller %K src %x dest %x size %d"
	.align 4
TR_copyin_noerr_start:
	.asciz "copyin_noerr_start:caller %K src %x dest %x size %d"
	.align 4

TR_copy_end:
	.asciz "copy_end"
	.align 4

TR_copy_fault:
	.asciz "copy_fault"
	.align 4
TR_copyout_fault:
	.asciz "copyout_fault"
	.align 4
TR_copyin_fault:
	.asciz "copyin_fault"
	.align 4

#define	TRACE_IN(a, b)		\
	save	%sp, -SA(MINFRAME), %sp;	\
	mov	%i0, %l0;	\
	mov	%i1, %l1;	\
	mov	%i2, %l2;	\
	TRACE_ASM_4(%o5, TR_FAC_BCOPY, a, b, %i7, %l0, %l1, %l2);	\
	mov	%l0, %i0;	\
	mov	%l1, %i1;	\
	mov	%l2, %i2;	\
	restore %g0, 0, %g0;

#else	/* TRACE */

#define	TRACE_IN(a,b)

#endif	/* TRACE */

/* #define DEBUG */

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

#if defined(lint)

/* ARGSUSED */
int
kcopy(caddr_t from, caddr_t to, size_t count)
{ return(0); }

#else	/* lint */

	.seg	".text"
	.align	4

	ENTRY(kcopy)

	TRACE_IN(TR_KCOPY_START, TR_kcopy_start)
#ifdef	DEBUG
	sethi	%hi(KERNELBASE), %o3
	cmp	%o0, %o3
	blu	1f
	nop

	cmp	%o1, %o3
	bgeu	3f
	nop

1:
	set	2f, %o0
	call	panic
	nop

2:	.asciz  "kcopy: arguments below kernelbase"
	.align 4
3:
#endif	DEBUG

	sethi	%hi(.copyerr), %o3	! copyerr is lofault value
	b	.do_copy		! common code
	or	%o3, %lo(.copyerr), %o3

/*
 * We got here because of a fault during kcopy.
 * Errno value is in %g1.
 */
.copyerr:
	TRACE_ASM_0 (%o2, TR_FAC_BCOPY, TR_COPY_FAULT, TR_copy_fault);

	st	%l6, [THREAD_REG + T_LOFAULT]	! restore old t_lofault
	ret
	restore	%g1, 0, %o0
	SET_SIZE(kcopy)

#endif	/* lint */

/*
 * Copy a block of storage - must not overlap (from + len <= to).
 * Registers: l6 - saved t_lofault
 *
 * bcopy(from, to, count)
 *	caddr_t from, to;
 *	size_t count;
 */

#if defined(lint)

/* ARGSUSED */
void
bcopy(caddr_t from, caddr_t to, size_t count)
{}

#else	/* lint */

	ENTRY(bcopy)

	TRACE_IN(TR_BCOPY_START, TR_bcopy_start)

#ifdef DEBUG

	tst	%o2				! turns out there
	bz	3f				! are quite a few cases
	nop					! that bcopy is called with
						! o1=0 and o2=0
	
 
	sethi   %hi(KERNELBASE), %o3
	cmp     %o0, %o3
	blu     1f
	nop
 
	cmp     %o1, %o3
	bgeu     3f
	nop
 
1:
	set     2f, %o0
	call    panic
	nop
         
2:      .asciz  "bcopy: arguments below kernelbase"
	.align 4
        
3:
#endif DEBUG


	ld	[THREAD_REG + T_LOFAULT], %o3
.do_copy:
	save	%sp, -SA(MINFRAME), %sp	! get another window
	ld	[THREAD_REG + T_LOFAULT], %l6	! save t_lofault
.bcopy_cmn:
	cmp	%i2, 12			! for small counts
	bl	.bytecp			! just copy bytes
	st	%i3, [THREAD_REG + T_LOFAULT]	! install new vector

	sethi	%hi(vme), %i4		! don't bother with loading
	ld	[%i4 + %lo(vme)], %i4	! pte's on systems that don't
	tst	%i4			! have a vmebus
	beq	.bcopy_obmem
	nop

	and	%i0, MMU_PAGEMASK, %i3
	or	%i3, FT_ALL<<8, %i3
	lda	[%i3]ASI_FLPR, %i4	! get source PTE
	and	%i4, 3, %i4
	cmp	%i4, MMU_ET_PTE
	bne,a	1f			! if not mapped in,
	ldub	[%i0], %g0		!   force source page into memory
1:	lda	[%i3]ASI_FLPR, %i4	!   and get source PTE again
	srl	%i4, 28, %i4		! convert pte to space

	! if source is vme D16, limit transfer width
	! to 16 bits. (space=0xA or 0xC)
	cmp	%i4, 0xA		! from user vme-d16
	be	.bcopy_vme16
	cmp	%i4, 0xC		! from supv vme-d16
	be	.bcopy_vme16

	! if source is vme D32, limit transfer width
	! to 32 bits. (space=0xB or 0xD)
	cmp	%i4, 0xB		! from user vme-d32
	be	.bcopy_vme32
	cmp	%i4, 0xD		! from supv vme-d32
	be	.bcopy_vme32

	and	%i1, MMU_PAGEMASK, %i3
	or	%i3, FT_ALL<<8, %i3
	lda	[%i3]ASI_FLPR, %i4	! get destination PTE
	and	%i4, 3, %i4
	cmp	%i4, MMU_ET_PTE
	bne,a	1f			! if not mapped in,
	ldub	[%i1], %g0		!   force destination page into memory
1:	lda	[%i3]ASI_FLPR, %i4	!   and get destination PTE again
	srl	%i4, 28, %i4		! convert pte to space

	! if destination is vme D16, limit transfer width
	! to 16 bits. (space=0xA or 0xC)
	cmp	%i4, 0xA		! to user vme-d16
	be	.bcopy_vme16
	cmp	%i4, 0xC		! to supv vme-d16
	be	.bcopy_vme16

	! if destination is vme D32, limit transfer width
	! to 32 bits. (space=0xB or 0xD)
	cmp	%i4, 0xB		! to user vme-d32
	be	.bcopy_vme32
	cmp	%i4, 0xD		! to supv vme-d32
	be	.bcopy_vme32
	nop

	!
	! If MXCC present, use block copy when:
        !   src and dest are page aligned;  length >= 1 page 
	!   src and/or dest are not cacheABLE
        !
	! Disabling interrupts prevents rescheduling locally, and blocks
	! cross processor interrupts necessary for demapping.  So
	! page stealing can not happen during mxcc block copies.
	! Interrupts are disabled/enabled on a page by page basis.
	!
        ! An mmu probe "entire" is used to insure we have valid src and  
        ! dest pages after interrupts are disabled; and to obtain the
	! src and dest physical addresses.
	!
.bcopy_obmem:
        set     mxcc, %o3
        ld      [%o3], %o3
        cmp     %o3, %g0 
        be      .bcopy_nb
        nop


	set	0x1000, %o0
        cmp     %i2, %o0        ! skip if too small 
        blt     .bcopy_nb                
        nop

	andcc	%i0, MMU_PAGEOFFSET, %g0
	bne	.bcopy_nb		! skip if src not page aligned
	nop

	andcc	%i1, MMU_PAGEOFFSET, %g0
	bne	.bcopy_nb		! skip if dest not page aligned
	nop

        ld      [THREAD_REG + T_CPU], %i5
        ldstub  [%i5 + BCOPY_RES], %o0  ! reserve mxcc block copy 
        tst     %o0
        bnz     .bcopy_nb               ! reserved already or not enabled
	nop

        set     MXCC_STRM_SRC, %l0      ! addr of mxcc stream source reg
        set     MXCC_STRM_DEST, %l1     ! addr of mxcc stream destination reg

	
.bc_pagetop:
        ldub    [%i0], %o4              ! Set R bit on src page
        stb     %o4, [%i1]              ! Set RM bits in dest page

	call	spl8			! prevent page stealing
					! spl8 raises priority to the
					! highest level (15)
	nop
	mov	%o0, %l7		! save old spl

	mov     %i0, %o0
        call    mmu_probe               ! probe src
        mov     %g0, %o1
        cmp     %o0, 0
        be      .bc_pagend              ! if invalid, start over
        mov     %o0, %i3

        mov     %i1, %o0
        call    mmu_probe               ! probe dest
        mov     %g0, %o1
        cmp     %o0, 0
        be      .bc_pagend              ! if invalid, start over
        mov     %o0, %i4

        btst    PTE_CACHEABLEMASK, %i3  ! src page marked cacheable?
        bz	1f			! if no, use mxcc 
	nop
        btst    PTE_CACHEABLEMASK, %i4  ! dest page marked cacheable?
        bnz	.bc_soft		! if no, use mxcc 
	nop
1:
	srl     %i3, 28, %l2            ! source space
        btst    PTE_CACHEABLEMASK, %i3  ! page marked cacheable?
        bnz,a   1f
        or      %l2, 0x10, %l2          ! or in cacheable bit
1:
        srl     %i3, 8, %l3             ! remove non-page bits from pte
        sll     %l3, MMU_PAGESHIFT, %l3 ! phys page minus space bits
        and     %i0, MMU_PAGEOFFSET, %o0 ! page offset via virt addr
        or      %l3, %o0, %l3           ! source phys addr complete
 
        srl     %i4, 28, %l4            ! destination space
        btst    PTE_CACHEABLEMASK, %i4  ! page marked cacheable?
        bnz,a   1f
        or      %l4, 0x10, %l4          ! or in cacheable bit
1:
        srl     %i4, 8, %l5             ! remove non-page bits from pte
        sll     %l5, MMU_PAGESHIFT, %l5 ! phys page minus space bits
        and     %i1, MMU_PAGEOFFSET, %o0 ! page offset via virt addr
        or      %l5, %o0, %l5           ! destination phys addr complete

	set	MMU_PAGESIZE, %o5
	add     %i0, %o5, %i0           ! adjust src
        add     %i1, %o5, %i1           ! adjust dst
        sub     %i2, %o5, %i2           ! adjust len
 
.bc_mxcc_loop:
        stda    %l2, [%l0]ASI_MXCC
        stda    %l4, [%l1]ASI_MXCC
        deccc   BCPY_BLKSZ, %o5         ! decrement count
        inc     BCPY_BLKSZ, %l3         ! increment phys addr
        bnz     .bc_mxcc_loop
        inc     BCPY_BLKSZ, %l5         ! increment phys addr
 
.bc_pagend:
	call	splx			! restore ipl 
	mov	%l7, %o0

	set	MMU_PAGESIZE, %o5
	cmp	%i2, %o5
	bge	.bc_pagetop
        nop

.bc_chk_dst:
	ldda	[%l1]ASI_MXCC, %l4	! wait for RDY in stream dest reg
	cmp	%l4, %g0
	bge	.bc_chk_dst
	nop

        set     MXCC_ERROR, %o5         ! Load addr of MXCC err reg
        ldda    [%o5]ASI_MXCC, %l4      ! %l5 hold bits 0-31 of paddr
        set     MXCC_ERR_AE, %o4
        btst    %o4, %l4                ! %l4 hold the status bits
        bz      .bc_pgsdone            ! If bit not set continue exit
	nop

        set     MXCC_ERR_EV, %i4        ! If error bit set...
        btst    %i4, %l4                !    check if valid bit set
        bz      .bc_pgsdone 
	nop

        set     0f, %o0
        call    panic
        nop
0:
        .asciz  "bcopy stream operation failed"
        .align  4


.bc_soft:
	call	splx			! restore ipl 
	mov	%l7, %o0

.bc_pgsdone:
	st	%g0, [%i5 + BCOPY_RES]	! unreserve mxcc block copy

	tst	%i2
	be	.cpdone
	nop	


	!
	! use aligned transfers where possible
	!
.bcopy_nb:
	xor	%i0, %i1, %o4		! xor from and to address
	btst	7, %o4			! if lower three bits zero
	bz	.aldoubcp		! can align on double boundary
	.empty	! assembler complaints about label

.bcopy_vme32:
.bcopy_words:
	xor	%i0, %i1, %o4		! xor from and to address
	btst	3, %o4			! if lower two bits zero
	bz	.alwordcp		! can align on word boundary
	btst	3, %i0			! delay slot, from address unaligned?
	!
	! use aligned reads and writes where possible
	! this differs from wordcp in that it copes
	! with odd alignment between source and destnation
	! using word reads and writes with the proper shifts
	! in between to align transfers to and from memory
	! i0 - src address, i1 - dest address, i2 - count
	! i3, i4 - tmps for used generating complete word
	! i5 (word to write)
	! l0 size in bits of upper part of source word (US)
	! l1 size in bits of lower part of source word (LS = 32 - US)
	! l2 size in bits of upper part of destination word (UD)
	! l3 size in bits of lower part of destination word (LD = 32 - UD)
	! l4 number of bytes leftover after aligned transfers complete
	! l5 the number 32
	!
	mov	32, %l5			! load an oft-needed constant
	bz	.align_dst_only
	btst	3, %i1			! is destnation address aligned?
	clr	%i4			! clear registers used in either case
	bz	.align_src_only
	clr	%l0
	!
	! both source and destination addresses are unaligned
	!
1:					! align source
	ldub	[%i0], %i3		! read a byte from source address
	add	%i0, 1, %i0		! increment source address
	or	%i4, %i3, %i4		! or in with previous bytes (if any)
	btst	3, %i0			! is source aligned?
	add	%l0, 8, %l0		! increment size of upper source (US)
	bnz,a	1b
	sll	%i4, 8, %i4		! make room for next byte

	sub	%l5, %l0, %l1		! generate shift left count (LS)
	sll	%i4, %l1, %i4		! prepare to get rest
	ld	[%i0], %i3		! read a word
	add	%i0, 4, %i0		! increment source address
	srl	%i3, %l0, %i5		! upper src bits into lower dst bits
	or	%i4, %i5, %i5		! merge
	mov	24, %l3			! align destination
1:
	srl	%i5, %l3, %i4		! prepare to write a single byte
	stb	%i4, [%i1]		! write a byte
	add	%i1, 1, %i1		! increment destination address
	sub	%i2, 1, %i2		! decrement count
	btst	3, %i1			! is destination aligned?
	bnz,a	1b
	sub	%l3, 8, %l3		! delay slot, decrement shift count (LD)
	sub	%l5, %l3, %l2		! generate shift left count (UD)
	sll	%i5, %l2, %i5		! move leftover into upper bytes
	cmp	%l2, %l0		! cmp # req'd to fill dst w old src left
	bg	.more_needed		! need more to fill than we have
	nop

	sll	%i3, %l1, %i3		! clear upper used byte(s)
	srl	%i3, %l1, %i3
	! get the odd bytes between alignments
	sub	%l0, %l2, %l0		! regenerate shift count
	sub	%l5, %l0, %l1		! generate new shift left count (LS)
	and	%i2, 3, %l4		! must do remaining bytes if count%4 > 0
	andn	%i2, 3, %i2		! # of aligned bytes that can be moved
	srl	%i3, %l0, %i4
	or	%i5, %i4, %i5
	st	%i5, [%i1]		! write a word
	subcc	%i2, 4, %i2		! decrement count
	bz	.unalign_out
	add	%i1, 4, %i1		! increment destination address

	b	2f
	sll	%i3, %l1, %i5		! get leftover into upper bits
.more_needed:
	sll	%i3, %l0, %i3		! save remaining byte(s)
	srl	%i3, %l0, %i3
	sub	%l2, %l0, %l1		! regenerate shift count
	sub	%l5, %l1, %l0		! generate new shift left count
	sll	%i3, %l1, %i4		! move to fill empty space
	b	3f
	or	%i5, %i4, %i5		! merge to complete word
	!
	! the source address is aligned and destination is not
	!
.align_dst_only:
	ld	[%i0], %i4		! read a word
	add	%i0, 4, %i0		! increment source address
	mov	24, %l0			! initial shift alignment count
1:
	srl	%i4, %l0, %i3		! prepare to write a single byte
	stb	%i3, [%i1]		! write a byte
	add	%i1, 1, %i1		! increment destination address
	sub	%i2, 1, %i2		! decrement count
	btst	3, %i1			! is destination aligned?
	bnz,a	1b
	sub	%l0, 8, %l0		! delay slot, decrement shift count
.xfer:
	sub	%l5, %l0, %l1		! generate shift left count
	sll	%i4, %l1, %i5		! get leftover
3:
	and	%i2, 3, %l4		! must do remaining bytes if count%4 > 0
	andn	%i2, 3, %i2		! # of aligned bytes that can be moved
2:
	ld	[%i0], %i3		! read a source word
	add	%i0, 4, %i0		! increment source address
	srl	%i3, %l0, %i4		! upper src bits into lower dst bits
	or	%i5, %i4, %i5		! merge with upper dest bits (leftover)
	st	%i5, [%i1]		! write a destination word
	subcc	%i2, 4, %i2		! decrement count
	bz	.unalign_out		! check if done
	add	%i1, 4, %i1		! increment destination address
	b	2b			! loop
	sll	%i3, %l1, %i5		! get leftover
.unalign_out:
	tst	%l4			! any bytes leftover?
	bz	.cpdone
	.empty				! allow next instruction in delay slot
1:
	sub	%l0, 8, %l0		! decrement shift
	srl	%i3, %l0, %i4		! upper src byte into lower dst byte
	stb	%i4, [%i1]		! write a byte
	subcc	%l4, 1, %l4		! decrement count
	bz	.cpdone			! done?
	add	%i1, 1, %i1		! increment destination
	tst	%l0			! any more previously read bytes
	bnz	1b			! we have leftover bytes
	mov	%l4, %i2		! delay slot, mv cnt where dbytecp wants
	b	.dbytecp		! let dbytecp do the rest
	sub	%i0, %i1, %i0		! i0 gets the difference of src and dst
	!
	! the destination address is aligned and the source is not
	!
.align_src_only:
	ldub	[%i0], %i3		! read a byte from source address
	add	%i0, 1, %i0		! increment source address
	or	%i4, %i3, %i4		! or in with previous bytes (if any)
	btst	3, %i0			! is source aligned?
	add	%l0, 8, %l0		! increment shift count (US)
	bnz,a	.align_src_only
	sll	%i4, 8, %i4		! make room for next byte
	b,a	.xfer
	!
	! if from address unaligned for double-word moves,
	! move bytes till it is, if count is < 56 it could take
	! longer to align the thing than to do the transfer
	! in word size chunks right away
	!
.aldoubcp:
	cmp	%i2, 56			! if count < 56, use wordcp, it takes
	bl,a	.alwordcp		! longer to align doubles than words
	mov	3, %o0			! mask for word alignment
	call	.alignit		! copy bytes until aligned
	mov	7, %o0			! mask for double alignment
	!
	! source and destination are now double-word aligned
	! see if transfer is large enough to gain by loop unrolling
	!
	cmp	%i2, 512		! if less than 512 bytes
	bge,a	.blkcopy		! just copy double-words (overwrite i3)
	mov	0x100, %i3		! blk copy chunk size for unrolled loop
	!
	! i3 has aligned count returned by alignit
	!
	and	%i2, 7, %i2		! unaligned leftover count
	sub	%i0, %i1, %i0		! i0 gets the difference of src and dst
5:
	ldd	[%i0+%i1], %o4		! read from address
	std	%o4, [%i1]		! write at destination address
	subcc	%i3, 8, %i3		! dec count
	bg	5b
	add	%i1, 8, %i1		! delay slot, inc to address
.wcpchk:
	cmp	%i2, 4			! see if we can copy a word
	bl	.dbytecp		! if 3 or less bytes use bytecp
	.empty
	!
	! for leftover bytes we fall into wordcp, if needed
	!
.wordcp:
	and	%i2, 3, %i2		! unaligned leftover count
5:
	ld	[%i0+%i1], %o4		! read from address
	st	%o4, [%i1]		! write at destination address
	subcc	%i3, 4, %i3		! dec count
	bg	5b
	add	%i1, 4, %i1		! delay slot, inc to address
	b,a	.dbytecp

	! we come here to align copies on word boundaries
.alwordcp:
	call	.alignit		! go word-align it
	mov	3, %o0			! bits that must be zero to be aligned
	b	.wordcp
	sub	%i0, %i1, %i0		! i0 gets the difference of src and dst

	!
	! byte copy, works with any alignment
	!
.bytecp:
	b	.dbytecp
	sub	%i0, %i1, %i0		! i0 gets difference of src and dst

	!
	! differenced byte copy, works with any alignment
	! assumes dest in %i1 and (source - dest) in %i0
	!
1:
	stb	%o4, [%i1]		! write to address
	inc	%i1			! inc to address
.dbytecp:
	deccc	%i2			! dec count
	bge,a	1b			! loop till done
	ldub	[%i0+%i1], %o4		! read from address
.cpdone:
	st	%l6, [THREAD_REG + T_LOFAULT]	! restore old t_lofault
	TRACE_ASM_0 (%o2, TR_FAC_BCOPY, TR_COPY_END, TR_copy_end)
	ret
	restore %g0, 0, %o0		! return (0)

/*
 * Common code used to align transfers on word and doubleword
 * boudaries.  Aligns source and destination and returns a count
 * of aligned bytes to transfer in %i3
 */
1:
	inc	%i0			! inc from
	stb	%o4, [%i1]		! write a byte
	inc	%i1			! inc to
	dec	%i2			! dec count
.alignit:
	btst	%o0, %i0		! %o0 is bit mask to check for alignment
	bnz,a	1b
	ldub	[%i0], %o4		! read next byte

	retl
	andn	%i2, %o0, %i3		! return size of aligned bytes
	SET_SIZE(bcopy)

#endif	/* lint */

#if defined(lint)

void
flush_icache(void)
{}

#else	/* lint */

	ENTRY(flush_icache)
	sta	%g0, [%g0]ASI_ICFCLR
	retl
	nop
	SET_SIZE(flush_icache)

#endif	/* lint */
/*
 * Copy a page of memory.
 * Assumes double word alignment and a count >= 256.
 * locked_pgcopy assumes pages are locked (can use copy hw if available).
 *
 * locked_pgcopy(from, to, count)
 *	caddr_t from, to;
 *	size_t count;
 */

#if defined(lint)

/* ARGSUSED */

/* ARGSUSED */
void
locked_pgcopy(caddr_t from, caddr_t to, size_t count)
{} 

#else	/* lint */

	ENTRY(locked_pgcopy)

	TRACE_IN(TR_PGCOPY_START, TR_pgcopy_start)

	save	%sp, -SA(MINFRAME), %sp	! get another window
	ld	[THREAD_REG + T_LOFAULT], %l6	! save t_lofault, for .cpdone

#ifdef	BCOPY_BUF
	!
	! %i0 - src
	! %i1 - dst
	! %i2 - len
	! %i3 - src pte
	! %i4 - dst pte
	! %i5 - cpu pointer
	! %l0 - mxcc stream src
	! %l1 - mxcc stream dst
	! %l2, %l3 - stream src data
	! %l4, %l5 - stream dst data
	! %l6 - t_lofault
	! %l7 - copy loop counter
	! %o* - temps
	!
bcp_bufchk:
	ld	[THREAD_REG + T_CPU], %i5
	ld	[%i5 + BCOPY_RES], %o0	! is hardware available
	tst	%o0
	bnz	.bcp_nobuf		! reserved or not enabled
	cmp	%i2, BCOPY_LIMIT	! see if it is worth while
	blt	.bcp_nobuf		! if not, punt

	xor	%i0, %i1, %o1		! check alignment of src and dest
	btst	0x1F, %o1
	bnz	.bcp_nobuf		! not aligned, can't use hardware
	btst	0x1F, %i0		! check src alignment
	bz	.bcp_buf		! if src is aligned, then so is dest
	nop				! if not, copy doubles until aligned
1:
	ldd	[%i0], %o0		! align to cache
	std	%o0, [%i1]
	sub	%i2, 8, %i2		! update count
	add	%i0, 8, %i0		! update source address
	btst	0x1F, %i0
	bnz	1b
	add	%i1, 8, %i1		! update dest address

.bcp_buf:
	ldstub	[%i5 + BCOPY_RES], %o3	! try to grab hardware
	tst	%o3
	bnz	.bcp_nobuf		! hardware in use, use software
	nop
!
! This is where we go if we are using HW bcopy
! but it isnt a viking/mxcc
!
	set	virtual_bcopy, %o3
	ld	[%o3], %o3
	cmp	%o3, %g0
	bz	.try_phys_bcopy
	nop

1:
! Attempt store to destination address to force page fault or other 
! exceptions to be handled.

! This is needed for a Ross625 A0-A3 bug where it's possible
! for the hardware bcopy to write into the destination if the destination
! is write protected under some circumstances.
	stb	%o4, [%i1]		! Try to write into dest page

	sta	%i1, [%i0]ASI_BC	! write buffer
	sub	%i2, BCPY_BLKSZ, %i2		! update count
	add	%i0, BCPY_BLKSZ, %i0		! update source address
	cmp	%i2, BCPY_BLKSZ		! check if finished
	bge	1b			! loop until done
	add	%i1, BCPY_BLKSZ, %i1		! update dest address
	b	.bytecp			! do remaining bytes, if any
	st	%g0, [%i5 + BCOPY_RES] ! interlock, unlock hardware

.try_phys_bcopy:

!

	set	MXCC_STRM_SRC, %l0	! addr of stream source reg
	set	MXCC_STRM_DEST, %l1	! addr of stream destination reg

.hw_bcopytop:
	ldub	[%i0], %o4		! Set R bit on src page
	stb	%o4, [%i1]		! Set RM bits in dest page

	!
	! Probe src and dest.
	! We must use mmu_probe() since ASI_FLPR doesn't do what we want.
	!
	mov	%i0, %o0
	call	mmu_probe		! probe src
	mov	%g0, %o1
	mov	%o0, %i3

	mov	%i1, %o0
	call	mmu_probe		! probe dst
	mov	%g0, %o1
	mov	%o0, %i4

	srl	%i3, 28, %l2		! source space
	btst	PTE_CACHEABLEMASK, %i3	! page marked cacheable?
	bnz,a	1f
	or	%l2, 0x10, %l2		! or in cacheable bit
1:
	srl	%i3, 8, %l3		! remove non-page bits from pte
	sll	%l3, MMU_PAGESHIFT, %l3	! phys page minus space bits
	and	%i0, MMU_PAGEOFFSET, %o0 ! page offset via virt addr
	or	%l3, %o0, %l3		! source phys addr complete

	srl	%i4, 28, %l4		! destination space
	btst	PTE_CACHEABLEMASK, %i4	! page marked cacheable?
	bnz,a	1f
	or	%l4, 0x10, %l4		! or in cacheable bit
1:
	srl	%i4, 8, %l5		! remove non-page bits from pte
	sll	%l5, MMU_PAGESHIFT, %l5	! phys page minus space bits
	and	%i1, MMU_PAGEOFFSET, %o0 ! page offset via virt addr
	or	%l5, %o0, %l5		! destination phys addr complete

	!
	! Calculate amount to copy in this iteration.
	! min(len, pagesize - pageoff(src), pagesize - pageoff(dst))
	!
	mov	%i2, %l7
	set	MMU_PAGESIZE, %o2
	and	%i0, MMU_PAGEOFFSET, %o0
	sub	%o2, %o0, %o0
	and	%i1, MMU_PAGEOFFSET, %o1
	sub	%o2, %o1, %o1
	cmp	%o0, %o1
	bg,a	1f
	mov	%o1, %o0
1:
	cmp	%l7, %o0
	bg,a	1f
	mov	%o0, %l7
1:
	andn	%l7, 0x1F, %l7		! round down to multiple of blksz
	add	%i0, %l7, %i0		! adjust src
	add	%i1, %l7, %i1		! adjust dst
	sub	%i2, %l7, %i2		! adjust len

.hw_bcopyloop:
	stda	%l2, [%l0]ASI_MXCC
	stda	%l4, [%l1]ASI_MXCC
	deccc	BCPY_BLKSZ, %l7		! decrement count
	inc	BCPY_BLKSZ, %l3		! increment phys addr
	bnz	.hw_bcopyloop
	inc	BCPY_BLKSZ, %l5		! increment phys addr

	cmp	%i2, BCOPY_LIMIT	! go around again
	bge	.hw_bcopytop
	nop

!
! Check Error Register to see if AE bit set, indicating problem with
! stream operation we just did.  Fatal, so we go down if a problem
! is found.
!
.hw_bcopyexit:
	ldda	[%l1]ASI_MXCC, %l4	! wait for RDY bit in stream dest reg
	cmp	%l4, %g0
	bge	.hw_bcopyexit
	nop

	set	MXCC_ERROR, %l7		! Load addr of MXCC err reg
	ldda	[%l7]ASI_MXCC, %l4	! %l5 hold bits 0-31 of paddr
	set	MXCC_ERR_AE, %o4		
	btst	%o4, %l4		! %l4 hold the status bits
	bz	1f			! If bit not set continue exit
	nop

	set	MXCC_ERR_EV, %i4	! If error bit set...
	btst	%i4, %l4		!    check if valid bit set
	bnz	6f
	.empty				! Silence complaint about label
1:
	st	%g0, [%i5 + BCOPY_RES]	! unlock hardware
2:
	cmp	%i2, 8
	bl	.bytecp			! do remaining bytes, if any
	nop
	ldd	[%i0], %o0		! copy doubles
	add	%i0, 8, %i0		! update source address
	sub	%i2, 8, %i2		! update count
	std	%o0, [%i1]
	b	2b
	add	%i1, 8, %i1		! update dest address

6:
	set	0f, %o0
	call	panic
	nop
0:
	.asciz	"bcopy stream operation failed"
	.align	4

.bcp_nobuf:
	mov	0x100, %i3		! blk copy chunk size for unrolled loop
#endif	/* BCOPY_BUF */

.blkcopy:
	sethi	%hi(tsunami), %o4
	ld	[%o4 + %lo(tsunami)], %o4
	tst	%o4
	bnz	.blkcopy_tsunami
	nop

	sethi	%hi(viking), %o4
	ld	[%o4 + %lo(viking)], %o4
	tst	%o4
	beq	.blkcopy_ross
	nop

.blkcopy_viking:
	ldd	[%i0+0x00], %o0;	ldd	[%i0+0x08], %o2
	ldd	[%i0+0x10], %l0;	ldd	[%i0+0x18], %l2
	std	%o0, [%i1+0x00];	std	%o2, [%i1+0x08]
	std	%l0, [%i1+0x10];	std	%l2, [%i1+0x18]

	ldd	[%i0+0x20], %o0;	ldd	[%i0+0x28], %o2
	ldd	[%i0+0x30], %l0;	ldd	[%i0+0x38], %l2
	std	%o0, [%i1+0x20];	std	%o2, [%i1+0x28]
	std	%l0, [%i1+0x30];	std	%l2, [%i1+0x38]

	ldd	[%i0+0x40], %o0;	ldd	[%i0+0x48], %o2
	ldd	[%i0+0x50], %l0;	ldd	[%i0+0x58], %l2
	std	%o0, [%i1+0x40];	std	%o2, [%i1+0x48]
	std	%l0, [%i1+0x50];	std	%l2, [%i1+0x58]

	ldd	[%i0+0x60], %o0;	ldd	[%i0+0x68], %o2
	ldd	[%i0+0x70], %l0;	ldd	[%i0+0x78], %l2
	std	%o0, [%i1+0x60];	std	%o2, [%i1+0x68]
	std	%l0, [%i1+0x70];	std	%l2, [%i1+0x78]

	ldd	[%i0+0x80], %o0;	ldd	[%i0+0x88], %o2
	ldd	[%i0+0x90], %l0;	ldd	[%i0+0x98], %l2
	std	%o0, [%i1+0x80];	std	%o2, [%i1+0x88]
	std	%l0, [%i1+0x90];	std	%l2, [%i1+0x98]

	ldd	[%i0+0xa0], %o0;	ldd	[%i0+0xa8], %o2
	ldd	[%i0+0xb0], %l0;	ldd	[%i0+0xb8], %l2
	std	%o0, [%i1+0xa0];	std	%o2, [%i1+0xa8]
	std	%l0, [%i1+0xb0];	std	%l2, [%i1+0xb8]

	ldd	[%i0+0xc0], %o0;	ldd	[%i0+0xc8], %o2
	ldd	[%i0+0xd0], %l0;	ldd	[%i0+0xd8], %l2
	std	%o0, [%i1+0xc0];	std	%o2, [%i1+0xc8]
	std	%l0, [%i1+0xd0];	std	%l2, [%i1+0xd8]

	ldd	[%i0+0xe0], %o0;	ldd	[%i0+0xe8], %o2
	ldd	[%i0+0xf0], %l0;	ldd	[%i0+0xf8], %l2
	std	%o0, [%i1+0xe0];	std	%o2, [%i1+0xe8]
	std	%l0, [%i1+0xf0];	std	%l2, [%i1+0xf8]

.instr_viking:
	sub	%i2, %i3, %i2		! decrement count
	add	%i0, %i3, %i0		! increment from address
	cmp	%i2, 0x100		! enough to do another block?
	bge	.blkcopy_viking		! yes, do another chunk
	add	%i1, %i3, %i1		! increment to address
	tst	%i2			! all done yet?
	ble	.cpdone			! yes, return
	cmp	%i2, 15			! can we do more cache lines
	bg,a	1f
	andn	%i2, 15, %i3		! %i3 bytes left, aligned (to 16 bytes)
	andn	%i2, 3, %i3		! %i3 bytes left, aligned to 4 bytes
	b	.wcpchk
	sub	%i0, %i1, %i0		! create diff of src and dest addr
1:
	!
	! we can't use viking code beauce we're jumping into the middle of
	! loop and the above code marches forward into the cache line whereas
	! we want to march backward, so we use the ross code.
	!
	set	.instr_ross, %o5	! address of copy instructions
	sub	%o5, %i3, %o5		! jmp address relative to instr
	jmp	%o5
	nop

.blkcopy_tsunami:
	ldd	[%i0+0xf8], %l0;	std	%l0, [%i1+0xf8]
	ldd	[%i0+0xf0], %l2;	std	%l2, [%i1+0xf0]
	ldd	[%i0+0xe8], %l0;	std	%l0, [%i1+0xe8]
	ldd	[%i0+0xe0], %l2;	std	%l2, [%i1+0xe0]

	ldd	[%i0+0xd8], %l0;	std	%l0, [%i1+0xd8]
	ldd	[%i0+0xd0], %l2;	std	%l2, [%i1+0xd0]
	ldd	[%i0+0xc8], %l0;	std	%l0, [%i1+0xc8]
	ldd	[%i0+0xc0], %l2;	std	%l2, [%i1+0xc0]

	ldd	[%i0+0xb8], %l0;	std	%l0, [%i1+0xb8]
	ldd	[%i0+0xb0], %l2;	std	%l2, [%i1+0xb0]
	ldd	[%i0+0xa8], %l0;	std	%l0, [%i1+0xa8]
	ldd	[%i0+0xa0], %l2;	std	%l2, [%i1+0xa0]

	ldd	[%i0+0x98], %l0;	std	%l0, [%i1+0x98]
	ldd	[%i0+0x90], %l2;	std	%l2, [%i1+0x90]
	ldd	[%i0+0x88], %l0;	std	%l0, [%i1+0x88]
	ldd	[%i0+0x80], %l2;	std	%l2, [%i1+0x80]

	ldd	[%i0+0x78], %l0;	std	%l0, [%i1+0x78]
	ldd	[%i0+0x70], %l2;	std	%l2, [%i1+0x70]
	ldd	[%i0+0x68], %l0;	std	%l0, [%i1+0x68]
	ldd	[%i0+0x60], %l2;	std	%l2, [%i1+0x60]

	ldd	[%i0+0x58], %l0;	std	%l0, [%i1+0x58]
	ldd	[%i0+0x50], %l2;	std	%l2, [%i1+0x50]
	ldd	[%i0+0x48], %l0;	std	%l0, [%i1+0x48]
	ldd	[%i0+0x40], %l2;	std	%l2, [%i1+0x40]

	ldd	[%i0+0x38], %l0;	std	%l0, [%i1+0x38]
	ldd	[%i0+0x30], %l2;	std	%l2, [%i1+0x30]
	ldd	[%i0+0x28], %l0;	std	%l0, [%i1+0x28]
	ldd	[%i0+0x20], %l2;	std	%l2, [%i1+0x20]

	ldd	[%i0+0x18], %l0;	std	%l0, [%i1+0x18]
	ldd	[%i0+0x10], %l2;	std	%l2, [%i1+0x10]
	ldd	[%i0+0x08], %l0;	std	%l0, [%i1+0x08]
	ldd	[%i0+0x00], %l2;	std	%l2, [%i1+0x00]

.instr_tsunami:
	sub	%i2, %i3, %i2		! decrement count
	add	%i0, %i3, %i0		! increment from address
	cmp	%i2, 0x100		! enough to do another block?
	bge	.blkcopy_tsunami	! yes, do another chunk
	add	%i1, %i3, %i1		! increment to address
	tst	%i2			! all done yet?
	ble	.cpdone			! yes, return
	cmp	%i2, 15			! can we do more cache lines
	bg,a	1f
	andn	%i2, 15, %i3		! %i3 bytes left, aligned (to 16 bytes)
	andn	%i2, 3, %i3		! %i3 bytes left, aligned to 4 bytes
	b	.wcpchk
	sub	%i0, %i1, %i0		! create diff of src and dest addr
1:
	set	.instr_tsunami, %o5	! address of copy instructions
	sub	%o5, %i3, %o5		! jmp address relative to instr
	jmp	%o5
	nop

.blkcopy_ross:
	!
	! loops have been unrolled so that 64 instructions(16 cache-lines)
	! are used; 256 bytes are moved each time through the loop
	! i0 - from; i1 - to; i2 - count; i3 - chunksize; o4,o5 -tmp
	!
	! We read a whole cache line and then we write it to
	! minimize thrashing.
	!
	ldd	[%i0+0xf8], %l0;	ldd	[%i0+0xf0], %l2
	std	%l0, [%i1+0xf8];	std	%l2, [%i1+0xf0]
	ldd	[%i0+0xe8], %l0;	ldd	[%i0+0xe0], %l2
	std	%l0, [%i1+0xe8];	std	%l2, [%i1+0xe0]

	ldd	[%i0+0xd8], %l0;	ldd	[%i0+0xd0], %l2
	std	%l0, [%i1+0xd8];	std	%l2, [%i1+0xd0]
	ldd	[%i0+0xc8], %l0;	ldd	[%i0+0xc0], %l2
	std	%l0, [%i1+0xc8];	std	%l2, [%i1+0xc0]

	ldd	[%i0+0xb8], %l0;	ldd	[%i0+0xb0], %l2
	std	%l0, [%i1+0xb8];	std	%l2, [%i1+0xb0]
	ldd	[%i0+0xa8], %l0;	ldd	[%i0+0xa0], %l2
	std	%l0, [%i1+0xa8];	std	%l2, [%i1+0xa0]

	ldd	[%i0+0x98], %l0;	ldd	[%i0+0x90], %l2
	std	%l0, [%i1+0x98];	std	%l2, [%i1+0x90]
	ldd	[%i0+0x88], %l0;	ldd	[%i0+0x80], %l2
	std	%l0, [%i1+0x88];	std	%l2, [%i1+0x80]

	ldd	[%i0+0x78], %l0;	ldd	[%i0+0x70], %l2
	std	%l0, [%i1+0x78];	std	%l2, [%i1+0x70]
	ldd	[%i0+0x68], %l0;	ldd	[%i0+0x60], %l2
	std	%l0, [%i1+0x68];	std	%l2, [%i1+0x60]

	ldd	[%i0+0x58], %l0;	ldd	[%i0+0x50], %l2
	std	%l0, [%i1+0x58];	std	%l2, [%i1+0x50]
	ldd	[%i0+0x48], %l0;	ldd	[%i0+0x40], %l2
	std	%l0, [%i1+0x48];	std	%l2, [%i1+0x40]

	ldd	[%i0+0x38], %l0;	ldd	[%i0+0x30], %l2
	std	%l0, [%i1+0x38];	std	%l2, [%i1+0x30]
	ldd	[%i0+0x28], %l0;	ldd	[%i0+0x20], %l2
	std	%l0, [%i1+0x28];	std	%l2, [%i1+0x20]

	ldd	[%i0+0x18], %l0;	ldd	[%i0+0x10], %l2
	std	%l0, [%i1+0x18];	std	%l2, [%i1+0x10]
	ldd	[%i0+0x08], %l0;	ldd	[%i0+0x00], %l2
	std	%l0, [%i1+0x08];	std	%l2, [%i1+0x00]

.instr_ross:
	sub	%i2, %i3, %i2		! decrement count
	add	%i0, %i3, %i0		! increment from address
	cmp	%i2, 0x100		! enough to do another block?
	bge	.blkcopy_ross		! yes, do another chunk
	add	%i1, %i3, %i1		! increment to address
	tst	%i2			! all done yet?
	ble	.cpdone			! yes, return
	cmp	%i2, 15			! can we do more cache lines
	bg,a	1f
	andn	%i2, 15, %i3		! %i3 bytes left, aligned (to 16 bytes)
	andn	%i2, 3, %i3		! %i3 bytes left, aligned to 4 bytes
	b	.wcpchk
	sub	%i0, %i1, %i0		! create diff of src and dest addr
1:
	set	.instr_ross, %o5	! address of copy instructions
	sub	%o5, %i3, %o5		! jmp address relative to instr
	jmp	%o5
	nop
	SET_SIZE(locked_pgcopy)

#endif	/* lint */

/*
 * Block copy with possibly overlapped operands.
 *
 * ovbcopy(from, to, count)
 *	char *from, *to;
 *	size_t count;
 */

#if defined(lint)

/* ARGSUSED */
void
ovbcopy(char * from, char * to, u_int count)
{}

#else	/* lint */

	ENTRY(ovbcopy)
	tst	%o2			! check count
	bg,a	1f			! nothing to do or bad arguments
	subcc	%o0, %o1, %o3		! difference of from and to address

	retl				! return
	nop
1:
	bneg,a	2f
	neg	%o3			! if < 0, make it positive
2:	cmp	%o2, %o3		! cmp size and abs(from - to)
	ble	bcopy			! if size <= abs(diff): use bcopy,
	.empty				!   no overlap
	cmp	%o0, %o1		! compare from and to addresses
	blu	.ov_bkwd		! if from < to, copy backwards
	nop
	!
	! Copy forwards.
	!
.ov_fwd:
	ldub	[%o0], %o3		! read from address
	inc	%o0			! inc from address
	stb	%o3, [%o1]		! write to address
	deccc	%o2			! dec count
	bg	.ov_fwd			! loop till done
	inc	%o1			! inc to address

	retl				! return
	nop
	!
	! Copy backwards.
	!
.ov_bkwd:
	deccc	%o2			! dec count
	ldub	[%o0 + %o2], %o3	! get byte at end of src
	bg	.ov_bkwd		! loop till done
	stb	%o3, [%o1 + %o2]	! delay slot, store at end of dst

	retl				! return
	nop
	SET_SIZE(ovbcopy)

#endif	/* lint */

/*
 * Zero a block of storage, returning an error code if we
 * take a kernel pagefault which cannot be resolved.
 * Returns errno value on pagefault error, 0 if all ok
 *
 * int
 * kzero(addr, count)
 *	caddr_t addr;
 *	size_t count;
 */

#if defined(lint)

/* ARGSUSED */
int
kzero(caddr_t addr, size_t count)
{ return(0); }

#else	/* lint */

	ENTRY(kzero)
#ifdef DEBUG
	sethi   %hi(KERNELBASE), %o2
	cmp     %o0, %o2
	bgeu    3f
	nop

	set     2f, %o0
	call    panic
	nop

2:      .asciz  "kzero: Argument is in user address space"
	.align 4

3:

#endif DEBUG

	sethi	%hi(.zeroerr), %o2
	b	.do_zero
	or	%o2, %lo(.zeroerr), %o2

/*
 * We got here because of a fault during kzero.
 * Errno value is in %g1.
 */
.zeroerr:
	st	%o5, [THREAD_REG + T_LOFAULT]	! restore old t_lofault
	retl
	mov	%g1, %o0
	SET_SIZE(kzero)

#endif	/* lint */

/*
 * Zero a block of storage. (also known as blkclr)
 * locked_bzero assumes page is locked (can use hw assist)
 *
 * bzero(addr, length)
 *	caddr_t addr;
 *	size_t count;
 */

#if defined(lint)

/* ARGSUSED */
void
bzero(caddr_t addr, size_t count)
{}

/* ARGSUSED */
void
blkclr(caddr_t addr, size_t count)
{}

/* ARGSUSED */
void
locked_bzero(caddr_t addr, size_t count)
{}

#else	/* lint */

	ENTRY2(bzero,blkclr)
#ifndef	BCOPY_BUF
	ALTENTRY(locked_bzero)
#endif

#ifdef DEBUG
        sethi   %hi(KERNELBASE), %o2
        cmp     %o0, %o2
        bgeu    3f
        nop

        set     2f, %o0
        call    panic
        nop

2:      .asciz  "bzero: Argument is in user address space"
        .align 4

3:

#endif DEBUG
	ld	[THREAD_REG + T_LOFAULT], %o2
.do_zero:
	ld	[THREAD_REG + T_LOFAULT], %o5	! save t_lofault
	cmp	%o1, 15			! check for small counts
	bl	.byteclr		! just clear bytes
	st	%o2, [THREAD_REG + T_LOFAULT]	! install new vector
	!
	! Check for word alignment.
	!
	btst	3, %o0
	bz	.bzero_probe
	mov	0x100, %o3		! constant size of main loop
	!
	!
	! clear bytes until word aligned
	!
1:	clrb	[%o0]
	add	%o0, 1, %o0
	btst	3, %o0
	bnz	1b
	sub	%o1, 1, %o1
.bzero_probe:
	sethi	%hi(vme), %o4		! don't bother with loading
	ld	[%o4 + %lo(vme)], %o4	! pte's on systems that don't
	tst	%o4			! have a vmebus
	beq	.bzero_obmem
	nop

	!
	! Word aligned.
	! Check page type to see if word/double access is 
	! allowed (i.e. not VME_D16|VME_D32)
	! This assumes that the destination will not change to 
	! VME_DXX during the bzero. This will get a data fault with
	! be_vmeserr set if unsuccessful. 
	!
	and	%o0, MMU_PAGEMASK, %o2
	or	%o2, FT_ALL<<8, %o2
	lda	[%o2]ASI_FLPR, %o4	! get target PTE
	and	%o4, 3, %g3
	cmp	%g3, MMU_ET_PTE
	be	1f			! if not mapped in,
	nop
	ldub	[%o0], %g0		!   force target page into memory
	lda	[%o2]ASI_FLPR, %o4	!   and get target PTE again.
1:	srl	%o4, 28, %o4		! convert pte to space

	! if target is vme D16, limit transfer width
	! to 16 bits. (space=0xA or 0xC)
	cmp	%o4, 0xA		! from user vme-d16
	be	.bzero_vme16
	cmp	%o4, 0xC		! from supv vme-d16
	be	.bzero_vme16

	! if target is vme D32, limit transfer width
	! to 32 bits. (space=0xB or 0xD)
	cmp	%o4, 0xB		! from user vme-d32
	be	.bzero_vme32
	cmp	%o4, 0xD		! from supv vme-d32
	be	.bzero_vme32
	nop

	!
	! obmem, if needed move a word to become double-word aligned.
	!
.bzero_obmem:
	btst	7, %o0			! is double aligned?
	bz	.bzero_nobuf
	clr	%g1			! clr g1 for second half of double %g0
	clr	[%o0]			! clr to double boundry
	sub	%o1, 4, %o1
	b	.bzero_nobuf
	add	%o0, 4, %o0

	!std	%g0, [%o0+0xf8]
.bzero_blk:
	std	%g0, [%o0+0xf0]
	std	%g0, [%o0+0xe8]
	std	%g0, [%o0+0xe0]
	std	%g0, [%o0+0xd8]
	std	%g0, [%o0+0xd0]
	std	%g0, [%o0+0xc8]
	std	%g0, [%o0+0xc0]
	std	%g0, [%o0+0xb8]
	std	%g0, [%o0+0xb0]
	std	%g0, [%o0+0xa8]
	std	%g0, [%o0+0xa0]
	std	%g0, [%o0+0x98]
	std	%g0, [%o0+0x90]
	std	%g0, [%o0+0x88]
	std	%g0, [%o0+0x80]
	std	%g0, [%o0+0x78]
	std	%g0, [%o0+0x70]
	std	%g0, [%o0+0x68]
	std	%g0, [%o0+0x60]
	std	%g0, [%o0+0x58]
	std	%g0, [%o0+0x50]
	std	%g0, [%o0+0x48]
	std	%g0, [%o0+0x40]
	std	%g0, [%o0+0x38]
	std	%g0, [%o0+0x30]
	std	%g0, [%o0+0x28]
	std	%g0, [%o0+0x20]
	std	%g0, [%o0+0x18]
	std	%g0, [%o0+0x10]
	std	%g0, [%o0+0x08]
	std	%g0, [%o0]
.zinst:
	add	%o0, %o3, %o0		! increment source address
	sub	%o1, %o3, %o1		! decrement count
.bzero_nobuf:
	cmp	%o1, 0x100		! can we do whole chunk?
	bge,a	.bzero_blk
	std	%g0, [%o0+0xf8]		! do first double of chunk

	cmp	%o1, 7			! can we zero any more double words
	ble	.byteclr		! too small go zero bytes

	andn	%o1, 7, %o3		! %o3 bytes left, double-word aligned
	srl	%o3, 1, %o2		! using doubles, need 1 instr / 2 words
	set	.zinst, %o4		! address of clr instructions
	sub	%o4, %o2, %o4		! jmp address relative to instr
	jmp	%o4
	nop
	!
	! do leftover bytes
	!
3:
	add	%o0, 1, %o0		! increment address
.byteclr:
	subcc	%o1, 1, %o1		! decrement count
	bge,a	3b
	clrb	[%o0]			! zero a byte

	st	%o5, [THREAD_REG + T_LOFAULT]	! restore old t_lofault
	retl
	mov	0, %o0			! return (0)

/*
 * bcopy_vme16(from, to, count)
 * Block copy to/from a 16 bit vme device.
 * There is little optimization because the VME is so slow.
 */
.bcopy_vme16:
	xor	%i0, %i1, %i4		! test for mutually half word aligned
	btst	1, %i4
	bnz	.bytecp			! misaligned, copy bytes
	.empty
	btst	1, %i0			! test for initial byte
	bz,a	1f
	andn	%i2, 1, %i3		! count of aligned bytes to clear

	ldub	[%i0], %i4		! copy initial byte
	add	%i0, 1, %i0
	stb	%i4, [%i1]
	add	%i1, 1, %i1
	sub	%i2, 1, %i2
	andn	%i2, 1, %i3		! count of aligned bytes to clear
1:
	and	%i2, 1, %i2		! unaligned leftover count
	sub	%i0, %i1, %i0		! i0 gets the difference
2:
	lduh	[%i0+%i1], %i4		! read from address
	sth	%i4, [%i1]		! write at destination address
	subcc	%i3, 2, %i3		! dec count
	bg	2b
	add	%i1, 2, %i1		! delay slot, inc to address

	b,a	.dbytecp		! copy remaining bytes, if any

/*
 * bzero_vme16(addr, length)
 * Zero a block of VME_D16 memory.
 * There is little optimization because the VME is so slow.
 * We come here word aligned
 */
.bzero_vme16:
        andn    %o1, 1, %o3             ! count of aligned shorts to clear
	and	%o1, 1, %o1		! unaligned leftover count
1:	clrh	[%o0]			! zero short
	subcc	%o3, 2, %o3		! decrement count
	bg	1b
	add	%o0, 2, %o0		! increment address

	b,a	.byteclr		! zero remaining bytes, if any

/*
 * bzero_vme32(addr, length)
 * Zero a block of VME_D32 memory.
 * There is little optimization because the VME is so slow.
 * We come here word aligned
 */
.bzero_vme32:
        andn    %o1, 3, %o3             ! count of aligned words to clear
        and     %o1, 3, %o1             ! unaligned leftover byte count
1:	clr     [%o0]                   ! zero word
        subcc   %o3, 4, %o3             ! decrement count
        bg      1b
        add     %o0, 4, %o0             ! increment address
 
        b,a     .byteclr		! zero remaining bytes, if any
	nop    
	SET_SIZE(bzero)
	SET_SIZE(blkclr)

#ifdef	BCOPY_BUF
	ENTRY(locked_bzero)
	ld	[THREAD_REG + T_LOFAULT], %o5
	cmp	%o1, 15			! check for small counts
	bl	.byteclr		! just clear bytes
	mov	0x100, %o3		! constant size of bzero_nobuf loop
	!
	! Check for word alignment.
	!
	btst	3, %o0
	bz	.locked_bzero_obmem
	nop
	!
	! clear bytes until word aligned
	!
1:	clrb	[%o0]
	add	%o0, 1, %o0
	btst	3, %o0
	bnz	1b
	sub	%o1, 1, %o1
	!
	! HW version of routine.
	! obmem, if needed move a word to become double-word aligned.
	!
.locked_bzero_obmem:
	!
	! %i0 - dst
	! %i1 - len
	! %i2 - dst pte
	! %i3 - copy loop counter
	! %i4 - cpu pointer
	! %i5 - t_lofault
	! %l0 - mxcc stream dst
	! %l2, %l3 - mxcc dst data
	! %o* - temps
	!
	save	%sp, -SA(MINFRAME), %sp	! get another window
	btst	7, %i0
	bz	.locked_bzero_bufchk	! is double aligned?
	clr	%g1			! clr g1 for second half of double %g0
	clr	[%i0]			! clr to double boundry
	sub	%i1, 4, %i1		! decrement count
	add	%i0, 4, %i0		! increment address
	/*
	 * The source and destination are now double-word aligned,
	 * see if the bcopy buffer is available.
	 */
.locked_bzero_bufchk:
	set	0x100, %i3		! setup i3 in case we use bzero_nbuf
	ld	[THREAD_REG + T_CPU], %i4
	ld	[%i4 + BCOPY_RES], %o0	! is hardware available
	tst	%o0
	bnz,a	.bzero_nobuf
	restore	%g0, 0, %g0

	cmp	%i1, BCOPY_LIMIT	! for small counts use software
	bl,a	.bzero_nobuf
	restore	%g0, 0, %g0

	btst	0x1F, %i0		! check if cache-line aligned
1:	bz,a	.locked_bzero_buf
	ldstub	[%i4 + BCOPY_RES], %o0	! try to grab bcopy buffer
	std	%g0, [%i0]
	add	%i0, 8, %i0
	sub	%i1, 8, %i1		! decrement count
	b	1b
	btst	0x1F, %i0		! check if aligned

.locked_bzero_buf:
	tst	%o0
	bnz,a	.bzero_nobuf		! give up, hardware in use 
	restore	%g0, 0, %g0

	set	virtual_bcopy, %l5
	ld	[%l5], %l5
	cmp	%l5, %g0
	bz	.try_phys_bzero
	nop

	set	0, %l0
	set	0, %l1

0:	stda	%l0, [%i0]ASI_BF
	dec	BCPY_BLKSZ, %i1
	cmp	%i0, BCPY_BLKSZ
	bge	0b
	inc	BCPY_BLKSZ, %i0	

	st	%g0, [%i4 + BCOPY_RES]	! interlock, unlock bcopy buffer
	b	.byteclr
	restore	%g0, 0, %g0

.try_phys_bzero:

	set	MXCC_STRM_DATA, %l0	! fill stream data with zeros
	stda	%g0, [%l0]ASI_MXCC	! need 4 doubles (g1 clr'd above)
	add	%l0, 8, %l0
	stda	%g0, [%l0]ASI_MXCC
	add	%l0, 8, %l0
	stda	%g0, [%l0]ASI_MXCC
	add	%l0, 8, %l0
	stda	%g0, [%l0]ASI_MXCC

	set	MXCC_STRM_DEST, %l0	! addr of stream destination reg

.hw_bzerotop:
	stb	%g0, [%i0]		! Set RM bits in dest page

	!
	! Probe src and dest.
	! We must use mmu_probe() since ASI_FLPR doesn't do what we want.
	!
	mov	%i0, %o0
	call	mmu_probe		! probe src
	mov	%g0, %o1
	mov	%o0, %i2

	srl	%i2, 28, %l2		! destination space
	btst	PTE_CACHEABLEMASK, %i2	! page marked cacheable?
	bnz,a	1f
	or	%l2, 0x10, %l2		! or in cacheable bit
1:
	srl	%i2, 8, %l3		! remove non-page bits from pte
	sll	%l3, MMU_PAGESHIFT, %l3	! phys page minus space bits
	and	%i0, MMU_PAGEOFFSET, %o0 ! page offset via virt addr
	or	%l3, %o0, %l3		! destination phys addr complete

	!
	! Calculate amount to copy in this iteration.
	! min(len, pagesize - pageoff(dst))
	!
	set	MMU_PAGESIZE, %o0
	and	%i0, MMU_PAGEOFFSET, %i3
	sub	%o0, %i3, %i3
	cmp	%i1, %i3
	bl,a	1f
	mov	%i1, %i3
1:
	andn	%i3, 0x1F, %i3		! round down to multiple of blksz
	add	%i0, %i3, %i0		! adjust dst
	sub	%i1, %i3, %i1		! adjust len

.hw_bzeroloop:
	stda	%l2, [%l0]ASI_MXCC
	deccc	BCPY_BLKSZ, %i3
	bnz	.hw_bzeroloop
	inc	BCPY_BLKSZ, %l3

	cmp	%i1, BCOPY_LIMIT	! go around again
	bge	.hw_bzerotop
	nop

!
! Check Error Register to see if AE bit set, indicating problem with
! stream operation we just did.  Fatal, so we go down if a problem
! is found.
!
.hw_bzeroexit:
	ldda	[%l0]ASI_MXCC, %l4	! wait for RDY bit in stream dest reg
	cmp	%l4, %g0
	bge	.hw_bzeroexit
	nop

	set	MXCC_ERROR, %l7		! Load addr of MXCC err reg
	ldda	[%l7]ASI_MXCC, %l4	! %l5 hold bits 0-31 of paddr
	set	MXCC_ERR_AE, %i3
	btst	%i3, %l4		! %l4 hold the status bits
	bz	1f			! if not set continue exiting
	nop

	set	MXCC_ERR_EV, %l7	! If error bit set....
	btst	%l7, %l4		!    check if valid bit set
	bnz	6f
	.empty				! Silence complaint about label

1:
	st	%g0, [%i4 + BCOPY_RES]	! unlock hardware
2:
	cmp	%i1, 8
	bl,a	.byteclr		! do rest, if any
	restore	%g0, 0, %g0
	std	%g0, [%i0]		! zero doubles
	sub	%i1, 8, %i1		! update count
	b	2b
	add	%i0, 8, %i0		! update dest address

6:
	set	0f, %o0
	call	panic
	nop
0:
	.asciz	"bzero stream operation failed"
	.align	32

	SET_SIZE(locked_bzero)
#endif	/* BCOPY_BUF */

#endif	/* lint */

/*
 * Copy a null terminated string from one point to another in
 * the kernel address space.
 * NOTE - don't use %o5 in this routine as copy{in,out}str uses it.
 *
 * copystr(from, to, maxlength, lencopied)
 *	caddr_t from, to;
 *	u_int maxlength, *lencopied;
 */

#if defined(lint)

/* ARGSUSED */
int
copystr(char *from, char *to, size_t maxlength, size_t *lencopied)
{ return(0); }

#else	/* lint */

	ENTRY(copystr)
#ifdef DEBUG
        sethi   %hi(KERNELBASE), %o4
        cmp     %o0, %o4
        blu     1f
        nop
 
        cmp     %o1, %o4
        bgeu    3f
        nop
 
1:
        set     2f, %o0
        call    panic
        nop
 
2:      .asciz  "copystr: Args in User Space"
        .align  4
 
3:
#endif DEBUG
.do_copystr:
	mov	%o2, %o4		! save original count
	tst	%o2
	bg,a	1f
	sub	%o0, %o1, %o0		! o0 gets the difference of src and dst

	!
	! maxlength <= 0
	!
	bz	.cs_out			! maxlength = 0
	mov	ENAMETOOLONG, %o0

	retl				! maxlength < 0
	mov	EFAULT, %o0			! return failure

	!
	! Do a byte by byte loop.
	! We do this instead of a word by word copy because most strings
	! are small and this takes a small number of cache lines.
	!
0:
	stb	%g1, [%o1]		! store byte
	tst	%g1			! null byte?
	bnz	1f
	add	%o1, 1, %o1		! incr dst addr

	b	.cs_out			! last byte in string
	mov	0, %o0			! ret code = 0
1:
	subcc	%o2, 1, %o2		! test count
	bge,a	0b
	ldub	[%o0+%o1], %g1		! delay slot, get source byte

	mov	0, %o2			! max number of bytes moved
	mov	ENAMETOOLONG, %o0	! ret code = ENAMETOOLONG
.cs_out:
	tst	%o3			! want length?
	bz	2f
	.empty
	sub	%o4, %o2, %o4		! compute length and store it
	st	%o4, [%o3]
2:
	retl
	nop				! return (ret code)
	SET_SIZE(copystr)

#endif	/* lint */

/*
 * Transfer data to and from user space -
 * Note that these routines can cause faults
 * It is assumed that the kernel has nothing at
 * less than KERNELBASE in the virtual address space.
 *
 * Note that copyin(9F) and copyout(9F) are part of the
 * DDI/DKI which specifies that they return '-1' on "errors."
 *
 * Sigh.
 *
 * So there's two extremely similar routines - xcopyin() and xcopyout()
 * which return the errno that we've faithfully computed.  This
 * allows other callers (e.g. uiomove(9F)) to work correctly.
 * Given that these are used pretty heavily, we expand the calling
 * sequences inline for all flavours (rather than making wrappers).
 */

/*
 * Copy kernel data to user space.
 *
 * int
 * copyout(caddr_t kaddr, caddr_t uaddr, size_t count)
 *
 * int
 * xcopyout(caddr_t kaddr, caddr_t uaddr, size_t count)
 */

#if defined(lint)

/*ARGSUSED*/
int
copyout(caddr_t kaddr, caddr_t uaddr, size_t count)
{ return (0); }

#else	/* lint */

	ENTRY(copyout)

	TRACE_IN(TR_COPYOUT_START, TR_copyout_start)

	sethi	%hi(KERNELBASE), %g1	! test uaddr < KERNELBASE
#ifdef DEBUG
	cmp     %o0, %g1
	bgeu    3f
	nop

	set     2f, %o0
	call    panic
	nop
2:
	.asciz  "copyout: from arg not in KERNEL space"
	.align 4

3:
#endif DEBUG

	cmp	%o1, %g1
	sethi	%hi(.copyioerr), %o3	! .copyioerr is lofault value
	bleu	.do_copy		! common code
	or	%o3, %lo(.copyioerr), %o3

	TRACE_ASM_0 (%o2, TR_FAC_BCOPY, TR_COPYOUT_FAULT, TR_copyout_fault);

	retl				! return failure
	mov	-1, %o0
	SET_SIZE(copyout)

#endif	/* lint */

#ifdef	lint

/*ARGSUSED*/
int
xcopyout(caddr_t kaddr, caddr_t uaddr, size_t count)
{ return (0); }

#else	/* lint */

	ENTRY(xcopyout)

	TRACE_IN(TR_COPYOUT_START, TR_copyout_start)

	sethi	%hi(KERNELBASE), %g1	! test uaddr < KERNELBASE
	cmp	%o1, %g1
	sethi	%hi(.xcopyioerr), %o3	! .xcopyioerr is lofault value
	bleu	.do_copy		! common code
	or	%o3, %lo(.xcopyioerr), %o3

	TRACE_ASM_0(%o2, TR_FAC_BCOPY, TR_COPYOUT_FAULT, TR_copyout_fault);

	retl				! return failure
	mov	EFAULT, %o0
	SET_SIZE(xcopyout)

#endif	/* lint */
	
/*
 * Copy user data to kernel space.
 *
 * int
 * copyin(caddr_t uaddr, caddr_t kaddr, size_t count)
 *
 * int
 * xcopyin(caddr_t uaddr, caddr_t kaddr, size_t count)
 */

#if defined(lint)

/*ARGSUSED*/
int
copyin(caddr_t uaddr, caddr_t kaddr, size_t count)
{ return (0); }

#else	/* lint */

	ENTRY(copyin)

	TRACE_IN(TR_COPYIN_START, TR_copyin_start)

	sethi	%hi(KERNELBASE), %g1	! test uaddr < KERNELBASE
#ifdef DEBUG
        cmp     %o1, %g1
        bgeu    3f
        nop
 
        set     2f, %o0
        call    panic
        nop
2:
        .asciz  "copyin: to arg not in KERNEL space"
        .align  4
 
3:
#endif DEBUG
	cmp	%o0, %g1
	sethi	%hi(.copyioerr), %o3	! .copyioerr is lofault value
	bleu	.do_copy		! common code
	or	%o3, %lo(.copyioerr), %o3

	TRACE_ASM_0 (%o2, TR_FAC_BCOPY, TR_COPYIN_FAULT, TR_copyin_fault);

	retl				! return failure
	mov	-1, %o0

/*
 * We got here because of a fault during copy{in,out}.
 * Errno value is in %g1, but DDI/DKI says return -1 (sigh).
 */
.copyioerr:
	st	%l6, [THREAD_REG + T_LOFAULT]	! restore old t_lofault
	ret
	restore	%g0, -1, %o0
	SET_SIZE(copyin)

#endif	/* lint */

#ifdef	lint

/*ARGSUSED*/
int
xcopyin(caddr_t uaddr, caddr_t kaddr, size_t count)
{ return (0); }

#else	/* lint */

	ENTRY(xcopyin)

	TRACE_IN(TR_COPYIN_START, TR_copyin_start)

	sethi	%hi(KERNELBASE), %g1	! test uaddr < KERNELBASE
	cmp	%o0, %g1
	sethi	%hi(.xcopyioerr), %o3	! .xcopyioerr is lofault value
	bleu	.do_copy		! common code
	or	%o3, %lo(.xcopyioerr), %o3

	TRACE_ASM_0 (%o2, TR_FAC_BCOPY, TR_COPYIN_FAULT, TR_copyin_fault);

	retl				! return failure
	mov	EFAULT, %o0

/*
 * We got here because of a fault during xcopy{in,out}.
 * Errno value is in %g1.
 */
.xcopyioerr:
	st	%l6, [THREAD_REG + T_LOFAULT]	! restore old t_lofault
	ret
	restore	%g1, 0, %o0
	SET_SIZE(xcopyin)

#endif	/* lint */

/*
 * Copy a null terminated string from the user address space into
 * the kernel address space.
 *
 * copyinstr(uaddr, kaddr, maxlength, lencopied)
 *	caddr_t uaddr, kaddr;
 *	size_t maxlength, *lencopied;
 */

#if defined(lint)

/* ARGSUSED */
int
copyinstr(char *uaddr, char *kaddr, size_t maxlength, size_t *lencopied)
{ return(0); }

#else	/* lint */

	ENTRY(copyinstr)
	sethi	%hi(KERNELBASE), %g1	! test uaddr < KERNELBASE
#ifdef DEBUG
        cmp     %o1, %g1
        bgeu    3f
        nop
 
        set     2f, %o0
        call    panic
        nop
 
2:      .asciz  "copyinstr: To arg not in kernel space"
        .align  4
3:
#endif DEBUG
	cmp	%o0, %g1
	bgeu	.copystrerr
	mov	%o7, %o5		! save return address

.cs_common:
	set	.copystrerr, %g1
	call	.do_copystr
	st	%g1, [THREAD_REG + T_LOFAULT]	! catch faults

	jmp	%o5 + 8			! return (results of copystr)
	clr	[THREAD_REG + T_LOFAULT]		! clear fault catcher
	SET_SIZE(copyinstr)

#endif	/* lint */

/*
 * Copy a null terminated string from the kernel
 * address space to the user address space.
 *
 * copyoutstr(kaddr, uaddr, maxlength, lencopied)
 *	caddr_t kaddr, uaddr;
 *	size_t maxlength, *lencopied;
 */

#if defined(lint)

/* ARGSUSED */
int
copyoutstr(char *kaddr, char *uaddr, size_t maxlength, size_t *lencopied)
{ return(0); }

#else	/* lint */

	ENTRY(copyoutstr)
	sethi	%hi(KERNELBASE), %g1	! test uaddr < KERNELBASE
#ifdef DEBUG
        cmp     %o0, %g1
        bgeu    3f
        nop
 
        set     2f, %o0
        call    panic
        nop
 
2:      .asciz  "copyoutstr: From arg in user space"
        .align  4
3:
#endif DEBUG
	cmp	%o1, %g1
	blu	.cs_common
	mov	%o7, %o5		! save return address
	! fall through

/*
 * Fault while trying to move from or to user space.
 * Set and return error code.
 */
.copystrerr:
	mov	EFAULT, %o0
	jmp	%o5 + 8			! return failure
	clr	[THREAD_REG + T_LOFAULT]
	SET_SIZE(copyoutstr)

#endif	/* lint */

/*
 * Copy a block of words -- must not overlap
 *
 * wcopy(from, to, count)
 *	int *from;
 *	int *to;
 *	size_t count;
 */

#if defined(lint)

/* ARGSUSED */
void
wcopy(int *from, int *to, size_t count)
{}

#else	/* lint */

	ENTRY(wcopy)
	tst	%o2
	bz	0f
	btst	0x3, %o2
	bz	4f
	btst	0x1, %o2
	bz	2f
	ld	[%o0], %o3
				! this block copies one word
	deccc	1, %o2
	inc	4, %o0
	st	%o3, [%o1]
	bz	0f
	btst	0x3, %o2
	bz	4f
	inc	4, %o1
	ld	[%o0], %o3
2:				! this block copies two words
	deccc	2, %o2
	ld	[%o0 + 4], %o4
	st	%o3, [%o1]
	inc	8, %o0
	st	%o4, [%o1 + 4]
	bz	0f
	inc	8, %o1
4:				! this block repeatedly copies four words
	ld	[%o0], %o3
	deccc	4, %o2
	ld	[%o0 + 4], %o4
	st	%o3, [%o1]
	ld	[%o0 + 8], %o5
	st	%o4, [%o1 + 4]
	ld	[%o0 + 12], %o3
	st	%o5, [%o1 + 8]
	inc	16, %o0
	st	%o3, [%o1 + 12]
	bnz	4b
	inc	16, %o1
0:
	retl
	nop
	SET_SIZE(wcopy)

#endif	/* lint */


/*
 * Copy a block of storage - must not overlap (from + len <= to).
 * No fault handler installed (to be called under on_fault())
 *
 * copyin_noerr(ufrom, kto, count)
 *	caddr_t ufrom, kto;
 *	size_t count;
 */

#if defined(lint)

/* ARGSUSED */
void
copyin_noerr(caddr_t ufrom, caddr_t kto, size_t count)
{}

#else	/* lint */

	ENTRY(copyin_noerr)

	TRACE_IN(TR_COPYIN_NOERR_START, TR_copyin_noerr_start)

#ifdef DEBUG
	sethi	%hi(KERNELBASE), %g1	! test uaddr < KERNELBASE
	cmp	%o0, %g1
	bgu	1f
	nop
	cmp	%o1, %g1
	bgeu	3f
	nop

1:
	set	2f, %o0
	call	panic
	nop

	!no return

2: 	
	.asciz	"copyin_noerr: address args not in correct user/kernel space"
	.align	4
3:	

#endif DEBUG
	ba	.do_copy
	ld	[THREAD_REG + T_LOFAULT], %o3

	SET_SIZE(copyin_noerr)
#endif lint

/*
 * Copy a block of storage - must not overlap (from + len <= to).
 * No fault handler installed (to be called under on_fault())
 *
 * copyout_noerr(kfrom, uto, count)
 *	caddr_t kfrom, uto;
 *	size_t count;
 */

#if defined(lint)

/* ARGSUSED */
void
copyout_noerr(caddr_t kfrom, caddr_t uto, size_t count)
{}

#else	/* lint */

	ENTRY(copyout_noerr)

	TRACE_IN(TR_COPYOUT_NOERR_START, TR_copyout_noerr_start)


#ifdef DEBUG
	sethi	%hi(KERNELBASE), %g1	! test uaddr < KERNELBASE
	cmp	%o1, %g1
	bgeu	1f
	nop
	cmp	%o0, %g1		! test for kaddr
	bgeu	3f
	nop

1:
	set	2f, %o0
	call	panic
	nop

	!no return

2: 	
	.asciz	"copyout_noerr: address args not in correct user/kernel space"
	.align 4
3:
#endif DEBUG
	ba	.do_copy
	ld	[THREAD_REG + T_LOFAULT], %o3

	SET_SIZE(copyout_noerr)
#endif lint


/*
 * Copy a block of storage - must not overlap (from + len <= to).
 * No fault handler installed (to be called under on_fault())
 *
 * ucopy(ufrom, uto, count)
 *	caddr_t kfrom, uto;
 *	size_t count;
 */


/*
 * Zero a block of storage in user space
 *
 * uzero(uaddr, length)
 *	caddr_t addr;
 *	size_t count;
 */

#if defined(lint)

/* ARGSUSED */
void
uzero(caddr_t addr, size_t count)
{}

#else lint

	ENTRY(uzero)


#ifdef DEBUG
	sethi	%hi(KERNELBASE), %g1	! test uaddr < KERNELBASE
	cmp	%o0, %g1
	bleu	3f	
	nop

	set	2f, %o0
	call	panic
	nop

2:	
	.asciz	"uzero: address arg is not user space"
	.align 4
3:
#endif DEBUG

	ba	.do_zero
	ld	[THREAD_REG + T_LOFAULT], %o2
	
	SET_SIZE(uzero)
#endif lint

/*
 * copy a block of storage in user space
 *
 * ucopy(ufrom, uto, ulength)
 *      caddr_t ufrom, uto;
 *      size_t ulength;
 */
 
#if defined(lint)
 
/* ARGSUSED */
void
ucopy (caddr_t ufrom, caddr_t uto, size_t ulength)
{}
 
#else lint
 
        ENTRY(ucopy)
 
 
#ifdef DEBUG
        sethi   %hi(KERNELBASE), %g1    ! test uaddr < KERNELBASE
        cmp     %o1, %g1
        bgu   	1f 
        nop

	cmp	%o0, %g1
	bleu	3f	
	nop

1: 
        set     2f, %o0
        call    panic
        nop
 
2:
	.asciz  "ucopy: address arg is not user space"
	.align 4
3:
#endif DEBUG

	ba	.do_copy
        ld      [THREAD_REG + T_LOFAULT], %o3

	SET_SIZE(ucopy)
#endif lint
