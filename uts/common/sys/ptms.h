/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_PTMS_H
#define	_SYS_PTMS_H

#pragma ident	"@(#)ptms.h	1.12	93/05/28 SMI"	/* SVr4.0 11.7 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * =========================================================================
 * =   WARNING!!!! This source is not supported in future source releases. =
 * =========================================================================
 */

/*
 * Structures and definitions supporting the pseudo terminal
 * drivers.
 */
struct pt_ttys {
	unsigned pt_state;	/* state of master/slave pair */
	queue_t *ptm_rdq; 	/* master's read queue pointer */
	queue_t *pts_rdq; 	/* slave's read queue pointer */
	mblk_t	*pt_bufp;	/* ptr. to zero byte msg. blk. */
	pid_t	tty;		/* controlling tty pid */
	int	pt_access;	/* counter to control access to this struct */
	kcondvar_t pt_cv;	/* condition variable for exclusive access */
};

/*
 * pt_state values
 */
#define	PTLOCK	01	/* master/slave pair is locked */
#define	PTMOPEN 02  	/* master side is open */
#define	PTSOPEN 04	/* slave side is open */

#ifdef _KERNEL

/*
 * Multi-threading primitives.
 * Values of pt_access: -1 if a writer is accessing the struct
 *			0  if no one is reading or writing
 *			> 0 equals to the number of readers accessing the struct
 */
#define	PT_ENTER_READ(p) {			\
	mutex_enter(&pt_lock);			\
	while ((p)->pt_access < 0)		\
		cv_wait(&((p)->pt_cv), &pt_lock);	\
	(p)->pt_access++;			\
	mutex_exit(&pt_lock);			\
}

#define	PT_ENTER_WRITE(p) {			\
	mutex_enter(&pt_lock);			\
	while ((p)->pt_access != 0)		\
		cv_wait(&((p)->pt_cv), &pt_lock);	\
	(p)->pt_access = -1;			\
	mutex_exit(&pt_lock);			\
}

#define	PT_EXIT_READ(p) {			\
	mutex_enter(&pt_lock);			\
	if ((--((p)->pt_access)) == 0)		\
		cv_broadcast(&((p)->pt_cv));	\
	mutex_exit(&pt_lock);			\
}

#define	PT_EXIT_WRITE(p) {			\
	mutex_enter(&pt_lock);			\
	(p)->pt_access = 0;			\
	cv_broadcast(&((p)->pt_cv));		\
	mutex_exit(&pt_lock);			\
}

/*
 * pt_lock, ptms_tty[] and pt_cnt are defined in ptms_conf.c
 */
extern struct pt_ttys	*ptms_tty;
extern int		pt_cnt;
extern kmutex_t		pt_lock;

extern void ptms_initspace(void);

#endif /* _KERNEL */

/*
 * ioctl commands
 */
#define	ISPTM	(('P'<<8)|1)	/* query for master */
#define	UNLKPT	(('P'<<8)|2)	/* unlock master/slave pair */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PTMS_H */
