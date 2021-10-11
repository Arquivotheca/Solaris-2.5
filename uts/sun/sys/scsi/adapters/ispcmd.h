/*
 * Copyright (c) 1989-1994, by Sun Microsystems, Inc.
 */

#ifndef _SYS_SCSI_ADAPTERS_ISPCMD_H
#define	_SYS_SCSI_ADAPTERS_ISPCMD_H

#pragma ident	"@(#)ispcmd.h	1.14	95/01/10 SMI"

#include <sys/scsi/scsi_types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	PKT_PRIV_LEN		8	/* preferred pkt_private length */
#define	PKT2CMD(pkt)		((struct isp_cmd *)(pkt)->pkt_ha_private)
#define	CMD2PKT(sp)		((sp)->cmd_pkt)

/*
 * isp_cmd is selectively zeroed.  During packet allocation, some
 * fields need zeroing, others need to be initialized.
 * isp_cmd needs to be a multiple of 32 bytes. cmd_isp_request/response
 * need to be 32 byte aligned which why these members have to be at
 * the beginning of this structure
 */
struct isp_cmd {
	struct isp_request	cmd_isp_request; /* don't move!! */
	struct isp_response	cmd_isp_response; /* don't move!! */

	struct scsi_pkt		*cmd_pkt;	/* needs to be INITialized */
	struct isp_cmd		*cmd_forw;	/* queue link */
						/* needs ZEROING */
	u_char			*cmd_cdbp;	/* active command pointer */
	u_char			*cmd_scbp;	/* active status pointer */

	u_long			cmd_dmacount;
	ddi_dma_handle_t	cmd_dmahandle;	/* dma handle */
	ddi_dma_cookie_t	cmd_dmacookie;	/* current dma cookie */
	u_long			cmd_start_time;	/* lbolt start time */
	clock_t			cmd_deadline;	/* cmd completion time */
	union scsi_cdb		cmd_cdb_un;	/* 'generic' Sun cdb */
						/* needs ZEROING */
						/* and word alignment */
#define	cmd_cdb cmd_cdb_un.cdb_opaque
	u_long			cmd_flags;	/* private flags */
						/* needs ZEROING */
	u_short			cmd_slot;	/* index free slot list */
	u_int			cmd_cdblen;	/* length of cdb */
						/* needs to be INITialized */
	u_int			cmd_scblen;	/* length of scb */
						/* needs to be INITialized */
	u_int			cmd_privlen;	/* length of tgt private */
						/* needs to be INITialized */
	u_char			cmd_pkt_private[PKT_PRIV_LEN];
						/* needs ZEROING */
						/* and word alignment */
};


/*
 * Define size of extended scsi cmd pkt (ie. includes ARQ)
 */
#define	EXTCMDS_STATUS_SIZE  (sizeof (struct scsi_arq_status))
#define	EXTCMDS_SIZE  ((((EXTCMDS_STATUS_SIZE + sizeof (struct isp_cmd) + \
	sizeof (struct scsi_pkt))) + 0x1f) & ~0x1f)

/*
 * These are the defined flags for this structure.
 */
#define	CFLAG_FINISHED		0x0001	/* command completed */
#define	CFLAG_COMPLETED		0x0002	/* completion routine called */
#define	CFLAG_IN_TRANSPORT	0x0004	/* in use by isp driver */
#define	CFLAG_TRANFLAG		0x000f	/* transport part of flags */
#define	CFLAG_DMAVALID		0x0010	/* dma mapping valid */
#define	CFLAG_DMASEND		0x0020	/* data is going 'out' */
#define	CFLAG_CMDIOPB		0x0040	/* this is an 'iopb' packet */
#define	CFLAG_CDBEXTERN		0x0100	/* cdb kmem_alloc'd */
#define	CFLAG_SCBEXTERN		0x0200	/* scb kmem_alloc'd */
#define	CFLAG_FREE		0x0400	/* packet is on free list */
#define	CFLAG_PRIVEXTERN	0x1000	/* target private was */
					/* kmem_alloc'd */
#define	CFLAG_DMA_PARTIAL	0x2000	/* partial xfer OK */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_ISPCMD_H */
