/* @(#)rac_private.h	1.5 91/03/11 SMI */

/*
 * rac_private.h, Copyright (C) 1990, Sun Microsystems, Inc.
 */

/*
 *	These defines, and the following structure represent RAC implementation
 *	details and may not be relied upon in the future.
 */
#define	CLRAC_DROP	999	/* drop previous call and destroy RAC handle */
#define	CLRAC_POLL	998	/* check status of asynchronous call */
#define	CLRAC_RECV	997	/* receive results of a previous rac_send() */
#define	CLRAC_SEND	996	/* initiate asynchronous call and return */

struct rac_send_req {
	unsigned long	proc;
	xdrproc_t	xargs;
	void		*argsp;
	xdrproc_t	xresults;
	void		*resultsp;
	struct timeval	timeout;
};


#ifndef MIN
#define	MIN(a, b)	(((a) < (b)) ? (a) : (b))
#endif
