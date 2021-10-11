/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ident	"@(#)asm_putnext.s	1.1	91/12/19 SMI"

/*
 * This is the SPARC assembly version of uts/common/os/putnext.c.
 * The code is very closely aligned with the C version but uses the
 * following tricks:
 *	- It does a manual save and restore of the syncq pointer and
 *	  the return address (%i7) into sq_save except for SQ_CIPUT syncqs.
 *	- It accesses sq_count and sq_flags in one load/store assuming
 *	  that sq_flags is the low-order 16 bytes on the word.
 *	- Moving out the uncommon code sections to improve the I cache
 *	  performance.
 *
 * The DDI defines putnext() as a function returning int
 * but it is used everywhere as a function returning void.
 * The DDI should be changed, but we gratuitously return 0 for now.
 * (This has no effect on performance.)
 */

#if defined(lint)
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#else	/* lint */
#include "assym.s"
#endif	/* lint */

#include <sys/mutex.h>
#include <sys/t_lock.h>
#include <sys/asm_linkage.h>
#include <sys/machlock.h>
#include <sys/machthread.h>

#if !defined(lint)

#define	NO_EARLY_RELEASE
#undef DEBUG
#ifdef DEBUG
	.seg	".data"
	.global	putnext_early;
putnext_early:
	.word	0
	.global	putnext_not_early;
putnext_not_early:
	.word	0
	.seg	".text"
	.align	4
#endif DEBUG

#ifdef TRACE
	.global	TR_put_start;
TR_put_start:
	.asciz	"put:(%X, %X)";
	.global	TR_putnext_start;
TR_putnext_start:
	.asciz	"putnext_start:(%X, %X)";
	.global	TR_putnext_end;
TR_putnext_end:
	.asciz	"putnext_end:(%X, %X, %X) done";
	.align	4
#endif	/* TRACE */

! Need defines for
!	SD_LOCK
!	Q_FLAG Q_NEXT Q_STREAM Q_SYNCQ Q_QINFO
!	QI_PUTP
!	SQ_LOCK SQ_CHECK SQ_WAIT SQ_SAVE

/*
 * Assumes that sq_flags and sq_count can be read as long with sq_count
 * being the high-order part.
 */
#define	SQ_COUNTSHIFT	16
#define	SQ_CHECK	SQ_COUNT

! Performance:
!	mutex_enter: 12 instr, 2 ld/use stalls (as of this writing)
!	mutex_exit: 13 instr, 1 ld/use stalls (as of this writing)
!	putnext uses 3 mutex_enter/exit pairs; put only uses 2.
!	putnext not hot/unsafe: 46+3*25 instr, 0+3*3 ld/use stalls
!		6 loads, 2 stores, 2 ldd, 2 std
!	putnext hot: -6 (+window overflow)
!	put: putnext -(10) instr
!
! Registers:
!	%i0: qp
!	%i1: mp
!	%i2: stp
!	%i3: sq_check
!	%i4: q_flag, q_qinfo, qi_putp
!	%i5: sq
!	%l0: temp (1 << SQ_COUNTSHIFT)
!	%l1: temp (1 << SQ_COUNTSHIFT) - 1 (mask for sq_flags)

#endif	/* !lint */

#if defined(lint)

/* ARGSUSED */
void
put(queue_t *qp, mblk_t *mp)
{}

#else	/* lint */

! %i0 qp
! %i1 mp
	ENTRY(put)
	save	%sp, -SA(MINFRAME), %sp
	TRACE_ASM_2(%o3, TR_FAC_STREAMS_FR, TR_PUT_START,
		TR_put_start, %i0, %i1);
	ld	[%i0+Q_SYNCQ], %i5
	sethi	%hi(1<<SQ_COUNTSHIFT), %l0
	call	mutex_enter, 1
	add	%i5, SQ_LOCK, %o0		! delay
	ld	[%i5+SQ_CHECK], %i3
	ba	.put_entry
	ld	[%i0+Q_QINFO], %i4		! delay
	SET_SIZE(put)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
int
putnext(queue_t *qp, mblk_t *mp)
{
	return (0);
}

#else	/* lint */

	ENTRY(putnext)
	save	%sp, -SA(MINFRAME), %sp
	TRACE_ASM_2(%o3, TR_FAC_STREAMS_FR, TR_PUTNEXT_START,
		TR_putnext_start, %i0, %i1);
	ld	[%i0+Q_STREAM], %i2
	ld	[%i0+Q_FLAG], %i4
	call	mutex_enter, 1
	add	%i2, SD_LOCK, %o0		! delay
	btst	QUNSAFE, %i4
	be,a	.not_unsafe
	ld	[%i0+Q_NEXT], %i0		! delay

	ba,a	.from_unsafe

