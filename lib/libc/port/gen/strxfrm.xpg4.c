/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */
#ident	"@(#)strxfrm.xpg4.c	1.3	95/04/09 SMI"

#include "_libcollate.h"

extern int	strcmp(const char *, const char *);
extern char *	strcpy(char *, const char *);
extern char *	strncpy(char *, const char *, size_t);

_coll_info *_load_coll_(int *);
static int _strcoll_C(const char *, const char *);
static size_t	strxfrm_C(char *s1, const char *s2, size_t n);
int _xpg4_strcoll_(char *, char *, _coll_info *, int *);
int _xpg4_strxfrm_(int, char *, char *, size_t, _coll_info *, int *);

#ifdef _DEVELOP_
#pragma weak strcoll = _strcoll
#pragma weak strxfrm = _strxfrm
#endif

size_t
#ifdef _DEVELOP_
_strxfrm(char *s1, const char *s2, size_t n)
#else
_strxfrm_xpg4(char *s1, const char *s2, size_t n)
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
		ret = _xpg4_strxfrm_(T_STRXFRM, s1, (char *)s2, n, ci, &e);
#ifdef DDEBUG
	if (e != 0)
		fprintf(stderr, "strxfrm err status = %d\n", e);
#endif
	return (ret);

use_c:
	return (strxfrm_C(s1, s2, n));
}

int
#ifdef _DEVELOP_
_strcoll(const char *s1, const char *s2)
#else
_strcoll_xpg4(const char *s1, const char *s2)
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
		ret = _xpg4_strcoll_((char *)s1, (char *)s2, ci, &e);
#ifdef DDEBUG
	if (e != 0)
		fprintf(stderr, "strcoll err status = %d\n", e);
#endif
	return (ret);

use_c:
	return (strcoll_C(s1, s2));
}

static int
strcoll_C(const char *s1, const char *s2)
{
	return (strcmp(s1, s2));
}

static size_t
strxfrm_C(char *s1, const char *s2, size_t n)
{

	if (n != 0)
		strncpy(s1, s2, n);
	return (strlen(s2));
}
