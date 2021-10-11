#ident	"@(#)handles.c 1.4 91/12/20"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <database/handles.h>
#include <stdlib.h>
#include <string.h>

/*LINTLIBRARY*/

static struct file_handle *handles;

struct file_handle *
new_handle(hid, host)
	int hid;
	char *host;
{
	struct file_handle *h;

	h = (struct file_handle *)malloc(sizeof (struct file_handle));
	if (h == NULL_HANDLE) {
		return (h);
	}
	if ((h->host = malloc((unsigned)(strlen(host)+1))) == NULL) {
		free((char *)h);
		return (NULL_HANDLE);
	}
	h->hid = hid;
	(void) strcpy(h->host, host);
	h->nxt = handles;
	handles = h;
	return (h);
}

struct file_handle *
handle_lookup(hid)
	int hid;
{
	register struct file_handle *h;

	for (h = handles; h; h = h->nxt)
		if (h->hid == hid)
			return (h);
	return (NULL_HANDLE);
}

void
free_handle(h)
	struct file_handle *h;
{
	register struct file_handle *p;

	if (h == handles) {
		handles = h->nxt;
		free(h->host);
		free((char *)h);
	} else {
		for (p = handles; p; p = p->nxt) {
			if (p->nxt == h) {
				p->nxt = h->nxt;
				free(h->host);
				free((char *)h);
			}
		}
	}
}
