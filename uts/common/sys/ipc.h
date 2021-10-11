/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_IPC_H
#define	_SYS_IPC_H

#pragma ident	"@(#)ipc.h	1.15	94/09/03 SMI"	/* SVr4.0 11.10	*/

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Common IPC Access Structure */

/*
 * The kernel supports both the SVR3 ipc_perm and expanded ipc_perm
 * structures simultaneously.
 */

/*
 * Applications that read /dev/mem must be built like the kernel. A new
 * symbol "_KMEMUSER" is defined for this purpose.
 */

#if defined(_KERNEL) || defined(_KMEMUSER)
/* SVR3 ipc_perm structure */
struct o_ipc_perm {
	o_uid_t	uid;	/* owner's user id */
	o_gid_t	gid;	/* owner's group id */
	o_uid_t	cuid;	/* creator's user id */
	o_gid_t	cgid;	/* creator's group id */
	o_mode_t mode;	/* access modes */
	ushort	seq;	/* slot usage sequence number */
	key_t	key;	/* key */
};
#endif	/* defined(_KERNEL) */

struct ipc_perm {
	uid_t	uid;	/* owner's user id */
	gid_t	gid;	/* owner's group id */
	uid_t	cuid;	/* creator's user id */
	gid_t	cgid;	/* creator's group id */
	mode_t	mode;	/* access modes */
	ulong_t	seq;	/* slot usage sequence number */
	key_t	key;	/* key */
	long	pad[4]; /* reserve area */
};

/* Common IPC Definitions. */
/* Mode bits. */
#define	IPC_ALLOC	0100000		/* entry currently allocated */
#define	IPC_CREAT	0001000		/* create entry if key doesn't exist */
#define	IPC_EXCL	0002000		/* fail if key exists */
#define	IPC_NOWAIT	0004000		/* error if request must wait */

/* Keys. */
#define	IPC_PRIVATE	(key_t)0	/* private key */

/* Control Commands. */

#define	IPC_RMID	10	/* remove identifier */
#define	IPC_SET		11	/* set options */
#define	IPC_STAT	12	/* get options */

#if defined(_KERNEL) || defined(_KMEMUSER)
	/* For compatibility */
#define	IPC_O_RMID	0	/* remove identifier */
#define	IPC_O_SET	1	/* set options */
#define	IPC_O_STAT	2	/* get options */
#endif	/* defined(_KERNEL) */

#if defined(__STDC__) && !defined(_KERNEL) && !defined(_XOPEN_SOURCE)
key_t ftok(const char *, int);
#endif

#if defined(_KERNEL)
int	ipcaccess(struct ipc_perm *, int, struct cred *);
int	ipcget(key_t, int, struct ipc_perm *, int, int, int *,
		struct ipc_perm **);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IPC_H */
