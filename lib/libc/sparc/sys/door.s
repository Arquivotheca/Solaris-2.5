#ident "@(#)door.s 94/12/06	1.2 SMI"

	.file "door.s"

#include <sys/asm_linkage.h>
#include <sys/door.h>

	ANSI_PRAGMA_WEAK(_door_create,function)
	ANSI_PRAGMA_WEAK(_door_call,function)
	ANSI_PRAGMA_WEAK(_door_return,function)
	ANSI_PRAGMA_WEAK(_door_revoke,function)
	ANSI_PRAGMA_WEAK(_door_info,function)
	ANSI_PRAGMA_WEAK(_door_cred,function)

#include "SYS.h"

/*
 * Pointer to server create function
 */
	.section	".bss"
	.common	__door_server_func, 4, 4

/*
 * int
 * _door_create(void (*)(), void *, u_int)
 */
	ENTRY(_door_create)
	mov	DOOR_CREATE, %o5	! subcode
	SYSTRAP(door)
	SYSCERROR
	retl
	mov	%o1, %o0	! (int)longlong
	SET_SIZE(_door_create)

/*
 * int
 * _door_revoke(int)
 */
	ENTRY(_door_revoke)
	mov	DOOR_REVOKE, %o5	! subcode
	SYSTRAP(door)
	SYSCERROR
	retl
	mov	%o1, %o0	! (int)longlong
	SET_SIZE(_door_revoke)
/*
 * int
 * _door_info(int, door_info_t *)
 */
	ENTRY(_door_info)
	mov	DOOR_INFO, %o5	! subcode
	SYSTRAP(door)
	SYSCERROR
	retl
	mov	%o1, %o0	! (int)longlong
	SET_SIZE(_door_info)

/*
 * int
 * _door_cred(door_cred_t *)
 */
	ENTRY(_door_cred)
	mov	DOOR_CRED, %o5	! subcode
	SYSTRAP(door)
	SYSCERROR
	retl
	mov	%o1, %o0	! (int)longlong
	SET_SIZE(_door_cred)
/*
 * int
 * _door_call(int d, void **buf, int *bsize, int *asize, int *nfd)
 */
	ENTRY(_door_call);
	save	%sp, -SA(MINFRAME), %sp
	mov	%i0, %o0	! descriptor
	ld	[%i1], %o1	! *buf
	ld	[%i2], %o2	! *bsize
	ld	[%i3], %o3	! *asize
	ld	[%i4], %o4	! *nfd
	mov	DOOR_CALL, %o5	! subcode
	SYSTRAP(door)
	bcc,a	1f
	st	%o1, [%i1]	! (delay) update *buf

	restore	%o0, %g0, %o0	! errno
	mov	%o7, %g1
	call	_cerror
	mov	%g1, %o7
	retl
	nop
1:
	st	%o2, [%i2]	! updated *bsize
	st	%o3, [%i3]	! updated *asize
	st	%o4, [%i4]	! updated *nfd
	ret
	restore %o0, %g0, %o0
	SET_SIZE(_door_call)

/*
 * _door_return(void *, int, int, int, caddr_t stk_base)
 */
	ENTRY(_door_return)
	/* 
	 * Curthread sits on top of the thread stack area.
	 */
	sub	%g7, SA(MINFRAME), %o4
door_restart:
	mov	DOOR_RETURN, %o5	! subcode
	SYSTRAP(door)
	/*
	 * All new invocations come here (unless there is an error
	 * in the door_return).
	 *
	 * on return, we're serving a door_call:
	 *	o0=cookie,	(or errno)
	 *	o1=data_buf,
	 *	o2=data_size,
	 *	o3=door_ptr,
	 *	o4=door_size,
	 *	o5=pc
	 *	g1=nservers (0 = make more server threads, -1 = error)
	 */
	bcs	2f		! errno is set
	tst	%g1		! (delay) test nservers
	bg	1f		! everything looks o.k.
	nop
	/*
	 * this is the last server thread - call creation func for more
	 */
	save	%sp, -SA(MINFRAME), %sp
#if defined(PIC)
	PIC_SETUP(g1)
	ld	[%g1 + __door_server_func], %g1
	ld	[%g1], %g1
#else
	sethi	%hi(__door_server_func), %g1
	ld	[%g1 + %lo(__door_server_func)], %g1
#endif	/* defined(PIC) */
	call	%g1, 0
	nop
	restore
1:
	mov	%g0, %i7
	jmpl	%o5, %o7	/* Do the call */
	mov	%g0, %i6	/* The stack trace stops here */
	call	thr_exit	/* Exit the thread if we return here */
	nop
	/* NOTREACHED */
2:
	/* Error during door_return call */
	cmp	%o0, EINTR	! interrupted while waiting?
	be	door_restart
	nop
	mov	%o7, %g1
	call	_cerror
	mov	%g1, %o7
	retl
	nop
	SET_SIZE(_door_return)
