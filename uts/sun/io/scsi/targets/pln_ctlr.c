/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)pln_ctlr.c	1.16	95/02/10 SMI"

/*
 * Pluto controller target driver
 *
 * Compiled separately, but linked together with the pluto
 * SCSI host adapter driver, pln.
 *
 * Updated to run on Solaris 5.4
 *
 */


#include <note.h>
#include <sys/file.h>
#include <sys/scsi/scsi.h>
#include <sys/stat.h>
#include <sys/fc4/fc.h>
#include <sys/fc4/fcp.h>
#include <sys/fc4/fc_transport.h>
#include <sys/scsi/adapters/plndef.h>
#include <sys/scsi/targets/pln_ctlr.h>
#include <sys/scsi/adapters/plnvar.h>



/*
 * Function prototypes
 */
int	pln_ctlr_probe(dev_info_t *dip);
int	pln_ctlr_attach(dev_info_t *, struct pln *);
void	pln_ctlr_detach(dev_info_t *dip);
static int	pln_ctlr_close(dev_t dev, int flag,
		int otyp, cred_t *cred_p);
static int	pln_ctlr_strategy(struct buf *bp);
static int	pln_ctlr_ioctl(dev_t dev, int cmd, int arg,
		int dlag, cred_t *cred_p, int *rval_p);
static int	pln_ctlr_ioctl_cmd(dev_t dev, struct uscsi_cmd *in,
		enum uio_seg cdbspace, enum uio_seg dataspace);
static int	pln_ctlr_unit_ready(dev_t dev);
static void	pln_ctlr_start(struct pln_controller *pctlr);
static int	pln_ctlr_runout(caddr_t arg);
static void	pln_ctlr_done(struct pln_controller *pctlr,
		struct buf *bp);
static void	pln_ctlr_make_cmd(struct pln_controller	*pctlr,
		struct buf *bp, int (*func)());
static void	pln_ctlr_restart(caddr_t arg);
static void	pln_ctlr_callback(struct scsi_pkt *pkt);
static int	pln_ctlr_handle_sense(struct pln_controller *pctlr,
		struct buf *bp);
static void	pln_ctlr_log(struct pln_controller *pctlr, int level,
		const char *fmt, ...);
static int	pln_ctlr_open(dev_t *dev_p, int flag, int otyp, cred_t *cred_p);


/*
 * cb_ops for pln:ctlr leaf driver
 *
 *	although we actually have a strategy routine,
 *	it is for internal use by this pln:ctlr only.
 */
struct cb_ops pln_ctlr_cb_ops = {
	pln_ctlr_open,			/* open */
	pln_ctlr_close,			/* close */
	nodev,				/* strategy */
	nodev,				/* print */
	nodev,				/* dump */
	nodev,				/* read */
	nodev,				/* write */
	pln_ctlr_ioctl,			/* ioctl */
	nodev,				/* devmap */
	nodev,				/* mmap */
	nodev,				/* segmap */
	nochpoll,			/* poll */
	ddi_prop_op,			/* cb_prop_op */
	0,				/* streamtab  */
	D_MP | D_NEW			/* Driver compatibility flag */

};

/*
 * Local Static Data
 */
static int	pln_ctlr_retry_count		= PLN_CTLR_RETRY_COUNT;
static void	*pln_ctlr_state			= NULL;
/*
 * the pln_ctlr_attach_mutex only protects pln_ctlr_max_instance in
 * multi-threaded attach situations
 */
static kmutex_t pln_ctlr_attach_mutex;
static int	pln_ctlr_max_instance		= 0;

#ifdef	PLN_CTLR_DEBUG
static int	pln_ctlr_debug		= 1;
#else
#define		pln_ctlr_debug		0
#endif	/* PLN_CTLR_DEBUG */

/*
 * Warlock directives
 */
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", uscsi_cmd))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", uio))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", buf))
_NOTE(MUTEX_PROTECTS_DATA(scsi_device::sd_mutex, diskhd::av_forw))
_NOTE(MUTEX_PROTECTS_DATA(scsi_device::sd_mutex, diskhd::b_forw))
_NOTE(DATA_READABLE_WITHOUT_LOCK(pln_ctlr_max_instance))

/*
 * Called from pln _init
 * instead of from kernel
 */
int
pln_ctlr_init(void)
{
	int	status;
	status = ddi_soft_state_init(&pln_ctlr_state,
		sizeof (struct pln_controller), 1);

#ifdef	PLN_CTLR_DEBUG
	pln_ctlr_log(0, PLN_CTLR_CE_DEBUG1,
		"pln_ctlr_init: Version pln_ctlr.c\t1.16\t95/02/10\n");
	pln_ctlr_log(0, PLN_CTLR_CE_DEBUG1,
		"pln_ctlr_init: pln_ctlr_state=0x%x\n",
		pln_ctlr_state);
#endif	/* PLN_CTLR_DEBUG */

	mutex_init(&pln_ctlr_attach_mutex, "pln_ctlr_attach_mutex",
		MUTEX_DRIVER, NULL);
	return (status);
}

