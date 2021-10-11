#ident	"@(#)iflist.h	1.1	95/02/25 SMI"

/* Copyright (c) 1995 by Sun Microsystems, Inc. */

#ifndef _IFLIST_H
#define	_IFLIST_H

struct iflist {
	char *name;
	struct iflist *next;
};

extern struct iflist	*iflist;

void add_interface(char *);
void register_interfaces(void);

#endif	/* _IFLIST_H */
