#ifndef HANDLES_H
#define	HANDLES_H

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*	"@(#)handles.h 1.2 91/12/20 */

#include <stdio.h>

struct file_handle {
	struct file_handle *nxt;
	int	hid;
	char *host;
};

#define	NULL_HANDLE (struct file_handle *)0

#ifdef __STDC__
extern struct file_handle *new_handle(int, char *);
extern struct file_handle *handle_lookup(int);
extern void free_handle(struct file_handle *);
#else
extern struct file_handle *new_handle();
extern struct file_handle *handle_lookup();
extern void free_handle();
#endif
#endif	/* HANDLES_H */