.not_unsafe:
	sethi	%hi(1<<SQ_COUNTSHIFT), %l0
	ld	[%i0+Q_SYNCQ], %i5
	ld	[%i0+Q_QINFO], %i4
	call	mutex_enter, 1
	add	%i5, SQ_LOCK, %o0		! delay
	ld	[%i5+SQ_CHECK], %i3
	call	mutex_exit, 1
	add	%i2, SD_LOCK, %o0		! delay
.put_entry:
	btst	(SQ_GOAWAY|SQ_CIPUT|SQ_UNSAFE), %i3	! sq_flags set?
	bne	.flags_in
	ld	[%i4+QI_PUTP], %i4		! delay
	or	%l0, SQ_EXCL, %l0
	add	%i3, %l0, %i3			! set SQ_EXCL, inc sq_count
	st	%i3, [%i5+SQ_CHECK]
	call	mutex_exit, 1
	add	%i5, SQ_LOCK, %o0		! delay

.call_putp:
#ifndef NO_EARLY_RELEASE
	! We made it into the destination queue. Check if we were
	! tail called in which case we can leave the source queue
	! and change our return address.
	! Check if the return address (%o7) == putnext_return_address
	set	.putnext_return_address, %i3
	cmp	%i3, %i7
	bne	.not_early_release
	restore					! delay

	! Leave the previous syncq early since the put procedure was
	! tail-optimized away.
	! Get the previous syncq from %l4. Keep the return address in %l5
	mov	%l4, %o2

	! exit the source syncq (%i2)
	save	%sp, -SA(MINFRAME), %sp
	! Copy SQ_SAVE from the previous syncq to this syncq
#ifdef DEBUG
	set	putnext_early, %o0
	ld	[%o0], %o1
	inc	%o1
	st	%o1, [%o0]
#endif DEBUG
	ldd	[%i2+SQ_SAVE], %l4
	call	mutex_enter, 1
	add	%i2, SQ_LOCK, %o0		! delay
	ld	[%i2+SQ_CHECK], %i3		! get sq_check
	std	%l4, [%i5+SQ_SAVE]
	sethi	%hi(1<<SQ_COUNTSHIFT), %l0
	btst	(SQ_QUEUED|SQ_UNSAFE|SQ_WANTWAKEUP|SQ_WANTEXWAKEUP), %i3
	bne	.flags_out2			! handle flags being set
	sub	%i3, %l0, %i3			! delay, count--
	andn	%i3, SQ_EXCL, %i3
	st	%i3, [%i2+SQ_CHECK]
	call	mutex_exit, 1
	add	%i2, SQ_LOCK, %o0		! delay

.done2:
	ba	.putnext_return_address
	restore					! delay

.not_early_release:
#ifdef DEBUG
	save	%sp, -SA(MINFRAME), %sp
	set	putnext_not_early, %o0
	ld	[%o0], %o1
	inc	%o1
	st	%o1, [%o0]
	restore
#endif DEBUG
	! Unrotate window and save o5 and o7
	std	%l4, [%o5+SQ_SAVE]
	mov	%o7, %l5
.putnext_return_address:
#else NO_EARLY_RELEASE
	! Unrotate window and save o5 and o7
	restore
	std	%l4, [%o5+SQ_SAVE]
	mov	%o7, %l5
#endif NO_EARLY_RELEASE
	call	%o4, 2
	mov	%o5, %l4			! delay
	! restore o5 and o7 and rotate window back
	mov	%l4, %o5
	mov	%l5, %o7
	ldd	[%o5+SQ_SAVE], %l4
	save	%sp, -SA(MINFRAME), %sp

.putp_done:					! hot case jumps here
	call	mutex_enter, 1
	add	%i5, SQ_LOCK, %o0		! delay
	ld	[%i5+SQ_CHECK], %i3		! get sq_check
	sethi	%hi(1<<SQ_COUNTSHIFT), %l0
	btst	(SQ_QUEUED|SQ_UNSAFE|SQ_WANTWAKEUP|SQ_WANTEXWAKEUP), %i3
	bne	.flags_out			! above flags set?
	sub	%i3, %l0, %i3			! delay, count--
	andn	%i3, SQ_EXCL, %i3
	st	%i3, [%i5+SQ_CHECK]
	call	mutex_exit, 1
	add	%i5, SQ_LOCK, %o0		! delay
	TRACE_ASM_3(%o3, TR_FAC_STREAMS_FR, TR_PUTNEXT_END,
		TR_putnext_end, %g0, %i5, %i1);
	ret
	restore	%g0, 0, %o0			! delay

