/*
 * Copyright (C) by Sun Microsystems Inc.
 */

#pragma ident "@(#)tl.c 1.19	95/07/27 SMI"

/*
 *  Multithreaded STREAMS Local Transport Provider
 *
 */


#include	<sys/types.h>
#include	<sys/stream.h>
#include	<sys/stropts.h>
#include	<sys/tihdr.h>
#include	<sys/tiuser.h>
#include	<sys/strlog.h>
#include	<sys/log.h>
#include	<sys/debug.h>
#include	<sys/cred.h>
#include	<sys/errno.h>
#include	<sys/kmem.h>
#include	<sys/mkdev.h>
#include	<sys/tl.h>
#include	<sys/stat.h>
#include	<sys/conf.h>
#include	<sys/modctl.h>
#include	<sys/strsun.h>
#include	<sys/socket.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>

/*
 * TBD List
 * 11. Eliminate te_oconp field (use te_state instead)
 * 14 Eliminate state changes through table
 * 16. AF_UNIX socket options
 * 17. connect() for ticlts
 * 18. support for"netstat" to show AF_UNIX plus TLI local
 *	transport connections
 * 20. clts flow controls - optimize single sender
 * 21. sanity check to flushing on sending M_ERROR
 */

/*
 * CONSTANT DECLARATIONS
 * --------------------
 */

/*
 * Local declarations
 */
#ifdef TL_DEBUG
#define	NEXTSTATE(EV, ST)	change_and_log_state(EV, ST)
static int change_and_log_state(int, int);
#else
#define	NEXTSTATE(EV, ST)	ti_statetbl[EV][ST]
#endif

#define	NR	127
#define	TL_DEFAULTADDRSZ sizeof (int)
#define	TL_MAXQLEN	48	/* Max conn indications allowed */
#define	BADSEQNUM	(-1)	/* initial seq number used by T_DICON_IND */
#define	TL_BUFWAIT	(1*HZ)	/* wait for allocb buffer timeout */
#define	TL_CLTS_CACHADDR_MAX 32	/* max size of addr cache for clts provider */
#define	TL_TIDUSZ (64*1024)	/* tidu size when "strmsgz" is unlimited (0) */

/*
 * Definitions for module_info
 */
#define		TL_ID		(104)		/* module ID number */
#define		TL_NAME		"tl"		/* module name */
#define		TL_MINPSZ	(0)		/* min packet size */
#define		TL_MAXPSZ	INFPSZ 		/* max packet size ZZZ */
#define		TL_HIWAT	(16*1024)	/* hi water mark */
#define		TL_LOWAT	(256)		/* lo water mark */

/*
 * Definition of minor numbers/modes for new transport provider modes
 */
#define		TL_TICOTS	0	/* connection oriented transport */
#define		TL_TICOTSORD 	1	/* COTS w/ orderly release */
#define		TL_TICLTS 	2	/* connectionless transport */
#define		TL_MINOR_START	(TL_TICLTS+1) /* start of minor numbers */

/*
 * LOCAL MACROS
 */

#define	TL_ALIGN(p)	(((unsigned long)(p) + (sizeof (long)-1))\
						&~ (sizeof (long)-1))
#define	TL_EQADDR(ap1, ap2)	((ap1)->ta_alen != (ap2)->ta_alen ? 0 : \
		(bcmp((ap1)->ta_abuf, (ap2)->ta_abuf, (ap2)->ta_alen) == 0))

/*
 * EXTERNAL VARIABLE DECLARATIONS
 * -----------------------------
 */
/*
 * state table defined in the OS space.c
 */
extern	char	ti_statetbl[TE_NOEVENTS][TS_NOSTATES];

/*
 * used for TIDU size - defined in sun/conf/param.c
 */
extern int strmsgsz;

/*
 * STREAMS DRIVER ENTRY POINTS PROTOTYPES
 */
static int tl_open(queue_t *, dev_t *, int, int, cred_t *);
static int tl_close(queue_t *, int, cred_t *);
static void tl_wput(queue_t *, mblk_t *);
static void tl_wsrv(queue_t *);
static void tl_rsrv(queue_t *);
static int tl_identify(dev_info_t *);
static int tl_attach(dev_info_t *, ddi_attach_cmd_t);
static int tl_detach(dev_info_t *, ddi_detach_cmd_t);
static int tl_info(dev_info_t *, ddi_info_cmd_t, void *, void **);


/*
 * GLOBAL DATA STRUCTURES AND VARIABLES
 * -----------------------------------
 */


/*
 *	transport addr structure
 */
typedef struct tl_addr {
	long	ta_alen;		/* length of abuf */
	char	*ta_abuf;		/* the addr itself */
} tl_addr_t;

/*
 *	transport endpoint structure
 */
typedef struct tl_endpt {
	struct tl_endpt *te_nextp; 	/* next in list of open endpoints */
	struct tl_endpt *te_prevp;	/* prev in list of open endpoints */
	int 	te_state;		/* TPI state of endpoint */
	int	te_mode;		/* ticots, ticotsord or ticlts */
	queue_t	*te_rq;			/* stream read queue */
	tl_addr_t te_ap;		/* addr bound to this endpt */
	minor_t	te_minor;		/* minor number */
	unsigned int te_flag;		/* flag field */
	unsigned long te_qlen;		/* max conn requests equests */
	unsigned long te_nicon;		/* count of conn requests */
	struct tl_endpt *te_iconp; 	/* list of conn indications pending */
	struct tl_endpt *te_oconp;	/* outgoing conn request pending */
	struct tl_endpt *te_conp;	/* connected endpt */
	queue_t *te_backwq;		/* back pointer for qenable-TBD */
	queue_t *te_flowrq;		/* flow controlled on whom */
	tl_addr_t te_lastdest;		/* ticlts last destination */
	struct tl_endpt *te_lastep;	/* ticlts last dest. endpoint */
	int te_bufcid;			/* outstanding bufcall id */
	int te_timoutid;		/* outstanding timeout id */
	tl_credopt_t	te_cred;	/* endpoint user credentials */
} tl_endpt_t;

#define	TL_SETCRED	0x1	/* flag to indicate sending of credentials */
#define	TL_CLOSING	0x2	/* flag to indicating we are closing */
#define	TL_SOCKCHECKED	0x4	/* flag we have checked upstream for sockmod */
#define	TL_ISASOCKET	0x8	/* flag that we are a socket */


static	struct	module_info	tl_minfo = {
	TL_ID,			/* mi_idnum */
	TL_NAME,		/* mi_idname */
	TL_MINPSZ,		/* mi_minpsz */
	TL_MAXPSZ,		/* mi_maxpsz */
	TL_HIWAT,		/* mi_hiwat */
	TL_LOWAT		/* mi_lowat */
};

static	struct	qinit	tl_rinit = {
	NULL,			/* qi_putp */
	(int (*)())tl_rsrv,	/* qi_srvp */
	tl_open,		/* qi_qopen */
	tl_close,		/* qi_qclose */
	NULL,			/* qi_qadmin */
	&tl_minfo,		/* qi_minfo */
	NULL			/* qi_mstat */
};

static	struct	qinit	tl_winit = {
	(int (*)())tl_wput,	/* qi_putp */
	(int (*)())tl_wsrv,	/* qi_srvp */
	NULL,			/* qi_qopen */
	NULL,			/* qi_qclose */
	NULL,			/* qi_qadmin */
	&tl_minfo,		/* qi_minfo */
	NULL			/* qi_mstat */
};

static	struct streamtab	tlinfo = {
	&tl_rinit,		/* st_rdinit */
	&tl_winit,		/* st_wrinit */
	NULL,			/* st_muxrinit */
	NULL			/* st_muxwrinit */
};

static struct cb_ops cb_tl_ops = {
	nulldev,				/* cb_open */
	nulldev,				/* cb_close */
	nodev,					/* cb_strategy */
	nodev,					/* cb_print */
	nodev,					/* cb_dump */
	nodev,					/* cb_read */
	nodev,					/* cb_write */
	nodev,					/* cb_ioctl */
	nodev,					/* cb_devmap */
	nodev,					/* cb_mmap */
	nodev,					/* cb_segmap */
	nochpoll,				/* cb_chpoll */
	ddi_prop_op,				/* cb_prop_op */
	&tlinfo,				/* cb_stream */
	(int) (D_NEW|D_MP|D_MTQPAIR|D_MTOUTPERIM|D_MTOCEXCL)
						/* cb_flag */
};

static struct dev_ops tl_ops  = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	tl_info,		/* devo_getinfo */
	tl_identify,		/* devo_identify */
	nulldev,		/* devo_probe */
	tl_attach,		/* devo_attach */
	tl_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&cb_tl_ops,		/* devo_tl_ops */
	(struct bus_ops *)NULL	/* devo_bus_ops */
};

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module -- pseudo driver here */
	"TPI Local Transport Driver - tl",
	&tl_ops,		/* driver ops */
};

/*
 * Module linkage information for the kernel.
 */
static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

/*
 * default address declaration - used for default
 * address generation heuristic
 */
static	int	tl_defaultabuf = 0x1000;
static struct	tl_addr	tl_defaultaddr = {TL_DEFAULTADDRSZ,
						(char *) &tl_defaultabuf};
/*
 * Templates for response to info request
 * Check sanity of unlimited connect data etc.
 */
static struct T_info_ack tl_cots_info_ack =
	{
		T_INFO_ACK,	/* PRIM_type -always T_INFO_ACK */
		-1,		/* TSDU size -1 => unlimited */
		-1,		/* ETSDU size -1 => unlimited */
		-1,		/* CDATA_size -1 => unlimited */
		-1,		/* DDATA_size -1 =>unlimited  */
		-1,		/* ADDR_size -1 => unlimited */
		-1,		/* OPT_size */
		0,		/* TIDU_size - fill at run time */
		T_COTS,		/* SERV_type */
		-1,		/* CURRENT_state */
		0		/* PROVIDER_flag */
	};

static struct T_info_ack tl_clts_info_ack =
	{
		T_INFO_ACK,	/* PRIM_type - always T_INFO_ACK */
		0,		/* TSDU_size - fill at run time */
		-2,		/* ETSDU_size -2 => not supported */
		-2,		/* CDATA_size -2 => not supported */
		-2,		/* DDATA_size  -2 => not supported */
		-1,		/* ADDR_size -1 => unlimited */
		-1,		/* OPT_size */
		0,		/* TIDU_size - fill at run time */
		T_CLTS,		/* SERV_type */
		-1,		/* CURRENT_state */
		0		/* PROVIDER_flag */
	};

/*
 * Linked list of open driver streams
 */
static tl_endpt_t *tl_olistp = NULL;

/*
 * Marker for round robin flow control back enabling
 * and a global mutex to protect it.
 */
static tl_endpt_t *tl_flow_rr_head;
static kmutex_t	tl_flow_rr_lock;

/*
 * private copy of devinfo pointer used in tl_info
 */
static dev_info_t *tl_dip;


/*
 * INTER MODULE DEPENDENCIES
 * -------------------------
 *
 * This is for sharing common option management code which happens
 * to live in "ip" kernel module only. Needed to do AF_UNIX socket
 * options
 */
char _depends_on[] = "drv/ip";
typedef	int	(*pfi_t)();
typedef	boolean_t	(*pfb_t)();
extern void optcom_req(queue_t *q, mblk_t *mp, pfi_t setfn,
			pfi_t getfn, pfb_t chkfn, int priv);

/*
 * LOCAL FUNCTION PROTOTYPES
 * -------------------------
 */
static int tl_do_proto(queue_t *, mblk_t *);
static int tl_do_pcproto(queue_t *, mblk_t *);
static void tl_do_ioctl(queue_t *, mblk_t *);
static void tl_error_ack(queue_t *, mblk_t *, long, long, long);
static void tl_bind(queue_t *, mblk_t *);
static void tl_ok_ack(queue_t *, mblk_t  *mp, long);
static void tl_unbind(queue_t *, mblk_t *);
static void tl_optmgmt(queue_t *, mblk_t *);
static void tl_conn_req(queue_t *, mblk_t *);
static void tl_conn_res(queue_t *, mblk_t *);
static void tl_discon_req(queue_t *, mblk_t *);
static void tl_info_req(queue_t *, mblk_t *);
static int tl_data(queue_t *, mblk_t  *);
static int tl_exdata(queue_t *, mblk_t *);
static int tl_ordrel(queue_t *, mblk_t *);
static int tl_unitdata(queue_t *, mblk_t *);
static void tl_uderr(queue_t *, mblk_t *, long);
static tl_endpt_t *tl_addr_in_use(int, tl_addr_t *);
static int tl_get_any_addr(int, tl_addr_t *);
static void tl_co_unconnect(tl_endpt_t *);
static void tl_cl_mctl_link(queue_t *, mblk_t *);
static mblk_t *tl_reallocb(mblk_t *, int);
static mblk_t *tl_resizemp(mblk_t *, int);
static int tl_preallocate(mblk_t *, mblk_t **, mblk_t **, int);
static void tl_discon_ind(tl_endpt_t *, long);
static mblk_t *tl_discon_ind_alloc(long, long);
static mblk_t *tl_ordrel_ind_alloc(void);
static void tl_merror(queue_t *, mblk_t *, int);
static void tl_fill_option(char *, unsigned long, int, char *);
static boolean_t tl_chk_opt(int, int);
static int tl_get_opt(queue_t *, int, int, unsigned char *);
static int tl_set_opt(queue_t *, int, int, unsigned char *, int);
static void tl_memrecover(queue_t *, mblk_t *, int);
static int tl_isasocket(queue_t *, tl_endpt_t *);

/*
 * LOCAL FUNCTIONS AND DRIVER ENTRY POINTS
 * ---------------------------------------
 */

/*
 * Loadable module routines
 */
int
_init(void)
{
	int	error;

	mutex_init(&tl_flow_rr_lock, "ticlts flow-control-head lock",
		MUTEX_DEFAULT, NULL);
	error = mod_install(&modlinkage);
	if (error != 0) {
		mutex_destroy(&tl_flow_rr_lock);
	}
	return (error);
}

