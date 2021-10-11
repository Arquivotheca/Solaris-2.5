/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_FILE_H
#define	_SYS_FILE_H

#pragma ident	"@(#)file.h	1.41	95/08/30 SMI"	/* SVr4.0 11.28 */

#include <sys/t_lock.h>
#ifdef _KERNEL
#include <sys/user.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * fio locking:
 *   f_rwlock	protects f_vnode and f_cred
 *   f_tlock	protects the rest
 *
 *   The purpose of locking in this layer is to keep the kernel
 *   from panicing if, for example, a thread calls close() while
 *   another thread is doing a read().  It is up to higher levels
 *   to make sure 2 threads doing I/O to the same file don't
 *   screw each other up.
 */
/*
 * One file structure is allocated for each open/creat/pipe call.
 * Main use is to hold the read/write pointer associated with
 * each open file.
 */
typedef struct file {
	kmutex_t	f_tlock;	/* short term lock */
	ushort_t	f_flag;
	ushort_t	f_pad;		/* Explicit pad to 4 byte boundary */
	struct vnode	*f_vnode;	/* pointer to vnode structure */
	offset_t	f_offset;	/* read/write character pointer */
	struct cred	*f_cred;	/* credentials of user who opened it */
	caddr_t		f_audit_data;	/* file audit data */
	int		f_count;	/* reference count */
} file_t;

/* flags */

#define	FOPEN		0xFFFFFFFF
#define	FREAD		0x01
#define	FWRITE		0x02
#define	FNDELAY		0x04
#define	FAPPEND		0x08
#define	FSYNC		0x10	/* file (data+inode) integrity while writing */
#ifdef C2_AUDIT
#define	FREVOKED	0x20	/* C2 Security - Revoke Subsystem */
#endif
#define	FDSYNC		0x40	/* file data only integrity while writing */
#define	FRSYNC		0x8000	/* sync flag for read operations */
				/* combined with FSYNC or FDSYNC, it has */
				/* effect on read same as write		*/
				/* Should be within first 8 bits but */
				/* no space, so we change FMASK also */
#define	FNONBLOCK	0x80

#define	FMASK		0x80FF	/* should be disjoint from FASYNC */
				/* should include FRSYNC also */

/* open-only modes */

#define	FCREAT		0x0100
#define	FTRUNC		0x0200
#define	FEXCL		0x0400
#define	FNOCTTY		0x0800
#define	FASYNC		0x1000	/* asyncio is in progress */
#define	FOFFSET		0x2000

/* fsync pseudo flag */

#define	FNODSYNC	0x10000

/* file descriptor flags */
#define	FCLOSEXEC	001	/* close on exec */
#define	FRESERVED	002	/* slot is reserved */
#define	FCLOSING	004	/* file descriptor is being closed */

#ifdef _KERNEL

#ifdef C2_AUDIT
extern int audit_getf(int);
#define	GETF(fd) ((curproc->p_lwptotal == 1)? \
			((fd) >= 0 && (fd) < PTOU(curproc)->u_nofiles) ? \
				(audit_active)?(audit_getf((fd)), \
					PTOU(curproc)->u_flist[(fd)].uf_ofile)\
				: \
					PTOU(curproc)->u_flist[(fd)].uf_ofile\
			: \
				(struct file *) 0 \
		    : \
			getf((fd)) \
)
#else
#define	GETF(fd) ((curproc->p_lwptotal == 1)? \
			((fd) >= 0 && (fd) < PTOU(curproc)->u_nofiles) ? \
				PTOU(curproc)->u_flist[(fd)].uf_ofile \
			: \
				(struct file *) 0 \
		    : \
			getf((fd)) \
)
#endif

#define	RELEASEF(fd) { if (curproc->p_lwptotal > 1) releasef(fd); }

/*
 * Fake flag => internal ioctl call for layered drivers.
 * Note that this flag deliberately *won't* fit into
 * the f_flag field of a file_t.
 *
 * Part of the Solaris 2.x DDI/DKI.
 */
#define	FKIOCTL		0x80000000	/* ioctl addresses are from kernel */

/*
 * Fake flag => this time to specify that the open(9E)
 * comes from another part of the kernel, not userland.
 *
 * Part of the Solaris 2.x DDI/DKI.
 */
#define	FKLYR		0x40000000	/* layered driver call */

#endif	/* _KERNEL */

/* miscellaneous defines */

#define	NULLFP ((struct file *)0)

#ifndef L_SET
#define	L_SET	0	/* for lseek */
#endif /* L_SET */

#if defined(_KERNEL)

/*
 * Routines dealing with user per-open file flags and
 * user open files.
 */
struct proc;	/* forward reference for function prototype */

extern file_t *getf(int);
extern file_t *getandset(int);
extern void releasef(int);
extern void areleasef(int, proc_t *);
#ifndef	_BOOT
extern void closeall(int);
#endif
extern int closef(file_t *);
extern int ufalloc(int, int *);
extern int falloc(struct vnode *, int, file_t **, int *);
extern void finit(void);
extern void unfalloc(file_t *);
extern void setf(int, file_t *);
extern char getpof(int);
extern void setpof(int, char);
extern int filesearch(struct vnode *);
extern int fassign(struct vnode **, int, int *);
extern void bump_fcnts(struct proc *);
extern void close_exec(struct proc *);


#endif	/* defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FILE_H */