!
! Handle flags that are set when entering the syncq:
!	- SQ_GOAWAY: put on syncq
!	- SQ_CIPUT: do not set SQ_EXCL, call put procedure with rotated window
!	- SQ_UNSAFE: get unsafe_driver lock. Recheck SQ_STAYAWAY after getting
!	  the lock and put message on syncq if a SQ_STAYAWAY flag is set.
!
.flags_in:
	! Handle the different flags
	btst	SQ_GOAWAY, %i3
	be,a	.test_hot
	btst	SQ_CIPUT, %i3			! delay

	mov	%i1, %o2
	mov	%i0, %o1
	clr	%o3
	call	fill_syncq, 4
	mov	%i5, %o0			! delay
	call	mutex_exit, 1
	add	%i5, SQ_LOCK, %o0		! delay
	TRACE_ASM_3(%o3, TR_FAC_STREAMS_FR, TR_PUTNEXT_END,
		TR_putnext_end, %i0, %i1, %g0);
	ret
	restore	%g0, 0, %o0			! delay

.test_hot:
! done by caller: btst	SQ_CIPUT, %i3
	bne,a	.hot
	add	%i3, %l0, %i3			! delay, add one to sq_count

	! Must be unsafe when we get here
	mov	%i0, %o0			! qp
	mov	%i1, %o1			! mp
	sub	%l0, 1, %l1			! 0xffff
	and	%i3, %l1, %o2			! flags
	srl	%i3, SQ_COUNTSHIFT, %o3		! count
	call	putnext_to_unsafe, 4
	mov	%i5, %o4			! delay, sq
	tst	%o0
	be	.call_putp
	nop					! delay
	ret
	restore	%g0, 0, %o0

.hot:
	! hot case
	st	%i3, [%i5+SQ_CHECK]
	call	mutex_exit, 1
	add	%i5, SQ_LOCK, %o0		! delay
	mov	%i0, %o0
	call	%i4, 2
	mov	%i1, %o1			! delay
	ba,a	.putp_done




!
! Handle flags when leaving the syncq:
!	- SQ_QUEUED but not SQ_STAYAWAY: drain the syncq
!	- SQ_UNSAFE: drop unsafe_driver lock
!	- SQ_WANTWAKEUP: cv_broadcast on sq_wait
!	- SQ_WANTEXWAKEUP: cv_broadcast on sq_exitwait
!
.flags_out:
	add	%i3, %l0, %i3			! undo count--
	mov	%i5, %i0			! sq
	! mp already in %i1
	sub	%l0, 1, %l1			! 0xffff
	and	%i3, %l1, %i2			! flags
	srl	%i3, SQ_COUNTSHIFT, %i3		! count
	call	putnext_tail, 4
	restore					! delay

#ifndef NO_EARLY_RELEASE
!
! Handle flags when leaving the syncq (for early release):
!	- SQ_QUEUED but not SQ_STAYAWAY: drain the syncq
!	- SQ_UNSAFE: drop unsafe_driver lock
!	- SQ_WANTWAKEUP: cv_broadcast on sq_wait
!	- SQ_WANTEXWAKEUP: cv_broadcast on sq_exitwait
!
! SQ is in %i2 (not in %i5)
.flags_out2:
	add	%i3, %l0, %i3			! undo count--
	mov	%i2, %o0			! sq
	mov	%i1, %o1			! mp
	sub	%l0, 1, %l1			! 0xffff
	and	%i3, %l1, %o2			! flags
	call	putnext_tail, 4
	srl	%i3, SQ_COUNTSHIFT, %o3		! delay, count
	ba	.done2
	nop					! delay
#endif NO_EARLY_RELEASE

!
! Handle the case when comming from an unsafe queue
! Use claimq/releaseq on the "from" queue to prevent q_next from changing
! and call put(q->q_next, mp) after dropping the unsafe_driver lock.
!
.from_unsafe:
	! Do the unsafe things
	call	mutex_exit, 1
	add	%i2, SD_LOCK, %o0		! delay
	! %i0 and %i1 unchanged
	call	putnext_from_unsafe, 2
	restore					! delay
	SET_SIZE(putnext)

#endif	/* lint */
