/*	Copyright (c) 1991 Sun Microsystems	*/
/*	  All Rights Reserved  	*/
#define	dlopen	_dlopen
#define	dlsym	_dlsym
#define	dlclose	_dlclose

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <euc.h>
#include <widec.h>
#include <wctype.h>
#include <limits.h>
#include <synch.h>
#include <thread.h>
#include "mtlibintl.h"

#pragma weak wdinit = _wdinit
#pragma weak wdchkind = _wdchkind
#pragma weak wdbindf = _wdbindf
#pragma weak wddelim = _wddelim
#pragma weak mcfiller = _mcfiller
#pragma weak mcwrap = _mcwrap

#if defined(PIC)

#include <locale.h>
#include <dlfcn.h>

#define	LC_NAMELEN 255	/* From _locale.h of libc. */
#define RETURN(x)  { int i = x; mutex_unlock(&wd_lock); return i; }
#define RETURN_C(x)  { wchar_t *i = x; mutex_unlock(&wd_lock); return i; }
#ifdef _REENTRANT
static mutex_t wd_lock = DEFAULTMUTEX;
#endif _REENTRANT

static int	wdchkind_C(wchar_t);
static int	(*wdchknd)(wchar_t) = wdchkind_C;
static int	wdbindf_C(wchar_t, wchar_t, int);
static int	(*wdbdg)(wchar_t, wchar_t, int) = wdbindf_C;
static wchar_t	*wddelim_C(wchar_t, wchar_t, int);
static wchar_t	*(*wddlm)(wchar_t, wchar_t, int) = wddelim_C;
static wchar_t	(*mcfllr)(void) = NULL;
static int	(*mcwrp)(void) = NULL;
static void	*modhandle = NULL;
static int	initialized = 0;

#endif /* !PIC */

/*
 * _wdinit() initializes other word-analyzing routines according to the
 * current locale.  Programmers are supposed to call this routine
 * every time the locale for the LC_CTYPE category is changed.  It returns
 * 0 when every initialization completes successfully, or -1 otherwise.
 */
int
_wdinit()
{
#if defined(PIC)

	char wdmodpath[LC_NAMELEN + 39];

	if (modhandle)
		(void) dlclose(modhandle);
#ifdef I18NDEBUG
	strcpy(wdmodpath, getenv("LC_ROOT") ?
	    getenv("LC_ROOT") : "/usr/lib/locale");
	strcat(wdmodpath, "/");
#else
	strcpy(wdmodpath, "/usr/lib/locale/");
#endif /* I18NDEBUG */
	strcat(wdmodpath, setlocale(LC_CTYPE, NULL));
	strcat(wdmodpath, "/LC_CTYPE/wdresolve.so");
	if ((modhandle = dlopen(wdmodpath, RTLD_LAZY)) != NULL) {
		wdchknd = (int(*)(wchar_t))dlsym(modhandle, "_wdchkind_");
		if (wdchknd == NULL) wdchknd = wdchkind_C;
		wdbdg = (int(*)(wchar_t, wchar_t, int))dlsym(modhandle,
			"_wdbindf_");
		if (wdbdg == NULL) wdbdg = wdbindf_C;
		wddlm = (wchar_t *(*)(wchar_t, wchar_t, int))
			dlsym(modhandle, "_wddelim_");
		if (wddlm == NULL) wddlm = wddelim_C;
		mcfllr = (wchar_t(*)())dlsym(modhandle, "_mcfiller_");
		mcwrp = (int(*)())dlsym(modhandle, "_mcwrap_");
	} else {
		wdchknd = wdchkind_C;
		wdbdg = wdbindf_C;
		wddlm = wddelim_C;
		mcfllr = NULL;
		mcwrp = NULL;
	}
	initialized = 1;
	return ((modhandle && wdchknd && wdbdg && wddlm && mcfllr && mcwrp) ?
		0 : -1);
#else /* !PIC */
	return (0);	/* A fake success from static lib version. */
#endif /* !PIC */
}

/*
 * _wdchkind() returns a non-negative integral value unique to the kind
 * of the character represented by given argument.
 */
int
_wdchkind(wchar_t wc)
{
#if defined(PIC)
	mutex_lock(&wd_lock);
	if (!initialized)
		_wdinit();
	RETURN ((*wdchknd)(wc));
}
static int
wdchkind_C(wchar_t wc)
{
#endif /* !PIC */
	switch (wcsetno(wc)) {
		case 1:
			return (2);
			break;	/* NOT REACHED */
		case 2:
			return (3);
			break;	/* NOT REACHED */
		case 3:
			return (4);
			break;	/* NOT REACHED */
		case 0:
			return (isalpha(wc) || isdigit(wc) || wc == ' ');
			break;	/* NOT REACHED */
	}
	return (0);
}

/*
 * _wdbindf() returns an integral value (0 - 7) indicating binding
 *  strength of two characters represented by the first two arguments.
 * It returns -1 when either of the two character is not printable.
 */
int
_wdbindf(wchar_t wc1, wchar_t wc2, int type)
{
#if defined(PIC)
	mutex_lock(&wd_lock);
	if (!initialized)
		_wdinit();
	if (!iswprint(wc1) || !iswprint(wc2))
		return (-1);
	RETURN ((*wdbdg)(wc1, wc2, type));
}
static int
wdbindf_C(wchar_t wc1, wchar_t wc2, int type)
{
#else
	if (!iswprint(wc1) || !iswprint(wc2))
		return (-1);
#endif /* !PIC */
	if (csetlen((int)wc1) > 1 && csetlen((int)wc2) > 1)
		return (4);
	return (6);
}

/*
 * _wddelim() returns a pointer to a null-terminated word delimiter
 * string in wchar_t type that is thought most appropriate to join
 * a text line ending with the first argument and a line beginning
 * with the second argument, with.  When either of the two character
 * is not printable it returns a pointer to a null wide character.
 */
wchar_t *
_wddelim(wchar_t wc1, wchar_t wc2, int type)
{
#if defined(PIC)
	mutex_lock(&wd_lock);
	if (!initialized)
		_wdinit();
	if (!iswprint(wc1) || !iswprint(wc2))
		RETURN_C (L"");
	RETURN_C ((*wddlm)(wc1, wc2, type));
}
static wchar_t *
wddelim_C(wchar_t wc1, wchar_t wc2, int type)
{
#else /* !PIC */
	if (!iswprint(wc1) || !iswprint(wc2))
		return (L"");
#endif /* !PIC */
	return (L" ");
}

/*
 * _mcfiller returns a printable ASCII character suggested for use in
 * filling space resulted by a multicolumn character at the right margin.
 */
wchar_t
_mcfiller()
{
#if defined(PIC)
	wchar_t fillerchar;

	mutex_lock(&wd_lock);
	if (!initialized)
		_wdinit();
	if (mcfllr) {
		fillerchar = (*mcfllr)();
		if (!fillerchar)
			fillerchar = '~';
		if (iswprint(fillerchar)) {
			mutex_unlock(&wd_lock);
			return (fillerchar);
		}
	}
	mutex_unlock(&wd_lock);
	return ('~');
#else /* !PIC */
	return ('~');
#endif /* !PIC */
}

/*
 * mcwrap returns an integral value indicating if a multicolumn character
 * on the right margin should be wrapped around on a terminal screen.
 */
int
_mcwrap()
{
#if defined(PIC)

	mutex_lock(&wd_lock);
	if (!initialized)
		_wdinit();
	if (mcwrp)
		if ((*mcwrp)() == 0)
			RETURN (0);
	RETURN (1);
#else /* !PIC */
	return (1);
#endif /* !PIC */
}
