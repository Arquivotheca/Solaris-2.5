/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mqueue.c	1.5	93/04/13 SMI"

#include "synonyms.h"
#include <mqueue.h>
#include <errno.h>


mqd_t
mq_open(const char *name, int oflag, ...)
{
	errno = ENOSYS;
	return (mqd_t)(-1);
}

int
mq_close(mqd_t mqdes)
{
	errno = ENOSYS;
	return (-1);
}

int
mq_unlink(const char *name)
{
	errno = ENOSYS;
	return (-1);
}

int
mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio)
{
	errno = ENOSYS;
	return (-1);
}

ssize_t
mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio)
{
	errno = ENOSYS;
	return (ssize_t)(-1);
}

int
mq_notify(mqd_t mqdes, const struct sigevent *notification)
{
	errno = ENOSYS;
	return (-1);
}

int
mq_setattr(mqd_t mqdes, const struct mq_attr *mqstat, struct mq_attr *omqstat)
{
	errno = ENOSYS;
	return (-1);
}

int
mq_getattr(mqd_t mqdes, struct mq_attr *mqstat)
{
	errno = ENOSYS;
	return (-1);
}
