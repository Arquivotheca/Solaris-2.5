#pragma ident	"@(#)nlist.c	1.2	92/07/20 SMI" 

/*
 * Copyright (c) 1987 Sun Microsystems, Inc.
 */

#include <sys/file.h>

/*
 * nlist - retreive attributes from name list (string table version)
 *         [The actual work is done in ../common/_nlist.c]
 */
nlist(name, list)
	char *name;
	struct nlist *list;
{
	register int fd;
	register int e;

	fd = open(name, O_RDONLY, 0);
	e = _nlist(fd, list);
	close(fd);
	return (e);
}