void
pln_ctlr_fini(void)
{
	ddi_soft_state_fini(&pln_ctlr_state);
	mutex_destroy(&pln_ctlr_attach_mutex);
}

int
pln_ctlr_probe(
	dev_info_t		*dip)
{
	int			instance;

	PLN_CTLR_LOG(NULL, PLN_CTLR_CE_DEBUG3,
		"pln_ctlr_probe:\n");

	/*
	 * Keep a max instance count for the runout handling
	 * routine to use to find the structure.
	 */
	instance = ddi_get_instance(dip);
	mutex_enter(&pln_ctlr_attach_mutex);
	if (instance > pln_ctlr_max_instance) {
		pln_ctlr_max_instance = instance;
	}
	mutex_exit(&pln_ctlr_attach_mutex);
	return (DDI_SUCCESS);
}




int
pln_ctlr_attach(dev_info_t *dip, struct pln *pln)
{
	struct pln_controller	*pctlr = NULL;
	struct scsi_device	*sd = NULL;
	char			name[MAXNAMELEN];
	char			buf[32];
#ifdef	ON1093
	pln_address_t		*pln_addr;
#endif	/* ON1093 */
	int			instance;


	instance = ddi_get_instance(dip);
	PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG2,
		"pln_ctlr_attach: sd=0x%x instance=0x%x\n",
		sd, instance);

	(void) strcpy(name, "0");

	/*
	 * Allocate and initialize the pln_ctlr structure
	 */
	if (ddi_soft_state_zalloc(pln_ctlr_state, instance) != DDI_SUCCESS) {
		cmn_err(CE_CONT,
			"pln_ctlr_attach: controller struct alloc failed\n");
		return (DDI_FAILURE);
	}

	pctlr = ddi_get_soft_state(pln_ctlr_state, instance);

	/*
	 * Allocate and initialize the scsi_device structure
	 */
	sd = (struct scsi_device *)kmem_zalloc(sizeof (*sd), KM_SLEEP);
	if (sd == (struct scsi_device *)0) {
		cmn_err(CE_CONT,
			"pln_ctlr_attach: scsi_device alloc failed\n");
		goto error;
	}


#ifdef	ON1093
	/*
	 * If not 1093 then we use the address
	 * already set up in pln
	 */

	/*
	 * Allocate and initialize the address structure
	 */
	pln_addr = (pln_address_t *)
		kmem_zalloc(sizeof (pln_address_t), KM_SLEEP);
	if (pln_addr == NULL) {
		cmn_err(CE_CONT,
			"pln_ctlr_attach: pln_address alloc failed\n");
		goto error;
	}
	/*
	 * Initialize addressing for pln:ctlr
	 */
	pln_addr->pln_entity = PLN_ENTITY_CONTROLLER;
	pln_addr->pln_port = 0;
	pln_addr->pln_target = 0;
	pln_addr->pln_reserved = 0;
#endif	/* ON1093 */

	/* */
	pctlr->pln_scsi_devicep = sd;
	sd->sd_private = (opaque_t)pctlr;

	sd->sd_dev = dip;
#ifdef	ON1093
	sd->sd_address.a_cookie = (int)&pln->pln_tran;
	sd->sd_address.a_addr_ptr = pln_addr;
	sd->sd_lkarg = pln->pln_iblock;
#else	ON1093
	sd->sd_address.a_hba_tran = pln->pln_tran;
#endif	/* ON1093 */

	/*
	 * Initialize the scsi_device mutex
	 */
	(void) sprintf(buf, "sd mutex %s", name);
#ifdef	ON1093
	mutex_init(&sd->sd_mutex, buf, MUTEX_DRIVER, sd->sd_lkarg);
#else	ON1093
	mutex_init(&sd->sd_mutex, buf, MUTEX_DRIVER, NULL);
#endif	/* ON1093 */

	/*
	 * Provide necessary  information to enable the system to
	 * create the /devices hierarchies.
	 */
	if (ddi_create_minor_node(dip, "ctlr", S_IFCHR,
		instance, NULL, NULL) == DDI_FAILURE) {
		PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG, "Create Minor Failed\n");
		goto error;
	}

	/*
	 * Print structure pointers for debug
	 */
	PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG1,
	    "&pln_controller: 0x%x &scsi_device: 0x%x \n",
	    pctlr, sd);
