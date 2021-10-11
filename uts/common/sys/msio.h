/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ifndef _SYS_MSIO_H
#define	_SYS_MSIO_H

#pragma ident	"@(#)msio.h	1.11	92/07/14 SMI"	/* SunOS4.0 1.6 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Mouse related ioctls
 */
typedef struct {
	int	jitter_thresh;
	int	speed_law;
	int	speed_limit;
} Ms_parms;

#define	MSIOC		('m'<<8)	/* same as mtio.h - change ? */
#define	MSIOGETPARMS	(MSIOC|1)	/* get / set jitter, speed  */
#define	MSIOSETPARMS	(MSIOC|2)	/* law, or speed limit */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MSIO_H */
