/*
 * Copyright (c) 1989-1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)scsi_transport.c	1.15	95/03/17 SMI"

/*
 * Main Transport Routine for SCSA
 */
#include <sys/scsi/scsi.h>
#include <sys/thread.h>

#define	A_TO_TRAN(ap)	((ap)->a_hba_tran)
#define	P_TO_TRAN(pkt)	((pkt)->pkt_address.a_hba_tran)
#define	P_TO_ADDR(pkt)	(&((pkt)->pkt_address))

#ifdef DEBUG
#define	SCSI_POLL_STAT
#endif

#ifdef SCSI_POLL_STAT
int	scsi_poll_user;
int	scsi_poll_intr;
#endif

extern	kmutex_t	scsi_flag_nointr_mutex;
extern	kcondvar_t	scsi_flag_nointr_cv;

static void
scsi_flag_nointr_comp(struct scsi_pkt *pkt)
{
	mutex_enter(&scsi_flag_nointr_mutex);
	pkt->pkt_comp = NULL;
	/*
	 * We need cv_broadcast, because there can be more
	 * than one thread sleeping on the cv. We
	 * will wake all of them. The correct  one will
	 * continue and the rest will again go to sleep.
	 */
	cv_broadcast(&scsi_flag_nointr_cv);
	mutex_exit(&scsi_flag_nointr_mutex);
}

/*
 * A packet can have FLAG_NOINTR set because of target driver or
 * scsi_poll(). If FLAG_NOINTR is set and we are in user context,
 * we can avoid busy waiting in HBA by replacing the callback
 * function with our own function and resetting FLAG_NOINTR. We
 * can't do this in interrupt context because cv_wait will
 * sleep with CPU priority raised high and in case of some failure,
 * the CPU will be stuck in high priority.
 */
int
scsi_transport(struct scsi_pkt *pkt)
{
	register struct scsi_address	*ap = P_TO_ADDR(pkt);

	if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
		return (*A_TO_TRAN(ap)->tran_start)(ap, pkt);
	} else if ((curthread->t_flag & T_INTR_THREAD) || panicstr) {
#ifdef SCSI_POLL_STAT
		mutex_enter(&scsi_flag_nointr_mutex);
		scsi_poll_intr++;
		mutex_exit(&scsi_flag_nointr_mutex);
#endif
		return (*A_TO_TRAN(ap)->tran_start)(ap, pkt);
	} else {
		register	savef;
		void		(*savec)();
		int		status;

#ifdef SCSI_POLL_STAT
		mutex_enter(&scsi_flag_nointr_mutex);
		scsi_poll_user++;
		mutex_exit(&scsi_flag_nointr_mutex);
#endif
		savef = pkt->pkt_flags;
		savec = pkt->pkt_comp;

		pkt->pkt_comp = scsi_flag_nointr_comp;
		pkt->pkt_flags &= ~FLAG_NOINTR;
		pkt->pkt_flags |= FLAG_IMMEDIATE_CB;

		if ((status = (*A_TO_TRAN(ap)->tran_start)(ap, pkt)) ==
			TRAN_ACCEPT) {
			mutex_enter(&scsi_flag_nointr_mutex);
			while (pkt->pkt_comp != NULL) {
				cv_wait(&scsi_flag_nointr_cv,
					&scsi_flag_nointr_mutex);
			}
			mutex_exit(&scsi_flag_nointr_mutex);
		}

		pkt->pkt_flags = savef;
		pkt->pkt_comp = savec;
		return (status);
	}
}
