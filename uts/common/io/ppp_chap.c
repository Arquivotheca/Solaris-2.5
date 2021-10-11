#pragma ident	"@(#)ppp_chap.c 1.11	94/02/16 SMI"

/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

/*
 * Implements the CHAP authentication protocol as described in
 * RFC1334.
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/stropts.h>
#include <sys/ddi.h>
#include <sys/debug.h>
#include <sys/time.h>
#include <sys/syslog.h>
#include <sys/strlog.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/t_lock.h>
#include <sys/strsun.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>

#include <netinet/ip.h>
#include <netinet/tcp.h>

#ifdef ISERE_TREE
#include <ppp/vjcomp.h>
#include <ppp/ppp_ioctl.h>
#include <ppp/ppp_sys.h>
#include <ppp/ppp_pap.h>
#include <ppp/ppp_chap.h>
#include <ppp/ppp_extern.h>
#else
#include <sys/vjcomp.h>
#include <sys/ppp_ioctl.h>
#include <sys/ppp_sys.h>
#include <sys/ppp_pap.h>
#include <sys/ppp_chap.h>
#include <sys/ppp_extern.h>
#endif

static chapTuple_t chap_statetbl[CHAP_NEVENTS][CHAP_STATES] = {
/* Aboth */ { SRCIRC+C6, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR,
FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR},
/* Arem */ { SRCIRC+C4, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR,
FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR},
/* Aloc */ { C1, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR,
FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR},
/* Chall */ { FSM_ERR, SRRIRR+C2, SRRIRR+C2, SRRIRR+C2, FSM_ERR, FSM_ERR,
SRRIRR+C8, SRRIRR+C9, SRRIRR+C8, SRRIRR+C9,  SRRIRR+C8,	 SRRIRR+C9},
/* Succ */ { FSM_ERR, FSM_ERR, AAS+C3, FSM_ERR, FSM_ERR, FSM_ERR,
FSM_ERR, FSM_ERR, C10, AAS+C11, FSM_ERR, FSM_ERR},
/* Fail */ { FSM_ERR, FSM_ERR, LAF+C0, FSM_ERR, FSM_ERR, FSM_ERR,
FSM_ERR, FSM_ERR, LAF+C0, LAF+C0, FSM_ERR, FSM_ERR },
/* GoodR */ { FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, SRSAAS+C5, SRS+C5,
SRS+C7, SRS+C7, SRS+C9, SRS+C9, SRSAAS+C11, SRS+C11},
/* BadR */ { FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, SRFRAF+C0, SRFRAF+C0,
SRFRAF+C0, SRFRAF+C0, SRFRAF+C0, SRFRAF+C0,  SRFRAF+C0,	 SRFRAF+C0},
/* TOL+ */ { FSM_ERR, C1, SRR+C2, FSM_ERR, FSM_ERR, FSM_ERR, C6,
C7, SRR+C8, SRR+C9, FSM_ERR, FSM_ERR},
/* TOL- */ { FSM_ERR, LAF+C0, LAF+C2, FSM_ERR, FSM_ERR, FSM_ERR,
LAF+C0, LAF+C0, LAF+C0, LAF+C0, LAF+C0, FSM_ERR},
/* TOR+ */ { FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, SRC+C4, FSM_ERR,
SRC+C6, FSM_ERR, SRC+C8, FSM_ERR, SRC+C10, FSM_ERR},
/* TOR- */ { FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, RAF+C0, FSM_ERR,
RAF+C0, FSM_ERR, RAF+C0, FSM_ERR, RAF+C10, FSM_ERR},
/* Force */ { FSM_ERR, FSM_ERR, FSM_ERR, FSM_ERR, SRCIRC+C4, SRCIRC+C4,
SRCIRC+C6, SRCIRC+C6, SRCIRC+C8, SRCIRC+C8, SRCIRC+C8, SRCIRC+C10},
/* Close */ { C0, C0, C0, C0, C0, C0,
C0, C0, C0, C0, C0, C0 }
};

#define	no_chall_timer_state(new_state) ((new_state) == C0 || \
					(new_state) == C1 || \
					(new_state) == C2 || \
					(new_state) == C3 || \
					(new_state) == C5 || \
					(new_state) == C7 || \
					(new_state) == C9 || \
					(new_state) == C11)

#define	no_resp_timer_state(new_state) ((new_state) == C0 || \
					(new_state) == C3 || \
					(new_state) == C4 || \
					(new_state) == C5 || \
					(new_state) == C10 || \
					(new_state) == C11)

char *chapEvent_txt[] = {"AuthBoth", "AuthRem", "AuthLoc", "Chall", "Succ",
	"Fail", "GoodResp", "BadResp", "TogtResp", "ToeqResp", "TogtChall",
	"ToeqChall", "Force", "Close" };

char *chapAction_txt[] = {"Noaction", "Srcirc", "Srrirr", "Srsras",
	"Srs", "Srfraf", "Aas", "Laf", "Raf", "Srsaaf", "Srr", "Src"};

char *chapState_txt[] = {"C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7",
		"C8", "C9", "C10", "C11" };

