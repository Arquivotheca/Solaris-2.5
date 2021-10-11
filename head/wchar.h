/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef	_WCHAR_H
#define	_WCHAR_H

#pragma ident	"@(#)wchar.h	1.4	95/03/08 SMI"

#include <stdio.h>
#include <ctype.h>
#include <stddef.h>
#include <time.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _WCHAR_T
#define	_WCHAR_T
typedef	long	wchar_t;
#endif

#ifndef	_WINT_T
#define	_WINT_T
typedef	long	wint_t;
#endif

#ifndef	_WCTYPE_T
#define	_WCTYPE_T
typedef	int	wctype_t;
#endif

#ifndef	WEOF
#define	WEOF	(-1)
#endif

#ifdef __STDC__

extern int iswalpha(wint_t);
extern int iswupper(wint_t);
extern int iswlower(wint_t);
extern int iswdigit(wint_t);
extern int iswxdigit(wint_t);
extern int iswalnum(wint_t);
extern int iswspace(wint_t);
extern int iswpunct(wint_t);
extern int iswprint(wint_t);
extern int iswgraph(wint_t);
extern int iswcntrl(wint_t);
extern int iswctype(wint_t, wctype_t);
extern wint_t towlower(wint_t);
extern wint_t towupper(wint_t);
extern wint_t fgetwc(FILE *);
extern wchar_t *fgetws(wchar_t *, int, FILE *);
extern wint_t fputwc(wint_t, FILE *);
extern int fputws(const wchar_t *, FILE *);
extern wint_t ungetwc(wint_t, FILE *);
extern wint_t getwc(FILE *);
extern wint_t getwchar(void);
extern wint_t putwc(wint_t, FILE *);
extern wchar_t putwchar(wint_t);
extern double wcstod(const wchar_t *, wchar_t **);
extern long wcstol(const wchar_t *, wchar_t **, int);
extern unsigned long wcstoul(const wchar_t *, wchar_t **, int);
extern wchar_t *wcscat(wchar_t *, const wchar_t *);
extern wchar_t *wcschr(const wchar_t *, wchar_t);
extern int wcscmp(const wchar_t *, const wchar_t *);
extern int wcscoll(const wchar_t *, const wchar_t *);
extern wchar_t *wcscpy(wchar_t *, const wchar_t *);
extern size_t wcscspn(const wchar_t *, const wchar_t *);
extern size_t wcslen(const wchar_t *);
extern wchar_t *wcsncat(wchar_t *, const wchar_t *, size_t);
extern int wcsncmp(const wchar_t *, const wchar_t *, size_t);
extern wchar_t *wcsncpy(wchar_t *, const wchar_t *, size_t);
extern wchar_t *wcspbrk(const wchar_t *, const wchar_t *);
extern wchar_t *wcsrchr(const wchar_t *, wchar_t);
extern size_t wcsspn(const wchar_t *, const wchar_t *);
extern wchar_t *wcstok(wchar_t *, const wchar_t *);
extern wchar_t *wcswcs(const wchar_t *, const wchar_t *);
extern int wcswidth(const wchar_t *, size_t);
extern size_t wcsxfrm(wchar_t *, const wchar_t *, size_t);
extern int wcwidth(const wchar_t);
extern size_t wcsftime(wchar_t *, size_t, const char *, const struct tm *);
extern wctype_t wctype(const char *);

#else

extern  int iswalpha();
extern  int iswupper();
extern  int iswlower();
extern  int iswdigit();
extern  int iswxdigit();
extern  int iswalnum();
extern  int iswspace();
extern  int iswpunct();
extern  int iswprint();
extern  int iswgraph();
extern  int iswcntrl();
extern  int iswctype();
extern  wint_t towlower();
extern  wint_t towupper();
extern  wint_t fgetwc();
extern  wchar_t *fgetws();
extern  wint_t fputwc();
extern  int fputws();
extern  wint_t  ungetwc();
extern wint_t getwc();
extern wint_t getwchar();
extern wint_t putwc();
extern wchar_t putwchar();
extern wint_t ungetwc();
extern double wcstod();
extern long wcstol();
extern unsigned long wcstoul();
extern wchar_t *wcscat();
extern wchar_t *wcschr();
extern int wcscmp();
extern int wcscoll();
extern wchar_t *wcscpy();
extern size_t wcscspn();
extern size_t wcslen();
extern wchar_t *wcsncat();
extern int wcsncmp();
extern wchar_t *wcsncpy();
extern wchar_t *wcspbrk();
extern wchar_t *wcsrchr();
extern size_t wcsspn();
extern wchar_t *wcstok();
extern wchar_t *wcswcs();
extern int wcswidth();
extern size_t wcsxfrm();
extern int wcwidth();
extern size_t wcsftime();
extern wctype_t wctype();

#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _WCHAR_H */
