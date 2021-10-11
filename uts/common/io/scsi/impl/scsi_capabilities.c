/*
 * Copyright (c) 1988-1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)scsi_capabilities.c	1.8	94/01/10 SMI"

/*
 *
 * Generic Capabilities Routines
 *
 */

#include <sys/scsi/scsi.h>

#define	A_TO_TRAN(ap)	(ap->a_hba_tran)

int
scsi_ifgetcap(struct scsi_address *ap, char *cap, int whom)
{
	return (*A_TO_TRAN(ap)->tran_getcap)(ap, cap, whom);
}

int
scsi_ifsetcap(struct scsi_address *ap, char *cap, int value, int whom)
{
	return (*A_TO_TRAN(ap)->tran_setcap)(ap, cap, value, whom);
}