static void aas(chapMachine_t *);
static void laf(chapMachine_t *);
static void raf(chapMachine_t *);
static void irr(chapMachine_t *);
static void irc(chapMachine_t *);
static void src(chapMachine_t *);
static void srr(chapMachine_t *);
static void srs(chapMachine_t *);
static void srf(chapMachine_t *);
static void chap_chall_timeout();
static void chap_resp_timeout();
static void cancel_chall_restart_timer();
static void cancel_resp_restart_timer();
static void calc_resp(u_int chall_id, struct chall_resp *,
	struct chall_resp *, chapMachine_t *, int *);
static void start_resp_restart_timer();
static void start_chall_restart_timer();
static void make_challenge_value(chapMachine_t *);
static int chap_md5verify(u_int, struct chall_resp *, chapMachine_t *);
static int chap_verify(u_int, struct chall_resp *, chapMachine_t *, u_int);
static void chap_md5calc_resp(u_int, struct chall_resp *,
	struct chall_resp *, chapMachine_t *);

extern int  ppp_debug;

/*
 * chap_action()
 *
 * Dispatches function calls for the fsm's actions
 */
void
chap_action(machp, action, new_state)
chapMachine_t *machp;
chapAction_t action;
chapState_t  new_state;
{
	PPP_STRDBG2("Doing action: %s, new state: %s\n",
		chapAction_txt[action], chapState_txt[new_state]);

	switch (action) {
	case Srcirc:
		src(machp);
		irc(machp);
		break;
	case Srrirr:
		srr(machp);
		irr(machp);
		break;
	case Srs:
		srs(machp);
		break;
	case Srsaas:
		srs(machp);
		aas(machp);
		break;
	case Srfraf:
		srf(machp);
		raf(machp);
		break;
	case Aas:
		aas(machp);
		break;
	case Laf:
		laf(machp);
		break;
	case Raf:
		raf(machp);
		break;
	case Srr:
		srr(machp);
		break;
	case Src:
		src(machp);
		break;
	default:
		break;
	}
	machp -> state = new_state;
}

/*
 * chap_fsm()
 *
 * Processes an event in the fsm
 */
void
chap_fsm(machp, event)
chapMachine_t *machp;
chapEvent_t event;
{
	chapTuple_t tuple;
	chapState_t new_state;
	chapAction_t func;

	ASSERT(event < CHAP_NEVENTS);
	ASSERT(machp -> state < CHAP_STATES);
	PPP_STRDBG("Event: %s\n", chapEvent_txt[event]);
	tuple = chap_statetbl[event][machp -> state];
	if (tuple == FSM_ERR) {
		return;
	}
	new_state = tuple & 0xff;
	func = (chapAction_t) tuple >> 8;
	chap_action(machp, func, new_state);
	if (no_chall_timer_state(new_state)) {
		cancel_chall_restart_timer(machp);
	}
	if (no_resp_timer_state(new_state)) {
		cancel_resp_restart_timer(machp);
	}
}

/*
 * do_incoming_chap()
 *
 * Handles a chap packet from the remote hosts
 */
void
do_incoming_chap(machp, mp)
chapMachine_t		*machp;
mblk_t			*mp;
{
	mblk_t			*zmp, *rp;
	struct ppp_hdr		*hp, *resp_hdr;
	u_short			chlength;
	chapEvent_t		event;
	struct chall_resp	*resp, *chall;
	u_char			replength, repid, chid;
	int			len;

	if (!ISPULLEDUP(mp)) {
		zmp = msgpullup(mp, -1);
		freemsg(mp);
		mp = zmp;
		if (mp == NULL) {
			return;
		}
	}

	hp = (struct ppp_hdr *) mp -> b_rptr;

	switch (hp -> pkt.code) {

	case Success:
		if (hp-> pkt.id != machp-> respid) {
			goto discard;
		}
		event = Succ;
		break;
	case Challenge:
		if (machp -> response)
			freemsg(machp -> response);
		rp = machp -> response =
		    ppp_alloc_frame(pppCHAP, Response, hp-> pkt.id);
		if (rp == NULL) {
			freemsg(mp);
			return;
		}
		machp -> respid = chid = hp-> pkt.id;
		resp_hdr = (struct ppp_hdr *) rp -> b_rptr;
		resp = (struct chall_resp *) rp -> b_wptr;

		chlength = ntohs(hp -> pkt.length);
		adjmsg(mp, PPP_HDRSZ);
		chlength -= PPP_HDRSZ;

		chall = (struct chall_resp *) mp -> b_rptr;

		calc_resp(chid, chall, resp, machp, &len);

		resp_hdr -> pkt.length =
		    htons(ntohs(resp_hdr -> pkt.length) + len);

		rp -> b_wptr += len;
		event = Chall;
		break;

	case Response:
		replength = ntohs(hp -> pkt.length);
		repid = hp -> pkt.id;
		if (repid != machp -> crid) {
			goto discard;
		}
		adjmsg(mp, PPP_HDRSZ);
		replength -= PPP_PKT_HDRSZ;

		resp = (struct chall_resp *) mp -> b_rptr;
		if (chap_verify(repid, resp, machp, replength)) {
			rp = machp -> result =
				ppp_alloc_frame(pppCHAP, Success, repid);
			if (rp == NULL) {
				freemsg(mp);
				return;
			}
			event = GoodResp;
		} else {
			rp = machp -> result =
				ppp_alloc_frame(pppCHAP, Failure, repid);
			if (rp == NULL) {
				freemsg(mp);
				return;
			}
			event = BadResp;
		}
		break;

	case Failure:
		if (hp->pkt.id != machp-> respid) {
			goto discard;
		}
		event = Fail;
		break;
	}
	chap_fsm(machp, event);
discard:
	freemsg(mp);
}

