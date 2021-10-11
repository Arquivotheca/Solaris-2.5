/*
 * Copyright (c) 1988, 1989 Sun Microsystems, Inc.
 */

#ifndef	_SYS_SCSI_SCSI_ADDRESS_H
#define	_SYS_SCSI_SCSI_ADDRESS_H

#pragma ident	"@(#)scsi_address.h	1.11	94/01/10 SMI"

#include <sys/scsi/scsi_types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SCSI address definition.
 *
 *	A SCSI command is sent to a specific lun (sometimes a specific
 *	sub-lun) on a specific target on a specific SCSI bus.
 *	XXX sub-luns are not supported by SUN's host adapter drivers.
 *
 *	The structure here defines this addressing scheme.
 */
struct scsi_address {
	struct scsi_hba_tran	*a_hba_tran;	/* Transport vectors for */
						/*    the SCSI bus */
	u_short			a_target;	/* Target on that bus */
	u_char			a_lun;		/* Lun on that Target */
	u_char			a_sublun;	/* Sublun on that Lun */
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_SCSI_ADDRESS_H */
