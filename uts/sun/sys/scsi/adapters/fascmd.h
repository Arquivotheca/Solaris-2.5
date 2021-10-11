/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_SCSI_ADAPTERS_FASCMD_H
#define	_SYS_SCSI_ADAPTERS_FASCMD_H

#pragma ident	"@(#)fascmd.h	1.12	95/05/09 SMI"

#include <sys/note.h>
#include <sys/scsi/scsi_types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The transport layer deals with things in terms of the following structure.
 * Note	that the target	driver's view of things	is the scsi_pkt	that is
 * enfolded as the first element of the	following structure.
 */
#define	PKT_PRIV_LEN	8	/* preferred pkt_private length	*/
#define	PKT2CMD(pkt)		((struct fas_cmd *)(pkt)->pkt_ha_private)
#define	CMD2PKT(sp)		((sp)->cmd_pkt)

#define	EXTCMD_SIZE (sizeof (struct fas_cmd) + sizeof (struct scsi_pkt))
#define	EXTCMDS_STATUS_SIZE (sizeof (struct scsi_arq_status))

struct fas_cmd {
	struct scsi_pkt		*cmd_pkt;	/* the generic packet itself */
	struct fas_cmd		*cmd_forw;	/* ready fifo que link */
	u_char			*cmd_cdbp;	/* active command pointer */

	u_long			cmd_data_count;	/* aggregate data count */
	u_long			cmd_cur_addr;	/* current dma address */

	u_short			cmd_qfull_retries;

	u_short			cmd_nwin;	/* number of windows */
	u_short			cmd_cur_win;	/* current window */

	u_short			cmd_saved_win;	/* saved window */
	u_long			cmd_saved_data_count; /* saved aggr. count */
	u_long			cmd_saved_cur_addr; /* saved virt address */
	long			cmd_pkt_flags;	/* copy	of pkt_flags */

	ddi_dma_handle_t	cmd_dmahandle;	/* dma handle */
	ddi_dma_cookie_t	cmd_dmacookie;	/* current dma cookie */
	u_long			cmd_dmacount;	/* total xfer count */

	union scsi_cdb		cmd_cdb_un;	/* 'generic' Sun cdb */
#define	cmd_cdb	cmd_cdb_un.cdb_opaque

	u_long			cmd_flags;	/* private flags */
	struct scsi_arq_status	cmd_scb;
	u_char			cmd_scblen;	/* length of scb */
	u_char			cmd_slot;
	u_char			cmd_age;	/* cmd age (tagged queing) */
	u_char			cmd_cdblen;	/* length of cdb */
	u_char			cmd_pkt_private[PKT_PRIV_LEN];
	u_char			cmd_privlen;	/* length of tgt private */
	u_char			cmd_tag[2];	/* command tag */
	u_char			cmd_actual_cdblen; /* length of	cdb */

#ifdef ODDBYTE_HACK
	struct buf		*cmd_bp;
#endif
};

/*
 * private data	for arq	pkt
 */
struct arq_private_data	{
	struct buf	*arq_save_bp;
	struct fas_cmd	*arq_save_sp;
};

/*
 * A note about	the cmd_cdb && cmd_scb structures:
 *
 *	If the command allocation requested exceeds the	size of	CDB_SIZE,
 *	the cdb	will be	allocated outside this structure (via kmem_zalloc)
 *	The same applies to cmd_scb.
 */

/*
 * These are the defined flags for this	structure.
 */
#define	CFLAG_CMDDISC		0x0001	/* cmd currently disconnected */
#define	CFLAG_WATCH		0x0002	/* watchdog time for this command */
#define	CFLAG_FINISHED		0x0004	/* command completed */
#define	CFLAG_CHKSEG		0x0008	/* check cmd_data within seg */
#define	CFLAG_COMPLETED		0x0010	/* completion routine called */
#define	CFLAG_PREPARED		0x0020	/* pkt has been	init'ed	*/
#define	CFLAG_IN_TRANSPORT	0x0040	/* in use by host adapter driver */
#define	CFLAG_RESTORE_PTRS	0x0080	/* implicit restore ptr on reconnect */
#define	CFLAG_TRANFLAG		0x00ff	/* covers transport part of flags */
#define	CFLAG_CMDPROXY		0x000100 /* cmd	is a 'proxy' command */
#define	CFLAG_CMDARQ		0x000200 /* cmd	is a 'rqsense' command */
#define	CFLAG_DMAVALID		0x000400 /* dma	mapping	valid */
#define	CFLAG_DMASEND		0x000800 /* data	is going 'out' */
#define	CFLAG_CMDIOPB		0x001000 /* this	is an 'iopb' packet */
#define	CFLAG_CDBEXTERN		0x002000 /* cdb	kmem_alloc'd */
#define	CFLAG_SCBEXTERN		0x004000 /* scb	kmem_alloc'd */
#define	CFLAG_FREE		0x008000 /* packet is on	free list */
#define	CFLAG_PRIVEXTERN	0x020000 /* target private kmem_alloc'd	*/
#define	CFLAG_DMA_PARTIAL	0x040000 /* partial xfer OK */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_FASCMD_H */
