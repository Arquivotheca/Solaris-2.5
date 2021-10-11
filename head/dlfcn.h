/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _DLFCN_H
#define	_DLFCN_H

#pragma ident	"@(#)dlfcn.h	1.14	93/10/26 SMI"	/* SVr4.0 1.2	*/

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Information structure returned by dladdr().
 */
typedef struct	dl_info {
	const char *	dli_fname;	/* file containing address range */
	void *		dli_fbase;	/* base address of file image */
	const char *	dli_sname;	/* symbol name */
	void *		dli_saddr;	/* symbol address */
} Dl_info;

/*
 * Declarations used for dynamic linking support routines.
 */
#ifdef __STDC__
extern void *	dlopen(const char *, int);
extern void *	dlsym(void *, const char *);
extern int	dlclose(void *);
extern char *	dlerror(void);
extern int	dladdr(void *, Dl_info *);
#else
extern void *	dlopen();
extern void *	dlsym();
extern int	dlclose();
extern char *	dlerror();
extern int	dladdr();
#endif

/*
 * Valid values for handle argument to dlsym(3x).
 */
#define	RTLD_NEXT	(void *)-1	/* look in `next' shared object */

/*
 * Valid values for mode argument to dlopen.
 */
#define	RTLD_LAZY	1		/* lazy function call binding */
#define	RTLD_NOW	2		/* immediate function call binding */
#define	RTLD_GLOBAL	0x100		/* allow symbols to be global */


#ifdef	__cplusplus
}
#endif

#endif	/* _DLFCN_H */
