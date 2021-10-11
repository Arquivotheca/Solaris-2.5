/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef _SYS_ZSMACH_H
#define	_SYS_ZSMACH_H

#pragma ident	"@(#)zsmach.h	1.4	93/06/14 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Sun 4 platform dependent software definitions for zs driver.
 */
#define	ZSDELAY()	drv_usecwait(zs_usec_delay)
#define	ZSFLUSH()
#define	ZSNEXTPOLL(zs, zscurr)
#define	MAXZS		6

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZSMACH_H */
