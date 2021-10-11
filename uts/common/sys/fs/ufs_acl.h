/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_FS_UFS_ACL_H
#define	_SYS_FS_UFS_ACL_H

#pragma ident	"@(#)ufs_acl.h	1.4	94/02/07 SMI"

#include <sys/types.h>
#include <sys/cred.h>
#include <sys/vfs.h>
#include <sys/acl.h>
#include <sys/fs/ufs_fs.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct ufs_acl {
	union {
		struct ufs_acl	*acl_next;	/* Next ACL for this inode */
		u_short		acl_tag;	/* Entry type */
	} acl_un;
	o_mode_t	acl_perm;		/* Permission bits */
	uid_t		acl_who;		/* User or group ID */
} ufs_acl_t;

#define	acl_tag acl_un.acl_tag
#define	acl_next acl_un.acl_next

/*
 * In-core ACL mask
 */
typedef struct ufs_aclmask {
	short		acl_ismask;	/* Is mask defined? */
	o_mode_t	acl_maskbits;	/* Permission mask */
} ufs_aclmask_t;

/*
 * full acl
 */
typedef struct ic_acl {
	ufs_acl_t	*owner;		/* owner object */
	ufs_acl_t	*group;		/* group object */
	ufs_acl_t	*other;		/* other object */
	ufs_acl_t	*users;		/* list of users */
	ufs_acl_t	*groups;	/* list of groups */
	ufs_aclmask_t	class;		/* mask */
} ic_acl_t;

/*
 * In-core shadow inode
 */
typedef	struct si {
	struct si *s_next;		/* signature hash next */
	struct si *s_forw;		/* inode hash next */
	struct si *s_fore;		/* unref'd list next */

	int	s_flags;		/* see below */
	ino_t	s_shadow;		/* shadow inode number */
	dev_t	s_dev;			/* device (major,minor) */
	long	s_signature;		/* signature for all ACLs */
	long 	s_use;			/* on disk use count */
	long	s_ref;			/* in core reference count */
	krwlock_t s_lock;		/* lock for this structure */

	ic_acl_t  s_a;			/* acls */
	ic_acl_t  s_d;			/* def acls */
} si_t;

#define	aowner	s_a.owner
#define	agroup	s_a.group
#define	aother	s_a.other
#define	ausers	s_a.users
#define	agroups	s_a.groups
#define	aclass	s_a.class

#define	downer	s_d.owner
#define	dgroup	s_d.group
#define	dother	s_d.other
#define	dusers	s_d.users
#define	dgroups	s_d.groups
#define	dclass	s_d.class
#define	s_prev	s_forw

/*
 * s_flags
 */
#define	SI_CACHED 0x0001		/* Is in si_cache */

/*
 * Header to identify data on disk
 */
typedef struct ufs_fsd {
	int	fsd_type;		/* type of data */
	int	fsd_size;		/* size in bytes of ufs_fsd and data */
	char	fsd_data[1];		/* data */
} ufs_fsd_t;

/*
 * Data types  (fsd_type)
 */
#define	FSD_FREE	(0)		/* Free entry */
#define	FSD_ACL		(1)		/* Access Control Lists */
#define	FSD_DFACL	(2)		/* reserved for future use */
#define	FSD_RESERVED3	(3)		/* reserved for future use */
#define	FSD_RESERVED4	(4)		/* reserved for future use */
#define	FSD_RESERVED5	(5)		/* reserved for future use */
#define	FSD_RESERVED6	(6)		/* reserved for future use */
#define	FSD_RESERVED7	(7)		/* reserved for future use */

/*
 * flags for acl_validate
 */
#define	ACL_CHECK	0x01
#define	DEF_ACL_CHECK	0x02

#define	MODE_CHECK(M, PERM) ((((M) & PERM) == (M)) ? 0 : EACCES)

#define	MODE2ACL(P, MODE, CRED)					\
	ASSERT((P));						\
	(P)->acl_next = NULL;					\
	(P)->acl_perm &= ((MODE) & 7);				\
	(P)->acl_who = (CRED);

#define	ACL_MOVE(P, T, B)					\
{								\
	ufs_acl_t *acl;						\
	for (acl = (P); acl; acl = acl->acl_next) {		\
		(B)->acl_tag = (T);				\
		(B)->acl_perm = acl->acl_perm;			\
		(B)->acl_who = acl->acl_who;			\
		(B)++;						\
	}							\
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_UFS_ACL_H */
