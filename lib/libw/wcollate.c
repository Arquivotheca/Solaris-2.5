/* wscoll() and wsxfrm(). */
/* This is Sun's propriatry implementation of wsxfrm() and wscoll()	*/
/* using dynamic linking.  It is probably free from AT&T copyright.	*/
/* 	COPYRIGHT (C) 1991 SUN MICROSYSTEMS, INC.			*/
/*	ALL RIGHT RESERVED.						*/

#ident	"@(#)wcollate.c	1.4	94/11/18 SMI"

#pragma weak wcscoll = _wcscoll
#pragma weak wcsxfrm = _wcsxfrm
#pragma weak wscoll = _wcscoll
#pragma weak wsxfrm = _wcsxfrm

#include <wchar.h>

size_t
_wcsxfrm(wchar_t *s1, const wchar_t *s2, size_t n)
{
#if defined(PIC)	/* Compiled for shared lib. */
	if (_is_xpg_collate())
		return (_wcsxfrm_xpg4(s1, s2, n));
	else
		return (_wcsxfrm_dyn(s1, s2, n));
#else /* !PIC  --- compiled for static libc. */
	return (_wcsxfrm_C(s1, s2, n));
#endif /* PIC */
}

int
_wcscoll(const wchar_t *s1, const wchar_t *s2)
{
#if defined(PIC)	/* Compiled for shared lib. */
	if (_is_xpg_collate())
		return (_wcscoll_xpg4(s1, s2));
	else
		return (_wcscoll_dyn(s1, s2));
#else /* !PIC  --- compiled for static libc. */
	return (_wcscoll_C(s1, s2));
#endif /* PIC */
}
