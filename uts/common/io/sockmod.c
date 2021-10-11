/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sockmod.c	1.91	95/08/25 SMI"   /* SVr4.0 1.17	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		  All rights reserved.
 *
 */

/*
 * Socket Interface Library cooperating module.
 *
 * TODO:
 *	(1) Remove support for remembering local and remote
 *	    addresses, and instead rely on the transport
 *	    supporting the XPG4 inspired TPI primitives.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/socketvar.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/tiuser.h>
#include <sys/debug.h>
#include <sys/strlog.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/sockmod.h>
#include <sys/cmn_err.h>
#include <sys/tl.h>
#include <sys/sysmacros.h>
#include <netinet/in.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

/*
 * This is the loadable module wrapper.
 */

static struct streamtab sockinfo;

static struct fmodsw fsw = {
	"sockmod",
	&sockinfo,
	D_MP|D_MTQPAIR|D_SYNCSTR
};

/*
 * Module linkage information for the kernel.
 */

static struct modlstrmod modlstrmod = {
	&mod_strmodops, "socket interface lib", &fsw
};

static struct modlinkage modlinkage = {
	MODREV_1, &modlstrmod, NULL
};

static int module_keepcnt = 0;	/* ==0 means the module is unloadable */

/*
 * Socket structures are allocated dynamically and
 * freed when not in use. The `so_hash' pointers
 * points to the very first one.
 */
#define	SO_HASH_SIZE	256
#define	SO_HASH(driverq) (((unsigned long)(driverq) >> 8) % SO_HASH_SIZE)
static struct so_so	*so_hash[SO_HASH_SIZE];
static int		so_cnt = 0;
static int		so_id = 0;

/*
 * Protects:
 * - all instances of so->closeflags (SC_WCLOSE), and the following
 *	so_so members: so_cbacks_outstanding, so_cbacks_inprogress
 *	so_isaccepting, so_acceptor, so_next and so_ptpn.
 * - module_keepcnt, so_cnt, and so_id/.
 * - udata.so_state between t_ok_ack_get() and T_DISCON_IND or T_ORDREL_IND
 *   messages
 * All other socket structures fields are protected by the inner perimeter.
 */
static kmutex_t			so_global_lock;

/*
 * Protect traversal of linked list of
 * AF_UNIX socket. We use a mutex because
 * we don't do it much and only unix
 * domain sockets are affected.
 */
static krwlock_t		so_ux_rwlock;

/*
 * Pointer to the beginning of a list of
 * so_so entries that represent UNIX
 * domain sockets.
 * Has to be global so that netstat(8)
 * can find it for unix domain.
 */
struct so_so	*so_ux_list = (struct so_so *)NULL;

/*
 * Local macro to check pointer alignement
 */
#define	IS_NOT_ALIGNED(X)	(((uint)(X) & (sizeof (uint)-1)) != 0)


int
_init(void)
{
	int			error;

	rw_init(&so_ux_rwlock, "sockmod: so unix list", RW_DEFAULT, NULL);
	mutex_init(&so_global_lock, "sockmod global lock", MUTEX_DEFAULT, NULL);

	error = mod_install(&modlinkage);
	if (error != 0) {
		rw_destroy(&so_ux_rwlock);
		mutex_destroy(&so_global_lock);
		return (error);
	}

	return (0);
}

int
_fini(void)
{
	int	error;

	/*
	 * protect module_keepcnt from potential esballoc free_* routine
	 * Since module_keepcnt is probably heading toward 0, this
	 * becomes more of a TSO issue then actual protection
	 */
	mutex_enter(&so_global_lock);
	if (module_keepcnt != 0) {
		mutex_exit(&so_global_lock);
		return (EBUSY);
	}
	mutex_exit(&so_global_lock);

	error = mod_remove(&modlinkage);
	if (error != 0)
		return (error);
	rw_destroy(&so_ux_rwlock);
	mutex_destroy(&so_global_lock);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

#define	SIMOD_ID	50
#define	SIMWAIT		(1*HZ)
#define	MSGBLEN(A)	(int)((A)->b_wptr - (A)->b_rptr)
#define	MBLKLEN(A)	(int)((A)->b_datap->db_lim - (A)->b_datap->db_base)
#define	SI_ALIGN(p)	(((unsigned long)(p) + (sizeof (long)-1))\
						&~ (sizeof (long)-1))
typedef struct sockaddr * sockaddr_t;

/*
 * Used for freeing the band 1
 * message associated with urgent
 * data handling, and for when a
 * T_DISCON_IND is received.
 */
struct free_ptr {
	frtn_t		free_rtn;
	char		*buf;
	int		buflen;
	mblk_t		*mp;
	struct so_so	*so;
};

extern kmutex_t *QLOCK();

extern	int		nulldev();
extern	void		enterq(), leaveq();

static	int		tlitosyserr(int);
static	long		_t_setsize(long);
static	int		so_options(queue_t *, mblk_t *);
static	mblk_t		*_s_getmblk(mblk_t *, size_t);
static	void		snd_ERRACK(queue_t *, mblk_t *, int, int);
static	void		snd_OKACK(queue_t *, mblk_t *, int);
static	int		so_init(struct so_so *, struct T_info_ack *);
static	void		strip_zerolen(mblk_t *);
static	void		save_addr(struct netbuf *, caddr_t, size_t);
static	void		snd_ZERO(queue_t *, mblk_t *);
static	void		snd_FLUSHR(queue_t *, mblk_t *);
static	void		snd_ERRORW(queue_t *, mblk_t *);
static	void		snd_IOCNAK(queue_t *, mblk_t *, int);
static	void		snd_HANGUP(queue_t *, mblk_t *);
static	void		ux_dellink(struct so_so *);
static	void		ux_addlink(struct so_so *);
static	struct so_so	*ux_findlink(caddr_t, size_t);
static	void		ux_restoreaddr(struct so_so *, mblk_t *,
						caddr_t, size_t);
static	void		ux_saveraddr(struct so_so *, struct bind_ux *);
static	void		fill_udata_req_addr(mblk_t *, caddr_t, size_t);
static	void		fill_udata_ind_addr(mblk_t *, caddr_t, size_t);
static  mblk_t		*_s_makeopt(struct so_so *);
static  void		_s_setopt(mblk_t *, struct so_so *);
static	void		do_ERROR(queue_t *, mblk_t *);
static	mblk_t		*_s_getloaned(queue_t *, mblk_t *, int);
static	void		free_urg(char *);
static	void		free_zero_err(char *);
static	void		free_zero_zero(char *);
static	mblk_t		*_s_getband1(queue_t *, mblk_t *);
static	int		do_urg_inline(queue_t *, mblk_t *);
static	int		do_urg_outofline(queue_t *, mblk_t *);
static	mblk_t		*do_esbzero_err(queue_t *);
static	mblk_t		*do_esbzero_zero(queue_t *);
static	void		socklog(struct so_so *, char *, int);
static	void		so_addlink(struct so_so *);
static	void		so_dellink(struct so_so *);
static	void		t_ok_ack_get(struct so_so *);
static	int		t_con_res_get(queue_t *, mblk_t *);
static	struct so_so	*so_findlink(queue_t *);
static	void		recover(queue_t *, mblk_t *, int);
static	void		sofree(struct so_so *);
static	mblk_t		*create_tconn_ind(char *, size_t, char *, size_t,
							long);
static void		copy_o_si_udata(struct si_udata *, struct o_si_udata *);

int	dosocklog = 0;

/*
 * Standard STREAMS templates.
 */
static int	sockmodopen(),
		sockmodclose();
static void	sockmodrput(queue_t *q, mblk_t *mp),
		sockmodwput(queue_t *q, mblk_t *mp),
		sockmodwsrv(queue_t *q),
		sockmodrsrv(queue_t *q);
static int	sockmodwproc(queue_t *q, mblk_t *mp),
		sockmodrproc(queue_t *q, mblk_t *mp),
		sockmodrrw(queue_t *q, struiod_t *udp),
		sockmodrinfo(queue_t *q, infod_t *idp),
		sockmodwrw(queue_t *q, struiod_t *udp),
		sockmodwinfo(queue_t *q, infod_t *idp);

static struct module_info sockmod_info = {
	SIMOD_ID,
	"sockmod",
	0,		/* Write side set in sockmodopen() */
	INFPSZ,		/* Write side set in sockmodopen() */
	512,		/* Always left small */
	128		/* Always left small */
};
static struct qinit sockmodrinit = {
	(int (*)())sockmodrput,
	(int (*)())sockmodrsrv,
	sockmodopen,
	sockmodclose,
	nulldev,
	&sockmod_info,
	NULL,
	sockmodrrw,
	sockmodrinfo,
	STRUIOT_DONTCARE
};
static struct qinit sockmodwinit = {
	(int (*)())sockmodwput,
	(int (*)())sockmodwsrv,
	sockmodopen,
	sockmodclose,
	nulldev,
	&sockmod_info,
	NULL,
	sockmodwrw,
	sockmodwinfo,
	STRUIOT_DONTCARE
};
static struct streamtab sockinfo = {
	&sockmodrinit,
	&sockmodwinit,
	NULL,
	NULL
};


/*
 * Dummy qbufcall callback routine used by open and close.
 * The framework will wake up qwait_sig when we return from
 * this routine (as part of leaving the perimeters.)
 * (The framework enters the perimeters before calling the qbufcall() callback
 * and leaves the perimeters after the callback routine has executed. The
 * framework performs an implicit wakeup of any thread in qwait/qwait_sig
 * when it leaves the perimeter. See qwait(9E).)
 */
/* ARGSUSED */
static void dummy_callback(arg)
	int arg;
{}


/*
 * sockmodopen - open routine gets called when the
 *	module gets pushed onto the stream.
 */
/*ARGSUSED*/
static int
sockmodopen(q, dev, flag, sflag, crp)
	register queue_t	*q;
	register dev_t		*dev;
	register int		flag;
	register int		sflag;
	register cred_t		*crp;
{
	register struct so_so		*so;
	register struct stroptions	*stropt;
	register mblk_t			*bp = NULL;
	register mblk_t			*np = NULL;
	int				error;
	register queue_t		*wq;

	ASSERT(q != (queue_t *)NULL);
	wq = WR(q);

	if (q->q_ptr) {
		return (0);
	}

	so = (struct so_so *)kmem_zalloc(sizeof (struct so_so),	KM_SLEEP);
	so->rdq = q;
	q->q_ptr = (caddr_t)so;
	wq->q_ptr = (caddr_t)so;

	/*
	 * protect module_keepcnt from esballoc free_* routines,
	 * potential T_ERROR_ACK on accepting socket, etc.
	 */

	mutex_enter(&so_global_lock);
	module_keepcnt++;
	so->so_id = ++so_id;
	mutex_exit(&so_global_lock);

	socklog(so, "sockmodopen: Allocated so for q %x\n", (int)q);

	qprocson(q);

	/*
	 * Allocate the required messages upfront.
	 */
	while ((bp = allocb(sizeof (struct T_info_req) +
	    sizeof (struct T_info_ack), BPRI_HI)) == (mblk_t *)NULL) {
		int id;

		id = qbufcall(q, sizeof (struct T_info_req) +
				sizeof (struct T_info_ack), BPRI_HI,
				dummy_callback, 0);
		if (!qwait_sig(q)) {
			qunbufcall(q, id);
			error = EINTR;
			goto fail;
		}
		qunbufcall(q, id);
	}

	while ((np = allocb(sizeof (*stropt), BPRI_HI)) == (mblk_t *)NULL) {
		int id;

		id = qbufcall(q, sizeof (*stropt), BPRI_HI,
				dummy_callback, 0);
		if (!qwait_sig(q)) {
			qunbufcall(q, id);
			error = EINTR;
			goto fail;
		}
		qunbufcall(q, id);
	}

	/* Check if pushed on sockmod and return EALREADY if this is the case */
	{
		queue_t *nq;

		for (nq = wq->q_next; SAMESTR(nq); nq = nq->q_next)
			if (wq->q_qinfo == nq->q_qinfo) {
				error = EALREADY;
				goto fail;
			}
	}

	/*
	 * Send down a T_INFO_REQ.
	 * When the response is received,
	 * the address buffers will be
	 * allocated. We assume this will
	 * occur before they are actually
	 * needed, which is a pretty safe
	 * assumption.
	 */
	so->flags |= S_WINFO;
	bp->b_datap->db_type = M_PCPROTO;
	*(long *)bp->b_wptr = (long)T_INFO_REQ;
	bp->b_wptr += sizeof (struct T_info_req);
	putnext(wq, bp);
	bp = NULL;

	/* Wait for response to info req */
	while (so->flags & S_WINFO) {
		if (!qwait_sig(q)) {
			error = EINTR;
			goto fail;
		}
	}

	if (so->so_error != 0) {
		error = so->so_error;
		goto fail;
	}
	/*
	 * Set our write maximum and minimum packet sizes
	 * to that of the transport provider. If a
	 * transport provider is to be PR_ATOMIC then
	 * its q_minpsz should be set to 1 for write(2)
	 * to fail messages to large to be sent in a
	 * single crack.
	 */
	wq->q_minpsz = wq->q_next->q_minpsz;
	wq->q_maxpsz = wq->q_next->q_maxpsz;

	bp = np;
	bp->b_datap->db_type = M_SETOPTS;
	stropt = (struct stroptions *)bp->b_rptr;
	stropt->so_flags = SO_READOPT|SO_ERROPT;
	stropt->so_readopt = RPROTDIS;
	stropt->so_erropt = RERRNONPERSIST;
	if (so->udata.servtype == T_CLTS)
		stropt->so_readopt |= RMSGD;
	bp->b_wptr += sizeof (*stropt);
	putnext(q, bp);
	bp = NULL;

	/*
	 * Add this socket structure to the front of
	 * the list.
	 */
	so_addlink(so);

	return (0);

fail:
	qprocsoff(q);
	freemsg(bp);
	freemsg(np);
	kmem_free(so, sizeof (struct so_so));
	q->q_ptr = NULL;
	wq->q_ptr = NULL;
	mutex_enter(&so_global_lock);
	module_keepcnt--;
	mutex_exit(&so_global_lock);
	return (error);
}

static mblk_t *
so_mreadon()
{
	mblk_t			*mp;
	struct stroptions	*stropt;

	mp = allocb(sizeof (*stropt), BPRI_MED);
	if (mp == NULL)
		return (NULL);
	mp->b_datap->db_type = M_SETOPTS;
	stropt = (struct stroptions *)mp->b_rptr;
	stropt->so_flags = SO_MREADON;
	mp->b_wptr += sizeof (*stropt);
	return (mp);
}

/*
 * sockmodclose - This routine gets called when the module
 *		gets popped off of the stream.
 */
/*ARGSUSED*/
static int
sockmodclose(q, flag, credp)
	register queue_t	*q;
	register int		flag;
	cred_t			*credp;
{
	register struct so_so	*so;
	register mblk_t		*mp;

	ASSERT(q != NULL);
	so = (struct so_so *)q->q_ptr;
	ASSERT(so != NULL);

	socklog(so, "sockmodclose: Entered q %x\n", (int)q);

	/*
	 * Put any remaining messages downstream.
	 */
	while ((mp = getq(OTHERQ(q))) != NULL) {
		if (mp == so->bigmsg) {
			ASSERT(so->flags & S_WRDISABLE);
			socklog(so,
	"sockmodclose: Taking big message off write queue\n", 0);

			freemsg(mp);
			so->flags &= ~S_WRDISABLE;
			so->bigmsg = NULL;
		} else
			putnext(OTHERQ(q), mp);
	}
	ASSERT(so->bigmsg == NULL);
	ASSERT(!(so->flags & S_WRDISABLE));

	if (so->wbufcid) {
		qunbufcall(q, so->wbufcid);
		so->wbufcid = 0;
	}
	if (so->rbufcid) {
		qunbufcall(q, so->rbufcid);
		so->rbufcid = 0;
	}
	if (so->wtimoutid) {
		(void) quntimeout(q, so->wtimoutid);
		so->wtimoutid = 0;
	}
	if (so->rtimoutid) {
		(void) quntimeout(q, so->rtimoutid);
		so->rtimoutid = 0;
	}

	/*
	 * Synchonize with any call back routines
	 * which might fire off while we are testing
	 * so->so_cbacks_outstanding and/or so->so_cbacks_inprogress
	 * The so->so_cbacks_inprogress gets incremented upon entry
	 * into the call back routines and decremented
	 * upon exit. If we find its value > 0, we
	 * wait until the call back routines clear
	 * it. We need to drop the locks we are
	 * holding so that the call back routine
	 * can make progress.
	 */
	mutex_enter(&so_global_lock);
	while (so->so_cbacks_inprogress > 0) {
		mutex_exit(&so_global_lock);
		qwait(q);
		mutex_enter(&so_global_lock);
	}

	socklog(so, "sockmodclose: so->so_cbacks_outstanding %d\n",
		so->so_cbacks_outstanding);

	if (so->so_cbacks_outstanding || so->so_isaccepting) {
		/*
		 * Outstanding callbacks
		 */
		so->closeflags |= SC_WCLOSE;
		sofree(so);
		so->rdq->q_ptr = WR(so->rdq)->q_ptr = NULL;
		so->rdq = NULL;
	} else {
		/*
		 * Delete this socket structure
		 * from the linked list.
		 */
		sofree(so);
		so_dellink(so);
	}
	mutex_exit(&so_global_lock);
	/*
	 * Since we are D_MTQPAIR we can defer the qprocsoff until the
	 * very end. Callbacks are all handled above so this is the only
	 * thread that can exist in this instance.
	 */
	qprocsoff(q);
	return (0);
}


static void
sofree(so)
	struct so_so *so;
{
	mblk_t	*mp, *nmp;

	freemsg(so->iocsave);
	so->iocsave = NULL;

	mp = so->consave;
	while (mp) {
		nmp = mp->b_next;
		mp->b_next = NULL;
		freemsg(mp);
		mp = nmp;
	}
	so->consave = NULL;

	if (so->oob) {
		freemsg(so->oob);
		so->oob = NULL;
	}
	if (so->urg_msg) {
		freemsg(so->urg_msg);
		so->urg_msg = NULL;
	}

	if (so->laddr.buf) {
		/*
		 * If this was a UNIX domain endpoint, then
		 * update the linked list.
		 */
#ifdef AF_UNIX
		if ((so->laddr.len > 0) &&
		    ((sockaddr_t)so->laddr.buf)->sa_family
		    == AF_UNIX)
			ux_dellink(so);
#endif /* AF_UNIX */
		kmem_free(so->laddr.buf, so->laddr.maxlen + 1);
		so->laddr.buf = NULL;
	}
	if (so->raddr.buf)
		kmem_free(so->raddr.buf, so->raddr.maxlen + 1);
	so->raddr.buf = NULL;
}


/*
 * sockmodrput - Module read queue put procedure.
 *		 This is called from the module or
 *		 driver downstream.
 *		 Handles data messages if it can and
 *		 queues the rest.
 */
