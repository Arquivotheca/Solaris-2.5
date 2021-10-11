/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)strlog.c	1.17	95/08/17 SMI"	/* SVr4.0 1.14	*/

/*
 * Streams log interface routine.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strstat.h>
#include <sys/log.h>
#include <sys/inline.h>
#include <sys/strlog.h>
#include <sys/systm.h>
#include <sys/debug.h>
#include <sys/cred.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/syslog.h>
#include <sys/cmn_err.h>
#include <sys/msgbuf.h>
#include <sys/stat.h>
#include <sys/strsubr.h>

#include <sys/conf.h>
#include <sys/sunddi.h>

extern kmutex_t prf_lock;

void loginit(void);
int shouldtrace(short, short, char);
int sendmsg(struct log *lp, mblk_t *);
int log_internal(mblk_t *, int, int);

static int logtrace(struct log *, short mid, short sid, char);

/* now defined in space.c because log is loadable */
extern int numlogtrc;		/* number of processes reading trace log */
extern int numlogerr;		/* number of processes reading error log */
extern int numlogcons;		/* number of processes reading console log */
extern int log_errseq, log_trcseq, log_conseq;	/* logger sequence numbers */

static int loginit_flag;

/* now defined in space.c because log is loadable */
extern kmutex_t log_lock;

/*
 * Initialization function - called during system initialization to
 * initialize global variables and data structures.
 */
void
loginit(void)
{
	register int i;

	numlogtrc = 0;
	numlogerr = 0;
	numlogcons = 0;
	log_errseq = 0;
	log_trcseq = 0;
	log_conseq = 0;
	for (i = 0; i < log_cnt; i++)
		log_log[i].log_state = 0;
	mutex_init(&log_lock, "log lock", MUTEX_DEFAULT, NULL);
	loginit_flag = 1;
}

/*
 * Kernel logger interface function.  Attempts to construct a log
 * message and send it up the logger stream.  Delivery will not be
 * done if message blocks cannot be allocated or if the logger
 * is not registered (exception is console logger).
 *
 * Returns 0 is a message is not seen by a reader, either because
 * nobody was reading or an allocation failed.  Returns 1 otherwise.
 *
 * Remember, strlog cannot be called from this driver itself. It is
 * assumed that the code calling it is outside of the perimeter of this
 * driver.
 */

/* PRINTFLIKE5 */
int
strlog(
	short mid,
	short sid,
	char level,
	u_short flags,
	char *fmt,
	...
)
{
	register char *dst, *src;
	register int i;
	VA_LIST argp;
	struct log_ctl *lcp;
	int nlog;
	mblk_t *dbp, *cbp;

	ASSERT(flags & (SL_ERROR|SL_TRACE|SL_CONSOLE));

	/*
	 * return if log driver has not yet been initialized
	 */

	if (!loginit_flag)
		return (0);

	if (flags & SL_ERROR) {
		if (numlogerr == 0)
			flags &= ~SL_ERROR;
		else
			log_errseq++;
	}
	if (flags & SL_TRACE) {
		if ((numlogtrc == 0) || !shouldtrace(mid, sid, level))
			flags &= ~SL_TRACE;
		else
			log_trcseq++;
	}
	if (!(flags & (SL_ERROR|SL_TRACE|SL_CONSOLE))) {
		return (0);
	}

	/*
	 * allocate message blocks for log text, log header, and
	 * proto control field.
	 */
	if (!(dbp = allocb(LOGMSGSZ, BPRI_HI)))
		return (0);
	if (!(cbp = allocb(sizeof (struct log_ctl), BPRI_HI))) {
		freeb(dbp);
		return (0);
	}

	/*
	 * copy log text into text message block.  This consists of a
	 * format string and NLOGARGS integer arguments.
	 */
	dst = (char *)dbp->b_wptr;
	src = fmt;
	logstrcpy(dst, src);

	/*
	 * dst now points to the null byte at the end of the format string.
	 * Move the wptr to the first int boundary after dst.
	 */
	dbp->b_wptr = (unsigned char *)logadjust(dst);

	ASSERT((int)(dbp->b_datap->db_lim-dbp->b_wptr) >=
	    NLOGARGS * sizeof (int));

	VA_START(argp, fmt);

	for (i = 0; i < NLOGARGS; i++) {
		*((int *)dbp->b_wptr) = VA_ARG(argp, int);
		dbp->b_wptr += sizeof (int);
	}

	/*
	 * set up proto header
	 */
	cbp->b_datap->db_type = M_PROTO;
	cbp->b_cont = dbp;
	cbp->b_wptr += sizeof (struct log_ctl);
	lcp = (struct log_ctl *)cbp->b_rptr;
	lcp->mid = mid;
	lcp->sid = sid;
	(void) drv_getparm(LBOLT, (unsigned long *) &lcp->ltime);
	(void) drv_getparm(TIME, (unsigned long *) &lcp->ttime);
	lcp->level = level;
	lcp->flags = flags;

	nlog = 0;
	i = 0;
	if (lcp->flags & SL_TRACE) {
		nlog++;
		lcp->pri = LOG_KERN|LOG_DEBUG;
		i += log_internal(cbp, log_trcseq, LOGTRC);
	}
	if (lcp->flags & SL_ERROR) {
		nlog++;
		lcp->pri = LOG_KERN|LOG_ERR;
		i += log_internal(cbp, log_errseq, LOGERR);
	}
	if (lcp->flags & SL_CONSOLE) {
		nlog++;
		log_conseq++;
		if (lcp->flags & SL_FATAL)
			lcp->pri = LOG_KERN|LOG_CRIT;
		else if (lcp->flags & SL_ERROR)
			lcp->pri = LOG_KERN|LOG_ERR;
		else if (lcp->flags & SL_WARN)
			lcp->pri = LOG_KERN|LOG_WARNING;
		else if (lcp->flags & SL_NOTE)
			lcp->pri = LOG_KERN|LOG_NOTICE;
		else if (lcp->flags & SL_TRACE)
			lcp->pri = LOG_KERN|LOG_DEBUG;
		else
			lcp->pri = LOG_KERN|LOG_INFO;
		i += log_internal(cbp, log_conseq, LOGCONS);
	}
	freemsg(cbp);
	return ((i == nlog) ? 1 : 0);
}

