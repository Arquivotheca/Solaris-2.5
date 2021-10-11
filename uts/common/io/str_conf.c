/*
 * Copyright (c) 1986-1990 by Sun Microsystems, Inc.
 */

#ident	"@(#)str_conf.c	1.35	93/02/05 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/stream.h>

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/t_lock.h>

struct	fmodsw fmodsw[] =
{
	{ "",		NULL,		0 }, /* reserved for loadable modules */
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
	{ "",		NULL,		0 },
};

int	fmodcnt = sizeof (fmodsw) / sizeof (fmodsw[0]) - 1;

kmutex_t fmodsw_lock;	/* Lock for dynamic allocation of fmodsw entries */
