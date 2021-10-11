/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SEMAPHORE_H
#define	_SEMAPHORE_H

#pragma ident	"@(#)semaphore.h	1.7	93/12/20 SMI"

#include <sys/types.h>
#include <sys/fcntl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_UINT32_T
typedef unsigned long	uint32_t;
#define	_UINT32_T
#endif
#ifndef	_UINT64_T
typedef u_longlong_t	uint64_t;
#define	_UINT64_T
#endif

typedef struct {
	/* this structure must be the same as sema_t in <synch.h> */
	uint32_t	sem_count;	/* semaphore count */
	uint32_t	sem_type;
	uint64_t	sem_pad1[3];	/* reserved for a mutex_t */
	uint64_t 	sem_pad2[2];	/* reserved for a cond_t */
}	sem_t;

/*
 * function prototypes
 */
#if	defined(__STDC__)
int	sem_init(sem_t *sem, int pshared, unsigned int value);
int	sem_destroy(sem_t *sem);
sem_t	*sem_open(const char *name, int oflag, ...);
int	sem_close(sem_t *sem);
int	sem_unlink(const char *name);
int	sem_wait(sem_t *sem);
int	sem_trywait(sem_t *sem);
int	sem_post(sem_t *sem);
int	sem_getvalue(sem_t *sem, int *sval);
#else
int	sem_init();
int	sem_destroy();
sem_t	*sem_open();
int	sem_close();
int	sem_unlink();
int	sem_wait();
int	sem_trywait();
int	sem_post();
int	sem_getvalue();
#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SEMAPHORE_H */
