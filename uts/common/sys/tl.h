/*
 * Copyright (c) 1993 by Sun Microsystems Inc.
 */

#ifndef _SYS_TL_H
#define	_SYS_TL_H

#pragma ident	"@(#)tl.h	1.2	94/05/10 SMI"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These are Sun private declarations. Not to be used by any
 * external applications/code.
 */

/*
 * Protocol level for option header - (hex for ascii "TL")
 * (Hopefully unique!)
 */
#define	TL_PROT_LEVEL 0x544c

/*
 * Option and data structures used for sending credentials
 */
#define	TL_OPT_PEER_CRED 10
typedef struct tl_credopt {
	uid_t	tc_uid;		/* Effective user id */
	gid_t	tc_gid;		/* Effective group id */
	uid_t	tc_ruid;	/* Real user id */
	gid_t	tc_rgid;	/* Real group id */
	uid_t	tc_suid;	/* Saved user id (from exec) */
	gid_t	tc_sgid;	/* Saved group id (from exec) */
	ulong_t	tc_ngroups;	/* number of supplementary groups */
} tl_credopt_t;


/*
 * Ioctl's for the 'tl' driver
 */
#define	TL_IOC		(('T' << 16)|('L' << 8))
#define	TL_IOC_CREDOPT	(TL_IOC|001)


#ifdef _KERNEL
/*
 * M_CTL types
 * Sun private for support of linking the streams for connectionless
 * mode of driver. Used by sockmod
 */
#define	TL_CL_LINK	1
#define	TL_CL_UNLINK	2

/*
 * Socket link M_CTL structure
 */
struct tl_sictl {
	long type;
	long ADDR_offset;
	long ADDR_len;
};
#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TL_H */
