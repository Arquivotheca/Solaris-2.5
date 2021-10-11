/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */
#ident	"@(#)libwcollate.c	1.2	94/10/04 SMI"

#include "_libcollate.h"

_coll_info *_load_coll_(int *);

static int _wcscoll_C(const wchar_t *, const wchar_t *);
static size_t	wcsxfrm_C(const wchar_t *s1, const wchar_t *s2, size_t n);

int _xpg4_strcoll_(char *, char *, _coll_info *, int *);
int _xpg4_strxfrm_(int, char *, char *, size_t, _coll_info *, int *);

int _xpg4_wcscoll(wchar_t *, wchar_t *, _coll_info *, int *);
int _xpg4_wcsxfrm(wchar_t *, wchar_t *, size_t, _coll_info *, int *);

/*
 * wcscoll() and wsxfrm() for XPG4.
 * 	These routines basically use _xpg4_* routines defined in
 *	libcollate.c. (which is to be reside in libc.).
 */
int
_xpg4_wcscoll(wchar_t *w1, wchar_t *w2, _coll_info *c1, int *e)
{
	int len;
	int ret;
	char *mb1 = NULL;
	char *mb2 = NULL;
	int size;
	int my_error = 0;

	/*
	 * Create multibyte string for the first string.
	 */
#ifdef DDEBUG
printf("_xpg4_wcscoll(%ws, %ws)\n", w1, w2);
printf("The current locale is '%s'.\n", setlocale(LC_ALL, 0));
#endif

	for (len = 0; w1[len] != 0; len++);
	size = len*MB_CUR_MAX + 1;
	mb1 = malloc(size);
	if (mb1 == NULL) {
		/*
		 * Could not allocate memory.
		 */
		*e = 10;
		return (-1);
	}
	ret = wcstombs(mb1, w1, size);
	if (ret == -1) {
		*e = 20;
		free(mb1);
		return (-1);
	}

	/*
	 * Create multibyte string for the second string.
	 */
	for (len = 0; w2[len] != 0; len++);
	size = len*MB_CUR_MAX + 1;
	mb2 = malloc(size);
	if (mb2 == NULL) {
		/*
		 * Could not allocate memory.
		 */
		*e = 30;
		free(mb1);
		return (-1);
	}
	ret = wcstombs(mb2, w2, size);
	if (ret == -1) {
		*e = 40;
		free(mb1);
		free(mb2);
		return (-1);
	}

	/*
	 * Now we have two multi-byte strings.
	 * Let xpg4_strcoll() do the work.
	 */
#ifdef DDEBUG
printf("_xpg4_wcscoll passing (%s,%s) to _xpg4_strcoll_.\n", mb1, mb2);
#endif
	ret = _xpg4_strcoll_(mb1, mb2, c1, &my_error);

	/*
	 * You can return.
	 */
	*e = my_error;
	free(mb1);
	free(mb2);

	return (ret);
}

int
_xpg4_wcsxfrm(wchar_t *w, wchar_t *ws, size_t n, _coll_info *ci, int *e)
{
	char *mb1 = NULL;
	int len;
	int ret;
	int size;
	int my_error = 0;

	/*
	 * Create multibyte string.
	 */
	for (len = 0; ws[len] != 0; len++);
	size = len*MB_CUR_MAX + 1;
	mb1 = malloc(size);
	if (mb1 == NULL) {
		/*
		 * Could not allocate memory.
		 */
		*e = 10;
		return (-1);
	}
	ret = wcstombs(mb1, ws, size);
	if (ret == -1) {
		*e = 20;
		free(mb1);
		return (-1);
	}

	/*
	 * Let _xpg4_strxfrm_() do the work.
	 */
	ret = _xpg4_strxfrm_(T_WCSXFRM, (char *)w, mb1, n, ci, &my_error);

	/*
	 * You can return.
	 */
	*e = my_error;
	free(mb1);

	return (ret);
}