/*
 * check_param()
 *
 * Checks the parameter for an ioctl to the chap layer
 */
static void *
check_param(mp, size)
mblk_t			*mp;
int			size;
{
	register struct iocblk	*iocp;

	ASSERT(size > 0);

/*
 * check parameter size
 */
	iocp = (struct iocblk *) mp->b_rptr;
	if (iocp->ioc_count != size) {
		return (NULL);
	}

	return ((void *) mp->b_cont->b_rptr);
}

/*
 * chap_external_event()
 *
 * Handles events directed to chap from other layers or lm
 */
void
chap_external_event(lp, exevent)
pppLink_t		*lp;
u_int		exevent;
{
	chapMachine_t	*machp;
	chapEvent_t	event;
	switch (exevent) {

	case PPPIN_AUTH_LOC:
		event = AuthLoc;
		break;
	case PPPIN_AUTH_REM:
		event = AuthRem;
		break;
	case PPPIN_AUTH_BOTH:
		event = AuthBoth;
		break;
	case PPPIN_FORCE_REM:
		event = Force;
		break;
	case PPPIN_TIMEOUT1:
		machp = lp -> chap;
		if (machp -> chall_timedoutid != machp -> chall_restart)
			return;
		machp -> chall_restart = 0;
		if (machp -> chall_restart_counter > 0) {
			start_chall_restart_timer(machp);
			event = TogtChall;
		} else {
			event = ToeqChall;
		}
		break;
	case PPPIN_TIMEOUT2:
		machp = lp -> chap;
		if (machp -> resp_timedoutid != machp -> resp_restart)
			return;
		machp -> resp_restart = 0;
		if (machp -> resp_restart_counter > 0) {
			start_resp_restart_timer(machp);
			event = TogtChall;
		} else {
			event = ToeqChall;
		}
		break;
	case PPPIN_CLOSE:
		event = ChapClose;
		break;
	default:
		return;
	}
	mutex_enter(&lp-> lplock);
	chap_fsm(lp -> chap, event);
	mutex_exit(&lp-> lplock);
}


/*
 * chap_ioctl()
 *
 * Processes ioctls to the chap layer
 */
int
chap_ioctl(machp, command, mp)
chapMachine_t	*machp;
int		command;
register mblk_t			*mp;
{
	chapPasswdEntry_t *passwd;

	switch (command) {
	case PPP_SET_LOCAL_PASSWD:
		PPP_FSMDBG0("Setting CHAP local passwd\n");
		passwd = (chapPasswdEntry_t *)
		    check_param(mp, sizeof (*passwd));
		if (passwd -> chapPasswdLen < CHAP_MAX_PASSWD &&
		    passwd -> chapNameLen < CHAP_MAX_NAME) {
			bcopy((caddr_t) passwd -> chapPasswd,
			    (caddr_t) machp -> local_secret,
			    passwd -> chapPasswdLen);
			machp -> local_secret_size = passwd -> chapPasswdLen;
			bcopy((caddr_t) passwd -> chapName,
			    (caddr_t) machp -> local_name,
			    passwd -> chapNameLen);
			machp -> local_name_size = passwd -> chapNameLen;
		} else {
			return (-1);
		}
		break;
	case PPP_SET_REMOTE_PASSWD:
		PPP_FSMDBG0("Setting CHAP remote passwd\n");
		passwd = (chapPasswdEntry_t *)
		    check_param(mp, sizeof (*passwd));
		if (passwd -> chapPasswdLen < CHAP_MAX_PASSWD &&
		    passwd -> chapNameLen < CHAP_MAX_NAME) {
			bcopy((caddr_t) passwd -> chapPasswd,
			    (caddr_t) machp -> remote_secret,
			    passwd -> chapPasswdLen);
			machp -> remote_secret_size = passwd -> chapPasswdLen;
			bcopy((caddr_t) passwd -> chapName,
			    (caddr_t) machp -> remote_name,
			    passwd -> chapNameLen);
			machp -> remote_name_size = passwd -> chapNameLen;
		} else {
			return (-1);
		}
		break;

	default:
		return (-1);
	}

	return (0);
}

/*
 * src()
 *
 * Send remote a challenge packet
 */