int
_fini(void)
{
	int	error;

	error = mod_remove(&modlinkage);
	if (error != 0)
		return (error);
	mutex_destroy(&tl_flow_rr_lock);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Driver Entry Points and Other routines
 */
static int
tl_identify(dev_info_t *devi)
{
	if (strcmp((char *)ddi_get_name(devi), "tl") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

static int
tl_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH) {
		/*
		 * DDI_ATTACH is only non-reserved
		 * value of cmd
		 */
		return (DDI_FAILURE);
	}

	tl_dip = devi;
	if (ddi_create_minor_node(devi, "ticots", S_IFCHR,
			TL_TICOTS, NULL, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (DDI_FAILURE);
	}
	if (ddi_create_minor_node(devi, "ticotsord", S_IFCHR,
		TL_TICOTSORD, NULL, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (DDI_FAILURE);
	}
	if (ddi_create_minor_node(devi, "ticlts", S_IFCHR,
		TL_TICLTS, NULL, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

static int
tl_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	ASSERT(tl_olistp == NULL);
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);
	ddi_remove_minor_node(devi, NULL);
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
tl_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{

	int retcode;

	switch (infocmd) {

	case DDI_INFO_DEVT2DEVINFO:
		if (tl_dip == NULL)
			retcode = DDI_FAILURE;
		else {
			*result = (void *) tl_dip;
			retcode = DDI_SUCCESS;
		}
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *) 0;
		retcode = DDI_SUCCESS;
		break;

	default:
		retcode = DDI_FAILURE;
	}
	return (retcode);
}

/* ARGSUSED */
static int
tl_open(rq, devp, oflag, sflag, credp)
	queue_t	*rq;
	dev_t	*devp;
	int	oflag;
	int	sflag;
	cred_t	*credp;
{
	minor_t minordev;
	tl_credopt_t	*credoptp;
	tl_endpt_t *tep;
	register tl_endpt_t *elp, *prev_elp, **prev_nextpp;
	int rc;

	rc = 0;

	/*
	 * Driver is called directly. Both CLONEOPEN and MODOPEN
	 * are illegal
	 */
	if ((sflag == CLONEOPEN) || (sflag == MODOPEN))
		return (ENXIO);

	if (rq->q_ptr)
		goto done;

	tep = kmem_zalloc(sizeof (tl_endpt_t), KM_SLEEP);
	/*
	 * initialize the endpoint - already zeroed
	 */
	tep->te_state = TS_UNBND;

	/*
	 * fill credopt from cred
	 */
	credoptp = &tep->te_cred;
	credoptp->tc_ngroups = credp->cr_ngroups;
	credoptp->tc_uid = credp->cr_uid;
	credoptp->tc_gid = credp->cr_gid;
	credoptp->tc_ruid = credp->cr_ruid;
	credoptp->tc_rgid = credp->cr_rgid;
	credoptp->tc_suid = credp->cr_suid;
	credoptp->tc_sgid = credp->cr_sgid;

	/*
	 * mode of driver is determined by the minor number
	 * used to open it. It can be TL_TICOTS, TL_TICOTSORD
	 * or TL_TICLTS
	 */
	tep->te_mode = getminor(*devp);

	/*
	 * Initialize buffer for addr cache if connectionless provider
	 */
	if (tep->te_mode == TL_TICLTS) {
		tep->te_lastdest.ta_alen = -1;
		tep->te_lastdest.ta_abuf = kmem_zalloc(TL_CLTS_CACHADDR_MAX,
			KM_SLEEP);
	}

	/*
	 * Find any unused minor number >= TL_MINOR_START (a number greater
	 * than minor numbers this driver can be opened with)
	 */

	minordev = TL_MINOR_START;

	/*
	 * Following code for speed not readability!
	 * - Traverse the ordered list looking for a free minor
	 *   device number. The insert the new node with the new
	 *   minor device number into the list
	 */
	prev_nextpp = &tl_olistp;
	prev_elp = NULL;
	while ((elp = *prev_nextpp) != NULL) {
		if (minordev < elp->te_minor)
			break;
		minordev++;
		prev_nextpp = &elp->te_nextp;
		prev_elp = elp;
	}
	tep->te_minor = minordev;
	tep->te_nextp = *prev_nextpp;
	tep->te_prevp = prev_elp;
	if (elp)
		elp->te_prevp = tep;
	*prev_nextpp = tep;	/* this updates "tl_olistp" if first! */


	*devp = makedevice(getmajor(*devp), minordev);	/* clone the driver */

	tep->te_rq = rq;
	rq->q_ptr = WR(rq)->q_ptr = (char *) tep;

done:
	qprocson(rq);
	return (rc);
}

/* ARGSUSED1 */
static int
tl_close(rq, flag, credp)
	queue_t *rq;
	int	flag;
	cred_t	*credp;
{
	tl_endpt_t *tep, *elp;
	queue_t *wq;

	tep = (tl_endpt_t *)rq->q_ptr;

	ASSERT(tep);

	wq = WR(rq);

	/*
	 * Drain out all messages on queue except for TL_TICOTS where the
	 * abortive release semantics permit discarding of data on close
	 */
	tep->te_flag |= TL_CLOSING;
	if (tep->te_mode != TL_TICOTS) {
		while (wq->q_first != NULL) {
			tl_wsrv(wq);
			/*
			 * tl_wsrv returning without draining (or discarding
			 * in tl_merror). This can only happen when we are
			 * recovering from allocation failures and have a
			 * bufcall or timeout outstanding on the queue
			 */
			if (wq->q_first != NULL) {
				ASSERT(tep->te_bufcid || tep->te_timoutid);
				qwait(wq);
			}
		}
	}

	wq->q_next = NULL;

	qprocsoff(rq);

	if (tep->te_bufcid)
		qunbufcall(rq, tep->te_bufcid);
	if (tep->te_timoutid)
		quntimeout(rq, tep->te_timoutid);

	rq->q_ptr = wq->q_ptr = NULL;

	if ((tep->te_mode == TL_TICOTS) ||
	    (tep->te_mode == TL_TICOTSORD)) {
		/*
		 * connection oriented - unconnect endpoints
		 */
		tl_co_unconnect(tep);
	} else {
		/*
		 * Do connectionless specific cleanup
		 */
		kmem_free(tep->te_lastdest.ta_abuf, TL_CLTS_CACHADDR_MAX);
		tep->te_lastdest.ta_alen = -1; /* uninitialized */
		tep->te_lastdest.ta_abuf = NULL;
		/*
		 * traverse list and delete cached references to this
		 * endpoint being closed
		 */
		elp = tl_olistp;
		while (elp) {
			if (elp->te_lastep == tep) {
				elp->te_lastep = NULL;
				elp->te_lastdest.ta_alen = -1;
			}
			elp = elp->te_nextp;
		}
	}

	tep->te_state = -1;	/* uninitialized */

	if (tep->te_ap.ta_abuf) {
		(void) kmem_free(tep->te_ap.ta_abuf, tep->te_ap.ta_alen);
		tep->te_ap.ta_alen = -1; /* uninitialized */
		tep->te_ap.ta_abuf = NULL;
	}

	/*
	 * disconnect te structure from list of open endpoints
	 */
	if (tep->te_prevp != NULL)
		tep->te_prevp->te_nextp = tep->te_nextp;
	if (tep->te_nextp != NULL)
		tep->te_nextp->te_prevp = tep->te_prevp;
	if (tep == tl_olistp) {
		tl_olistp = tl_olistp->te_nextp;
	}

	/*
	 * Advance tl_flow_rr_head pointer.
	 * Note. Advancing to tail of list (NULL) is fine and handled correctly
	 *      in tl_rsrv
	 * mutex_enter(&tl_flow_rr_lock); needed if no outer perimeter
	 */
	if (tl_flow_rr_head == tep)
		tl_flow_rr_head = tep->te_nextp;
	/*
	 * mutex_exit(&tl_flow_rr_lock); needed if no outer perimeter
	 */
	(void) kmem_free((char *) tep, sizeof (tl_endpt_t));
	return (0);
}



static void
tl_wput(wq, mp)
	queue_t *wq;
	mblk_t *mp;
{
	tl_endpt_t		*tep, *peer_tep;
	int			msz;
	queue_t			*peer_rq;
	union T_primitives	*prim;
	long			prim_type;

	tep = (tl_endpt_t *) wq->q_ptr;
	msz = MBLKL(mp);

	/*
	 * fastpath for data
	 */
	if (tep->te_mode != TL_TICLTS) { /* connection oriented */
		peer_tep = tep->te_conp;
		if ((DB_TYPE(mp) == M_DATA) &&
			(tep->te_state == TS_DATA_XFER) &&
			(! wq->q_first) &&
			(peer_tep) &&
			(peer_tep->te_state == TS_DATA_XFER) &&
			(peer_rq = peer_tep->te_rq) &&
			(canputnext(peer_rq))) {
				putnext(peer_rq, mp);
				return;
			}
	} else {		/* connectionless */
		if ((DB_TYPE(mp) == M_PROTO) &&
			(tep->te_state == TS_IDLE) &&
			(msz >= sizeof (struct T_unitdata_req)) &&
			(*(long *) mp->b_rptr == T_UNITDATA_REQ) &&
			(! wq->q_first)) {
			/* send T_UNITDATA_IND to linked endpoint */
			(void) tl_unitdata(wq, mp);
			return;
		}
	}

	switch (DB_TYPE(mp)) {

	case M_DATA:	/* slowpath */
		if (tep->te_mode == TL_TICLTS) {
			STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
				"tl_wput:M_DATA invalid for ticlts driver");
			tl_merror(wq, mp, EPROTO);
			return;
		}
		(void) putq(wq, mp);
		break;

	case M_CTL:
		if (wq->q_first) {
			(void) putq(wq, mp);
			break;
		}
		if (tep->te_mode == TL_TICLTS) {
			switch (*(long *)mp->b_rptr) {

			case TL_CL_LINK:
				qwriter(wq, mp, tl_cl_mctl_link, PERIM_OUTER);
				return;

			case TL_CL_UNLINK:
				wq->q_next = NULL;
				freemsg(mp);
				break;

			default:
				break;

			}
		} else {
			STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
				"tl_wput:M_CTL msg not supported on provider");
		}
		break;


	case M_IOCTL:

		tl_do_ioctl(wq, mp);
		break;

	case M_FLUSH:

		/*
		 * do canonical M_FLUSH procesing
		 */

		if (*mp->b_rptr & FLUSHW) {
			flushq(wq, FLUSHALL);
			*mp->b_rptr &= ~FLUSHW;
		}
		if (*mp->b_rptr & FLUSHR) {
			flushq(RD(wq), FLUSHALL);
			qreply(wq, mp);
		} else
			freemsg(mp);
		break;

	case M_PROTO:
		prim = (union T_primitives *) mp->b_rptr;
		prim_type = prim->type;

		/*
		 * process in service procedure if
		 * message already queued or
		 * short message error or
		 * messages one of certain types
		 */
		if ((wq->q_first) ||
			(msz < sizeof (prim_type)) ||
			(prim_type == T_DATA_REQ) ||
			(prim_type == T_EXDATA_REQ) ||
			(prim_type == T_ORDREL_REQ) ||
			(prim_type == T_UNITDATA_REQ)) {
			(void) putq(wq, mp);
			return;
		}
		(void) tl_do_proto(wq, mp);
		break;

	case M_PCPROTO:
		if (wq->q_first) {
			(void) putq(wq, mp);
			return;
		}
		(void) tl_do_pcproto(wq, mp);
		break;

	default:
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_wput:default:unexpected Streams message");
		freemsg(mp);
		break;
	}
}



static void
tl_wsrv(wq)
	queue_t *wq;
{
	mblk_t *mp;
	tl_endpt_t *tep;

	tep = (tl_endpt_t *) wq->q_ptr;

	/*
	 * clear flow control pointer
	 * (in case we were enabled after flow control)
	 */
	tep->te_flowrq = NULL;


	while (mp = getq(wq)) {
		switch (DB_TYPE(mp)) {

		case M_DATA:
			if (tl_data(wq, mp) == -1)
				return;
			break;

		case M_PROTO:
			if (tl_do_proto(wq, mp) == -1)
				return;
			break;
		case M_PCPROTO:
			if (tl_do_pcproto(wq, mp) == -1)
				return;
			break;
		default:
			STRLOG(TL_ID, tep->te_minor, 1,
			SL_TRACE|SL_ERROR,
				"tl_wsrv:unexpected message type");
			freemsg(mp);
			break;
		}
	}
}

static void
tl_rsrv(rq)
	queue_t *rq;
{
	tl_endpt_t *tep, *elp;

	tep = (tl_endpt_t *) rq->q_ptr;
	ASSERT(tep != NULL);

	/*
	 * enable queue for data transfer
	 */

	if ((tep->te_mode != TL_TICLTS) &&
		((tep->te_state == TS_DATA_XFER) ||
		(tep->te_state == TS_WIND_ORDREL)||
		(tep->te_state == TS_WREQ_ORDREL))) {
		/*
		 * If unconnect was done (due to a close) the peer might be
		 * gone.
		 */
		if (tep->te_conp != NULL)
			(void) qenable(WR(tep->te_conp->te_rq));
	} else {
		if (tep->te_state == TS_IDLE) {
			/*
			 * more than one sender could be flow controlled
			 * look for next endpoint on the list flow
			 * controlled on us
			 */
			mutex_enter(&tl_flow_rr_lock);
			elp = tl_flow_rr_head;
			while (elp) {
				if (elp->te_flowrq == rq) {
					qenable(WR(elp->te_rq));
				}
				elp = elp->te_nextp;
			}
			if (tl_flow_rr_head != tl_olistp) {
				/*
				 * started from middle of list
				 * and reached end of list
				 * Start from beginning again
				 * and search upto starting point
				 */
				elp = tl_olistp;
				while (elp != tl_flow_rr_head) {
					if (elp->te_flowrq == rq) {
						qenable(WR(elp->te_rq));
					}
					elp = elp->te_nextp;
				}
			}
			/*
			 * Advance tl_flow_rr_head
			 */
			if (tl_flow_rr_head)
				tl_flow_rr_head = tl_flow_rr_head->te_nextp;
			else
				tl_flow_rr_head = tl_olistp;
			mutex_exit(&tl_flow_rr_lock);
		}
	}
}



static int
tl_do_proto(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t		*tep;
	int			msz, rc;
	union T_primitives	*prim;

	tep = (tl_endpt_t *) wq->q_ptr;
	msz = MBLKL(mp);
	rc = 0;

	prim = (union T_primitives *) mp->b_rptr;
	if (msz < sizeof (prim->type)) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_wput:M_PROTO: bad message");
		tl_merror(wq, mp, EPROTO);
		return (-1);
	}

	switch (prim->type) {

	case T_BIND_REQ:
		qwriter(wq, mp, tl_bind, PERIM_OUTER);
		break;

	case T_UNBIND_REQ:
		tl_unbind(wq, mp);
		break;

	case T_OPTMGMT_REQ:
		tl_optmgmt(wq, mp);
		break;

	case T_CONN_REQ:
		ASSERT(tep->te_mode != TL_TICLTS);
		qwriter(wq, mp, tl_conn_req, PERIM_OUTER);
		break;

	case T_CONN_RES:
		ASSERT(tep->te_mode != TL_TICLTS);
		qwriter(wq, mp, tl_conn_res, PERIM_OUTER);
		break;

	case T_DISCON_REQ:
		ASSERT(tep->te_mode != TL_TICLTS);
		qwriter(wq, mp, tl_discon_req, PERIM_OUTER);
		break;

	case T_DATA_REQ:
		ASSERT(tep->te_mode != TL_TICLTS);
		rc = tl_data(wq, mp);
		break;

	case T_EXDATA_REQ:
		ASSERT(tep->te_mode != TL_TICLTS);
		rc = tl_exdata(wq, mp);
		break;

	case T_ORDREL_REQ:
		ASSERT(tep->te_mode == TL_TICOTSORD);
		rc = tl_ordrel(wq, mp);
		break;

	case T_UNITDATA_REQ:
		ASSERT(tep->te_mode == TL_TICLTS);
		rc = tl_unitdata(wq, mp);
		break;

	default:
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_wput:default:unknown TPI msg primitive");
		tl_merror(wq, mp, EPROTO);
		return (-1);
	}
	return (rc);
}


static int
tl_do_pcproto(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t		*tep;
	int			msz;
	union T_primitives	*prim;

	tep = (tl_endpt_t *) wq->q_ptr;

	msz = MBLKL(mp);

	prim = (union T_primitives *) mp->b_rptr;
	if (msz < sizeof (prim->type)) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_wput:M_PCPROTO: bad message");
		tl_merror(wq, mp, EPROTO);
		return (-1);
	}
	switch (prim->type) {

	case T_INFO_REQ:
		tl_info_req(wq, mp);
		break;

	default:
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_wput:default:unknown TPI msg primitive");
		tl_merror(wq, mp, EPROTO);
		break;
	}
	return (0);
}


