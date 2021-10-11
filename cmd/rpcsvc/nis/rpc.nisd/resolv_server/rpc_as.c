/* Copyright (c) 1993 Sun Microsystems Inc */

/* Taken from 4.1.3 ypserv resolver code. */

#include "rpc_as.h"
#include <sys/types.h>
#include <signal.h>
#ifndef TDRPC
#include <unistd.h>
#endif

#define	CHECK_PARENT_SECS	600 /* every 10 min */

extern pid_t ppid;
extern void cleanup();

static rpc_as  **rpc_as_handles;
static	fd_set	rpc_as_fdset;

fd_set rpc_as_get_fdset()
{
	return (rpc_as_fdset);
}

static bool_t rpc_as_init()
{


	if (rpc_as_handles == NULL) {
		rpc_as_handles = (rpc_as **)
			mem_alloc(FD_SETSIZE * sizeof (rpc_as *));
	if (rpc_as_handles == NULL) {
		return (FALSE);
		}
#ifdef TDRPC
	bzero((char *)rpc_as_handles, FD_SETSIZE *sizeof (rpc_as *));
#else
	memset((char *)rpc_as_handles, 0, FD_SETSIZE *sizeof (rpc_as *));
#endif
	}
	return (TRUE);
}

rpc_as *rpc_as_fd_to_as(fd)
{
	if ((rpc_as_handles == NULL) && (!rpc_as_init())) return (NULL);
	if (fd < FD_SETSIZE) {
	return (rpc_as_handles[fd]);
	} else return (NULL);


}




/*
 * Activate a asynchronous handle.
 */
rpc_as_register(xprt)
	rpc_as *xprt;
{

	if ((rpc_as_handles == NULL) && (!rpc_as_init())) return (-1);
	if (xprt->as_fd < 0) return (-1); /* cant register less than zero */
	if (xprt->as_fd < FD_SETSIZE) {
		rpc_as_handles[xprt->as_fd] = xprt;
		FD_SET(xprt->as_fd, &rpc_as_fdset);
		return (0);
	} else return (-1);
}

/*
 * De-activate an asynchronous handle.
 */
rpc_as_unregister(xprt)
	rpc_as *xprt;
{

	if ((rpc_as_handles == NULL) && (!rpc_as_init())) return (-1);
	if (xprt->as_fd < 0) return (-1); /* cant unregister less than zero */

	if ((xprt->as_fd < FD_SETSIZE) &&
			(rpc_as_handles[xprt->as_fd] == xprt)) {
		rpc_as_handles[xprt->as_fd] = (rpc_as *)0;
		FD_CLR(xprt->as_fd, &rpc_as_fdset);
		return (0);
	}
	return (-1);
}
void
rpc_as_rcvreqset(readfds)
	fd_set *readfds;
{
	int setsize;
	u_long *maskp;
	u_long mask;
	int bit;
	int sock;
	int s2;
	rpc_as *xprt;

	if ((rpc_as_handles == NULL) && (!rpc_as_init())) return;

	setsize = FD_SETSIZE;
	maskp = (u_long *)readfds->fds_bits;
	for (sock = 0; sock < setsize; sock += NFDBITS) {
		for (mask = *maskp++; bit = ffs((int)mask);
					mask ^= (1 << (bit - 1))) {
			/* sock has input waiting */
			s2 = sock + bit -1;
			xprt = rpc_as_handles[s2];
			if (xprt){
				if (xprt->as_recv) xprt->as_recv(xprt, s2);
				else rpc_as_unregister(xprt); /* too bad */
				FD_CLR(s2, readfds);
			}
		}
	}
}



struct timeval rpc_as_get_timeout()
{
	int		tsecs;
	int		tusecs;
	struct timeval  now;
	struct timeval	ans;
	static struct timeval last;
	struct rpc_as   **x;

	u_long *maskp;
	u_long mask;
	int bit;
	int sock;
	int i;

	ans.tv_sec = CHECK_PARENT_SECS; /* check parent time */
	ans.tv_usec = 0;

	if ((rpc_as_handles == NULL) && (!rpc_as_init())) return (ans);
	x = rpc_as_handles;

	/* Calculate elapsed time */
	(void) gettimeofday(&now, (struct timezone *) 0);
	if (last.tv_sec){
		tsecs = now.tv_sec - last.tv_sec;
		tusecs = now.tv_usec - last.tv_usec;
		last = now;
	} else {
		last = now;
		tsecs = 0;
		tusecs = 0;
	}
	while (tusecs < 0)  {
	tusecs += 1000000;
	tsecs--;
	}
	if (tsecs < 0) tsecs = 0;

	maskp = (u_long *)rpc_as_fdset.fds_bits;
	for (sock = 0; sock < FD_SETSIZE; sock += NFDBITS)
	    for (mask = *maskp++; bit = ffs((int)mask);
					mask ^= (1 << (bit - 1))) {
		/* sock has input waiting */
		i = sock + bit -1;
		if (x[i] != (struct rpc_as *) 0) {
			if (x[i]->as_timeout_flag){

			x[i]->as_timeout_remain.tv_sec -= tsecs;
			x[i]->as_timeout_remain.tv_usec -= tusecs;

			while (x[i]->as_timeout_remain.tv_usec < 0){
				x[i]->as_timeout_remain.tv_sec--;
				x[i]->as_timeout_remain.tv_usec += 1000000;
				}
			if (x[i]->as_timeout_remain.tv_sec < 0){
				x[i]->as_timeout_remain.tv_sec = 0;
				x[i]->as_timeout_remain.tv_usec = 0;
				}

			if (timercmp(&(x[i]->as_timeout_remain), &ans,
							< /*EMPTY*/))
				ans = x[i]->as_timeout_remain;
			}
		}
	}

	return (ans);
}
void rpc_as_timeout(twaited)
struct timeval twaited;
{

	struct rpc_as   **x;
	int		i;
	u_long *maskp;
	u_long mask;
	int bit;
	int sock;

	/* ppid only set when using transient from parent (nisd) */
	if (ppid && kill(ppid, 0))
		cleanup(0);

	if ((rpc_as_handles == NULL) && (!rpc_as_init())) return;
	x = rpc_as_handles;

	maskp = (u_long *)rpc_as_fdset.fds_bits;
	for (sock = 0; sock < FD_SETSIZE; sock += NFDBITS)
	    for (mask = *maskp++; bit = ffs((int)mask);
					mask ^= (1 << (bit - 1))) {
		/* sock has input waiting */
		i = sock + bit -1;
		if (x[i] != (struct rpc_as *) 0) {
			if (x[i]->as_timeout_flag){

			if ((timercmp(&(x[i]->as_timeout_remain), &twaited,
							< /*EMPTY*/)) ||
		(timercmp(&(x[i]->as_timeout_remain), &twaited,
							== /*EMPTY*/))) {
				if (x[i]->as_timeout) x[i]->as_timeout(x[i]);
				else rpc_as_unregister(x[i]); /* punt */

			}
		}
		}
		}
}