static void
src(machp)
chapMachine_t		*machp;
{
	mblk_t		*fp;
	struct ppp_hdr	*hdr;
	struct chall_resp *chall;


	if (machp->chall) {
		hdr = (struct ppp_hdr *) machp -> chall->b_rptr;
		hdr->pkt.id = ++machp->crid;
		fp = machp->chall;
	} else {
		fp = ppp_alloc_frame(pppCHAP, Challenge, ++machp -> crid);
		if (fp == NULL) {
			return;
		}

		hdr = (struct ppp_hdr *) fp -> b_rptr;

		chall = (struct chall_resp *) fp -> b_wptr;

		machp -> chall_size = 10;
		chall -> value_size = (u_char) machp -> chall_size;

		make_challenge_value(machp);

		bcopy((caddr_t) machp -> chall_value, (caddr_t) chall -> value,
			machp -> chall_size);

		fp -> b_wptr += machp -> chall_size + 1;

		bcopy((caddr_t) machp -> local_name, (caddr_t) fp->b_wptr,
		    machp -> local_name_size);

		fp->b_wptr += machp -> local_name_size;

		hdr -> pkt.length =
		    htons(ntohs(hdr -> pkt.length) + 1 + machp -> chall_size +
		    machp -> local_name_size);
		machp -> chall = fp;

	}
	machp->chall = copymsg(fp);

	ppp_putnext(WR(machp->readq), fp);
	machp->chall_restart_counter--;
}

/*
 * srr()
 *
 * Send remote a response packet in response to a challenge
 */
static void
srr(machp)
chapMachine_t		*machp;
{
	mblk_t		*fp;

	ASSERT(machp -> response);
	fp = copymsg(machp -> response);

	ppp_putnext(WR(machp -> readq), fp);
	machp -> resp_restart_counter--;
}

/*
 * srs()
 *
 * Send remote a success packet.
 */
static void
srs(machp)
chapMachine_t		*machp;
{
	ASSERT(machp -> result);
	ppp_putnext(WR(machp -> readq), machp -> result);
	machp -> result = NULL;
}

/*
 *
 * srf()
 *
 * Send remote a failure packet.
 */
static void
srf(machp)
chapMachine_t		*machp;
{
	ASSERT(machp -> result);
	ppp_putnext(WR(machp -> readq), machp -> result);
	machp -> result = NULL;
}

/*
 * aas()
 *
 * Report that authentication has succeeded.
 */
static void
aas(machp)
chapMachine_t		*machp;
{
	pppLink_t *lp;

	lp = machp -> linkp;

	ppp_internal_event(lp, PPP_AUTH_SUCCESS, pppCHAP);
}


/*
 * laf()
 *
 * Report that local authentication has failed.
 */
static void
laf(machp)
chapMachine_t		*machp;
{
	pppLink_t *lp;

	lp = machp -> linkp;

	ppp_error_ind(lp, pppLocalAuthFailed, NULL, (u_int) 0);
	ppp_internal_event(lp, PPP_LOCAL_FAILURE, pppCHAP);
}


/*
 * raf()
 *
 * Report that remote authenitication has failed.
 */
static void
raf(machp)
chapMachine_t		*machp;
{
	pppLink_t *lp;

	lp = machp -> linkp;

	ppp_error_ind(lp, pppRemoteAuthFailed, NULL, (u_int) 0);
	ppp_internal_event(lp, PPP_REMOTE_FAILURE, pppCHAP);
}

/*
 * irc()
 *
 * Restart the challenge resend timer.
 */
static void
irc(machp)
chapMachine_t	 *machp;
{
	cancel_chall_restart_timer(machp);
	machp -> chall_restart_counter = machp -> chapMaxRestarts;
	machp -> chall_restart = timeout(chap_chall_timeout, (caddr_t) machp,
		(machp -> chapRestartTimerValue*hz)/1000);
}

/*
 * calc_resp()
 *
 * Calculate the response to a particular challenge value.
 */
static void
calc_resp(chall_id, chall, resp, machp, len)
u_int chall_id;
struct chall_resp *chall, *resp;
chapMachine_t	  *machp;
int			*len;
{
	u_char *name_ptr;

	resp -> value_size = MAX_CHALL_SIZE;
	chap_md5calc_resp(chall_id, chall, resp, machp);
	name_ptr = resp -> value + resp -> value_size;
	bcopy((caddr_t) machp -> local_name, (caddr_t) name_ptr,
	    machp -> local_name_size);
	*len = resp -> value_size + machp -> local_name_size + 1;
}

/*
 * irr()
 *
 * Start the response resend timer.
 */
static void
irr(machp)
chapMachine_t	 *machp;
{
	cancel_resp_restart_timer(machp);
	machp -> resp_restart_counter = machp -> chapMaxRestarts;
	machp -> resp_restart = timeout(chap_resp_timeout, (caddr_t) machp,
		(machp -> chapRestartTimerValue*hz)/1000);
}


/*
 * cancel_chall_restart_timer()
 *
 * Cancel outstanding challenge restart timer
 * (no problem if timer is not running)
 */
static void
cancel_chall_restart_timer(machp)
chapMachine_t	 *machp;
{
	if (machp->chall_restart == 0) {
		return;
	}

	(void) untimeout(machp->chall_restart);
	machp->chall_restart = 0;
}


/*
 * cancel_resp_restart_timer()
 *
 * Cancel outstanding response restart timer
 * (no problem if timer is not running)
 */
