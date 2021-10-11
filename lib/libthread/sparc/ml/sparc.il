/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)sparc.il	1.7	94/11/03	SMI"

/*
 * XXX
 * Needs to be ported to new sys/synch.h definitions.
 */
#ifdef BUGS_GALORE
/*
 * _mutex_lock(mp)
 *	mutex_t *mp;
 *  {
 *	if (!_lock_try(&mp->lock))
 *		_mutex_op_lock(mp);
 *  }
 */
	.inline	_mutex_lock, 4
	ldstub	[%o0 + 1 /* MUTEX_LOCK */], %o1
	cmp	%o1, 0xff
	bnz	1f			!when not zero, lock was just acquired
	nop
	call	_mutex_op_lock
	nop
1:
	.end

/*
 * _mutex_unlock(mp)
 *	mutex_t *mp;
 *  {
 *	_lock_clear(&mp->lock);
 *	if (mp->wanted)
 *		_mutex_op_unlock(mp);
 *  }
 */
	.inline	_mutex_unlock, 4
	clrb	[%o0 + 1 /* MUTEX_LOCK */]
	ldub	[%o0 + 0 /* MUTEX_WANTED */], %o1
	andcc	%o1, 1, %g0
	bz	1f			!skip when wanted flag is not set
	nop
	call	_mutex_op_unlock
	nop
1:
	.end

/*
 * _sigon()
 *  {
 *	if (curthread->t_nosig == 0)
 *		return;
 *	if (--curthread->t_nosig == 0 && curthread->t_sig)
 *		_sigx();
 *  }
 */
	.inline _sigon, 0
	add	%g7, _thread, %o0
	ldub	[%o0 + T_NOSIG], %o1
	cmp	%o1, %g0
	bz	1f
	nop
	subcc	%o1, 1, %o1
	bnz	1f
	stb	%o1, [%o0 + T_NOSIG]
	ldub	[%o0 + T_SIG], %o1
	cmp	%o1, %g0
	bz	1f
	nop
	call	_sigx
	mov	%o1, %o0
1:
	.end
/*
 * _sigoff()
 *  {
 *	curthread->t_nosig++;
 *  }
 */
	.inline _sigoff, 0
	add	%g7, _thread, %o0
	ldub	[%o0 + T_NOSIG], %o1
	add	%o1, 1, %o1
	stb	%o1, [%o0 + T_NOSIG]
	.end
/*
 * _lmutex_lock(mp)
 *	mutex_t *mp;
 *  {
 *	_sigoff();
 *	mutex_lock();
 *  }
 */
	.inline _lmutex_lock, 4
	add	%g7, _thread, %o1
	ldub	[%o1 + T_NOSIG], %o2
	add	%o2, 1, %o2
	stb	%o2, [%o1 + T_NOSIG]
	ldstub	[%o0 + 1 /* MUTEX_LOCK */], %o1
	cmp	%o1, 0xff
	bnz	1f			!when not zero, lock was just acquired
	nop
	call	_mutex_op_lock
	nop
1:
	.end
/*
 * _lmutex_unlock(mp)
 *	mutex_t *mp;
 *  {
 *	mutex_unlock();
 *	_sigon();
 *  }
 */
	.inline _lmutex_unlock, 4
	clrb	[%o0 + 1 /* MUTEX_LOCK */]
	ldub	[%o0 + 0 /* MUTEX_WANTED */], %o1
	andcc	%o1, 1, %g0
	bz	1f			!skip when wanted flag is not set
	nop
	call	_mutex_op_unlock
	nop
1:
	add	%g7, _thread, %o0
	ldub	[%o0 + T_NOSIG], %o1
	cmp	%o1, %g0
	bz	1f
	nop
	subcc	%o1, 1, %o1
	bnz	1f
	stb	%o1, [%o0 + T_NOSIG]
	ldub	[%o0 + T_SIG], %o1
	cmp	%o1, %g0
	bz	1f
	nop
	call	_sigx
	mov	%o1, %o0
1:
	.end
/*
 * _sched_lock()
 *  {
 *	_sigoff();
 *	lwp_mutex_lock(&_sched_lock);
 *  }
 */
	.inline _sched_lock, 0
	add	%g7, _thread, %o0
	ldub	[%o0 + T_NOSIG], %o1
	add	%o1, 1, %o1
	stb	%o1, [%o0 + T_NOSIG]
	set	_schedlock, %o0
	ldstub	[%o0 + 1 /* MUTEX_LOCK */], %o1
	cmp	%o1, 0xff
	bnz	1f			!when not zero, lock was just acquired
	nop
	call	___lwp_mutex_lock
	nop
1:
	.end
/*
 * _sched_lock_()
 * {
 *	lwp_mutex_lock(&_sched_lock);
 * }
 */
	.inline _sched_lock_, 0
	set	_schedlock, %o0
	ldstub	[%o0 + 1 /* MUTEX_LOCK */], %o1
	cmp	%o1, 0xff
	bnz	1f			!when not zero, lock was just acquired
	nop
	call	___lwp_mutex_lock
	nop
1:
	.end
/*
 * _sched_unlock()
 * {
 *	lwp_mutex_unlock(&_sched_lock);
 *	_sigon();
 * }
 */
	.inline _sched_unlock, 0
	set	_schedlock, %o0
	clrb	[%o0 + 1 /* MUTEX_LOCK */]
	ldub	[%o0 + 0 /* MUTEX_WANTED */], %o1
	andcc	%o1, 1, %g0
	bz	1f			!skip when wanted flag is not set
	nop
	call	__lwp_mutex_unlock
	nop
1:
	add	%g7, _thread, %o0
	ldub	[%o0 + T_NOSIG], %o1
	cmp	%o1, %g0
	bz	1f
	nop
	subcc	%o1, 1, %o1
	bnz	1f
	stb	%o1, [%o0 + T_NOSIG]
	ldub	[%o0 + T_SIG], %o1
	cmp	%o1, %g0
	bz	1f
	nop
	call	_sigx
	mov	%o1, %o0
1:
	.end
/*
 * _sched_unlock_()
 * {
 *	lwp_mutex_unlock(&_sched_lock);
 * }
 */
	.inline _sched_unlock_, 0
	set	_schedlock, %o0
	clrb	[%o0 + 1 /* MUTEX_LOCK */]
	ldub	[%o0 + 0 /* MUTEX_WANTED */], %o1
	andcc	%o1, 1, %g0
	bz	1f			!skip when wanted flag is not set
	nop
	call	__lwp_mutex_unlock
	nop
1:
	.end
#endif