static void
tl_do_ioctl(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	struct iocblk *iocbp;
	tl_endpt_t *tep;

	iocbp = (struct iocblk *) mp->b_rptr;
	tep = (tl_endpt_t *) wq->q_ptr;

	switch (iocbp->ioc_cmd) {
	case TL_IOC_CREDOPT:
		if (iocbp->ioc_count != sizeof (uint) ||
			mp->b_cont == (mblk_t *) NULL ||
			((uint)mp->b_cont->b_rptr & (sizeof (uint) - 1)))
			goto iocnak;

		if (*(uint *)mp->b_cont->b_rptr)
			tep->te_flag |= TL_SETCRED;
		else
			tep->te_flag &= ~TL_SETCRED;
		miocack(wq, mp, 0, 0);
		break;

	default:
iocnak:
		miocnak(wq, mp, 0, EINVAL);
		break;
	}
}


/*
 * send T_ERROR_ACK
 * Note: assumes enough memory or caller passed big enough mp
 *	- no recovery from allocb failures
 */

static void
tl_error_ack(wq, mp, tli_err, unix_err, type)
queue_t *wq;
mblk_t *mp;
long tli_err, unix_err, type;
{
	tl_endpt_t *tep;
	unsigned int ack_sz;
	struct T_error_ack *err_ack;
	mblk_t *ackmp;

	tep = (tl_endpt_t *) wq->q_ptr;
	ack_sz = sizeof (struct T_error_ack);

	if (mp->b_cont) {
		freemsg(mp->b_cont);
		mp->b_cont = (mblk_t *) NULL;
	}
	ackmp = tl_resizemp(mp, ack_sz);
	if (! ackmp) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_error_ack:out of mblk memory");
		tl_merror(wq, mp, ENOMEM);
		return;
	}
	DB_TYPE(mp) = M_PCPROTO;
	err_ack = (struct T_error_ack *) ackmp->b_rptr;
	err_ack->PRIM_type = T_ERROR_ACK;
	err_ack->ERROR_prim = type;
	err_ack->TLI_error = tli_err;
	err_ack->UNIX_error = unix_err;

	/*
	 * send error ack message
	 */
	qreply(wq, ackmp);
}



/*
 * send T_OK_ACK
 * Note: assumes enough memory or caller passed big enough mp
 *	- no recovery from allocb failures
 */
static void
tl_ok_ack(wq, mp, type)
queue_t *wq;
mblk_t *mp;
long type;
{
	tl_endpt_t *tep;
	mblk_t *ackmp;
	struct T_ok_ack *ok_ack;

	tep = (tl_endpt_t *) wq->q_ptr;

	if (mp->b_cont) {
		freemsg(mp->b_cont);
		mp->b_cont = NULL;
	}

	ackmp = tl_resizemp(mp, sizeof (struct T_ok_ack));
	if (! ackmp) {
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_ok_ack:allocb failure");
		tl_merror(wq, mp, ENOMEM);
		return;
	}

	DB_TYPE(ackmp) = M_PCPROTO;
	ok_ack = (struct T_ok_ack *) ackmp->b_rptr;
	ok_ack->PRIM_type  = T_OK_ACK;
	ok_ack->CORRECT_prim = type;

	(void) qreply(wq, ackmp);
}



/*
 * qwriter call back function. Called with qwriter so
 * that concurrent bind operations to same transport address do not
 * succeed
 */
static void
tl_bind(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t *tep;
	struct T_bind_ack *b_ack;
	struct T_bind_req *bind;
	mblk_t *ackmp, *bamp;
	unsigned long	qlen;
	long alen, aoff;
	tl_addr_t addr_req;
	char *addr_startp;
	int save_state;
	long msz, basize;
	long tli_err, unix_err;

	unix_err = 0;
	tep = (tl_endpt_t *) wq->q_ptr;
	bind = (struct T_bind_req *) mp->b_rptr;

	msz = MBLKL(mp);

	save_state = tep->te_state;

	/*
	 * validate state
	 */
	if (tep->te_state != TS_UNBND) {
		STRLOG(TL_ID, tep->te_minor, 1,
			SL_TRACE|SL_ERROR,
			"tl_wput:T_BIND_REQ:out of state, state=%d",
			tep->te_state);
		tli_err = TOUTSTATE;
		goto error;
	}
	tep->te_state = NEXTSTATE(TE_BIND_REQ, tep->te_state);


	/*
	 * validate message
	 * Note: dereference fields in struct inside message only
	 * after validating the message length.
	 */
	if (msz < sizeof (struct T_bind_req)) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_wput:tl_bind: invalid message length");
		tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
		tli_err = TSYSERR; unix_err = EINVAL;
		goto error;
	}
	alen = bind->ADDR_length;
	aoff = bind->ADDR_offset;
	if (((alen > 0) && ((aoff < 0) || ((aoff + alen) > msz)))) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_wput:tl_bind: invalid message");
		tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
		tli_err = TSYSERR; unix_err = EINVAL;
		goto error;
	}
	if ((alen < 0) || (alen > (msz-sizeof (struct T_bind_req)))) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_wput:tl_bind: bad addr in  message");
		tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
		tli_err = TBADADDR;
		goto error;
	}

#ifdef DEBUG
	/*
	 * Mild form of ASSERT()ion to detect broken TPI apps.
	 * if (! assertion)
	 *	log warning;
	 */
	if (! ((alen == 0 && aoff == 0) ||
		(aoff >= (sizeof (struct T_bind_req))))) {
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_wput:tl_bind: addr overlaps TPI message");
	}
#endif
	/*
	 * negotiate max conn req pending
	 */
	if (tep->te_mode != TL_TICLTS) { /* connection oriented */
		qlen = bind->CONIND_number;
		if (qlen > TL_MAXQLEN)
			qlen = TL_MAXQLEN;
	}

	if (alen == 0) {
		/*
		 * assign any free address
		 */
		if (! tl_get_any_addr(tep->te_mode, &tep->te_ap)) {
			STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
				"tl_bind:failed to get buffer for any address");
			tli_err = TSYSERR; unix_err = ENOMEM;
			goto error;
		}
	} else {
		addr_req.ta_alen = alen;
		addr_req.ta_abuf = (char *) (mp->b_rptr + aoff);
		if (tl_addr_in_use(tep->te_mode, &addr_req) != NULL) {
			if (! tl_get_any_addr(tep->te_mode, &tep->te_ap)) {
				STRLOG(TL_ID, tep->te_minor, 1,
					SL_TRACE|SL_ERROR,
					"tl_bind:unable to get any addr buf");
				if (tep->te_ap.ta_abuf)
					(void) kmem_free(tep->te_ap.ta_abuf,
							tep->te_ap.ta_alen);
				tli_err = TSYSERR; unix_err = ENOMEM;
				goto error;
			}
		} else {
			/*
			 * requested address is free - copy it to new
			 * buffer and return
			 */
			tep->te_ap.ta_abuf = (char *)
				kmem_alloc(addr_req.ta_alen, KM_NOSLEEP);
			if (tep->te_ap.ta_abuf == NULL) {
				STRLOG(TL_ID, tep->te_minor, 1,
					SL_TRACE|SL_ERROR,
					"tl_bind:unable to get any addr buf");
				if (tep->te_ap.ta_abuf)
					(void) kmem_free(tep->te_ap.ta_abuf,
							tep->te_ap.ta_alen);
				tli_err = TSYSERR; unix_err = ENOMEM;
				goto error;
			}
			tep->te_ap.ta_alen = addr_req.ta_alen;
			bcopy(addr_req.ta_abuf, tep->te_ap.ta_abuf,
				addr_req.ta_alen);
		}
	}

	/*
	 * prepare T_BIND_ACK TPI message
	 */
	basize = sizeof (struct T_bind_ack) + tep->te_ap.ta_alen;
	bamp = tl_reallocb(mp, basize);
	if (! bamp) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_wput:tl_bind: allocb failed");
		(void) kmem_free(tep->te_ap.ta_abuf, tep->te_ap.ta_alen);
		/*
		 * roll back state changes
		 */
		tep->te_state = TS_UNBND;
		tl_memrecover(wq, mp, basize);
		return;
	}

	DB_TYPE(bamp) = M_PCPROTO;
	b_ack = (struct T_bind_ack *) bamp->b_rptr;
	b_ack->PRIM_type = T_BIND_ACK;
	b_ack->CONIND_number = qlen;
	b_ack->ADDR_length = tep->te_ap.ta_alen;
	b_ack->ADDR_offset = sizeof (struct T_bind_ack);
	addr_startp = (char *) (bamp->b_rptr +
				b_ack->ADDR_offset);
	bcopy(tep->te_ap.ta_abuf, addr_startp, tep->te_ap.ta_alen);

	tep->te_qlen = qlen;

	tep->te_state = NEXTSTATE(TE_BIND_ACK, tep->te_state);
	/*
	 * send T_BIND_ACK message
	 */
	(void) qreply(wq, bamp);
	return;

error:
	ackmp = tl_reallocb(mp, sizeof (struct T_error_ack));
	if (! ackmp) {
		/*
		 * roll back state changes
		 */
		tep->te_state = save_state;
		tl_memrecover(wq, mp, sizeof (struct T_error_ack));
		return;
	}
	tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
	tl_error_ack(wq, ackmp, tli_err, unix_err, T_BIND_REQ);
}


static void
tl_unbind(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t *tep;
	mblk_t *ackmp;
	mblk_t *freemp;

	tep = (tl_endpt_t *) wq->q_ptr;
	/*
	 * preallocate memory for max of T_OK_ACK and T_ERROR_ACK
	 * ==> allocate for T_ERROR_ACK (known max)
	 */
	if (! tl_preallocate(mp, &ackmp, &freemp,
				sizeof (struct T_error_ack))) {
			tl_memrecover(wq, mp, sizeof (struct T_error_ack));
			return;
	}
	/*
	 * memory resources commited
	 * Note: no message validation. T_UNBIND_REQ message is
	 * same size as PRIM_type field so already verified earlier.
	 */

	/*
	 * validate state
	 */
	if (tep->te_state != TS_IDLE) {
		STRLOG(TL_ID, tep->te_minor, 1,
			SL_TRACE|SL_ERROR,
			"tl_wput:T_UNBIND_REQ:out of state, state=%d",
			tep->te_state);
		tl_error_ack(wq, ackmp, TOUTSTATE, 0, T_UNBIND_REQ);
		freemsg(freemp);
		return;
	}
	tep->te_state = NEXTSTATE(TE_UNBIND_REQ, tep->te_state);

	/*
	 * TPI says on T_UNBIND_REQ:
	 *    send up a M_FLUSH to flush both
	 *    read and write queues
	 */
	(void) putnextctl1(RD(wq), M_FLUSH, FLUSHRW);

	tep->te_qlen = 0;
	(void) kmem_free(tep->te_ap.ta_abuf, tep->te_ap.ta_alen);

	tep->te_ap.ta_alen = -1; /* uniitialized */
	tep->te_ap.ta_abuf = NULL;

	tep->te_state = NEXTSTATE(TE_OK_ACK1, tep->te_state);

	/*
	 * send  T_OK_ACK
	 */
	tl_ok_ack(wq, ackmp, T_UNBIND_REQ);

	freemsg(freemp);
}


