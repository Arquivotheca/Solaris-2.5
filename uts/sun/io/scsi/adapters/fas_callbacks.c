/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma	ident  "@(#)fas_callbacks.c 1.6     95/08/18 SMI"

/*
 * ISSUES
 * - cb_info should not	be kmem_zalloc'ed
 */

#include <sys/scsi/scsi.h>
#include <sys/note.h>
#include <sys/scsi/adapters/fasreg.h>
#include <sys/scsi/adapters/fasvar.h>
#include <sys/scsi/adapters/fascmd.h>
#undef TRACE			/* XXX remove when tracing compiles */
#include <sys/vtrace.h>

#ifdef	FASDEBUG
extern int  fasdebug;
extern int  fasdebug_instance; /* debug	all instances */
#endif	/* FASDEBUG */

void fas_complete_arq_pkt(struct fas *fas, struct fas_cmd *sp, int slot);
void fas_call_pkt_comp(register	struct fas *fas,
    register struct fas_cmd *sp);
void fas_empty_callbackQ(register struct callback_info *cb_info);
int fas_init_callbacks(struct fas *fas);
void fas_printf(struct fas *fas,	const char *fmt, ...);

int
fas_init_callbacks(struct fas *fas)
{
	register struct	callback_info  *cb_info;
	char buf[64];

	ASSERT(fas->f_callback_info == NULL);

	cb_info	= (struct callback_info	*)kmem_zalloc(
	    sizeof (struct callback_info),  KM_SLEEP);
	fas->f_callback_info =	cb_info;

	sprintf(buf, "fas%d_callback_mutex", fas->f_instance);
	mutex_init(&cb_info->c_mutex, buf, MUTEX_DRIVER,
	    fas->f_iblock);

	return (0);
}

void
fas_empty_callbackQ(register struct callback_info *cb_info)
{
	register struct	fas_cmd	*sp;

	mutex_enter(&cb_info->c_mutex);
	/*
	 * don't recurse into calling back: the target driver
	 * may call scsi_transport() again which may call
	 * fas_empty_callbackQ again
	 */
	if (cb_info->c_in_callback) {
		goto done;
	}
	cb_info->c_in_callback = 1;

	while (cb_info->c_qf) {
		sp = cb_info->c_qf;
		cb_info->c_qf =	sp->cmd_forw;
		if (cb_info->c_qb == sp) {
			cb_info->c_qb =	NULL;
		}
		mutex_exit(&cb_info->c_mutex);
		(*sp->cmd_pkt->pkt_comp)(sp->cmd_pkt);
		mutex_enter(&cb_info->c_mutex);
	}
	cb_info->c_in_callback = 0;
done:
	mutex_exit(&cb_info->c_mutex);
}

/*
 * fas_call_pkt_comp does sanity checking to ensure that we don't
 * call	completion twice on the	same packet or a packet	that has been freed.
 * if there is a completion function specified,	the packet is queued
 * up and it is	left to	the fas_callback thread	to empty the queue at
 * a lower priority; note that there is	one callback queue per fas
 *
 * we use a separate thread for	calling	back into the target driver
 * this	thread unqueues	packets	from the callback queue
 */
void
fas_call_pkt_comp(register struct fas *fas, register struct fas_cmd *sp)
{
	TRACE_0(TR_FAC_SCSI, TR_FAS_CALL_PKT_COMP_START,
	    "fas_call_pkt_comp_start");

	ASSERT(sp != 0);
	ASSERT((sp->cmd_flags &	CFLAG_COMPLETED) == 0);
	ASSERT((sp->cmd_flags &	CFLAG_FREE) == 0);
	ASSERT(sp->cmd_flags & CFLAG_FINISHED);
	ASSERT(fas->f_ncmds >= fas->f_ndisc);
	ASSERT((sp->cmd_flags &	CFLAG_CMDDISC) == 0);
	ASSERT(sp != fas->f_current_sp);
	ASSERT(sp != fas->f_active[sp->cmd_slot]->f_slot[sp->cmd_tag[1]]);

	sp->cmd_flags &= ~CFLAG_IN_TRANSPORT;
	sp->cmd_flags |= CFLAG_COMPLETED;
	sp->cmd_qfull_retries = 0;

	/*
	 * if this was an auto request sense, complete immediately to free
	 * the arq pkt
	 */
	if (sp->cmd_pkt->pkt_comp && !(sp->cmd_flags & CFLAG_CMDARQ)) {
		register struct	callback_info *cb_info = fas->f_callback_info;

		if (sp->cmd_pkt->pkt_reason != CMD_CMPLT) {
			IPRINTF6("completion for %d.%d,	sp=%x, "
			    "reason=%s,	stats=%x, state=%x\n",
				Tgt(sp), Lun(sp), sp,
				scsi_rname(sp->cmd_pkt->pkt_reason),
				sp->cmd_pkt->pkt_statistics,
				sp->cmd_pkt->pkt_state);
		} else {
			EPRINTF2("completion queued for	%d.%dn",
				Tgt(sp), Lun(sp));
		}

		/*
		 * append the packet or	start a	new queue
		 */
		mutex_enter(&cb_info->c_mutex);
		if (cb_info->c_qf) {
			/*
			 * add to tail
			 */
			register struct	fas_cmd	*dp = cb_info->c_qb;
			ASSERT(dp != NULL);
			cb_info->c_qb =	sp;
			sp->cmd_forw = NULL;
			dp->cmd_forw = sp;
		} else {
			/*
			 * start new queue
			 */
			cb_info->c_qf =	cb_info->c_qb =	sp;
			sp->cmd_forw = NULL;
		}
		mutex_exit(&cb_info->c_mutex);

	} else if ((sp->cmd_flags & CFLAG_CMDARQ) && sp->cmd_pkt->pkt_comp) {
		/*
		 * pkt_comp may	be NULL	when we	are aborting/resetting but then
		 * the callback	will be	redone later
		 */
		fas_complete_arq_pkt(fas, sp, sp->cmd_slot);

	} else	{
		EPRINTF2("No completion	routine	for %x reason %x\n",
		    sp,	sp->cmd_pkt->pkt_reason);
	}
	TRACE_0(TR_FAC_SCSI, TR_FAS_CALL_PKT_COMP_END,
	    "fas_call_pkt_comp_end");
}
