/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef _SYS_FS_UFS_MOUNT_H
#define	_SYS_FS_UFS_MOUNT_H

#pragma ident	"@(#)ufs_mount.h	1.8	94/10/28 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

struct ufs_args {
	int	flags;
};

struct ufs_args_fop {
	int	flags;
	long	toosoon;
};

/*
 * UFS mount option flags
 */
#define	UFSMNT_NOINTR	0x00000001	/* disallow interrupts on lockfs */
#define	UFSMNT_SYNCDIR	0x00000002	/* synchronous local directory ops */
#define	UFSMNT_NOSETSEC	0x00000004	/* disallow use of ufs_setsecattr */

/*
 * action to take when internal inconsistency is detected
 */
#define	UFSMNT_ONERROR_REPAIR	0x00000010	/* fsck */
#define	UFSMNT_ONERROR_PANIC	0x00000020	/* forced system shutdown */
#define	UFSMNT_ONERROR_LOCK	0x00000040	/* error lock the fs */
#define	UFSMNT_ONERROR_UMOUNT	0x00000080	/* forced umount of the fs */
#define	UFSMNT_ONERROR_RDONLY	0x00000100	/* readdonly (unimplimented) */
#define	UFSMNT_ONERROR_FLGMASK	0x000001F0

#define	UFSMNT_ONERROR_REPAIR_STR	"repair"
#define	UFSMNT_ONERROR_PANIC_STR	"panic"
#define	UFSMNT_ONERROR_LOCK_STR		"lock"
#define	UFSMNT_ONERROR_RDONLY_STR	"readonly"	/* unimplimented */
#define	UFSMNT_ONERROR_UMOUNT_STR	"umount"

/* default action is to repair fs */
#define	UFSMNT_ONERROR_DEFAULT		UFSMNT_ONERROR_PANIC

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_UFS_MOUNT_H */