/*
 * Option management code from drv/ip is used here
 * Note: TL_PROT_LEVEL/TL_IOC_CREDOPT option is not known to inet/optcom.c
 *	so optcom_req() will fail T_OPTMGMT_REQ. However, that is what we
 *	want as that option is 'unorthodox' and only valid in T_CONN_IND,
 *	T_CONN_CON  and T_UNITDATA_IND and not in T_OPTMGMT_REQ/ACK
 * Note2: use of optcom_req means this routine is an exception to
 *	 recovery from allocb() failures.
 */

static void
tl_optmgmt(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t *tep;
	mblk_t *ackmp;

	tep = (tl_endpt_t *) wq->q_ptr;

	/*  all states OK for AF_UNIX options ? */
	if (!tl_isasocket(wq, tep) && tep->te_state != TS_IDLE) {
		STRLOG(TL_ID, tep->te_minor, 1,
			SL_TRACE|SL_ERROR,
			"tl_wput:T_OPTMGMT_REQ:out of state, state=%d",
			tep->te_state);
		/*
		 * preallocate memory for T_ERROR_ACK
		 */
		ackmp = allocb(sizeof (struct T_error_ack), BPRI_MED);
		if (! ackmp) {
			tl_memrecover(wq, mp, sizeof (struct T_error_ack));
			return;
		}

		tl_error_ack(wq, ackmp, TOUTSTATE, 0, T_OPTMGMT_REQ);
		freemsg(mp);
		return;
	}

	/*
	 * call common option management routine from drv/ip
	 */
	optcom_req(wq, mp, tl_set_opt, tl_get_opt, tl_chk_opt,
		(int)(tep->te_cred.tc_uid == 0));
}


static void
tl_conn_req(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t *tep;
	struct T_conn_req *creq;
	long msz, alen, aoff, olen, ooff;
	int err;
	tl_endpt_t *peer_tep;
	char *addr_startp;
	long  ci_msz, size;
	mblk_t *indmp, *freemp, *ackmp;
	mblk_t *dimp, *cimp;
	struct T_discon_ind *di;
	struct T_conn_ind *ci;
	tl_addr_t dst;

	/*
	 * preallocate memory for:
	 * 1. max of T_ERROR_ACK and T_OK_ACK
	 *	==> known max T_ERROR_ACK
	 * 2. max of T_DISCON_IND and T_CONN_IND
	 */
	ackmp = allocb(sizeof (struct T_error_ack), BPRI_MED);
	if (! ackmp) {
		tl_memrecover(wq, mp, sizeof (struct T_error_ack));
		return;
	}
	/*
	 * memory committed for T_OK_ACK/T_ERROR_ACK now
	 * will be commited for T_DISCON_IND/T_CONN_IND later
	 */


	tep = (tl_endpt_t *) wq->q_ptr;
	creq = (struct T_conn_req *) mp->b_rptr;
	msz = MBLKL(mp);

	if (tep->te_state != TS_IDLE) {
		STRLOG(TL_ID, tep->te_minor, 1,
			SL_TRACE|SL_ERROR,
			"tl_wput:T_CONN_REQ:out of state, state=%d",
			tep->te_state);
		tl_error_ack(wq, ackmp, TOUTSTATE, 0, T_CONN_REQ);
		freemsg(mp);
		return;
	}

	/*
	 * validate the message
	 * Note: dereference fields in struct inside message only
	 * after validating the message length.
	 */
	if (msz < sizeof (struct T_conn_req)) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_conn_req:invalid message length");
		tl_error_ack(wq, ackmp, TSYSERR, EINVAL, T_CONN_REQ);
		freemsg(mp);
		return;
	}
	alen = creq->DEST_length;
	aoff = creq->DEST_offset;
	olen = creq->OPT_length;
	ooff = creq->OPT_offset;
	if (((alen > 0) && ((aoff+alen) > msz)) ||
	    ((olen > 0) && ((ooff+olen) > msz))) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_conn_req:invalid message");
		tl_error_ack(wq, ackmp, TSYSERR, EINVAL, T_CONN_REQ);
		freemsg(mp);
		return;
	}
	if ((alen <= 0) || (alen > (msz-sizeof (struct T_conn_req)))) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_conn_req:bad addr in message,alen=%ld, msz=%ld",
			alen, msz);
		tl_error_ack(wq, ackmp, TBADADDR, 0, T_CONN_REQ);
		freemsg(mp);
		return;
	}
#ifdef DEBUG
	/*
	 * Mild form of ASSERT()ion to detect broken TPI apps.
	 * if (! assertion)
	 *	log warning;
	 */
	if (! (aoff >= sizeof (struct T_conn_req))) {
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_conn_req: addr overlaps TPI message");
	}
#endif
	if (olen) {
		/*
		 * no opts in connect req
		 * supported in this provider
		 */
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_conn_req:options not supported in message");
		tl_error_ack(wq, ackmp, TBADOPT, 0, T_CONN_REQ);
		freemsg(mp);
		return;
	}

	tep->te_state = NEXTSTATE(TE_CONN_REQ, tep->te_state);
	/*
	 * start business of connecting
	 * send T_OK_ACK only after memory committed
	 */

	err = 0;
	/*
	 * get endpoint to connect to
	 * check that peer with DEST addr is bound to addr
	 * and has CONIND_number > 0
	 */
	dst.ta_alen = alen;
	dst.ta_abuf = (char *) (mp->b_rptr + aoff);

	/*
	 * Verify if remote addr is in use
	 */
	if ((peer_tep = tl_addr_in_use(tep->te_mode, &dst)) == NULL) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_conn_req:no one at connect address");
		err = ECONNREFUSED;
	} else if (peer_tep->te_nicon >= peer_tep->te_qlen)  {
		/*
		 * validate that number of incoming connection is
		 * not to capacity on destination endpoint
		 */
		STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE,
			"tl_conn_req: qlen overflow connection refused");
			err = ECONNREFUSED;
	} else if (!((peer_tep->te_state == TS_IDLE) ||
			(peer_tep->te_state == TS_WRES_CIND))) {
		STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE,
			"tl_conn_req:peer in bad state");
		err = ECONNREFUSED;
	}

	/*
	 * preallocate now for T_DISCON_IND or T_CONN_IND
	 */
	if (err)
		size = sizeof (struct T_discon_ind);
	else {
		/*
		 * calculate length of T_CONN_IND message
		 */
		olen = 0;
		if (peer_tep->te_flag & TL_SETCRED) {
			olen = sizeof (struct opthdr) +
				OPTLEN(sizeof (tl_credopt_t));
			/* 1 option only */
		}
		ci_msz = sizeof (struct T_conn_ind) + tep->te_ap.ta_alen;
		ci_msz = TL_ALIGN(ci_msz) + olen;
		size = max(ci_msz, sizeof (struct T_discon_ind));
	}
	if (! tl_preallocate(mp, &indmp, &freemp, size)) {
		/*
		 * roll back state changes
		 */
		tep->te_state = TS_IDLE;
		tl_memrecover(wq, mp, size);
		freemsg(ackmp);
		return;
	}
	/*
	 * memory commited for T_DISCON_IND/T_CONN_IND now
	 */

	/*
	 * ack validity of request
	 */
	tep->te_state = NEXTSTATE(TE_OK_ACK1, tep->te_state);
	tl_ok_ack(wq, ackmp, T_CONN_REQ);

	/* if validation failed earlier - now send T_DISCON_IND */
	if (err) {
		/*
		 *  prepare T_DISCON_IND message
		 */
		dimp = tl_resizemp(indmp, size);
		if (! dimp) {
			STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
				"tl_conn_req:discon_ind:allocb failure");
			tl_merror(wq, indmp, ENOMEM);
			freemsg(freemp);
			return;
		}
		if (dimp->b_cont) {
			/* no user data in prider generated discon ind */
			freemsg(dimp->b_cont);
			dimp->b_cont = NULL;
		}

		DB_TYPE(dimp) = M_PROTO;
		di = (struct T_discon_ind *) dimp->b_rptr;
		di->PRIM_type  = T_DISCON_IND;
		di->DISCON_reason = err;
		di->SEQ_number = BADSEQNUM;

		tep->te_state = NEXTSTATE(TE_DISCON_IND1, tep->te_state);
		/*
		 * send T_DISCON_IND message
		 */
		(void) qreply(wq, dimp);
		freemsg(freemp);
		return;
	}

	/*
	 * prepare message to send T_CONN_IND
	 */


	/*
	 * allocate the message - original data blocks retained
	 * in the returned mblk
	 */
	cimp = tl_resizemp(indmp, size);
	if (! cimp) {
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_conn_req:con_ind:allocb failure");
		tl_merror(wq, indmp, ENOMEM);
		freemsg(freemp);
		return;
	}

	DB_TYPE(cimp) = M_PROTO;
	ci = (struct T_conn_ind *) cimp->b_rptr;
	ci->PRIM_type  = T_CONN_IND;
	ci->SRC_offset = sizeof (struct T_conn_ind);
	ci->SRC_length = tep->te_ap.ta_alen;
	ci->SEQ_number = (long) tep;
	addr_startp = (char *) indmp->b_rptr + ci->SRC_offset;
	bcopy(tep->te_ap.ta_abuf, addr_startp, tep->te_ap.ta_alen);
	if (peer_tep->te_flag & TL_SETCRED) {
		ci->OPT_offset = TL_ALIGN(ci->SRC_offset + ci->SRC_length);
		ci->OPT_length = olen; /* because only 1 option */
		tl_fill_option((char *)(indmp->b_rptr + ci->OPT_offset),
					sizeof (tl_credopt_t),
					TL_OPT_PEER_CRED,
					(char *) &tep->te_cred);
	} else {
		ci->OPT_offset = 0;
		ci->OPT_length = 0;
	}

	/*
	 * register connection request with server peer
	 * pre-pend to list of incoming connections
	 */
	tep->te_iconp = peer_tep->te_iconp;
	peer_tep->te_iconp = tep;
	peer_tep->te_nicon++;

	tep->te_oconp = peer_tep; /* can this be in te_conp ? */

	peer_tep->te_state = NEXTSTATE(TE_CONN_IND, peer_tep->te_state);
	/*
	 * send the T_CONN_IND message
	 */
	(void) putnext(peer_tep->te_rq, cimp);

	freemsg(freemp);
}



