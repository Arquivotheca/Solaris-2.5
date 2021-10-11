/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MHD_H
#define	_SYS_MHD_H

#pragma ident	"@(#)mhd.h	1.4	94/04/22 SMI"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Definitions for multi-host device I/O control commands
 */
#define	MHIOC		('M'<<8)
#define	MHIOCENFAILFAST	(MHIOC|1)
#define	MHIOCTKOWN	(MHIOC|2)
#define	MHIOCRELEASE	(MHIOC|3)
#define	MHIOCSTATUS	(MHIOC|4)

/*
 * Following is the structure to specify the delay parameters in
 * milliseconds, via the MHIOCTKOWN ioctl.
 */
struct mhioctkown {
	int reinstate_resv_delay;
	int min_ownership_delay;
	int max_ownership_delay;
};

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_MHD_H */