static void
cancel_resp_restart_timer(machp)
chapMachine_t	 *machp;
{
	if (machp->resp_restart == 0) {
		return;
	}

	(void) untimeout(machp->resp_restart);
	machp->resp_restart = 0;
}


/*
 * start_chall_restart_timer()
 *
 * Start the challenge resend timer.
 */
static void
start_chall_restart_timer(machp)
chapMachine_t	 *machp;
{
	machp->chall_restart = timeout(chap_chall_timeout, (caddr_t) machp,
			(machp->chapRestartTimerValue*hz)/1000);
}


/*
 * start_resp_restart_timer()
 *
 * Start the response resend timer.
 */
static void
start_resp_restart_timer(machp)
chapMachine_t	 *machp;
{
	machp->resp_restart = timeout(chap_resp_timeout, (caddr_t) machp,
			(machp->chapRestartTimerValue*hz)/1000);
}

/*
 * chap_chall_timeout()
 *
 * Generate a timeout event in response to the challenge resend timer
 * expired
 */
static void
chap_chall_timeout(machp)
chapMachine_t	 *machp;
{
	ASSERT(machp);

	machp -> chall_timedoutid = machp -> chall_restart;

	ppp_cross_fsm(machp -> linkp, PPPIN_TIMEOUT1, pppCHAP, CROSS_LO);
}

/*
 * chap_resp_timeout()
 *
 * Generate a timeout event in response to the response resend timer
 * expired
 */
static void
chap_resp_timeout(machp)
chapMachine_t	 *machp;
{
	ASSERT(machp);

	machp -> resp_timedoutid = machp -> resp_restart;

	ppp_cross_fsm(machp -> linkp, PPPIN_TIMEOUT2, pppCHAP, CROSS_LO);
}

/*
 * alloc_chap_machine()
 *
 * Allocate a chap machine structure
 *
 * Returns alloced structure on success, NULL on failure
 */
chapMachine_t *
alloc_chap_machine(readq, linkp)
queue_t		*readq;
pppLink_t	*linkp;

{
	register chapMachine_t *machp;

	machp = (chapMachine_t *)
	    kmem_zalloc(sizeof (chapMachine_t), KM_NOSLEEP);

	if (machp == NULL) {
		return (NULL);
	}

	machp->readq = readq;

	machp -> state = C0;

	machp -> chapMaxRestarts = CHAP_DEF_MAXRESTART;
	machp -> chapRestartTimerValue = CHAP_DEF_RESTIMER;
	machp -> chall_size = 0;

	machp -> linkp = linkp;

	return (machp);
}

/*
 * free_chap_machine()
 *
 * Free the allocated memory of a chap machine structure.
 */
void
free_chap_machine(machp)
chapMachine_t *machp;
{
	cancel_chall_restart_timer(machp);
	cancel_resp_restart_timer(machp);
	if (machp -> response)
		freemsg(machp -> response);
	if (machp -> result)
		freemsg(machp -> result);
	if (machp -> chall)
		freemsg(machp -> chall);
	(void) kmem_free(machp, sizeof (chapMachine_t));
}

/*
 * make_challenge_value()
 *
 * Create a challenge value to send to the remote host.
 */
static void
make_challenge_value(machp)
	chapMachine_t *machp;
{
	u_long tempval;
	u_char *tempptr;
	int i;

	for (i = 0; i < machp -> chall_size; i++) {
		if (i % sizeof (tempval) == 0) {
			tempval = ppp_rand();
			tempptr = (u_char *) &tempval;
		}
		machp -> chall_value[i] = *tempptr++;
	}
}

/*
 *
 * Routines for doing MD5 algorithm with chap
 *
 *
 */

static void md5get_digest();

/*
 * chap_verify()
 *
 * Verify the authentication of a response
 *
 * Returns 0 for bad response, 1 for good response
 */
static int
chap_verify(repid, resp, machp, data_len)
u_int repid;
struct chall_resp *resp;
chapMachine_t	  *machp;
u_int data_len;
{
	int	name_len;
	u_char	*name_ptr;

	if (resp -> value_size > data_len)
		return (0);

	name_len = data_len - resp -> value_size -1;
	name_ptr = resp -> value + resp -> value_size;

	if (name_len != machp -> remote_name_size)
			return (0);
	if (bcmp((caddr_t) name_ptr, (caddr_t) machp -> remote_name,
	    machp -> remote_name_size) != 0)
		return (0);

	return (chap_md5verify(repid, resp, machp));
}

/*
 * chap_md5verify()
 *
 * Verify the authentication of a response using the md5 algorithm
 *
 * Returns 0 for bad response, 1 for good response
 */
static int
chap_md5verify(repid, resp, machp)
u_int repid;
struct chall_resp *resp;
chapMachine_t	  *machp;
{
	u_char digest[DIGEST_SIZE];

	if (resp -> value_size != DIGEST_SIZE) {
		return (0);
	}

	md5get_digest(digest, repid, machp -> remote_secret,
		(int) machp -> remote_secret_size, machp -> chall_value,
		(int) machp -> chall_size);

	if (bcmp((caddr_t) digest,
	    (caddr_t) resp -> value, DIGEST_SIZE) != 0) {
		return (0);
	}
	return (1);
}