#ifdef	ON1093
	PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG1,
	    "&pln_address_t: 0x%x\n", pln_addr);
#endif	/* ON1093 */

	/*
	 * allocate one 'buf' special buffer
	 * and init the condition variable that is used to allocate it.
	 *
	 * There is only one 'buf' buffer for uscsi commands.
	 * That means these command are single threaded.
	 *
	 */
	pctlr->pln_sbufp = getrbuf(KM_NOSLEEP);
	if (pctlr->pln_sbufp == (struct buf *)NULL) {
		goto error;
	}
	cv_init(&pctlr->pln_sbuf_cv, "targ_cv", CV_DRIVER, NULL);

#ifndef __lock_lint
	NEW_STATE(PLN_CTLR_STATE_ATTACHED);
#endif  __lock_lint

	PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG1, "Attached pln_ctlr driver\n");

	return (DDI_SUCCESS);

error:
	PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG,
		"Attached of pln_ctlr driver failed\n");
	/*
	 * Use detach to clean up.
	 */
	pln_ctlr_detach(dip);

	return (DDI_FAILURE);
}


/*
 * NOTE: This needs to be cleaned up if it is to
 * be used generally.
 * - Needs a flag to decide if data structures were allocated
 *   and mutex protection.
 */
/*ARGSUSED*/
void
pln_ctlr_detach(dev_info_t *dip)
{
	int			instance;
	struct scsi_device	*sd;
	pln_controller_t	*pctlr;

	cmn_err(CE_CONT, "pln_ctlr_detach:\n");


	instance = ddi_get_instance(dip);

	pctlr = ddi_get_soft_state(pln_ctlr_state, instance);
	if (pctlr == NULL) {
		PLN_CTLR_LOG(0, CE_WARN, "No Target Struct for pln_ctlr%d\n",
		    instance);
		return;
	}
	sd = pctlr->pln_scsi_devicep;

	/*
	 * Remove other data structures allocated in pln_ctlr_attach()
	 */
	cv_destroy(&pctlr->pln_sbuf_cv);
	if (sd) {
		/*
		 * This was incorrect in 1093 code
		 */
#ifdef	ON1093
		if (sd->sd_address.a_addr_ptr)
			kmem_free((caddr_t)sd->sd_address.a_addr_ptr,
			sizeof (pln_address_t));
#endif	/* ON1093 */
		kmem_free((caddr_t)sd, sizeof (*sd));
	}
	if (pctlr->pln_sbufp)
		freerbuf(pctlr->pln_sbufp);
	ddi_soft_state_free(pln_ctlr_state, instance);
	ddi_remove_minor_node(dip, NULL);
}

/*
 * Unix Entry Points
 */

/* ARGSUSED */
static int
pln_ctlr_open(
	dev_t			*dev_p,
	int			flag,
	int			otyp,
	cred_t			*cred_p)
{
	dev_t			dev = *dev_p;
	struct pln_controller	*pctlr;

	pctlr = (struct pln_controller *)
		ddi_get_soft_state(pln_ctlr_state, getminor(dev));
	if (pctlr == NULL) {
		return (ENXIO);
	}

	/*
	 * Test to make sure unit still is powered on and is ready
	 * by doing Test Unit Ready.
	 */
	if ((flag & (FNDELAY|FNONBLOCK)) == 0) {
	    if (pln_ctlr_unit_ready(dev) == 0) {
		return (ENODEV);
	    }
	}

	mutex_enter(PLN_CTLR_MUTEX);
	NEW_STATE(PLN_CTLR_STATE_OPEN);
	mutex_exit(PLN_CTLR_MUTEX);

	return (0);

}

/*ARGSUSED*/
static int
pln_ctlr_close(
	dev_t			dev,
	int			flag,
	int			otyp,
	cred_t			*cred_p)
{
	struct pln_controller	*pctlr;

	pctlr = (struct pln_controller *)
		ddi_get_soft_state(pln_ctlr_state, getminor(dev));
	if (pctlr == NULL) {
		return (ENXIO);
	}

	mutex_enter(PLN_CTLR_MUTEX);

	/*
	 * Do any Clean up work here
	 */
	NEW_STATE(PLN_CTLR_STATE_CLOSED);

	mutex_exit(PLN_CTLR_MUTEX);
	return (0);
}


static int
pln_ctlr_strategy(
	struct buf		 *bp)
{
	struct diskhd		*dp;
	struct pln_controller	*pctlr;

	pctlr = (struct pln_controller *)
		ddi_get_soft_state(pln_ctlr_state, getminor(bp->b_edev));
	if (pctlr == NULL) {
		bp->b_resid = bp->b_bcount;
		bp->b_error = ENXIO;
		bp->b_flags |= B_ERROR;
		biodone(bp);
		return (0);
	}

