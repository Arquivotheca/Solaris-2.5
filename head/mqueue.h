/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _MQUEUE_H
#define	_MQUEUE_H

#pragma ident	"@(#)mqueue.h	1.7	94/01/07 SMI"

#include <sys/feature_tests.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/time.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef void	*mqd_t;		/* opaque message queue descriptor */

struct mq_attr {
	long	mq_flags;	/* message queue flags */
	long	mq_maxmsg;	/* maximum number of messages */
	long	mq_msgsize;	/* maximum message size */
	long	mq_curmsgs;	/* number of messages currently queued */
	int	mq_pad[12];
};

/*
 * function prototypes
 */
#if	defined(__STDC__)
#if	(_POSIX_C_SOURCE - 0 > 0) && (_POSIX_C_SOURCE - 0 <= 2)
#error	"POSIX Message Passing is not supported in POSIX.1-1990"
#endif
#include <sys/siginfo.h>
mqd_t	mq_open(const char *name, int oflag, ...);
int	mq_close(mqd_t mqdes);
int	mq_unlink(const char *name);
int	mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
		unsigned int msg_prio);
ssize_t	mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
		unsigned int *msg_prio);
int	mq_notify(mqd_t mqdes, const struct sigevent *notification);
int	mq_getattr(mqd_t mqdes, struct mq_attr *mqstat);
int	mq_setattr(mqd_t mqdes, const struct mq_attr *mqstat,
		struct mq_attr *omqstat);
#else
mqd_t	mq_open();
int	mq_close();
int	mq_unlink();
int	mq_send();
ssize_t	mq_receive();
int	mq_notify();
int	mq_getattr();
int	mq_setattr();
#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _MQUEUE_H */