/*
 * chap_md5calc_resp()
 *
 * Calculate the response to a challenge using md5
 */
static void
chap_md5calc_resp(chall_id, chall, resp, machp)
u_int chall_id;
struct chall_resp *chall, *resp;
chapMachine_t	  *machp;
{

	md5get_digest(resp -> value, chall_id, machp -> local_secret,
		(int) machp -> local_secret_size, chall -> value,
		(int) chall -> value_size);
	resp -> value_size = (int) DIGEST_SIZE;
}

/*
 * md5get_digest()
 *
 * Compute md5 digest.
 */
static void
md5get_digest(digest, serid, secret, secret_len, chall, chall_len)
u_char *digest;
u_char serid;
u_char *secret;
int secret_len;
u_char *chall;
int chall_len;
{
	MD5_CTX context;
	u_char cval[1 + MAX_CHALL_SIZE + CHAP_MAX_PASSWD];
	u_char *tptr;
	int cval_len;

	tptr = cval;

	*(tptr++) = serid;

	bcopy((caddr_t) secret, (caddr_t) tptr, secret_len);
	tptr += secret_len;

	bcopy((caddr_t) chall, (caddr_t) tptr, chall_len);
	tptr += chall_len;
	tptr += chall_len;

	cval_len = 1 + secret_len + chall_len;

	MD5Init(&context);
	MD5Update(&context, cval, cval_len);
	MD5Final(digest, &context);
}


/*
 * MD5C.C - RSA Data Security, Inc., MD5 message-digest algorithm
 */

/*
 * Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
rights reserved.

License to copy and use this software is granted provided that it
is identified as the "RSA Data Security, Inc. MD5 Message-Digest
Algorithm" in all material mentioning or referencing this software
or this function.

License is also granted to make and use derivative works provided
that such works are identified as "derived from the RSA Data
Security, Inc. MD5 Message-Digest Algorithm" in all material
mentioning or referencing the derived work.

RSA Data Security, Inc. makes no representations concerning either
the merchantability of this software or the suitability of this
software for any particular purpose. It is provided "as is"
without express or implied warranty of any kind.

These notices must be retained in any copies of any part of this
documentation and/or software.
 */

/*
 * Constants for MD5Transform routine.
 */

#define	S11 7
#define	S12 12
#define	S13 17
#define	S14 22
#define	S21 5
#define	S22 9
#define	S23 14
#define	S24 20
#define	S31 4
#define	S32 11
#define	S33 16
#define	S34 23
#define	S41 6
#define	S42 10
#define	S43 15
#define	S44 21

#ifdef __STDC__
static void MD5Transform(MDUINT4 [4], unsigned char [64]);
static void Encode(unsigned char *, MDUINT4 *, unsigned int);
static void Decode(MDUINT4 *, unsigned char *, unsigned int);
static void MD5_memcpy(MDPOINTER, MDPOINTER, unsigned int);
static void MD5_memset(MDPOINTER, int, unsigned int);
#else /* __STDC__ */
static void MD5Transform(/* MDUINT4 [4], unsigned char [64]) */);
static void Encode(/* unsigned char *, MDUINT4 *, unsigned int */);
static void Decode(/* MDUINT4 *, unsigned char *, unsigned int */);
static void MD5_memcpy(/* MDPOINTER, MDPOINTER, unsigned int */);
static void MD5_memset(/* MDPOINTER, int, unsigned int */);
#endif /* __STDC__ */

static unsigned char PADDING[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*
 * F, G, H and I are basic MD5 functions.
	*/
#define	F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define	G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define	H(x, y, z) ((x) ^ (y) ^ (z))
#define	I(x, y, z) ((y) ^ ((x) | (~z)))

/*
 * ROTATE_LEFT rotates x left n bits.
	*/
#define	ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

/*
 * FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
Rotation is separate from addition to prevent recomputation.
	*/
#define	FF(a, b, c, d, x, s, ac) { \
	(a) += F((b), (c), (d)) + (x) + (MDUINT4)(ac); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
	}
#define	GG(a, b, c, d, x, s, ac) { \
	(a) += G((b), (c), (d)) + (x) + (MDUINT4)(ac); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
	}
#define	HH(a, b, c, d, x, s, ac) { \
	(a) += H((b), (c), (d)) + (x) + (MDUINT4)(ac); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
	}
#define	II(a, b, c, d, x, s, ac) { \
	(a) += I((b), (c), (d)) + (x) + (MDUINT4)(ac); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
	}

/*
 * MD5 initialization. Begins an MD5 operation, writing a new context.
 */

void
MD5Init(context)
MD5_CTX *context;					 /* context */
{
	context->count[0] = context->count[1] = 0;
/*
 * Load magic initialization constants.
 */
	context->state[0] = (u_long) 0x67452301;
	context->state[1] = (u_long) 0xefcdab89;
	context->state[2] = (u_long) 0x98badcfe;
	context->state[3] = (u_long) 0x10325476;
}

/*
 * MD5 block update operation. Continues an MD5 message-digest
 * operation, processing another message block, and updating the
 * context.
 */

