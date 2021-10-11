/* strcoll() and strxfrm(). */
/* This is Sun's propriatry implementation of strxfrm() and strcoll()	*/
/* using dynamic linking.  It is probably free from AT&T copyright.	*/
/* 	COPYRIGHT (C) 1991 SUN MICROSYSTEMS, INC.			*/
/*	ALL RIGHT RESERVED.						*/

#ident	"@(#)strxfrm.c	1.27	94/12/01 SMI"

#pragma weak strcoll = _strcoll
#pragma weak strxfrm = _strxfrm
#pragma weak strcoll_C = _strcoll_C
#pragma weak strxfrm_C = _strxfrm_C

/* Until "synonyms.h" gets fixed up. */
#define	dlopen	_dlopen
#define	dlsym	_dlsym
#define	dlclose	_dlclose

#include "_libc_gettext.h"
#include <dlfcn.h>
#include <locale.h>
#include <stdio.h> /* BUFSIZ and fprintf() in case of error. */
#define	MAXLOCALENAME 24

extern int	strcmp(const char *, const char *);
extern char *	strcpy(char *, const char *);
extern char *	strncpy(char *, const char *, size_t);

static size_t	strxfrm_C(char *s1, const char *s2, size_t n);
static int	strcoll_C(const char *s1, const char *s2);

static size_t	_strxfrm_C(char *s1, const char *s2, size_t n);
static int	_strcoll_C(const char *s1, const char *s2);

#ifdef DEBUG
extern const char *getenv(const char *);
#define	GET_LC_COLLATE	_setlocale(LC_COLLATE, NULL) /* Portable. */
#else /* !DEBUG */
#include "_locale.h"
#define	GET_LC_COLLATE	_cur_locale[LC_COLLATE]	/* Non-portable. */
#endif /* !DEBUG */

#if defined(PIC)	/* Compiled for shared lib. */
/* strcoll() and strxfrm() in this implementation are just executor	*/
/* of _strcoll_() and _strxfrm_() found in the  dynamic linkable	*/
/* shared objext found in 						*/
/*	/usr/lib/locale/${LC_COLLATE}/LC_COLLATE/coll.so		*/
/* In "C" locale, strcoll() is strcmp() and strxfrm() is like strcpy().	*/

static size_t	(*strxfrm_fp)(char *, const char *, size_t) = strxfrm_C;
static void 	*strcoll_dl_handle = NULL;
static void	loadcoll(void);
static int	(*strcoll_fp)(const char *, const char *) = strcmp;

size_t
_strxfrm_dyn(char *s1, const char *s2, size_t n)
{
	loadcoll();
	return (*strxfrm_fp)(s1, s2, n);
}

int
_strcoll_dyn(const char *s1, const char *s2)
{

	loadcoll();
	return (*strcoll_fp)(s1, s2);
}

/*
 * Check which collation routine you want to use.
 *      If ( ./locale/<locale>/LC_COLLATE/CollTable exists)
 *              _collate_xpg = 1
 *      else
 *              _collate_xpg = 0
 */
static int _collate_xpg = 0;


size_t
_strxfrm(char *s1, const char *s2, size_t n)
{
	if (_is_xpg_collate() == 1)
		return (_strxfrm_xpg4(s1, s2, n));
	else
		return (_strxfrm_dyn(s1, s2, n));
}

int
_strcoll(const char *s1, const char *s2)
{
	if (_is_xpg_collate() == 1)
		return (_strcoll_xpg4(s1, s2));
	else
		return (_strcoll_dyn(s1, s2));
}

/* * * INTERNAL FUNCTIONS * * */
static
void
loadcoll(void)
{
	char		strcoll_dl_name[BUFSIZ];
	char		*cur_locale = GET_LC_COLLATE;
	static char 	last_locale[MAXLOCALENAME+1] = {0};

	if (strcmp(last_locale, cur_locale) == 0)
		return;

	/* Locale has been changed -or- first time initialization. */

	if (strcmp(cur_locale, (const char *)"C") == 0) {
		/*
		 * C is a special case - pick up strcmp()
		 *   w/o dynamic linking.
		 */
		strcoll_fp = strcmp;
		strxfrm_fp = strxfrm_C;
		goto success;
	}

	/*
	 * Otherwise, try linking with the locale's "coll.so"
	 * shared object.
	 */
	/* Close the handle if it's been open. */
	if (strcoll_dl_handle) _dlclose(strcoll_dl_handle);

	/* Attach new module. */
#ifdef DEBUG
	{
	char *root = getenv("LC_ROOT");
	strcpy(strcoll_dl_name, root?root:"/usr/lib/locale");
	strcat(strcoll_dl_name, "/");
	}
#else /* !DEBUG */
	strcpy(strcoll_dl_name, (const char *)"/usr/lib/locale/");
#endif
	strcat(strcoll_dl_name, cur_locale);
	strcat(strcoll_dl_name, (const char *)"/LC_COLLATE/coll.so");
#ifdef DEBUG
	printf("Loading %s...\n", strcoll_dl_name);
#endif
	if ((strcoll_dl_handle = _dlopen(strcoll_dl_name, RTLD_LAZY)) &&
	    (strxfrm_fp = (size_t (*)(char *, const char *, size_t))
	    _dlsym(strcoll_dl_handle, (char *)((const char *)"_strxfrm_"))) &&
	    (strcoll_fp = (int (*)(const char *, const char *))
	    _dlsym(strcoll_dl_handle, (char *)((const char *)"_strcoll_"))))
		goto success;

	/*
	 * Loading failed - display warning and continue to run
	 * in "C" locale.
	 */
	fprintf(stderr, _libc_gettext((const char *)"libc:strcoll/xfrm:%s\n"),
		_dlerror());
	strxfrm_fp = strxfrm_C;
	strcoll_fp = strcmp;
	/* Continue, pretending loading was OK. */
success:
	strcpy(last_locale, cur_locale);
} /* loadcoll */
#else /* !PIC  --- compiled for static libc. */
/* strcoll() and strxfrm() in static libc are limited to the "C"	*/
/* behavior: strcoll() is strcmp() and strxfrm() is like strcpy().	*/

size_t
/* ARGSUSED0 */
_strxfrm_dyn(char *s1, const char *s2, size_t n)
{
}

size_t
/* ARGSUSED0 */
_strcoll_dyn(char *s1, const char *s2, size_t n)
{
}


size_t
_strxfrm(char *s1, const char *s2, size_t n)
{
	return (_strxfrm_C(s1, s2, n));
}

int
_strcoll(const char *s1, const char *s2)
{
	return (_strcoll_C(s1, s2));
}

#endif /* !PIC */

static int
_strcoll_C(const char *s1, const char *s2)
{
	return (strcmp(s1, s2));
}

static size_t
_strxfrm_C(char *s1, const char *s2, size_t n)
/* strxfrm() in the "C" locale. */
{

	if (n != 0)
		strncpy(s1, s2, n);
	return (strlen(s2));
}
