/*
 * Copyright (c) 1988, 1989 Sun Microsystems, Inc.
 */

#ifndef	_SYS_SCSI_SCSI_PARAMS_H
#define	_SYS_SCSI_SCSI_PARAMS_H

#pragma ident	"@(#)scsi_params.h	1.10	94/04/06 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * General SCSI parameters
 */

#define	NTARGETS		8	/* total # of targets per SCSI bus */
#define	NTARGETS_WIDE		16	/* #targets per wide SCSI bus */
#define	NLUNS_PER_TARGET	8	/* number of luns per target */

#define	NUM_SENSE_KEYS		16	/* total number of Sense keys */

#define	NTAGS			256	/* number of tags per lun */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_SCSI_PARAMS_H */
