/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_SYNCH_H
#define	_SYS_SYNCH_H

#pragma ident	"@(#)synch.h	1.21	93/04/13 SMI"

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_UINT8_T
#define	_UINT8_T
typedef unsigned char	uint8_t;
#endif
#ifndef	_UINT32_T
#define	_UINT32_T
typedef unsigned long	uint32_t;
#endif
#ifndef	_UINT64_T
#define	_UINT64_T
typedef u_longlong_t	uint64_t;
#endif

/*
 * Thread and LWP mutexes have the same type
 * definitions.
 */
typedef struct _lwp_mutex {
	struct _mutex_flags {
		uint8_t		flag[4];
		uint32_t 	type;
	} flags;
	union _mutex_lock_un {
		struct _mutex_lock {
			uint8_t	pad[8];
		} lock64;
		uint64_t owner64;
	} lock;
	uint64_t data;
} lwp_mutex_t;

#define	mutex_lockw	lock.lock64.pad[7]

/*
 * Thread and LWP condition variables have the same
 * type definition.
 */
typedef struct _lwp_cond {
	struct _lwp_cond_flags {
		uint8_t		flag[4];
		uint32_t 	type;
	} flags;
	uint64_t data;
} lwp_cond_t;


/*
 * LWP semaphores
 */

typedef struct _lwp_sema {
	uint32_t	count;		/* semaphore count */
	uint32_t	type;
	uint8_t		flags[8];	/* last byte reserved for waiters */
	uint64_t	data;		/* optional data */
} lwp_sema_t;

/*
 * Definitions of synchronization types.
 */
#define	USYNC_THREAD	0		/* private to a process */
#define	USYNC_PROCESS	1		/* shared by processes */
#define	TRACE_TYPE	2

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SYNCH_H */