	/*
	 * Check here if we are still alive
	 */

	mutex_enter(PLN_CTLR_MUTEX);
	/*
	 * Get queue for this device
	 */
	dp = &pctlr->pln_utab;
	bp->av_forw = NULL;
	if (dp->b_actf == NULL) {
	    dp->b_actf = dp->b_actl = bp;
	} else {
	    dp->b_actl = dp->b_actl->av_forw = bp;
	}
	bp->b_flags &= ~(B_DONE|B_ERROR);
	bp->b_resid = 0;
	bp->av_back = (struct buf *)0;


	if (dp->b_forw == NULL) {

		mutex_exit(PLN_CTLR_MUTEX);

		/*
		 * Device inactive - try to start command up
		 */
		pln_ctlr_start(pctlr);

		return (0);
	} else if (BP_PKT(dp->b_actf) == 0) {
		/*
		 * try and map this one
		 */
		pln_ctlr_make_cmd(pctlr, dp->b_actf, NULL_FUNC);
	}

	mutex_exit(PLN_CTLR_MUTEX);

	return (0);
}

/*
 * This routine implements the ioctl calls.
 */
/* ARGSUSED3 */
static int
pln_ctlr_ioctl(
	dev_t			dev,
	int			cmd,
	int			arg,
	int			flag,
	cred_t			*cred_p,
	int			*rval_p)
{
	int	i;
	struct uscsi_cmd	*scmd;
	long			data[512 / (sizeof (long))];

	bzero((caddr_t)data, sizeof (data));

	switch (cmd) {
	case USCSICMD:
		/*
		* Run a generic ucsi.h command.
		*/
		scmd = (struct uscsi_cmd *)data;
		if (copyin((caddr_t)arg, (caddr_t)scmd, sizeof (*scmd))) {
			return (EFAULT);
		}

		i = pln_ctlr_ioctl_cmd(dev, scmd, UIO_USERSPACE, UIO_USERSPACE);
		if (copyout((caddr_t)scmd, (caddr_t)arg, sizeof (*scmd))) {
			if (i == 0)
				i = EFAULT;
		}
		return (i);
	default:
		break;
	}
	return (ENOTTY);

}


/*
 * Run a USCSI command.
 *
 * space is for address space of cdb
 * addr_flag is for address space of the buffer
 */
static int
pln_ctlr_ioctl_cmd(
	dev_t			dev,
	struct uscsi_cmd	*in,
	enum uio_seg		cdbspace,
	enum uio_seg		dataspace)
{
	caddr_t			cdb;
	int			err;
	int			rw;
	struct buf		*bp;
	struct scsi_pkt		*pkt;
	struct uscsi_cmd	*scmd;
	struct pln_controller	*pctlr;
	int			rqlen;
	struct scsi_arq_status	*arq;


	pctlr = (struct pln_controller *)
		ddi_get_soft_state(pln_ctlr_state, getminor(dev));
	if (pctlr == NULL) {
		return (ENXIO);
	}
	ASSERT(mutex_owned(PLN_CTLR_MUTEX) == 0);

	/*
	 * In order to not worry about where the uscsi structure
	 * came from (or where the cdb it points to came from)
	 * we're going to make kmem_alloc'd copies of them
	 * here.
	 */
	scmd = in;
	cdb = kmem_zalloc((size_t)scmd->uscsi_cdblen, KM_SLEEP);
	if (cdbspace == UIO_SYSSPACE) {
		bcopy(scmd->uscsi_cdb, cdb, scmd->uscsi_cdblen);
	} else if (copyin(scmd->uscsi_cdb, cdb, (u_int)scmd->uscsi_cdblen)) {
		kmem_free(cdb, (size_t)scmd->uscsi_cdblen);
		return (EFAULT);
	}

	scmd = (struct uscsi_cmd *)kmem_alloc(sizeof (*scmd), KM_SLEEP);
	bcopy((caddr_t)in, (caddr_t)scmd, sizeof (*scmd));
	scmd->uscsi_cdb = cdb;
	rw = (scmd->uscsi_flags & USCSI_READ) ? B_READ : B_WRITE;

	/* default resid for request sense */
	scmd->uscsi_rqresid = scmd->uscsi_rqlen;

	/*
	 * Get the 'special' buffer...
	 */
	mutex_enter(PLN_CTLR_MUTEX);
	while (pctlr->pln_sbuf_busy)
		cv_wait(&pctlr->pln_sbuf_cv, PLN_CTLR_MUTEX);
	pctlr->pln_sbuf_busy = 1;

	/* since we are single threaded per command only one count needed */
	pctlr->pln_retry_count = 0;	/* reset the retry count */

	bp = pctlr->pln_sbufp;
	mutex_exit(PLN_CTLR_MUTEX);

