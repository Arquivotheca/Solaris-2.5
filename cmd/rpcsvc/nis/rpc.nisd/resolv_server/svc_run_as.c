/* Copyright (c) 1993 Sun Microsystems Inc */

/* Taken from 4.1.3 ypserv resolver code. */

/*
 * This is an example of using rpc_as.h an asynchronous polling
 * mechanism,  asynchronously polled fds are combined with the
 * service fds.  The minimum timeout is calculated, and
 * the user waits for that timeout or activity on either the
 * async fdset or the svc fdset.
 */

#include <rpc/rpc.h>
#include <sys/errno.h>
#include "rpc_as.h"

extern int dtbsize;

void
svc_run_as()
{
	fd_set		readfds;
	extern int	errno;
	struct timeval  timeout;
	int		i;
	int		selsize;

	selsize = dtbsize;
	if (selsize > FD_SETSIZE)
		selsize = FD_SETSIZE;

	for (;;) {
		readfds = rpc_as_get_fdset();

		for (i = 0; i < howmany(FD_SETSIZE, NFDBITS); i++)
			readfds.fds_bits[i] |= svc_fdset.fds_bits[i];

		timeout = rpc_as_get_timeout();
		switch (select(selsize, &readfds, (fd_set *) 0, (fd_set *) 0,
								&timeout)) {
		case -1:
			if (errno == EINTR) {
				continue;
			}
			perror("svc_run: - select failed: ");
			return;
		case 0:
			rpc_as_timeout(timeout);
		default:
			rpc_as_rcvreqset(&readfds);
			svc_getreqset(&readfds);
		}
	}
}
