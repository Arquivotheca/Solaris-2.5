/*
 * Copyright (c) 1988-1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_SCSI_SCSI_CTL_H
#define	_SYS_SCSI_SCSI_CTL_H

#pragma ident	"@(#)scsi_ctl.h	1.11	94/03/28 SMI"

#include <sys/scsi/scsi_types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SCSI Control Information
 *
 * Defines for stating level of reset.
 */

#define	RESET_ALL	0	/* reset SCSI bus, host adapter, everything */
#define	RESET_TARGET	1	/* reset SCSI target */

/*
 * Defines for scsi_reset_notify flag, to register or cancel
 * the notification of external and internal SCSI bus resets.
 */
#define	SCSI_RESET_NOTIFY	0x01	/* register the reset notification */
#define	SCSI_RESET_CANCEL	0x02	/* cancel the reset notification */

#ifdef	_KERNEL

/*
 * Kernel function declarations
 */

/*
 * Capabilities functions
 */

#ifdef	__STDC__
extern scsi_ifgetcap(struct scsi_address *ap, char *cap, int whom);
extern scsi_ifsetcap(struct scsi_address *ap, char *cap, int value, int whom);
#else	/* __STDC__ */
extern int scsi_ifgetcap(), scsi_ifsetcap();
#endif	/* __STDC__ */

/*
 * Abort and Reset functions
 */

#ifdef	__STDC__
extern int scsi_abort(struct scsi_address *, struct scsi_pkt *);
extern int scsi_reset(struct scsi_address *, int);
extern int scsi_reset_notify(struct scsi_address *ap, int flag,
	void (*callback)(caddr_t), caddr_t arg);
#else	/* __STDC__ */
extern int scsi_abort(), scsi_reset();
extern int scsi_reset_notify();
#endif	/* __STDC__ */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_SCSI_CTL_H */