	/*
	 * If we're going to do actual I/O, let physio do all the right things
	 */
	if (scmd->uscsi_buflen) {
		struct iovec aiov;
		struct uio auio;
		struct uio *uio = &auio;

		bzero((caddr_t)&auio, sizeof (struct uio));
		bzero((caddr_t)&aiov, sizeof (struct iovec));
		aiov.iov_base = scmd->uscsi_bufaddr;
		aiov.iov_len = scmd->uscsi_buflen;
		uio->uio_iov = &aiov;

		uio->uio_iovcnt = 1;
		uio->uio_resid = scmd->uscsi_buflen;
		uio->uio_segflg = dataspace;
		uio->uio_loffset = 0;
		uio->uio_fmode = 0;

		/*
		 * Let physio do the rest...
		 */
		bp->av_back = (struct buf *)0;
		bp->b_forw = (struct buf *)scmd;
		/*
		 * Note: we wait here until command is complete
		 */
		err = physio(pln_ctlr_strategy, bp, dev, rw, minphys, uio);
	} else {

		/*
		 * Since we do not move any data in this section
		 * call pln_ctlr_strategy directly
		 */
		bp->av_back = (struct buf *)0;
		bp->b_forw = (struct buf *)scmd;
		bp->b_flags = B_BUSY | rw;
		bp->b_edev = dev;
		bp->b_bcount = bp->b_blkno = 0;
		(void) pln_ctlr_strategy(bp);
		/*
		 * Note: we wait here until command is complete
		 */
		err = biowait(bp);
	}


done:
	/*
	 * get the status block, if any, and
	 * release any resources that we had.
	 */

	in->uscsi_status = 0;
	rqlen = scmd->uscsi_rqlen - scmd->uscsi_rqresid;
	rqlen = min(((int)in->uscsi_rqlen), rqlen);
	if ((pkt = BP_PKT(bp)) != NULL) {
		in->uscsi_status = SCBP_C(pkt);
		in->uscsi_resid = bp->b_resid;
		if (in->uscsi_status) {
		/*
		 * Update the Request Sense status and resid
		 */
		    in->uscsi_rqresid = in->uscsi_rqlen - rqlen;
		    in->uscsi_rqstatus = scmd->uscsi_rqstatus;
		/*
		 * Copy out the sense data for user processes
		 */
		    if (in->uscsi_rqbuf && rqlen && dataspace ==
			UIO_USERSPACE) {
			arq = (struct scsi_arq_status *)pkt->pkt_scbp;
			if (copyout((caddr_t)&arq->sts_sensedata,
			in->uscsi_rqbuf, rqlen)) {
				err = EFAULT;
			}
		    }
		    PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG1,
			"Copied out sense data to: 0x%x from: 0x%x\n",
			in->uscsi_rqbuf, &arq->sts_sensedata);
		}
		scsi_destroy_pkt(pkt);
		bp->av_back = (struct buf *)0;
	}


	/*
	 * Tell anybody who cares that the buffer is now free
	 */
	mutex_enter(PLN_CTLR_MUTEX);
	pctlr->pln_sbuf_busy = 0;
	cv_signal(&pctlr->pln_sbuf_cv);
	mutex_exit(PLN_CTLR_MUTEX);

	kmem_free(scmd->uscsi_cdb, (size_t)scmd->uscsi_cdblen);
	kmem_free((caddr_t)scmd, sizeof (*scmd));
	return (err);
}

/*
 * Check to see if the unit will respond to a TUR
 *
 * Returns true or false
 */
static int
pln_ctlr_unit_ready(
	dev_t			dev)
{
	struct uscsi_cmd	scmd;
	struct uscsi_cmd	*com = &scmd;
	char			cmdblk[CDB_GROUP0];

	bzero((caddr_t)&scmd, sizeof (struct uscsi_cmd));
	bzero(cmdblk, CDB_GROUP0);
	cmdblk[0] = (char)SCMD_TEST_UNIT_READY;

	scmd.uscsi_cdblen = CDB_GROUP0;
	scmd.uscsi_cdb = cmdblk;
	scmd.uscsi_timeout = 60;	/* timeout value */
	/* Don't get status */
	scmd.uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT|USCSI_WRITE;

	if (pln_ctlr_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE)) {
		return (0);
	}
	return (1);
}

/*
 * Unit start and Completion
 */
static void
pln_ctlr_start(
	struct pln_controller	*pctlr)
{
	struct buf		*bp;
	struct diskhd		*dp;

	mutex_enter(PLN_CTLR_MUTEX);
	dp = &pctlr->pln_utab;

	if (dp->b_forw || (bp = dp->b_actf) == NULL) {
		mutex_exit(PLN_CTLR_MUTEX);
		return;
	}