static void
sockmodrput(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	register union T_primitives	*pptr;
	register struct so_so		*so;

	so = (struct so_so *)q->q_ptr;
	ASSERT(so != NULL);

	/* Preserve message ordering during flow control */
	if (q->q_first != 0) {
		switch (mp->b_datap->db_type) {
		case M_FLUSH:
			/*
			 * Handle M_FLUSH directly to avoid reordering and
			 * flushing later messages.
			 */
			break;

		case M_PROTO:
		case M_PCPROTO:
			ASSERT(MSGBLEN(mp) >= sizeof (long));

			pptr = (union T_primitives *)mp->b_rptr;

			switch (pptr->type) {
			case T_DISCON_IND:
				/*
				 * Make sure all messages before the discon
				 * ind are processed (ignoring flow control)
				 * and then process the discon_ind.
				 * This is needed to ensure that discon_ind's
				 * get processed since we ignore M_FLUSH for
				 * AF_UNIX (in order to preserve data).
				 *
				 * The S_IGNORE_FLOW flag causes sockmodrsrv to
				 * ignore flow control - there is a small
				 * number of messages on sockmod's queue
				 * so this will not have
				 * any noticable effect on system memory usage.
				 * The flag is reset when the t_discon_ind is
				 * processed.
				 */
				socklog(so,
				    "sockmodrput: T_DISCON_IND - flow control",
				    0);
				so->flags |= S_IGNORE_FLOW;
				sockmodrsrv(q);
				if (q->q_first) {
					/* Must be due to recover() */
					ASSERT(so->rtimoutid || so->rbufcid);
					putq(q, mp);
					socklog(so,
						"sockmodrput: recover done",
						0);
					return;
				}
				break;
			default:
				(void) putq(q, mp);
				return;
			}
			break;
		default:
			(void) putq(q, mp);
			return;
		}
	}

	switch (mp->b_datap->db_type) {
	case M_DATA:
		/*
		 * If the socket is marked such that we don't
		 * want to get anymore data then free it.
		 */
		if (so->udata.so_state & SS_CANTRCVMORE) {
			freemsg(mp);
			break;
		}

		strip_zerolen(mp);
		/*
		 * Discard all zero-length messages to avoid the application
		 * reading zero.
		 */
		if (mp->b_rptr == mp->b_wptr && msgdsize(mp) == 0) {
			freemsg(mp);
			socklog(so, "sockmodrput: Got zero length M_DATA\n",
				0);
			break;
		}
		if (!canput(q->q_next)) {
			(void) putq(q, mp);
			break;
		}
		if (so->urg_msg) {
			putnext(q, so->urg_msg);
			so->urg_msg = (mblk_t *)NULL;
		}
		putnext(q, mp);
		break;

	case M_PROTO:
	case M_PCPROTO:
		/*
		 * Assert checks if there is enough data to determine type
		 */
		ASSERT(MSGBLEN(mp) >= sizeof (long));

		pptr = (union T_primitives *)mp->b_rptr;

		switch (pptr->type) {
		case T_UNITDATA_IND: {
			register struct T_unitdata_ind	*udata_ind;

			/*
			 * If we are connected, then we must ensure the
			 * source address is the one we connected to.
			 */
			udata_ind = (struct T_unitdata_ind *)mp->b_rptr;
			if (so->udata.so_state & SS_ISCONNECTED) {
				int af = ((sockaddr_t)so->raddr.buf)->sa_family;

				if (af == AF_UNIX) {
#ifdef AF_UNIX
					register char	*addr;

					addr = (char *)(mp->b_rptr +
						udata_ind->SRC_offset);
					if (bcmp(addr,
						(caddr_t)&so->rux_dev.addr,
						so->rux_dev.size) != 0) {
						/*
						 * Log error and free the msg.
						 */
						so->so_error = EINVAL;
						freemsg(mp);
						break;
					}
#endif /* AF_UNIX */
				} else if (af == AF_INET) {
					/* connected address in sock struct */
					struct sockaddr_in *lsin =
						(struct sockaddr_in *)
						so->raddr.buf;

					/* address in received packet */
					struct sockaddr_in *rsin =
						(struct sockaddr_in *)
						(mp->b_rptr +
						udata_ind->SRC_offset);

					/*
					 * We can do long compares because UDP
					 * and ICMP align the sockaddr_in
					 * struct in T_UNITDATA_IND messages
					 * that they send up, even though it is
					 * not required by TPI.  The Socket
					 * struct's connected address is also
					 * aligned.
					 */

					/* check for match on IP addr */
					if (lsin->sin_addr.s_addr !=
					    rsin->sin_addr.s_addr) {
						/*
						 * IP addr in packet doesn't
						 * match connected address.
						 * Log error and free the msg.
						 */
						so->so_error = EINVAL;
						freemsg(mp);
						break;
					}

					/*
					 * Check for match on port number.
					 * Allow wildcard match if connected
					 * port number is zero.
					 */
					if ((lsin->sin_port !=
					    rsin->sin_port) &&
					    (lsin->sin_port != 0)) {
						/*
						 * Log error and free the msg.
						 */
						so->so_error = EINVAL;
						freemsg(mp);
						break;
					}
				} else {
					if (bcmp(so->raddr.buf,
				(caddr_t)(mp->b_rptr+udata_ind->SRC_offset),
						so->raddr.len) != 0) {
						/*
						 * Log error and free the msg.
						 */
						so->so_error = EINVAL;
						freemsg(mp);
						break;
					}
				}
			}

			/*
			 * If flow control is blocking us then
			 * let the service procedure handle it.
			 */
			if (!canput(q->q_next)) {
				(void) putq(q, mp);
				break;
			}

			if (((sockaddr_t)so->laddr.buf)->sa_family == AF_UNIX) {
#ifdef AF_UNIX
				register struct so_so	*oso;
				register size_t		size;
				register char		*addr;

				addr = (caddr_t)(mp->b_rptr +
						udata_ind->SRC_offset);
				if ((oso = ux_findlink(addr,
					(size_t)udata_ind->SRC_length)) ==
							NULL) {
					freemsg(mp);
					break;
				}

				size = sizeof (*udata_ind) + oso->laddr.len;
				if (MBLKLEN(mp) < size) {
					register mblk_t		*bp;

					if ((bp = _s_getmblk((mblk_t *)NULL,
						size)) == (mblk_t *)NULL) {
						recover(q, mp, size);
						break;
					}
					*(struct T_unitdata_ind *)bp->b_wptr =
							*udata_ind;

					bp->b_cont = mp->b_cont;
					mp->b_cont = NULL;
					freemsg(mp);
					mp = bp;
				}
				fill_udata_ind_addr(mp, oso->laddr.buf,
							oso->laddr.len);
#endif /* AF_UNIX */
			}
			/*
			 * Check for zero length message and ensure that
			 * we always leave one linked to the header if
			 * there is no "real" data.
			 * This facilitates sending zero length messages
			 * on dgram sockets.
			 */
			if (mp->b_cont && msgdsize(mp->b_cont) == 0) {
				register mblk_t		*bp;

				/*
				 * Zero length message.
				 */

				socklog(so,
				"sockmodrput: Got zero length msg\n", 0);

				bp = mp->b_cont;
				freemsg(bp->b_cont);
				bp->b_cont = NULL;
			} else
				strip_zerolen(mp);

#ifdef C2_AUDIT
			if (audit_active)
				audit_sock(T_UNITDATA_IND, q, mp, SIMOD_ID);
#endif
			putnext(q, mp);
			break;
		}

		case T_DATA_IND:

			/*
			 * If the socket is marked such that we don't
			 * want to get anymore data then free it.
			 */
			if (so->udata.so_state & SS_CANTRCVMORE) {
				freemsg(mp);
				break;
			}

			strip_zerolen(mp);

			/*
			 * Discard all zero-length messages to avoid
			 * the application reading zero.
			 */
			if (msgdsize(mp) == 0) {
				freemsg(mp);
				socklog(so,
				    "sockmodrput: zero length T_DATA_IND\n",
				    0);
				break;
			}
			if (!canput(q->q_next)) {
				(void) putq(q, mp);
				break;
			}
			if (so->urg_msg) {
				putnext(q, so->urg_msg);
				so->urg_msg = (mblk_t *)NULL;
			}
			putnext(q, mp);
			break;
		default:
			(void) sockmodrproc(q, mp);
			break;
		}
		break;
	default:
		(void) sockmodrproc(q, mp);
		break;
	}
}

/*
 * sockmodrsrv - Module read queue service procedure.
 *		 Handles everything the write put
 *		 procedure dosen't want to.
 */
static void
sockmodrsrv(q)
	register queue_t	*q;
{
	mblk_t		*mp;

	while ((mp = getq(q)) != (mblk_t *)NULL) {
		if (sockmodrproc(q, mp)) {
			/*
			 * sockmodrproc did a putbq - stop processing
			 * messages.
			 */
			return;
		}
	}
}

/*
 * Process a read side message.
 * Return 0 if ok. Non-zero if message was putbq'd.
 */
