/*
 * Copyright (c) 1988, 1989, 1990 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_SCSI_SCSI_PKT_H
#define	_SYS_SCSI_SCSI_PKT_H

#pragma ident	"@(#)scsi_pkt.h	1.26	95/03/19 SMI"

#include <sys/scsi/scsi_types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SCSI packet definition.
 *
 *	This structure defines the packet which is allocated by a library
 *	function and handed to a target driver. The target driver fills
 *	in some information, and passes it to the library for transport
 *	to an addressed SCSI target/lun/sublun. The host adapter found by
 *	the library fills in some other information as the command is
 *	processed. When the command completes (or can be taken no further)
 *	the function specified in the packet is called with a pointer to
 *	the packet as it argument. From fields within the packet, the target
 *	driver can determine the success or failure of the command.
 */

struct scsi_pkt {
	opaque_t pkt_ha_private;	/* private data for host adapter */
	struct scsi_address pkt_address;	/* destination packet is for */
	opaque_t pkt_private;		/* private data for target driver */
	void	(*pkt_comp)();		/* completion routine */
	long	pkt_flags;		/* flags */
	long	pkt_time;		/* time allotted to complete command */
	u_char	*pkt_scbp;		/* pointer to status block */
	u_char	*pkt_cdbp;		/* pointer to command block */
	long	pkt_resid;		/* data bytes not transferred */
	u_long	pkt_state;		/* state of command */
	u_long	pkt_statistics;		/* statistics */
	u_char	pkt_reason;		/* reason completion called */
};

/*
 * Definitions for the pkt_flags field.
 */
#define	FLAG_NOINTR	0x0001	/* Run command with no cmd completion	*/
				/* callback; command has been completed	*/
				/* upon	return from scsi_transport(9F)	*/
#define	FLAG_NODISCON	0x0002	/* Run command without disconnects	*/
#define	FLAG_SUBLUN	0x0004	/* Use the sublun field in pkt_address	*/
#define	FLAG_NOPARITY	0x0008	/* Run command without parity checking	*/
#define	FLAG_HTAG	0x1000	/* Run command as HEAD OF QUEUE tagged cmd */
#define	FLAG_OTAG	0x2000	/* Run command as ORDERED QUEUE tagged cmd */
#define	FLAG_STAG	0x4000	/* Run command as SIMPLE QUEUE tagged cmd */

#define	FLAG_QUEHOLD	0x0200	/* Don't advance HA que until cmd completes */
#define	FLAG_SENSING	0x0400	/* Running request sense for failed pkt */
#define	FLAG_IMMEDIATE_CB \
			0x0800	/* Immediate callback on command */
				/* completion, ie. do not defer */
#define	FLAG_HEAD	0x8000	/* Run command as HEAD OF QUEUE tagged cmd */

#define	FLAG_TAGMASK	(FLAG_HTAG|FLAG_OTAG|FLAG_STAG)

/*
 * Definitions for Uscsi options to the flags field
 */
#define	FLAG_SILENT	0x00010000
#define	FLAG_DIAGNOSE	0x00020000
#define	FLAG_ISOLATE	0x00040000

/*
 * Definitions for the pkt_reason field.
 */

#define	CMD_CMPLT	0	/* no transport errors- normal completion */
#define	CMD_INCOMPLETE	1	/* transport stopped with not normal state */
#define	CMD_DMA_DERR	2	/* dma direction error occurred */
#define	CMD_TRAN_ERR	3	/* unspecified transport error */
#define	CMD_RESET	4	/* SCSI bus reset destroyed command */
#define	CMD_ABORTED	5	/* Command transport aborted on request */
#define	CMD_TIMEOUT	6	/* Command timed out */
#define	CMD_DATA_OVR	7	/* Data Overrun */
#define	CMD_CMD_OVR	8	/* Command Overrun */
#define	CMD_STS_OVR	9	/* Status Overrun */
#define	CMD_BADMSG	10	/* Message not Command Complete */
#define	CMD_NOMSGOUT	11	/* Target refused to go to Message Out phase */
#define	CMD_XID_FAIL	12	/* Extended Identify message rejected */
#define	CMD_IDE_FAIL	13	/* Initiator Detected Error message rejected */
#define	CMD_ABORT_FAIL	14	/* Abort message rejected */
#define	CMD_REJECT_FAIL 15	/* Reject message rejected */
#define	CMD_NOP_FAIL	16	/* No Operation message rejected */
#define	CMD_PER_FAIL	17	/* Message Parity Error message rejected */
#define	CMD_BDR_FAIL	18	/* Bus Device Reset message rejected */
#define	CMD_ID_FAIL	19	/* Identify message rejected */
#define	CMD_UNX_BUS_FREE	20	/* Unexpected Bus Free Phase occurred */
#define	CMD_TAG_REJECT	21	/* Target rejected our tag message */

/*
 * Definitions for the pkt_state field
 */

#define	STATE_GOT_BUS		0x01	/* SCSI bus arbitration succeeded */
#define	STATE_GOT_TARGET	0x02	/* Target successfully selected */
#define	STATE_SENT_CMD		0x04	/* Command successfully sent */
#define	STATE_XFERRED_DATA	0x08	/* Data transfer took place */
#define	STATE_GOT_STATUS	0x10	/* SCSI status received */
#define	STATE_ARQ_DONE		0x20	/* auto rqsense took place */

/*
 * Definitions for the pkt_statistics field
 */

#define	STAT_DISCON	0x1	/* Command experienced a disconnect */
#define	STAT_SYNC	0x2	/* Command did a synchronous data transfer */
#define	STAT_PERR	0x4	/* Command experienced a SCSI parity error */
#define	STAT_BUS_RESET	0x8	/* Command experienced a bus reset */
#define	STAT_DEV_RESET	0x10	/* Command experienced a device reset */
#define	STAT_ABORTED	0x20	/* Command was aborted */
#define	STAT_TIMEOUT	0x40	/* Command experienced a timeout */

/*
 * Definitions for what scsi_transport returns
 */

#define	TRAN_ACCEPT		1
#define	TRAN_BUSY		0
#define	TRAN_BADPKT		-1
#define	TRAN_FATAL_ERROR	-2	/* HBA cannot accept any pkts */

#ifdef	_KERNEL
/*
 * Kernel function declarations
 */

#define	pkt_transport	scsi_transport

#ifdef	__STDC__
extern int scsi_transport(struct scsi_pkt *pkt);
#else	/* __STDC__ */
extern int scsi_transport(/* struct scsi_pkt *pkt */);
#endif	/* __STDC__ */

#define	SCSI_POLL_TIMEOUT	60

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_SCSI_PKT_H */
