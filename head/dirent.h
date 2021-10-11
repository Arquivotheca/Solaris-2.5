/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _DIRENT_H
#define	_DIRENT_H

#pragma ident	"@(#)dirent.h	1.19	95/08/28 SMI"	/* SVr4.0 1.6.1.5   */

#include <sys/feature_tests.h>

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__EXTENSIONS__) || \
	(!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))

#define	MAXNAMLEN	512		/* maximum filename length */
#define	DIRBUF		1048		/* buffer size for fs-indep. dirs */

#endif /* defined(__EXTENSIONS__) || (!defined(_POSIX_C_SOURCE) ... */

#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)

typedef struct
	{
	int	dd_fd;		/* file descriptor */
	int	dd_loc;		/* offset in block */
	int	dd_size;	/* amount of valid data */
	char	*dd_buf;	/* directory block */
	}	DIR;		/* stream data from opendir() */


#else

typedef struct
	{
	int	d_fd;		/* file descriptor */
	int	d_loc;		/* offset in block */
	int	d_size;		/* amount of valid data */
	char	*d_buf;		/* directory block */
	}	DIR;		/* stream data from opendir() */

#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) ...  */

#if defined(__STDC__)

extern DIR		*opendir(const char *);
extern struct dirent	*readdir(DIR *);
#if defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE) || \
	defined(_XOPEN_SOURCE)
extern long		telldir(DIR *);
extern void		seekdir(DIR *, long);
#endif /* defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE) ... */
extern void		rewinddir(DIR *);
extern int		closedir(DIR *);

#else

extern DIR		*opendir();
extern struct dirent	*readdir();
#if defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE) || \
	defined(_XOPEN_SOURCE)
extern long		telldir();
extern void		seekdir();
#endif /* defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE) ... */
extern void		rewinddir();
extern int		closedir();

#endif

#if defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE) || \
	defined(_XOPEN_SOURCE)
#define	rewinddir(dirp)	seekdir(dirp, 0L)
#endif

/*
 * readdir_r() prototype is defined here.
 */

#if	defined(__EXTENSIONS__) || defined(_REENTRANT) || \
	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

#if	defined(__STDC__)

#if	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

#ifdef __PRAGMA_REDEFINE_EXTNAME
extern int readdir_r(DIR *, struct dirent *, struct dirent **);
#pragma redefine_extname readdir_r __posix_readdir_r
#else  /* __PRAGMA_REDEFINE_EXTNAME */

static int
readdir_r(DIR *__dp, struct dirent *__ent, struct dirent **__res)
{
	extern int __posix_readdir_r(DIR *, struct dirent *, struct dirent **);
	return (__posix_readdir_r(__dp, __ent, __res));
}
#endif /* __PRAGMA_REDEFINE_EXTNAME */

#else  /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

extern struct dirent *readdir_r(DIR *__dp, struct dirent *__ent);

#endif  /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

#else  /* __STDC__ */

#if	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

#ifdef __PRAGMA_REDEFINE_EXTNAME
extern int readdir_r();
#pragma redefine_extname readdir_r __posix_readdir_r
#else  /* __PRAGMA_REDEFINE_EXTNAME */

static int
readdir_r(__dp, __ent, __res)
	DIR *__dp;
	struct dirent *__ent;
	struct dirent **__res;
{
	extern int __posix_readdir_r();
	return (__posix_readdir_r(__dp, __ent, __res));
}
#endif /* __PRAGMA_REDEFINE_EXTNAME */

#else  /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

extern struct dirent *readdir_r();

#endif /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

#endif /* __STDC__ */

#endif /* defined(__EXTENSIONS__) || (__STDC__ == 0 ... */

#ifdef	__cplusplus
}
#endif

#include <sys/dirent.h>

#endif	/* _DIRENT_H */