static void
tl_conn_res(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t *tep, *sep;
	struct T_conn_res *cres;
	long msz, olen, ooff;
	int err;
	char *addr_startp;
	tl_endpt_t *acc_ep, *cl_ep;
	tl_endpt_t *itep, *prev_itep;
	long size;
	mblk_t *ackmp, *respmp, *freemp;
	mblk_t *dimp, *ccmp;
	struct T_discon_ind *di;
	struct T_conn_con *cc;

	/*
	 * preallocate memory for:
	 * 1. max of T_ERROR_ACK and T_OK_ACK
	 *	==> known max T_ERROR_ACK
	 * 2. max of T_DISCON_IND and T_CONN_CON
	 */
	ackmp = allocb(sizeof (struct T_error_ack), BPRI_MED);
	if (! ackmp) {
		tl_memrecover(wq, mp, sizeof (struct T_error_ack));
		return;
	}
	/*
	 * memory committed for T_OK_ACK/T_ERROR_ACK now
	 * will be commited for T_DISCON_IND/T_CONN_CON later
	 */


	tep = (tl_endpt_t *) wq->q_ptr;
	cres = (struct T_conn_res *) mp->b_rptr;
	msz = MBLKL(mp);

	/*
	 * validate state
	 */
	if (tep->te_state != TS_WRES_CIND) {
		STRLOG(TL_ID, tep->te_minor, 1,
			SL_TRACE|SL_ERROR,
			"tl_wput:T_CONN_RES:out of state, state=%d",
			tep->te_state);
		tl_error_ack(wq, ackmp, TOUTSTATE, 0, T_CONN_RES);
		freemsg(mp);
		return;
	}

	/*
	 * validate the message
	 * Note: dereference fields in struct inside message only
	 * after validating the message length.
	 */
	if (msz < sizeof (struct T_conn_res)) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_conn_res:invalid message length");
		tl_error_ack(wq, ackmp, TSYSERR, EINVAL, T_CONN_RES);
		freemsg(mp);
		return;
	}
	olen = cres->OPT_length;
	ooff = cres->OPT_offset;
	if (((olen > 0) && ((ooff+olen) > msz))) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_conn_res:invalid message");
		tl_error_ack(wq, ackmp, TSYSERR, EINVAL, T_CONN_RES);
		freemsg(mp);
		return;
	}
	if (olen) {
		/*
		 * no opts in connect res
		 * supported in this provider
		 */
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_conn_res:options not supported in message");
		tl_error_ack(wq, ackmp, TBADOPT, 0, T_CONN_RES);
		freemsg(mp);
		return;
	}

	tep->te_state = NEXTSTATE(TE_CONN_RES, tep->te_state);
	/*
	 * find accepting endpoint
	 */
	sep = tl_olistp;
	while (sep) {
		if (cres->QUEUE_ptr == sep->te_rq)
			break;
		sep = sep->te_nextp;
	}
	acc_ep = sep;		/* NULL if not found! */

	/*
	 * get client endpoint - same as SEQ_number in this implementation
	 * comment: possible to do only in local transports.
	 */
	cl_ep = (tl_endpt_t *) cres->SEQ_number;
	if (! cl_ep) {
		STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE|SL_ERROR,
			"tl_conn_res:remote endpoint sequence number bad");
		tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
		tl_error_ack(wq, ackmp, TBADSEQ, 0, T_CONN_RES);
		freemsg(mp);
		return;
	}

	/*
	 * if endpoint does not exist - nack it
	 */
	if (acc_ep == NULL) {
		STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE|SL_ERROR,
			"tl_conn_res:bad accepting endpoint");
		tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
		tl_error_ack(wq, ackmp, TBADF, 0, T_CONN_RES);
		freemsg(mp);
		return;
	}

	/*
	 * validate that accepting endpoint, if different from listening
	 * has address bound => state is TS_IDLE
	 * TROUBLE in XPG4 !!?
	 */
	if ((tep != acc_ep) && (acc_ep->te_state != TS_IDLE)) {
		STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE|SL_ERROR,
			"tl_conn_res:accepting endpoint has no address bound,"
			"state=%d", acc_ep->te_state);
		tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
		tl_error_ack(wq, ackmp, TOUTSTATE, 0, T_CONN_RES);
		freemsg(mp);
		return;
	}

	/*
	 * validate if accepting endpt same as listening, then
	 * no other incoming connection should be on the queue
	 */

	if ((tep == acc_ep) && (tep->te_nicon > 1)) {
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_conn_res: > 1 conn_ind on listener-acceptor");
		tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
		tl_error_ack(wq, ackmp, TBADF, 0, T_CONN_RES);
		freemsg(mp);
		return;
	}

	/*
	 * Mark for deletion, the entry corresponding to client
	 * on list of pending connections made by the listener
	 *  search list to see if client is one of the
	 * recorded as a listener and use "itep" value later for deleting
	 */
	prev_itep = NULL;
	itep = tep->te_iconp;
	while (itep) {
		if (itep == cl_ep)
			break;
		prev_itep = itep;
		itep = itep->te_iconp;
	}

	if (! itep) {
		STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE|SL_ERROR,
			"tl_conn_res:no client in listener list");
		tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
		tl_error_ack(wq, ackmp, TBADSEQ, 0, T_CONN_RES);
		freemsg(mp);
		return;
	}

	/*
	 * validate client state to be TS_WCON_CREQ
	 */
	err = 0;
	if (cl_ep->te_state != TS_WCON_CREQ) {
		err = ECONNREFUSED;
		/*
		 * T_DISCON_IND sent later after commiting memory
		 * and acking validity of request
		 */
		STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE,
			"tl_conn_res:peer in bad state");
	}

	/*
	 * preallocate now for T_DISCON_IND or T_CONN_CONN
	 * ack validity of request (T_OK_ACK) after memory commited
	 */

	if (err)
		size = sizeof (struct T_discon_ind);
	else {
		/*
		 * calculate length of T_CONN_CON message
		 */
		olen = 0;
		if (cl_ep->te_flag & TL_SETCRED) {
			olen = sizeof (struct opthdr) +
				OPTLEN(sizeof (tl_credopt_t));
		}
		size = TL_ALIGN(sizeof (struct T_conn_con) +
				acc_ep->te_ap.ta_alen) + olen;
	}
	if (! tl_preallocate(mp, &respmp, &freemp, size)) {
		/*
		 * roll back state changes
		 */
		tep->te_state = TS_WRES_CIND;
		tl_memrecover(wq, mp, size);
		freemsg(ackmp);
		return;
	}

	/*
	 * Now ack validity of request
	 */
	if (tep->te_nicon == 1) {
		if (tep == acc_ep)
			tep->te_state = NEXTSTATE(TE_OK_ACK2, tep->te_state);
		else
			tep->te_state = NEXTSTATE(TE_OK_ACK3, tep->te_state);
	} else
		tep->te_state = NEXTSTATE(TE_OK_ACK4, tep->te_state);

	tl_ok_ack(wq, ackmp, T_CONN_RES);

	/*
	 * send T_DISCON_IND now if client state validation failed earlier
	 */
	if (err) {
		/*
		 * flush the queues - why always ?
		 */
		(void) putnextctl1(acc_ep->te_rq, M_FLUSH, FLUSHRW);

		dimp = tl_resizemp(respmp, size);
		if (! dimp) {
			STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
				"tl_conn_res:con_ind:allocb failure");
			tl_merror(wq, respmp, ENOMEM);
			freemsg(freemp);
			return;
		}
		if (dimp) {
			/* no user data in provider generated discon ind */
			freemsg(dimp->b_cont);
			dimp->b_cont = NULL;
		}

		DB_TYPE(dimp) = M_PROTO;
		di = (struct T_discon_ind *) dimp->b_rptr;
		di->DISCON_reason = err;
		di->SEQ_number = BADSEQNUM;

		tep->te_state = NEXTSTATE(TE_DISCON_IND1, tep->te_state);
		/*
		 * send T_DISCON_IND message
		 */
		(void) putnext(acc_ep->te_rq, dimp);
		freemsg(freemp);
		return;
	}

	/*
	 * now start connecting the accepting endpoint
	 */
	if (tep != acc_ep)
		acc_ep->te_state = NEXTSTATE(TE_PASS_CONN, acc_ep->te_state);

	/*
	 * prepare connect confirm T_CONN_CON message
	 */

	/*
	 * allocate the message - original data blocks
	 * retained in the returned mblk
	 */
	ccmp = tl_resizemp(respmp, size);
	if (! ccmp) {
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_conn_res:conn_con:allocb failure");
		tl_merror(wq, respmp, ENOMEM);
		freemsg(freemp);
		return;
	}

	DB_TYPE(ccmp) = M_PROTO;
	cc = (struct T_conn_con *) ccmp->b_rptr;
	cc->PRIM_type  = T_CONN_CON;
	cc->RES_offset = sizeof (struct T_conn_con);
	cc->RES_length = acc_ep->te_ap.ta_alen;
	addr_startp = (char *) ccmp->b_rptr + cc->RES_offset;
	bcopy(acc_ep->te_ap.ta_abuf, addr_startp, acc_ep->te_ap.ta_alen);
	if (cl_ep->te_flag & TL_SETCRED) {
		cc->OPT_offset = TL_ALIGN(cc->RES_offset + cc->RES_length);
		cc->OPT_length = olen;
		tl_fill_option((char *)(ccmp->b_rptr + cc->OPT_offset),
					sizeof (tl_credopt_t),
					TL_OPT_PEER_CRED,
					(char *) &acc_ep->te_cred);
	} else {
		cc->OPT_offset = 0;
		cc->OPT_length = 0;
	}

	/*
	 * make connection linking
	 * accepting and client endpoints
	 */
	cl_ep->te_conp = acc_ep;
	acc_ep->te_conp = cl_ep;

	cl_ep->te_oconp = NULL;
	acc_ep->te_oconp = NULL;
	/*
	 * remove endpoint from incoming conenction
	 * list using "itep" value saved earlier
	 * delete client from list of incoming conenctions
	 */
	if (prev_itep) {
		/* delete it */
		prev_itep->te_iconp = itep->te_iconp;
	} else {
		/* first in list - delete it */
		tep->te_iconp = itep->te_iconp;
	}
	itep->te_iconp = NULL;	/* break link of deleted node */
	tep->te_nicon--;

	/*
	 * data blocks already linked in reallocb()
	 */

	/*
	 * link queues so that I_SENDFD will work
	 */
	WR(acc_ep->te_rq)->q_next = cl_ep->te_rq;
	WR(cl_ep->te_rq)->q_next = acc_ep->te_rq;

	/*
	 * change client state on TE_CONN_CON event
	 */
	cl_ep->te_state = NEXTSTATE(TE_CONN_CON, cl_ep->te_state);

	/*
	 * send T_CONN_CON up on client side
	 */
	putnext(cl_ep->te_rq, ccmp);

	freemsg(freemp);
}




static void
tl_discon_req(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t *tep;
	struct T_discon_req *dr;
	long msz;
	tl_endpt_t *peer_tep;
	tl_endpt_t *itep, *prev_itep;
	long size;
	mblk_t *ackmp, *dimp, *freemp, *respmp;
	struct T_discon_ind *di;
	int save_state, tep_state;


	/*
	 * preallocate memory for:
	 * 1. max of T_ERROR_ACK and T_OK_ACK
	 *	==> known max T_ERROR_ACK
	 * 2. for  T_DISCON_IND
	 */
	ackmp = allocb(sizeof (struct T_error_ack), BPRI_MED);
	if (! ackmp) {
		tl_memrecover(wq, mp, sizeof (struct T_error_ack));
		return;
	}
	/*
	 * memory committed for T_OK_ACK/T_ERROR_ACK now
	 * will be commited for T_DISCON_IND  later
	 */

	tep = (tl_endpt_t *) wq->q_ptr;
	dr = (struct T_discon_req *) mp->b_rptr;
	msz = MBLKL(mp);

	/*
	 * validate the state
	 */
	tep_state = save_state = tep->te_state;
	if (! (tep_state >= TS_WCON_CREQ && tep_state <= TS_WRES_CIND) &&
	    ! (tep_state >= TS_DATA_XFER && tep_state <= TS_WREQ_ORDREL)) {
		STRLOG(TL_ID, tep->te_minor, 1,
			SL_TRACE|SL_ERROR,
			"tl_wput:T_DISCON_REQ:out of state, state=%d",
			tep->te_state);
		tl_error_ack(wq, ackmp, TOUTSTATE, 0, T_DISCON_REQ);
		freemsg(mp);
		return;
	}
	tep->te_state  = NEXTSTATE(TE_DISCON_REQ, tep->te_state);

	/* validate the message */
	if (msz < sizeof (struct T_discon_req)) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_discon_req:invalid message");
		tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
		tl_error_ack(wq, ackmp, TSYSERR, EINVAL, T_DISCON_REQ);
		freemsg(mp);
		return;
	}

	/*
	 * if server, then validate that client exists
	 * by connection sequence number etc.
	 */
	if (tep->te_nicon > 0) { /* server */
		/*
		 * validate sequnce number
		 */
		if (! dr->SEQ_number) {
			STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE|SL_ERROR,
				"tl_discon_req:bad(null) sequence number");
			tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
			tl_error_ack(wq, ackmp, TBADSEQ, 0, T_DISCON_REQ);
			freemsg(mp);
			return;
		}
		peer_tep = (tl_endpt_t *) dr->SEQ_number;

		/*
		 * search server list for disconnect client
		 * - use value of "itep" later for deletion
		 */
		prev_itep = NULL;
		itep = tep->te_iconp;
		while (itep) {
			if (itep == peer_tep)
				break;
			prev_itep = itep;
			itep = itep->te_iconp;
		}

		if (! itep) {
			STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE|SL_ERROR,
				"tl_discon_req:no disconnect endpoint");
			tep->te_state = NEXTSTATE(TE_ERROR_ACK, tep->te_state);
			tl_error_ack(wq, ackmp, TBADSEQ, 0, T_DISCON_REQ);
			freemsg(mp);
			return;
		}
	}

	/*
	 * preallocate now for T_DISCON_IND
	 * ack validity of request (T_OK_ACK) after memory commited
	 */
	size = sizeof (struct T_discon_ind);
	if (! tl_preallocate(mp, &respmp, &freemp, size)) {
		/*
		 * roll back state changes
		 */
		tep->te_state = save_state;
		tl_memrecover(wq, mp, size);
		freemsg(ackmp);
		return;
	}

	/*
	 * prepare message to ack validity of request
	 */
	if (tep->te_nicon == 0)
		tep->te_state = NEXTSTATE(TE_OK_ACK1, tep->te_state);
	else
		if (tep->te_nicon == 1)
			tep->te_state = NEXTSTATE(TE_OK_ACK2, tep->te_state);
		else
			tep->te_state = NEXTSTATE(TE_OK_ACK4, tep->te_state);

	/*
	 * Flushing queues according to TPI
	 */
	if ((tep->te_state == TS_DATA_XFER) ||
	    (tep->te_state == TS_WIND_ORDREL) ||
	    (tep->te_state == TS_WREQ_ORDREL))
		(void) putnextctl1(RD(wq), M_FLUSH, FLUSHRW);

	/* send T_OK_ACK up  */
	tl_ok_ack(wq, ackmp, T_DISCON_REQ);

	/*
	 * now do disconnect business
	 */
	if (tep->te_nicon > 0) { /* listener */
		/*
		 * disconnect incoming connect request pending to tep
		 */
		if ((dimp = tl_resizemp(respmp, size)) == NULL) {
			STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE|SL_ERROR,
				"tl_discon_req: reallocb failed");
			tl_merror(wq, respmp, ENOMEM);
			freemsg(freemp);
			return;
		}
		di = (struct T_discon_ind *) dimp->b_rptr;
		di->SEQ_number = BADSEQNUM;
		peer_tep->te_state = NEXTSTATE(TE_DISCON_IND1,
						peer_tep->te_state);
		peer_tep->te_oconp = NULL;
		/*
		 * remove endpoint from incoming connection list
		 * using "itep" value saved earlier
		 * - remove disconnect client from list on server
		 */
		if (prev_itep) {
			/* delete it */
			prev_itep->te_iconp = itep->te_iconp;
		} else {
			/* first in list - delete it */
			tep->te_iconp = itep->te_iconp;
		}
		itep->te_iconp = NULL; /* break link of deleted node */
		tep->te_nicon--;

		tep->te_oconp = NULL;
	} else if (tep->te_oconp) { /* client */
		/*
		 * disconnect an outgoing request pending from tep
		 */
		peer_tep = tep->te_oconp; /* tep: client, peer_tep: server */

		if ((dimp = tl_resizemp(respmp, size)) == NULL) {
			STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE|SL_ERROR,
				"tl_discon_req: reallocb failed");
			tl_merror(wq, respmp, ENOMEM);
			freemsg(freemp);
			return;
		}
		/*
		 * delete client from server list and
		 * make state changes for server
		 */
		prev_itep = NULL;
		itep = peer_tep->te_iconp;
		while (itep) {
			if (itep == tep) {
				if (prev_itep) {
					/* delete it */
					prev_itep->te_iconp = itep->te_iconp;
				} else {
					/* first in list - delete it */
					peer_tep->te_iconp = itep->te_iconp;
				}
				if (peer_tep->te_nicon == 1)
					peer_tep->te_state =
					    NEXTSTATE(TE_DISCON_IND2,
							peer_tep->te_state);
				else
					peer_tep->te_state =
					    NEXTSTATE(TE_DISCON_IND3,
							peer_tep->te_state);
				peer_tep->te_nicon--;
				prev_itep = itep;
				itep = itep->te_iconp;
				/* break link of deleted node */
				prev_itep->te_iconp = NULL;
				break;
			} else {
				prev_itep = itep;
				itep = itep->te_iconp;
			}
		}

		di = (struct T_discon_ind *) dimp->b_rptr;
		di->SEQ_number = (long)tep;
		tep->te_oconp = NULL;
	} else if (tep->te_conp) { /* connected! */
		peer_tep = tep->te_conp;

		if ((dimp = tl_resizemp(respmp, size)) == NULL) {
			STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE|SL_ERROR,
				"tl_discon_req: reallocb failed");
			tl_merror(wq, respmp, ENOMEM);
			freemsg(freemp);
			return;
		}
		di = (struct T_discon_ind *) dimp->b_rptr;
		di->SEQ_number = BADSEQNUM;

		save_state = peer_tep->te_state;
		peer_tep->te_state = NEXTSTATE(TE_DISCON_IND1,
						peer_tep->te_state);
	} else {
		/* Not connected */
		ASSERT(WR(tep->te_rq)->q_next == NULL);
		freemsg(freemp);
		return;
	}

	/*
	 * Flush queues on peer before sending up
	 * T_DISCON_IND according to TPI
	 */
	if ((peer_tep->te_state == TS_DATA_XFER) ||
	    (peer_tep->te_state == TS_WIND_ORDREL) ||
	    (peer_tep->te_state == TS_WREQ_ORDREL))
		(void) putnextctl1(peer_tep->te_rq, M_FLUSH, FLUSHRW);

	DB_TYPE(dimp) = M_PROTO;
	di->PRIM_type  = T_DISCON_IND;
	di->DISCON_reason = ECONNRESET;

	/*
	 * data blocks already linked into dimp by tl_preallocate()
	 */
	/*
	 * send indication message to peer user module
	 */
	putnext(peer_tep->te_rq, dimp);
	if (tep->te_conp) {	/* disconnect pointers if connected */
		tep->te_conp = NULL;
		peer_tep->te_conp = NULL;
		/*
		 * unlink the streams
		 */
		(WR(tep->te_rq))->q_next = NULL;
		(WR(peer_tep->te_rq))->q_next = NULL;
	}

	freemsg(freemp);
}


