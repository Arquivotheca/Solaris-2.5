/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_MEMERR_H
#define	_SYS_MEMERR_H

#pragma ident	"@(#)memerr.h	1.15	93/06/22 SMI"
/* SunOS-4.1 1.9	*/

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * All sun4d implementations have ECC
 */

extern u_int memerr_init(void);
extern void memerr_ECI(u_int enable);
extern u_int memerr(u_int type);

/* parameters to memerr() */
#define	MEMERR_CE	(1 << 0)
#define	MEMERR_UE	(1 << 1)
#define	MEMERR_FATAL	(1 << 2)

extern int memscrub_init(void);
extern int memscrub_add_span(u_int pfn, u_int pages);
extern int memscrub_delete_span(u_int pfn, u_int bytes);

#define	PROP_DEVICE_ID "device-id"	/* move elsewhere */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MEMERR_H */
