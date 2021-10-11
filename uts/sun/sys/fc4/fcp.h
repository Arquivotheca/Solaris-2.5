/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_FC4_FCP_H
#define	_SYS_FC4_FCP_H

#pragma ident	"@(#)fcp.h	1.3	95/02/10 SMI"

/*
 * Frame format and protocol definitions for transferring
 * commands and data between a SCSI initiator and target
 * using an FC4 serial link interface.
 */

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * FCP Device Data Frame Information Categories
 */
#define	FCP_SCSI_DATA		0x01	/* frame contains SCSI data */
#define	FCP_SCSI_CMD		0x02	/* frame contains SCSI command */
#define	FCP_SCSI_RSP		0x03	/* frame contains SCSI response */
#define	FCP_SCSI_XFER_RDY	0x05	/* frame contains xfer rdy block */


/*
 * FCP SCSI Control structure
 */
typedef struct fcp_cntl {
	u_char	cntl_reserved_0;		/* reserved */
	u_char	cntl_reserved_1	: 5,		/* reserved */
		cntl_qtype	: 3;		/* tagged queueing type */
	u_char	cntl_reserved_2	: 2,		/* reserved */
		cntl_reset	: 1,		/* reset (may change) */
		cntl_read_data	: 1,		/* initiator read */
		cntl_write_data	: 1,		/* initiator write */
		cntl_reserved_3	: 3;		/* reserved */
	u_char	cntl_reserved_4;		/* reserved */
} fcp_cntl_t;


/*
 * FCP SCSI Control Tagged Queueing types - cntl_qtype
 */
#define	FCP_QTYPE_SIMPLE	0		/* simple queueing */
#define	FCP_QTYPE_HEAD_OF_Q	1		/* head of queue */
#define	FCP_QTYPE_ORDERED	2		/* ordered queueing */
#define	FCP_QTYPE_ACA_Q_TAG	4		/* ACA queueing */



/*
 * FCP SCSI Entity Address
 *
 * ent_addr_0 is always the first and highest layer of
 * the hierarchy.  The depth of the hierarchy of addressing,
 * up to a maximum of four layers, is arbitrary and
 * device-dependent.
 */
typedef struct fcp_ent_addr {
	u_short	ent_addr_0;		/* entity address 0 */
	u_short	ent_addr_1;		/* entity address 1 */
	u_short	ent_addr_2;		/* entity address 2 */
	u_short	ent_addr_3;		/* entity address 3 */
} fcp_ent_addr_t;


/*
 * Maximum size of SCSI cdb in FCP SCSI command
 */
#define	FCP_CDB_SIZE		16

/*
 * FCP SCSI command payload
 */
typedef struct fcp_cmd {
	fcp_ent_addr_t	fcp_ent_addr;			/* entity address */
	fcp_cntl_t	fcp_cntl;			/* SCSI options */
	u_char		fcp_cdb[FCP_CDB_SIZE];		/* SCSI cdb */
	int		fcp_data_len;			/* data length */
} fcp_cmd_t;


/*
 * FCP SCSI status
 */
typedef struct fcp_status {
	u_short reserved_0;			/* reserved */
	u_char	reserved_1	: 5,		/* reserved */
		resid_len_set	: 1,		/* resid non-zero */
		sense_len_set	: 1,		/* sense_len non-zero */
		rsp_len_set	: 1;		/* response_len non-zero */
	u_char	scsi_status;			/* status of cmd */
} fcp_status_t;


/*
 * FCP SCSI Response Payload
 */
typedef struct fcp_rsp {
	u_int	reserved_0;			/* reserved */
	u_int	reserved_1;			/* reserved */
	union {
		fcp_status_t	fcp_status;		/* command status */
		u_int		i_fcp_status;
	}fcp_u;
	u_int		fcp_resid;		/* resid of operation */
	u_int		fcp_sense_len;		/* sense data length */
	u_int		fcp_response_len;	/* response data length */
	/*
	 * 'n' bytes of scsi sense info follow
	 * 'm' bytes of scsi response info follow
	 */
} fcp_rsp_t;

/*
 * SCSI FCP_RSP_INFO structure
 */
typedef	struct fcp_scsi_bus_err {
	u_char	rsp_info_type;
	u_char	resv;
	u_short	isp_status;
	u_short	isp_state_flags;
	u_short isp_stat_flags;
} fcp_scsi_bus_err_t;

/*
 * Definitions of FCP_RSP_INFO fields
 */
/* Type */
#define	FCP_RSP_SCSI_BUS_ERR		1
#define	FCP_RSP_SCSI_PORT_ERR		2
#define	FCP_RSP_SOC_ERR			3

/* Definitions for isp_status: */
#define	FCP_RSP_CMD_COMPLETE		0x0000
#define	FCP_RSP_CMD_INCOMPLETE		0x0001
#define	FCP_RSP_CMD_DMA_ERR		0x0002
#define	FCP_RSP_CMD_TRAN_ERR		0x0003
#define	FCP_RSP_CMD_RESET		0x0004
#define	FCP_RSP_CMD_ABORTED		0x0005
#define	FCP_RSP_CMD_TIMEOUT		0x0006
#define	FCP_RSP_CMD_OVERRUN		0x0007

/*
 *	Definitions for isp_state_flags:
 */
#define	FCP_RSP_ST_GOT_BUS		0x0100
#define	FCP_RSP_ST_GOT_TARGET		0x0200
#define	FCP_RSP_ST_SENT_CMD		0x0400
#define	FCP_RSP_ST_XFRD_DATA		0x0800
#define	FCP_RSP_ST_GOT_STATUS		0x1000
#define	FCP_RSP_ST_GOT_SENSE		0x2000

/*
 *	Definitions for isp_stat_flags:	"ik12"
 */
#define	FCP_RSP_STAT_DISC		0x0001
#define	FCP_RSP_STAT_SYNC		0x0002
#define	FCP_RSP_STAT_PERR		0x0004
#define	FCP_RSP_STAT_BUS_RESET		0x0008
#define	FCP_RSP_STAT_DEV_RESET		0x0010
#define	FCP_RSP_STAT_ABORTED		0x0020
#define	FCP_RSP_STAT_TIMEOUT		0x0040
#define	FCP_RSP_STAT_NEGOTIATE		0x0080


/*
 * FCP SCSI_ XFER_RDY Payload
 */
typedef struct fcp_xfer_rdy {
	u_long		fcp_seq_offset;		/* relative offset */
	u_long		fcp_burst_len;		/* buffer space */
	u_long		reserved;		/* reserved */
} fcp_xfer_rdy_t;


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FC4_FCP_H */