static int
sockmodrproc(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	register union T_primitives	*pptr;
	register struct so_so		*so;
	register struct iocblk		*iocbp;
	register mblk_t			*bp;
	register size_t			size;

	ASSERT(q != NULL);
	so = (struct so_so *)q->q_ptr;
	ASSERT(so != NULL);

	switch (mp->b_datap->db_type) {
	default:
		putnext(q, mp);
		break;

	case M_DATA:
		/*
		 * If the socket is marked such that we don't
		 * want to get anymore data then free it.
		 */
		if (so->udata.so_state & SS_CANTRCVMORE) {
			freemsg(mp);
			break;
		}

		strip_zerolen(mp);
		/*
		 * Discard all zero-length messages to avoid the application
		 * reading zero.
		 */
		if (mp->b_rptr == mp->b_wptr && msgdsize(mp) == 0) {
			freemsg(mp);
			socklog(so, "sockmodrproc: Got zero length M_DATA\n",
				0);
			break;
		}

		if (!canput(q->q_next) && !(so->flags & S_IGNORE_FLOW)) {
			(void) putbq(q, mp);
			return (1);
		}
		if (so->urg_msg) {
			putnext(q, so->urg_msg);
			so->urg_msg = (mblk_t *)NULL;
		}
		putnext(q, mp);
		break;

	case M_PROTO:
	case M_PCPROTO:
		/*
		 * Assert checks if there is enough data to determine type
		 */
		ASSERT(MSGBLEN(mp) >= sizeof (long));

		pptr = (union T_primitives *)mp->b_rptr;

		switch (pptr->type) {
		default:
			putnext(q, mp);
			break;

		case T_DISCON_IND: {
			mblk_t		*op, *readon;
			mblk_t	*ump; /* for T_unbind_req */
			struct T_unbind_req *urp;

			socklog(so,
				"sockmodrproc: Got T_DISCON_IND Reason: %d\n",
				pptr->discon_ind.DISCON_reason);

			so->flags &= ~S_IGNORE_FLOW;

			if (so->udata.so_options & SO_ACCEPTCONN) {
				/*
				 * Ignore discon_ind to avoid sending
				 * M_ERROR and permanently disabling the
				 * listener stream.
				 */
				socklog(so,
					"sockmodrproc: listener ignored "
					"T_DISCON_IND\n",
					0);
				freemsg(mp);
				break;
			}

			if ((so->flags & S_WUNBIND_DISCON) == 0) {
				/*
				 * Send T_UNBIND_REQ down to transport
				 * if not already sent. (We could be here
				 * after recover() so it could have been sent)
				 * XXX Caveat: There is risk that the M_FLUSH
				 * preceding T_DISCON_IND has not reached write
				 * side of transport and will blow this away
				 * if it gets put on the queue when M_FLUSH
				 * loops back and flushes the write side. We
				 * assume transports do not put T_UNBIND_REQs
				 * on write side queue and process it in the
				 * put procedure. (True of transports used
				 * for PF_INET and PF_UNIX sockets - tcp and
				 * tl). For transports that do put it on queue,
				 * if T_UNBIND_REQ gets flushed,
				 * S_WUNBIND_DISCON state in sockmod is not
				 * cleared (and therefore M_FLUSHes are
				 * ignored - see M_FLUSH processing).
				 * However, the stream is being M_ERROR'd in
				 * T_DISCON_IND processing and no activity which
				 * will generate M_FLUSHs is expected.
				 */
				if ((ump = allocb(sizeof (struct T_unbind_req),
						BPRI_MED)) == NULL) {
					recover(q, mp,
						sizeof (struct T_unbind_req));
					return (1);
				}
				ump->b_datap->db_type = M_PROTO;
				urp = (struct T_unbind_req *) ump->b_rptr;
				urp->PRIM_type = T_UNBIND_REQ;
				ump->b_wptr += sizeof (struct T_unbind_req);
				/*
				 * send down the unbind request
				 */
				so->flags |= S_WUNBIND_DISCON;
				putnext(WR(q), ump);
			}

			bp = (mblk_t *)NULL;

			/* Pre-allocate necessary resources */
			if ((op = allocb(2, BPRI_MED)) == NULL) {
				recover(q, mp, 2);
				return (1);
			}
			if (((so->udata.so_state & SS_ISCONNECTING) &&
				(so->flags & S_WRDISABLE) == 0) ||
				((sockaddr_t)so->laddr.buf)->sa_family ==
					AF_UNIX) {
				if ((bp = do_esbzero_err(q)) == NULL) {
					freeb(op);
					recover(q, mp, 20);	/* XXX */
					return (1);
				}
			} else {
				if ((bp = do_esbzero_zero(q)) == NULL) {
					freeb(op);
					recover(q, mp, 20);	/* XXX */
					return (1);
				}
			}
			if ((readon = so_mreadon()) == NULL) {
				freeb(op);
				freemsg(bp);
				recover(q, mp, sizeof (struct stroptions));
				return (1);
			}
			so->so_error = pptr->discon_ind.DISCON_reason;

			/*
			 * If this is in response to a connect,
			 * and the caller is waiting, then send the
			 * disconnect up.
			 */
			if (so->udata.so_state & SS_ISCONNECTING) {
				if ((so->flags & S_WRDISABLE) == 0) {
					/* Turn on M_READ */
					putnext(q, readon);

					/*
					 * Close down the write side and
					 * begin to close down the read side.
					 */
					snd_ERRORW(q, op);
					linkb(mp, bp);

					/*
					 * Send the disconnect up, so that
					 * the reason can be extracted. An
					 * M_ERROR will be generated when
					 * the message is read.
					 */
					putnext(q, mp);

					/*
					 * there could be an acceptor
					 * messing with so_state in
					 * in t_ok_ack_get(), so grab lock
					 */
					mutex_enter(&so_global_lock);
					so->udata.so_state |=
					(SS_CANTRCVMORE|SS_CANTSENDMORE);
					so->udata.so_state &=
					~(SS_ISCONNECTED|SS_ISCONNECTING);
					mutex_exit(&so_global_lock);
					break;
				} else {
					/*
					 * Enable the write service queue to
					 * be scheduled, and schedule it.
					 */
					enableok(WR(q));
					qenable(WR(q));
				}
			}

			/* Turn on M_READ */
			putnext(q, readon);

			/*
			 * there could be an acceptor
			 * messing with so_state in
			 * in t_ok_ack_get(), so grab lock
			 */
			mutex_enter(&so_global_lock);
			so->udata.so_state |=
			    (SS_CANTRCVMORE|SS_CANTSENDMORE);
			so->udata.so_state &=
			    ~(SS_ISCONNECTED|SS_ISCONNECTING);
			mutex_exit(&so_global_lock);

			/*
			 * If this is UNIX domain, then send up
			 * a zero length esballoc'ed message and
			 * when the callback happens send up
			 * an M_ERROR. This way, existing messages
			 * are not lost and eof is received by the
			 * reader. This is UNIX domain semantics,
			 * and is not strictly what we want when a
			 * T_DISCON_IND is received thus it is a
			 * special case.
			 */
			if (((sockaddr_t)so->laddr.buf)->sa_family != AF_UNIX) {
				do_ERROR(q, mp);
				freeb(op);
				putnext(q, bp);
			} else {
				snd_ERRORW(q, op);
				freemsg(mp);
				putnext(q, bp);
			}
			break;
		}		/* case T_DISCON_IND */

		case T_ORDREL_IND: {
			mblk_t	*readon;

			socklog(so, "sockmodrproc: Got T_ORDREL_IND\n", 0);

			if ((readon = so_mreadon()) == NULL) {
				recover(q, mp, sizeof (struct stroptions));
				return (1);
			}
			/* Turn on M_READ */
			putnext(q, readon);

			/*
			 * there could be an acceptor
			 * messing with so_state in
			 * in t_ok_ack_get(), so grab lock
			 */
			mutex_enter(&so_global_lock);
			so->udata.so_state |= SS_CANTRCVMORE;

			/*
			 * Some providers send this when not fully connected.
			 * X.25 needs to retrieve disconnect reason after
			 * disconnect for compatibility. It uses T_ORDREL_IND
			 * instead of T_DISCON_IND so that it may use the
			 * endpoint after a connect failure to retrieve the
			 * reason using an ioctl. This is TPI violation.
			 */
			if (so->udata.so_state & SS_ISCONNECTING) {
				so->udata.so_state &= ~SS_ISCONNECTING;
			}
			mutex_exit(&so_global_lock);

			/*
			 * Send up zero length message(EOF) to
			 * wakeup anyone in a read(), or select().
			 * Attempt to send an esballoc'ed zero length message
			 * so that we can replace it when it is read. If
			 * allocation fails fall back on the single zero-length
			 * message.
			 */
			readon = do_esbzero_zero(q);
			if (readon == NULL) {
				recover(q, mp, 1);
				return (1);
			}
			freemsg(mp);
			putnext(q, readon);
			/*
			 * If write side was noenabled (and a "bigmsg" put
			 * on it during connection establishment), we need
			 * to enable the service routine (which will also
			 * remove the "bigmsg")
			 */
			if (so->flags & S_WRDISABLE) {
				ASSERT(so->bigmsg);
				/*
				 * Enable the write service queue to
				 * be scheduled, and schedule it.
				 */
				enableok(WR(q));
				qenable(WR(q));
			}
			break;
		}		/* case T_ORDREL_IND */

		case T_CONN_IND: {
			register mblk_t			*nbp;

			socklog(so, "sockmodrproc: Got T_CONN_IND\n", 0);

			/*
			 * Make sure we can dup the new
			 * message before proceeding.
			 */
			if (((nbp = dupmsg(mp)) == (mblk_t *)NULL) &&
			    ((nbp = copymsg(mp)) == (mblk_t *)NULL)) {
				recover(q, mp, sizeof (mblk_t));
				return (1);
			}

			if (((sockaddr_t)so->laddr.buf)->sa_family == AF_UNIX) {
#ifdef AF_UNIX
				register struct T_conn_ind	*conn_ind;
				register char			*addr;
				register struct so_so		*oso;
				size_t				size;

				/*
				 * To make sure the user sees a string rather
				 * than a dev/ino pair, we have to find the
				 * source socket structure and copy in the
				 * local (string) address.
				 */
				conn_ind = (struct T_conn_ind *)mp->b_rptr;
				size = (size_t)conn_ind->SRC_length;
				addr = (caddr_t)conn_ind + conn_ind->SRC_offset;
				if ((oso = ux_findlink(addr, size)) == NULL) {
					freemsg(mp);
					break;
				}
				/*
				 * Allocate new mblk for larger address
				 * and copy T_conn_ind with new address
				 */
				if ((bp = create_tconn_ind(oso->laddr.buf,
						oso->laddr.len,
						(char *) (mp->b_rptr +
							conn_ind->OPT_offset),
						conn_ind->OPT_length,
						conn_ind->SEQ_number))
						== (mblk_t *)NULL) {
					size = sizeof (struct T_conn_ind) +
						oso->laddr.len +
						conn_ind->OPT_length +
						sizeof (long); /* alignment */
					recover(q, mp, size);
					freemsg(nbp);
					return (1);
				}
				freemsg(mp);
				mp = bp;
#endif /* AF_UNIX */
			}

			/*
			 * Save our dup'ed copy.
			 */
			nbp->b_next = so->consave;
			so->consave = nbp;

#ifdef C2_AUDIT
			if (audit_active)
				audit_sock(T_CONN_IND, q, mp, SIMOD_ID);
#endif

			putnext(q, mp);
			break;
		}	/* case T_CONN_IND */

		case T_CONN_CON: {
			register struct T_conn_con	*conn_con;

			socklog(so, "sockmodrproc: Got T_CONN_CON\n", 0);

			/*
			 * Pass this up only if the user is waiting-
			 * tell the write service procedure to
			 * go for it.
			 */
			so->udata.so_state &= ~SS_ISCONNECTING;
			so->udata.so_state |= SS_ISCONNECTED;

			if (so->flags & S_WRDISABLE) {
				ASSERT(so->bigmsg);
				freemsg(mp);

				/*
				 * Enable the write service queue to
				 * be scheduled, and schedule it.
				 */
				enableok(WR(q));
				qenable(WR(q));
				break;
			}

			conn_con = (struct T_conn_con *)mp->b_rptr;
			if (((sockaddr_t)so->raddr.buf)->sa_family == AF_UNIX) {
#ifdef AF_UNIX
				register char		*addr;

				/*
				 * We saved the destination address, when
				 * the T_CONN_REQ was processed, so put
				 * it back.
				 */
				addr = (caddr_t)(mp->b_rptr +
						conn_con->RES_offset);
				size = sizeof (*conn_con) + so->raddr.len;
				if (MBLKLEN(mp) < size) {
					register struct T_conn_con *nconn_con;
					register mblk_t		   *bp;

					if ((bp = _s_getmblk((mblk_t *)NULL,
						size)) == (mblk_t *)NULL) {
						recover(q, mp, size);
						return (1);
					}

					bp->b_datap->db_type = M_PROTO;

					nconn_con =
						(struct T_conn_con *)bp->b_rptr;
					*nconn_con = *conn_con;
					freemsg(mp);
					mp = bp;
				}
				conn_con = (struct T_conn_con *)mp->b_rptr;
				addr = (caddr_t)(mp->b_rptr +
						conn_con->RES_offset);
				(void) bcopy(so->raddr.buf,
						addr, so->raddr.len);

				conn_con->RES_length = so->raddr.len;

				mp->b_wptr = mp->b_rptr + size;
#endif /* AF_UNIX */
			} else
				save_addr(&so->raddr,
				    (caddr_t)(mp->b_rptr+conn_con->RES_offset),
				    (size_t)conn_con->RES_length);

			putnext(q, mp);
			break;
		}			/* case T_CONN_CON */

		case T_UDERROR_IND:
			/*
			 * For connected datagram sockets check if
			 * the address matches the remote address.
			 */
			if (so->udata.servtype == T_CLTS &&
			    (so->udata.so_state & SS_ISCONNECTED)) {
				u_char *addr;

				addr = (u_char *)pptr +
					pptr->uderror_ind.DEST_offset;
				if (addr < mp->b_rptr ||
				    addr + pptr->uderror_ind.DEST_length >
				    mp->b_wptr) {
					/* Malformed address */
					freemsg(mp);
					break;
				}
				if (pptr->uderror_ind.DEST_length !=
				    so->raddr.len ||
				    bcmp(so->raddr.buf,
					    (char *)addr, so->raddr.len)) {
					freemsg(mp);
					break;
				}
				/* Match */
				socklog(so,
					"sockmodrproc: Got connected "
					"T_UDERROR_IND error %d\n",
					so->so_error);

				so->so_error = pptr->uderror_ind.ERROR_type;

				mp->b_datap->db_type = M_ERROR;
				mp->b_rptr = mp->b_wptr = mp->b_datap->db_base;

				/*
				 * Generate only a read side error since
				 * write side errors cause SIGPIPE.
				 */
				*mp->b_wptr++ = so->so_error;
				*mp->b_wptr++ = NOERROR;
				putnext(q, mp);
				break;
			}

			/*
			 * Just set so_error.
			 */
			so->so_error = pptr->uderror_ind.ERROR_type;

			socklog(so,
				"sockmodrproc: Got T_UDERROR_IND error %d\n",
				so->so_error);

			freemsg(mp);
			break;

		case T_UNITDATA_IND: {
			register struct T_unitdata_ind	*udata_ind;

			if (!canput(q->q_next) &&
			    !(so->flags & S_IGNORE_FLOW)) {
				(void) putbq(q, mp);
				return (1);
			}

			udata_ind = (struct T_unitdata_ind *)mp->b_rptr;
			if (((sockaddr_t)so->laddr.buf)->sa_family == AF_UNIX) {
#ifdef AF_UNIX
				/*
				 * UNIX domain, copy useful address.
				 */
				register char			*addr;
				register struct so_so		*oso;

				addr = (caddr_t)(mp->b_rptr +
						udata_ind->SRC_offset);

				if ((oso = ux_findlink(addr,
					(size_t)udata_ind->SRC_length)) ==
								NULL) {
					freemsg(mp);
					break;
				}

				size = sizeof (*udata_ind) + oso->laddr.len;
				if (MBLKLEN(mp) < size) {
					register mblk_t		*bp;

					if ((bp = _s_getmblk((mblk_t *)NULL,
						size)) == (mblk_t *)NULL) {
						recover(q, mp, size);
						return (1);
					}
					*(struct T_unitdata_ind *)bp->b_wptr =
							*udata_ind;

					bp->b_cont = mp->b_cont;
					mp->b_cont = NULL;
					freemsg(mp);
					mp = bp;
				}
				fill_udata_ind_addr(mp, oso->laddr.buf,
							oso->laddr.len);
#endif /* AF_UNIX */
			}
			/*
			 * Check for zero length message and ensure that
			 * we always leave one linked to the header if
			 * there is no "real" data.
			 * This facilitates sending zero length messages
			 * on dgram sockets.
			 */
			if (mp->b_cont && msgdsize(mp->b_cont) == 0) {
				register mblk_t		*bp;

				/*
				 * Zero length message.
				 */

				socklog(so,
				"sockmodrproc: Got zero length msg\n", 0);

				bp = mp->b_cont;
				freemsg(bp->b_cont);
				bp->b_cont = NULL;
			} else
				strip_zerolen(mp);
#ifdef C2_AUDIT
			if (audit_active)
				audit_sock(T_UNITDATA_IND, q, mp, SIMOD_ID);
#endif

			putnext(q, mp);
			break;
		}		/* case T_UNITDATA_IND */

		case T_DATA_IND:
			/*
			 * If the socket is marked such that we don't
			 * want to get anymore data then free it.
			 */
			if (so->udata.so_state & SS_CANTRCVMORE) {
				freemsg(mp);
				break;
			}

			strip_zerolen(mp);

			/*
			 * Discard all zero-length messages to avoid
			 * the application reading zero.
			 */
			if (msgdsize(mp) == 0) {
				freemsg(mp);
				socklog(so,
				    "sockmodrproc: zero length T_DATA_IND\n",
				    0);
				break;
			}

			if (!canput(q->q_next) &&
			    !(so->flags & S_IGNORE_FLOW)) {
				(void) putbq(q, mp);
				return (1);
			}
			if (so->urg_msg) {
				putnext(q, so->urg_msg);
				so->urg_msg = (mblk_t *)NULL;
			}
			putnext(q, mp);
			break;			/* case T_DATA_IND */

		case T_EXDATA_IND:

			socklog(so, "sockmodrproc: Got T_EXDATA_IND\n", 0);

			/*
			 * If the socket is marked such that we don't
			 * want to get anymore data then free it.
			 */
			if (so->udata.so_state & SS_CANTRCVMORE) {
				freemsg(mp);
				break;
			}

			/*
			 * Discard all zero-length messages to avoid
			 * the application reading zero.
			 */
			if (msgdsize(mp) == 0) {
				freemsg(mp);
				socklog(so,
				    "sockmodrproc: zero length T_EXDATA_IND\n",
				    0);
				break;
			}

			if (!canput(q->q_next) &&
			    !(so->flags & S_IGNORE_FLOW)) {
				(void) putbq(q, mp);
				return (1);
			}

			if ((so->udata.so_options & SO_OOBINLINE) == 0) {
				if (do_urg_outofline(q, mp) < 0)
					return (1);
			} else if (do_urg_inline(q, mp) < 0)
					return (1);
			break;			/* case T_EXDATA_IND */

		case T_ERROR_ACK:
			ASSERT(MSGBLEN(mp) == sizeof (struct T_error_ack));

			socklog(so, "sockmodrproc: Got T_ERROR_ACK\n", 0);

			/* Restore db_type - recover() might have changed it */
			mp->b_datap->db_type = M_PCPROTO;


			if (pptr->error_ack.ERROR_prim == T_UNBIND_REQ &&
						so->flags & S_WUNBIND_DISCON) {
				/*
				 * internally generated T_UNBIND_REQ
				 * caused this - clear state discard it and
				 * return
				 * No further processing done.
				 */
				so->flags &= ~S_WUNBIND_DISCON;
				socklog(so,
		"sockmodrproc: T_ERROR_ACK: discon T_UNBIND_REQ- tlierror %x\n",
				pptr->error_ack.TLI_error);

				freemsg(mp);
				return (0);
			}

			if (pptr->error_ack.ERROR_prim == T_CONN_RES) {
				struct so_so *oso;
				/* Clear any association with an acceptor */

				mutex_enter(&so_global_lock);
				oso = so->so_acceptor;
				if (oso != NULL) {
					ASSERT(oso->so_isaccepting);
					oso->so_isaccepting = 0;
					so->so_acceptor = NULL;

					if ((oso->closeflags & SC_WCLOSE) &&
					    oso->so_cbacks_outstanding == 0) {

						socklog(so,
						    "error for conn_res: "
						    "freeing so_so\n", 0);
						so_dellink(oso);
					}
				}
				mutex_exit(&so_global_lock);
			}

			if (pptr->error_ack.ERROR_prim == T_UNBIND_REQ &&
						so->flags & S_WUNBIND) {
				if (pptr->error_ack.TLI_error == TSYSERR)
					so->so_error =
						pptr->error_ack.UNIX_error;
				else
					so->so_error = tlitosyserr(
					    (int)pptr->error_ack.TLI_error);

				/*
				 * The error is a result of
				 * our internal unbind request.
				 */
				so->flags &= ~S_WUNBIND;
				freemsg(mp);

				snd_IOCNAK(q, so->iocsave, so->so_error);
				so->iocsave = (mblk_t *)NULL;
				break;
			}

			if (pptr->error_ack.ERROR_prim == T_INFO_REQ &&
							so->flags & S_WINFO) {
				if (pptr->error_ack.TLI_error == TSYSERR)
					so->so_error =
						pptr->error_ack.UNIX_error;
				else
					so->so_error = tlitosyserr(
					    (int)pptr->error_ack.TLI_error);

				socklog(so,
		"sockmodrproc: T_ERROR_ACK: T_INFO_REQ - error %x\n",
							so->so_error);
				so->flags &= ~S_WINFO;

				freemsg(mp);
				break;
			}

			if (pptr->error_ack.ERROR_prim == T_DISCON_REQ) {
				freemsg(mp);
				break;
			}

			if (pptr->error_ack.ERROR_prim == T_CONN_REQ)
				so->udata.so_state &= ~SS_ISCONNECTING;

			if ((so->flags & WAITIOCACK) == 0) {
				putnext(q, mp);
				break;
			}

			ASSERT(so->iocsave != NULL);
			if (so->iocsave->b_cont == NULL ||
			    pptr->error_ack.ERROR_prim !=
			    *(long *)so->iocsave->b_cont->b_rptr) {
				putnext(q, mp);
				break;
			}

			if (pptr->error_ack.ERROR_prim == T_BIND_REQ) {
				if (so->udata.so_options & SO_ACCEPTCONN)
					so->udata.so_options &= ~SO_ACCEPTCONN;
			} else if (pptr->error_ack.ERROR_prim ==
				    T_OPTMGMT_REQ) {

				socklog(so,
		"sockmodrproc: T_ERROR_ACK-optmgmt- %x\n", so->so_option);

				if (pptr->error_ack.TLI_error == TBADOPT &&
							so->so_option) {
					/*
					 * Must have been a T_NEGOTIATE
					 * for a socket option. Make it
					 * all work.
					 */
					freemsg(mp);
					mp = _s_makeopt(so);
					so->so_option = 0;
					goto out;
				} else
					so->so_option = 0;
			}

			switch (pptr->error_ack.ERROR_prim) {
			case T_OPTMGMT_REQ:
			case T_BIND_REQ:
			case T_UNBIND_REQ:
			case T_INFO_REQ:
				/*
				 * Get saved ioctl msg and set values
				 */
				iocbp = (struct iocblk *)so->iocsave->b_rptr;
				iocbp->ioc_error = 0;
				iocbp->ioc_rval = pptr->error_ack.TLI_error;
				if (iocbp->ioc_rval == TSYSERR) {
					iocbp->ioc_rval |=
						pptr->error_ack.UNIX_error << 8;
					so->so_error =
						pptr->error_ack.UNIX_error;
				} else
					so->so_error =
						tlitosyserr(iocbp->ioc_rval);

				so->iocsave->b_datap->db_type = M_IOCACK;
				putnext(q, so->iocsave);

				so->iocsave = NULL;
				so->flags &= ~WAITIOCACK;
				freemsg(mp);
				return (0);
			}	/* switch (pptr->error_ack.ERROR_prim) */
			/*
			 * If it hasn't made any sense to us
			 * we may as well log the fact and free it.
			 */
			socklog(so, "sockmodrproc: Bad T_ERROR_ACK prim %d\n",
				pptr->error_ack.ERROR_prim);
			freemsg(mp);
			break;		/* case T_ERROR_ACK */

		case T_OK_ACK:

			socklog(so, "sockmodrproc: Got T_OK_ACK\n", 0);

			/* Restore db_type - recover() might have changed it */
			mp->b_datap->db_type = M_PCPROTO;

			switch (pptr->ok_ack.CORRECT_prim) {
			case T_CONN_RES:
				t_ok_ack_get(so);
				break;

			case T_UNBIND_REQ:

				if (so->flags & S_WUNBIND_DISCON) {
					/*
					 * Internally generated T_UNBIND_REQ
					 * generated this.
					 * Clear state - discard - and
					 * return. No further processing.
					 */
					so->flags &= ~S_WUNBIND_DISCON;
					freemsg(mp);
					return (0);
				}
#ifdef AF_UNIX
				if (((sockaddr_t)so->laddr.buf)->sa_family
						== AF_UNIX)
					ux_dellink(so);
#endif AF_UNIX
				if (so->flags & S_WUNBIND) {
					so->flags &= ~S_WUNBIND;
					freemsg(mp);

					/*
					 * Put the saved TI_BIND request
					 * onto my write queue.
					 */

					socklog(so,
			"sockmodrproc: Handling saved TI_BIND request\n", 0);

					mp = so->iocsave;
					so->iocsave = (mblk_t *)NULL;
					(void) sockmodwproc(WR(q), mp);
					return (0);
				}
				break;

			case T_DISCON_REQ:
				/*
				 * Don't send it up.
				 */
				freemsg(mp);
				return (0);
			}

			if (so->flags & WAITIOCACK) {
				ASSERT(so->iocsave != NULL);
				if (so->iocsave->b_cont == NULL ||
				    pptr->ok_ack.CORRECT_prim !=
				    *(long *)so->iocsave->b_cont->b_rptr) {
					putnext(q, mp);
					break;
				}
				goto out;
			}
			putnext(q, mp);
			break;		/* case T_OK_ACK */

		case T_BIND_ACK: {
			register struct T_bind_ack	*bind_ack;

			socklog(so, "sockmodrproc: Got T_BIND_ACK\n", 0);

			/* Restore db_type - recover() might have changed it */
			mp->b_datap->db_type = M_PCPROTO;

			if ((so->flags & WAITIOCACK) == 0) {
				putnext(q, mp);
				break;
			}

			ASSERT(so->iocsave != NULL);
			if (so->iocsave->b_cont == NULL ||
			    *(long *)so->iocsave->b_cont->b_rptr !=
			    T_BIND_REQ) {
				putnext(q, mp);
				break;
			}

			bind_ack = (struct T_bind_ack *)mp->b_rptr;
#ifdef AF_UNIX
			if (so->laddr.len &&
				((sockaddr_t)so->laddr.buf)->sa_family ==
					AF_UNIX) {
				register char			*addr;

				addr = (caddr_t)(mp->b_rptr +
						bind_ack->ADDR_offset);

				/*
				 * If we don't have a copy of the actual
				 * address bound to then save one.
				 */
				if (so->lux_dev.size == 0) {
					(void) bcopy(addr,
					(caddr_t)&so->lux_dev.addr,
						(size_t)bind_ack->ADDR_length);
					so->lux_dev.size =
							bind_ack->ADDR_length;
				}

				/*
				 * UNIX domain, we have to put back the
				 * string part of the address as well as
				 * the actual address bound to.
				 */
				size = sizeof (struct bind_ux) +
						sizeof (*bind_ack);
				if (MBLKLEN(mp) < size) {
					register struct T_bind_ack *nbind_ack;
					register mblk_t		*bp;

					if ((bp = _s_getmblk((mblk_t *)NULL,
						size)) == (mblk_t *)NULL) {
						recover(q, mp, size);
						return (1);
					}
					bp->b_datap->db_type = M_PROTO;

					nbind_ack =
						(struct T_bind_ack *)bp->b_wptr;
					*nbind_ack = *bind_ack;

					ux_restoreaddr(so, bp, addr,
						(size_t)bind_ack->ADDR_length);
					bp->b_wptr = bp->b_rptr + size;

					freemsg(mp);
					mp = bp;
				} else {
					ux_restoreaddr(so, mp, addr,
						(size_t)bind_ack->ADDR_length);
					mp->b_wptr = mp->b_rptr + size;
				}
			}
#endif /* AF_UNIX */
			else {
				/*
				 * Remember the bound address.
				 */
				save_addr(&so->laddr,
				(caddr_t)(mp->b_rptr+bind_ack->ADDR_offset),
					(size_t)bind_ack->ADDR_length);
			}

			so->udata.so_state |= SS_ISBOUND;

			goto out;
		}		/* case T_BIND_ACK */

		case T_OPTMGMT_ACK:

			socklog(so, "sockmodrproc: Got T_OPTMGMT_ACK\n", 0);

			/* Restore db_type - recover() might have changed it */
			mp->b_datap->db_type = M_PCPROTO;

			if (so->flags & WAITIOCACK) {
				ASSERT(so->iocsave != NULL);
				if (so->iocsave->b_cont == NULL ||
				    *(long *)so->iocsave->b_cont->b_rptr !=
				    T_OPTMGMT_REQ) {
					putnext(q, mp);
					break;
				}
				if (so->so_option) {

					socklog(so,
		"sockmodrproc: T_OPTMGMT_ACK option %x\n", so->so_option);

					/*
					 * Check that the value negotiated is
					 * the one that we stored.
					 */
					_s_setopt(mp, so);
					so->so_option = 0;
				}
				goto out;
			}
			putnext(q, mp);
			break;		/* case T_OPTMGMT_ACK */

		case T_INFO_ACK:

			socklog(so, "sockmodrproc: Got T_INFO_ACK\n", 0);

			/* Restore db_type - recover() might have changed it */
			mp->b_datap->db_type = M_PCPROTO;

			if (so->flags & S_WINFO) {
				so->so_error = so_init(so,
					(struct T_info_ack *)pptr);

				so->laddr.len = 0;
				so->raddr.len = 0;

				/*
				 * Reserve space for local and remote
				 * addresses. Add 1 to length since kmem_alloc
				 * returns NULL for a zero-length allocation.
				 */
				if ((so->laddr.buf = (char *)kmem_alloc(
						so->udata.addrsize + 1,
						KM_NOSLEEP))
						== (char *)NULL) {
					socklog(so,
					"sockmodrproc: kmem_alloc failed\n", 0);
					recover(q, mp,
						2*so->udata.addrsize + 2);
					return (1);
				}
				(void) bzero(so->laddr.buf, so->udata.addrsize);
				so->laddr.len = so->laddr.maxlen =
					so->udata.addrsize;
				if ((so->raddr.buf = (char *)kmem_alloc(
						so->udata.addrsize + 1,
						KM_NOSLEEP))
						== (char *)NULL) {
					kmem_free(so->laddr.buf,
							so->laddr.maxlen + 1);
					so->laddr.len = so->laddr.maxlen = 0;
					socklog(so,
					"sockmodrproc: kmem_alloc failed\n", 0);
					recover(q, mp,
						2*so->udata.addrsize + 2);
					return (1);
				}
				so->raddr.len = so->raddr.maxlen =
					so->udata.addrsize;
				(void) bzero(so->raddr.buf, so->udata.addrsize);

				so->flags &= ~S_WINFO;
				freemsg(mp);
				break;
			} else {
				/*
				 * The library never issues a
				 * direct request for transport
				 * information.
				 */
				putnext(q, mp);
				break;
			}
out:
			iocbp = (struct iocblk *)so->iocsave->b_rptr;
			ASSERT(so->iocsave->b_datap != NULL);
			so->iocsave->b_datap->db_type = M_IOCACK;
			mp->b_datap->db_type = M_DATA;
			freemsg(so->iocsave->b_cont);
			so->iocsave->b_cont = mp;
			iocbp->ioc_error = 0;
			iocbp->ioc_rval = 0;
			iocbp->ioc_count = MSGBLEN(mp);

			putnext(q, so->iocsave);

			so->iocsave = NULL;
			so->flags &= ~WAITIOCACK;
			break;
		}	/* switch (pptr->type) */
		break;		/* case M_PROTO, M_PCPROTO */

	case M_FLUSH:

		socklog(so, "sockmodrproc: Got M_FLUSH\n", 0);

		if ((so->flags & S_WUNBIND_DISCON) ||
		    (so->flags & S_WUNBIND)) {
			/*
			 * This M_FLUSH is due to sockmod internally
			 * generated T_UNBIND_REQ messages. It should
			 * be ignored.
			 */
			socklog(so,
				"sockmodrproc: M_FLUSH dropped -internal\n", 0);
			freemsg(mp);
			break;
		}

		/*
		 * This is a kludge until something better
		 * is done. If this is a AF_UNIX socket,
		 * ignore the M_FLUSH and don't propogate it.
		 */
		if (so->laddr.len > 0 &&
				((sockaddr_t)so->laddr.buf)->sa_family ==
						AF_UNIX) {
			if (*mp->b_rptr & FLUSHW) {
				socklog(so,
				    "sockmodrproc: M_FLUSH - cleared FLUSHR\n",
				    0);
				*mp->b_rptr &= ~FLUSHR;
				putnext(q, mp);
				break;
			}
			socklog(so, "sockmodrproc: Ignoring M_FLUSH\n", 0);
			freemsg(mp);
			break;
		}

		if (*mp->b_rptr & FLUSHR)
			flushq(q, FLUSHDATA);

		putnext(q, mp);
		break;			/* case M_FLUSH */

	case M_IOCACK: {
		register struct iocblk		*iocbp;

		socklog(so, "sockmodrproc: Got M_IOCACK\n", 0);

		iocbp = (struct iocblk *)mp->b_rptr;
		switch (iocbp->ioc_cmd) {
		default:
			putnext(q, mp);
			break;

		case TI_GETPEERNAME:
		case TI_GETMYNAME:
			ASSERT(so->iocsave != NULL);
			freemsg(so->iocsave);

			if (iocbp->ioc_cmd == TI_GETPEERNAME &&
					so->udata.servtype == T_CLTS)
				so->udata.so_state |= SS_ISCONNECTED;

			so->iocsave = NULL;
			so->flags &= ~WAITIOCACK;
			putnext(q, mp);
			break;
		}
		break;
	}			/* case M_IOCACK */

	case M_IOCNAK: {
		register struct iocblk		*iocbp;

		socklog(so, "sockmodrproc: Got M_IOCNAK\n", 0);

		iocbp = (struct iocblk *)mp->b_rptr;
		switch (iocbp->ioc_cmd) {
		default:
			putnext(q, mp);
			break;

		case TI_GETMYNAME:
			ASSERT(so->iocsave != NULL);
			freemsg(mp);
			mp = so->iocsave;
			so->iocsave = NULL;
			so->flags |= NAMEPROC;
			if (ti_doname(WR(q), mp, so->laddr.buf, so->laddr.len,
				so->raddr.buf, so->raddr.len) != DONAME_CONT) {
				so->flags &= ~NAMEPROC;
			}
			so->flags &= ~WAITIOCACK;
			break;

		case TI_GETPEERNAME:
			freemsg(mp);
			ASSERT(so->iocsave != NULL);
			mp = so->iocsave;
			so->iocsave = NULL;
			so->flags |= NAMEPROC;
			if (ti_doname(WR(q), mp, so->laddr.buf, so->laddr.len,
				so->raddr.buf, so->raddr.len) != DONAME_CONT) {
				so->flags &= ~NAMEPROC;
			}
			so->flags &= ~WAITIOCACK;
			if (so->udata.servtype == T_CLTS)
				so->udata.so_state |= SS_ISCONNECTED;
			break;
		}
		break;
	}			/* case M_IOCNACK */
	}	/* switch (mp->b_datap->db_type) */
	return (0);
}

/*
 * sockmodrrw - Module read queue rw procedure.
 *		This is called from the module upstream.
 *		Handles only data messages, all others
 *		are handled as they would be by rput().
 */
static int
sockmodrrw(q, udp)
	register queue_t	*q;
	register struiod_t	*udp;
{
	register mblk_t		*mp;
	register struct so_so	*so;
	register int		rval = 0;

	so = (struct so_so *)q->q_ptr;
	ASSERT(so != NULL);

	if (q->q_first != 0) {
		/*
		 * Flow-controlled, just punt (let sockmodrsrv() handle it).
		 */
		return (EBUSY);
	}

	/*
	 * And now the fast-path, call the downstream modules read.
	 */
	if ((rval = rwnext(q, udp)) != 0 || ! (mp = udp->d_mp) != NULL)
		/*
		 * Error or nothing read from below.
		 */
		return (rval);

	switch (mp->b_datap->db_type) {
	case M_DATA:
		/*
		 * If the socket is marked such that we don't
		 * want to get anymore data then free it.
		 */
		if (so->udata.so_state & SS_CANTRCVMORE) {
			udp->d_mp = (mblk_t *)0;
			freemsg(mp);
			return (0);
		}

		strip_zerolen(mp);
		/*
		 * Discard all zero-length messages to avoid the application
		 * reading zero.
		 */
		if (mp->b_rptr == mp->b_wptr && msgdsize(mp) == 0) {
			udp->d_mp = NULL;
			freemsg(mp);
			socklog(so, "sockmodrrw: Got zero length M_DATA\n",
				0);
			return (0);
		}

		if (so->urg_msg) {
			/*
			 * Urgent message, so put it then the mblk (chain),
			 * we need to do this to preserve message ordering.
			 */
			putnext(q, so->urg_msg);
			so->urg_msg = (mblk_t *)NULL;
			putnext(q, udp->d_mp);
			udp->d_mp = (mblk_t *)0;
			return (0);
		}

		return (0);

	case M_PROTO:
	case M_PCPROTO:
		/*
		 * Type of mblk that rput() might be
		 * able to process, so pass it on.
		 */
		udp->d_mp = (mblk_t *)0;
		(void) sockmodrput(q, mp);
		return (0);

	default:
		/*
		 * Type of mblk that rput() won't process,
		 * so do what rput() would do.
		 */
		udp->d_mp = (mblk_t *)0;
		(void) sockmodrproc(q, mp);
		return (0);

	}
}

/*
 * sockmodrinfo - Module read queue information procedure.
 *		  This is called from the module upstream.
 */