static void
tl_info_req(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t *tep;
	int ack_sz;
	mblk_t *ackmp;
	struct T_info_ack *ia;
	long tl_tidusz;

	tep = (tl_endpt_t *) wq->q_ptr;

	ack_sz = sizeof (struct T_info_ack);
	ackmp = tl_reallocb(mp, ack_sz);
	if (! ackmp) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_info_req: reallocb failed");
		tl_memrecover(wq, mp, ack_sz);
		return;
	}
	/*
	 * Note: T_INFO_REQ message has only PRIM_type field
	 * so it is already validated earlier.
	 */

	/*
	 * Deduce TIDU size to use
	 */
	if (strmsgsz)
		tl_tidusz = strmsgsz;
	else {
		/*
		 * Note: "strmsgsz" being 0 has semantics that streams
		 * message sizes can be unlimited. We use a defined constant
		 * instead.
		 */
		tl_tidusz = TL_TIDUSZ;
	}

	/*
	 * fill in T_INFO_ACK contents
	 */
	DB_TYPE(ackmp) = M_PCPROTO;
	ia = (struct T_info_ack *) ackmp->b_rptr;

	if (tep->te_mode == TL_TICLTS) {
		bcopy((char *)&tl_clts_info_ack, (char *) ia,
			sizeof (tl_clts_info_ack));
		ia->TSDU_size = tl_tidusz; /* TSDU and TIDU size are same */
	} else {
		bcopy((char *)&tl_cots_info_ack, (char *) ia,
			sizeof (tl_cots_info_ack));
		if (tep->te_mode == TL_TICOTSORD)
			ia->SERV_type = T_COTS_ORD;
	}
	ia->TIDU_size = tl_tidusz;
	ia->CURRENT_state = tep->te_state;

	/*
	 * send ack message
	 */
	(void) qreply(wq, ackmp);
}

static int
tl_data(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t *tep;
	union T_primitives *prim;
	long msz;
	tl_endpt_t *peer_tep;
	queue_t *peer_rq;

	tep = (tl_endpt_t *) wq->q_ptr;
	prim = (union T_primitives *) mp->b_rptr;
	msz = MBLKL(mp);

	if (tep->te_mode == TL_TICLTS) {
		STRLOG(TL_ID, tep->te_minor, 2, SL_TRACE|SL_ERROR,
			"tl_wput:clts:unattached M_DATA");
		tl_merror(wq, mp, EPROTO);
		return (-1);
	}

	/*
	 * connection oriented provider
	 */
	switch (tep->te_state) {
	case TS_IDLE:
		/*
		 * Other end not here - do nothing.
		 */
		freemsg(mp);
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_data:cots with endpoint idle");
		return (0);

	case TS_DATA_XFER:
		/* valid states */
		break;
	case TS_WREQ_ORDREL:
		if (tep->te_conp == NULL) {
			/*
			 * Other end closed - generate discon_ind
			 * with reason 0 to cause an EPIPE but no
			 * read side error on AF_UNIX sockets.
			 */
			freemsg(mp);
			STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
				"tl_data: WREQ_ORDREL and no peer");
			tl_discon_ind(tep, 0);
			return (0);
		}
		break;
	default:
		/* invalid state for event TE_DATA_REQ */
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_data:cots:out of state");
		tl_merror(wq, mp, EPROTO);
		return (-1);
	}
	/*
	 * tep->te_state = NEXTSTATE(TE_DATA_REQ, tep->te_state);
	 * (State stays same on this event)
	 */

	if (DB_TYPE(mp) == M_PROTO) {
		if (msz < sizeof (struct T_data_req)) {
			STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
				"tl_data:M_PROTO:invalid message");
			tl_merror(wq, mp, EPROTO);
			return (-1);
		}
	}

	/*
	 * get connected endpoint
	 */
	peer_tep = tep->te_conp;

	peer_rq = peer_tep->te_rq;

	/*
	 * Put it back if flow controlled except when we are closing.
	 * Note: Messages already on queue when we are closing is bounded
	 * so we can ignore flow control.
	 */
	if (! canputnext(peer_rq) && !(tep->te_flag & TL_CLOSING)) {
		(void) putbq(wq, mp);
		return (-1);
	}

	/*
	 * validate peer state
	 */
	switch (peer_tep->te_state) {
	case TS_DATA_XFER:
	case TS_WIND_ORDREL:
		/* valid states */
		break;
	default:
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_data:rx side:invalid state");
		tl_merror(WR(peer_rq), mp, EPROTO);
		return (0);
	}
	if (DB_TYPE(mp) == M_PROTO) {
		/* reuse message block - just change REQ to IND */
		prim->type = T_DATA_IND;
	}
	/*
	 * peer_tep->te_state = NEXTSTATE(TE_DATA_IND, peer_tep->te_state);
	 * (peer state stays same on this event)
	 */
	/*
	 * send data to connected peer
	 */
	(void) putnext(peer_rq, mp);
	return (0);
}


static int
tl_exdata(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t *tep;
	union T_primitives *prim;
	long msz;
	tl_endpt_t *peer_tep;
	queue_t *peer_rq;

	tep = (tl_endpt_t *) wq->q_ptr;
	prim = (union T_primitives *) mp->b_rptr;
	msz = MBLKL(mp);

	/*
	 * validate state
	 */
	switch (tep->te_state) {
	case TS_IDLE:
		/*
		 * Other end not here - do nothing.
		 */
		freemsg(mp);
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_exdata:cots with endpoint idle");
		return (0);

	case TS_DATA_XFER:
		/* valid states */
		break;
	case TS_WREQ_ORDREL:
		if (tep->te_conp == NULL) {
			/*
			 * Other end closed - generate discon_ind
			 * with reason 0 to cause an EPIPE but no
			 * read side error on AF_UNIX sockets.
			 */
			freemsg(mp);
			STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
				"tl_exdata: WREQ_ORDREL and no peer");
			tl_discon_ind(tep, 0);
			return (0);
		}
		break;
	default:
		STRLOG(TL_ID, tep->te_minor, 1,
			SL_TRACE|SL_ERROR,
			"tl_wput:T_EXDATA_REQ:out of state, state=%d",
			tep->te_state);
		tl_merror(wq, mp, EPROTO);
		return (-1);
	}
	/*
	 * tep->te_state = NEXTSTATE(TE_EXDATA_REQ, tep->te_state);
	 * (state stayes same on this event)
	 */

	if (msz < sizeof (struct T_exdata_req)) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_exdata:invalid message");
		tl_merror(wq, mp, EPROTO);
		return (-1);
	}

	/*
	 * get connected endpoint
	 */
	peer_tep = tep->te_conp;

	peer_rq = peer_tep->te_rq;

	/*
	 * Put it back if flow controlled except when we are closing.
	 * Note: Messages already on queue when we are closing is bounded
	 * so we can ignore flow control.
	 */
	if (! canputnext(peer_rq) && !(tep->te_flag & TL_CLOSING)) {
		(void) putbq(wq, mp);
		return (-1);
	}

	/*
	 * validate state on peer
	 */
	switch (peer_tep->te_state) {
	case TS_DATA_XFER:
	case TS_WIND_ORDREL:
		/* valid states */
		break;
	default:
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_exdata:rx side:invalid state");
		tl_merror(WR(peer_rq), mp, EPROTO);
		return (0);
	}
	/*
	 * peer_tep->te_state = NEXTSTATE(TE_DATA_IND, peer_tep->te_state);
	 * (peer state stays same on this event)
	 */
	/*
	 * reuse message block
	 */
	prim->type = T_EXDATA_IND;

	/*
	 * send data to connected peer
	 */
	(void) putnext(peer_rq, mp);
	return (0);
}



static int
tl_ordrel(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_endpt_t *tep;
	union T_primitives *prim;
	long msz;
	tl_endpt_t *peer_tep;
	queue_t *peer_rq;

	tep = (tl_endpt_t *) wq->q_ptr;
	prim = (union T_primitives *) mp->b_rptr;
	msz = MBLKL(mp);

	/*
	 * validate state
	 */
	switch (tep->te_state) {
	case TS_DATA_XFER:
	case TS_WREQ_ORDREL:
		/* valid states */
		break;
	default:
		STRLOG(TL_ID, tep->te_minor, 1,
			SL_TRACE|SL_ERROR,
			"tl_wput:T_ORDREL_REQ:out of state, state=%d",
			tep->te_state);
		tl_merror(wq, mp, EPROTO);
		return (-1);
	}
	tep->te_state = NEXTSTATE(TE_ORDREL_REQ, tep->te_state);

	if (msz < sizeof (struct T_ordrel_req)) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_ordrel:invalid message");
		tl_merror(wq, mp, EPROTO);
		return (-1);
	}

	/*
	 * get connected endpoint
	 */
	if (tep->te_conp == NULL) {
		/* Peer closed */
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE,
			"tl_ordrel: peer gone");
		freemsg(mp);
		return (0);
	}
	peer_tep = tep->te_conp;

	peer_rq = peer_tep->te_rq;

	/*
	 * Put it back if flow controlled except when we are closing.
	 * Note: Messages already on queue when we are closing is bounded
	 * so we can ignore flow control.
	 */
	if (! canputnext(peer_rq) && !(tep->te_flag & TL_CLOSING)) {
		(void) putbq(wq, mp);
		return (-1);
	}

	/*
	 * validate state on peer
	 */
	switch (peer_tep->te_state) {
	case TS_DATA_XFER:
	case TS_WIND_ORDREL:
		/* valid states */
		break;
	default:
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_ordrel:rx side:invalid state");
		tl_merror(WR(peer_rq), mp, EPROTO);
		return (0);
	}
	peer_tep->te_state = NEXTSTATE(TE_ORDREL_IND, peer_tep->te_state);

	/*
	 * reuse message block
	 */
	prim->type = T_ORDREL_IND;
	STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE,
		"tl_ordrel: send ordrel_ind");

	/*
	 * send data to connected peer
	 */
	(void) putnext(peer_rq, mp);
	if (peer_tep->te_state == TS_IDLE) { /* break pointer connections */
		tep->te_conp = NULL;
		peer_tep->te_conp = NULL;
		/*
		 * unlink the streams
		 */
		(WR(tep->te_rq))->q_next = NULL;
		(WR(peer_tep->te_rq))->q_next = NULL;
	}

	return (0);
}


