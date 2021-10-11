/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _STRING_H
#define	_STRING_H

#pragma ident	"@(#)string.h	1.17	95/08/28 SMI"	/* SVr4.0 1.7.1.12 */

#include <sys/feature_tests.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _SIZE_T
#define	_SIZE_T
typedef unsigned int	size_t;
#endif

#ifndef NULL
#define	NULL	0
#endif

#if defined(__STDC__)

extern void *memcpy(void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern char *strcpy(char *, const char *);
extern char *strncpy(char *, const char *, size_t);

extern char *strcat(char *, const char *);
extern char *strncat(char *, const char *, size_t);

extern int memcmp(const void *, const void *, size_t);
extern int strcmp(const char *, const char *);
extern int strcoll(const char *, const char *);
extern int strncmp(const char *, const char *, size_t);
extern size_t strxfrm(char *, const char *, size_t);

extern void *memchr(const void *, int, size_t);
extern char *strchr(const char *, int);
extern size_t strcspn(const char *, const char *);
extern char *strpbrk(const char *, const char *);
extern char *strrchr(const char *, int);
extern size_t strspn(const char *, const char *);
extern char *strstr(const char *, const char *);
extern char *strtok(char *, const char *);
#if	defined(__EXTENSIONS__) || defined(_REENTRANT) || \
	    (_POSIX_C_SOURCE - 0 >= 199506L)
extern char *strtok_r(char *, const char *, char **);
#endif	/* defined(__EXTENSIONS__) || defined(_REENTRANT) .. */
extern void *memset(void *, int, size_t);
extern char *strerror(int);
extern size_t strlen(const char *);

#if defined(__EXTENSIONS__) || __STDC__ == 0 || \
		defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE)

extern void *memccpy(void *, const void *, int, size_t);

#endif

#if defined(__EXTENSIONS__) || (__STDC__ == 0 && \
		!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))

extern char *strdup(const char *);
extern char *strsignal(int);
extern int ffs(const int);
extern int strcasecmp(const char *, const char *);
extern int strncasecmp(const char *, const char *, size_t);

#endif

#else	/* __STDC__ */

extern char
	*strcpy(),
	*strncpy(),
	*strcat(),
	*strncat(),
	*strchr(),
	*strrchr(),
	*strpbrk(),
	*strtok(),
#if	defined(__EXTENSIONS__) || defined(_REENTRANT) || \
	    (_POSIX_C_SOURCE - 0 >= 199506L)
	*strtok_r(),
#endif	/* defined(__EXTENSIONS__) || defined(_REENTRANT).. */
	*strstr();
extern int
	strcmp(),
	strncmp();
extern size_t
	strlen(),
	strspn(),
	strcspn();
extern void
	*memccpy(),
	*memchr(),
	*memcpy(),
	*memmove(),
	*memset();
extern int memcmp();

#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
extern char
	*strdup(),
	*strsignal();
extern int
	ffs(),
	strcasecmp(),
	strncasecmp();
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) */

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _STRING_H */