static int
sockmodrinfo(q, idp)
	register queue_t	*q;
	register infod_t	*idp;
{
	register unsigned	cmd = idp->d_cmd;
	register int		res = 0;
	register mblk_t		*mp = q->q_first;

	while (mp && ! datamsg(mp->b_datap->db_type))
		mp = mp->b_next;
	if (mp) {
		/*
		 * We have enqued mblk(s).
		 */
		if (cmd & INFOD_BYTES)
			res |= INFOD_BYTES;
		if (cmd & INFOD_COUNT)
			res |= INFOD_COUNT;
		if (cmd & INFOD_FIRSTBYTES) {
			idp->d_bytes = msgdsize(mp);
			res |= INFOD_FIRSTBYTES;
			idp->d_cmd &= ~INFOD_FIRSTBYTES;
		}
		if (cmd & INFOD_COPYOUT) {
			mblk_t *mp1 = mp;
			int error;
			int n;

			while (mp1 && idp->d_uiop->uio_resid) {
				n = MIN(idp->d_uiop->uio_resid,
					mp1->b_wptr - mp1->b_rptr);
				if (n != 0 && (error = uiomove(
				    (char *)mp1->b_rptr, n,
				    UIO_READ, idp->d_uiop)) != 0)
					return (error);
				mp1 = mp1->b_cont;
			}
			res |= INFOD_COPYOUT;
			idp->d_cmd &= ~INFOD_COPYOUT;
		}
		if (cmd & (INFOD_BYTES|INFOD_COUNT)) {
			do {
				if (cmd & INFOD_BYTES)
					idp->d_bytes += msgdsize(mp);
				if (cmd & INFOD_COUNT)
					idp->d_count++;
			} while ((mp = mp->b_next) != NULL);
		}
	}
	if (res)
		idp->d_res |= res;

	if (isuioq(q))
		/*
		 * This is the struio() Q (last), nothing more todo.
		 */
		return (0);

	if (idp->d_cmd)
		/*
		 * Need to look at all mblk(s) or haven't completed
		 * all cmds, so pass info request on.
		 */
		return (infonext(q, idp));

	return (0);
}

/*
 * sockmodwput - Module write queue put procedure.
 *		 Called from the module or driver
 *		 upstream.
 *		 Handles messages that must be passed
 *		 through with minimum delay and queues
 *		 the rest for the service procedure to
 *		 handle.
 */
static void
sockmodwput(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	register struct so_so		*so;
	register union T_primitives	*pptr;
	register mblk_t			*bp;
	register size_t			size;

	so = (struct so_so *)q->q_ptr;
	ASSERT(so != NULL);

	/* Preserve message ordering during flow control */
	if (q->q_first != 0 && mp->b_datap->db_type < QPCTL) {
		(void) putq(q, mp);
		return;
	}

	/*
	 * Inline processing of data (to avoid additional procedure call).
	 * Rest is handled in sockmodwproc.
	 */
	switch (mp->b_datap->db_type) {
	case M_DATA:
		if ((so->udata.so_state & SS_ISCONNECTED) == 0) {
			/*
			 * Set so_error, and free the message.
			 */
			so->so_error = ENOTCONN;
			freemsg(mp);
			break;
		}
		/*
		 * Pre-pend the M_PROTO header.
		 */
		if (so->udata.servtype == T_CLTS) {
			if (((sockaddr_t)so->raddr.buf)->sa_family == AF_UNIX) {
#ifdef AF_UNIX
				size = sizeof (struct T_unitdata_req) +
							so->rux_dev.size;
				if ((bp = _s_getmblk((mblk_t *)NULL, size)) ==
								NULL) {
					recover(q, mp, size);
					break;
				}

				fill_udata_req_addr(bp,
						(caddr_t)&so->rux_dev.addr,
						so->rux_dev.size);
#endif /* AF_UNIX */
			} else {
				/*
				 * Not UNIX domain.
				 */
				size = sizeof (struct T_unitdata_req) +
							so->raddr.len;
				if ((bp = _s_getmblk((mblk_t *)NULL, size)) ==
								NULL) {
					recover(q, mp, size);
					return;
				}
				fill_udata_req_addr(bp, so->raddr.buf,
							so->raddr.len);
			}
			linkb(bp, mp);
			mp = bp;
		}

		if (!canput(q->q_next)) {
			(void) putbq(q, mp);
			return;
		}
		putnext(q, mp);
		break;

	case M_PROTO:
		/*
		 * Assert checks if there is enough data to determine type
		 */
		ASSERT(MSGBLEN(mp) >= sizeof (long));

		pptr = (union T_primitives *)mp->b_rptr;

		switch (pptr->type) {
		case T_DATA_REQ:
			if ((so->udata.so_state & SS_ISCONNECTED) == 0) {
				/*
				 * Set so_error and free the message.
				 */
				so->so_error = ENOTCONN;
				freemsg(mp);
				break;
			}

			if (!canput(q->q_next)) {
				(void) putq(q, mp);
				break;
			}
			putnext(q, mp);
			break;

		case T_UNITDATA_REQ: {
			register struct T_unitdata_req	*udata_req;

			/*
			 * If no destination address then make it look
			 * like a plain M_DATA and try again.
			 */
			udata_req = (struct T_unitdata_req *)mp->b_rptr;
			if (MSGBLEN(mp) < sizeof (*udata_req)) {

				socklog(so,
					"sockmodwput: Bad unitdata header %d\n",
					MSGBLEN(mp));

				freemsg(mp);
				break;
			}
			if (udata_req->DEST_length == 0) {
				if (mp->b_cont == (mblk_t *)NULL) {
					/*
					 * Zero length message.
					 */
					mp->b_datap->db_type = M_DATA;
					mp->b_wptr = mp->b_rptr;
				} else {
					bp = mp->b_cont;
					mp->b_cont = NULL;
					freemsg(mp);
					mp = bp;
				}
				(void) sockmodwproc(q, mp);
				break;
			}

			if (!canput(q->q_next)) {
				(void) putq(q, mp);
				break;
			}
			putnext(q, mp);
			break;
		}	/* case T_UNITDATA_REQ: */
		default:
			(void) sockmodwproc(q, mp);
			break;
		}
		break;
	default:
		(void) sockmodwproc(q, mp);
		break;
	}
}

/*
 * sockmodwsrv - Module write queue service procedure.
 *		 Handles messages that the put procedure
 *		 couldn't or didn't want to handle.
 */
static void
sockmodwsrv(q)
	register queue_t	*q;
{
	register struct so_so	*so;
	mblk_t			*mp;

	ASSERT(q != NULL);
	so = (struct so_so *)q->q_ptr;
	ASSERT(so != NULL);

	while ((mp = getq(q)) != (mblk_t *)NULL) {
		/*
		 * If we have been disabled, and the message
		 * we have is the message which caused select
		 * to work then just free it.
		 */
		if (so->flags & S_WRDISABLE) {
			ASSERT(so->bigmsg);
			if (mp == so->bigmsg) {
				socklog(so,
		"sockmodwsrv: Taking big message off write queue\n", 0);
				freemsg(mp);
				so->flags &= ~S_WRDISABLE;
				so->bigmsg = NULL;
				continue;
			}
		}
		if (sockmodwproc(q, mp)) {
			/*
			 * sockmodwproc did a putbq - stop processing
			 * messages.
			 */
			return;
		}
	}
}

/*
 * sockmodwrw - Module write queue rw procedure.
 *		This is called from the module
 *		upstream. Handles only data
 *		messages, all others are handled
 *		as they would be by wput().
 */
static int
sockmodwrw(q, udp)
	register queue_t	*q;
	register struiod_t	*udp;
{
	register mblk_t			*mp;
	register struct so_so		*so;
	register int			rval;

	so = (struct so_so *)q->q_ptr;
	ASSERT(so != NULL);

	/* Preserve message ordering during flow control */
	if (q->q_first != 0) {
		mp = udp->d_mp;
		udp->d_mp = (mblk_t *)0;
		if (! (rval = struioget(q, mp, udp)))
			(void) putq(q, mp);
		return (rval);
	}
	mp = udp->d_mp;

	switch (mp->b_datap->db_type) {

	case M_DATA:

		if ((so->udata.so_state & SS_ISCONNECTED) == 0) {
			/*
			 * Set so_error, and free the message.
			 */
			so->so_error = ENOTCONN;
			udp->d_mp = (mblk_t *)0;
			freemsg(mp);
			return (0);
		}
		if (so->udata.servtype == T_CLTS) {
			/*
			 * Pre-pend the M_PROTO header, we need to do this
			 * here, for now just hand it off too wput() until
			 * we can handle the flow control from below case.
			 * That is, handle decomposition when the rwnext()
			 * returns EWOULDBLOCK.
			 */
			udp->d_mp = (mblk_t *)0;
			if (! (rval = struioget(q, mp, udp)))
				sockmodwput(q, mp);
			return (rval);
		}
		if ((rval = rwnext(q, udp)) == EINVAL) {
			/*
			 * Down-stream module doesn't support rwnext(), so
			 * just get the mblk data and do what wput() would.
			 */
			udp->d_mp = (mblk_t *)0;
			if (! (rval = struioget(q, mp, udp))) {
				if (! canput(q->q_next))
					(void) putbq(q, mp);
				else
					putnext(q, mp);
			}
			return (rval);
		}
		return (rval);

	case M_PROTO:
		/*
		 * Type of mblk that wput() might be
		 * able to process, so pass it on.
		 */
		udp->d_mp = (mblk_t *)0;
		if (! (rval = struioget(q, mp, udp)))
			sockmodwput(q, mp);
		return (rval);

	default:
		/*
		 * Type of mblk that wput() won't process,
		 * so do what wput() would do, process it.
		 */
		udp->d_mp = (mblk_t *)0;
		if (! (rval = struioget(q, mp, udp)))
			(void) sockmodwproc(q, mp);
		return (rval);
	}
}

/*
 * sockmodwinfo - Module write queue information procedure.
 *		  This is called from the module upstream.
 */
static int
sockmodwinfo(q, idp)
	register queue_t	*q;
	register infod_t	*idp;
{
	return (infonext(q, idp));
}

/*
 * Process a write side message.
 * Return 0 if ok. Non-zero if message was putbq'd.
 */
