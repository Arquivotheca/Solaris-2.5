/* wscoll() and wsxfrm(). */
/* This is Sun's propriatry implementation of wsxfrm() and wscoll()	*/
/* using dynamic linking.  It is probably free from AT&T copyright.	*/
/* 	COPYRIGHT (C) 1991 SUN MICROSYSTEMS, INC.			*/
/*	ALL RIGHT RESERVED.						*/

#ident	"@(#)wsxfrm.c	1.13	94/11/18 SMI"

/* Until "synonyms.h" gets fixed up. */
#define	dlopen	_dlopen
#define	dlsym	_dlsym
#define	dlclose	_dlclose

#include <dlfcn.h>
#include <locale.h>
#include <stdio.h> /* BUFSIZ */
#include <wchar.h>
#include <synch.h>
#include <thread.h>
#include "mtlibw.h"

#define	MAXLOCALENAME 24

extern int	wscmp(const wchar_t *, const wchar_t *);
extern wchar_t *wscpy(wchar_t *, const wchar_t *);
extern wchar_t *wsncpy(wchar_t *, const wchar_t *, size_t);

static size_t	wsxfrm_C(wchar_t *s1, const wchar_t *s2, size_t n);

#ifdef DEBUG
	extern const char *getenv(const char *);
#endif /* DEBUG */

#if defined(PIC)	/* Compiled for shared lib. */
/* wcscoll() and wcsxfrm() in this implementation are just executor	*/
/* of _wscoll_() and _wsxfrm_() found in the  dynamic linkable		*/
/* shared objext found in 						*/
/*	/usr/lib/locale/${LC_COLLATE}/LC_COLLATE/coll.so		*/
/* In "C" locale, wscoll() is wscmp() and wsxfrm() is like wscpy().	*/

static size_t	(*wsxfrm_fp)(wchar_t *, const wchar_t *, size_t) = wsxfrm_C;
static void 	*coll_dl_handle = NULL;
static void	loadcoll(void);
static int	(*wscoll_fp)(const wchar_t *, const wchar_t *) = wscmp;

size_t
_wcsxfrm_dyn(wchar_t *s1, const wchar_t *s2, size_t n)
{
	loadcoll();
	return ((*wsxfrm_fp)(s1, s2, n));
}

int
_wcscoll_dyn(const wchar_t *s1, const wchar_t *s2)
{
	loadcoll();
	return ((*wscoll_fp)(s1, s2));
}

/* * * INTERNAL FUNCTIONS * * */
static
void
loadcoll(void)
{
	char		coll_dl_name[BUFSIZ];
	char		*cur_locale = setlocale(LC_COLLATE, NULL);
	static char 	last_locale[MAXLOCALENAME+1] = {0};

	if (strcmp(last_locale, cur_locale) == 0)
		return;

	/* Locale has been changed -or- first time initialization. */
	if (strcmp(cur_locale, "C") == 0) {
		/*
		 * C is a special case - pick up wscmp()
		 *  w/o dynamic linking.
		 */
		wscoll_fp = wscmp;
		wsxfrm_fp = wsxfrm_C;
		goto success;
	}

	/*
	 * Otherwise, try linking with the locale's "coll.so"
	 * shared object.
	 */
	/* Close the handle if it's been open. */
	if (coll_dl_handle) _dlclose(coll_dl_handle);

	/* Attach new module. */
#ifdef DEBUG
	{
	char *root = getenv("LC_ROOT");
	strcpy(coll_dl_name, root ? root : "/usr/lib/locale");
	strcat(coll_dl_name, "/");
	}
#else /* !DEBUG */
	strcpy(coll_dl_name, "/usr/lib/locale/");
#endif
	strcat(coll_dl_name, cur_locale);
	strcat(coll_dl_name, "/LC_COLLATE/coll.so");
#ifdef DEBUG
	printf("Loading %s...\n", coll_dl_name);
#endif
	if ((coll_dl_handle = _dlopen(coll_dl_name, RTLD_LAZY)) &&
		(wsxfrm_fp = (size_t (*)(wchar_t *, const wchar_t *, size_t))
		_dlsym(coll_dl_handle, "_wsxfrm_")) &&
		(wscoll_fp = (int (*)(const wchar_t *, const wchar_t *))
		_dlsym(coll_dl_handle, "_wscoll_")))
			goto success;

	/* Loading failed - continue to run in "C" locale. */
	wsxfrm_fp = wsxfrm_C;
	wscoll_fp = wscmp;
	/* Continue, pretending loading was OK. */
success:
	strcpy(last_locale, cur_locale);
} /* loadcoll */
#else /* !PIC  --- compiled for static libc. */
/* wcscoll() and wcsxfrm() in static libc are limited to the "C"	*/
/* behavior: wcscoll() is wcscmp() and wcsxfrm() is like wcscpy().	*/
size_t
_wcsxfrm_dyn(wchar_t *s1, const wchar_t *s2, size_t n)
{
	return (wsxfrm_C(s1, s2, n));
}

int
_wcscoll_dyn(const wchar_t *s1, const wchar_t *s2)
{
	return (wscmp(s1, s2));
}
#endif /* !PIC */

size_t
_wsxfrm(wchar_t *s1, const wchar_t *s2, size_t n)
{
	return (wcsxfrm(s1, s2, n));
}

int
_wscoll(const wchar_t *s1, const wchar_t *s2)
{
	return (wcscoll(s1, s2));
}

static size_t
wsxfrm_C(wchar_t *s1, const wchar_t *s2, size_t n)
/* wsxfrm() in the "C" locale. */
{

	if (n != 0)
		wsncpy(s1, s2, n);
	return (wslen(s2));
}