void
MD5Update(context, input, inputLen)
MD5_CTX *context;	/* context */
unsigned char *input;	/* input block */
unsigned int inputLen;	/* length of input block */
{
	unsigned int i, index, partLen;

	/* Compute number of bytes mod 64 */
	index = (unsigned int)((context->count[0] >> 3) & 0x3F);

	/* Update number of bits */
	if ((context->count[0] += ((MDUINT4)inputLen << 3))
	    < ((MDUINT4)inputLen << 3))
		context->count[1]++;
	context->count[1] += ((MDUINT4)inputLen >> 29);

	partLen = 64 - index;

/*
 * Transform as many times as possible.
 */
	if (inputLen >= partLen) {
		MD5_memcpy((MDPOINTER)&context->buffer[index],
		    (MDPOINTER)input, partLen);
		MD5Transform(context->state, context->buffer);

		for (i = partLen; i + 63 < inputLen; i += 64)
			MD5Transform(context->state, &input[i]);

		index = 0;
	}
	else
		i = 0;

	/* Buffer remaining input */
	MD5_memcpy((MDPOINTER)&context->buffer[index],
	    (MDPOINTER)&input[i], inputLen-i);
}

/*
 * MD5 finalization. Ends an MD5 message-digest operation, writing the
 * the message digest and zeroizing the context.
 */
void
MD5Final(digest, context)
unsigned char digest[16];			  /* message digest */
MD5_CTX *context;					/* context */
{
	unsigned char bits[8];
	unsigned int index, padLen;

	/* Save number of bits */
	Encode(bits, context->count, 8);

/*
 * Pad out to 56 mod 64.
 */
	index = (unsigned int)((context->count[0] >> 3) & 0x3f);
	padLen = (index < 56) ? (56 - index) : (120 - index);
	MD5Update(context, PADDING, padLen);

	/* Append length (before padding) */
	MD5Update(context, bits, 8);

	/* Store state in digest */
	Encode(digest, context->state, 16);

/*
 * Zeroize sensitive information.
 */
	MD5_memset((MDPOINTER)context, 0, sizeof (*context));
}

/*
 * MD5 basic transformation. Transforms state based on block.
 */
