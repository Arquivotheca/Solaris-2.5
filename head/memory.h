/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef	_MEMORY_H
#define	_MEMORY_H

#pragma ident	"@(#)memory.h	1.8	92/07/14 SMI"	/* SVr4.0 1.4.1.2 */

#ifdef	__cplusplus
extern "C" {
#endif

/* from stddef.h */
#ifndef _SIZE_T
#define	_SIZE_T
typedef	unsigned	size_t;
#endif

#if defined(__STDC__)
extern void *memccpy(void *, const void *, int, size_t);
extern void *memchr(const void *, int, size_t);
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int memcmp(const void *, const void *, size_t);
#else
extern void
	*memccpy(),
	*memchr(),
	*memcpy(),
	*memset();
extern int memcmp();
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMORY_H */
