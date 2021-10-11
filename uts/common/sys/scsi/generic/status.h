/*
 * Copyright (c) 1988-994 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_SCSI_GENERIC_STATUS_H
#define	_SYS_SCSI_GENERIC_STATUS_H

#pragma ident	"@(#)status.h	1.11	94/02/08 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SCSI status completion block
 *
 * Compliant with SCSI-2, Rev 10b
 * August 22, 1989
 *
 * The SCSI standard specifies one byte of status.
 *
 */

/*
 * The definition of of the Status block as a bitfield
 */

struct scsi_status {
#if defined(_BIT_FIELDS_LTOH)
	u_char	sts_vu0		: 1,	/* vendor unique 		*/
		sts_chk		: 1,	/* check condition 		*/
		sts_cm		: 1,	/* condition met 		*/
		sts_busy	: 1,	/* device busy or reserved 	*/
		sts_is		: 1,	/* intermediate status sent 	*/
		sts_vu6		: 1,	/* vendor unique 		*/
		sts_vu7		: 1,	/* vendor unique 		*/
		sts_resvd	: 1;	/* reserved 			*/
#elif defined(_BIT_FIELDS_HTOL)
	u_char	sts_resvd	: 1,	/* reserved */
		sts_vu7		: 1,	/* vendor unique */
		sts_vu6		: 1,	/* vendor unique */
		sts_is		: 1,	/* intermediate status sent */
		sts_busy	: 1,	/* device busy or reserved */
		sts_cm		: 1,	/* condition met */
		sts_chk		: 1,	/* check condition */
		sts_vu0		: 1;	/* vendor unique */
#else
#error	One of _BIT_FIELDS_LTOH or _BIT_FIELDS_HTOL must be defined
#endif	/* _BIT_FIELDS_LTOH */
};
#define	sts_scsi2	sts_vu6		/* SCSI-2 modifier bit */

/*
 * if auto request sense has been enabled, then use this structure
 */
struct scsi_arq_status {
	struct scsi_status	sts_status;
	struct scsi_status	sts_rqpkt_status;
	u_char			sts_rqpkt_reason;	/* reason completion */
	u_char			sts_rqpkt_resid;	/* residue */
	u_long			sts_rqpkt_state;	/* state of command */
	u_long			sts_rqpkt_statistics;	/* statistics */
	struct scsi_extended_sense sts_sensedata;
};

#define	SECMDS_STATUS_SIZE  (sizeof (struct scsi_arq_status))

/*
 * Bit Mask definitions, for use accessing the status as a byte.
 */

#define	STATUS_MASK			0x3E
#define	STATUS_GOOD			0x00
#define	STATUS_CHECK			0x02
#define	STATUS_MET			0x04
#define	STATUS_BUSY			0x08
#define	STATUS_INTERMEDIATE		0x10
#define	STATUS_SCSI2			0x20

#define	STATUS_INTERMEDIATE_MET		(STATUS_INTERMEDIATE|STATUS_MET)
#define	STATUS_RESERVATION_CONFLICT	(STATUS_INTERMEDIATE|STATUS_BUSY)
#define	STATUS_TERMINATED		(STATUS_SCSI2|STATUS_CHECK)
#define	STATUS_QFULL			(STATUS_SCSI2|STATUS_BUSY)

#ifdef	__cplusplus
}
#endif

/*
 * Some deviations from the one byte of Status are known. Each
 * implementation will define them specifically
 */

#include <sys/scsi/impl/status.h>

#endif	/* _SYS_SCSI_GENERIC_STATUS_H */