static void MD5Transform(state, block)
MDUINT4 state[4];
unsigned char block[64];
{
	MDUINT4 a = state[0], b = state[1], c = state[2], d = state[3], x[16];

	Decode(x, block, 64);

	/* Round 1 */
	FF(a, b, c, d, x[ 0], S11, 0xd76aa478); /* 1 */
	FF(d, a, b, c, x[ 1], S12, 0xe8c7b756); /* 2 */
	FF(c, d, a, b, x[ 2], S13, 0x242070db); /* 3 */
	FF(b, c, d, a, x[ 3], S14, 0xc1bdceee); /* 4 */
	FF(a, b, c, d, x[ 4], S11, 0xf57c0faf); /* 5 */
	FF(d, a, b, c, x[ 5], S12, 0x4787c62a); /* 6 */
	FF(c, d, a, b, x[ 6], S13, 0xa8304613); /* 7 */
	FF(b, c, d, a, x[ 7], S14, 0xfd469501); /* 8 */
	FF(a, b, c, d, x[ 8], S11, 0x698098d8); /* 9 */
	FF(d, a, b, c, x[ 9], S12, 0x8b44f7af); /* 10 */
	FF(c, d, a, b, x[10], S13, 0xffff5bb1); /* 11 */
	FF(b, c, d, a, x[11], S14, 0x895cd7be); /* 12 */
	FF(a, b, c, d, x[12], S11, 0x6b901122); /* 13 */
	FF(d, a, b, c, x[13], S12, 0xfd987193); /* 14 */
	FF(c, d, a, b, x[14], S13, 0xa679438e); /* 15 */
	FF(b, c, d, a, x[15], S14, 0x49b40821); /* 16 */

	/* Round 2 */
	GG(a, b, c, d, x[ 1], S21, 0xf61e2562); /* 17 */
	GG(d, a, b, c, x[ 6], S22, 0xc040b340); /* 18 */
	GG(c, d, a, b, x[11], S23, 0x265e5a51); /* 19 */
	GG(b, c, d, a, x[ 0], S24, 0xe9b6c7aa); /* 20 */
	GG(a, b, c, d, x[ 5], S21, 0xd62f105d); /* 21 */
	GG(d, a, b, c, x[10], S22,  0x2441453); /* 22 */
	GG(c, d, a, b, x[15], S23, 0xd8a1e681); /* 23 */
	GG(b, c, d, a, x[ 4], S24, 0xe7d3fbc8); /* 24 */
	GG(a, b, c, d, x[ 9], S21, 0x21e1cde6); /* 25 */
	GG(d, a, b, c, x[14], S22, 0xc33707d6); /* 26 */
	GG(c, d, a, b, x[ 3], S23, 0xf4d50d87); /* 27 */
	GG(b, c, d, a, x[ 8], S24, 0x455a14ed); /* 28 */
	GG(a, b, c, d, x[13], S21, 0xa9e3e905); /* 29 */
	GG(d, a, b, c, x[ 2], S22, 0xfcefa3f8); /* 30 */
	GG(c, d, a, b, x[ 7], S23, 0x676f02d9); /* 31 */
	GG(b, c, d, a, x[12], S24, 0x8d2a4c8a); /* 32 */

	/* Round 3 */
	HH(a, b, c, d, x[ 5], S31, 0xfffa3942); /* 33 */
	HH(d, a, b, c, x[ 8], S32, 0x8771f681); /* 34 */
	HH(c, d, a, b, x[11], S33, 0x6d9d6122); /* 35 */
	HH(b, c, d, a, x[14], S34, 0xfde5380c); /* 36 */
	HH(a, b, c, d, x[ 1], S31, 0xa4beea44); /* 37 */
	HH(d, a, b, c, x[ 4], S32, 0x4bdecfa9); /* 38 */
	HH(c, d, a, b, x[ 7], S33, 0xf6bb4b60); /* 39 */
	HH(b, c, d, a, x[10], S34, 0xbebfbc70); /* 40 */
	HH(a, b, c, d, x[13], S31, 0x289b7ec6); /* 41 */
	HH(d, a, b, c, x[ 0], S32, 0xeaa127fa); /* 42 */
	HH(c, d, a, b, x[ 3], S33, 0xd4ef3085); /* 43 */
	HH(b, c, d, a, x[ 6], S34,  0x4881d05); /* 44 */
	HH(a, b, c, d, x[ 9], S31, 0xd9d4d039); /* 45 */
	HH(d, a, b, c, x[12], S32, 0xe6db99e5); /* 46 */
	HH(c, d, a, b, x[15], S33, 0x1fa27cf8); /* 47 */
	HH(b, c, d, a, x[ 2], S34, 0xc4ac5665); /* 48 */

	/* Round 4 */
	II(a, b, c, d, x[ 0], S41, 0xf4292244); /* 49 */
	II(d, a, b, c, x[ 7], S42, 0x432aff97); /* 50 */
	II(c, d, a, b, x[14], S43, 0xab9423a7); /* 51 */
	II(b, c, d, a, x[ 5], S44, 0xfc93a039); /* 52 */
	II(a, b, c, d, x[12], S41, 0x655b59c3); /* 53 */
	II(d, a, b, c, x[ 3], S42, 0x8f0ccc92); /* 54 */
	II(c, d, a, b, x[10], S43, 0xffeff47d); /* 55 */
	II(b, c, d, a, x[ 1], S44, 0x85845dd1); /* 56 */
	II(a, b, c, d, x[ 8], S41, 0x6fa87e4f); /* 57 */
	II(d, a, b, c, x[15], S42, 0xfe2ce6e0); /* 58 */
	II(c, d, a, b, x[ 6], S43, 0xa3014314); /* 59 */
	II(b, c, d, a, x[13], S44, 0x4e0811a1); /* 60 */
	II(a, b, c, d, x[ 4], S41, 0xf7537e82); /* 61 */
	II(d, a, b, c, x[11], S42, 0xbd3af235); /* 62 */
	II(c, d, a, b, x[ 2], S43, 0x2ad7d2bb); /* 63 */
	II(b, c, d, a, x[ 9], S44, 0xeb86d391); /* 64 */

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;

/*
 * Zeroize sensitive information.
 */
	MD5_memset((MDPOINTER)x, 0, sizeof (x));
}

/*
 * Encodes input (MDUINT4) into output (unsigned char). Assumes len is
 * a multiple of 4.
 */
static void Encode(output, input, len)
unsigned char *output;
MDUINT4 *input;
unsigned int len;
{
	unsigned int i, j;

	for (i = 0, j = 0; j < len; i++, j += 4) {
		output[j] = (unsigned char)(input[i] & 0xff);
		output[j+1] = (unsigned char)((input[i] >> 8) & 0xff);
		output[j+2] = (unsigned char)((input[i] >> 16) & 0xff);
		output[j+3] = (unsigned char)((input[i] >> 24) & 0xff);
	}
}

/*
 * Decodes input (unsigned char) into output (MDUINT4). Assumes len is
 * a multiple of 4.
 */
static void Decode(output, input, len)
MDUINT4 *output;
unsigned char *input;
unsigned int len;
{
	unsigned int i, j;

	for (i = 0, j = 0; j < len; i++, j += 4)
		output[i] = ((MDUINT4)input[j]) |
		    (((MDUINT4)input[j+1]) << 8) |
		    (((MDUINT4)input[j+2]) << 16) |
		    (((MDUINT4)input[j+3]) << 24);
}

/*
 * Note: Replace "for loop" with standard memcpy if possible.
 */

static void MD5_memcpy(output, input, len)
MDPOINTER output;
MDPOINTER input;
unsigned int len;
{
	unsigned int i;

	for (i = 0; i < len; i++)
		output[i] = input[i];
}

/*
 * Note: Replace "for loop" with standard memset if possible.
 */
static void MD5_memset(output, value, len)
MDPOINTER output;
int value;
unsigned int len;
{
	unsigned int i;

	for (i = 0; i < len; i++)
		((char *)output)[i] = (char)value;
}
