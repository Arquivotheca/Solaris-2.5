/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

#ifndef _SYS_SHM_H
#define	_SYS_SHM_H

#pragma ident	"@(#)shm.h	1.26	95/02/13 SMI"	/* SVr4.0 11.19 */

#include <sys/ipc.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 *	IPC Shared Memory Facility.
 */

/*
 *	Implementation Constants.
 */
#if (defined(_KERNEL) || defined(_KMEMUSER))
#define	SHMLBA  PAGESIZE	/* segment low boundary address multiple */
				/* (SHMLBA must be a power of 2) */
#else
#include <sys/unistd.h>		/* needed for _SC_PAGESIZE */
extern long _sysconf(int);	/* System Private interface to sysconf() */
#define	SHMLBA	(_sysconf(_SC_PAGESIZE))
#endif	/* defined(_KERNEL) || defined(_KMEMUSER)) */

/*
 *	Permission Definitions.
 */
#define	SHM_R	0400	/* read permission */
#define	SHM_W	0200	/* write permission */

/*
 *	Message Operation Flags.
 */
#define	SHM_RDONLY	010000	/* attach read-only (else read-write) */
#define	SHM_RND		020000	/* round attach address to SHMLBA */
#define	SHM_SHARE_MMU	040000  /* share VM resources such as page table */

/*
 *	Structure Definitions.
 */

/*
 *	There is a shared mem id data structure (shmid_ds) for each
 *	segment in the system.
 */
#if defined(_KERNEL) || defined(_KMEMUSER)

#include <sys/t_lock.h>

struct shmid_ds {
	struct ipc_perm shm_perm;	/* operation permission struct */
	int		shm_segsz;	/* size of segment in bytes */
	struct anon_map	*shm_amp;	/* segment anon_map pointer */
	ushort		shm_lkcnt;	/* number of times it is being locked */
	pid_t		shm_lpid;	/* pid of last shmop */
	pid_t		shm_cpid;	/* pid of creator */
	ulong		shm_nattch;	/* used only for shminfo */
	ulong		shm_cnattch;	/* used only for shminfo */
	time_t		shm_atime;	/* last shmat time */
	long		shm_pad1;	/* reserved for time_t expansion */
	time_t		shm_dtime;	/* last shmdt time */
	long		shm_pad2;	/* reserved for time_t expansion */
	time_t		shm_ctime;	/* last change time */
	long		shm_pad3;	/* reserved for time_t expansion */
	kcondvar_t	shm_cv;
	char		shm_pad4[2];	/* reserved for kcondvar_t expansion */
	struct as	*shm_sptas;
	long		shm_pad5[2];	/* reserve area  */
};

/* SVR3 structure */
struct o_shmid_ds {
	struct o_ipc_perm shm_perm;	/* operation permission struct */
	int		shm_segsz;	/* size of segment in bytes */
	struct anon_map	*shm_amp;	/* segment anon_map pointer */
	ushort		shm_lkcnt;	/* number of times it is being locked */
	char 		shm_pad[2];
	o_pid_t		shm_lpid;	/* pid of last shmop */
	o_pid_t		shm_cpid;	/* pid of creator */
	ushort		shm_nattch;	/* used only for shminfo */
	ushort		shm_cnattch;	/* used only for shminfo */
	time_t		shm_atime;	/* last shmat time */
	time_t		shm_dtime;	/* last shmdt time */
	time_t		shm_ctime;	/* last change time */
};

#else	/* user definition */

typedef unsigned long shmatt_t;

/* this maps to the kernel struct shmid_ds */
struct shmid_ds {
	struct ipc_perm	shm_perm;	/* operation permission struct */
	int		shm_segsz;	/* size of segment in bytes */
#if defined(_XOPEN_SOURCE)
	void		*shm_amp;
#else
	struct anon_map	*shm_amp;	/* segment anon_map pointer */
#endif
	ushort_t	shm_lkcnt;	/* number of times it is being locked */
	pid_t		shm_lpid;	/* pid of last shmop */
	pid_t		shm_cpid;	/* pid of creator */
	shmatt_t	shm_nattch;	/* used only for shminfo */
	ulong_t		shm_cnattch;	/* used only for shminfo */
	time_t		shm_atime;	/* last shmat time */
	long		shm_pad1;	/* reserved for time_t expansion */
	time_t		shm_dtime;	/* last shmdt time */
	long		shm_pad2;	/* reserved for time_t expansion */
	time_t		shm_ctime;	/* last change time */
	long		shm_pad3;	/* reserved for time_t expansion */
	long		shm_pad4[4];	/* reserve area  */
};

#endif	/* end defined(_KERNEL) */

#if defined(__EXTENSIONS__) || \
	(!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))
struct	shminfo {
	int	shmmax,		/* max shared memory segment size */
		shmmin,		/* min shared memory segment size */
		shmmni,		/* # of shared memory identifiers */
		shmseg;		/* max attached shared memory	  */
				/* segments per process		  */
};
#endif /* defined(__EXTENSIONS__) || (!defined(_POSIX_C_SOURCE) && ... */

/*
 * Shared memory control operations
 */
#define	SHM_LOCK	3	/* Lock segment in core */
#define	SHM_UNLOCK	4	/* Unlock segment */

#if defined(_KERNEL)
void	shminit(void);
void	shmfork(struct proc *, struct proc *);
void	shmexit(struct proc *);
#else
#if defined(__STDC__)
int shmget(key_t, size_t, int);
int shmctl(int, int, struct shmid_ds *);
void *shmat(int, const void *, int);
#if defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4)
int shmdt(const void *);
#else
int shmdt(char *);
#endif /* defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4) */
#else /* __STDC __ */
int shmctl();
int shmget();
void *shmat();
int shmdt();
#endif /* __STDC __ */

#endif /* defined(_KERNEL) */


/*
 * Protected by p->p_lock
 */
#if defined(__EXTENSIONS__) || \
	(!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))
typedef struct segacct {
	struct segacct	*sa_next;
	caddr_t		 sa_addr;
	size_t		 sa_len;
	struct anon_map *sa_amp;
	struct as	*sa_as;
} segacct_t;
#endif /* defined(__EXTENSIONS__) || (!defined(_POSIX_C_SOURCE) && ... */

#ifdef _KERNEL
/*
 * Structures allocated in machdep.c
 */
extern struct shmid_ds	*shmem;		/* shared memory id pool */
extern struct shminfo	shminfo;	/* configuration parameters */
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SHM_H */
