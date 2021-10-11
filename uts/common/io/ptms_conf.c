/*
 * Copyright (c) 1987-1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)ptms_conf.c	1.8	93/05/28 SMI"

/*
 * Pseudo-terminal driver.
 *
 * Configuration dependent variables
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/termios.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/kmem.h>
#include <sys/ptms.h>

#ifndef	NPTY
#define	NPTY	48		/* crude XXX */
#endif

int	pt_cnt = NPTY;

struct	pt_ttys *ptms_tty;

kmutex_t	pt_lock;	/* initialized in ptm.c */

/*
 * Allocate space for data structures at runtime.
 */
void
ptms_initspace(void)
{
	ptms_tty = (struct pt_ttys *)
	    kmem_zalloc(pt_cnt * sizeof (struct pt_ttys), KM_SLEEP);
}
