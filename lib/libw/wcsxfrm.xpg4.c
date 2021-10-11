/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */
#ident	"@(#)wcsxfrm.xpg4.c	1.4	95/04/10 SMI"

#include "_libcollate.h"

_coll_info *_load_coll_(int *);
static int _wcscoll_C(const wchar_t *, const wchar_t *);
static size_t	wcsxfrm_C(const wchar_t *s1, const wchar_t *s2, size_t n);
int _xpg4_wcscoll(wchar_t *, wchar_t *, _coll_info *, int *);
int _xpg4_wcsxfrm(wchar_t *, wchar_t *, size_t, _coll_info *, int *);

#ifdef _DEVELOP_
#pragma weak wcscoll = _wcscoll
#pragma weak wcsxfrm = _wcsxfrm
#endif
/*
 *  wcscoll() and wcsxfrm() top routines.
 */
int
#ifdef _DEVELOP_
_wcscoll(const wchar_t *ws1, const wchar_t *ws2)
#else
_wcscoll_xpg4(const wchar_t *ws1, const wchar_t *ws2)
#endif
{
	int ret;
	int e = 0;
	_coll_info *ci;

	ci = _load_coll_(&ret);
	if (ret == COLL_USE_C)
		goto use_c;
	/*
	 * Dispatch to a right collating function.
	 */
	if (ci->u.hp->flags & USE_BINARY) {
		/*
		 * Collation is done based on code value.
		 * This is C locale collating order.
		 */
		goto use_c;
	} else
		ret = _xpg4_wcscoll((wchar_t *)ws1, (wchar_t *)ws2, ci, &e);
#ifdef DDEBUG
	if (e != 0)
		fprintf(stderr, "wcscoll err status = %d\n", e);
#endif
	return (ret);

use_c:
	return (wcscoll_C(ws1, ws2));
}

size_t
#ifdef _DEVELOP_
_wcsxfrm(wchar_t *s1, const wchar_t *s2, size_t n)
#else
_wcsxfrm_xpg4(wchar_t *s1, const wchar_t *s2, size_t n)
#endif
{
	int ret;
	int e = 0;
	_coll_info *ci;

	ci = _load_coll_(&ret);
	if (ret == COLL_USE_C)
		goto use_c;
	/*
	 * Dispatch to a right collating function.
	 */
	if (ci->u.hp->flags & USE_BINARY) {
		/*
		 * Collation is done based on code value.
		 * This is C locale collating order.
		 */
		goto use_c;
	} else
		ret = _xpg4_wcsxfrm((wchar_t *)s1, (wchar_t *)s2, n, ci, &e);
#ifdef DDEBUG
	if (e != 0)
		fprintf(stderr, "wcxfrm err status = %d\n", e);
#endif
	return (ret);

use_c:
	return (wcsxfrm_C(s1, s2, n));
}

/*
 * wcscoll() and wcsxfrm_C() routines for C locale.
 * Taken from Solaris 2.4.
 */
static int
wcscoll_C(const wchar_t *s1, const wchar_t *s2)
{
	return (wcscmp(s1, s2));
}

static size_t
wcsxfrm_C(const wchar_t *s1, const wchar_t *s2, size_t n)
{

	if (n != 0)
		wcsncpy(s1, s2, n);
	return (wslen(s2));
}
