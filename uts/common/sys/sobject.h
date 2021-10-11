/*
 * Copyright (c) Sun Microsystems Inc. 1991-1993
 */

#ifndef	_SYS_SOBJECT_H
#define	_SYS_SOBJECT_H

#pragma ident	"@(#)sobject.h	1.6	94/11/08 SMI"

#include <sys/types.h>
#include <sys/turnstile.h>
#include <sys/thread.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Type-number definitions for the various synchronization
 * objects defined for the system. The numeric values
 * assigned to the various definitions are explicit, since
 * the synch-object mapping array depends on these values.
 */

typedef enum syncobj {
	SOBJ_NONE	= 0,	/* undefined synchonization object */
	SOBJ_MUTEX	= 1,	/* mutex synchonization object */
	SOBJ_READER	= 2,	/* readers' lock synchonization object */
	SOBJ_WRITER	= 3,	/* writer lock synchonization object */
	SOBJ_CV		= 4,	/* cond. variable synchonization object */
	SOBJ_SEMA	= 5,	/* semaphore synchonization object */
	SOBJ_USER	= 6,	/* user-level synchronization object */
	SOBJ_SHUTTLE 	= 7	/* shuttle synchronization object */
} syncobj_t;

/*
 * The following data structure is used to map
 * synchronization object type numbers to the
 * synchronization object's sleep queue number
 * or the synch. object's owner function.
 */

typedef struct _sobj_ops {
	char		*sobj_class;
	syncobj_t	sobj_type;
	qobj_t		sobj_qnum;
	kthread_t *	(*sobj_owner)();
	void		(*sobj_unsleep)(kthread_t *);
	void		(*sobj_changepri)(kthread_t *, pri_t);
} sobj_ops_t;

#ifdef	_KERNEL

#define	SOBJ_TYPE(sobj_ops)		sobj_ops->sobj_type
#define	SOBJ_QNUM(sobj_ops)		sobj_ops->sobj_qnum
#define	SOBJ_OWNER(sobj_ops, sobj)	(*(sobj_ops->sobj_owner))(sobj)
#define	SOBJ_UNSLEEP(sobj_ops, t)	(*(sobj_ops->sobj_unsleep))(t)
#define	SOBJ_CHANGEPRI(sobj_ops, t, pri)	\
				(*(sobj_ops->sobj_changepri))(t, pri)

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SOBJECT_H */