static void
tl_uderr(wq, mp, err)
queue_t *wq;
mblk_t *mp;
long err;
{
	unsigned int err_sz;
	tl_endpt_t *tep;
	struct T_unitdata_req *udreq;
	mblk_t *err_mp;
	long alen;
	long olen;
	struct T_uderror_ind *uderr;
	char *addr_startp;

	err_sz = sizeof (struct T_uderror_ind);
	tep = (tl_endpt_t *) wq->q_ptr;
	udreq = (struct T_unitdata_req *) mp->b_rptr;
	alen = udreq->DEST_length;
	olen = udreq->OPT_length;

	if (alen > 0)
		err_sz = TL_ALIGN(err_sz + alen);
	if (olen > 0)
		err_sz += olen;

	err_mp = allocb(err_sz, BPRI_MED);
	if (! err_mp) {
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_uderr:allocb failure");
		/*
		 * Note: no rollback of state needed as it does
		 * not change in connectionless transport
		 */
		tl_memrecover(wq, mp, err_sz);
		return;
	}

	err_mp->b_wptr = err_mp->b_rptr + err_sz;
	uderr = (struct T_uderror_ind *) err_mp->b_rptr;
	uderr->PRIM_type = T_UDERROR_IND;
	uderr->ERROR_type = err;
	uderr->DEST_length = alen;
	if (alen <= 0) {
		uderr->DEST_offset = 0;
	} else {
		uderr->DEST_offset =
			sizeof (struct T_uderror_ind);
		addr_startp  = (char *)mp->b_rptr
			+ udreq->DEST_offset;
		bcopy(addr_startp, (char *)err_mp->b_rptr + uderr->DEST_offset,
			alen);
	}
	if (olen <= 0) {
		uderr->OPT_offset = 0;
	} else {
		uderr->OPT_offset = TL_ALIGN(sizeof (struct T_uderror_ind) +
						uderr->DEST_length);
		addr_startp  = (char *) mp->b_rptr
			+ udreq->OPT_offset;
		bcopy(addr_startp, (char *)err_mp->b_rptr+uderr->OPT_offset,
			olen);
	}
	freemsg(mp);

	/*
	 * send indication message
	 */
	tep->te_state = NEXTSTATE(TE_UDERROR_IND, tep->te_state);

	qreply(wq, err_mp);
}


static int
tl_unitdata(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_addr_t destaddr;
	char *addr_startp;
	tl_endpt_t *tep, *peer_tep;
	mblk_t *ui_mp;
	struct T_unitdata_ind *udind;
	struct T_unitdata_req *udreq;
	long msz, alen, aoff, olen, ui_sz;

	tep = (tl_endpt_t *) wq->q_ptr;
	udreq = (struct T_unitdata_req *) mp->b_rptr;
	msz = MBLKL(mp);

	/*
	 * validate the state
	 */
	if (tep->te_state != TS_IDLE) {
		STRLOG(TL_ID, tep->te_minor, 1,
			SL_TRACE|SL_ERROR,
			"tl_wput:T_CONN_REQ:out of state");
		tl_merror(wq, mp, EPROTO);
		return (-1);
	}
	/*
	 * tep->te_state = NEXTSTATE(TE_UNITDATA_REQ, tep->te_state);
	 * (state does not change on this event)
	 */

	/*
	 * validate the message
	 * Note: dereference fields in struct inside message only
	 * after validating the message length.
	 */
	if (msz < sizeof (struct T_unitdata_req)) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_unitdata:invalid message length");
		tl_merror(wq, mp, EINVAL);
		return (-1);
	}
	alen = udreq->DEST_length;
	aoff = udreq->DEST_offset;
	olen = udreq->OPT_length;
	if ((alen < 0) ||
	    (aoff < 0) ||
	    ((alen > 0) && ((aoff+alen) > msz)) ||
	    (alen > (msz-sizeof (struct T_unitdata_req)))) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_unitdata:invalid unit data message");
		tl_merror(wq, mp, EINVAL);
		return (-1);
	}
	if (alen == 0 || olen) {
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_unitdata:option use(unsupported) or zero len addr");
		tl_uderr(wq, mp, EPROTO);
		return (0);
	}
#ifdef DEBUG
	/*
	 * Mild form of ASSERT()ion to detect broken TPI apps.
	 * if (! assertion)
	 *	log warning;
	 */
	if (! (aoff >= sizeof (struct T_unitdata_req))) {
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_unitdata:addr overlaps TPI message");
	}
#endif
	/*
	 * get destination endpoint
	 */
	destaddr.ta_alen = alen;
	destaddr.ta_abuf = (char *) (mp->b_rptr + aoff);

	if (TL_EQADDR(&destaddr, &tep->te_lastdest)) {
		/*
		 * Same as cached destination
		 */
		peer_tep = tep->te_lastep;
	} else  {
		/*
		 * Not the same as cached destination , need to search
		 * destination in the list of open endpoints
		 */
		if ((peer_tep = tl_addr_in_use(tep->te_mode, &destaddr))
			== NULL) {
			STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
				"tl_unitdata:no one at destination address");
			tl_uderr(wq, mp, EFAULT);
			return (0);
		}
		/*
		 * Save the address in cache if it fits, else clear
		 * the cache.
		 */
		if (destaddr.ta_alen <= TL_CLTS_CACHADDR_MAX) {
			tep->te_lastdest.ta_alen = destaddr.ta_alen;
			bcopy(destaddr.ta_abuf, tep->te_lastdest.ta_abuf,
				tep->te_lastdest.ta_alen);
			tep->te_lastep = peer_tep;
		} else {
			tep->te_lastdest.ta_alen = -1;
			tep->te_lastep = NULL;
		}
	}

	if (peer_tep->te_state != TS_IDLE) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_unitdata:provider in invalid state");
		tl_uderr(wq, mp, EPROTO);
		return (0);
	}

	/*
	 * Put it back if flow controlled except when we are closing.
	 * Note: Messages already on queue when we are closing is bounded
	 * so we can ignore flow control.
	 */
	if (!canputnext(peer_tep->te_rq) && !(tep->te_flag & TL_CLOSING)) {
		/* record what we are flow controlled on */
		tep->te_flowrq = peer_tep->te_rq;
		(void) putbq(wq, mp);
		return (-1);
	}
	/*
	 * prepare indication message
	 */

	/*
	 * calculate length of message
	 */
	olen = 0;
	if (peer_tep->te_flag & TL_SETCRED) {
		olen = sizeof (struct opthdr)+ OPTLEN(sizeof (tl_credopt_t));
					/* 1 option only */
	}

	ui_sz = TL_ALIGN(sizeof (struct T_unitdata_ind) + tep->te_ap.ta_alen) +
		olen;
	ui_mp = allocb(ui_sz, BPRI_MED);
	if (! ui_mp) {
		STRLOG(TL_ID, tep->te_minor, 4, SL_TRACE,
			"tl_unitdata:allocb failure:message queued");
		tl_memrecover(wq, mp, ui_sz);
		return (-1);
	}

	/*
	 * fill in T_UNITDATA_IND contents
	 */
	DB_TYPE(ui_mp) = M_PROTO;
	ui_mp->b_wptr = ui_mp->b_rptr + ui_sz;
	udind =  (struct T_unitdata_ind *) ui_mp->b_rptr;
	udind->PRIM_type = T_UNITDATA_IND;
	udind->SRC_offset = sizeof (struct T_unitdata_ind);
	udind->SRC_length = tep->te_ap.ta_alen;
	addr_startp = (char *)ui_mp->b_rptr + udind->SRC_offset;
	bcopy(tep->te_ap.ta_abuf, addr_startp, tep->te_ap.ta_alen);
	if (peer_tep->te_flag & TL_SETCRED) {
		udind->OPT_offset = TL_ALIGN(udind->SRC_offset +
						udind->SRC_length);
		udind->OPT_length = olen; /* because 1 opt only */
		tl_fill_option((char *) (ui_mp->b_rptr + udind->OPT_offset),
					sizeof (tl_credopt_t),
					TL_OPT_PEER_CRED,
					(char *) &tep->te_cred);
	} else {
		udind->OPT_offset = 0;
		udind->OPT_length = 0;
	}

	/*
	 * relink data blocks from mp to ui_mp
	 */
	ui_mp->b_cont = mp->b_cont;
	mp->b_cont = NULL;
	freemsg(mp);

	/*
	 * send indication message
	 */
	peer_tep->te_state = NEXTSTATE(TE_UNITDATA_IND, peer_tep->te_state);
	(void) putnext(peer_tep->te_rq, ui_mp);
	return (0);
}



/*
 * check if a given addr is in use
 * endpoint ptr returned or NULL
 * if not found
 */
static tl_endpt_t *
tl_addr_in_use(mode, ap)
int mode;
tl_addr_t *ap;
{
	tl_endpt_t *tep;

	tep = tl_olistp;
	while (tep) {
		/*
		 * compare full address buffers
		 */
		if (mode == tep->te_mode && TL_EQADDR(ap, &tep->te_ap)) {
			/* addr is in use */
			return (tep);
		}
		tep = tep->te_nextp;
	}
	return (NULL);
}


/*
 * generate a free addr and return it in struct pointed by ap
 * but alloc'ing space for address buffer
 *
 * return 0 for failure
 * return non-zero for success
 */

static int
tl_get_any_addr(mode, ap)
int mode;
tl_addr_t *ap;
{

	tl_addr_t *tap;
	char *abuf;

	/*
	 * do a sanity check for wraparound case
	 */

	/*
	 * check if default addr is in use
	 * if it is - bump it and try again
	 */
	tap = &tl_defaultaddr;
	for (;;) {
		if (tl_addr_in_use(mode, tap) != NULL) {
			/*
			 * bump default addr
			 */
			(*(int *) tap->ta_abuf)++;
		} else {
			/*
			 * found free address - copy it in
			 * new buffer and return it
			 */
			abuf = (char *) kmem_alloc(tap->ta_alen, KM_NOSLEEP);
			if (abuf == NULL)
				return (0); /* failure return */
			ap->ta_alen = tap->ta_alen;
			ap->ta_abuf = abuf;
			bcopy(tap->ta_abuf, ap->ta_abuf, tap->ta_alen);

			/*
			 * bump default address for next time
			 */
			(*(int *) tap->ta_abuf)++;
			return (1); /* successful return */
		}
		/* NOTREACHED */
	}
	/* NOTREACHED */
}

/*
 * snarfed from internet mi.c
 * reallocate if necessary, else reuse message block
 */

static mblk_t *
tl_reallocb(mp, new_size)
	mblk_t	*mp;
	int	new_size;
{
	mblk_t	*mp1;

	if (DB_REF(mp) == 1) {
		if ((mp->b_datap->db_lim - mp->b_rptr) >= new_size) {
			mp->b_wptr = mp->b_rptr + new_size;
			return (mp);
		}
		if ((MBLKSIZE(mp)) >= new_size) {
			mp->b_rptr = DB_BASE(mp);
			mp->b_wptr = mp->b_rptr + new_size;
			return (mp);
		}
	}
	if (mp1 = allocb(new_size, BPRI_MED)) {
		mp1->b_wptr = mp1->b_rptr + new_size;
		DB_TYPE(mp1) = DB_TYPE(mp);
		mp1->b_cont = mp->b_cont;
		freeb(mp);
	}
	return (mp1);
}

static mblk_t *
tl_resizemp(mp, new_size)
mblk_t *mp;
int new_size;
{
	if (! mp || (DB_REF(mp) > 1) || (MBLKSIZE(mp) < new_size))
		return (NULL);

	if ((mp->b_datap->db_lim - mp->b_rptr) >= new_size) {
		mp->b_wptr = mp->b_rptr + new_size;
		return (mp);
	}
	mp->b_rptr = DB_BASE(mp);
	mp->b_wptr = mp->b_rptr + new_size;
	return (mp);
}



static int
tl_preallocate(reusemp, outmpp, freempp, size)
mblk_t *reusemp, **outmpp, **freempp;
int size;
{
	if ((reusemp == NULL) || (DB_REF(reusemp) > 1) ||
	    (MBLKSIZE(reusemp) < size)) {
		if (! (*outmpp = allocb(size, BPRI_MED))) {
			/* out of memory */
			*freempp = (mblk_t *)NULL;
			return (0);
		}
		*freempp = reusemp;
		(*outmpp)->b_cont = reusemp->b_cont;
		reusemp->b_cont = (mblk_t *) NULL;
	} else {
		*outmpp = reusemp;
		*freempp = (mblk_t *)NULL;
	}
	return (1);
}

static void
tl_co_unconnect(tep)
tl_endpt_t *tep;
{
	tl_endpt_t *peer_tep, *itep, *prev_itep;
	tl_endpt_t *cl_tep, *srv_tep;
	mblk_t *d_mp;

	/*
	 * unlink peer queue if connected
	 */
	peer_tep = tep->te_conp;
	if (peer_tep)
		WR(peer_tep->te_rq)->q_next = NULL;

	/*
	 * disconnect processing cleanup the te structure
	 */

	if (tep->te_nicon > 0) {
		/*
		 * If incoming requests pending, change state
		 * of clients on disconnect ind event and send
		 * discon_ind pdu to modules above them
		 * for server: all clients get disconnect
		 */

		cl_tep = tep->te_iconp;
		while (cl_tep) {
			d_mp = tl_discon_ind_alloc(ECONNREFUSED, BADSEQNUM);
			if (! d_mp) {
				STRLOG(TL_ID, tep->te_minor, 3,
					SL_TRACE|SL_ERROR,
					"tl_co_unconnect:icmng:allocb failure");
				return;
			}
			cl_tep->te_oconp = NULL;
			cl_tep->te_state = NEXTSTATE(TE_DISCON_IND1,
							cl_tep->te_state);
			(void) putnext(cl_tep->te_rq, d_mp);
			tep->te_nicon--;
			cl_tep = cl_tep->te_iconp;
		}
	} else if (tep->te_oconp) {
		/*
		 * If outgoing request pending, change state
		 * of server on discon ind event
		 */

		srv_tep = tep->te_oconp;
		/*
		 * send discon_ind to server
		 */
		d_mp = tl_discon_ind_alloc(ECONNRESET, (long) tep);
		if (! d_mp) {
			STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
				"tl_co_unconnect:outgoing:allocb failure");
			return;
		}

		/*
		 * remove requests from this endpoint on peer server
		 * => remove client from server list
		 */
		prev_itep = NULL;
		itep = srv_tep->te_iconp;
		while (itep) {
			if (itep == tep) {
				if (prev_itep) {
					/* delete it */
					prev_itep->te_iconp = itep->te_iconp;
				} else {
					/* first in list - delete it */
					srv_tep->te_iconp = itep->te_iconp;
				}
				/* change state on server */
				if (srv_tep->te_nicon == 1) {
					srv_tep->te_state =
						NEXTSTATE(TE_DISCON_IND2,
							srv_tep->te_state);
				} else {
					srv_tep->te_state =
						NEXTSTATE(TE_DISCON_IND3,
							srv_tep->te_state);
				}
				srv_tep->te_nicon--;
				prev_itep = itep;
				itep = itep->te_iconp;
				/* break link of deleted node */
				prev_itep->te_iconp = NULL;
				break;
			} else {
				prev_itep = itep;
				itep = itep->te_iconp;
			}
		}
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE,
			"tl_co_unconnect:ocon: discon_ind state %d",
				srv_tep->te_state);
		(void) putnext(srv_tep->te_rq, d_mp);
		tep->te_oconp = NULL;
	} else if (tep->te_conp) {
		/*
		 * unconnect existing connection
		 * If connected, change state of peer on
		 * discon ind event and send discon ind pdu
		 * to module above it
		 */

		peer_tep = tep->te_conp;
		if (peer_tep->te_mode == TL_TICOTSORD &&
		    (peer_tep->te_state == TS_WIND_ORDREL ||
		    (peer_tep->te_state == TS_DATA_XFER))) {
			/*
			 * send ordrel ind
			 */
			STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE,
			"tl_co_unconnect:connected: ordrel_ind state %d->%d",
				peer_tep->te_state,
				NEXTSTATE(TE_ORDREL_IND, peer_tep->te_state));
			d_mp = tl_ordrel_ind_alloc();
			if (! d_mp) {
				STRLOG(TL_ID, tep->te_minor, 3,
				    SL_TRACE|SL_ERROR,
				    "tl_co_unconnect:connected:allocb failure");
				return;
			}
			peer_tep->te_state =
				NEXTSTATE(TE_ORDREL_IND, peer_tep->te_state);
			(void) putnext(peer_tep->te_rq, d_mp);
			/*
			 * Handle flow control case.
			 * This will generate a t_discon_ind message with
			 * reason 0 if there is data queued on the write
			 * side.
			 */
			qenable(WR(peer_tep->te_rq));
		} else {
			STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
				"tl_co_unconnect: state %d", tep->te_state);
			tl_discon_ind(peer_tep, ECONNRESET);
		}
		peer_tep->te_conp = tep->te_conp = NULL;
		/*
		 * unlink the stream
		 */
		(WR(tep->te_rq))->q_next = NULL;
		(WR(peer_tep->te_rq))->q_next = NULL;
	}
}



