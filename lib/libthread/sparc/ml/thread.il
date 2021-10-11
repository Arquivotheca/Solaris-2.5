/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)thread.il	1.4	94/11/03	SMI"

/*
 * _curthread()
 *
 * returns the value of the currently active thread.
 */
	.inline	_curthread, 4
	mov	%g7, %o0
	.end


	.inline	_flush_store, 4
	ldstub	[%sp - 4], %g0
	.end