	if (!BP_PKT(bp)) {
		pln_ctlr_make_cmd(pctlr, bp, pln_ctlr_runout);
		if (!BP_PKT(bp)) {
			NEW_STATE(PLN_CTLR_STATE_RWAIT);
			mutex_exit(PLN_CTLR_MUTEX);
			return;
		} else {
			NEW_STATE(PLN_CTLR_STATE_OPEN);
		}
	}

	dp->b_forw = bp;
	dp->b_actf = bp->b_actf;
	bp->b_actf = 0;
	/*
	 * Clear Out the resid now.
	 */
	bp->b_resid = 0;

	mutex_exit(PLN_CTLR_MUTEX);
	if (scsi_transport(BP_PKT(bp)) != TRAN_ACCEPT) {
		bp->b_flags |= B_ERROR;
		pln_ctlr_done(pctlr, bp);
	} else {
		mutex_enter(PLN_CTLR_MUTEX);
		if (dp->b_actf && !BP_PKT(dp->b_actf)) {
			pln_ctlr_make_cmd(pctlr, dp->b_actf, NULL_FUNC);
		}
		mutex_exit(PLN_CTLR_MUTEX);
	}
}

/*
 * pln_ctlr_runout
 *	the callback function for resource allocation
 *
 *	This routine is called by the SCSI packet allocation
 *	routine when resources are not available.
 *
 *	This routine attempts to free resources by starting
 *	a command.
 *
 *	Note: It could be re-written to not use the pln_ctlr_max_instance
 *	but we would have to pass a structure pointer to the SCSI
 *	resource allocation routine.
 *
 */
/*ARGSUSED*/
static int
pln_ctlr_runout(
	caddr_t			arg)
{
	int			i;
	int			rval = 1;
	struct pln_controller	*pctlr;

	/*
	 * We have to look at all the devices, because we do not know
	 * who might be waiting for resource.
	 */
	for (i = 0; i < pln_ctlr_max_instance; i++) {
		if ((pctlr = ddi_get_soft_state(pln_ctlr_state, i)) == NULL)
			continue;
		PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG1,
			"pln_ctlr_runout: instance= 0x%x\n", i);

		mutex_enter(PLN_CTLR_MUTEX);
		if (pctlr->pln_state == PLN_CTLR_STATE_RWAIT) {
			mutex_exit(PLN_CTLR_MUTEX);
			pln_ctlr_start(pctlr);
			mutex_enter(PLN_CTLR_MUTEX);
			if (pctlr->pln_state == PLN_CTLR_STATE_RWAIT) {
				rval = 0;
				mutex_exit(PLN_CTLR_MUTEX);
				break;
			}
		}
		mutex_exit(PLN_CTLR_MUTEX);
	}
	return (rval);
}

/*
 * Done with a command.
 *
 */
static void
pln_ctlr_done(
	struct pln_controller	*pctlr,
	struct buf		*bp)
{
	struct diskhd		*dp;

	mutex_enter(PLN_CTLR_MUTEX);
	dp = &pctlr->pln_utab;

	if (bp == dp->b_forw) {
		dp->b_forw = NULL;
	}
	mutex_exit(PLN_CTLR_MUTEX);

	/*
	 * Start the next one before releasing resources on this one
	 */
	pln_ctlr_start(pctlr);

	/*
	 * tell waiting thread command is done
	 */
	biodone(bp);
}

static void
pln_ctlr_make_cmd(
	struct pln_controller	*pctlr,
	struct buf		*bp,
	int			(*func)())
{
	struct scsi_pkt		*pkt;
	struct uscsi_cmd	*scmd = (struct uscsi_cmd *)bp->b_forw;
	int			flags;

	ASSERT(mutex_owned(PLN_CTLR_MUTEX));

	flags = 0;

	/*
	 * allocate scsi packet
	 *
	 * NOTE: Always allocate a sense packet
	 */
	pkt = scsi_resalloc(ROUTE, scmd->uscsi_cdblen,
	    sizeof (struct scsi_arq_status),
	    (bp->b_bcount)? (opaque_t)bp: (opaque_t)0, func);
	makecom_g0(pkt, PLN_CTLR_SCSI_DEVP, flags,
		scmd->uscsi_cdb[0], 0, 0);
	bcopy(scmd->uscsi_cdb,
	    (caddr_t)pkt->pkt_cdbp, scmd->uscsi_cdblen);

	pkt->pkt_comp = pln_ctlr_callback;
	pkt->pkt_time = scmd->uscsi_timeout;
	pkt->pkt_private = (opaque_t)bp;
	bp->av_back = (struct buf *)pkt;
}

/*
 * Interrupt Service Routines
 */

