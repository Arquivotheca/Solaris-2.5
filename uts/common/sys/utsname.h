/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All Rights Reserved
 */

#ifndef _SYS_UTSNAME_H
#define	_SYS_UTSNAME_H

#pragma ident	"@(#)utsname.h	1.24	95/07/14 SMI"	/* From SVr4.0 11.14 */

#include <sys/feature_tests.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * If you are compiling the kernel, the value used in initializing
 * the utsname structure in the master.d/kernel file better be the
 * same as SYS_NMLN.
 */
#if (defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE)) && !defined(_KERNEL)

#define	_SYS_NMLN	257	/* 4.0 size of utsname elements */
				/* Must be at least 257 to 	*/
				/* support Internet hostnames.  */

#if defined(__EXTENSIONS__)
#define	SYS_NMLN	_SYS_NMLN	/* Make visible the advertized, but */
					/* namespace inappropriate name. */
#endif

struct utsname {
	char	sysname[_SYS_NMLN];
	char	nodename[_SYS_NMLN];
	char	release[_SYS_NMLN];
	char	version[_SYS_NMLN];
	char	machine[_SYS_NMLN];
};

#else	/* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) || */
	/* defined(_KERNEL) */

#define	SYS_NMLN	257	/* 4.0 size of utsname elements */
				/* Must be at least 257 to 	*/
				/* support Internet hostnames.  */

struct utsname {
	char	sysname[SYS_NMLN];
	char	nodename[SYS_NMLN];
	char	release[SYS_NMLN];
	char	version[SYS_NMLN];
	char	machine[SYS_NMLN];
};

#endif /* defined (_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) */

#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern struct utsname utsname;
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) ... */

#if !defined(_KERNEL)

#if defined(i386) || defined(__i386)

#if defined(__STDC__)

static int uname(struct utsname *);
static int _uname(struct utsname *);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int nuname(struct utsname *);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) ... */
extern int _nuname(struct utsname *);

#else	/* defined(__STDC__) */

static int uname();
static int _uname();
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int nuname();
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) ... */
extern int _nuname();

#endif	/* defined(__STDC__) */

static int
#if defined(__STDC__)
_uname(struct utsname *_buf)
#else
_uname(_buf)
struct utsname *_buf;
#endif
{
	return (_nuname(_buf));
}

static int
#if defined(__STDC__)
uname(struct utsname *_buf)
#else
uname(_buf)
struct utsname *_buf;
#endif
{
	return (_nuname(_buf));
}

#else	/* defined(i386) || defined(__i386) */

#if defined(__STDC__)
extern int uname(struct utsname *);
#else
extern int uname();
#endif	/* (__STDC__) */

#endif	/* defined(i386) || defined(__i386) */

#endif	/* !(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UTSNAME_H */
