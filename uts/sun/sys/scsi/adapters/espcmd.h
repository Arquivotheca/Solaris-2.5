/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_SCSI_ADAPTERS_ESPCMD_H
#define	_SYS_SCSI_ADAPTERS_ESPCMD_H

#pragma ident	"@(#)espcmd.h	1.15	95/02/13 SMI"

#include <sys/scsi/scsi_types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	PKT_PRIV_LEN	8	/* preferred pkt_private length */

/*
 * define size of extended scsi cmd pkt (ie. includes ARQ)
 */
#define	EXTCMDS_STATUS_SIZE  (sizeof (struct scsi_arq_status))

/*
 * esp_cmd is selectively zeroed.  During packet allocation, some
 * fields need zeroing, others will be initialized in esp_prepare_pkt()
 */
struct esp_cmd {
	struct scsi_pkt		cmd_pkt;	/* the generic packet itself */
	struct esp_cmd		*cmd_forw;	/* ready fifo que link	*/
	u_char			*cmd_cdbp;	/* active command pointer */
	u_char			*cmd_scbp;	/* active status pointer */
	u_long			cmd_flags;	/* private flags */

	u_long			cmd_data_count;	/* aggregate data count */
	u_long			cmd_cur_addr;	/* current dma address */

	u_short			cmd_nwin;	/* number of windows */
	u_short			cmd_cur_win;	/* current window */

	u_short			cmd_saved_win;	/* saved window */
	u_long			cmd_saved_data_count; /* saved aggr. count */
	u_long			cmd_saved_cur_addr; /* saved virt address */

	ddi_dma_handle_t	cmd_dmahandle;	/* dma handle */
	ddi_dma_cookie_t	cmd_dmacookie;	/* current dma cookie */
	u_long			cmd_dmacount;	/* total xfer count */

	long			cmd_timeout;	/* command timeout */
	union scsi_cdb		cmd_cdb_un;	/* 'generic' Sun cdb */
#define	cmd_cdb	cmd_cdb_un.cdb_opaque
	u_char			cmd_pkt_private[PKT_PRIV_LEN];
	u_char			cmd_cdblen;	/* actual length of cdb */
	u_char			cmd_cdblen_alloc; /* length of cdb alloc'ed */
	u_char			cmd_qfull_retries; /* QFULL retry count */
	u_int			cmd_scblen;	/* length of scb */
	u_int			cmd_privlen;	/* length of tgt private */
	u_char			cmd_scb[EXTCMDS_STATUS_SIZE]; /* arq size  */
	u_short			cmd_age;	/* cmd age (tagged queing) */
	u_char			cmd_tag[2];	/* command tag */
};


/*
 * These are the defined flags for this structure.
 */
#define	CFLAG_CMDDISC		0x0001	/* cmd currently disconnected */
#define	CFLAG_WATCH		0x0002	/* watchdog time for this command */
#define	CFLAG_FINISHED		0x0004	/* command completed */
#define	CFLAG_COMPLETED		0x0010	/* completion routine called */
#define	CFLAG_PREPARED		0x0020	/* pkt has been init'ed */
#define	CFLAG_IN_TRANSPORT 	0x0040	/* in use by host adapter driver */
#define	CFLAG_RESTORE_PTRS	0x0080	/* implicit restore ptr on reconnect */
#define	CFLAG_TRANFLAG		0x00ff	/* covers transport part of flags */
#define	CFLAG_CMDPROXY		0x000100	/* cmd is a 'proxy' command */
#define	CFLAG_CMDARQ		0x000200	/* cmd is a 'rqsense' command */
#define	CFLAG_DMAVALID		0x000400	/* dma mapping valid */
#define	CFLAG_DMASEND		0x000800	/* data is going 'out' */
#define	CFLAG_CMDIOPB		0x001000	/* this is an 'iopb' packet */
#define	CFLAG_CDBEXTERN		0x002000	/* cdb kmem_alloc'd */
#define	CFLAG_SCBEXTERN		0x004000	/* scb kmem_alloc'd */
#define	CFLAG_FREE		0x008000	/* packet is on free list */
#define	CFLAG_PRIVEXTERN	0x020000 	/* kmem_alloc'd */
#define	CFLAG_DMA_PARTIAL	0x040000 	/* partial xfer OK */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_ESPCMD_H */
