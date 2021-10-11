/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_SCSI_ADAPTERS_NCRREG_H
#define	_SYS_SCSI_ADAPTERS_NCRREG_H

#pragma ident	"@(#)ncrreg.h	1.9	94/09/29 SMI"

/*
 * Based upon redhots version 1.8
 *
 *	8/90	payne	Add nodisco capability code support.
 *	1/90	ksam	Add multi-host (3/e in 4/110) support.
 *	11/89	mjacob	Add SCSI-3 and 3/E support (4.1-FCS release).
 *	8/89	ksam	Initial porting to 4/110 based on new Sun's SCSA.
 */

/*
 * Hardware Definitions
 */

/*
 * Include definitions of the NCR 5380 SBC (SCSI Bus Controller)
 */

#include <sys/scsi/adapters/ncrsbc.h>

/*
 * Include defintions of ancillary control registers
 */

#include <sys/scsi/adapters/ncrctl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__STDC__
#define	VOLATILE	volatile
#else
#define	VOLATILE
#endif


/*
 * HBA interface macros
 */
#define	SDEV2TRAN(sd)		((sd)->sd_address.a_hba_tran)
#define	SDEV2ADDR(sd)		(&((sd)->sd_address))
#define	PKT2TRAN(pkt)		((pkt)->pkt_address.a_hba_tran)
#define	ADDR2TRAN(ap)		((ap)->a_hba_tran)

#define	TRAN2NCR(tran)		((struct ncr *)(tran)->tran_hba_private)
#define	SDEV2NCR(sd)		(TRAN2NCR(SDEV2TRAN(sd)))
#define	PKT2NCR(pkt)		(TRAN2NCR(PKT2TRAN(pkt)))
#define	ADDR2NCR(ap)		(TRAN2NCR(ADDR2TRAN(ap)))


/*
 * Soft structure Definitions for the NCR 5380 based SCSI host adapters
 */

struct ncr {

	/*
	 * Transport structure for this instance of the hba
	 */
	scsi_hba_tran_t		*n_tran;

	/*
	 * dev_info_t reference can be found in the transport structure
	 */
#define	n_dip			n_tran->tran_hba_dip

	/*
	 * Mutex
	 */
	kmutex_t		n_mutex;

	/*
	 * Interrupt block cookie
	 */
	ddi_iblock_cookie_t	n_iblock;


	/*
	 * Configuration information for this host adapter.
	 */
	u_char	n_type;		/* type of ncr 5380 variant */
	u_char	n_id;		/* initiator id */
	u_char	n_state;	/* state of this command while active */
	u_char	n_laststate;	/* last state of this command (while active) */

	/*
	 * Message handling: enough space is reserved for the expected length
	 * of all messages we could either send or receive.
	 *
	 * For sending, we expect to send only (possibly) an EXTENDED IDENTIFY
	 * message. We keep a history of the last message sent, and in order
	 * to control which message to send, an output message length is set
	 * to indicate whether and how much of the message area is to be used
	 * in sending a message. If a target shifts to message out phase
	 * unexpectedly, the default action will be to send a MSG_NOP message.
	 *
	 * After the successful transmission of a message, the initial message
	 * byte is moved to the n_last_msgout area for tracking what was the
	 * last message sent.
	 */

#define	OMSGHWSIZE	5
	u_char		n_cur_msgout[OMSGHWSIZE];
	u_char		n_last_msgout;
	u_char		n_omsglen;
	u_char		n_omsgidx;

	/*
	 * We expect, at, most, to receive a maximum of 7 bytes
	 * of an incoming extended message (MODIFY DATA POINTER),
	 * and thus reserve enough space for that.
	 */
#define	IMSGHWSIZE	8
	u_char		n_imsgarea[IMSGHWSIZE];
#define	n_last_msgin	n_imsgarea[0]
#define	n_cdr		n_imsgarea[1]

	/*
	 * These are used for incoming extended message only...
	 */
	u_char		n_emsglen;
	u_char		n_emsgindex;

	/*
	 * Target information
	 *	Disconnect/Reselect capabilities
	 */
	u_char		n_nodisc;

	/*
	 * Instrumentation
	 */
	u_char n_ntargets;	/* number of known targets */
	u_char n_ncmds;		/* number of commands stored here at present */
	u_char n_npolling;	/* number of polling commands stored up */
	u_char n_ndisc;		/* number of disconnected cmds at present */
	u_long n_preempt;	/* number of times a select was preempted */
	u_long n_disconnects;	/* number of disconnects */
	u_long n_nlinked;	/* number of linked commands */
	u_long n_ints;		/* number of actual h/w interrupts */
	u_long n_spurint;	/* numver of spurious interrupts */
	u_long n_pmints[5];	/* number and kind of Phase Mismatch ints */

	/*
	 * Hardware pointers
	 */
	VOLATILE struct	ncrsbc	*n_sbc;	/* ptr to 5380 SBC registers */
	VOLATILE struct	ncrctl	*n_ctl;	/* ptr to control registers */
	VOLATILE struct	ncrdma	*n_dma;	/* ptr to dma registers */

	/*
	 * Linked list of structures.
	 * (used to be n_dmabase)
	 */

	struct	ncr	*n_nxt;	/* next in a linked list */

