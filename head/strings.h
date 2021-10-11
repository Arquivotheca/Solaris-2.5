/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_STRINGS_H
#define	_STRINGS_H

#pragma ident	"@(#)strings.h	1.1	95/03/01 SMI"

#include <sys/feature_tests.h>

#if !defined(_XOPEN_SOURCE)
#include <string.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__STDC__)

extern int bcmp(const void *s1, const void *s2, size_t n);
extern void bcopy(const void *s1, void *s2, size_t n);
extern void bzero(void *s, size_t n);

extern char *index(const char *s, int c);
extern char *rindex(const char *s, int c);

#else

extern int bcmp();
extern void bcopy();
extern void bzero();

extern char *index();
extern char *rindex();

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _STRINGS_H */