static int
sockmodwproc(q, mp)
	register queue_t	*q;
	register mblk_t			*mp;
{
	register struct so_so		*so;
	struct iocblk			*iocbp;
	register mblk_t			*bp;
	register union T_primitives	*pptr;
	register size_t			size;

	ASSERT(q != NULL);
	so = (struct so_so *)q->q_ptr;
	ASSERT(so != NULL);

	switch (mp->b_datap->db_type) {
	case M_DATA:
		/*
		 * If CLTS, pre-pend the M_PROTO header.
		 */
		if (so->udata.servtype == T_CLTS) {
			if (((sockaddr_t)so->raddr.buf)->sa_family == AF_UNIX) {
#ifdef AF_UNIX
				size = sizeof (struct T_unitdata_req) +
							so->rux_dev.size;
				if ((bp = _s_getmblk((mblk_t *)NULL, size)) ==
								NULL) {
					recover(q, mp, size);
					return (1);
				}
				fill_udata_req_addr(bp,
						(caddr_t)&so->rux_dev.addr,
						so->rux_dev.size);
#endif /* AF_UNIX */
			} else {
				size = sizeof (struct T_unitdata_req) +
							so->raddr.len;
				if ((bp = _s_getmblk((mblk_t *)NULL, size)) ==
								NULL) {
					recover(q, mp, size);
					return (1);
				}
				fill_udata_req_addr(bp, so->raddr.buf,
							so->raddr.len);
			}
			linkb(bp, mp);
			mp = bp;
		}

		if (!canput(q->q_next)) {
			(void) putbq(q, mp);
			return (1);
		}
		putnext(q, mp);
		break;		/* case M_DATA */

	case M_PROTO:
		/*
		 * Assert checks if there is enough data to determine type
		 */
		ASSERT(MSGBLEN(mp) >= sizeof (long));

		pptr = (union T_primitives *)mp->b_rptr;

		switch (pptr->type) {
		default:
			putnext(q, mp);
			break;

		case T_DATA_REQ:
			if ((so->udata.so_state & SS_ISCONNECTED) == 0) {
				/*
				 * Set so_error and free the message.
				 */
				so->so_error = ENOTCONN;
				freemsg(mp);
				break;
			}

			if (!canput(q->q_next)) {
				(void) putbq(q, mp);
				return (1);
			}
			putnext(q, mp);
			break;	/* case T_DATA_REQ */

		case T_UNITDATA_REQ: {
			register struct T_unitdata_req	*udata_req;

			/*
			 * If no destination address then make it look
			 * like a plain M_DATA and try again.
			 */
			udata_req = (struct T_unitdata_req *)mp->b_rptr;
			if (MSGBLEN(mp) < sizeof (*udata_req)) {
				socklog(so,
					"sockmodwproc: Bad unitdata_req %d\n",
					MSGBLEN(mp));

				freemsg(mp);
				break;
			}
			if (udata_req->DEST_length == 0) {
				if (mp->b_cont == (mblk_t *)NULL) {
					/*
					 * Zero length message.
					 */
					mp->b_datap->db_type = M_DATA;
					mp->b_wptr = mp->b_rptr;
				} else {
					bp = mp->b_cont;
					mp->b_cont = NULL;
					freemsg(mp);
					mp = bp;
				}
				return (sockmodwproc(q, mp));
			}

			if (!canput(q->q_next)) {
				(void) putbq(q, mp);
				return (1);
			}
#ifdef C2_AUDIT
			if (audit_active)
				audit_sock(T_UNITDATA_REQ, q, mp, SIMOD_ID);
#endif
			putnext(q, mp);
			break;
		}	/* case T_UNITDATA_REQ: */

		case T_EXDATA_REQ:
			socklog(so, "sockmodwproc: Got T_EXDATA_REQ\n", 0);

			if ((so->udata.so_state & SS_ISCONNECTED) == 0) {
				/*
				 * Set so_error and free the message.
				 */
				so->so_error = ENOTCONN;
				freemsg(mp);
				break;
			}

			if (!canput(q->q_next)) {
				(void) putbq(q, mp);
				return (1);
			}
			putnext(q, mp);
			break;	/* case T_EXDATA_REQ */

		case T_CONN_REQ: {
			struct T_conn_req		*con_req;
			register int			error;
			register struct sockaddr	*addr;
			register struct bind_ux		*bind_ux;

			socklog(so, "sockmodwproc: Got T_CONN_REQ\n", 0);

			/*
			 * Make sure we can get an mblk large
			 * enough for any eventuality.
			 */
			size = max(sizeof (struct T_error_ack),
					sizeof (struct T_ok_ack));
			if ((bp = _s_getmblk((mblk_t *)NULL, size)) ==
							(mblk_t *)NULL) {
				recover(q, mp, size);
				return (1);
			}

			con_req = (struct T_conn_req *)mp->b_rptr;
			if (MSGBLEN(mp) < sizeof (*con_req) ||
				MSGBLEN(mp) < (con_req->DEST_offset +
						con_req->DEST_length)) {
				snd_ERRACK(q, bp, T_CONN_REQ, EINVAL);
				freemsg(mp);
				break;
			}

			addr = (sockaddr_t)(mp->b_rptr + con_req->DEST_offset);
			bind_ux = (struct bind_ux *)addr;

			/*
			 * If CLTS, we have to do the connect.
			 */
			if (so->udata.servtype == T_CLTS) {
				/*
				 * If the destination address is NULL, then
				 * dissolve the association.
				 */
				if (con_req->DEST_length == 0 ||
					addr->sa_family !=
			((sockaddr_t)so->laddr.buf)->sa_family) {

					socklog(so,
			"sockmodwproc: CLTS: Invalid address\n", 0);

					so->raddr.len = 0;
					so->udata.so_state &= ~SS_ISCONNECTED;
					snd_ERRACK(q, bp, T_CONN_REQ,
								EAFNOSUPPORT);

					/*
					 * Dissolve any association.
					 */
					so->so_conn = (struct so_so *)NULL;

					freemsg(mp);
					break;
				}

				/*
				 * Remember the destination address.
				 */
				if (con_req->DEST_length > so->udata.addrsize) {
					snd_ERRACK(q, bp, T_CONN_REQ, EPROTO);
					freemsg(mp);
					break;
				}
				if (addr->sa_family == AF_UNIX) {
					struct so_so	*oso;
#ifdef AF_UNIX
					socklog(so,
			"sockmodwproc: T_CONN_REQ(CLTS-UX)\n", 0);

					if (con_req->DEST_length !=
							sizeof (*bind_ux) ||
						bind_ux->extsize >
						sizeof (bind_ux->extaddr)) {
						snd_ERRACK(q, bp, T_CONN_REQ,
								EINVAL);
						freemsg(mp);
						break;
					}

					/*
					 * Point this end at the other
					 * end so that netstat will work.
					 */
					if ((oso = ux_findlink(
					(caddr_t)&bind_ux->ux_extaddr.addr,
					(size_t)bind_ux->ux_extaddr.size)) ==
						(struct so_so *)NULL) {
						snd_ERRACK(q, bp, T_CONN_REQ,
								ECONNREFUSED);
						freemsg(mp);
						break;
					}

					socklog(so,
		"sockmodwproc: T_CONN_REQ(CLTS-UX) so %x\n", (int)so);
					socklog(so,
		"sockmodwproc: T_CONN_REQ(CLTS-UX) oso %x\n", (int)oso);

					so->so_conn = oso;

					ux_saveraddr(so, bind_ux);

					socklog(so,
		"sockmodwproc: T_CONN_REQ(CLTS-UX) size %x\n",
		so->rux_dev.size);
#endif /* AF_UNIX */
				} else {
					/*
					 * Not UNIX domain.
					 */
					save_addr(&so->raddr, (caddr_t)addr,
						(size_t)con_req->DEST_length);
				}
				so->udata.so_state |= SS_ISCONNECTED;

				/*
				 * Now send back the T_OK_ACK
				 */
				snd_OKACK(q, bp, T_CONN_REQ);
				freemsg(mp);

				break;
			}

			/*
			 * COTS:
			 * Make sure not already connecting/ed.
			 */
			error = 0;
			if (so->udata.so_state & SS_ISCONNECTED)
				error = EISCONN;
			else if (so->udata.so_state & SS_ISCONNECTING)
				error = EALREADY;
			else if ((so->udata.so_state & SS_ISBOUND) == 0) {
				/*
				 * TCP can handle a T_CONN_REQ without being
				 * bound.
				 */
				if (so->udata.sockparams.sp_family == AF_INET &&
				    so->udata.sockparams.sp_type ==
							SOCK_STREAM) {
					so->udata.so_state |= SS_ISBOUND;
					((sockaddr_t)so->laddr.buf)->sa_family
						= AF_INET;
				} else
					error = EPROTO;
			} else if (con_req->DEST_length == 0 ||
			    addr->sa_family !=
			    ((sockaddr_t)so->laddr.buf)->sa_family)
				error = EAFNOSUPPORT;

			if (error) {
				snd_ERRACK(q, bp, T_CONN_REQ, error);
				freemsg(mp);
				break;
			}

			/*
			 * COTS: OPT_length will be -1 if
			 * user has O_NDELAY set.
			 */
			if (con_req->OPT_length == -1) {
				register mblk_t		*nmp;

				/*
				 * Put a large enough message on my write
				 * queue to cause the stream head
				 * to block anyone doing a write, and also
				 * cause select to work as we want, i.e.
				 * to not return true until a T_CONN_CON
				 * is returned.
				 * Note that no module/driver upstream
				 * can have a service procedure if this
				 * is to work.
				 */
				size = q->q_hiwat;
				if ((nmp = _s_getmblk((mblk_t *)NULL, size)) ==
								NULL) {
					recover(q, mp, size);
					freemsg(bp);
					return (1);
				}

				con_req->OPT_length = 0;

				nmp->b_datap->db_type = M_PROTO;
				nmp->b_wptr = nmp->b_datap->db_lim;
				so->bigmsg = nmp;
			} else
				so->bigmsg = NULL;

			/*
			 * Check for UNIX domain.
			 */
			if (addr->sa_family == AF_UNIX) {
#ifdef AF_UNIX
				socklog(so,
		"sockmodwproc: T_CONN_REQ(COTS) on UNIX domain\n", 0);

				if (con_req->DEST_length != sizeof (*bind_ux) ||
						bind_ux->extsize >
						sizeof (bind_ux->extaddr)) {
					snd_ERRACK(q, bp, T_CONN_REQ, EPROTO);
					freemsg(mp);
					freemsg(so->bigmsg);
					so->bigmsg = NULL;
					break;
				}

				/*
				 * Remember destination and
				 * adjust address.
				 */
				ux_saveraddr(so, bind_ux);

				(void) bcopy((caddr_t)&so->rux_dev.addr,
						(caddr_t)addr,
						(size_t)so->rux_dev.size);

				con_req->DEST_length = so->rux_dev.size;
				mp->b_wptr = mp->b_rptr + con_req->DEST_offset
						+ con_req->DEST_length;
#endif /* AF_UNIX */
			}

			freemsg(bp);	/* No longer needed */

			so->udata.so_state |= SS_ISCONNECTING;
#ifdef C2_AUDIT
			if (audit_active)
				audit_sock(T_CONN_REQ, q, mp, SIMOD_ID);
#endif
			putnext(q, mp);

			if (so->bigmsg) {
				/*
				 * Prevent the write service procedure
				 * from being enabled so that the large
				 * message that we are about to put on it
				 * will not be lost. The queue will be enabled
				 * when the T_CONN_CON is received.
				 */
				so->flags |= S_WRDISABLE;
				noenable(q);

				/*
				 * Enqueue the large message
				 */

				socklog(so, "sockmodwproc: Putting bigmsg %d\n",
					MSGBLEN(so->bigmsg));

				(void) putq(q, so->bigmsg);
				return (1);
			}
			break;
		}	/* case T_CONN_REQ: */

		case T_CONN_RES:
			if (t_con_res_get(q, mp))
				return (1);
			break;
		}	/* switch (pptr->type) */
		break;	/* case M_PROTO */

	case M_FLUSH:

		socklog(so, "sockmodwproc: Got M_FLUSH\n", 0);

		/*
		 * If this socket is not supposed to be
		 * sending any more data then the M_FLUSH
		 * will probably have come from the stream head
		 * as a result of our sending up an M_ERROR
		 * to close the write side.
		 * We do not want to propogate this downstream,
		 * because in some cases the transport will
		 * still have outstanding data waiting to go
		 * out.
		 */
		if (so->udata.so_state & SS_CANTSENDMORE) {
			freemsg(mp);
			break;
		}

		if (*mp->b_rptr & FLUSHW) {
			/* XXX flush semantics for select? */
			if (so->bigmsg) {
				/* assert that so->bigmsg is on the queue */
				ASSERT(so->bigmsg->b_prev ||
					so->bigmsg->b_next ||
					q->q_first == so->bigmsg);
				ASSERT(so->flags & S_WRDISABLE);
				so->flags &= ~S_WRDISABLE;
				so->bigmsg = NULL;
			}
			ASSERT(!(so->flags & S_WRDISABLE));
			flushq(q, FLUSHDATA);
		}
		putnext(q, mp);
		break;		/* case M_FLUSH */

	case M_IOCTL:

		socklog(so, "sockmodwproc: Got M_IOCTL\n", 0);

		ASSERT(MSGBLEN(mp) == sizeof (struct iocblk));

		iocbp = (struct iocblk *)mp->b_rptr;
		if (so->flags & WAITIOCACK) {
			snd_IOCNAK(RD(q), mp, EPROTO);
			break;
		}

		switch (iocbp->ioc_cmd) {
		default:
			putnext(q, mp);
			break;

		case SI_GETINTRANSIT: {
			register int		error;
			register queue_t	*qp;

			socklog(so, "sockmodwproc: Got SI_GETINTRANSIT q %x\n",
				(int)q);

			/*
			 * Return the peer's in-transit count.
			 */
			error = 0;
			if (so->udata.servtype != T_COTS_ORD &&
					so->udata.servtype != T_COTS)
				error = EINVAL;
			else if ((so->udata.so_state & SS_ISCONNECTED) == 0)
				error = ENOTCONN;
			else if (MSGBLEN(mp->b_cont) < sizeof (ulong))
				error = EINVAL;
			else if (((sockaddr_t)so->laddr.buf)->sa_family !=
			    AF_UNIX)
				error = EINVAL;

			if (error) {
				snd_IOCNAK(RD(q), mp, error);
				break;
			}

			/*
			 * Find our stream head and then
			 * follow it all the way to the
			 * the other end looking for any
			 * message queue that is not empty.
			 */
			qp = RD(q);
			for (qp = qp->q_next; qp->q_next; qp = qp->q_next)
				;

			qp = WR(qp);
			while (qp->q_first == (mblk_t *)NULL) {
				if (qp->q_next != (queue_t *)NULL)
					qp = qp->q_next;
				else
					break;
			}

			if (qp->q_first)
				error = 1;

			socklog(so,
			"sockmodwproc: SI_GETINTRANSIT: error = %d\n", error);

			*(int *)mp->b_cont->b_rptr = error;
			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);

			break;
		}		/* case SI_GETINTRANSIT */

		case SI_SOCKPARAMS: {

			socklog(so, "sockmodwproc: Got SI_SOCKPARAMS\n", 0);

			if (iocbp->ioc_count < sizeof (struct si_sockparams) ||
				mp->b_cont == (mblk_t *)NULL ||
				IS_NOT_ALIGNED(mp->b_cont->b_rptr)) {
				iocbp->ioc_error = EINVAL;
				snd_IOCNAK(RD(q), mp, iocbp->ioc_error);
				break;
			}

			so->udata.sockparams = *(struct si_sockparams *)
				mp->b_cont->b_rptr; /* struct copy */


			mp->b_datap->db_type = M_IOCACK;
			iocbp->ioc_count = 0;
			qreply(q, mp);
			break;

		}		/* case SI_SOCKPARAMS */

		case SI_TCL_LINK: {
			register char			*addr;
			register int			addrlen;
			register struct tl_sictl	*tcl_sictl;

			if (((sockaddr_t)so->laddr.buf)->sa_family != AF_UNIX) {
				snd_IOCNAK(RD(q), mp, EOPNOTSUPP);
				break;
			}
			if (so->udata.servtype != T_CLTS) {
				snd_IOCNAK(RD(q), mp, EOPNOTSUPP);
				break;
			}

			if (mp->b_cont) {
				/*
				 * Make sure there is a peer.
				 */
				if (ux_findlink((caddr_t)mp->b_cont->b_rptr,
						(size_t)MSGBLEN(mp->b_cont)) ==
								NULL) {
					snd_IOCNAK(RD(q), mp, ECONNREFUSED);
					break;
				}

				size = sizeof (*tcl_sictl)+MSGBLEN(mp->b_cont);
				if ((bp = _s_getmblk((mblk_t *)NULL, size)) ==
								NULL) {
					recover(q, mp, size);
					return (1);
				}
				addr = (caddr_t)mp->b_cont->b_rptr;
				addrlen = MSGBLEN(mp->b_cont);
			} else {
				/*
				 * Connected, verify remote address.
				*/
				if (so->rux_dev.size == 0) {
					snd_IOCNAK(RD(q), mp, ECONNREFUSED);
					break;
				}

				size = sizeof (*tcl_sictl) + so->rux_dev.size;
				if ((bp = _s_getmblk((mblk_t *)NULL, size)) ==
								NULL) {
					recover(q, mp, size);
					return (1);
				}
				addr = (caddr_t)&so->rux_dev.addr;
				addrlen = so->rux_dev.size;
			}

			tcl_sictl = (struct tl_sictl *)bp->b_wptr;
			tcl_sictl->type = TL_CL_LINK;
			tcl_sictl->ADDR_len = addrlen;
			tcl_sictl->ADDR_offset = sizeof (*tcl_sictl);
			(void) bcopy(addr,
				(caddr_t)(bp->b_wptr + tcl_sictl->ADDR_offset),
				(size_t)tcl_sictl->ADDR_len);
			bp->b_datap->db_type = M_CTL;
			bp->b_wptr += (tcl_sictl->ADDR_offset +
							tcl_sictl->ADDR_len);

			putnext(q, bp);

			iocbp->ioc_count = 0;
			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			break;
		}		/* case SI_TCL_LINK */

		case SI_TCL_UNLINK:
			if (((sockaddr_t)so->laddr.buf)->sa_family != AF_UNIX) {
				snd_IOCNAK(RD(q), mp, EOPNOTSUPP);
				break;
			}
			if (so->udata.servtype != T_CLTS) {
				snd_IOCNAK(RD(q), mp, EOPNOTSUPP);
				break;
			}

			/*
			 * Format an M_CTL and send it down.
			 */
			size = sizeof (long);
			if ((bp = _s_getmblk((mblk_t *)NULL, size)) ==
						(mblk_t *)NULL) {
				recover(q, mp, size);
				return (1);
			}
			*(long *)bp->b_wptr = TL_CL_UNLINK;
			bp->b_datap->db_type = M_CTL;
			bp->b_wptr += sizeof (long);
			putnext(q, bp);

			iocbp->ioc_count = 0;
			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			break;		/* case SI_TCL_UNLINK */

		case MSG_OOB:
		case MSG_PEEK:
		case MSG_OOB|MSG_PEEK: {
			register int		ilen;
			register int		olen;
			register mblk_t		*ibp;
			register mblk_t		*obp;
			register caddr_t	pos;
			int			error;

			error = 0;
			if (so->udata.etsdusize == 0 ||
				(so->udata.so_state & SS_ISBOUND) == 0 ||
				((sockaddr_t)so->laddr.buf)->sa_family
						== AF_UNIX)
				error = EOPNOTSUPP;
			else if (mp->b_cont == (mblk_t *)NULL ||
					so->udata.so_options & SO_OOBINLINE)
				error = EINVAL;
			else if (so->oob == (mblk_t *)NULL)
				error = EWOULDBLOCK;

			if (error) {
				snd_IOCNAK(RD(q), mp, error);
				break;
			}

			/*
			 * Process the data. We copy from the so->oob mblk chain
			 * to the mp->b_cont chain. The space available in the
			 * latter chain is the difference between b_wptr and
			 * b_rptr in each mblk.
			 * We go around the loop until we run out of data
			 * in so->oob or until we run out of room
			 * in mp->b_cont.
			 */
			iocbp->ioc_count = 0;
			obp = mp->b_cont;
			ibp = so->oob;
			pos = (caddr_t)ibp->b_rptr;
			olen = MSGBLEN(obp);
			obp->b_wptr = obp->b_rptr;
			for (;;) {
				ilen = MSGBLEN(ibp);
				size = MIN(olen, ilen);

				(void) bcopy(pos, (caddr_t)obp->b_wptr, size);

				pos += size;
				if ((iocbp->ioc_cmd & MSG_PEEK) == 0)
					ibp->b_rptr += size;
				obp->b_wptr += size;
				iocbp->ioc_count += size;
				ilen -= size;
				olen -= size;
				if (olen == 0) {
					/*
					 * This user block is exhausted, see
					 * if there is another.
					 */
					if (obp->b_cont) {
						/*
						 * Keep going
						 */
						obp = obp->b_cont;
						olen = MSGBLEN(obp);
						obp->b_wptr = obp->b_rptr;
						continue;
					}
					/*
					 * No more user blocks, finished.
					 */
					break;
				} else {
					/*
					 * This oob block is exhausted, see
					 * if there is another.
					 */
					if (ibp->b_cont) {
						ibp = ibp->b_cont;
						pos = (caddr_t)ibp->b_rptr;
						continue;
					}
					/*
					 * No more oob data, finished.
					 */
					break;
				}
			}
			if (ilen == 0 && (iocbp->ioc_cmd & MSG_PEEK) == 0) {
				freemsg(so->oob);
				so->oob = NULL;
			}

			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			break;
		}		/* case MSG_OOB, MSG_PEEK */

		case SI_LISTEN: {
			register struct T_bind_req	*bind_req;
			register int			error;

			socklog(so, "sockmodwproc: Got SI_LISTEN\n", 0);

			/*
			 * If we are already bound and the
			 * backlog is 0 then we just change state.
			 *
			 * If we are already bound, and backlog is not
			 * zero, we have to do an
			 * unbind followed by the callers bind, in
			 * order to set the number of connect
			 * indications correctly. When we have done what
			 * we have needed to do we just change the callers
			 * ioctl type and start again.
			 *
			 * We avoid doing the unbind for AF_INET since
			 * we know that TCP can handle a bind with an
			 * increased backlog. This is needed to avoid the race
			 * condition caused by the unbind relinquishing
			 * the port number.
			 */
			bind_req = (struct T_bind_req *)mp->b_cont->b_rptr;
			if ((so->udata.so_state & SS_ISBOUND) == 0) {
				/*
				 * Change it to a T_BIND_REQ and
				 * try again.
				 */
				iocbp->ioc_cmd = TI_BIND;
				so->udata.so_options |= SO_ACCEPTCONN;
				return (sockmodwproc(q, mp));
			}
			/*
			 * Don't bother if the backlog is 0, the
			 * original bind got it right.
			 */
			if (bind_req->CONIND_number == 0) {

				socklog(so,
		"sockmodwproc: Already bound and backlog = 0\n", 0);

				so->udata.so_options |= SO_ACCEPTCONN;

				mp->b_datap->db_type = M_IOCACK;
				iocbp->ioc_count = 0;
				qreply(q, mp);

				break;
			}
			if (iocbp->ioc_count <
				(sizeof (*bind_req) + so->laddr.len) ||
						mp->b_cont == (mblk_t *)NULL) {
				snd_IOCNAK(RD(q), mp, EINVAL);
				break;
			}
			if (((sockaddr_t)so->laddr.buf)->sa_family == AF_INET) {
				/*
				 * Change it to a T_BIND_REQ and
				 * try again since we know TCP can handle
				 * increasing the CONIND_number.
				 */
				iocbp->ioc_cmd = TI_BIND;
				so->udata.so_options |= SO_ACCEPTCONN;
				return (sockmodwproc(q, mp));
			}
			/*
			 * Set up the T_UNBIND_REQ request.
			 */
			size = sizeof (struct T_unbind_req);
			if ((bp = _s_getmblk((mblk_t *)NULL, size)) ==
							(mblk_t *)NULL) {
				recover(q, mp, size);
				return (1);
			}

			bp->b_datap->db_type = M_PROTO;
			*(long *)bp->b_wptr = T_UNBIND_REQ;
			bp->b_wptr += sizeof (struct T_unbind_req);

			/*
			 * Set up the subsequent T_BIND_REQ.
			 */
			error = 0;
			iocbp->ioc_cmd = TI_BIND;
			if (((sockaddr_t)so->laddr.buf)->sa_family == AF_UNIX) {
#ifdef AF_UNIX
				struct bind_ux	bindx;

				/*
				 * UNIX domain.
				 */
				size = so->lux_dev.size;
				if (bind_req->ADDR_length < size)
					error = EINVAL;
				else {
					bzero((caddr_t)&bindx,
						sizeof (struct bind_ux));
					(void) bcopy((caddr_t)so->laddr.buf,
						(caddr_t)&bindx.name,
						so->laddr.len);
					(void) bcopy((caddr_t)&so->lux_dev,
						(caddr_t)&bindx.ux_extaddr,
						sizeof (struct ux_extaddr));
					(void) bcopy((caddr_t)&bindx,
						(caddr_t)bind_req +
						bind_req->ADDR_offset,
						sizeof (struct bind_ux));
					bind_req->ADDR_length =
						sizeof (struct bind_ux);
				}
#endif /* AF_UNIX */
			} else {
				size = so->laddr.len;
				if (bind_req->ADDR_length < size)
					error = EINVAL;
				else {
					(void) bcopy(so->laddr.buf,
						(caddr_t)(mp->b_cont->b_rptr +
						bind_req->ADDR_offset),
						size);
					bind_req->ADDR_length = size;
				}
			}
			if (error) {
				snd_IOCNAK(RD(q), mp, error);
				freemsg(bp);
				break;
			}
			/*
			 * No error, so send down the unbind.
			 */
			so->flags |= S_WUNBIND;

			/*
			 * Save the TI_BIND request until the
			 * T_OK_ACK comes back.
			 */
			so->iocsave = mp;
			putnext(q, bp);

			/*
			 * update our state.
			 */
			so->udata.so_options |= SO_ACCEPTCONN;
			break;
		}		/* case SI_LISTEN */

		case O_SI_GETUDATA:

			socklog(so, "sockmodwproc: Got O_SI_GETUDATA\n", 0);

			if (iocbp->ioc_count < sizeof (struct o_si_udata) ||
						mp->b_cont == (mblk_t *)NULL) {
				snd_IOCNAK(RD(q), mp, EINVAL);
				break;
			}

			copy_o_si_udata(&so->udata,
				(struct o_si_udata *)mp->b_cont->b_rptr);
			mp->b_datap->db_type = M_IOCACK;
			iocbp->ioc_count = sizeof (struct o_si_udata);
			qreply(q, mp);
			break;		/* case O_SI_GETUDATA */

		case SI_GETUDATA:

			socklog(so, "sockmodwproc: Got SI_GETUDATA\n", 0);

			if (iocbp->ioc_count < sizeof (struct si_udata) ||
						mp->b_cont == (mblk_t *)NULL) {
				snd_IOCNAK(RD(q), mp, EINVAL);
				break;
			}

			(void) bcopy((caddr_t)&so->udata,
						(caddr_t)mp->b_cont->b_rptr,
						sizeof (struct si_udata));
			mp->b_datap->db_type = M_IOCACK;
			iocbp->ioc_count = sizeof (struct si_udata);
			qreply(q, mp);
			break;		/* case SI_GETUNDATA */

		case TI_GETPEERNAME:

			socklog(so,
				"sockmodwproc: Got TI_GETPEERNAME state %x\n",
				so->udata.so_state);

			if ((so->udata.so_state & SS_ISCONNECTED) == 0) {
				snd_IOCNAK(RD(q), mp, ENOTCONN);
				break;
			}

			/*
			 * See if this is a UNIX
			 * domain endpoint.
			 */
			if (so->raddr.len &&
				((sockaddr_t)so->laddr.buf)->sa_family ==
						AF_UNIX) {
#ifdef AF_UNIX
				socklog(so,
					"sockmodwproc: peer len %d\n",
					so->raddr.len);

				so->flags |= NAMEPROC;
				if (ti_doname(q, mp, so->laddr.buf,
					so->laddr.len, so->raddr.buf,
					so->raddr.len) != DONAME_CONT) {
					so->flags &= ~NAMEPROC;
				}
				break;
#endif /* AF_UNIX */
			}

			/*
			 * If this is a connectionless endpoint,
			 * then we will have handled the connect.
			 */
			if (so->udata.servtype == T_CLTS) {
				so->flags |= NAMEPROC;
				if (ti_doname(q, mp, so->laddr.buf,
					so->laddr.len, so->raddr.buf,
					so->raddr.len) != DONAME_CONT) {
					so->flags &= ~NAMEPROC;
				}
				break;
			}

			/*
			 * See if the transport provider supports it.
			 */
			if ((bp = copymsg(mp)) == (mblk_t *)NULL) {
				snd_IOCNAK(RD(q), mp, EAGAIN);
				break;
			}

			so->iocsave = mp;
			so->flags |= WAITIOCACK;

			putnext(q, bp);
			break;		/* case TI_GETPEERNAME */

		case TI_GETMYNAME:
			/*
			 * See if we have a copy of the address, or
			 * more importantly, see if this is a UNIX
			 * domain endpoint.
			 */
			if (so->laddr.len &&
				((sockaddr_t)so->laddr.buf)->sa_family ==
						AF_UNIX) {
#ifdef AF_UNIX
				so->flags |= NAMEPROC;
				if (ti_doname(q, mp, so->laddr.buf,
					so->laddr.len, so->raddr.buf,
					so->raddr.len) != DONAME_CONT) {
					so->flags &= ~NAMEPROC;
				}
				break;
#endif /* AF_UNIX */
			}

			/*
			 * See if the transport provider supports it.
			 */
			if ((bp = copymsg(mp)) == (mblk_t *)NULL) {
				snd_IOCNAK(RD(q), mp, EAGAIN);
				break;
			}

			so->iocsave = mp;
			so->flags |= WAITIOCACK;

			putnext(q, bp);
			break;		/* case TI_GETMYNAME */

		case SI_SETPEERNAME:

			socklog(so, "sockmodwproc: Got SI_SETPEERNAME\n", 0);

			if (iocbp->ioc_uid != 0)
				iocbp->ioc_error = EPERM;
			else if (so->udata.servtype != T_CLTS &&
				(so->udata.so_state & SS_ISCONNECTED) == 0)
				iocbp->ioc_error = ENOTCONN;
			else if (iocbp->ioc_count == 0 ||
				iocbp->ioc_count > so->raddr.maxlen ||
					(bp = mp->b_cont) == (mblk_t *)NULL)
				iocbp->ioc_error = EINVAL;

			if (iocbp->ioc_error) {
				snd_IOCNAK(RD(q), mp, iocbp->ioc_error);
				break;
			}

			so->udata.so_state |= SS_ISCONNECTED;
			save_addr(&so->raddr, (caddr_t)bp->b_rptr,
							iocbp->ioc_count);

			mp->b_datap->db_type = M_IOCACK;
			iocbp->ioc_count = 0;
			qreply(q, mp);
			break;		/* case SI_SETPEERNAME */

		case SI_SETMYNAME:

			socklog(so, "sockmodwproc: Got SI_SETMYNAME\n", 0);

			if (iocbp->ioc_uid != 0)
				iocbp->ioc_error = EPERM;
			else if (iocbp->ioc_count == 0 ||
				(so->udata.so_state & SS_ISBOUND) == 0 ||
				iocbp->ioc_count > so->laddr.maxlen ||
					(bp = mp->b_cont) == (mblk_t *)NULL)
				iocbp->ioc_error = EINVAL;

			if (iocbp->ioc_error) {
				snd_IOCNAK(RD(q), mp, iocbp->ioc_error);
				break;
			}

			save_addr(&so->laddr, (caddr_t)bp->b_rptr,
					iocbp->ioc_count);

			mp->b_datap->db_type = M_IOCACK;
			iocbp->ioc_count = 0;
			qreply(q, mp);
			break;		/* case SI_SETMYNAME */

		case SI_SHUTDOWN: {
			register int	how;
			register mblk_t	*op, *np, *readon;

			socklog(so, "sockmodwproc: Got SI_SHUTDOWN\n", 0);

			if (iocbp->ioc_count < sizeof (int) ||
						mp->b_cont == (mblk_t *)NULL)
				iocbp->ioc_error = EINVAL;
			else if ((how = *(int *)mp->b_cont->b_rptr) > 2 ||
				    how < 0)
				iocbp->ioc_error = EINVAL;

			socklog(so, "sockmodwproc: SI_SHUTDOWN how %d\n", how);

			if (iocbp->ioc_error) {
				snd_IOCNAK(RD(q), mp, iocbp->ioc_error);
				break;
			}

			/* Pre-allocate resources */
			op = readon = (mblk_t *)NULL;
			if (how == 0 || how == 2) {
				if ((op = allocb(2, BPRI_MED)) == NULL) {
					recover(q, mp, 2);
					return (1);
				}
				if ((readon = so_mreadon()) == NULL) {
					freemsg(op);
					recover(q, mp,
						sizeof (struct stroptions));
					return (1);
				}
			}
			if (how == 1 || how == 2) {
				size = max(sizeof (struct T_ordrel_req),
					sizeof (struct T_discon_req));
				if ((bp = allocb(size, BPRI_MED)) == NULL) {
					freemsg(op);
					freemsg(readon);
					recover(q, mp, size);
					return (1);
				}
				if ((np = allocb(2, BPRI_MED)) == NULL) {
					freemsg(op);
					freemsg(readon);
					freeb(bp);
					recover(q, mp, 2+2+size);
					return (1);
				}
			}

			switch (how) {
			case 0:
				/* Turn on M_READ */
				putnext(q, readon);

				so->udata.so_state |= SS_CANTRCVMORE;

				/*
				 * Send an M_FLUSH(FLUSHR) message upstream.
				 */
				snd_FLUSHR(RD(q), op);

				mp->b_datap->db_type = M_IOCACK;
				iocbp->ioc_count = 0;
				qreply(q, mp);

				break;

			case 1:
				so->udata.so_state |= SS_CANTSENDMORE;

				if (so->udata.servtype == T_COTS_ORD) {
					/*
					 * Send an orderly release.
					 */
					bp->b_datap->db_type = M_PROTO;
					*(long *)bp->b_wptr = T_ORDREL_REQ;
					bp->b_wptr +=
						sizeof (struct T_ordrel_req);
					putnext(q, bp);
				} else
					freeb(bp);

				mp->b_datap->db_type = M_IOCACK;
				iocbp->ioc_count = 0;
				qreply(q, mp);

				snd_ERRORW(RD(q), np);

				break;

			case 2:
				/*
				 * If orderly release is supported then send
				 * one, else send a disconnect.
				 */
				/* Turn on M_READ */
				putnext(q, readon);

				so->udata.so_state |= (SS_CANTRCVMORE|
							SS_CANTSENDMORE);
				if (so->udata.servtype == T_COTS_ORD) {
					bp->b_datap->db_type = M_PROTO;
					*(long *)bp->b_wptr = T_ORDREL_REQ;
					bp->b_wptr +=
						sizeof (struct T_ordrel_req);
					putnext(q, bp);

				} else if (so->udata.servtype == T_COTS) {
					register struct T_discon_req *req;

					req = (struct T_discon_req *)bp->b_wptr;
					req->PRIM_type = T_DISCON_REQ;
					req->SEQ_number = -1;

					bp->b_datap->db_type = M_PROTO;
					bp->b_wptr += sizeof (*req);
					putnext(q, bp);
				}
				/*
				 * Send an M_FLUSH(FLUSHR) message upstream.
				 */
				snd_FLUSHR(RD(q), np);

				mp->b_datap->db_type = M_IOCACK;
				iocbp->ioc_count = 0;
				qreply(q, mp);

				snd_HANGUP(RD(q), op);

				break;
			}
			break;
		}	/* case SI_SHUTDOWN: */

		case TI_BIND:
		case TI_UNBIND:
		case TI_OPTMGMT:
			if (mp->b_cont == (mblk_t *)NULL) {
				snd_IOCNAK(RD(q), mp, EINVAL);
				break;
			}
			if (!pullupmsg(mp->b_cont, -1)) {
				snd_IOCNAK(RD(q), mp, ENOSR);
				break;
			}
			if (iocbp->ioc_cmd == TI_BIND) {
				register struct T_bind_req	*bind_req;
				register struct sockaddr	*addr;

				bind_req =
					(struct T_bind_req *)mp->b_cont->b_rptr;

				if (MSGBLEN(mp->b_cont) < sizeof (*bind_req)) {
					snd_IOCNAK(RD(q), mp, EPROTO);
					break;
				}
				if (MSGBLEN(mp->b_cont) <
						(bind_req->ADDR_offset +
						bind_req->ADDR_length)) {
					snd_IOCNAK(RD(q), mp, EPROTO);
					break;
				}

				addr = (sockaddr_t)(mp->b_cont->b_rptr +
						bind_req->ADDR_offset);

				if (bind_req->ADDR_length >= 2 &&
					addr->sa_family == AF_UNIX) {
#ifdef AF_UNIX
					register struct bind_ux *bind_ux;

					/*
					 * Sanity check on size.
					 */
					if (bind_req->ADDR_length <
					    sizeof (*bind_ux)) {
						snd_IOCNAK(RD(q), mp, EPROTO);
						break;
					}

					socklog(so,
				"sockmodwproc: UNIX domain BIND\n", 0);

					/*
					 * Remember the address string
					 */
					bind_ux = (struct bind_ux *)addr;
					save_addr(&so->laddr, (caddr_t)addr,
						sizeof (struct sockaddr_un));

					/*
					 * If the user specified an address
					 * to bind to then save it and adjust
					 * the address so that the transport
					 * provider sees what we want it to.
					 */
					size = bind_ux->extsize;
					if (size) {

						socklog(so,
				"sockmodwproc: Non null BIND\n", 0);

						/*
						 * Non-Null bind request.
						 */
						(void) bcopy(
						(caddr_t)&bind_ux->extaddr,
						(caddr_t)&so->lux_dev.addr,
						size);
						so->lux_dev.size = size;

						/*
						 * Adjust destination, by moving
						 * the bind part of bind_ux
						 * to the beginning of the
						 * address.
						 */
						(void) bcopy(
						(caddr_t)&so->lux_dev.addr,
						(caddr_t)bind_ux,
						so->lux_dev.size);

						bind_req->ADDR_length =
							so->lux_dev.size;
						mp->b_cont->b_wptr =
							mp->b_cont->b_rptr +
							sizeof (*bind_req) +
							so->lux_dev.size;
					} else {
						bind_req->ADDR_length = 0;
						bind_req->ADDR_offset = 0;
						mp->b_cont->b_wptr =
							mp->b_cont->b_rptr +
							sizeof (*bind_req);

						so->lux_dev.size = 0;
					}
					iocbp->ioc_count = MSGBLEN(mp->b_cont);

					socklog(so,
		"sockmodwproc: BIND length %d\n", bind_req->ADDR_length);

					/*
					 * Add it to the list of UNIX
					 * domain endpoints.
					 */
					ux_addlink(so);
#endif /* AF_UNIX */
				}
			}
			if (iocbp->ioc_cmd == TI_OPTMGMT) {
				register int	retval;

				/*
				 * Do any socket level options
				 * processing.
				 */
				retval = so_options(q, mp->b_cont);
				if (retval == 1) {
					mp->b_datap->db_type = M_IOCACK;
					qreply(q, mp);
					break;
				}
				if (retval < 0) {
					snd_IOCNAK(RD(q), mp, -retval);
					break;
				}
			}

			if ((bp = copymsg(mp->b_cont)) == (mblk_t *)NULL) {
				snd_IOCNAK(RD(q), mp, ENOSR);
				break;
			}

			so->iocsave = mp;
			so->flags |= WAITIOCACK;

			if (iocbp->ioc_cmd == TI_GETINFO)
				bp->b_datap->db_type = M_PCPROTO;
			else
				bp->b_datap->db_type = M_PROTO;

			putnext(q, bp);
			break;
		}	/* switch (iocbp->ioc_cmd) */
		break;		/* case M_IOCTL */

	case M_IOCDATA:

		socklog(so, "sockmodwproc: Got M_IOCDATA\n", 0);

		if (so->flags & NAMEPROC) {
			if (ti_doname(q, mp, so->laddr.buf, so->laddr.len,
				so->raddr.buf, so->raddr.len) != DONAME_CONT)
				so->flags &= ~NAMEPROC;
			break;
		}
		putnext(q, mp);
		break;		/* case M_IOCDATA */

	case M_READ:

		socklog(so, "sockmodwproc: Got M_READ\n", 0);

		/*
		 * If the socket is marked SS_CANTRCVMORE then
		 * send up a zero length message, to make the user
		 * get EOF. Otherwise just forget it.
		 */
		if (so->udata.so_state & SS_CANTRCVMORE)
			snd_ZERO(RD(q), mp);
		else
			freemsg(mp);
		break;		/* case M_READ */

	default:
		putnext(q, mp);
		break;

	}	/* switch (mp->b_datap->db_type) */
	return (0);
}

