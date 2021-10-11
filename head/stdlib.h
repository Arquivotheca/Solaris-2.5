/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _STDLIB_H
#define	_STDLIB_H

#pragma ident	"@(#)stdlib.h	1.27	95/08/28 SMI"	/* SVr4.0 1.22	*/

#include <sys/feature_tests.h>

#if defined(__EXTENSIONS__) || \
	(defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4))
#include <sys/wait.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

typedef	struct {
	int	quot;
	int	rem;
} div_t;

typedef struct {
	long	quot;
	long	rem;
} ldiv_t;

#if __STDC__ - 0 == 0 && !defined(_NO_LONGLONG)
typedef struct {
	long long	quot;
	long long	rem;
} lldiv_t;
#endif	/* __STDC__ - 0 == 0 && !defined(_NO_LONGLONG) */

#ifndef _SIZE_T
#define	_SIZE_T
typedef unsigned int    size_t;
#endif

#ifndef _UID_T
#define	_UID_T
typedef long	uid_t;
#endif

#ifndef NULL
#define	NULL	0
#endif

#define	EXIT_FAILURE	1
#define	EXIT_SUCCESS    0
#define	RAND_MAX	32767

#ifndef _WCHAR_T
#define	_WCHAR_T
typedef long wchar_t;
#endif

#if defined(__STDC__)

extern unsigned char	__ctype[];

#define	MB_CUR_MAX	__ctype[520]

extern double atof(const char *);
extern int atoi(const char *);
extern long int atol(const char *);
extern double strtod(const char *, char **);
extern long int strtol(const char *, char **, int);
extern unsigned long int strtoul(const char *, char **, int);

extern int rand(void);
extern void srand(unsigned int);
#if	defined(__EXTENSIONS__) || defined(_REENTRANT) || \
	    (_POSIX_C_SOURCE - 0 >= 199506L)
extern int rand_r(unsigned int *);
#endif	/* defined(__EXTENSIONS__) || defined(_REENTRANT).. */

extern void *calloc(size_t, size_t);
extern void free(void *);
extern void *malloc(size_t);
extern void *realloc(void *, size_t);

extern void abort(void);
extern int atexit(void (*)(void));
extern void exit(int);
extern char *getenv(const char *);
extern int system(const char *);

extern void *bsearch(const void *, const void *, size_t, size_t,
	int (*)(const void *, const void *));
extern void qsort(void *, size_t, size_t,
	int (*)(const void *, const void *));

extern int abs(int);
extern div_t div(int, int);
extern long int labs(long);
extern ldiv_t ldiv(long, long);

extern int mbtowc(wchar_t *, const char *, size_t);
extern int mblen(const char *, size_t);
extern int wctomb(char *, wchar_t);

extern size_t mbstowcs(wchar_t *, const char *, size_t);
extern size_t wcstombs(char *, const wchar_t *, size_t);

#if defined(__EXTENSIONS__) || \
	(__STDC__ == 0 && !defined(_POSIX_C_SOURCE)) || \
	(defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4))

extern double drand48(void);
extern double erand48(unsigned short *);
extern long jrand48(unsigned short *);
extern void lcong48(unsigned short *);
extern long lrand48(void);
extern long mrand48(void);
extern long nrand48(unsigned short *);
extern unsigned short *seed48(unsigned short *);
extern void srand48(long);
extern int putenv(const char *);
extern void setkey(const char *);

#endif /* defined(__EXTENSIONS__) || (__STDC__ == 0 && ... */

#if (defined(__EXTENSIONS__) || \
	(__STDC__ == 0 && !defined(_POSIX_C_SOURCE))) && \
	(_XOPEN_VERSION - 0 < 4)
extern void swab(const char *, char *, int);
#endif

#if defined(__EXTENSIONS__) || \
	(__STDC__ == 0 && !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))

extern long a64l(const char *);
extern int dup2(int, int);
extern char *ecvt(double, int, int *, int *);
extern char *fcvt(double, int, int *, int *);
extern char *qecvt(long double, int, int *, int *);
extern char *qfcvt(long double, int, int *, int *);
extern char *qgcvt(long double, int, char *);
extern char *getcwd(char *, size_t);
extern char *getlogin(void);
extern int getopt(int, char *const *, const char *);
extern int getsubopt(char **, char *const *, char **);
extern char *optarg;
extern int optind, opterr, optopt;
extern char *getpass(const char *);
extern int getpw(uid_t, char *);
extern char *gcvt(double, int, char *);
extern int isatty(int);
extern char *l64a(long);
extern void *memalign(size_t, size_t);
extern char *mktemp(char *);
extern char *realpath(char *, char *);
extern char *ttyname(int);
extern int ttyslot(void);
extern void *valloc(size_t);
extern char *ptsname(int);
extern int  grantpt(int);
extern int  unlockpt(int);

#if __STDC__ - 0 == 0 && !defined(_NO_LONGLONG)
extern long long atoll(const char *);
extern long long llabs(long long);
extern lldiv_t lldiv(long long, long long);
extern char *lltostr(long long, char *);
extern long long strtoll(const char *, char **, int);
extern unsigned long long strtoull(const char *, char **, int);
extern char *ulltostr(unsigned long long, char *);
#endif	/* __STDC__ - 0 == 0 && !defined(_NO_LONGLONG) */

#endif

#else /* not __STDC__ */

extern unsigned char	_ctype[];

#define	MB_CUR_MAX	_ctype[520]

extern double atof();
extern int atoi();
extern long int atol();
extern double strtod();
extern long int strtol();
extern unsigned long strtoul();

extern int rand();
extern void srand();
#if	defined(__EXTENSIONS__) || defined(_REENTRANT) || \
	    (_POSIX_C_SOURCE - 0 >= 199506L)
extern int rand_r();
#endif	/* defined(__EXTENSIONS__) || defined(_REENTRANT).. */

extern void *calloc();
extern void free();
extern void *malloc();
extern void *realloc();

extern void abort();
extern int atexit();
extern void exit();
extern char *getenv();
extern int system();

extern void *bsearch();
extern void qsort();

extern int abs();
extern div_t div();
extern long int labs();
extern ldiv_t ldiv();

extern int mbtowc();
extern int mblen();
extern int wctomb();

extern size_t mbstowcs();
extern size_t wcstombs();

extern long a64l();
extern int dup2();
extern char *ecvt();
extern char *fcvt();
extern char *qecvt();
extern char *qfcvt();
extern char *qgcvt();
extern char *getcwd();
extern char *getlogin();
extern int getopt();
extern int getsubopt();
extern char *optarg;
extern int optind, opterr, optopt;
extern char *getpass();
extern int getpw();
extern char *gcvt();
extern int isatty();
extern char *l64a();
extern void *memalign();
extern char *mktemp();
extern int putenv();
extern char *realpath();
extern void setkey();
extern void swab();
extern char *ttyname();
extern int ttyslot();
extern void *valloc();
extern char *ptsname();
extern int  grantpt();
extern int  unlockpt();

extern double drand48();
extern double erand48();
extern long jrand48();
extern void lcong48();
extern long lrand48();
extern long mrand48();
extern long nrand48();
extern unsigned short *seed48();
extern void srand48();

#if !defined(_NO_LONGLONG)
extern long long atoll();
extern long long llabs();
extern lldiv_t lldiv();
extern char *lltostr();
extern long long strtoll();
extern unsigned long long strtoull();
extern char *ulltostr();
#endif	/* !defined(_NO_LONGLONG) */

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _STDLIB_H */