/*
 * qwriter call back function for linking the streams
 * called as qwriter because the address needs to be searched
 * in global list of open end-points.
 */
static void
tl_cl_mctl_link(wq, mp)
queue_t *wq;
mblk_t *mp;
{
	tl_addr_t addr;
	struct tl_sictl *sictlp;
	tl_endpt_t *tep;

	sictlp = (struct tl_sictl *) mp->b_rptr;

	addr.ta_alen = sictlp->ADDR_len;
	addr.ta_abuf = (char *) (mp->b_rptr + sictlp->ADDR_offset);

	if ((tep = tl_addr_in_use(TL_TICLTS, &addr)) != NULL) {
		if (wq->q_next == NULL)
			wq->q_next = tep->te_rq;
	}
	freemsg(mp);
}

/*
 * Note: The following routine does not recover from allocb()
 * failures
 */
static void
tl_discon_ind(tep, reason)
tl_endpt_t *tep;
long reason;
{
	mblk_t *d_mp;

	/*
	 * flush the queues.
	 */
	flushq(tep->te_rq, FLUSHDATA);
	(void) putnextctl1(tep->te_rq, M_FLUSH, FLUSHRW);

	/*
	 * send discon ind
	 */
	d_mp = tl_discon_ind_alloc(reason, (long) tep);
	if (! d_mp) {
		STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE|SL_ERROR,
			"tl_discon_ind:allocb failure");
		return;
	}
	STRLOG(TL_ID, tep->te_minor, 3, SL_TRACE,
		"tl_discon_ind: state %d->%d",
		tep->te_state,
		NEXTSTATE(TE_DISCON_IND1, tep->te_state));
	tep->te_state = NEXTSTATE(TE_DISCON_IND1, tep->te_state);
	putnext(tep->te_rq, d_mp);
}

/*
 * Note: The following routine does not recover from allocb()
 * failures
 */
static mblk_t
*tl_discon_ind_alloc(reason, seqnum)
long reason;
long seqnum;
{
	mblk_t *mp;
	struct T_discon_ind *tdi;

	if (mp = allocb(sizeof (struct T_discon_ind), BPRI_MED)) {
		DB_TYPE(mp) = M_PROTO;
		mp->b_wptr = mp->b_rptr + sizeof (struct T_discon_ind);
		tdi = (struct T_discon_ind *)mp->b_rptr;
		tdi->PRIM_type = T_DISCON_IND;
		tdi->DISCON_reason = reason;
		tdi->SEQ_number = seqnum;
	}
	return (mp);
}


/*
 * Note: The following routine does not recover from allocb()
 * failures
 */
static mblk_t
*tl_ordrel_ind_alloc(void)
{
	mblk_t *mp;
	struct T_ordrel_ind *toi;

	if (mp = allocb(sizeof (struct T_ordrel_ind), BPRI_MED)) {
		DB_TYPE(mp) = M_PROTO;
		mp->b_wptr = mp->b_rptr + sizeof (struct T_ordrel_ind);
		toi = (struct T_ordrel_ind *)mp->b_rptr;
		toi->PRIM_type = T_ORDREL_IND;
	}
	return (mp);
}


/*
 * Send M_ERROR
 * Note: assumes caller ensured enough space in mp or enough
 *	memory available. Does not attempt recovery from allocb()
 *	failures
 */

static void
tl_merror(wq, mp, error)
queue_t *wq;
mblk_t *mp;
int error;
{
	tl_endpt_t *tep;

	tep = (tl_endpt_t *) wq->q_ptr;

	/*
	 * flush all messages on queue. we are shutting
	 * the stream down on fatal error
	 */
	flushq(wq, FLUSHALL);
	if ((tep->te_mode == TL_TICOTS) ||
	    (tep->te_mode == TL_TICOTSORD)) {
		/* connection oriented - unconnect endpoints */
		tl_co_unconnect(tep);
	}
	if (mp->b_cont) {
		freemsg(mp->b_cont);
		mp->b_cont = NULL;
	}

	if ((MBLKSIZE(mp) < 1) || (DB_REF(mp) > 1)) {
		freemsg(mp);
		mp = allocb(1, BPRI_HI);
		if (!mp) {
			STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
				"tl_merror:M_PROTO: out of memory");
			return;
		}
	}
	if (mp) {
		DB_TYPE(mp) = M_ERROR;
		mp->b_rptr = DB_BASE(mp);
		*mp->b_rptr = (char)error;
		mp->b_wptr = mp->b_rptr + sizeof (char);
		qreply(wq, mp);
	}
}



static void
tl_fill_option(buf, optlen, optname, optvalp)
char *buf;
unsigned long optlen;
int optname;
char *optvalp;
{
	struct opthdr *opt;

	opt = (struct opthdr *) buf;
	opt->len = OPTLEN(optlen);
	opt->level = TL_PROT_LEVEL;
	opt->name = (long) optname;
	bcopy(optvalp, buf + sizeof (struct opthdr), (size_t) optlen);
}



static boolean_t
tl_chk_opt(level, name)
int level, name;
{
	switch (level) {
	case SOL_SOCKET:
		switch (name) {	/* :TBD: Trim list to AF_UNIX options */
		case SO_LINGER:
		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_DONTROUTE:
		case SO_USELOOPBACK:
		case SO_BROADCAST:
		case SO_REUSEADDR:
		case SO_OOBINLINE:
		case SO_SNDBUF:
		case SO_RCVBUF:
		case SO_SNDLOWAT:
		case SO_RCVLOWAT:
			/*
			 * XXX Fail the check until we really implement
			 * AF_UNIX socket options.
			 */
			return (B_FALSE); /* TRUE */
		case SO_TYPE:
			/*
			 * XXX Note: Hack alert ! We do not fail this though it
			 * is not really used or implemented. (It is an option
			 * that cannot be set). The sideeffect of this is that
			 * T_DEFAULT returns a non-zero buffer which is required
			 * for Sparc ABI test suites. Take this out when
			 * any of the above socket options are implemented
			 * and there is atleast one option that checks true
			 * and causes T_DEFAULT to return non-zero length
			 * values.
			 */
			return (B_TRUE); /* TRUE */
		default:
			return (B_FALSE); /* FALSE */
		}
		/* NOTREACHED */
	case TL_PROT_LEVEL:
		switch (name) {
		case TL_OPT_PEER_CRED:
			/*
			 * option not supposed to retrieved/set directly
			 * Only sent in T_CON_{IND,CON}, T_UNITDATA_IND
			 * when some internal flags set by other options
			 * Direct retrieval always designed to fail for this
			 * option
			 */
			return (B_FALSE); /* FALSE */
		default:
			return (B_FALSE); /* FALSE */
		}
		/* NOTREACHED */
	default:
		return (B_FALSE);	/* FALSE */
	}
	/* NOTREACHED */
}

/* ARGSUSED */
static int
tl_get_opt(wq, level, name, ptr)
queue_t *wq;
int level;
int name;
unsigned char *ptr;
{
	int len;

	len = 0;

	/*
	 * Assumes: option level and name sanity satisfied earlier
	 * by a call to tl_chk_opt()
	 */

	switch (level) {
	case SOL_SOCKET:
		/*
		 * : TBD fill AF_UNIX socket options
		 */
		break;
	case TL_PROT_LEVEL:
		switch (name) {
		case TL_OPT_PEER_CRED:
			/*
			 * option not supposed to retrieved directly
			 * Only sent in T_CON_{IND,CON}, T_UNITDATA_IND
			 * when some internal flags set by other options
			 * Direct retrieval always designed to fail(ignored)
			 * for this option.
			 */
			break;
		}
	}
	return (len);
}

/* ARGSUSED */
static int
tl_set_opt(wq, level, name, ptr, len)
queue_t *wq;
int	level, name;
unsigned char *ptr;
int	len;
{
	int error;

	error = 0;		/* NOERROR */

	/*
	 * Assumes: option level and name sanity satisfied earlier
	 * by a call to tl_chk_opt()
	 */

	switch (level) {
	case SOL_SOCKET:
		/*
		 * : TBD fill AF_UNIX socket options and then stop
		 * returning error
		 */
		error = EINVAL;
		break;
	case TL_PROT_LEVEL:
		switch (name) {
		case TL_OPT_PEER_CRED:
			/*
			 * option not supposed to be set directly
			 * Its value in intialized for each endpoint at
			 * driver open time.
			 * Direct setting always designed to fail for this
			 * option.
			 */
			error = EPROTO;
			break;
		}
	}
	return (error);
}


static void
tl_timer(wq)
queue_t *wq;
{
	tl_endpt_t *tep;

	tep = (tl_endpt_t *) wq->q_ptr;
	ASSERT(tep);

	tep->te_timoutid = 0;

	enableok(wq);
	/*
	 * Note: can call wsrv directly here and save context switch
	 * Consider change when qtimeout (not timeout) is active
	 */
	qenable(wq);
}

static void
tl_buffer(wq)
queue_t *wq;
{
	tl_endpt_t *tep;

	tep = (tl_endpt_t *) wq->q_ptr;
	ASSERT(tep);

	tep->te_bufcid = 0;

	enableok(wq);
	/*
	 *  Note: can call wsrv directly here and save context switch
	 * Consider change when qbufcall (not bufcall) is active
	 */
	qenable(wq);
}

static void
tl_memrecover(wq, mp, size)
queue_t *wq;
mblk_t *mp;
int size;
{
	tl_endpt_t *tep;

	tep = (tl_endpt_t *) wq->q_ptr;
	noenable(wq);

	freezestr(wq);
	insq(wq, wq->q_first, mp);
	unfreezestr(wq);

	if (tep->te_bufcid || tep->te_timoutid) {
		STRLOG(TL_ID, tep->te_minor, 1, SL_TRACE|SL_ERROR,
			"tl_memrecover:recover 0x%x pending", (int)wq);
		return;
	}

	if (! (tep->te_bufcid = qbufcall(wq, size, BPRI_MED,
					tl_buffer, (long)wq))) {
		tep->te_timoutid =
			qtimeout(wq, tl_timer, (caddr_t)wq, TL_BUFWAIT);
	}
}

#ifdef TL_DEBUG
static int
change_and_log_state(ev, st)
int ev, st;
{

	STRLOG(TL_ID, -1, 1, SL_TRACE,
		"Event= %d, State = %d, Nextstate= %d\n",
		ev, st, ti_statetbl[ev][st]);
	return (ti_statetbl[ev][st]);
}
#endif TL_DEBUG

static int
tl_isasocket(wq, tep)
	queue_t *wq;		/* down stream queue pointer */
	tl_endpt_t *tep;	/* transport endpoint structure pointer */
{
	queue_t *rq = RD(wq);

	if (rq && !(tep->te_flag & TL_SOCKCHECKED)) {
		/* get upstream read queue pointer */
		queue_t *uq = rq->q_next;
		if (uq) {
			struct qinit *qi;
			qi = uq->q_qinfo;
			if (qi) {
				struct module_info *mi = qi->qi_minfo;
				if (mi && mi->mi_idname &&
				    (strcmp(mi->mi_idname, "sockmod") == 0)) {
					/* its a socket */
					tep->te_flag |= TL_ISASOCKET;
					STRLOG(TL_ID, tep->te_minor, 3,
					    SL_TRACE|SL_ERROR,
					"tl_isasocket: tep 0x%x is a socket",
					    tep);
				} else {
					/* not a socket */
					tep->te_flag &= ~TL_ISASOCKET;
					STRLOG(TL_ID, tep->te_minor, 3,
					    SL_TRACE|SL_ERROR,
				"tl_isasocket: tep 0x%x is not a socket",
					    tep);
				}

			}
			/* no qi, then not a socket */
		}
		tep->te_flag |= TL_SOCKCHECKED;
	}
	return (tep->te_flag & TL_ISASOCKET);
}