/*
 * This uses the so_global_lock to
 * make sure the accepting queue hangs around until
 * we are at least done with changing
 * its state etc.
 * Return 0 if ok. Non-zero if message was putbq'd.
 */
static int
t_con_res_get(q, mp)
	queue_t *q;
	mblk_t	*mp;
{
	struct so_so *so = (struct so_so *)q->q_ptr;
	register mblk_t			*bp;
	register size_t			size;
	register struct T_conn_res	*conn_res;
	register struct T_conn_ind	*conn_ind;
	register mblk_t			*pmp;
	register struct so_so		*oso;

	socklog(so, "sockmodwsrv: Got T_CONN_RES\n", 0);

	if (MSGBLEN(mp) < sizeof (*conn_res)) {
		size = sizeof (struct T_error_ack);
		if ((bp = _s_getmblk((mblk_t *)mp, size)) ==
		    NULL) {
			recover(q, mp, size);
			return (1);
		}
		snd_ERRACK(q, bp, T_CONN_RES, EINVAL);
		return (0);
	}

	/*
	 * We have to set the local and remote addresses
	 * for the endpoint on which the connection was
	 * accepted on. The endpoint is marked connected
	 * when the T_OK_ACK is received.
	 */
	conn_res = (struct T_conn_res *)mp->b_rptr;

	/*
	 * Find the new endpoints queue_t.
	 */
	mutex_enter(&so_global_lock);
	oso = so_findlink(conn_res->QUEUE_ptr);

	if (oso == NULL) {
		/*
		 * Something wrong here
		 * let the transport provider
		 * find it.
		 */

		mutex_exit(&so_global_lock);
		socklog(so, "sockmodwsrv: No queue_t\n", 0);

		putnext(q, mp);
		return (0);
	}
	if (so->so_acceptor != NULL) {
		mutex_exit(&so_global_lock);
		socklog(so, "sockmodwsrv: t_con_res already in progress\n", 0);
		snd_IOCNAK(q, mp, EPROTO);
		return (0);
	}
	/*
	 * To find the stream corresponding
	 * to `resfd' when the T_OK_ACK comes
	 * back we use the so_acceptor field.
	 * We update the state of the `resfd'
	 * stream to ensure that if a devious
	 * program closes it the close routine
	 * can detect it.
	 */
	so->so_acceptor = oso;
	/*
	 * All so_isaccepting fields are protected by the so_global_lock.
	 * Once so_isaccepting has been set the queue can not close.
	 */
	oso->so_isaccepting = 1;
	mutex_exit(&so_global_lock);

	/*
	 * Set the local address of the
	 * new endpoint to the local address
	 * of the endpoint on which the connect
	 * request was received.
	 */
	save_addr(&oso->laddr, so->laddr.buf, so->laddr.len);

	/*
	 * Set the peer address of the
	 * new endpoint to the source address
	 * of the connect request.
	 * We have to find the saved
	 * T_CONN_IND for this sequence number
	 * to retrieve the correct SRC address.
	 */
	pmp = NULL;
	for (bp = so->consave; bp; bp = bp->b_next) {
		conn_ind = (struct T_conn_ind *)bp->b_rptr;
		if (conn_ind->SEQ_number ==
		    conn_res->SEQ_number)
			break;
		pmp = bp;
	}
	if (bp != NULL) {
		if (pmp)
			pmp->b_next = bp->b_next;
		else
			so->consave = bp->b_next;
		bp->b_next = NULL;
	}
	if (((sockaddr_t)so->laddr.buf)->sa_family == AF_UNIX) {
#ifdef AF_UNIX
		register struct so_so		*nso;

		if ((nso = ux_findlink((caddr_t)(bp->b_rptr +
						conn_ind->SRC_offset),
				    (size_t)conn_ind->SRC_length)) ==
				(struct so_so *)NULL) {

			socklog(so,
				"sockmodwsrv: UNIX: No peer\n", 0);

			oso->raddr.len = 0;
		} else {
			save_addr(&oso->raddr, nso->laddr.buf,
				nso->laddr.len);

			/*
			 * Give each end of the connection
			 * a pointer to the other so that
			 * netstat works as it should.
			 * This is pointing each end of the
			 * prototype connection at each other.
			 */
			oso->so_conn = nso;
			nso->so_conn = oso;
		}
#endif /* AF_UNIX */
	} else
		save_addr(&oso->raddr,
			(caddr_t)(bp->b_rptr+conn_ind->SRC_offset),
			conn_ind->SRC_length);

	/*
	 * The new socket inherits the properties of the
	 * old socket except SO_ACCEPTCONN.
	 */
	oso->udata.so_state = so->udata.so_state;
	oso->udata.so_options = so->udata.so_options & ~SO_ACCEPTCONN;
	oso->linger = so->linger;

	freemsg(bp);
	putnext(q, mp);
	return (0);
}

/*
 * Returns  -<error number>
 *	0 if option needs to be passed down
 *	1 if option has been serviced
 *
 *	Should not assume the T_OPTMGMT_REQ buffer is large enough to hold
 *	the T_OPTMGMT_ACK message.
 */
