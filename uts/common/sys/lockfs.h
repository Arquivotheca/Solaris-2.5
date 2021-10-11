/*	Copyright (c) 1991 Sun Microsystems, Inc (SMI) */
/*	All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF 		*/
/*	Sun Microsystems, Inc					*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_LOCKFS_H
#define	_SYS_LOCKFS_H

#pragma ident	"@(#)lockfs.h	1.8	94/10/28 SMI"	/* SunOS-4.1.1 */

#ifdef	__cplusplus
extern "C" {
#endif

struct lockfs {
	u_long		lf_lock;	/* desired lock */
	u_long		lf_flags;	/* misc flags */
	u_long		lf_key;		/* lock key */
	u_long		lf_comlen;	/* length of comment */
	caddr_t		lf_comment;	/* address of comment */
};

/*
 * lf_lock and lf_locking
 */
#define	LOCKFS_ULOCK	0	/* unlock */
#define	LOCKFS_WLOCK	1	/* write  lock */
#define	LOCKFS_NLOCK	2	/* name   lock */
#define	LOCKFS_DLOCK	3	/* delete lock */
#define	LOCKFS_HLOCK	4	/* hard   lock */
#define	LOCKFS_ELOCK	5	/* error  lock */
#define	LOCKFS_ROELOCK	6	/* error  lock (read-only) - unimplemented */
#define	LOCKFS_MAXLOCK	6	/* maximum lock number */

/*
 * lf_flags
 */
#define	LOCKFS_BUSY	0x00000001	/* lock is being set */
#define	LOCKFS_MOD	0x00000002	/* file system modified */

#define	LOCKFS_MAXCOMMENTLEN	1024	/* maximum comment length */

/*
 * some nice checking macros
 */

#define	LOCKFS_IS_BUSY(LF)	((LF)->lf_flags & LOCKFS_BUSY)
#define	LOCKFS_IS_MOD(LF)	((LF)->lf_flags & LOCKFS_MOD)

#define	LOCKFS_CLR_BUSY(LF)	((LF)->lf_flags &= ~LOCKFS_BUSY)
#define	LOCKFS_CLR_MOD(LF)	((LF)->lf_flags &= ~LOCKFS_MOD)

#define	LOCKFS_SET_MOD(LF)	((LF)->lf_flags |= LOCKFS_MOD)
#define	LOCKFS_SET_BUSY(LF)	((LF)->lf_flags |= LOCKFS_BUSY)

#define	LOCKFS_IS_WLOCK(LF)	((LF)->lf_lock == LOCKFS_WLOCK)
#define	LOCKFS_IS_HLOCK(LF)	((LF)->lf_lock == LOCKFS_HLOCK)
#define	LOCKFS_IS_ROELOCK(LF)	((LF)->lf_lock == LOCKFS_ROELOCK)
#define	LOCKFS_IS_ELOCK(LF)	((LF)->lf_lock == LOCKFS_ELOCK)
#define	LOCKFS_IS_ULOCK(LF)	((LF)->lf_lock == LOCKFS_ULOCK)
#define	LOCKFS_IS_NLOCK(LF)	((LF)->lf_lock == LOCKFS_NLOCK)
#define	LOCKFS_IS_DLOCK(LF)	((LF)->lf_lock == LOCKFS_DLOCK)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_LOCKFS_H */