static void
pln_ctlr_restart(
	caddr_t			arg)
{
	struct pln_controller	*pctlr = (struct pln_controller *)arg;
	struct buf		*bp;

	mutex_enter(PLN_CTLR_MUTEX);
	bp = pctlr->pln_utab.b_forw;
	mutex_exit(PLN_CTLR_MUTEX);

	if (bp) {
		struct scsi_pkt *pkt = BP_PKT(bp);

		if (scsi_transport(pkt) != TRAN_ACCEPT) {
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_ERROR;
			pln_ctlr_done(pctlr, bp);
		}
	}
}

/*
 * Command completion processing
 */
static void
pln_ctlr_callback(
	struct scsi_pkt		*pkt)
{
	struct pln_controller	*pctlr;
	struct buf		*bp;
	struct diskhd		*dp;
	int			action, com;


	bp = (struct buf *)pkt->pkt_private;
	pctlr = ddi_get_soft_state(pln_ctlr_state, getminor(bp->b_edev));

	mutex_enter(PLN_CTLR_MUTEX);
	PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG3,
		"pln_ctlr_callback: pkt->pkt_reason=0x%x\n",
		pkt->pkt_reason);

	/*
	 * check first to see if all OK
	 *
	 * (Optimize for performance)
	 */
	if ((pkt->pkt_reason == CMD_CMPLT) && (SCBP_C(pkt) == 0) &&
		(pkt->pkt_resid == 0)) {
		/*
		 * Everythink O.K.
		 */
		action = COMMAND_DONE;

	} else if ((pkt->pkt_reason == CMD_CMPLT) && (SCBP_C(pkt) == 0)) {
		/*
		 * NON-ZERO residual
		 *
		 * But no error reported
		 */
		PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG,
		    "pln_ctlr_callback: non-zero residual: 0x%x\n",
		    pkt->pkt_resid);

		action = COMMAND_DONE_ERROR;

		/*
		 * Get the low order bits of the command byte
		 */
		com = GETCMD((union scsi_cdb *)pkt->pkt_cdbp);

		/*
		 * If the command is a read or a write, and we have
		 * a non-zero pkt_resid, that is an error. We should
		 * attempt to retry the operation if possible.
		 */
		if (com == SCMD_READ || com == SCMD_WRITE) {
		    if (pctlr->pln_retry_count++ < pln_ctlr_retry_count) {
			    action = QUE_COMMAND;
		    }
		}

		/*
		 * pkt_resid will reflect, at this point, a residual
		 * of how many bytes left to be transferred there were
		 * from the actual scsi command.
		 *
		 * We only care about reporting this type of error
		 * in any but read or write commands. Since we have
		 * snagged any non-zero pkt_resids with read or writes
		 * above, all we have to do here is add pkt_resid to
		 * b_resid.
		 */
		if (action != QUE_COMMAND) {
		    bp->b_resid += pkt->pkt_resid;
		    pctlr->pln_retry_count = 0;
		}
	} else if ((pkt->pkt_reason == CMD_CMPLT) && (SCBP_C(pkt) != 0)) {
	/*
	 * Cmd is complete
	 * Status is non-zero
	 */
	    dp = &pctlr->pln_utab;

	    if (SCBP(pkt)->sts_busy) {
		/*
		 * SSA Controller is reserved or busy
		 */
		action = COMMAND_DONE_ERROR;
		if (SCBP(pkt)->sts_is) {
			/*
			 * Controller is reserved
			 * Fail immediately - See Bug Id: 1147670
			 */
			PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG,
				"pln_ctlr_callback: Reservation Conflict\n");
		} else if (pctlr->pln_retry_count++ < pln_ctlr_retry_count) {
			PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG,
				"pln_ctlr_callback: Controller busy\n");
			if (dp->b_forw == NULL)
				dp->b_forw = bp;
			timeout(pln_ctlr_restart, (caddr_t)pctlr,
				PLN_CTLR_BSY_TIMEOUT);
			action = JUST_RETURN;
		}
	    } else {
		action = pln_ctlr_handle_sense(pctlr, bp);
	    }
	} else {
		/*
		 * Command not complete
		 */
		if (pctlr->pln_retry_count++ < pln_ctlr_retry_count) {
			action = QUE_COMMAND;
		} else action = COMMAND_DONE_ERROR;

	}
	mutex_exit(PLN_CTLR_MUTEX);

	switch (action) {
	    case COMMAND_DONE_ERROR:
		    bp->b_resid = bp->b_bcount;
		    bp->b_flags |= B_ERROR;
		    /*FALLTHROUGH*/
	    case COMMAND_DONE:
		    pln_ctlr_done(pctlr, bp);
		    break;
	    case QUE_COMMAND:
		    if (scsi_transport(BP_PKT(bp)) != TRAN_ACCEPT) {
			    bp->b_resid = bp->b_bcount;
			    bp->b_flags |= B_ERROR;
			    pln_ctlr_done(pctlr, bp);
			    return;
		    }
		    break;
	    case JUST_RETURN:
		    break;
	}
}


