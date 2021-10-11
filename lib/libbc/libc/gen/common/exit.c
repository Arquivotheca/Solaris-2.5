#pragma ident	"@(#)exit.c	1.2	92/07/20 SMI" 

#include <sys/types.h>

/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

struct handlers {
	void	(*handler)();
	caddr_t	arg;
	struct	handlers *next;
};

extern	void _cleanup();

/* the list of handlers and their arguments */
struct	handlers *_exit_handlers;

/*
 * exit -- do termination processing, then evaporate process
 */
void
exit(code)
	int code;
{
	register struct handlers *h;

	while (h = _exit_handlers) {
		_exit_handlers = h->next;
		(*h->handler)(code, h->arg);
	}
	_cleanup();
	_exit(code);
}