static int
so_options(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;

{
	/*
	 * Trap the ones that we must handle directly
	 * or that we must take action on in addition
	 * to sending downstream for the TP.
	 */
	register struct T_optmgmt_req	*opt_req;
	register struct opthdr		*opt;
	register struct so_so		*so;

	so = (struct so_so *)q->q_ptr;
	opt_req = (struct T_optmgmt_req *)mp->b_rptr;
	if (MSGBLEN(mp) < sizeof (*opt_req))
		return (-EINVAL);

	opt = (struct opthdr *)(mp->b_rptr + opt_req->OPT_offset);

	if (MSGBLEN(mp) < (opt_req->OPT_length + sizeof (*opt_req)))
		return (-EINVAL);

	if (opt->level != SOL_SOCKET)
		return (0);

	switch (opt_req->MGMT_flags) {
	case T_CHECK:
		/*
		 * Retrieve current value.
		 */
		switch (opt->name) {
		case SO_ERROR:
			*(int *)OPTVAL(opt) = so->so_error;
			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (int);
			so->so_error = 0;
			return (1);

		case SO_DEBUG:
		case SO_OOBINLINE:
		case SO_REUSEADDR:
		case SO_BROADCAST:
		case SO_KEEPALIVE:
		case SO_DONTROUTE:
		case SO_USELOOPBACK:
			*(int *)OPTVAL(opt) = so->udata.so_options & opt->name;
			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (int);
			return (1);

		case SO_LINGER: {
			struct linger  *l;

			if (opt->len != sizeof (struct linger))
				return (-EINVAL);

			l = (struct linger *)OPTVAL(opt);
			if (so->udata.so_options & SO_LINGER) {
				l->l_onoff = 1;
				l->l_linger = so->linger;
			} else {
				l->l_onoff = 0;
				l->l_linger = 0;
			}
			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (struct linger);
			return (1);
		}

		case SO_SNDBUF:
			*(int *)OPTVAL(opt) = so->sndbuf;
			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (int);
			return (1);

		case SO_RCVBUF:
			*(int *)OPTVAL(opt) = so->rcvbuf;
			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (int);
			return (1);

		case SO_SNDLOWAT:
			*(int *)OPTVAL(opt) = so->sndlowat;
			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (int);
			return (1);

		case SO_RCVLOWAT:
			*(int *)OPTVAL(opt) = so->rcvlowat;
			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (int);
			return (1);

		case SO_SNDTIMEO:
			*(int *)OPTVAL(opt) = so->sndtimeo;
			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (int);
			return (1);

		case SO_RCVTIMEO:
			*(int *)OPTVAL(opt) = so->rcvtimeo;
			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (int);
			return (1);

		case SO_PROTOTYPE:
			*(int *)OPTVAL(opt) = so->prototype;
			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (int);
			return (1);

		default:
			return (-ENOPROTOOPT);
		}

	case T_NEGOTIATE:
		/*
		 * We wait until the negotiated option comes
		 * back before setting most of these.
		 */
		switch (opt->name) {
		case SO_TYPE:
		case SO_ERROR:
			return (-ENOPROTOOPT);

		case SO_LINGER:
			if (opt->len != OPTLEN(sizeof (struct linger)))
				return (-EINVAL);
			break;

		case SO_OOBINLINE:
			if (*(int *)OPTVAL(opt))
				so->udata.so_options |= SO_OOBINLINE;
			else
				so->udata.so_options &= ~SO_OOBINLINE;

			opt_req->PRIM_type = T_OPTMGMT_ACK;
			opt->len = sizeof (int);
			return (1);

		case SO_DEBUG:
		case SO_USELOOPBACK:
		case SO_REUSEADDR:
		case SO_BROADCAST:
		case SO_KEEPALIVE:
		case SO_DONTROUTE:
		case SO_SNDBUF:
		case SO_RCVBUF:
		case SO_SNDLOWAT:
		case SO_RCVLOWAT:
		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
		case SO_PROTOTYPE:
			if (opt->len != OPTLEN(sizeof (int)))
				return (-EINVAL);
			break;

		default:
			return (-ENOPROTOOPT);
		}
	}
	/*
	 * Set so_option so that we know what
	 * we are dealing with.
	 */
	so->so_option = opt->name;
	return (0);
}

/*
 * The transport provider does not support the option,
 * but we must because it is a SOL_SOCKET option.
 * If value is non-zero, then the option should
 * be set, otherwise it is reset.
 */
static mblk_t *
_s_makeopt(so)
	register struct so_so	*so;
{
	register mblk_t		*bp;
	struct T_optmgmt_req	*opt_req;
	struct linger		*l;
	register struct opthdr	*opt;

	/*
	 * Get the saved request.
	 */
	opt_req = (struct T_optmgmt_req *)so->iocsave->b_cont->b_rptr;
	opt = (struct opthdr *)(so->iocsave->b_cont->b_rptr +
					opt_req->OPT_offset);
	switch (opt->name) {
	case SO_LINGER:
		l = (struct linger *)OPTVAL(opt);
		if (l->l_onoff) {
			so->udata.so_options |= SO_LINGER;
			so->linger = l->l_linger;
		} else {
			so->udata.so_options &= ~SO_LINGER;
			so->linger = 0;
		}

		opt_req->PRIM_type = T_OPTMGMT_ACK;
		opt->len = sizeof (struct linger);
		break;

	case SO_DEBUG:
	case SO_KEEPALIVE:
	case SO_DONTROUTE:
	case SO_USELOOPBACK:
	case SO_BROADCAST:
	case SO_REUSEADDR:
		if (*(int *)OPTVAL(opt))
			so->udata.so_options |= opt->name;
		else
			so->udata.so_options &= ~opt->name;

		opt_req->PRIM_type = T_OPTMGMT_ACK;
		opt->len = sizeof (int);
		break;

	case SO_SNDBUF:
		so->sndbuf = *(int *)OPTVAL(opt);

		opt_req->PRIM_type = T_OPTMGMT_ACK;
		opt->len = sizeof (int);
		break;

	case SO_RCVBUF:
		so->rcvbuf = *(int *)OPTVAL(opt);

		opt_req->PRIM_type = T_OPTMGMT_ACK;
		opt->len = sizeof (int);
		break;

	case SO_SNDLOWAT:
		so->sndlowat = *(int *)OPTVAL(opt);

		opt_req->PRIM_type = T_OPTMGMT_ACK;
		opt->len = sizeof (int);
		break;

	case SO_RCVLOWAT:
		so->rcvlowat = *(int *)OPTVAL(opt);

		opt_req->PRIM_type = T_OPTMGMT_ACK;
		opt->len = sizeof (int);
		break;

	case SO_SNDTIMEO:
		so->sndtimeo = *(int *)OPTVAL(opt);

		opt_req->PRIM_type = T_OPTMGMT_ACK;
		opt->len = sizeof (int);
		break;

	case SO_RCVTIMEO:
		so->rcvtimeo = *(int *)OPTVAL(opt);

		opt_req->PRIM_type = T_OPTMGMT_ACK;
		opt->len = sizeof (int);
		break;

	case SO_PROTOTYPE:
		so->prototype = *(int *)OPTVAL(opt);

		opt_req->PRIM_type = T_OPTMGMT_ACK;
		opt->len = sizeof (int);
		break;
	}

	bp = so->iocsave->b_cont;
	so->iocsave->b_cont = NULL;
	return (bp);
}

/*
 * The transport provider returned T_OPTMGMT_ACK,
 * copy the values it negotiated.
 */
static void
_s_setopt(mp, so)
	register mblk_t		*mp;
	register struct so_so	*so;
{
	struct T_optmgmt_ack	*opt_ack;
	register struct opthdr	*opt;
	register struct linger	*l;

	opt_ack = (struct T_optmgmt_ack *)mp->b_rptr;
	opt = (struct opthdr *)(mp->b_rptr + opt_ack->OPT_offset);

	switch (opt->name) {
	case SO_DEBUG:
	case SO_USELOOPBACK:
	case SO_REUSEADDR:
	case SO_BROADCAST:
	case SO_KEEPALIVE:
	case SO_DONTROUTE:
		if (*(int *)OPTVAL(opt))
			so->udata.so_options |= opt->name;
		else
			so->udata.so_options &= ~opt->name;
		break;

	case SO_LINGER:
		l = (struct linger *)OPTVAL(opt);
		if (l->l_onoff) {
			so->udata.so_options |= SO_LINGER;
			so->linger = l->l_linger;
		} else {
			so->udata.so_options &= ~SO_LINGER;
			so->linger = 0;
		}
		break;

	case SO_SNDBUF:
		so->sndbuf = *(int *)OPTVAL(opt);
		break;

	case SO_RCVBUF:
		so->rcvbuf = *(int *)OPTVAL(opt);
		break;

	case SO_SNDLOWAT:
		so->sndlowat = *(int *)OPTVAL(opt);
		break;

	case SO_RCVLOWAT:
		so->rcvlowat = *(int *)OPTVAL(opt);
		break;

	case SO_SNDTIMEO:
		so->sndtimeo = *(int *)OPTVAL(opt);
		break;

	case SO_RCVTIMEO:
		so->rcvtimeo = *(int *)OPTVAL(opt);
		break;

	case SO_PROTOTYPE:
		so->prototype = *(int *)OPTVAL(opt);
		break;
	}
}


/*
 * Set sizes of buffers
 */
#define	DEFSIZE	128
static long
_t_setsize(infosize)
	long	infosize;
{
	switch (infosize)
	{
		case -1:
			return (DEFSIZE);
		case -2:
			return (0);
		default:
			return (infosize);
	}
}

/*
 * Translate a TLI error into a system error as best we can.
 */
static ushort tli_errs[] = {
		0,		/* no error	*/
		EADDRNOTAVAIL,  /* TBADADDR	*/
		ENOPROTOOPT,	/* TBADOPT	*/
		EACCES,		/* TACCES	*/
		EBADF,		/* TBADF	*/
		EADDRNOTAVAIL,	/* TNOADDR	*/
		EPROTO,		/* TOUTSTATE	*/
		EPROTO,		/* TBADSEQ	*/
		0,		/* TSYSERR - will never get	*/
		EPROTO,		/* TLOOK - should never be sent by transport */
		EMSGSIZE,	/* TBADDATA	*/
		EMSGSIZE,	/* TBUFOVFLW	*/
		EPROTO,		/* TFLOW	*/
		EWOULDBLOCK,	/* TNODATA	*/
		EPROTO,		/* TNODIS	*/
		EPROTO,		/* TNOUDERR	*/
		EINVAL,		/* TBADFLAG	*/
		EPROTO,		/* TNOREL	*/
		EOPNOTSUPP,	/* TNOTSUPPORT	*/
		EPROTO,		/* TSTATECHNG	*/
};

static int
tlitosyserr(terr)
	register int	terr;
{
	if (terr > (sizeof (tli_errs) / sizeof (ushort)))
		return (EPROTO);
	else
		return ((int)tli_errs[terr]);
}

/*
 * This function will walk through the message block given
 * looking for a single data block large enough to hold
 * size bytes. If it finds one it will free the surrounding
 * blocks and return a pointer to the one of the appropriate
 * size. If no component of the passed in message is large enough,
 * then if the system can't provide one of suitable size the
 * passed in message block is untouched. If the system can provide
 * one then the passed in message block is freed.
 */
static mblk_t *
_s_getmblk(mp, size)
	register mblk_t		*mp;
	register size_t		size;
{
	register mblk_t		*nmp;
	register mblk_t		*bp;

	bp = mp;
	while (bp) {
		if (MBLKLEN(bp) >= (int)size) {
			bp->b_rptr = bp->b_wptr = bp->b_datap->db_base;
			while (mp && bp != mp) {
				/*
				 * Free each block up to the one
				 * we want.
				 */
				nmp = mp->b_cont;
				freeb(mp);
				mp = nmp;
			}
			if (bp->b_cont) {
				/*
				 * Free each block after the one
				 * we want.
				 */
				nmp = bp->b_cont;
				freemsg(nmp);
				bp->b_cont = 0;
			}
			return (bp);
		}
		bp = bp->b_cont;

	}
	if ((bp =  allocb(size, BPRI_MED)) == (mblk_t *)NULL) {
		/*
		 * But we have not touched mp.
		 */

		(void) strlog(SIMOD_ID, -1, 0, SL_TRACE,
				"_s_getmblk: No memory\n");

		return ((mblk_t *)NULL);
	} else {

#ifdef DEBUG
		(void) strlog(SIMOD_ID, -1, 0, SL_TRACE,
				"_s_getmblk: Allocated %d bytes\n", size);
#endif /* DEBUG */

		freemsg(mp);
		return (bp);
	}
}

static void
snd_ERRACK(q, bp, prim, serr)
	register queue_t	*q;
	register mblk_t		*bp;
	register int		serr;
	register int		prim;
{
	register struct T_error_ack	*tea;
	register struct so_so		*so;

	so = (struct so_so *)q->q_ptr;

	tea = (struct T_error_ack *) bp->b_rptr;
	bp->b_wptr += sizeof (struct T_error_ack);
	bp->b_datap->db_type = M_PCPROTO;
	tea->ERROR_prim = prim;
	tea->PRIM_type = T_ERROR_ACK;
	tea->TLI_error = TSYSERR;
	tea->UNIX_error = serr;
	qreply(q, bp);

	so->so_error = serr;
}

static void
snd_OKACK(q, mp, prim)
	register queue_t	*q;
	register mblk_t		*mp;
	register int		prim;
{
	register struct T_ok_ack	*ok_ack;

	mp->b_datap->db_type = M_PCPROTO;
	ok_ack = (struct T_ok_ack *)mp->b_rptr;
	mp->b_wptr += sizeof (struct T_ok_ack);
	ok_ack->CORRECT_prim = prim;
	ok_ack->PRIM_type = T_OK_ACK;
	qreply(q, mp);
}


static
so_init(so, info_ack)
	register struct so_so		*so;
	register struct T_info_ack	*info_ack;
{
	/*
	 * Common stuff.
	 */
	so->udata.servtype = info_ack->SERV_type;
	so->udata.tidusize = _t_setsize(info_ack->TIDU_size);
	so->udata.tsdusize = so->tp_info.tsdu =
		_t_setsize(info_ack->TSDU_size);

	so->udata.addrsize = so->tp_info.addr =
		_t_setsize(info_ack->ADDR_size);

	so->udata.optsize = so->tp_info.options =
		_t_setsize(info_ack->OPT_size);

	so->udata.etsdusize = so->tp_info.etsdu =
		_t_setsize(info_ack->ETSDU_size);

	switch (info_ack->SERV_type) {
	case T_CLTS:
		switch (info_ack->CURRENT_state) {
		case TS_UNBND:
			so->udata.so_state = 0;
			so->udata.so_options = 0;
			break;

		case TS_IDLE:
			so->udata.so_state |= SS_ISBOUND;
			so->udata.so_options = 0;
			break;

		default:
			return (EINVAL);
		}
		break;

	case T_COTS:
	case T_COTS_ORD:
		switch (info_ack->CURRENT_state) {
		case TS_UNBND:
			so->udata.so_state = 0;
			so->udata.so_options = 0;
			break;

		case TS_IDLE:
			so->udata.so_state |= SS_ISBOUND;
			so->udata.so_options = 0;
			break;

		case TS_DATA_XFER:
			so->udata.so_state |= (SS_ISBOUND|SS_ISCONNECTED);
			so->udata.so_options = 0;
			break;

		default:
			return (EINVAL);
		}
		break;

	default:
		return (EINVAL);
	}
	return (0);
}

static void
strip_zerolen(mp)
	register mblk_t	*mp;
{
	register mblk_t *bp = mp;

	/*
	 * Assumes the first mblk is never zero length,
	 * and is actually some kind of header.
	 *
	 * Changed to a while loop to make sure we check the last
	 * mblk.  (randyf 8/3/95)
	 */
	mp = mp->b_cont;
	while (mp) {
		if (MSGBLEN(mp) == 0) {
			bp->b_cont = mp->b_cont;
			mp->b_cont = NULL;
			freeb(mp);
			mp = bp->b_cont;
		} else {
			bp = mp;
			mp = mp->b_cont;
		}
	}
}

static void
save_addr(save, buf, len)
	register struct netbuf	*save;
	register char		*buf;
	register size_t		len;
{
	register size_t llen;

	llen = min(save->maxlen, len);

#ifdef DEBUG
	(void) strlog(SIMOD_ID, -1, 0, SL_TRACE,
			"save_addr: Copying %d bytes\n", llen);
#endif /* DEBUG */

	(void) bcopy(buf, save->buf, llen);
	save->len = llen;
}

static void
snd_ZERO(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	mp->b_datap->db_type = M_DATA;
	mp->b_rptr = mp->b_wptr = mp->b_datap->db_base;

	if (mp->b_cont) {
		freemsg(mp->b_cont);
		mp->b_cont = NULL;
	}

	socklog((struct so_so *)q->q_ptr,
			"snd_ZERO: Sending up zero length msg\n", 0);

	putnext(q, mp);
}

static void
snd_ERRORW(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	mp->b_datap->db_type = M_ERROR;
	mp->b_rptr = mp->b_wptr = mp->b_datap->db_base;

	*mp->b_wptr++ = NOERROR;
	*mp->b_wptr++ = EPIPE;

	socklog((struct so_so *)q->q_ptr,
			"snd_ERRORW: Sending up M_ERROR q:\n", (int)q->q_ptr);

	putnext(q, mp);
}

static void
snd_FLUSHR(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	mp->b_datap->db_type = M_FLUSH;
	mp->b_rptr = mp->b_wptr = mp->b_datap->db_base;
	*mp->b_wptr++ = FLUSHR;
	putnext(q, mp);
}

static void
snd_HANGUP(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	mp->b_rptr = mp->b_wptr = mp->b_datap->db_base;
	mp->b_datap->db_type = M_HANGUP;
	putnext(q, mp);
}

static void
snd_IOCNAK(q, mp, error)
	register queue_t	*q;
	register mblk_t		*mp;
	register int		error;
{
	register struct iocblk	*iocbp;
	register struct so_so	*so;

	mp->b_datap->db_type = M_IOCNAK;

	iocbp = (struct iocblk *)mp->b_rptr;
	so = (struct so_so *)q->q_ptr;
	iocbp->ioc_error = so->so_error = error;
	iocbp->ioc_count = 0;

	putnext(q, mp);
}

/*
 * The following complicated procedure is an attempt to get the
 * semantics right for closing the socket down.
 *
 * mp must be large enough to hold the M_ERROR message.
 */
static void
do_ERROR(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	register struct so_so	*so;

	so = (struct so_so *)q->q_ptr;

	/*
	 * Disconnect received. Send up new M_ERROR
	 * with read side set to disconnect error
	 * and write side set to EPIPE.
	 */

	socklog(so, "do_ERROR: Sending up M_ERROR with read error %d\n",
		so->so_error);

	mp->b_wptr = mp->b_rptr = mp->b_datap->db_base;
	mp->b_datap->db_type = M_ERROR;
	*mp->b_wptr++ = so->so_error ? so->so_error : NOERROR;
	*mp->b_wptr++ = EPIPE;
	putnext(q, mp);
}

#ifdef AF_UNIX
/*
 * Looks up the socket structure which has as
 * its local dev/ino the same as passed in.
 */
static struct so_so *
ux_findlink(addr, len)
	register char		*addr;
	register size_t		len;
{
	register struct so_so	*so;

	rw_enter(&so_ux_rwlock, RW_READER);

	for (so = so_ux_list; so != NULL; so = so->so_ux.next)
		if (bcmp(addr, (caddr_t)&so->lux_dev.addr, len) == 0)
			break;

	rw_exit(&so_ux_rwlock);

	return (so);
}

static void
ux_dellink(so)
	register struct so_so	*so;
{
	register struct so_so	*oso;

	rw_enter(&so_ux_rwlock, RW_WRITER);

	if ((so->so_ux.next == NULL) && (so->so_ux.prev == NULL) &&
	    (so != so_ux_list)) {
		/* not on the linked list - do nothing */
		rw_exit(&so_ux_rwlock);
		return;
	}

	if ((oso = so->so_ux.next) != (struct so_so *)NULL)
		oso->so_ux.prev = so->so_ux.prev;

	if ((oso = so->so_ux.prev) != (struct so_so *)NULL)
		oso->so_ux.next = so->so_ux.next;
	else
		so_ux_list = so->so_ux.next;

	/* Defend against multiple calls */
	so->so_ux.next = NULL;
	so->so_ux.prev = NULL;

	rw_exit(&so_ux_rwlock);
}

static void
ux_addlink(so)
	register struct so_so	*so;
{
	rw_enter(&so_ux_rwlock, RW_WRITER);

	so->so_ux.next = so_ux_list;
	so->so_ux.prev = NULL;
	if (so_ux_list)
		so_ux_list->so_ux.prev = so;
	so_ux_list = so;

	rw_exit(&so_ux_rwlock);
}

/*
 * When a T_BIND_ACK is received, copy back
 * both parts of the address into the right
 * places for the user.
 */
static void
ux_restoreaddr(so, mp, addr, addrlen)
	register struct so_so		*so;
	register mblk_t			*mp;
	register char			*addr;
	register size_t			addrlen;
{
	struct T_bind_ack		*bind_ack;
	struct bind_ux			*bind_ux;

	bind_ack = (struct T_bind_ack *)mp->b_rptr;
	bind_ux = (struct bind_ux *)(mp->b_rptr + bind_ack->ADDR_offset);

	/*
	 * Copy address actually bound to.
	 */
	(void) bcopy(addr, (caddr_t)&bind_ux->extaddr, addrlen);
	bind_ux->extsize = addrlen;

	/*
	 * Copy address the user thought was bound to.
	 */
	(void) bzero((caddr_t)&bind_ux->name, sizeof (bind_ux->name));
	(void) bcopy(so->laddr.buf, (caddr_t)&bind_ux->name,
			so->laddr.len);
	bind_ack->ADDR_length = sizeof (*bind_ux);
}

/*
 * In a T_CONN_REQ, save both parts
 * of the address.
 */
static void
ux_saveraddr(so, bind_ux)
	register struct so_so		*so;
	register struct bind_ux		*bind_ux;
{
	save_addr(&so->raddr, (caddr_t)&bind_ux->name,
		sizeof (struct sockaddr_un));

	(void) bcopy((caddr_t)&bind_ux->extaddr,
		(caddr_t)&so->rux_dev.addr, bind_ux->extsize);
	so->rux_dev.size = bind_ux->extsize;
}

/*
 * Fill in a T_UNITDATA_REQ address
 */
static void
fill_udata_req_addr(bp, addr, len)
	register mblk_t			*bp;
	register char			*addr;
	register size_t			len;
{
	register struct T_unitdata_req	*udata_req;

	udata_req = (struct T_unitdata_req *)bp->b_rptr;
	udata_req->DEST_length = len;
	udata_req->DEST_offset = sizeof (*udata_req);
	(void) bcopy(addr, (caddr_t)(bp->b_rptr + udata_req->DEST_offset), len);

	udata_req->PRIM_type = T_UNITDATA_REQ;
	udata_req->OPT_length = 0;
	udata_req->OPT_offset = 0;

	bp->b_datap->db_type = M_PROTO;
	bp->b_wptr = bp->b_rptr + sizeof (*udata_req) + len;
}

/*
 * Fill in a T_UNITDATA_IND address
 */
static void
fill_udata_ind_addr(bp, addr, len)
	register mblk_t			*bp;
	register char			*addr;
	register size_t			len;
{
	register struct T_unitdata_ind	*udata_ind;

	udata_ind = (struct T_unitdata_ind *)bp->b_rptr;
	udata_ind->SRC_length = len;
	udata_ind->SRC_offset = sizeof (*udata_ind);
	(void) bcopy(addr, (caddr_t)(bp->b_rptr + udata_ind->SRC_offset), len);

	bp->b_datap->db_type = M_PROTO;
	bp->b_wptr = bp->b_rptr + sizeof (*udata_ind) + len;
}
#endif /* AF_UNIX */

static mblk_t *
_s_getloaned(q, mp, size)
	register queue_t	*q;
	register mblk_t		*mp;
	register int		size;
{
	char			*buf;
	struct free_ptr		*urg_ptr;
	mblk_t			*bp;
	struct so_so		*so;

	so = (struct so_so *)q->q_ptr;

	/*
	 * Allocate the message that will be used
	 * in the callback routine to flush the
	 * band 1 message from the stream head.
	 */
	if ((bp = allocb(2, BPRI_MED)) == (mblk_t *)NULL) {
		recover(q, mp, 2);
		return ((mblk_t *)NULL);
	}

	/*
	 * Allocate the buffer to hold the OOB data.
	 */
	if ((buf = (char *)kmem_alloc(size, KM_NOSLEEP)) == NULL) {
		recover(q, mp, size);
		freeb(bp);
		return ((mblk_t *)NULL);
	}

	if ((so->udata.so_options) & SO_OOBINLINE) {
		register caddr_t	ptr;
		register size_t		left;
		register size_t		count;
		register mblk_t		*nmp;

		/*
		 * Copy the OOB data into
		 * the buffer, skipping the
		 * M_PROTO header.
		 */
		count = 0;
		left = size;
		ptr = buf;
		for (nmp = mp->b_cont; nmp; nmp = nmp->b_cont) {
			count = min(left, (size_t)MSGBLEN(nmp));
			bcopy((caddr_t)nmp->b_rptr, ptr, count);
			ptr += count;
			left -= count;
		}
	}

	/*
	 * Allocate the data structure we need to
	 * be passed to the callback routine when
	 * the esballoc'ed message is freed.
	 */
	if ((urg_ptr = (struct free_ptr *)kmem_alloc(sizeof (*urg_ptr),
				KM_NOSLEEP)) == NULL) {
		recover(q, mp, sizeof (*urg_ptr));
		kmem_free(buf, size);
		freeb(bp);
		return ((mblk_t *)NULL);
	}

	/*
	 * Initialize the free routine data structure,
	 * with the information it needs to do its
	 * tasks.
	 */
	urg_ptr->free_rtn.free_func = free_urg;
	urg_ptr->free_rtn.free_arg = (char *)urg_ptr;
	urg_ptr->buf = buf;
	urg_ptr->buflen = size;
	urg_ptr->mp = bp;

	/*
	 * Need to do the following because a free
	 * routine could be called when the
	 * queue pointer is invalid.
	 */
	urg_ptr->so = so;

	/*
	 * Get a class 0 data block
	 * and attach our buffer.
	 */
	if ((bp = esballoc((uchar_t *)buf, size, BPRI_MED,
			&urg_ptr->free_rtn)) == (mblk_t *)NULL) {
		recover(q, mp, sizeof (mblk_t));
		kmem_free(buf, size);
		freeb(urg_ptr->mp);
		kmem_free(urg_ptr, sizeof (*urg_ptr));

		return ((mblk_t *)NULL);
	}
	bp->b_wptr += size;

	mutex_enter(&so_global_lock);
	so->so_cbacks_outstanding++;
	mutex_exit(&so_global_lock);
	return (bp);
}

/*
 * Call back function associated with _s_getloaned().
 */
static void
free_urg(arg)
	register char		*arg;
{
	register struct free_ptr	*urg_ptr;
	register struct so_so		*so;
	register mblk_t			*bp;

	urg_ptr = (struct free_ptr *)arg;
	so = (struct so_so *)urg_ptr->so;

	socklog(so, "free_urg: hasoutofband %d\n", so->hasoutofband);

	/*
	 * Synchonize with sockmodclose() while
	 * we test the SC_WCLOSE flag.
	 */
	mutex_enter(&so_global_lock);

	if (so->closeflags & SC_WCLOSE) {
		/* Queue is gone. */
		/*
		 * Free the message that we would have
		 * done the M_FLUSH with.
		 */
		freeb(urg_ptr->mp);
		/*
		 * Free up the buffers.
		 */
		kmem_free(urg_ptr->buf, urg_ptr->buflen);
		kmem_free(urg_ptr, sizeof (*urg_ptr));

		so->hasoutofband--;
		ASSERT(so->so_cbacks_outstanding > 0);
		so->so_cbacks_outstanding--;
		if (so->so_cbacks_outstanding == 0 && !so->so_isaccepting) {
			socklog(so, "free_urg: freeing so_so\n", 0);

			so_dellink(so);
		}
		mutex_exit(&so_global_lock);
		return;
	}
	so->so_cbacks_inprogress++;
	ASSERT(so->so_cbacks_inprogress > 0); /* Wraparound check! */
	mutex_exit(&so_global_lock);

	enterq(so->rdq);

	so->hasoutofband--;
	if (so->hasoutofband == 0) {
		/*
		 * There is no more URG data pending,
		 * so we can flush the band 1 message.
		 */
		bp = urg_ptr->mp;
		bp->b_datap->db_type = M_FLUSH;
		*bp->b_wptr++ = FLUSHR|FLUSHBAND;
		*bp->b_wptr++ = 1;	/* Band to flush */

		socklog(so, "free_urg: sending up M_FLUSH - band 1\n", 0);

		putnext(so->rdq, bp);
	} else
		freeb(urg_ptr->mp);

	mutex_enter(&so_global_lock);
	ASSERT(so->so_cbacks_outstanding > 0);
	so->so_cbacks_outstanding--;
	ASSERT(so->so_cbacks_inprogress > 0);
	so->so_cbacks_inprogress--;
	mutex_exit(&so_global_lock);
	leaveq(so->rdq);

	/*
	 * Free up the buffers.
	 */
	kmem_free(urg_ptr->buf, urg_ptr->buflen);
	kmem_free(urg_ptr, sizeof (*urg_ptr));
}

static int
do_urg_outofline(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	register struct so_so	*so;
	register mblk_t		*bp, *fbp;
	int			size;
	register queue_t	*qp;
	register mblk_t		*first_mb;
	int			first_mb_band = 0;
	int			last_mb_band = 0;

	so = (struct so_so *)q->q_ptr;
	size = sizeof (struct T_exdata_ind);

	/*
	 * Find the stream head.
	 * Queue traversal is safe here, since the
	 * framework assures the stablility of q_next.
	 */

	for (qp = q->q_next; qp->q_next; qp = qp->q_next)
		;

	mutex_enter(QLOCK(qp));
	if ((first_mb = qp->q_first) != NULL) {
		first_mb_band = first_mb->b_band;
		last_mb_band = qp->q_last->b_band;
	}
	mutex_exit(QLOCK(qp));

	/*
	 * We have to deal with a special case up front:  If we have
	 * no outstanding urgent data, and if there is still a band 1
	 * message at the stream head, we want to do a FLUSHBAND on band 1
	 * so that a new SIGURG will be delivered.
	 */
	if (first_mb != NULL && first_mb_band == 1 && so->oob == NULL) {
		socklog(so, "do_urg_outofline: will flush band 1 message\n", 0);
		if ((fbp = allocb(sizeof (short), BPRI_HI)) == NULL) {
			socklog(so, "do_urg_outofline: allocb(2) failed!\n", 0);
			recover(q, mp, sizeof (short));
			return (-1);
		}
		fbp->b_datap->db_type = M_FLUSH;
		*fbp->b_wptr++ = FLUSHR|FLUSHBAND;
		*fbp->b_wptr++ = 1;
		/*
		 * Now fake it so it doesn't look like we have a band 1 message
		 */
		if (last_mb_band == 0) {
			first_mb_band = 0;
		} else {
			first_mb = NULL;
		}
	} else {
		fbp = NULL;
	}
	if (first_mb == (mblk_t *)NULL) {
		mblk_t	*nbp;
		mblk_t	*nmp;

		socklog(so, "do_urg_outofline: nothing at stream head\n", 0);

		/*
		 * This is to make SIGURG happen,
		 * and I_ATMARK to return TRUE.
		 */
		if ((nmp = _s_getband1(q, mp)) == (mblk_t *)NULL) {
			if (fbp) {
				freeb(fbp);
			}
			return (-1);
		}
		nmp->b_flag |= MSGMARK;

		/*
		 * This is just to make SIGIO happen,
		 * it will be flushed immediately.
		 */
		if ((bp = allocb(1, BPRI_MED)) == (mblk_t *)NULL) {
			recover(q, mp, 1);
			freemsg(nmp);
			if (fbp) {
				freeb(fbp);
			}
			return (-1);
		}
		bp->b_datap->db_type = M_PROTO;

		/*
		 * Allocate a loaned message for future
		 * use when normal data is received.
		 */
		if ((nbp = _s_getloaned(q, mp, size)) == (mblk_t *)NULL) {
			freemsg(bp);
			freemsg(nmp);
			if (fbp) {
				freeb(fbp);
			}
			return (-1);
		}
		nbp->b_datap->db_type = M_PROTO;
		*(long *)nbp->b_rptr = T_EXDATA_IND;

		/*
		 * Change mp into an M_FLUSH,
		 * after first saving the OOB data.
		 */
		if (so->oob) {
			socklog(so, "do_urg_outofline: leak oob\n", 0);
			freemsg(so->oob);
		}
		so->oob = mp->b_cont;
		mp->b_cont = (mblk_t *)NULL;

		mp->b_datap->db_type = M_FLUSH;
		mp->b_rptr = mp->b_wptr = mp->b_datap->db_base;
		*mp->b_wptr++ = FLUSHR|FLUSHBAND;
		*mp->b_wptr++ = 0;	/* Band to flush */

		so->urg_msg = nbp;	/* Save free msg	*/

		socklog(so, "do_urg_outofline: sending band 0, 1, M_FLUSH\n",
			0);

		if (fbp) {
			putnext(q, fbp);
		}
		putnext(q, bp);		/* Send up band 0	*/
		putnext(q, nmp);	/* Send up band 1	*/
		putnext(q, mp);		/* Flush band 0		*/

		so->hasoutofband++;

	} else {
		/*
		 * Something on stream head's read queue.
		 */
		if (first_mb_band == 1 && last_mb_band == 0) {

			socklog(so, "do_urg_outofline: both bands\n", 0);

			ASSERT(fbp == NULL);
			/*
			 * Both bands at stream head.
			 * Get the marked message buffer,
			 * it is used to make I_ATMARK
			 * work properly and to generate SIGIO.
			 */
			if ((bp = _s_getloaned(q, mp, size)) == (mblk_t *)NULL)
				return (-1);

			/*
			 * _s_getloaned() has already incremented
			 * bp->b_wptr by "size".
			 */
			bp->b_datap->db_type = M_PROTO;
			*(long *)bp->b_rptr = T_EXDATA_IND;
			bp->b_flag |= MSGMARK;

			socklog(so, "do_urg_outofline: sending up band 0\n", 0);

			putnext(q, bp);

			/*
			 * Now save away the OOB data.
			 */
			if (so->oob) {
				socklog(so, "do_urg_outofline: leak oob\n", 0);
				freemsg(so->oob);
			}
			so->oob = mp->b_cont;
			mp->b_cont = NULL;
			freeb(mp);

			so->hasoutofband++;

		} else if (first_mb_band == 0 && last_mb_band == 0) {

			socklog(so, "do_urg_outofline: Only normal data\n", 0);

			/*
			 * Only normal data at
			 * stream head.
			 * Get the marked message buffer,
			 * it is used to make I_ATMARK
			 * work properly and to generate SIGIO.
			 */
			if ((bp = _s_getloaned(q, mp, size)) == NULL) {
				if (fbp) {
					freeb(fbp);
				}
				return (-1);
			}

			/*
			 * _s_getloaned() has already incremented
			 * bp->b_wptr by "size".
			 */
			bp->b_datap->db_type = M_PROTO;
			*(long *)bp->b_rptr = T_EXDATA_IND;
			bp->b_flag |= MSGMARK;

			/*
			 * Now save away the OOB data,
			 * and re-use mp for the band 1 message.
			 */
			if (so->oob) {
				socklog(so, "do_urg_outofline: leak oob\n", 0);
				freemsg(so->oob);
			}
			so->oob = mp->b_cont;
			mp->b_cont = NULL;

			mp->b_band = 1;
			mp->b_flag |= MSGNOGET;

			socklog(so, "do_urg_outline: Sending up band 0, 1\n",
				0);

			if (fbp) {
				putnext(q, fbp);
			}
			putnext(q, bp);
			putnext(q, mp);

			so->hasoutofband++;

		} else if (first_mb_band == 1 &&
					last_mb_band == 1) {

			socklog(so, "do_urg_outofline: Just band 1 present\n",
			    0);

			ASSERT(fbp == NULL);
			/*
			 * Only band 1 at stream head -
			 * just save the OOB data.
			 */
			if (so->oob) {
				socklog(so, "do_urg_outofline: leak oob\n", 0);
				freemsg(so->oob);
			}
			so->oob = mp->b_cont;
			mp->b_cont = NULL;
			freeb(mp);
		}
	}
	return (0);
}

static int
do_urg_inline(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	register struct so_so	*so;
	register mblk_t		*bp;
	int			size;

	so = (struct so_so *)q->q_ptr;

	socklog(so, "do_urg_inline: hasoutofband = %d\n", so->hasoutofband);

	/*
	 * Get the class 0 mblk which will
	 * hold the OOB data, and generate
	 * the SIGIO.
	 */
	size = msgdsize(mp);
	if ((bp = _s_getloaned(q, mp, size)) == (mblk_t *)NULL)
		return (-1);
	bp->b_flag |= MSGMARK;

	socklog(so, "do_urg_inline: sending up band 0 msg\n", 0);

	putnext(q, bp);

	if (so->hasoutofband == 0) {
		/*
		 * Re-use mp for the band 1
		 * message. The contents were
		 * copied by _s_getloaned().
		 */
		freemsg(mp->b_cont);
		mp->b_cont = (mblk_t *)NULL;
		mp->b_band = 1;
		mp->b_flag |= MSGNOGET;

		socklog(so, "do_urg_inline: sending up band 1 msg\n", 0);

		putnext(q, mp);

	} else
		freemsg(mp);

	so->hasoutofband++;

	return (0);
}

static mblk_t *
_s_getband1(q, mp)
	register queue_t	*q;
	register mblk_t		*mp;
{
	register mblk_t		*nmp;

	/*
	 * Get the band 1 message block.
	 * This will generate SIGURG and
	 * make select return TRUE on exception
	 * events. It is never actually seen
	 * by the socket library or the application.
	 */
	if ((nmp = _s_getmblk((mblk_t *)NULL, 1)) == (mblk_t *)NULL) {
		recover(q, mp, 1);
		return ((mblk_t *)NULL);
	}

	nmp->b_datap->db_type = M_PROTO;
	nmp->b_band = 1;
	nmp->b_flag |= MSGNOGET;
	nmp->b_wptr += 1;

	return (nmp);
}

static mblk_t *
do_esbzero_err(q)
	register queue_t	*q;
{
	char			*buf;
	struct free_ptr		*zero_ptr;
	mblk_t			*bp;
	int			size;
	struct so_so		*so;

	so = (struct so_so *)q->q_ptr;

	socklog(so, "do_esbzero_err: Entered\n", 0);

	/*
	 * Allocate the M_ERROR message.
	 */
	if ((bp = allocb(2, BPRI_MED)) == (mblk_t *)NULL)
		return ((mblk_t *)NULL);

	/*
	 * Allocate the buffer.
	 */
	size = 1;
	if ((buf = (char *)kmem_alloc(size, KM_NOSLEEP)) == NULL) {
		freeb(bp);
		return ((mblk_t *)NULL);
	}

	/*
	 * Allocate the data structure we need to
	 * be passed to the callback routine when
	 * the esballoc'ed message is freed.
	 */
	if ((zero_ptr = (struct free_ptr *)kmem_alloc(sizeof (*zero_ptr),
				KM_NOSLEEP)) == NULL) {
		kmem_free(buf, size);
		freeb(bp);
		return ((mblk_t *)NULL);
	}

	/*
	 * Initialize the free routine data structure,
	 * with the information it needs to do its
	 * tasks.
	 */
	zero_ptr->free_rtn.free_func = free_zero_err;
	zero_ptr->free_rtn.free_arg = (char *)zero_ptr;
	zero_ptr->buf = buf;
	zero_ptr->buflen = size;
	zero_ptr->mp = bp;

	/*
	 * Need to do the following because a free
	 * routine could be called when the
	 * queue pointer is invalid.
	 */
	zero_ptr->so = so;

	/*
	 * Get a class 0 data block
	 * and attach our buffer.
	 */
	if ((bp = esballoc((uchar_t *)buf, size, BPRI_MED,
			&zero_ptr->free_rtn)) == (mblk_t *)NULL) {
		kmem_free(buf, size);
		freeb(zero_ptr->mp);
		kmem_free(zero_ptr, sizeof (*zero_ptr));

		return ((mblk_t *)NULL);
	}

	/*
	 * need to lock this.. if not some other
	 * thread (e.g. one of our esballoc free routines)
	 * may be modifiying at the same time
	 */
	mutex_enter(&so_global_lock);
	so->so_cbacks_outstanding++;
	mutex_exit(&so_global_lock);
	return (bp);
}

static mblk_t *
do_esbzero_zero(q)
	register queue_t	*q;
{
	char			*buf;
	struct free_ptr		*zero_ptr;
	mblk_t			*bp;
	int			size;
	struct so_so		*so;

	so = (struct so_so *)q->q_ptr;

	socklog(so, "do_esbzero_zero: Entered\n", 0);

	/*
	 * Allocate the buffer.
	 */
	size = 1;
	if ((buf = (char *)kmem_alloc(size, KM_NOSLEEP)) == NULL) {
		return ((mblk_t *)NULL);
	}

	/*
	 * Allocate the data structure we need to
	 * be passed to the callback routine when
	 * the esballoc'ed message is freed.
	 */
	if ((zero_ptr = (struct free_ptr *)kmem_alloc(sizeof (*zero_ptr),
				KM_NOSLEEP)) == NULL) {
		kmem_free(buf, size);
		return ((mblk_t *)NULL);
	}

	/*
	 * Initialize the free routine data structure,
	 * with the information it needs to do its
	 * tasks.
	 */
	zero_ptr->free_rtn.free_func = free_zero_zero;
	zero_ptr->free_rtn.free_arg = (char *)zero_ptr;
	zero_ptr->buf = buf;
	zero_ptr->buflen = size;
	zero_ptr->mp = NULL;

	/*
	 * Need to do the following because a free
	 * routine could be called when the
	 * queue pointer is invalid.
	 */
	zero_ptr->so = so;

	/*
	 * Get a class 0 data block
	 * and attach our buffer.
	 */
	if ((bp = esballoc((uchar_t *)buf, size, BPRI_MED,
			&zero_ptr->free_rtn)) == (mblk_t *)NULL) {
		kmem_free(buf, size);
		kmem_free(zero_ptr, sizeof (*zero_ptr));

		return ((mblk_t *)NULL);
	}

	/*
	 * need to lock this.. if not some other
	 * thread (e.g. one of our esballoc free routines)
	 * may be modifiying at the same time
	 */
	mutex_enter(&so_global_lock);
	so->so_cbacks_outstanding++;
	mutex_exit(&so_global_lock);
	return (bp);
}

/*
 * This routine is called when a zero length
 * message, sent to the stream head because a
 * disconnect had been received on a stream with
 * read data pending, is freed. It sends up
 * an M_ERROR to close the stream and make
 * reads return the correct error.
 */
static void
free_zero_err(arg)
	register char		*arg;
{
	register struct free_ptr	*zero_ptr;
	register struct so_so		*so;

	zero_ptr = (struct free_ptr *)arg;
	so = (struct so_so *)zero_ptr->so;

	socklog(so, "free_zero_err: entered\n", 0);

	/*
	 * Synchonize with sockmodclose() while
	 * we test the SC_WCLOSE flag. We need to
	 * grab the lock as a writer since we
	 * change the closeflags and so_cbacks_outstanding fields
	 * in the so structure.
	 */
	mutex_enter(&so_global_lock);
	if (so->closeflags & SC_WCLOSE) {
		/* Queue is gone */
		freeb(zero_ptr->mp);
		/*
		 * Free up the buffers.
		 */
		kmem_free(zero_ptr->buf, zero_ptr->buflen);
		kmem_free(zero_ptr, sizeof (*zero_ptr));

		ASSERT(so->so_cbacks_outstanding > 0);
		so->so_cbacks_outstanding--;
		if (so->so_cbacks_outstanding == 0 && !so->so_isaccepting) {
			socklog(so, "free_zero_err: freeing so_so\n", 0);
			so_dellink(so);
		}
		mutex_exit(&so_global_lock);
		return;
	}
	so->so_cbacks_inprogress++;
	ASSERT(so->so_cbacks_inprogress > 0); /* Wraparound check! */
	mutex_exit(&so_global_lock);

	enterq(so->rdq);

	socklog(so, "free_zero_err: calling do_ERROR\n", 0);

	do_ERROR(so->rdq, zero_ptr->mp);

	/*
	 * need to lock this.. if not some other
	 * thread (e.g. one of our esballoc free routines)
	 * may be modifiying at the same time
	 */
	mutex_enter(&so_global_lock);
	ASSERT(so->so_cbacks_outstanding > 0);
	so->so_cbacks_outstanding--;
	ASSERT(so->so_cbacks_inprogress > 0);
	so->so_cbacks_inprogress--;
	mutex_exit(&so_global_lock);
	leaveq(so->rdq);

	/*
	 * Free up the buffers.
	 */
	kmem_free(zero_ptr->buf, zero_ptr->buflen);
	kmem_free(zero_ptr, sizeof (*zero_ptr));
}

/*
 * This routine is called when a zero length
 * message, sent to the stream head because a
 * ordrel had been received on a stream with
 * read data pending, is freed. It sends up
 * another zero length message.
 */
static void
free_zero_zero(arg)
	register char		*arg;
{
	register struct free_ptr	*zero_ptr;
	register struct so_so		*so;
	mblk_t				*mp;

	zero_ptr = (struct free_ptr *)arg;
	so = (struct so_so *)zero_ptr->so;

	socklog(so, "free_zero_zero: entered\n", 0);

	ASSERT(zero_ptr->mp == NULL);
	/*
	 * Synchonize with sockmodclose() while
	 * we test the SC_WCLOSE flag. We need to
	 * grab the lock as a writer since we
	 * change the closeflags and so_cbacks_outstanding fields
	 * in the so structure.
	 */
	mutex_enter(&so_global_lock);
	if (so->closeflags & SC_WCLOSE) {
		/* Queue is gone */
		/*
		 * Free up the buffers.
		 */
		kmem_free(zero_ptr->buf, zero_ptr->buflen);
		kmem_free(zero_ptr, sizeof (*zero_ptr));

		ASSERT(so->so_cbacks_outstanding > 0);
		so->so_cbacks_outstanding--;
		if (so->so_cbacks_outstanding == 0 && !so->so_isaccepting) {
			socklog(so, "free_zero_zero: freeing so_so\n", 0);
			so_dellink(so);
		}
		mutex_exit(&so_global_lock);
		return;
	}
	so->so_cbacks_inprogress++;
	ASSERT(so->so_cbacks_inprogress > 0); /* Wraparound check! */
	mutex_exit(&so_global_lock);

	enterq(so->rdq);

	socklog(so, "free_zero_zero: calling do_esbzero_zero\n", 0);

	mp = do_esbzero_zero(so->rdq);
	if (mp)
		putnext(so->rdq, mp);

	mutex_enter(&so_global_lock);
	ASSERT(so->so_cbacks_outstanding > 0);
	so->so_cbacks_outstanding--;
	ASSERT(so->so_cbacks_inprogress > 0);
	so->so_cbacks_inprogress--;
	mutex_exit(&so_global_lock);
	leaveq(so->rdq);

	/*
	 * Free up the buffers.
	 */
	kmem_free(zero_ptr->buf, zero_ptr->buflen);
	kmem_free(zero_ptr, sizeof (*zero_ptr));
}

static void
socklog(so, str, arg)
	struct so_so	*so;
	char		*str;
	int		arg;
{
	if (so->udata.so_options & SO_DEBUG || dosocklog & 1) {
		if (dosocklog & 2)
			(void) cmn_err(CE_CONT, str, arg);
		else
			(void) strlog(SIMOD_ID, so->so_id, 0,
				SL_TRACE, str, arg);
	}
}

static void
so_dellink(so)
	register struct so_so	*so;
{
	register struct so_so	*next;

	ASSERT(MUTEX_HELD(&so_global_lock));

	ASSERT(!so->so_isaccepting && !so->so_cbacks_outstanding);
	/*
	 * Check if this is a listener that has an acceptor left
	 * in so_isaccepting state in which case we clear the isaccepting
	 * so that the acceptor can close.
	 */
	if (so->so_acceptor != NULL) {
		struct so_so *oso = so->so_acceptor;
		ASSERT(oso->so_isaccepting);
		so->so_acceptor = NULL;
		oso->so_isaccepting = 0;
		if ((oso->closeflags & SC_WCLOSE) &&
		    oso->so_cbacks_outstanding == 0) {
			socklog(so, "so_dellink: freeing oso so_so\n", 0);
			so_dellink(oso);
		}
	}

	if ((next = so->so_next) != NULL)
		next->so_ptpn = so->so_ptpn;
	*(so->so_ptpn) = next;

	so_cnt--;
	if (so->rdq != NULL)
		so->rdq->q_ptr = WR(so->rdq)->q_ptr = NULL;

	kmem_free(so, sizeof (struct so_so));
	module_keepcnt--;
}

static void
so_addlink(so)
	register struct so_so	*so;
{
	queue_t *driverq;
	struct so_so **sop;
	struct so_so	*next;

	/*
	 * Find my driver's read queue (for T_CON_RES handling)
	 */
	driverq = WR(so->rdq);
	while (SAMESTR(driverq))
		driverq = driverq->q_next;

	driverq = RD(driverq);

	sop = &so_hash[SO_HASH(driverq)];

	so->so_driverq = driverq;

	mutex_enter(&so_global_lock);
	if ((next = *sop) != NULL)
		next->so_ptpn = &so->so_next;
	so->so_next = next;
	so->so_ptpn = sop;
	*sop = so;

	so_cnt++;

	mutex_exit(&so_global_lock);
}

static struct so_so *
so_findlink(driverq)
	queue_t *driverq;
{
	register struct so_so	*so;

	ASSERT(MUTEX_HELD(&so_global_lock));

	for (so = so_hash[SO_HASH(driverq)]; so != NULL; so = so->so_next) {
		if (so->so_driverq == driverq) {
			break;
		}
	}
	return (so);
}


/*
 * This function is called when a T_OK_ACK
 * response is recived from the transport
 * provider after it has received a T_CONN_RES.
 */
static void
t_ok_ack_get(so)
	struct so_so		*so;
{
	struct so_so		*oso;

	socklog(so, "t_ok_ack_get: Entered\n", 0);

	mutex_enter(&so_global_lock);
	oso = so->so_acceptor;
	if (oso == NULL) {
		mutex_exit(&so_global_lock);
		/* Spurious T_OK_ACK from transport? */
		return;
	}
	ASSERT(oso->so_isaccepting);
	/*
	 * Make sure the other endpoint (resfd) has
	 * not closed in the meantime.
	 */
	if ((oso->closeflags & SC_WCLOSE) && oso->so_cbacks_outstanding == 0) {
		so->so_acceptor = NULL;
		oso->so_isaccepting = 0;
		socklog(so, "t_ok_ack_get: freeing so_so\n", 0);
		so_dellink(oso);
		mutex_exit(&so_global_lock);
		return;
	}

	/*
	 * All is well, update the state.
	 */
	so->so_acceptor = NULL;
	/*
	 * check to make sure a T_DISCON_IND or T_ORDREL_IND hasn't
	 * come up
	 */
	if ((oso->udata.so_state & (SS_CANTRCVMORE | SS_CANTSENDMORE)) == 0)
		oso->udata.so_state |= SS_ISCONNECTED;
	oso->so_isaccepting = 0;
	mutex_exit(&so_global_lock);
}

static void
sockmod_timer(q)
	queue_t	*q;
{
	struct so_so *so = (struct so_so *)q->q_ptr;

	ASSERT(so);

	if (q->q_flag & QREADR) {
		ASSERT(so->rtimoutid);
		so->rtimoutid = 0;
	} else {
		ASSERT(so->wtimoutid);
		so->wtimoutid = 0;
	}
	enableok(q);
	qenable(q);
}

static void
sockmod_buffer(q)
	queue_t	*q;
{
	struct so_so *so = (struct so_so *)q->q_ptr;

	ASSERT(so);

	if (q->q_flag & QREADR) {
		ASSERT(so->rbufcid);
		so->rbufcid = 0;
	} else {
		ASSERT(so->wbufcid);
		so->wbufcid = 0;
	}
	enableok(q);
	qenable(q);
}

static void
recover(q, mp, size)
	queue_t		*q;
	mblk_t		*mp;
	int		size;
{
	struct so_so	*so;
	int		id;

	so = q->q_ptr;

	/*
	 * Avoid re-enabling the queue.
	 */
	if (mp->b_datap->db_type == M_PCPROTO)
		mp->b_datap->db_type = M_PROTO;
	noenable(q);
	(void) putbq(q, mp);

	/*
	 * Make sure there is at most one outstanding request per queue.
	 */
	if (q->q_flag & QREADR) {
		if (so->rtimoutid || so->rbufcid) {
			socklog(so, "recover %x: pending\n", (int)q);
			return;
		}
	} else {
		if (so->wtimoutid || so->wbufcid) {
			socklog(so, "recover %x: pending\n", (int)q);
			return;
		}
	}
	socklog(so, "recover %d bytes\n", size);
	if (!(id = qbufcall(RD(q), size, BPRI_MED, sockmod_buffer, (long)q))) {
		id = qtimeout(RD(q), sockmod_timer, (caddr_t)q, SIMWAIT);
		if (q->q_flag & QREADR)
			so->rtimoutid = id;
		else
			so->wtimoutid = id;
	} else {
		if (q->q_flag & QREADR)
			so->rbufcid = id;
		else
			so->wbufcid = id;
	}
}

static mblk_t *
create_tconn_ind(addr, addrlen, optp, optlen, seqnum)
register char *addr;
register size_t addrlen;
register char *optp;
register size_t optlen;
register long seqnum;
{
	mblk_t *bp;
	struct T_conn_ind *nconn_ind;
	size_t size;

	size = sizeof (struct T_conn_ind) + addrlen;
	if (optlen) {
		size += SI_ALIGN(size);
		size += optlen;
	}
	if ((bp = _s_getmblk((mblk_t *)NULL,
			size)) == (mblk_t *)NULL) {
		return (NULL);
	}

	bp->b_datap->db_type = M_PROTO;

	nconn_ind = (struct T_conn_ind *)bp->b_rptr;
	nconn_ind->PRIM_type = T_CONN_IND;
	nconn_ind->SEQ_number = seqnum;
	nconn_ind->SRC_length = addrlen;
	nconn_ind->SRC_offset =	sizeof (struct T_conn_ind);
	bcopy((caddr_t)addr,
		(caddr_t)(bp->b_rptr + nconn_ind->SRC_offset), addrlen);
	if (optlen) {
		nconn_ind->OPT_length = optlen;
		nconn_ind->OPT_offset =	SI_ALIGN(nconn_ind->SRC_offset +
						nconn_ind->SRC_length);
		(void) bcopy((caddr_t) optp,
				(caddr_t)(bp->b_rptr + nconn_ind->OPT_offset),
				optlen);
	}
	bp->b_wptr = bp->b_rptr + size;
	return (bp);
}


static void
copy_o_si_udata(currentp, oldp)
struct si_udata *currentp;
struct o_si_udata *oldp;
{
	oldp->tidusize = currentp->tidusize;
	oldp->addrsize = currentp->addrsize;
	oldp->optsize = currentp->optsize;
	oldp->etsdusize = currentp->etsdusize;
	oldp->servtype = currentp->servtype;
	oldp->so_state = currentp->so_state;
	oldp->so_options = currentp->so_options;
	oldp->tsdusize = currentp->tsdusize;
}