static int
pln_ctlr_handle_sense(
	struct pln_controller	*pctlr,
	struct buf		*bp)
{
	struct diskhd		*dp = &pctlr->pln_utab;
	struct scsi_pkt		*pkt = BP_PKT(bp);
	struct scsi_arq_status	*sp;
	int			rval = COMMAND_DONE_ERROR;
	int			amt;

	ASSERT(mutex_owned(PLN_CTLR_MUTEX));
	PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG1,
		"pln_ctlr_handle_sense: pkt->pkt_scbp=0x%x\n",
		SCBP(pkt));

	if ((sp = (struct scsi_arq_status *)pkt->pkt_scbp) == 0)
		return (rval);

	amt = SENSE_LENGTH - sp->sts_rqpkt_resid;

	/*
	 * Now, check to see whether we got enough sense data to make any
	 * sense out if it (heh-heh).
	 */
	if (amt < SUN_MIN_SENSE_LENGTH) {
		return (rval);
	}

	PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG1,
		"pln_ctlr_handle_sense: sensedata=0x%x\n",
		sp->sts_sensedata);
	switch (sp->sts_sensedata.es_key) {
	case KEY_NOT_READY:
		PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG,
			"\t: KEY_NOT_READY\n");
		/*
		 * If we get a not-ready indication, wait a bit and
		 * try it again.
		 */
		if (pkt && pctlr->pln_retry_count++ < pln_ctlr_retry_count) {
			if (dp->b_forw == NULL)
				dp->b_forw = bp;
			timeout(pln_ctlr_restart, (caddr_t)pctlr,
				PLN_CTLR_BSY_TIMEOUT);
			rval = JUST_RETURN;
		} else {
			rval = COMMAND_DONE_ERROR;
		}
		break;

	case KEY_ABORTED_COMMAND:
		PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG,
			"\t: KEY_ABORTED_COMMAND\n");
		rval = QUE_COMMAND;
		break;
	case KEY_UNIT_ATTENTION:
		PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG,
			"\t: KEY_UNIT_ATTENTION\n");
		rval = QUE_COMMAND;
		break;
	case KEY_RECOVERABLE_ERROR:
		rval = COMMAND_DONE;
		break;
	case KEY_NO_SENSE:
	case KEY_HARDWARE_ERROR:
	case KEY_MEDIUM_ERROR:
	case KEY_MISCOMPARE:
	case KEY_VOLUME_OVERFLOW:
	case KEY_WRITE_PROTECT:
	case KEY_BLANK_CHECK:
	case KEY_ILLEGAL_REQUEST:
	default:
		rval = COMMAND_DONE_ERROR;
		PLN_CTLR_LOG(pctlr, PLN_CTLR_CE_DEBUG,
			"\t: KEY_OTHER\n");
		break;
	}

	return (rval);
}



/*ARGSUSED*/
static void
pln_ctlr_log(
	struct pln_controller	*pctlr,
	int			level,
	const char		*fmt,
				...)
{
#ifdef	PLN_CTLR_DEBUG
	char			name[16];
	char			buf[256];
	va_list			ap;

	if (pctlr) {
		sprintf(name, "pln_ctlr%d",
			ddi_get_instance(PLN_CTLR_SCSI_DEVP->sd_dev));
	} else {
		sprintf(name, "pln_ctlr");
	}

	va_start(ap, fmt);
	(void) vsprintf(buf, fmt, ap);
	va_end(ap);

	switch (level) {
	case CE_CONT:
	case CE_NOTE:
	case CE_WARN:
	case CE_PANIC:
		cmn_err(level, "%s:\t%s", name, buf);
		break;
	case PLN_CTLR_CE_DEBUG1:
		if (pln_ctlr_debug > 1)
			cmn_err(CE_CONT, "^%s:\t%s", name, buf);
		break;
	case PLN_CTLR_CE_DEBUG2:
		if (pln_ctlr_debug > 2)
			cmn_err(CE_CONT, "^%s:\t%s", name, buf);
		break;
	case PLN_CTLR_CE_DEBUG3:
		if (pln_ctlr_debug > 3)
			cmn_err(CE_CONT, "^%s:\t%s", name, buf);
		break;
	case PLN_CTLR_CE_DEBUG:
	default:
		cmn_err(CE_CONT, "^%s:\t%s", name, buf);
		break;
	}
#endif	PLN_CTLR_DEBUG
}
