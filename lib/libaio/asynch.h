/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef _SYS_ASYNCH_H
#define	_SYS_ASYNCH_H

#pragma ident	"@(#)asynch.h	1.8	94/04/20 SMI"

#include <sys/types.h>
#include <sys/aio.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	AIO_INPROGRESS	-2	/* values not set by the system */

int	aioread(int, caddr_t, int, off_t, int, aio_result_t *);
int	aiowrite(int, caddr_t, int, off_t, int, aio_result_t *);
int	aiocancel(aio_result_t *);
aio_result_t *aiowait(struct timeval *);

#define	MAXASYNCHIO 200		/* maxi.number of outstanding i/o's */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ASYNCH_H */
