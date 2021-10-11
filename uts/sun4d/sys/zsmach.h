/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef _SYS_ZSMACH_H
#define	_SYS_ZSMACH_H

#pragma ident	"@(#)zsmach.h	1.3	93/06/10 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Sun 4d platform dependent software definitions for zs driver.
 */
#define	MAXZS		40
#define	ZSDELAY()
#define	ZSFLUSH()	{ \
				register u_char tmp; \
				tmp = zs->zs_addr->zscc_control; \
			}
#define	ZSNEXTPOLL(zs, zscurr) \
	{ \
		register struct zscom   *zstmp; \
		zstmp = &zscom[(((CPU->cpu_id) >> 1) <<2) + 1]; \
		if ((zstmp != zs) && ((zstmp + 2) != zs)) { \
			zscurr = zs = zstmp; \
		} \
	}

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZSMACH_H */
