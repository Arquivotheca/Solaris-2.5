/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_IPD_EXTERN_H
#define	_SYS_IPD_EXTERN_H

#pragma ident	"@(#)ipd_extern.h	1.5	93/05/20 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * extern declarations
 */
extern queue_t 		*ipd_cm;
extern int 		ipd_debug;
extern timestruc_t 	hrestime;
extern queue_t 		*ipd_cm;
void			ipd_init();

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IPD_EXTERN_H */
