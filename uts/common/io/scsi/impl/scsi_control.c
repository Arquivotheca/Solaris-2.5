/*
 * Copyright (c) 1989-1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)scsi_control.c	1.8	94/03/28 SMI"

/*
 * Generic Abort and Reset Routines			*
 */

#include <sys/scsi/scsi.h>


#define	A_TO_TRAN(ap)	(ap->a_hba_tran)

int
scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	return (*A_TO_TRAN(ap)->tran_abort)(ap, pkt);
}

int
scsi_reset(struct scsi_address *ap, int level)
{
	return (*A_TO_TRAN(ap)->tran_reset)(ap, level);
}

int
scsi_reset_notify(struct scsi_address *ap, int flag,
	void (*callback)(caddr_t), caddr_t arg)
{
	if ((A_TO_TRAN(ap)->tran_reset_notify) == NULL) {
		return (DDI_FAILURE);
	}
	return (*A_TO_TRAN(ap)->tran_reset_notify)(ap, flag, callback, arg);
}