/*
 * Check mid, sid, and level against list of values requested by
 * processes reading trace messages.
 */
static int
logtrace(
	struct log	*lp,
	register short	mid,
	register short	sid,
	register char	level
)
{
	register struct trace_ids *tid;
	register int i;
	int ntid;

	ASSERT(lp->log_tracemp);
	tid = (struct trace_ids *)lp->log_tracemp->b_rptr;
	ntid = (long)(lp->log_tracemp->b_wptr - lp->log_tracemp->b_rptr) /
	    sizeof (struct trace_ids);
	for (i = 0; i < ntid; tid++, i++) {
		if ((tid->ti_level < level) && (tid->ti_level >= 0))
			continue;
		if ((tid->ti_mid != mid) && (tid->ti_mid >= 0))
			continue;
		if ((tid->ti_sid != sid) && (tid->ti_sid >= 0))
			continue;
		return (1);
	}
	return (0);
}

/*
 * Returns 1 if someone wants to see the trace message for the
 * given module id, sub-id, and level.  Returns 0 otherwise.
 */
int
shouldtrace(
	register short mid,
	register short sid,
	register char level
)
{
	register struct log *lp;
	register int i;

	i = CLONEMIN + 1;
	mutex_enter(&log_lock);
	for (lp = &log_log[i]; i < log_cnt; i++, lp++)
		if ((lp->log_state & LOGTRC) && logtrace(lp, mid, sid, level)) {
			mutex_exit(&log_lock);
			return (1);
		}
	mutex_exit(&log_lock);
	return (0);
}

/*
 * Send a log message to a reader.  Returns 1 if the
 * message was sent and 0 otherwise.
 */
int
sendmsg(
	register struct log	*lp,
	mblk_t			*mp
)
{
	register mblk_t		*bp2, *mp2;

	ASSERT(mutex_owned(&log_lock));

	if (bp2 = copyb(mp)) {
		if (mp2 = dupb(mp->b_cont)) {
			bp2->b_cont = mp2;
			while (!canput(lp->log_rdq)) {
				/*
				 * Try to keep the q from getting too full.
				 * It is OK to drop messages in busy
				 * conditions. Get in the most recent message
				 * by trimming the q size down to below the
				 * low-water mark.
				 */
				freemsg(getq(lp->log_rdq));
			}
			(void) putq(lp->log_rdq, bp2);
			return (1);
		} else {
			freeb(bp2);
		}
	}
	return (0);
}

/*
 * This is a driver internal function to send messages to the appropriate
 * reader depending on the type of log.
 *
 * Log a trace/error/console message.  Returns 1 if everyone sees the message
 * and 0 otherwise.
 */
int
log_internal(
	register mblk_t		*mp,
	int			seq_no,
	int			type_flag	/* what type of trace */
)
{
	register int 		i;
	register struct log	*lp;
	mblk_t			*bp;
	struct log_ctl		*lcp;
	int			nlog = 0;
	int			didsee = 0;

	bp = mp;
	lcp = (struct log_ctl *)bp->b_rptr;

	lcp->seq_no = seq_no;
	i = CLONEMIN + 1;

	mutex_enter(&log_lock);
	for (lp = &log_log[i]; i < log_cnt; i++, lp++)
		if ((lp->log_state & type_flag) &&
		    (type_flag != LOGTRC || logtrace(lp, lcp->mid, lcp->sid,
							lcp->level))) {
			nlog++;
			didsee += sendmsg(lp, bp);
		}

	mutex_exit(&log_lock);

	if (! didsee && (type_flag & LOGCONS) &&
	    ((lcp->pri & LOG_FACMASK) == LOG_KERN))
		for (mp = mp->b_cont; mp; mp = mp->b_cont)
			msgbuf_puts((caddr_t)mp->b_rptr);

	return ((nlog == didsee) ? 1 : 0);
}
