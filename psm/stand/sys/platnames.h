/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_PLATNAMES_H
#define	_SYS_PLATNAMES_H

#pragma ident	"@(#)platnames.h	1.4	94/12/19 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef sparc

/*
 * Historical cruft for sun4c and early sun4m machines.
 * Private to the sparc implementation of the library.
 */
extern struct cputype2name {
	short	cputype;
	char	*mfgname;
	char	*iarch;
} _cputype2name_tbl[];

extern short _get_cputype(void);

#endif /* sparc */

/*
 * External interfaces
 */
extern char *get_mfg_name(void);
extern int open_platform_file(char *,
    int (*)(char *, void *), void *, char *, char *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PLATNAMES_H */
