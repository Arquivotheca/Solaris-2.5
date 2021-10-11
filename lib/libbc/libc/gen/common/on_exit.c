#pragma ident	"@(#)on_exit.c	1.2	92/07/20 SMI" 

#include <sys/types.h>

/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

char	*malloc();

struct handlers {
	void	(*handler)();
	caddr_t	arg;
	struct	handlers *next;
};

extern	struct handlers *_exit_handlers;

int
on_exit(handler, arg)
	void (*handler)();
	caddr_t arg;
{
	register struct handlers *h =
	    (struct handlers *)malloc(sizeof (*h));
	
	if (h == 0)
		return (-1);
	h->handler = handler;
	h->arg = arg;
	h->next = _exit_handlers;
	_exit_handlers = h;
	return (0);
}