	/*
	 * Soft dma information
	 */
	u_long	n_lastdma;	/* last dma address */
	u_long	n_lastcount;	/* last dma count */
	u_long	n_lastbcr;	/* last snapshot of the byte count register */

	int	(*n_dma_cleanup)();	/* dma cleanup routine */

	/*
	 * If the slot is NULL, then the Host Adapter believes that
	 * that Target/Lun can accept a command.
	 *
	 * Addressing for using the _slot tags is fixed by (target<<3)|lun.
	 * The value ((short) -1) is reserved to undefine any _slot tag.
	 *
	 */
	short			n_last_slot;	/* last active target/lun */
	short			n_cur_slot;	/* current active target/lun */
	struct scsi_cmd		*n_slots[NTARGETS*NLUNS_PER_TARGET];

};

/*
 * NCR Host Adapter Types
 */

#define	IS_COBRA	0
#define	IS_SCSI3	1

#define	IS_VME(t)	((t) == IS_SCSI3)
#define	IS_ONBOARD(t)	((t) == IS_COBRA)

/*
 * Default Initiator ID
 */

#define	NCR_HOST_ID	7
#define	NUM_TO_BIT(x)	(1<<(x))

/*
 * DMA maxima
 */
#define	NCR_DMA_COBRA_MAX	(1<<20)
#define	NCR_DMA_MAX		(1<<24)

/*
 * Shorthand defines
 */

#define	N_SBC 		ncr->n_sbc
#define	N_CTL		ncr->n_ctl
#define	N_DMA		ncr->n_dma
#define	CBSR		(N_SBC->cbsr)
#define	CDR		(N_SBC->cdr)

#define	UPDATE_STATE(t)	ncr->n_laststate = ncr->n_state, ncr->n_state = (t);
#define	UPDATE_SLOT(t)	\
	ncr->n_last_slot = ncr->n_cur_slot,  ncr->n_cur_slot = (t);


#define	IN_DATA_STATE(ncr)	\
	((ncr)->n_state == ACTS_DATA_IN || (ncr)->n_state == ACTS_DATA_OUT)

/*
 * driver states (go into n_state && n_laststate)
 *	0x0		FREE
 */

#define	STATE_FREE		0x00		/* idle */


/*
 * These states cover being in one of the INFORMATION TRANSFER phases.
 * The states covered are Command, Message In, Message Out, Data In,
 * Data Out, and Status phases.
 */

#define	ACTS_ITPHASE_BASE	0x01		/* used internally */
#define	ACTS_UNKNOWN		0x01		/* unknown phase */
#define	ACTS_MSG_OUT		0x02
#define	ACTS_MSG_IN		0x03
#define	ACTS_STATUS		0x04
#define	ACTS_COMMAND		0x05
#define	ACTS_DATA_IN		0x06
#define	ACTS_DATA_OUT		0x07


/*
 * Completing states (of various kinds)
 */

#define	ACTS_CLEARING_DISC	0x10
#define	ACTS_CLEARING_DONE	0x11

/*
 * During reselection, the driver
 * passes thru this state briefly.
 */

#define	ACTS_RESELECTING	0x12

/*
 * This state is used by the driver to indicate to itself that it is
 * in the middle of aborting things.
 */

#define	ACTS_ABORTING		0x3e

/*
 * This state is used by the driver to indicate to itself that it is
 * in the middle of spanning a target driver completion call.
 */

#define	ACTS_SPANNING		0x3F

/*
 * Selecting States. These states represent a selection attempt
 * for a target.
 */

#define	STATE_STARTING		0x40
#define	STATE_ARBITRATION	0x41
#define	STATE_SELECTING		0x42
#define	STATE_SELECTED		0x43
#define	STATE_RESELECT		0x44
#define	STATE_RESET		0x4f

/*
 * The routine that attempts to select a
 *  target returns one of these values.
 */

#define	SEL_RESEL		-3
#define	SEL_ARBFAIL		-2
#define	SEL_FALSE		-1
#define	SEL_TRUE		0

/*
 * phase management actions
 */

#define	ACTION_RETURN		0	/* return from interrupt */
#define	ACTION_ABORT		1	/* abort current command */
#define	ACTION_FINISH		2	/* call completion for current cmd */
#define	ACTION_CONTINUE		3	/* continue in phase management */
#define	ACTION_SEARCH		4	/* disconnected- search for more work */

/*
 * Simple bus management functions either
 * return (0) for success or (-1) for failure.
 */

#define	FAILURE			-1
#define	SUCCESS			0

/*
 * The SCSA interface expects TRUE/FALSE returns like these
 */

#define	FALSE			0
#define	TRUE			1

/*
 * Phase Mismatch interrupt instrumentation
 */

#define	PM_SEL		0
#define	PM_MSGOUT	1
#define	PM_MSGIN	2
#define	PM_STATUS	3
#define	PM_CMD		4

/*
 * Reset actions
 */

#define	NCR_RESET_NCR		0x1
#define	NCR_RESET_DMA		0x2
#define	NCR_RESET_SCSIBUS	0x4
#define	NCR_RESET_ALL		(NCR_RESET_NCR|NCR_RESET_DMA|NCR_RESET_SCSIBUS)
#define	RESET_NOMSG		0x0
#define	RESET_MSG		0x1

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_NCRREG_H */
