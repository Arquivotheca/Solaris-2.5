/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */
#include <sys/select.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <sys/time.h>
#include <sys/poll.h>

#define	MASKVAL	(POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)

/*
 *	Given an fd_set pointer and the number of bits to check in it,
 *	initialize the supplied pollfd array for RPC's use (RPC only
 *	polls for input events).  We return the number of pollfd slots
 *	we initialized.
 */
int
__rpc_select_to_poll(fdmax, fdset, p0)
	int	fdmax;		/* number of bits we must test */
	fd_set	*fdset;		/* source fd_set array */
	struct pollfd	*p0;	/* target pollfd array */
{
	/* register declarations ordered by expected frequency of use */
	register long *in;
	register int j;		/* loop counter */
	register u_long b;	/* bits to test */
	register int n;
	register struct pollfd	*p = p0;

	/*
	 * For each fd, if the appropriate bit is set convert it into
	 * the appropriate pollfd struct.
	 */
	trace2(TR___rpc_select_to_poll, 0, fdmax);
	for (in = fdset->fds_bits, n = 0; n < fdmax; n += NFDBITS, in++)
		for (b = (u_long) *in, j = 0; b; j++, b >>= 1)
			if (b & 1) {
				p->fd = n + j;
				if (p->fd >= fdmax) {
					trace2(TR___rpc_select_to_poll,
							1, fdmax);
					return (p - p0);
				}
				p->events = MASKVAL;
				p++;
			}

	trace2(TR___rpc_select_to_poll, 1, fdmax);
	return (p - p0);
}

/*
 *	Convert from timevals (used by select) to milliseconds (used by poll).
 */
int
__rpc_timeval_to_msec(t)
	register struct timeval	*t;
{
	int	t1, tmp;

	/*
	 *	We're really returning t->tv_sec * 1000 + (t->tv_usec / 1000)
	 *	but try to do so efficiently.  Note:  1000 = 1024 - 16 - 8.
	 */
	trace1(TR___rpc_timeval_to_msec, 0);
	tmp = t->tv_sec << 3;
	t1 = -tmp;
	t1 += t1 << 1;
	t1 += tmp << 7;
	if (t->tv_usec)
		t1 += t->tv_usec / 1000;

	trace1(TR___rpc_timeval_to_msec, 1);
	return (t1);
}
