/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)hat_conf.c	1.12	94/06/09 SMI"

#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/systm.h>
#include <vm/hat.h>

extern struct hatops sunm_hatops;

struct hatsw hattab[] = {
	"sunm", &sunm_hatops,
	0, 0
};

#define	NHATTAB	(sizeof (hattab) / sizeof (struct hatsw))

/*
 * hat_getops - for a hat identified by the given name string,
 *	return a hat ops vector if one exists, else return (NULL);
 */
struct hatops *
hat_getops(namestr)
	char *namestr;
{
	int i;

	for (i = 0; i < NHATTAB; i++) {
		if (hattab[i].hsw_name == NULL)
			break;
		if (strcmp(namestr, hattab[i].hsw_name) == 0)
			return (hattab[i].hsw_ops);
	}

	return (NULL);
}
