/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef _SYS_ZSMACH_H
#define	_SYS_ZSMACH_H

#pragma ident	"@(#)zsmach.h	1.3	95/02/14 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * sun4u platform dependent software definitions for zs driver.
 */
#define	ZSDELAY()
#define	ZSFLUSH()	{ \
				volatile register u_char tmp; \
				tmp = zs->zs_addr->zscc_control; \
			}
#define	ZSNEXTPOLL(zs, zscurr)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZSMACH_H */
