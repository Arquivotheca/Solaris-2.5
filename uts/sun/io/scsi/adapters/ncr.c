/*
 * Copyright (c) 1989-1992, by Sun Microsystems, Inc.
 */


#pragma ident  "@(#)ncr.c 1.47     94/04/22 SMI"

/*
 *  OS host-adapter driver for NCR-5380 SCSI controller (new SCSA based)
 */
/*
 * From redhots ncr.c 1.17 90/08/28
 * Fixes added from redhots 1.18 on 2/5/91
 * Fixes added from redhots 1.19 on 2/21/91
 */

#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/ncrreg.h>
#include <sys/vtrace.h>
#include <sys/scsi/impl/pkt_wrapper.h>

#define	NCR_HIGH_IDECL	register int ncr_crit_s
#define	BLK_HIGH_INTR	ncr_crit_s = ddi_enter_critical()
#define	UNBLK_HIGH_INTR	ddi_exit_critical(ncr_crit_s)

/*
 * Debugging macros
 */

#define	PRINTF1	if (ncr_debug) printf
#define	PRINTF2	if (ncr_debug > 1) printf
#define	PRINTF3	if (ncr_debug > 2) printf
#define	EPRINTF	if (ncr_debug || (scsi_options & SCSI_DEBUG_HA)) printf
#define	INFORMATIVE 	(ncr_debug)


/*
 * Short hand defines
 */
#define	UNDEFINED		-1
#define	CURRENT_CMD(ncr)	((ncr)->n_slots[(ncr)->n_cur_slot])
#define	SLOT(sp)		((short)(Tgt((sp))<<3|(Lun((sp)))))
#define	NEXTSLOT(slot)		((slot)+1) & ((NTARGETS*NLUNS_PER_TARGET)-1)

#define	Tgt(sp)			((sp)->cmd_pkt.pkt_address.a_target)
#define	Lun(sp)			((sp)->cmd_pkt.pkt_address.a_lun)

#define	CNAME	ddi_get_name(ncr->n_dip)
#define	CNUM	ddi_get_instance(ncr->n_dip)
#define	NON_INTR(sp)	(((sp)->cmd_pkt.pkt_flags & FLAG_NOINTR) != 0)

/*
 * We use some extra bits in the flags
 */

#define	CFLAG_NODMA	0x10	/* run command w/o dma engine */

/*
 * We don't use these portions of the scsi_cmd struct, so we utilize the
 * storage for our own needs. Since a call to ddi_dma_htoc or ddi_dma_movwin
 * wipes a cookie, these values must be (re)initialized after such a call.
 */
#define	Handle		cmd_dmahandle
#define	Cookie		cmd_dmacookie
#define	cmd_dmawinsize	Cookie.dmac_notused
#define	cmd_curaddr	cmd_dsegs.sd_off

/*
 * Function prototypes
 */


/*
 * Autoconfiguration, nexus service functions
 */

static int ncr_identify(dev_info_t *);
static int ncr_attach(dev_info_t *, ddi_attach_cmd_t);
static int ncr_probe(dev_info_t *);

/*
 * SCSA interface routines
 */

static int ncr_scsi_tgt_init(dev_info_t *, dev_info_t *,
			scsi_hba_tran_t *, struct scsi_device *);
static int ncr_scsi_tgt_probe(struct scsi_device *, int (*)());
static void ncr_scsi_tgt_free(dev_info_t *, dev_info_t *,
			scsi_hba_tran_t *, struct scsi_device *);

static int ncr_start(struct scsi_address *ap, struct scsi_pkt *pkt);
static int ncr_abort(struct scsi_address *, struct scsi_pkt *);
static int ncr_reset(struct scsi_address *, int);
static int ncr_commoncap(struct scsi_address *, char *, int, int, int);
static int ncr_getcap(struct scsi_address *, char *, int);
static int ncr_setcap(struct scsi_address *, char *, int, int);
static struct scsi_pkt *ncr_scsi_init_pkt(struct scsi_address *ap,
	struct scsi_pkt *pkt, struct buf *bp, int cmdlen, int statuslen,
	int tgtlen, int flags, int (*callback)(), caddr_t arg);
static void ncr_scsi_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *pkt);

static struct scsi_pkt *ncr_scsi_pktalloc(struct scsi_address *ap,
	int cmdlen, int statuslen, int tgtlen, int (*callback)(),
	caddr_t callback_arg);
static void ncr_scsi_pktfree(struct scsi_address *ap,
	struct scsi_pkt *pkt);
static struct scsi_pkt *ncr_scsi_dmaget(struct scsi_pkt *pkt,
	opaque_t dmatoken, int (*callback)(), caddr_t callback_arg);
static void ncr_scsi_dmafree(struct scsi_address *ap,
	struct scsi_pkt *pkt);
static void ncr_scsi_sync_pkt(struct scsi_address *ap,
	struct scsi_pkt *pkt);

/*
 * Internal service routines
 */

static int ncr_ustart(struct ncr *, short);
static void ncr_finish(struct ncr *);
static int ncr_dopoll(struct ncr *);
static u_int ncr_intr(caddr_t);
static void ncr_svc(struct ncr *);
static int ncr_reselect(struct ncr *);
static void ncr_phasemanage(struct ncr *);
static int ncr_getphase(struct ncr *);
static int ncr_sendcmd(struct ncr *);
static int ncr_sendmsg(struct ncr *);
static int ncr_recvmsg(struct ncr *);
static int ncr_senddata(struct ncr *);
static int ncr_recvdata(struct ncr *);
static int ncr_recvstatus(struct ncr *);
static int ncr_select(struct ncr *);
static void ncr_preempt(struct ncr *);
static void ncr_disconnect(struct ncr *);
static int ncr_xfrin(struct ncr *, struct scsi_cmd *, int, u_char *, int);
static int ncr_xfrin_noack(struct ncr *, struct scsi_cmd *, int, u_char *);
static int ncr_xfrout(struct ncr *, int, u_char *, int);
static int ncr_cobra_dma_chkerr(struct ncr *, u_int);
static int ncr_cobra_dma_recv(struct ncr *);
static int ncr_cobra_dma_cleanup(struct ncr *);
static int ncr_vme_dma_recv(struct ncr *);
static int ncr_vme_dma_cleanup(struct ncr *);
static int ncr_flushbyte(struct ncr *, u_int, u_char);
static int ncr_fetchbyte(struct ncr *, u_char *);
static void ncr_dma_enable(struct ncr *, int);
static void ncr_dma_setup(struct ncr *);
static int ncr_dma_cleanup(struct ncr *);
static int ncr_dma_wait(struct ncr *);
static int ncr_NACKmsg(struct ncr *);
static int ncr_ACKmsg(struct ncr *);
static int ncr_sbcwait(u_char *, int, int, int);
static int ncr_csrwait(struct ncr *, int, int, int);
static void ncr_watch(caddr_t);
static void ncr_curcmd_timeout(struct ncr *);
static void ncr_internal_abort(struct ncr *);
static void ncr_do_abort(struct ncr *, int, int);
static void ncr_internal_reset(struct ncr *, int, int);
static void ncr_hw_reset(struct ncr *, int);
static void ncr_dump_datasegs(struct scsi_cmd *);
static void ncr_printstate(struct ncr *);
static int ncr_set_window(struct ncr *, struct scsi_cmd *, const char *);

/*
 * Local static data
 */

static struct ncr *ncr_softc = (struct ncr *)0;
static struct ncr *ncr_tail;
static int ncr_debug = 0;
static char *cbsr_bits = CBSR_BITS;
static long ncr_watchdog_tick;
static kmutex_t scsi_rmutex;
static int scsi_cb_id = 0;

/*
 * Autoconfiguration Section
 */

static struct dev_ops ncr_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	ncr_identify,		/* identify */
	ncr_probe,		/* probe */
	ncr_attach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	NULL,			/* bus operations */
	0			/* power */
};

static ddi_dma_lim_t ncrlim_cobra = {
	(u_long)0, (u_long)0x00ffffff, (u_int)((1<<24)-1), 4, 4, 1024
};

static ddi_dma_lim_t ncrlim_vme = {
	(u_long)0, (u_long)0xffffffff, (u_int)((1<<24)-1), 6, 2, 1024
};



/*ARGSUSED*/
static int
ncr_scsi_tgt_init(
	dev_info_t		*hba_dip,
	dev_info_t		*tgt_dip,
	scsi_hba_tran_t		*hba_tran,
	struct scsi_device	*sd)
{
#ifdef DEBUG
	if (sd->sd_address.a_target >= NTARGETS) {
		if (ncr_debug) {
			cmn_err(CE_CONT, "%s%d: %s target %d not supported\n",
			ddi_get_name(hba_dip), ddi_get_instance(hba_dip),
			ddi_get_name(tgt_dip), sd->sd_address.a_target);
		}
	}
#endif /* DEBUG */

	return ((sd->sd_address.a_target < NTARGETS) ?
		DDI_SUCCESS : DDI_FAILURE);
}


/*ARGSUSED*/
static int
ncr_scsi_tgt_probe(
	struct scsi_device	*sd,
	int			(*callback)())
{
	int	rval;

	rval = scsi_hba_probe(sd, callback);

#ifdef DEBUG
	if (ncr_debug) {
		char		*s;
		struct ncr	*ncr = SDEV2NCR(sd);

		switch (rval) {
		case SCSIPROBE_NOMEM:
			s = "scsi_probe_nomem";
			break;
		case SCSIPROBE_EXISTS:
			s = "scsi_probe_exists";
			break;
		case SCSIPROBE_NONCCS:
			s = "scsi_probe_nonccs";
			break;
		case SCSIPROBE_FAILURE:
			s = "scsi_probe_failure";
			break;
		case SCSIPROBE_BUSY:
			s = "scsi_probe_busy";
			break;
		case SCSIPROBE_NORESP:
			s = "scsi_probe_noresp";
			break;
		default:
			s = "???";
			break;
		}
		cmn_err(CE_CONT, "%s%d: %s target %d lun %d %s\n",
			ddi_get_name(ncr->n_dip),
			ddi_get_instance(ncr->n_dip),
			ddi_get_name(sd->sd_dev),
			sd->sd_address.a_target,
			sd->sd_address.a_lun, s);
	}
#endif	/* DEBUG */

	return (rval);
}


/*ARGSUSED*/
static void
ncr_scsi_tgt_free(
	dev_info_t		*hba_dip,
	dev_info_t		*tgt_dip,
	scsi_hba_tran_t		*hba_tran,
	struct scsi_device	*sd)
{
#ifdef	DEBUG
	if (ncr_debug) {
		cmn_err(CE_CONT, "%s%d: freeing target %s%d <%d,%d>\n",
			ddi_get_name(hba_dip), ddi_get_instance(hba_dip),
			ddi_get_name(tgt_dip), ddi_get_instance(tgt_dip),
			sd->sd_address.a_target, sd->sd_address.a_lun);
	}
#endif	/* DEBUG */
}


char _depends_on[] = "misc/scsi";

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>
extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module. This one is a driver */
	"NCR SCSI Host Adapter Driver",	/* Name of the module. */
	&ncr_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * This is the driver initialization routine.
 */
_init(void)
{
	int	i;

	if ((i = scsi_hba_init(&modlinkage)) != 0) {
		return (i);
	}
	mutex_init(&scsi_rmutex, "ncr resource mutex",
	    MUTEX_DRIVER, NULL);
	if ((i = mod_install(&modlinkage)) != 0) {
		scsi_hba_fini(&modlinkage);
		mutex_destroy(&scsi_rmutex);
	}
	return (i);
}

_fini(void)
{
	int	i;

	if ((i = mod_remove(&modlinkage)) == 0) {
		scsi_hba_fini(&modlinkage);
		mutex_destroy(&scsi_rmutex);
	}
	return (i);
}

_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
ncr_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "ncr") == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

static int
ncr_probe(dev_info_t *dev)
{
	auto caddr_t reg;
	u_long ctladdr, dmaaddr;

	PRINTF1("ncr_probe\n");
	if (ddi_map_regs(dev, (u_int)0, &reg, (off_t)0, NCRHWSIZE)) {
		printf("ncr: unable to map registers\n");
		return (DDI_PROBE_FAILURE);
	}

	/*
	 * Check for ncr 5380 Scsi Bus Ctlr chip with "ddi_peekc()"; struct
	 * ncr-5380 is common to all onboard scsi and vme scsi board. if not
	 * exist, return 0.
	 */

	/*
	 * We can use the first byte address of reg, 'coz we'll guarantee
	 * (by the time we are through) that that will always be the first
	 * byte of the NCR 5380 SBC (which should be the cbsr register).
	 */

	if (ddi_peekc(dev, reg, (char *)0) != DDI_SUCCESS) {
		goto failed;
	}
	dmaaddr = ((u_long) reg) + sizeof (struct ncrsbc);

	/*
	 * probe for different host adaptor interfaces
	 */

	if (cputype == CPU_SUN4_110) {
		/*
		 * probe for 4/110 dma interface
		 */
		PRINTF1("ncr_probe: probing 4-110\n");

		ctladdr = ((u_long) reg) + COBRA_CSR_OFF;
		if (ddi_peekl(dev, (long *)dmaaddr, (long *)0)
		    != DDI_SUCCESS) {
			goto failed;
		}
		PRINTF1("ncr_probe: 4/110 host exists\n");
	} else {
		/*
		 * This is either a SCSI-3 or a 3/E VME board
		 * This implementation only is interested in the SCSI-3.
		 */
		if (ddi_peeks(dev, (short *)(dmaaddr + 2), (short *)0)
		    != DDI_SUCCESS) {
			goto failed;
		}

		/*
		 * Make sure that it isn't a SCSI-2 board (which occupies 4k
		 * of VME space instead of the 2k that the SCSI-3 occupies).
		 * (the above is a quote from si.c. The code below doesn't
		 *  seem to bear this out exactly).
		 */
		PRINTF1("ncr_probe: checking a SCSI-3\n");
		if (ddi_peeks(dev, (short *)(((u_long) reg) + 0x800),
		    (short *)0) == DDI_SUCCESS) {
			goto failed;
		}

		/*
		 * Make sure that we're cool (really a scsi-3 board).
		 */
		ctladdr = ((u_long) reg) + CSR_OFF;
		if (ddi_peeks(dev, (short *)(ctladdr + 2), (short *)0)
		    != DDI_SUCCESS) {
			goto failed;
		}

		if ((((struct ncrctl *)ctladdr)->csr.lsw & NCR_CSR_ID) == 0) {
			printf("ncr%d: unmodified scsi-3 board- you lose..\n",
			    ddi_get_instance(dev));
			goto failed;
		}
		PRINTF1("ncr_probe: SCSI-3 host exists\n");
	}

	ddi_unmap_regs(dev, 0, &reg, 0, (off_t)NCRHWSIZE);
	return (DDI_PROBE_SUCCESS);

failed:
	ddi_unmap_regs(dev, 0, &reg, 0, (off_t)NCRHWSIZE);
	return (DDI_PROBE_FAILURE);
}

/*
 *	Standard Resource Allocation/Deallocation Routines
 */

/*
 * When Host Adapters don't want to supply their own resource allocation
 * routines, and they can live with certain assumptions about DVMA,
 * these routines are stuffed into their scsi_transport structures
 * which they then export to the library.
 */

/*
 * Local resource management data && defines
 */

static caddr_t scmds = (caddr_t)0;
static caddr_t secmds = (caddr_t)0; /* extended cmd pkts, see below */



static struct scsi_pkt *
ncr_scsi_init_pkt(struct scsi_address *ap, struct scsi_pkt *pkt,
	struct buf *bp, int cmdlen, int statuslen, int tgtlen,
	int flags, int (*callback)(), caddr_t arg)
{
	struct scsi_pkt		*new_pkt = NULL;

	/*
	 * Allocate a pkt
	 */
	if (!pkt) {
		pkt = ncr_scsi_pktalloc(ap, cmdlen, statuslen,
			tgtlen, callback, arg);
		if (pkt == NULL)
			return (NULL);
		if (flags & PKT_CONSISTENT) {
			((struct scsi_cmd *)pkt)->cmd_flags |= CFLAG_CMDIOPB;
		}
		new_pkt = pkt;
	} else {
		new_pkt = NULL;
	}

	/*
	 * Set up dma info
	 */
	if (bp) {
		if (ncr_scsi_dmaget(pkt, (opaque_t)bp,
		    callback, arg) == NULL) {
			if (new_pkt)
				ncr_scsi_pktfree(ap, new_pkt);
			return (NULL);
		}
	}

	return (pkt);
}



/*
 * pktalloc with callback arg
 */
static struct scsi_pkt *
ncr_scsi_pktalloc(struct scsi_address *ap, int cmdlen, int statuslen,
    int tgtlen, int (*callback)(), caddr_t callback_arg)
{
	int kf, failure;
	register struct scsi_cmd *cmd;
	register caddr_t cdbp, scbp;
	caddr_t	tgt;

	TRACE_3(TR_FAC_SCSI_RES, TR_SCSI_IMPL_PKTALLOC_START,
		"ncr_scsi_pktalloc_start: addr %x cmdlen %d statuslen %d",
		ap, cmdlen, statuslen);
	cdbp = scbp = (caddr_t)0;
	kf = (callback == SLEEP_FUNC)? KM_SLEEP: KM_NOSLEEP;
	failure = 0;
	cmd = (struct scsi_cmd *)0;


	if (cmdlen > sizeof (union scsi_cdb)) {
		if ((cdbp = kmem_zalloc((size_t)cmdlen, kf)) == NULL) {
			failure++;
		}
	}

	/*
	 * Allocate target-private data, if necessary
	 */
	if (tgtlen > PKT_PRIV_LEN) {
		if ((tgt = kmem_zalloc(tgtlen, kf)) == NULL) {
			failure++;
		}
	} else {
		tgt = NULL;
	}

	/*
	 * drive on regardless of failures; cleanup later
	 * we do fast allocs only for pkts with 2 types of statuslen's
	 */
	if ((statuslen > STATUS_SIZE) &&
	    (statuslen != EXTCMDS_STATUS_SIZE)) {
		if ((scbp = kmem_zalloc((size_t)statuslen, kf)) == NULL) {
			failure++;
		}
	}

	mutex_enter(&scsi_rmutex);
	if (statuslen == EXTCMDS_STATUS_SIZE) {
		cmd = (struct scsi_cmd *)
			kmem_fast_zalloc(&secmds, EXTCMDS_SIZE, 1, kf);
		if (cmd) {
			cmd->cmd_flags |= CFLAG_EXTCMDS_ALLOC;
		}
	} else {
		cmd = (struct scsi_cmd *)
			kmem_fast_zalloc(&scmds, sizeof (*cmd), 1, kf);
	}
	mutex_exit(&scsi_rmutex);
	if (cmd == (struct scsi_cmd *)NULL) {
		failure++;
	}

	if (!failure) {
		if (cdbp != (caddr_t)0) {
			cmd->cmd_pkt.pkt_cdbp = (opaque_t)cdbp;
			cmd->cmd_flags |= CFLAG_CDBEXTERN;
		} else {
			cmd->cmd_pkt.pkt_cdbp = (opaque_t)&cmd->cmd_cdb_un;
		}

		if (statuslen == EXTCMDS_STATUS_SIZE) {
			cmd->cmd_pkt.pkt_scbp = (opaque_t)
			    ((u_char *)cmd + sizeof (*cmd));
		} else if (scbp != (caddr_t)0) {
			cmd->cmd_pkt.pkt_scbp = (opaque_t)scbp;
			cmd->cmd_flags |= CFLAG_SCBEXTERN;
		} else {
			cmd->cmd_pkt.pkt_scbp = (opaque_t)&cmd->cmd_scb[0];
		}

		cmd->cmd_cdblen = (u_char)cmdlen;
		cmd->cmd_scblen = (u_char)statuslen;
		cmd->cmd_pkt.pkt_address = *ap;

		/*
		 * Set up target-private data area
		 */
		cmd->cmd_privlen = (u_char) tgtlen;
		if (tgtlen > PKT_PRIV_LEN) {
			cmd->cmd_flags |= CFLAG_PRIVEXTERN;
			cmd->cmd_pkt.pkt_private = tgt;
		} else if (tgtlen > 0) {
			cmd->cmd_pkt.pkt_private = cmd->cmd_pkt_private;
		}
	} else {
		if (tgt) {
			kmem_free(tgt, tgtlen);
		}
		if (cdbp) {
			kmem_free(cdbp, (size_t)cmdlen);
		}
		if (scbp) {
			kmem_free(scbp, (size_t)statuslen);
		}
		if (callback != NULL_FUNC) {
			TRACE_0(TR_FAC_SCSI_RES,
			    TR_SCSI_IMPL_PKTALLOC_CALLBACK_START,
			    "ncr_scsi_pktalloc_callback_call (begin)");
			ddi_set_callback(callback, callback_arg, &scsi_cb_id);
			TRACE_0(TR_FAC_SCSI_RES,
			    TR_SCSI_IMPL_PKTALLOC_CALLBACK_END,
			    "ncr_scsi_pktalloc_callback_call (end)");
		}
	}
	TRACE_0(TR_FAC_SCSI_RES, TR_SCSI_IMPL_PKTALLOC_END,
	    "ncr_scsi_pktalloc_end");
	return ((struct scsi_pkt *)cmd);
}



static void
ncr_scsi_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	ncr_scsi_dmafree(ap, pkt);
	ncr_scsi_pktfree(ap, pkt);
}


/*
 * packet free
 */
/*ARGSUSED*/
static void
ncr_scsi_pktfree(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct scsi_cmd *sp = (struct scsi_cmd *)pkt;

	TRACE_1(TR_FAC_SCSI_RES, TR_SCSI_IMPL_PKTFREE_START,
	    "ncr_scsi_pktfree_start: pkt %x", pkt);
	if (sp->cmd_flags & CFLAG_FREE) {
		cmn_err(CE_PANIC, "scsi_std_pktfree: freeing free packet");
		/* NOTREACHED */
	}
	if (sp->cmd_flags & CFLAG_CDBEXTERN) {
		kmem_free((caddr_t)sp->cmd_pkt.pkt_cdbp,
		    (size_t)sp->cmd_cdblen);
	}
	if (sp->cmd_flags & CFLAG_SCBEXTERN) {
		kmem_free((caddr_t)sp->cmd_pkt.pkt_scbp,
		    (size_t)sp->cmd_scblen);
	}
	if (sp->cmd_flags & CFLAG_PRIVEXTERN) {
		kmem_free((caddr_t)sp->cmd_pkt.pkt_private,
			(size_t)sp->cmd_privlen);
	}
	mutex_enter(&scsi_rmutex);
	if (sp->cmd_flags & CFLAG_EXTCMDS_ALLOC) {
		sp->cmd_flags = CFLAG_FREE;
		kmem_fast_free(&secmds, (caddr_t)sp);
	} else {
		sp->cmd_flags = CFLAG_FREE;
		kmem_fast_free(&scmds, (caddr_t)sp);
	}
	mutex_exit(&scsi_rmutex);
	if (scsi_cb_id) {
		TRACE_0(TR_FAC_SCSI_RES, TR_SCSI_IMPL_PKTFREE_RUN_CALLBACK,
		    "ncr_scsi_pktfree_run_callback_call");
		ddi_run_callback(&scsi_cb_id);
	}
	TRACE_0(TR_FAC_SCSI_RES, TR_SCSI_IMPL_PKTFREE_END,
	    "ncr_scsi_pktfree_end");
}


/*
 * Dma resource allocation
 */

static struct scsi_pkt *
ncr_scsi_dmaget(struct scsi_pkt *pkt, opaque_t dmatoken,
    int (*callback)(), caddr_t callback_arg)
{
	struct buf *bp = (struct buf *)dmatoken;
	struct scsi_cmd *cmd = (struct scsi_cmd *)pkt;
	dev_info_t *dip;
	int flags, rval;

	TRACE_1(TR_FAC_SCSI_RES, TR_SCSI_IMPL_DMAGET_START,
	    "ncr_scsi_dmaget_start: callback %x", callback);

	/*
	 * clear any stale flags
	 */

	cmd->cmd_flags &= ~(CFLAG_DMASEND|CFLAG_DMAVALID);

	/*
	 * Get the host adapter's dev_info pointer
	 */
	dip = PKT2TRAN(pkt)->tran_hba_dip;
	if (bp->b_flags & B_READ) {
		flags = DDI_DMA_READ;
	} else {
		cmd->cmd_flags |= CFLAG_DMASEND;
		flags = DDI_DMA_WRITE;
	}
	if (cmd->cmd_flags & CFLAG_CMDIOPB)
		flags |= DDI_DMA_CONSISTENT;
	TRACE_3(TR_FAC_SCSI_RES, TR_SCSI_IMPL_DMAGET_BUFSETUP_START,
	    "ncr_scsi_dmaget_bufsetup_call (begin): dip %x bp %x flag %d",
	    dip, bp, flags);
	rval = ddi_dma_buf_setup(dip, bp, flags, callback,
	    callback_arg, 0, &cmd->cmd_dmahandle);

	if (rval && rval != DDI_DMA_PARTIAL_MAP) {
		switch (rval) {
		case DDI_DMA_NORESOURCES:
			bp->b_error = 0;
			break;
		case DDI_DMA_NOMAPPING:
			bp->b_error = EFAULT;
			bp->b_flags |= B_ERROR;
			break;
		case DDI_DMA_TOOBIG:
			bp->b_error = EINVAL;
			bp->b_flags |= B_ERROR;
			break;
		}
		TRACE_1(TR_FAC_SCSI_RES, TR_SCSI_IMPL_DMAGET_BUFSETUP_FAILED,
		    "ncr_scsi_dmaget_bufsetup_call (failed): rval %d", rval);
		return ((struct scsi_pkt *)NULL);
	}
	cmd->cmd_dmacount = bp->b_bcount;
	if ((bp->b_flags & B_READ) == 0)
		cmd->cmd_flags |= CFLAG_DMASEND;
	cmd->cmd_flags |= CFLAG_DMAVALID;
	TRACE_1(TR_FAC_SCSI_RES, TR_SCSI_IMPL_DMAGET_END,
	    "ncr_scsi_dmaget_end: cmd %x", cmd);
	return ((struct scsi_pkt *)cmd);
}

/*ARGSUSED*/
static void
ncr_scsi_dmafree(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct scsi_cmd *cmd = (struct scsi_cmd *)pkt;

	if ((cmd->cmd_flags & CFLAG_DMAVALID) == 0) {
		return;
	}

	/*
	 * Free the mapping.
	 */
	ddi_dma_free(cmd->cmd_dmahandle);
	cmd->cmd_flags ^= CFLAG_DMAVALID;
}


/*ARGSUSED*/
static void
ncr_scsi_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	register int i;
	register struct scsi_cmd *sp = (struct scsi_cmd *)pkt;

	if (sp->cmd_flags & CFLAG_DMAVALID) {
		i = ddi_dma_sync(sp->cmd_dmahandle, 0, 0,
			(sp->cmd_flags & CFLAG_DMASEND) ?
			DDI_DMA_SYNC_FORDEV : DDI_DMA_SYNC_FORCPU);
		if (i != DDI_SUCCESS) {
			cmn_err(CE_WARN, "ncr: sync pkt failed");
		}
	}
}


/* ARGSUSED */
static int
ncr_attach(dev_info_t *dev, ddi_attach_cmd_t cmd)
{
	struct ncr *ncr;
	auto caddr_t reg;
	static int timein = 0;
	u_char junk;
	scsi_hba_tran_t	*tran;
	ddi_dma_lim_t *lim;
	ddi_idevice_cookie_t ncr_idevc;
	ddi_iblock_cookie_t ncr_iblk;
	u_long sbcaddr, ctladdr, dmaaddr;
	int host_type = -1;

	switch (cmd) {
	case DDI_ATTACH:
		break;

	default:
		return (DDI_FAILURE);
	}

	PRINTF1("ncr_attach\n");

	if (ddi_map_regs(dev, (u_int)0, &reg, (off_t)0, NCRHWSIZE)) {
		printf("ncr: unable to map registers\n");
		return (DDI_FAILURE);
	}

	/*
	 * Check for ncr 5380 Scsi Bus Ctlr chip with "ddi_peekc()"; struct
	 * ncr-5380 is common to all onboard scsi and vme scsi board. if not
	 * exist, return 0.
	 */

	/*
	 * We can use the first byte address of reg, 'coz we'll guarantee
	 * (by the time we are through) that that will always be the first
	 * byte of the NCR 5380 SBC (which should be the cbsr register).
	 */

	if (ddi_peekc(dev, reg, (char *)0) != DDI_SUCCESS) {
failed:
		ddi_unmap_regs(dev, 0, &reg, 0, (off_t)NCRHWSIZE);
		return (DDI_FAILURE);
	}
	sbcaddr = (u_long) reg;
	dmaaddr = ((u_long) reg) + sizeof (struct ncrsbc);

	/*
	 * probe for different host adaptor interfaces
	 */

	if (cputype == CPU_SUN4_110) {
		/*
		 * probe for 4/110 dma interface
		 */
		PRINTF1("ncr_attach: probing 4-110\n");

		ctladdr = ((u_long) reg) + COBRA_CSR_OFF;
		if (ddi_peekl(dev, (long *)dmaaddr, (long *)0)
		    != DDI_SUCCESS) {
			goto failed;
		}
		PRINTF1("ncr_attach: 4/110 host exists\n");
		host_type = IS_COBRA;
	} else {
		/*
		 * This is either a SCSI-3 or a 3/E VME board
		 * This implementation only is interested in the SCSI-3.
		 */
		if (ddi_peeks(dev, (short *)(dmaaddr + 2), (short *)0)
		    != DDI_SUCCESS) {
			goto failed;
		}

		/*
		 * Make sure that it isn't a SCSI-2 board (which occupies 4k
		 * of VME space instead of the 2k that the SCSI-3 occupies).
		 * (the above is a quote from si.c. The code below doesn't
		 *  seem to bear this out exactly).
		 */
		PRINTF1("ncr_attach: checking a SCSI-3\n");
		if (ddi_peeks(dev, (short *)(((u_long) reg) + 0x800),
		    (short *)0) == DDI_SUCCESS) {
			goto failed;
		}

		/*
		 * Make sure that we're cool (really a scsi-3 board).
		 */
		ctladdr = ((u_long) reg) + CSR_OFF;
		if (ddi_peeks(dev, (short *)(ctladdr + 2), (short *)0)
		    != DDI_SUCCESS) {
			goto failed;
		}

		if ((((struct ncrctl *)ctladdr)->csr.lsw & NCR_CSR_ID) == 0) {
			printf("ncr%d: unmodified scsi-3 board- you lose..\n",
			    ddi_get_instance(dev));
			goto failed;
		}
		PRINTF1("ncr_attach: SCSI-3 host exists\n");
		host_type = IS_SCSI3;
	}

	/*
	 * Since we know that some instantiations of this device can
	 * be plugged into slave-only VME slots, check to see whether
	 * this is one such.
	 */
	if (ddi_slaveonly(dev) == DDI_SUCCESS) {
		printf("ncr%d: not used - device in slave-only spot\n",
		    ddi_get_instance(dev));
		goto failed;
	}

	/*
	 * Establish initial softc values
	 */

	ncr = (struct ncr *)kmem_zalloc(sizeof (*ncr), KM_SLEEP);
	if (ncr_softc == NULL) {
		ncr_softc = ncr;
	} else {
		ncr_tail->n_nxt = ncr;
	}
	ncr_tail = ncr;

	/*
	 * Add dummy intr handler, just to get the cookie
	 */
	if (ddi_add_intr(dev, (u_int)0, &ncr_iblk,
	    &ncr_idevc, (u_int (*)(caddr_t)) nulldev, (caddr_t)ncr)) {
		cmn_err(CE_PANIC, "ncr_attach: cannot add dummy intr");
		/* NOTREACHED */
	}

	ncr->n_iblock = (void *) ncr_iblk;
	mutex_init(&ncr->n_mutex, "ncr mutex",
	    MUTEX_DRIVER, ncr->n_iblock);

	ddi_remove_intr(dev, (u_int)0, &ncr_iblk);

	/*
	 * Now add real intr handler
	 */
	if (ddi_add_intr(dev, (u_int)0, &ncr_iblk,
	    &ncr_idevc, ncr_intr, (caddr_t)ncr)) {
		cmn_err(CE_PANIC, "ncr_attach: cannot add intr");
		/* NOTREACHED */
	}


	/*
	 * Initialize some of software structure
	 */

	ncr->n_sbc = (struct ncrsbc *)(sbcaddr);
	ncr->n_dma = (struct ncrdma *)(dmaaddr);
	ncr->n_ctl = (struct ncrctl *)(ctladdr);
	ncr->n_type = (u_char)host_type;
	/*
	 * Initialize interrupt vector register (if any)
	 */

	if (IS_VME(ncr->n_type) && ncr_idevc.idev_vector) {
		N_CTL->iv_am =
		    (ncr_idevc.idev_vector & 0xff) | (VME_AM_DFLT << 8);
	}


	/*
	 * Finish establishing initial softc values
	 */

	ncr->n_id = (u_char) ddi_getprop(DDI_DEV_T_NONE, dev, 0,
	    "initiator-id", NCR_HOST_ID);

	/*
	 * Allocate a transport structure
	 */
	tran = scsi_hba_tran_alloc(dev, 0);
	if (tran == NULL) {
		cmn_err(CE_WARN, "ncr: scsi_hba_tran_alloc failed\n");
		ddi_remove_intr(dev, (u_int)0, &ncr_iblk);
		ddi_unmap_regs(dev, 0, &reg, 0, (off_t)NCRHWSIZE);
		mutex_destroy(&ncr->n_mutex);
		return (DDI_FAILURE);
	}

	ncr->n_tran		= tran;

	tran->tran_hba_private	= ncr;
	tran->tran_tgt_private	= NULL;

	tran->tran_tgt_init	= ncr_scsi_tgt_init;
	tran->tran_tgt_probe	= ncr_scsi_tgt_probe;
	tran->tran_tgt_free	= ncr_scsi_tgt_free;

	tran->tran_start	= ncr_start;
	tran->tran_abort	= ncr_abort;
	tran->tran_reset	= ncr_reset;
	tran->tran_getcap	= ncr_getcap;
	tran->tran_setcap	= ncr_setcap;
	tran->tran_init_pkt	= ncr_scsi_init_pkt;
	tran->tran_destroy_pkt	= ncr_scsi_destroy_pkt;
	tran->tran_dmafree	= ncr_scsi_dmafree;
	tran->tran_sync_pkt	= ncr_scsi_sync_pkt;

	/*
	 * Attach this instance of the hba
	 */
	lim = (ncr->n_type == IS_COBRA) ? &ncrlim_cobra : &ncrlim_vme;

	if (scsi_hba_attach(dev, lim, tran, 0, NULL) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ncr: scsi_hba_attach failed\n");
		scsi_hba_tran_free(tran);
		ddi_remove_intr(dev, (u_int)0, &ncr_iblk);
		ddi_unmap_regs(dev, 0, &reg, 0, (off_t)NCRHWSIZE);
		mutex_destroy(&ncr->n_mutex);
		return (DDI_FAILURE);
	}

	ncr->n_last_slot = ncr->n_cur_slot = UNDEFINED;

	if (ncr->n_type == IS_COBRA) {
		ncr->n_dma_cleanup = ncr_cobra_dma_cleanup;
	} else {
		ncr->n_dma_cleanup = ncr_vme_dma_cleanup;
	}

	if (timein == 0) {
		timein++;
		ncr_watchdog_tick = drv_usectohz((clock_t)
			(scsi_watchdog_tick * 1000000));
		(void) timeout(ncr_watch, (caddr_t)0, ncr_watchdog_tick);
	}

	mutex_enter(&ncr->n_mutex);
	ncr_internal_reset(ncr, NCR_RESET_ALL, RESET_NOMSG);

	/*
	 * Turn on Parity checking if desired, clear pending parity errors
	 */
	if (scsi_options & SCSI_OPTIONS_PARITY) {
		N_SBC->mr |= NCR_MR_EPC;
		junk = N_SBC->clr;
#ifdef	lint
		junk = junk;
#endif
	}
	mutex_exit(&ncr->n_mutex);

	/*
	 * Say that we're here
	 */

	ddi_report_dev(dev);

	/*
	 * And return success
	 */
	return (DDI_SUCCESS);
}

/*
 * External Interface Routines
 */
static ncr_nodma_warning;

static int
ncr_start(struct scsi_address *ap, register struct scsi_pkt *pkt)
{
	register struct scsi_cmd *sp = (struct scsi_cmd *)pkt;
	register struct ncr *ncr;
	register u_long maxdma;
	register short slot;

	/*
	 * get scsi address based on the "cookie" in cmd_packet
	 */
	ncr = ADDR2NCR(ap);

	maxdma = IS_VME(ncr->n_type)? NCR_DMA_MAX : NCR_DMA_COBRA_MAX;
	if (sp->cmd_dmacount >= maxdma) {
		return (TRAN_BADPKT);
	}

	/*
	 * reinitialize some fields in the 'cmd_pkt' that need it...
	 * if upper level requests watchdog, set timeout_cnt and flag
	 */

	sp->cmd_flags &= ~CFLAG_TRANFLAG;
	sp->cmd_pkt.pkt_reason = sp->cmd_pkt.pkt_state = 0;
	sp->cmd_pkt.pkt_statistics = 0;
	sp->cmd_cdbp = sp->cmd_pkt.pkt_cdbp;
	sp->cmd_scbp = sp->cmd_pkt.pkt_scbp;
	*sp->cmd_scbp = 0;		/* clear status byte array */
	if ((sp->cmd_timeout = sp->cmd_pkt.pkt_time) != 0)
		sp->cmd_flags |= CFLAG_WATCH;
	if (sp->cmd_flags & CFLAG_DMAVALID) {
		off_t off;
		u_int len;
		if (ddi_dma_htoc(sp->cmd_dmahandle, 0, &sp->cmd_dmacookie)) {
			cmn_err(CE_PANIC, "ncr: htoc failed");
			/* NOTREACHED */
		}
		/*
		 * We're ignoring type bits for the moment
		 */
		sp->cmd_data = sp->cmd_saved_data = 0;
		sp->cmd_dsegs.sd_cnt = 0;
		sp->cmd_pkt.pkt_resid = sp->cmd_dmacount;
		if (sp->cmd_dmacount > sp->cmd_dmacookie.dmac_size) {
			/*
			 * Since our DMA engine doesn't have an interrupt
			 * when its count exhausts, in order to support
			 * windows we have to do this a byte at a time
			 */
			sp->cmd_flags |= CFLAG_NODMA;
			if (++ncr_nodma_warning > 100) {
				cmn_err(CE_WARN,
			"ncr: nodma for addr %x size 0x%x, xfer size to big\n",
					sp->Cookie.dmac_address,
					sp->Cookie.dmac_size);
				ncr_nodma_warning = 0;
				/*
				 * we should really return BAD_PKT here
				 */
			}
		}
		if (ddi_dma_curwin(sp->Handle, &off, &len) == DDI_SUCCESS) {
			sp->cmd_dmawinsize = len;
		} else {
			sp->cmd_dmawinsize = 0;
		}
		sp->cmd_curaddr = sp->Cookie.dmac_address;
		if (sp->cmd_flags & CFLAG_CMDIOPB) {
			(void) ddi_dma_sync(sp->Handle, 0, (u_int) -1,
			    DDI_DMA_SYNC_FORDEV);
		}
	} else {
		sp->cmd_pkt.pkt_resid = 0;
	}

	/*
	 * If this is a non-interrupting command,
	 * we don't allow disconnects, but that
	 * is fielded down in the select code.
	 */

	slot =	((Tgt(sp) * NLUNS_PER_TARGET) | Lun(sp));
	mutex_enter(&ncr->n_mutex);
	if (ncr->n_slots[slot] != (struct scsi_cmd *)0) {
		PRINTF3("ncr_start: queuing error\n");
		mutex_exit(&ncr->n_mutex);
		return (TRAN_BUSY);
	}

	/*
	 * accept the command by setting the req job_ptr in appropriate slot
	 */

	ncr->n_ncmds++;
	ncr->n_slots[slot] = sp;

	if ((sp->cmd_pkt.pkt_flags & FLAG_NOINTR) == 0) {
		if ((ncr->n_npolling == 0) && (ncr->n_state == STATE_FREE)) {
			(void) ncr_ustart(ncr, slot);
		}
	} else  if (ncr->n_state == ACTS_ABORTING) {
		printf("%s%d: unable to start non-intr cmd\n", CNAME, CNUM);
		mutex_exit(&ncr->n_mutex);
		return (FALSE);
	} else {
		/*
		 * Wait till all current commands completed with STATE_FREE by
		 * "ncr_dopoll()", then fire off the job, check if preempted
		 * right away, try again; accept the command
		 */
		PRINTF3("ncr_start: poll cmd, state= %x\n", ncr->n_state);
		DISABLE_INTR(ncr);
		ncr->n_npolling++;
		while (ncr->n_npolling != 0) {
			/*
			 * drain any current active command(s)
			 */
			while (ncr->n_state != STATE_FREE) {
				if (ncr_dopoll(ncr)) {
					ENABLE_INTR(ncr);
					mutex_exit(&ncr->n_mutex);
					return (FALSE);
				}
			}
			/*
			 * were we preempted by a reselect coming back in?
			 * If so, 'round we go again....
			 */
			PRINTF3("ncr_start: ready to start cmd\n");
			if (ncr_ustart(ncr, slot) == FALSE)
				continue;
			/*
			 * Okay, now we're 'running' this command.
			 *
			 * ncr_dopoll will return when ncr->n_state ==
			 * STATE_FREE, but this can also mean a reselection
			 * preempt occurred.
			 */
			PRINTF3("ncr_start: poll_res\n");
			if (ncr_dopoll(ncr)) {
				ENABLE_INTR(ncr);
				mutex_exit(&ncr->n_mutex);
				return (FALSE);
			}
		}
		PRINTF3("ncr_start: start next slot= %x\n", NEXTSLOT(slot));
		(void) ncr_ustart(ncr, NEXTSLOT(slot));
		ENABLE_INTR(ncr);

	}
	mutex_exit(&ncr->n_mutex);
	PRINTF3("ncr_start: done\n");
	return (TRUE);
}

/*ARGSUSED*/
static int
ncr_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	PRINTF3("ncr_abort: pkt= %x\n", pkt);
	if (pkt != (struct scsi_pkt *)0) {
		struct ncr *ncr = ADDR2NCR(ap);
		PRINTF3("ncr_abort: call ncr_do_abort(RESET)\n");
		mutex_enter(&ncr->n_mutex);
		ncr_do_abort(ncr,
		    NCR_RESET_ALL, RESET_NOMSG);
		mutex_exit(&ncr->n_mutex);
		return (TRUE);
	} else {
		return (FALSE);
	}
}

static int
ncr_reset(struct scsi_address *ap, int level)
{
	struct ncr *ncr = ADDR2NCR(ap);

	PRINTF3("ncr_reset: level= %x\n", level);
	/*
	 * if RESET_ALL requested, call "ncr_do_abort()" to clear all
	 * outstanding jobs in the queue
	 */
	if (level == RESET_ALL) {
		mutex_enter(&ncr->n_mutex);
		ncr_do_abort(ncr, NCR_RESET_ALL, RESET_NOMSG);
		mutex_exit(&ncr->n_mutex);
		return (TRUE);
	} else
		return (FALSE);
}

static int
ncr_commoncap(struct scsi_address *ap, char *cap,
    int val, int tgtonly, int doset)
{
	register struct ncr *ncr = ADDR2NCR(ap);
	register cidx;
	register u_char tshift = (1<<ap->a_target);
	register u_char ntshift = ~tshift;
	register rval = FALSE;

	if ((tgtonly != 0 && tgtonly != 1) || cap == (char *)0) {
		return (rval);
	}

	cidx = scsi_hba_lookup_capstr(cap);
	if (cidx == -1) {
		rval = UNDEFINED;
	} else if (doset && (val == 0 || val == 1)) {
		/*
		 * At present, we can only set binary (0/1) values
		 */

		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
		case SCSI_CAP_MSG_OUT:
		case SCSI_CAP_PARITY:
		case SCSI_CAP_INITIATOR_ID:
			/*
			 * None of these are settable via
			 * the capability interface.
			 */
			break;

		case SCSI_CAP_DISCONNECT:

			if ((scsi_options & SCSI_OPTIONS_DR) == 0) {
				break;
			} else if (tgtonly) {
				if (val)
					ncr->n_nodisc &= ntshift;
				else
					ncr->n_nodisc |= tshift;
			} else {
				ncr->n_nodisc = (val) ? 0 : 0xff;
			}
			rval = TRUE;
			break;

		case SCSI_CAP_SYNCHRONOUS:

			break;

		case SCSI_CAP_WIDE_XFER:
		case SCSI_CAP_UNTAGGED_QING:
		case SCSI_CAP_TAGGED_QING:
		default:
			rval = UNDEFINED;
			break;
		}
	} else if (doset == 0) {
		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
			rval = IS_VME(ncr->n_type) ?
			    NCR_DMA_MAX : NCR_DMA_COBRA_MAX;
			break;
		case SCSI_CAP_MSG_OUT:
			rval = TRUE;
			break;
		case SCSI_CAP_DISCONNECT:
			if ((scsi_options & SCSI_OPTIONS_DR) &&
			    (tgtonly == 0 || (ncr->n_nodisc & tshift) == 0)) {
				rval = TRUE;
			}
			break;
		case SCSI_CAP_SYNCHRONOUS:
			rval = FALSE;
			break;
		case SCSI_CAP_PARITY:
			if (scsi_options & SCSI_OPTIONS_PARITY)
				rval = TRUE;
			break;
		case SCSI_CAP_INITIATOR_ID:
			rval = ncr->n_id;
			break;
		case SCSI_CAP_LINKED_CMDS:
#ifdef LINKED_CMDS
			rval = TRUE;
#endif
			break;
		default:
			rval = UNDEFINED;
			break;
		}
	}
	return (rval);
}

static int
ncr_getcap(struct scsi_address *ap, char *cap, int whom)
{
	return (ncr_commoncap(ap, cap, 0, whom, 0));
}

static int
ncr_setcap(struct scsi_address *ap, char *cap, int value, int whom)
{
	return (ncr_commoncap(ap, cap, value, whom, 1));
}

/*
 * Internal start and finish routines
 */

/*
 * Start the next command on the host adapter.
 * Search from start_slot for work to do.
 *
 *
 *	input:  (struct ncr) *ncr= pointer to a ncr software structure;
 *		(short) start_slot= requested slot (target/lun combo);
 *	return: (int) TRUE(1)= command started okay;
 *		(int) FALSE(0)= not started (due to reselection or no work)
 */

static int
ncr_ustart(register struct ncr *ncr, short start_slot)
{
	register struct scsi_cmd *sp;
	register short slot = start_slot;
	int found = 0;

	PRINTF3("ncr_ustart: start_slot= %x\n", start_slot);

	/*
	 * Start off a new job in the queue ONLY number of running cmds
	 * is less than disconnected cmds, in hope of NOT floating the bus
	 * and allow the previously disconnected jobs to be finished first,
	 * if more currently disconnected jobs, return ACTION_ABORT
	 *
	 */

	if (((int)ncr->n_ncmds - (int)ncr->n_ndisc) <= 0) {
		PRINTF3("ncr_ustart: NO new-job\n");
		return (FALSE);
	}

	/*
	 * search for any ready cmd available (started first with the req
	 * slot, then move to next slot until reaching req_one)
	 */

	do {
		sp = ncr->n_slots[slot];
		if (sp && ((sp->cmd_flags & CFLAG_CMDDISC) == 0)) {
			found++;
		} else {
			slot = NEXTSLOT(slot);
		}
	} while ((found == 0) && (slot != start_slot));

	if (!found) {
		return (FALSE);
	}

	UPDATE_STATE(STATE_STARTING);

	PRINTF3("ncr_ustart: starting %d.%d\n", Tgt(sp), Lun(sp));
	ncr->n_cur_slot = slot;
	ncr->n_omsgidx = ncr->n_omsglen = 0;


	/*
	 * Attempt to arbitrate for the bus and select the target.
	 *
	 * ncr_select() can return one of SEL_TRUE (target selected),
	 * SEL_ARBFAIL (unable to get the bus), SEL_FALSE (target did
	 * not respond to selection), or SEL_RESEL (a reselection attempt
	 * is in progress). As a side effect, if SEL_TRUE is the return,
	 * DMA (and interrupts) from the SBC are disabled.
	 *
	 */

	switch (ncr_select(ncr)) {
	case SEL_ARBFAIL:
		/*
		 * We should treat arbitration failures
		 * differently from selection failures.
		 */

	case SEL_FALSE:
		ncr_finish(ncr);
		return (FALSE);

	case SEL_TRUE:

		UPDATE_STATE(ACTS_UNKNOWN);
		if (NON_INTR(sp))
			ncr_phasemanage(ncr);
		return (TRUE);

	case SEL_RESEL:

		/*
		 * Couldn't select due to a reselection coming in.
		 * Push the state of this command back to what it was.
		 */
		ncr_preempt(ncr);
		return (FALSE);
	}
	/* NOTREACHED */
}

/*
 * Finish routine
 */

static void
ncr_finish(register struct ncr *ncr)
{
	short last_slot;
	register int span_states = 0;
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);

	PRINTF3("ncr_finish:\n");

	if (ncr->n_last_msgin == MSG_LINK_CMPLT ||
		ncr->n_last_msgin == MSG_LINK_CMPLT_FLAG) {
		span_states++;
	}

	if (sp->cmd_pkt.pkt_state & STATE_XFERRED_DATA) {
		/*
		 * XXX: We do not support more than one segment yet
		 */

		sp->cmd_pkt.pkt_resid =
		    sp->cmd_dmacount - sp->cmd_dsegs.sd_cnt;
		if (sp->cmd_flags & CFLAG_CMDIOPB) {
			(void) ddi_dma_sync(sp->Handle, 0, (u_int) -1,
			    DDI_DMA_SYNC_FORCPU);
		}
		if (INFORMATIVE && sp->cmd_pkt.pkt_resid) {
			PRINTF2("ncr_finish: %d.%d finishes with %d resid\n",
			    Tgt(sp), Lun(sp), sp->cmd_pkt.pkt_resid);
		}
	}


	ncr->n_ncmds -= 1;
	last_slot = ncr->n_last_slot = ncr->n_cur_slot;
	ncr->n_lastcount = 0;
	ncr->n_cur_slot = UNDEFINED;
	ncr->n_slots[last_slot] = (struct scsi_cmd *)0;
	ncr->n_omsglen = ncr->n_omsgidx = 0;
	ncr->n_last_msgin = 0xfe;
	UPDATE_STATE(STATE_FREE);

	PRINTF3("ncr_finish: span= %x, flag= %x\n",
		span_states, sp->cmd_pkt.pkt_flags);
	if (NON_INTR(sp)) {
		PRINTF3("ncr_finish: poll= calling upper target to finish\n");
		ncr->n_npolling -= 1;
		if (sp->cmd_pkt.pkt_comp) {
			mutex_exit(&ncr->n_mutex);
			(*sp->cmd_pkt.pkt_comp)(sp);
			mutex_enter(&ncr->n_mutex);
		}
	} else if (span_states > 0) {
		ncr->n_state = ACTS_SPANNING;
		if (sp->cmd_pkt.pkt_comp) {
			mutex_exit(&ncr->n_mutex);
			(*sp->cmd_pkt.pkt_comp)(sp);
			mutex_enter(&ncr->n_mutex);
		}
		/*
		 * This is we can check upon return that
		 * the target driver did the right thing...
		 *
		 * If the target driver didn't do the right
		 * thing, we have to abort the operation.
		 */
		if (ncr->n_slots[last_slot] == 0) {
			ncr_internal_abort(ncr);
		} else {
			PRINTF2("%s%d: linked command start\n", CNAME, CNUM);
			ncr->n_cur_slot = last_slot;
			ncr->n_nlinked++;
			if (ncr_ACKmsg(ncr)) {
				ncr_do_abort(ncr, NCR_RESET_ALL, RESET_NOMSG);
			} else {
				UPDATE_STATE(ACTS_UNKNOWN);
				ncr_phasemanage(ncr);
			}
		}
	} else {
		if (sp->cmd_pkt.pkt_comp) {
			mutex_exit(&ncr->n_mutex);
			(*sp->cmd_pkt.pkt_comp)(sp);
			mutex_enter(&ncr->n_mutex);
		}
		if (ncr->n_state == STATE_FREE) {
			(void) ncr_ustart(ncr, last_slot);
		}
	}
}



/*
 * Interrupt Service Routines
 */


/*
 * Polled service routine - called when interrupts are not feasible
 */

static int
ncr_dopoll(register struct ncr *ncr)
{
	register int i;

	PRINTF3("ncr_dopoll: state= %x\n", ncr->n_state);
	while (ncr->n_state != STATE_FREE) {
	    PRINTF3("ncr_dopoll: n_state= %x\n", ncr->n_state);
	    for (i = 0; (i < 3*120000) && (ncr->n_state != STATE_FREE); i++) {
		if (INTPENDING(ncr)) {
			ncr_svc(ncr);
			if (ncr->n_state == STATE_FREE)
				continue;
			else
				i = 0;
		} else {
			drv_usecwait(100);
		}

		if (i >= 3*120000 && ncr->n_state != STATE_FREE) {
			EPRINTF("ncr_dopoll: poll_cmd timeout, state= %x\n",
				ncr->n_state);
			return (FAILURE);
		}
	    }
	}
	return (SUCCESS);
}

/*
 * interrupt entry point
 */

static u_int
ncr_intr(caddr_t arg)
{
	register struct ncr *ncr = (struct ncr *)arg;
	register int serviced = DDI_INTR_UNCLAIMED;

	if (ncr->n_dip) {
		mutex_enter(&ncr->n_mutex);
		DISABLE_INTR(ncr);
		/* while (INTPENDING(ncr)) { */
		if (INTPENDING(ncr)) {
			ncr->n_ints++;
			ncr_svc(ncr);
			serviced = DDI_INTR_CLAIMED;
		}
		ENABLE_INTR(ncr);
		mutex_exit(&ncr->n_mutex);
	}
	return (serviced);
}

/*
 * Common interrupt service code- called asynchronously from ncr_poll() or
 * ncrintr(), or synchronously from varying places in the rest of the
 * driver to service interrupts.
 *
 * What kind of interrupts we'll get:
 *
 *	* RESELECTION interrupts
 *	* End of DATA PHASE interrupts (PHASE MISMATCH)
 *	* Some specific PHASE MISMATCH interrupts (driven by
 *	  enabling DMA mode in the sbc mr register after setting
 *	  a bogus phase into the tcr register- when REQ* is asserted
 *	  this causes a phase mismatch interrupt.
 *
 * XXX:	* Monitor LOSS OF BUSY interrupts
 */

static void
ncr_svc(register struct ncr *ncr)
{
	register u_char binary_id = NUM_TO_BIT(ncr->n_id);
	register u_char uctmp;

	/*
	 * 'Disabling' DMA also allows access to SBC registers
	 */
	DISABLE_DMA(ncr);
	uctmp = N_SBC->cbsr;

	if (ncr->n_type != IS_COBRA) {
		ncr->n_lastbcr = GET_BCR(ncr);
		PRINTF1("ncr_svc: csr 0x%b bcr 0x%x\n",
		    GET_CSR(ncr), CSR_BITS, ncr->n_lastbcr);
	}

	/*
	 * First check for a reselect interrupt coming in
	 */
	ncr->n_cdr = N_SBC->cdr;
	if (RESELECTING(uctmp, ncr->n_cdr, binary_id)) {
		if (ncr_reselect(ncr)) {
			uctmp = 0;
			ncr_do_abort(ncr, NCR_RESET_ALL, RESET_NOMSG);
		} else {
			uctmp = 1;
		}
	} else if (IN_DATA_STATE(ncr)) {
		/*
		 * XXX: should be able to return and await another REQ*
		 * XXX: here? Probably wouldn't help because interrupt
		 * XXX: latency + dma cleanup generally will give the
		 * XXX: targets time to shift to status and/or msg in
		 * XXX: phase.
		 */
		if ((*ncr->n_dma_cleanup)(ncr)) {
			ncr_do_abort(ncr, NCR_RESET_ALL, RESET_NOMSG);
			uctmp = 0;
		} else {
			uctmp = 1;
		}
	} else if (ncr->n_state == STATE_FREE) {
		if (ncr_debug) {
			printf("%s%d: spurious interrupt\n", CNAME, CNUM);
			ncr_printstate(ncr);
		}
		uctmp = N_SBC->clr;
		ncr->n_spurint++;
		uctmp = 0;
	} else {
		switch (ncr->n_laststate) {
		case STATE_SELECTED:
			ncr->n_pmints[PM_SEL]++;
			break;
		case ACTS_MSG_IN:
			ncr->n_pmints[PM_MSGIN]++;
			break;
		case ACTS_MSG_OUT:
			ncr->n_pmints[PM_MSGOUT]++;
			break;
		case ACTS_STATUS:
			ncr->n_pmints[PM_STATUS]++;
			break;
		case ACTS_COMMAND:
			ncr->n_pmints[PM_CMD]++;
			break;
		}
		/*
		 * dismiss cause of interrupt
		 */
		N_SBC->mr &= ~NCR_MR_DMA;
		uctmp = N_SBC->clr;
		uctmp = 1;
	}

	if (uctmp) {
		if (ncr->n_state != ACTS_UNKNOWN) {
			UPDATE_STATE(ACTS_UNKNOWN);
		}
		ncr_phasemanage(ncr);
	}

	/*
	 * Enabling dma also enables SBC interrupts
	 */
	ENABLE_DMA(ncr);
}

/*
 * Complete reselection
 */

static int
ncr_reselect(register struct ncr *ncr)
{
	NCR_HIGH_IDECL;
	struct scsi_cmd *sp;
	register target, lun;
	register u_char cdr;
	short slot;
	u_char msgin, binary_id = NUM_TO_BIT(ncr->n_id);

	if (ncr->n_ndisc == 0) {
		printf("%s%d: reselection with no disconnected jobs\n",
		    CNAME, CNUM);
		return (FAILURE);
	} else if (ncr->n_state != STATE_FREE) {
		printf("%s%d: reselection while not in free state\n", CNAME,
		    CNUM);
		return (FAILURE);
	}

	if ((scsi_options & SCSI_OPTIONS_PARITY) && ncr_debug) {
		if (N_SBC->bsr & NCR_BSR_PERR) {
			printf("%s%d: parity error during reselection\n",
			    CNAME, CNUM);
		}
	}
	/*
	 * CRITICAL CODE SECTION DON'T TOUCH
	 */

	BLK_HIGH_INTR;
	cdr = ncr->n_cdr & ~binary_id;
	lun = N_SBC->clr;	/* clear int */

	/*
	 * get reselecting target scsi id
	 */
	if (cdr == 0) {
		UNBLK_HIGH_INTR;
		printf("%s%d: NO reselect_id on the bus\n", CNAME, CNUM);
		return (FAILURE);
	}

	/*
	 * make sure there are only 2 scsi id's set
	 */

	for (target = 0; target < 8; target++) {
		if (cdr & (1 << target))
			break;
	}
	N_SBC->ser = 0; 	/* clear (re)sel int */
	cdr &= ~(1 << target);
	if (cdr != 0) {
		UNBLK_HIGH_INTR;
		printf("%s%d: reselection w > 2 SCSI ids on the bus\n",
			CNAME, CNUM);
		return (FAILURE);
	}

	/*
	 * Respond to reselection by asserting BSY*
	 */

	N_SBC->icr |= NCR_ICR_BUSY;
	UNBLK_HIGH_INTR;

	/*
	 * If reselection ok, target should drop select
	 */

	if (ncr_sbcwait((u_char *)&CBSR, NCR_CBSR_SEL, NCR_WAIT_COUNT, 0)) {
		printf("%s%d: target didn't drop select on reselection\n",
			CNAME, CNUM);
		return (FAILURE);
	}

	/*
	 * We respond by dropping our assertion of BSY*
	 */

	N_SBC->icr &= ~NCR_ICR_BUSY;
	N_SBC->tcr = TCR_UNSPECIFIED;
	N_SBC->ser = 0;	/* clear int */
	N_SBC->ser = binary_id; /* enable (re)sel int */

	UPDATE_STATE(STATE_RESELECT);

	if (ncr_getphase(ncr) != ACTION_CONTINUE) {
		printf("%s%d: no REQ during reselect\n", CNAME, CNUM);
		return (FAILURE);
	}

	if (ncr->n_state != ACTS_MSG_IN) {
		printf("%s%d: reselect not followed by a MSG IN phase\n",
			CNAME, CNUM);
		return (FAILURE);
	}

	/*
	 * Now pick up identify message byte, and acknowledge it.
	 */

	if (ncr_xfrin_noack(ncr, 0, PHASE_MSG_IN, &msgin)) {
		printf("%s%d: can't get IDENTIFY message on reselect\n",
			CNAME, CNUM);
		return (FAILURE);
	}

	if ((msgin & MSG_IDENTIFY) == 0 ||
	    (msgin & (INI_CAN_DISCON|BAD_IDENTIFY))) {
		printf("%s%d: mangled identify message 0x%x\n", CNAME, CNUM,
		    msgin);
		return (FAILURE);
	}
	lun = msgin & (NLUNS_PER_TARGET-1);

	/*
	 * now search for lun to reconnect to
	 */

	lun &= (NLUNS_PER_TARGET-1);
	slot = (target * NLUNS_PER_TARGET) | lun;

	if (ncr->n_slots[slot] == 0) {
		printf("%s%d: Cannot Reconnect Lun %d on Target %d\n",
			CNAME, CNUM, lun, target);
		return (FAILURE);
	}
	ncr->n_cur_slot = slot;
	ncr->n_ndisc--;
	sp = CURRENT_CMD(ncr);

	/*
	 * A reconnect implies a restore pointers operation
	 */

	sp->cmd_cdbp = sp->cmd_pkt.pkt_cdbp;
	sp->cmd_scbp = sp->cmd_pkt.pkt_scbp;
	if (sp->cmd_data != sp->cmd_saved_data) {
		sp->cmd_flags |= CFLAG_CHKSEG;
		sp->cmd_data = sp->cmd_saved_data;
		/*
		 * Revalidate current window (if necessary)
		 */
		if (sp->cmd_dmawinsize) {
			if (ncr_set_window(ncr, sp, "RESTORE PTRS")) {
				return (FAILURE);
			}
		}
	}
	sp->cmd_flags &= ~CFLAG_CMDDISC;

	/*
	 * and finally acknowledge the identify message
	 */
	ncr->n_last_msgin = msgin;
	UPDATE_STATE(ACTS_RESELECTING);
	if (ncr_ACKmsg(ncr)) {
		printf("%s%d: unable to acknowledge identify message\n",
			CNAME, CNUM);
		return (FAILURE);
	}
	return (SUCCESS);
}


/*
 * State Management Section
 */


/*
 * manage phases on the SCSI bus
 *
 * We assume (on entry) that we are connected to a
 * target and that CURRENT_CMD(ncr) is valid. We
 * also assume that the phase is unknown. We continue
 * calling one of a set of phase management routines
 * until we either are going to return (to ncr_svc
 * or ncr_ustart()), or have to abort the command,
 * or are going to call ncr_finish() (to complete
 * this command), or are going to turn aroung and
 * call ncr_ustart() again to start another command.
 *
 */

static void
ncr_phasemanage(register struct ncr *ncr)
{
	register int i, action;
	/*
	 * Important: The order of functions in this array *must* match
	 * the defines in ncrreg.h with respect to the linear sequence
	 * of states from ACTS_UNKNOWN thru ACTS_COMMAND.
	 */
	static int (*itvec[])() = {
		ncr_getphase,
		ncr_sendmsg,
		ncr_recvmsg,
		ncr_recvstatus,
		ncr_sendcmd,
		ncr_recvdata,
		ncr_senddata
	};
	static char *itnames[] = {
		"unknown", "msg out", "msg in", "status",
		"cmd", "data in", "data out"
	};

	action = ACTION_CONTINUE;

	do {
		i = ncr->n_state - ACTS_ITPHASE_BASE;
		PRINTF3("ncr_phasemanage: state = %s\n", itnames[i]);
		action = (*itvec[i])(ncr);
	} while (action == ACTION_CONTINUE);

	switch (action) {
	case ACTION_RETURN:
		break;
	case ACTION_ABORT:
		ncr_do_abort(ncr, NCR_RESET_ALL, RESET_NOMSG);
		break;
	case ACTION_FINISH:
		ncr_finish(ncr);
		break;
	case ACTION_SEARCH:
		ncr_disconnect(ncr);
		break;
	}
}

static int
ncr_getphase(register struct ncr *ncr)
{
	register u_char cbsr;
	register int lim, phase;
	u_long usecs = 0;
	struct scsi_cmd *sp = CURRENT_CMD(ncr);
	static char phasetab[8] = {
		ACTS_DATA_OUT,
		ACTS_DATA_IN,
		ACTS_COMMAND,
		ACTS_STATUS,
		-1,	/* 4 is undefined */
		-1,	/* 5 is undefined */
		ACTS_MSG_OUT,
		ACTS_MSG_IN
	};

	usecs = 10000000;

	for (lim = 0; lim < usecs; lim++) {
		cbsr = N_SBC->cbsr;
		if ((cbsr & NCR_CBSR_BSY) == 0) {
			/*
			 * Unexpected loss of busy!
			 */
			printf("%s%d: target(%x) dropped BSY\n",
				CNAME, CNUM, (ncr->n_cur_slot >>3));
			sp->cmd_pkt.pkt_reason = CMD_UNX_BUS_FREE;
			ncr_printstate(ncr);
			return (ACTION_ABORT);
		} else if ((cbsr & NCR_CBSR_REQ) == 0) {
			/*
			 * If REQ* is not asserted, than the phase bits
			 * are not valid. Delay a few u-secs before checking
			 * again..
			 */
			drv_usecwait(2);
			continue;
		}

		phase = (cbsr >> 2) & 0x7;
		PRINTF3("ncr_getphase: phase= 0x%x\n", phase);

		if (phasetab[phase] == -1) {
		    printf("%s%d: target(%x) garbage phase. CBSR = 0x%b\n",
			CNAME, CNUM, (ncr->n_cur_slot >> 3), cbsr, cbsr_bits);
			return (ACTION_ABORT);
		}

		/*
		 * Note that if the phase is a data phase, we don't attempt
		 * to match it. We leave that action to the discretion of the
		 * dma routines.
		 */
		if (phase > 1 && (N_SBC->bsr & NCR_BSR_PMTCH) == 0) {
			N_SBC->tcr = (u_char) phase;
		}
		UPDATE_STATE(phasetab[phase]);
		return (ACTION_CONTINUE);
	}
	printf("%s%d: REQ* never set\n", CNAME, CNUM);
	return (ACTION_ABORT);
}

/*
 * Send command bytes out the SCSI bus. REQ* should be asserted for us
 * to get here, and the phase should already be matched in the tcr register
 * (i.e., ncr_getphase() has done the right thing for us already).
 *
 * If this is a non-interrupting command, we should actually be able to
 * set up for a phase-mismatch interrupt after sending the command.
 *
 */

static int
ncr_sendcmd(register struct ncr *ncr)
{
	NCR_HIGH_IDECL;
	register u_char junk;
	register volatile struct ncrsbc *sbc = N_SBC;
	register int nonintr;
	struct scsi_cmd *sp = CURRENT_CMD(ncr);

	nonintr = (NON_INTR(sp) ? 1: 0);

	/*
	 * We send a single byte of a command out here.
	 * We could probably check to see whether REQ* is
	 * asserted quickly again here, rather than awaiting
	 * it in ncr_getphase() (for non-interrupting commands)
	 * or spotting it in either ncr_poll() or ncrintr().
	 *
	 * XXX: We should check for command overflow here!
	 */

	sbc->odr = *(sp->cmd_cdbp++);	/* load data */
	sbc->icr = NCR_ICR_DATA;	/* enable sbc to send data */

	/*
	 * complete req/ack handshake
	 */

	BLK_HIGH_INTR;
	sbc->icr |= NCR_ICR_ACK;
	if (!REQ_DROPPED(ncr)) {
		UNBLK_HIGH_INTR;
		EPRINTF("ncr_sendcmd: REQ not dropped, cbsr=0x%b\n",
			CBSR, cbsr_bits);
		sbc->tcr = TCR_UNSPECIFIED;
		sbc->icr = 0;
		return (ACTION_ABORT);
	}
	sbc->tcr = TCR_UNSPECIFIED;
	UNBLK_HIGH_INTR;
	if (nonintr == 0) {
		sbc->mr |= NCR_MR_DMA;
		junk = sbc->clr;
	}
#ifdef lint
	junk = junk;
#endif
	sbc->icr = 0;	/* clear ack */
	sp->cmd_pkt.pkt_state |= STATE_SENT_CMD;
	UPDATE_STATE(ACTS_UNKNOWN);
	if (nonintr) {
		return (ACTION_CONTINUE);
	} else {
		ENABLE_DMA(ncr);
		return (ACTION_RETURN);
	}
}

/*
 * Send out a message. We assume on entry that phase is matched in the tcr
 * register of the SBC, and that REQ* has been asserted by the target (i.e.,
 * ncr_getphase() has done the right thing).
 *
 * If this is a non-interrupting command, set up to get a phase-mismatch
 * interrupt on the next assertion of REQ* by the target.
 *
 */

static int
ncr_sendmsg(register struct ncr *ncr)
{
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);
	register volatile struct ncrsbc *sbc = N_SBC;
	u_char junk;
	register nonintr;
	NCR_HIGH_IDECL;

	nonintr = (NON_INTR(sp) ? 1: 0);

	if (ncr->n_omsglen == 0 || ncr->n_omsgidx >= ncr->n_omsglen) {
		/*
		 * No message to send or previous message exhausted.
		 * Send a NO-OP message instead.
		 */
		printf("%s%d: unexpected message out phase for target %d\n",
			CNAME, CNUM, Tgt(sp));
		ncr->n_omsglen = 1;
		ncr->n_omsgidx = 0;
		ncr->n_cur_msgout[0] = MSG_NOP;
	}

	if (ncr->n_omsgidx == 0) {
		ncr->n_last_msgout = ncr->n_cur_msgout[0];
	}

	/*
	 * load data
	 */

	PRINTF3("ncr_sendmsg: sending msg byte %d = 0x%x of %d len msg\n",
		ncr->n_omsgidx, ncr->n_cur_msgout[ncr->n_omsgidx],
		ncr->n_omsglen);

	sbc->odr = ncr->n_cur_msgout[ncr->n_omsgidx++];
	sbc->icr |= NCR_ICR_DATA;

	if (ncr->n_omsgidx >= ncr->n_omsglen) {
		ncr->n_omsgidx = ncr->n_omsglen = 0;
		sbc->icr &= ~NCR_ICR_ATN;
	}

	/*
	 * complete req/ack handshake
	 */
	BLK_HIGH_INTR;
	sbc->icr |= NCR_ICR_ACK;
	if (!REQ_DROPPED(ncr)) {
		UNBLK_HIGH_INTR;
		sbc->tcr = TCR_UNSPECIFIED;
		sbc->icr = 0;
		return (ACTION_ABORT);
	}
	sbc->tcr = TCR_UNSPECIFIED;
	UNBLK_HIGH_INTR;

	if (nonintr == 0) {
		sbc->mr |= NCR_MR_DMA;
		junk = sbc->clr;
	}

	/*
	 * Deassert ACK*. Note that this uses a mask-equal-not instead of
	 * a straight clear because we may wish to leave ATN* asserted.
	 */

	junk = sbc->icr & (NCR_ICR_ACK|NCR_ICR_DATA|NCR_ICR_ATN);
	junk &= ~(NCR_ICR_ACK|NCR_ICR_DATA);
	sbc->icr = junk;
	UPDATE_STATE(ACTS_UNKNOWN);
	if (nonintr) {
		return (ACTION_CONTINUE);
	} else {
		ENABLE_DMA(ncr);
		return (ACTION_RETURN);
	}
}

static int
ncr_recvmsg(register struct ncr *ncr)
{
	auto u_char msgin;
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);

	/*
	 * Pick up a message byte from the SCSI bus. Delay giving an ack
	 * until we know if we can handle this message- that way we can
	 * assert the ATN line before the ACK so that the target knows we are
	 * gonna reject this message.
	 */

	if (ncr_xfrin_noack(ncr, sp, PHASE_MSG_IN, &msgin)) {
		return (ACTION_ABORT);
	} else
		ncr->n_last_msgin = msgin;

	if (msgin & MSG_IDENTIFY) {
		/*
		 * We shouldn't be getting an identify message here.
		 */

		printf("%s%d: out of sequence identify message: 0x%x\n",
			CNAME, CNUM, msgin);
		if (ncr_ACKmsg(ncr)) {
			return (ACTION_ABORT);
		}
		UPDATE_STATE(ACTS_UNKNOWN);
		return (ACTION_CONTINUE);
	}

	if (ncr_debug > 2) {
		printf("%s%d: msg=<%s>\n", CNAME, CNUM, scsi_mname(msgin));
	}

	switch (msgin) {
	case MSG_DISCONNECT:
		/*
		 * Important! Set state *before calling ncr_ACKmsg()
		 * because ncr_ACKmsg() bases some actions on the
		 * new state!
		 */

		UPDATE_STATE(ACTS_CLEARING_DISC);
		if (ncr_ACKmsg(ncr)) {
			return (ACTION_ABORT);
		}

		return (ACTION_SEARCH);

	case MSG_LINK_CMPLT:
	case MSG_LINK_CMPLT_FLAG:
	case MSG_COMMAND_COMPLETE:
	{
		/*
		 * Note well that we *do NOT* ACK the message if it
		 * is a LINKED COMMAND COMPLETE or LINKED COMMAND
		 * COMPLETE (with flag) message. We leave that for
		 * ncr_finish() to do once the target driver has
		 * given us the next command to send. This is so
		 * that the DMA engine can be set up for the new
		 * command prior to ACKing this message.
		 */

		/*
		 * Important! Set state *before calling ncr_ACKmsg()
		 * because ncr_ACKmsg() bases some actions on the
		 * new state!
		 */
		sp->cmd_pkt.pkt_reason = CMD_CMPLT;
		if (msgin == MSG_COMMAND_COMPLETE) {
			UPDATE_STATE(ACTS_CLEARING_DONE);
			if (ncr_ACKmsg(ncr)) {
				return (ACTION_ABORT);
			}
		} else {
			UPDATE_STATE(ACTS_UNKNOWN);
		}
		return (ACTION_FINISH);
	}
	case MSG_SAVE_DATA_PTR:
		sp->cmd_saved_data = sp->cmd_data;
		break;
	case MSG_RESTORE_PTRS:
		sp->cmd_cdbp = sp->cmd_pkt.pkt_cdbp;
		sp->cmd_scbp = sp->cmd_pkt.pkt_scbp;
		if (sp->cmd_data != sp->cmd_saved_data) {
			sp->cmd_flags |= CFLAG_CHKSEG;
			sp->cmd_data = sp->cmd_saved_data;
			/*
			 * Revalidate current window (if necessary)
			 */
			if (sp->cmd_dmawinsize) {
				if (ncr_set_window(ncr, sp, "RESTORE PTRS")) {
					return (ACTION_ABORT);
				}
			}
		}
		break;
	case MSG_NOP:
		break;
	default:
		if (ncr_NACKmsg(ncr)) {
			return (ACTION_ABORT);
		}
		UPDATE_STATE(ACTS_UNKNOWN);
		return (ACTION_CONTINUE);
	}

	if (ncr_ACKmsg(ncr)) {
		return (ACTION_ABORT);
	}
	UPDATE_STATE(ACTS_UNKNOWN);
	return (ACTION_CONTINUE);
}


/*
 * Enable DMA. If this is a non-interrupting command, await for DMA completion.
 */

static int
ncr_senddata(register struct ncr *ncr)
{
	struct scsi_cmd *sp = CURRENT_CMD(ncr);
	register int reqamt;

	if ((sp->cmd_flags & CFLAG_DMASEND) == 0) {
		printf("ncr%d: unwanted data out phase\n", CNUM);
		return (ACTION_ABORT);
	}

	reqamt = ncr->n_lastcount;

	if (reqamt == 0) {
		ncr_dump_datasegs(sp);
		printf("ncr%d: data out overrun\n", CNUM);
		return (ACTION_ABORT);
	} else if (reqamt == -1) {
		u_char byte;
		PRINTF1("ncr_senddata: byte xfr_addr= %x\n", sp->cmd_curaddr);
		if (ncr_fetchbyte(ncr, &byte) == FAILURE) {
			return (ACTION_ABORT);
		}

		if ((ncr_xfrout(ncr, PHASE_DATA_OUT, &byte, 1) == FAILURE)) {
			printf("ncr%d: ncr_senddata polled output failure\n",
			    CNUM);
			return (ACTION_ABORT);
		}
		sp->cmd_curaddr++;
		sp->cmd_data++;
		sp->cmd_dsegs.sd_cnt++;
		sp->cmd_pkt.pkt_state |= STATE_XFERRED_DATA;
		ncr_dma_setup(ncr);
		UPDATE_STATE(ACTS_UNKNOWN);
		return (ACTION_CONTINUE);
	}

	ncr_dma_enable(ncr, 0);	/* 0 = send */

	if (NON_INTR(sp)) {
		if (ncr_dma_wait(ncr)) {
			printf("%s%d: wait for dma timed out\n", CNAME, CNUM);
			return (ACTION_ABORT);
		}
		sp->cmd_pkt.pkt_state |= STATE_XFERRED_DATA;
		UPDATE_STATE(ACTS_UNKNOWN);
		return (ACTION_CONTINUE);
	} else {
		return (ACTION_RETURN);
	}

}

/*
 * Enable DMA. If this is a non-interrupting command, await for DMA completion.
 */

static int
ncr_recvdata(register struct ncr *ncr)
{
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);
	register reqamt;

	if (sp->cmd_flags & CFLAG_DMASEND) {
		printf("%s%d: unwanted data in phase\n", CNAME, CNUM);
		return (ACTION_ABORT);
	}

	reqamt = ncr->n_lastcount;

	if (reqamt == 0) {
		ncr_dump_datasegs(sp);
		printf("ncr%d: data in overrun\n", CNUM);
		return (ACTION_ABORT);
	} else if (reqamt == -1) {
		u_char byte;
		PRINTF1("ncr_recvddata: byte xfr_addr= %x\n", sp->cmd_curaddr);
		if (ncr_xfrin(ncr, sp, PHASE_DATA_IN, &byte, 1) == FAILURE) {
			printf("%s%d: ncr_recvdata polled input failure\n",
			    CNAME, CNUM);
			return (ACTION_ABORT);
		}
		if (ncr_flushbyte(ncr, sp->cmd_curaddr, byte) == FAILURE) {
			return (ACTION_ABORT);
		}
		sp->cmd_curaddr++;
		sp->cmd_data++;
		sp->cmd_dsegs.sd_cnt++;
		sp->cmd_pkt.pkt_state |= STATE_XFERRED_DATA;
		ncr_dma_setup(ncr);
		UPDATE_STATE(ACTS_UNKNOWN);
		return (ACTION_CONTINUE);
	}

	ncr_dma_enable(ncr, 1);	/* 1 = recv */

	if (NON_INTR(sp)) {
		if (ncr_dma_wait(ncr)) {
			printf("%s%d: wait for dma timed out\n", CNAME, CNUM);
			return (ACTION_ABORT);
		}
		sp->cmd_pkt.pkt_state |= STATE_XFERRED_DATA;
		UPDATE_STATE(ACTS_UNKNOWN);
		return (ACTION_CONTINUE);
	} else {
		return (ACTION_RETURN);
	}
}

static int
ncr_recvstatus(register struct ncr *ncr)
{
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);
	register int amt, maxsb, action;

	maxsb = (((u_int) sp->cmd_pkt.pkt_scbp) + sp->cmd_scblen) -
	    ((u_int) sp->cmd_scbp);
	if (maxsb <= 0) {
		printf("%s%d: status overrun\n", CNAME, CNUM);
		/*
		 * XXX: reset bus
		 */
		sp->cmd_pkt.pkt_reason = CMD_STS_OVR;
		return (ACTION_FINISH);
	}

	if (NON_INTR(sp)) {
		amt = ncr_xfrin(ncr, sp, PHASE_STATUS, sp->cmd_scbp, maxsb);
		if (amt <= 0) {
			return (ACTION_ABORT);
		}
		PRINTF3("ncr_recvstatus: status= %x\n", *sp->cmd_scbp);
		sp->cmd_scbp += amt;
		UPDATE_STATE(ACTS_UNKNOWN);
		action = ACTION_CONTINUE;
	} else {
		if (ncr_xfrin_noack(ncr, sp, PHASE_STATUS, sp->cmd_scbp)) {
			return (ACTION_ABORT);
		}
		PRINTF3("ncr_recvstatus: status= %x\n", *sp->cmd_scbp);
		sp->cmd_scbp += 1;		/* 1091189: amt --> 1 */
		N_SBC->tcr = TCR_UNSPECIFIED;
		amt = N_SBC->clr;
		N_SBC->icr |= NCR_ICR_ACK;
		if (!REQ_DROPPED(ncr)) {
			return (ACTION_ABORT);
		}
		N_SBC->mr |= NCR_MR_DMA;
		N_SBC->icr = 0;
		ENABLE_DMA(ncr);
		action = ACTION_RETURN;
	}
	sp->cmd_pkt.pkt_state |= STATE_GOT_STATUS;
	UPDATE_STATE(ACTS_UNKNOWN);
	return (action);
}


/*
 * Utility Functions
 */

/*
 * Perform Arbitration and SCSI selection
 *
 *	input:  (struct ncr) *ncr= pointer to a ncr software structure;
 *	return: (int) SEL_TRUE		= target arb/selected successful;
 *		(int) SEL_ARBFAIL	= Arbitration failed (bus busy)
 *		(int) SEL_FALSE		= selection timed out
 *		(int) SEL_RESEL		= Reselection attempt underway
 */

/* local defines */
#define	WON_ARB	(((sbc->icr & NCR_ICR_LA) == 0) && \
	(((u_int) (CDR & ~binid)) < (u_int) binid))

static int
ncr_select(struct ncr *ncr)
{
	NCR_HIGH_IDECL;
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);
	register volatile struct ncrsbc *sbc = ncr->n_sbc;
	register int rval, retry;
	u_char uctmp;
	u_char binid = NUM_TO_BIT(ncr->n_id);


	PRINTF3("ncr_select: ready to do ARB/SEL for tgt %d\n", Tgt(sp));

	if (INTPENDING(ncr)) {
		if (ncr_debug > 2) {
			ncr_printstate(ncr);
			printf("ncr_select: preempting\n");
		}
		return (SEL_RESEL);
	}

	DISABLE_DMA(ncr);

	if (ncr_debug > 1)
		ncr_printstate(ncr);


	/*
	 * Attempt to arbitrate for the SCSI bus.
	 *
	 * It seems that the tcr must be 0 for arbitration to work.
	 */

	sbc->tcr = 0;
	sbc->mr &= ~NCR_MR_ARB;    /* turn off arb */
	sbc->icr = 0;
	sbc->odr = binid;

	UPDATE_STATE(STATE_ARBITRATION);

	rval = SEL_ARBFAIL;

	for (retry = 0; retry < NCR_ARB_RETRIES; retry++) {
		/* wait for scsi bus to become free */
		if (ncr_sbcwait((u_char *)&CBSR, NCR_CBSR_BSY,
		    NCR_WAIT_COUNT, 0)) {
			PRINTF3("%s%d: scsi bus continuously busy, cbsr= %x\n",
			    CNAME, CNUM, sbc->cbsr);
			break;
		}
		PRINTF3("ncr_select: bus free, current cbsr= %x\n", CBSR);

		/*
		 * If the bus is now FREE, turn on ARBitration
		 */

		sbc->mr |= NCR_MR_ARB;

		/*
		 * wait for ncr to begin arbitration by calling ncr_sbcwait().
		 * If failed due to reselection, turn off ARB, preempt the job
		 * and return SEL_RESEL.
		 */
		if (ncr_sbcwait((u_char *)&sbc->icr, NCR_ICR_AIP,
		    NCR_ARB_WAIT, 1)) {
			/*
			 * sbc may never begin arbitration
			 * due to a target reselecting us.
			 * (time critical)
			 */
			BLK_HIGH_INTR;
			sbc->mr &= ~NCR_MR_ARB;	/* turn off arb */
			if (RESELECTING(CBSR, CDR, binid)) {
				UNBLK_HIGH_INTR;
				rval = SEL_RESEL;
				break;
			}
			UNBLK_HIGH_INTR;
			/*
			 * Hmm- I've seen this actually get set on some 3/50s
			 */
			PRINTF3("%s%d: AIP never set, cbsr= 0x%x\n", CNAME,
			    CNUM, CBSR);
		} else {
			/*
			 * check to see if we won arbitration
			 * (time critical)
			 */
			BLK_HIGH_INTR;
			drv_usecwait(NCR_ARBITRATION_DELAY);
			if (WON_ARB) {
				UNBLK_HIGH_INTR;
				rval = SEL_FALSE;
				break;
			}
			UNBLK_HIGH_INTR;
		}
		PRINTF3("ncr_select: lost_arb, current cbsr= %x\n", CBSR);
		/*
		 * Lost arbitration. Maybe try again in a nano or two
		 * (time critical)
		 */
		BLK_HIGH_INTR;
		sbc->mr &= ~NCR_MR_ARB;	/* turn off ARB */
		if (RESELECTING(CBSR, CDR, binid)) {
			UNBLK_HIGH_INTR;
			rval = SEL_RESEL;
			break;
		}
		UNBLK_HIGH_INTR;
	}

	if (rval == SEL_ARBFAIL) {
		/*
		 * FAILED ARBITRATION even with retries.
		 * This shouldn't happen since we (usually)
		 * have the highest priority id on the scsi bus.
		 */

		sbc->icr = 0;
		if (scsi_options & SCSI_OPTIONS_PARITY) {
			sbc->mr = NCR_MR_EPC;
		} else {
			sbc->mr = 0;
		}
		uctmp = sbc->clr;

		/*
		 * After exhausting retries, and if there
		 * are any outstanding disconnected cmds,
		 * enable reselection attempts from them
		 * and return SEL_FALSE.
		 *
		 */

		PRINTF3("ncr_select: failed arb, target= %x\n", Tgt(sp));
		if (ncr->n_ndisc > 0) {
			sbc->ser = 0;
			sbc->ser = binid;
			ENABLE_DMA(ncr);
		}
		return (rval);
	} else if (rval == SEL_RESEL) {
		if (scsi_options & SCSI_OPTIONS_PARITY) {
			sbc->mr = NCR_MR_EPC;
		} else {
			sbc->mr = 0;
		}
		ENABLE_DMA(ncr);
		return (rval);
	}

	sp->cmd_pkt.pkt_reason = CMD_INCOMPLETE;


	UPDATE_STATE(STATE_SELECTING);

	/*
	 * calculate binary of target_id and host_id
	 * and or them together to send out.
	 */

	uctmp = ((1 << Tgt(sp)) | binid);
	sbc->odr = uctmp;


	uctmp = (NCR_ICR_SEL | NCR_ICR_BUSY | NCR_ICR_DATA);
	if ((scsi_options & SCSI_OPTIONS_DR) &&
	    ((sp->cmd_pkt.pkt_flags & (FLAG_NODISCON|FLAG_NOINTR)) == 0) &&
	    ((ncr->n_nodisc & (1<<Tgt(sp))) == 0)) {
		ncr->n_cur_msgout[0] = MSG_DR_IDENTIFY | Lun(sp);
		ncr->n_omsglen = 1;
		ncr->n_omsgidx = 0;
		uctmp |= NCR_ICR_ATN;
	}

	sbc->icr = uctmp;		/* start selection */

	sbc->mr &= ~NCR_MR_ARB;		/* turn off arb */

	drv_usecwait(NCR_BUS_CLEAR_DELAY + NCR_BUS_SETTLE_DELAY);

	/*
	 * Drop our assertion of BSY* (left on during arbitration)
	 */

	sbc->icr &= ~NCR_ICR_BUSY;

	drv_usecwait(1);

	/*
	 * Apparently the ncr chip lies about actually
	 * getting the bus, hence we'll check for a reselection
	 * attempt here.
	 */

	for (retry = 0; retry < NCR_SHORT_WAIT; retry++) {
		/*
		 * If BSY asserted, then the target has selected.
		 * If not, check for a reselection attempt.
		 */
		if (CBSR & NCR_CBSR_BSY) {
			PRINTF3("ncr_select: cbsr= 0x%b\n", CBSR, cbsr_bits);
			break;
		} else if (RESELECTING(CBSR, CDR, binid)) {
			if (scsi_options & SCSI_OPTIONS_PARITY) {
				sbc->mr = NCR_MR_EPC;
			} else {
				sbc->mr = 0;
			}
			sbc->icr = 0;
			ENABLE_DMA(ncr);
			PRINTF3("ncr_select: arb_won, but preempt again\n");
			return (SEL_RESEL);
		}
		drv_usecwait(10);
	}


	/*
	 * Say that we got the bus here rather than earlier.
	 */

	sp->cmd_pkt.pkt_state |= STATE_GOT_BUS;

	if (retry >= NCR_SHORT_WAIT) {
		/*
		 * Target failed selection
		 */
		sbc->icr = 0;
		if (scsi_options & SCSI_OPTIONS_PARITY) {
			sbc->mr = NCR_MR_EPC;
		} else {
			sbc->mr = 0;
		}
		uctmp = sbc->clr;

		/*
		 * if failed to select target, enable disconnect,
		 * if any discon_job pending
		 */

		if (ncr->n_ndisc > 0) {
			sbc->ser = 0;
			sbc->ser = binid;
			ENABLE_DMA(ncr);
		}
		sp->cmd_pkt.pkt_reason = CMD_INCOMPLETE;
		return (SEL_FALSE);
	}

	/*
	 * Drop SEL* and DATA*
	 */

	sbc->tcr = TCR_UNSPECIFIED;
	if (!NON_INTR(sp)) {
		/*
		 * Time critical
		 */
		BLK_HIGH_INTR;
		if (sp->cmd_flags & CFLAG_DMAVALID) {
			ncr_dma_setup(ncr);
		}
		sbc->mr |= NCR_MR_DMA;
		sbc->icr &= ~(NCR_ICR_SEL | NCR_ICR_DATA);
		UNBLK_HIGH_INTR;
		ENABLE_DMA(ncr);
	} else {
		if (sp->cmd_flags & CFLAG_DMAVALID) {
			ncr_dma_setup(ncr);
		}
		sbc->icr &= ~(NCR_ICR_SEL | NCR_ICR_DATA);
	}
	/*
	 * Flag that we have selected the target...
	 */

	sp->cmd_pkt.pkt_state |= STATE_GOT_TARGET;

	UPDATE_STATE(STATE_SELECTED);
	PRINTF3("ncr_select: select ok\n");
	return (SEL_TRUE);
}


static void
ncr_preempt(register struct ncr *ncr)
{
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);
	UPDATE_STATE(STATE_FREE);
	UPDATE_SLOT(UNDEFINED);
	ncr->n_preempt++;
	if (NON_INTR(sp) && INTPENDING(ncr)) {
		ncr_svc(ncr);
	}
}

static void
ncr_disconnect(struct ncr *ncr)
{
	struct scsi_cmd *sp = CURRENT_CMD(ncr);

	sp->cmd_pkt.pkt_statistics |= STAT_DISCON;
	sp->cmd_flags |= CFLAG_CMDDISC;
	ncr->n_ndisc++;
	ncr->n_last_slot = ncr->n_cur_slot;
	ncr->n_cur_slot = UNDEFINED;
	ncr->n_lastcount = 0;
	ncr->n_disconnects++;
	ncr->n_omsglen = ncr->n_omsgidx = 0;
	ncr->n_last_msgin = 0xff;
	UPDATE_STATE(STATE_FREE);
	if (NON_INTR(sp) == 0) {
		short nextslot;
		nextslot = Tgt(sp) + NLUNS_PER_TARGET;
		if (nextslot >= NLUNS_PER_TARGET*NTARGETS)
			nextslot = 0;
		(void) ncr_ustart(ncr, nextslot);
	}
}

/*
 * Programmed I/O Routines
 */

/*
 * Pick up incoming data byte(s)
 */

static int
ncr_xfrin(register struct ncr *ncr, struct scsi_cmd *sp, int phase,
    register u_char *datap, register int amt)
{
	NCR_HIGH_IDECL;
	register int i;
	register volatile struct ncrsbc *sbc = N_SBC;

	PRINTF3("ncr_xfrin: amt= %x\n", amt);

	/*
	 * Get data from the scsi bus.
	 */
	if ((sbc->cbsr & NCR_CBSR_REQ) == 0) {
		sbc->tcr = TCR_UNSPECIFIED;
		EPRINTF("ncr_xfrin: bad REQ, cbsr= 0x%b\n", CBSR, cbsr_bits);
		return (FAILURE);
	} else if ((sbc->bsr & NCR_BSR_PMTCH) == 0) {
		EPRINTF("ncr_xfrin: bad bus match, bsr= %x\n", sbc->bsr);
		if ((sbc->cbsr & (0x7<<2)) == phase)
			sbc->tcr = phase >> 2;
		else
			sbc->tcr = TCR_UNSPECIFIED;
	}

	for (i = 0; i < amt; i++) {
		/* wait for target request */
		if (i && !REQ_ASSERTED(ncr)) {
			sbc->tcr = TCR_UNSPECIFIED;
			return (FAILURE);
		}
		if (i && (sbc->bsr & NCR_BSR_PMTCH) == 0) {
			/*
			 * phase is not matched. Check to
			 * see whether we should match it
			 */

			if ((sbc->cbsr & (0x7<<2)) == phase)
				sbc->tcr = phase >> 2;
			else
				break;
		}
		/* grab data and complete req/ack handshake */
		*datap++ = (u_char) sbc->cdr;
		if (scsi_options & SCSI_OPTIONS_PARITY) {
			if (((sp != (struct scsi_cmd *)0) &&
			    ((sp->cmd_pkt.pkt_flags & FLAG_NOPARITY) == 0)) ||
			    (sp == (struct scsi_cmd *)0)) {
				if (N_SBC->bsr & NCR_BSR_PERR) {
					ncr_printstate(ncr);
					cmn_err(CE_PANIC,
					    "ncr_xfrin: parity error");
				}
			}
		}
		BLK_HIGH_INTR;
		sbc->icr |= NCR_ICR_ACK;
		sbc->tcr = TCR_UNSPECIFIED;
		UNBLK_HIGH_INTR;
		if (!REQ_DROPPED(ncr)) {
			EPRINTF("ncr_xfrin: REQ not dropped, cbsr=0x%b\n",
				CBSR, cbsr_bits);
			sbc->icr = 0;
			return (FAILURE);
		}
		/* Drop acknowledgement...  */
		sbc->icr = 0;
	}
	sbc->tcr = TCR_UNSPECIFIED;
	sbc->icr = 0;		/* duplicate */
	PRINTF3("ncr_xfrin: picked up %d\n", i);
	return (i);
}

static int
ncr_xfrin_noack(register struct ncr *ncr, struct scsi_cmd *sp,
    int phase, u_char *datap)
{
	register u_char indata;

	PRINTF3("ncr_xfrin_noack: phase= %x\n", phase);
	if ((N_SBC->cbsr & NCR_CBSR_REQ) == 0) {
		N_SBC->tcr = TCR_UNSPECIFIED;
		EPRINTF("ncr_xfrin_noack: bad REQ, cbsr=0x%b\n",
			CBSR, cbsr_bits);
		return (FAILURE);
	} else if ((N_SBC->bsr & NCR_BSR_PMTCH) == 0) {
		N_SBC->tcr = TCR_UNSPECIFIED;
		return (FAILURE);
	}

	/*
	 * Get data from the scsi bus, but NO acknowlegment; if current phase
	 * is MESSAGE_IN, clear TCR
	 */
	indata = N_SBC->cdr;
	if (scsi_options & SCSI_OPTIONS_PARITY) {
		if (((sp != (struct scsi_cmd *)0) &&
		    ((sp->cmd_pkt.pkt_flags & FLAG_NOPARITY) == 0)) ||
		    (sp == (struct scsi_cmd *)0)) {
			if (N_SBC->bsr & NCR_BSR_PERR) {
				ncr_printstate(ncr);
				cmn_err(CE_PANIC,
				    "ncr_xfrin_noack: parity error");
			}
		}
	}
	if (phase == PHASE_MSG_IN) {
		/*
		 * We've picked up the message byte, but not acknowledged it
		 * yet. Perhaps here is the best place to clear the tcr
		 * register in the case that a COMMAND COMPLETE or a
		 * DISCONNECT message was just sent and the target is gonna
		 * clear the SCSI bus just as soon as it is acknowledged. We
		 * have to have tcr 0 to recognize other targets then
		 * attempting to reselect us...
		 */
		N_SBC->tcr = 0;
	}
	*datap = indata;
	PRINTF3("ncr_xfrin_noack: indata= %x\n", indata);
	return (SUCCESS);
}

static int
ncr_xfrout(register struct ncr *ncr, int phase,
    register u_char *datap, register int amt)
{
	NCR_HIGH_IDECL;
	register int i;
	register volatile struct ncrsbc *sbc = N_SBC;

	PRINTF3("ncr_xfrout: amt= %x\n", amt);
	/*
	 * Send data to the scsi bus.
	 */
	if ((sbc->cbsr & NCR_CBSR_REQ) == 0) {
		sbc->tcr = TCR_UNSPECIFIED;
		EPRINTF("ncr_xfrout: bad REQ, cbsr= 0x%b\n", CBSR, cbsr_bits);
		return (FAILURE);
	} else if ((sbc->bsr & NCR_BSR_PMTCH) == 0) {
		PRINTF3("ncr_xfrout: bad bus match, bsr= %x, cbsr= %x\n",
			sbc->bsr, sbc->cbsr);
		if ((sbc->cbsr & (0x7<<2)) == phase)
			sbc->tcr = phase >> 2;
		else {
			PRINTF3("ncr_xfrout: can not match phase= %x\n", phase);
			sbc->tcr = TCR_UNSPECIFIED;
		}
	}

	for (i = 0; i < amt; i++) {
		/* wait for target request */
		if (i && !REQ_ASSERTED(ncr)) {
			sbc->tcr = TCR_UNSPECIFIED;
			PRINTF3("ncr_xfrout: req not asserted ,i= %x\n", i);
			return (FAILURE);
		}
		if (i && (sbc->bsr & NCR_BSR_PMTCH) == 0) {
			/*
			 * phase is not matched. Check to
			 * see whether we should match it
			 */

			if ((sbc->cbsr & (0x7<<2)) == phase) {
				sbc->tcr = phase >> 2;
			} else if (ncr_debug > 2)  {
				static char *urp =
		"ncr_xfrout: phased not matched, cbsr=0x%x, phase=0x%x\n";
				printf(urp, sbc->cbsr, phase);
				break;
			}
		}
		/* send data and complete req/ack handshake */
		sbc->odr = *datap;		/* load data */
		sbc->icr |= NCR_ICR_DATA;
		BLK_HIGH_INTR;
		sbc->icr |= NCR_ICR_ACK;
		if (!REQ_DROPPED(ncr)) {
			UNBLK_HIGH_INTR;
			EPRINTF("ncr_xfrout: REQ not dropped, cbsr=0x%b\n",
			    CBSR, cbsr_bits);
			sbc->tcr = TCR_UNSPECIFIED;
			sbc->icr = 0;
			return (FAILURE);
		}
		sbc->tcr = TCR_UNSPECIFIED;
		UNBLK_HIGH_INTR;
		/* Drop acknowledgement...  */
		sbc->icr = 0;
		datap++;
	}
	sbc->tcr = TCR_UNSPECIFIED;
	sbc->icr = 0;		/* duplicate */
	PRINTF3("ncr_xfrout: send out %d byte(s)\n", i);
	return (i);
}

/*
 * Dma Subroutines
 */

static int
ncr_cobra_dma_chkerr(register struct ncr *ncr, u_int bcr)
{
	struct scsi_cmd *sp = CURRENT_CMD(ncr);
	u_int csr;

	csr = GET_CSR(ncr);
	PRINTF3("ncr_dma_chkerr: csr= %x, bcr= %x\n", csr, bcr);

	/* check any abormal DMA conditions */
	if (csr & NCR_CSR_DMA_CONFLICT) {
		EPRINTF("ncr_cobra_dma_cleanup: dma conflict\n");
		return (FAILURE);
	} else if (csr & NCR_CSR_DMA_BUS_ERR) {
		/*
		 * Note from sw.c:
		 *
		 * Early Cobra units have a faulty gate array. It can cause an
		 * illegal memory access if full page DMA is being used.  For
		 * less than a full page, no problem.  This problem typically
		 * shows up when dumping core (in polled mode) where the last
		 * page of DVMA was being used.
		 *
		 * What this means is that if you camp on the dma gate array
		 * csr in polled mode, the ncr chip may attempt to prefetch
		 * across a page boundary into an invalid page, causing a
		 * spurious DMA error.
		 */
		if (!(bcr > 2 && NON_INTR(sp))) {
			EPRINTF("ncr_cobra_dma_chkerr: dma bus error\n");
			return (FAILURE);
		}
	}
	return (SUCCESS);
}

static int
ncr_cobra_dma_recv(register struct ncr *ncr)
{
	register u_long addr, round, bpr;

	/*
	 * if partial bytes left on the dma longword transfers, manually take
	 * care of this and return back how many bytes moved
	 */
	/*
	 * Grabs last few bytes which may not have been dma'd. Worst case is
	 * when longword dma transfers are being done and there are 3 bytes
	 * leftover.
	 *
	 * Note: limiting dma address to 20 bits (1 mb).
	 * MJ: in theory, the "obio" parent will do the correct
	 * MJ: translations for us.
	 */
	addr = GET_DMA_ADDR(ncr);
	round = addr & ~3;
	bpr = GET_BPR(ncr);
	switch (addr & 0x3) {
	case 3:
		if (ncr_flushbyte(ncr, (int)round+2, (u_char)((bpr>>8)&0xff)))
			return (FAILURE);
		/* FALLTHROUGH */
	case 2:
		if (ncr_flushbyte(ncr, (int)round+1, (u_char)((bpr>>16)&0xff)))
			return (FAILURE);
		/* FALLTHROUGH */
	case 1:
		if (ncr_flushbyte(ncr, (int)round, (u_char) (bpr>>24)))
			return (FAILURE);
		break;
	default:
		break;
	}
	ncr->n_lastbcr += (addr & 0x3);
	return (SUCCESS);
}

static int
ncr_cobra_dma_cleanup(register struct ncr *ncr)
{
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);
	register u_long amt, reqamt;

	DISABLE_DMA(ncr);

	amt = GET_DMA_ADDR(ncr);
	reqamt = ncr->n_lastcount;

	/*
	 * Now try and figure out how much actually transferred
	 */

	ncr->n_lastbcr = reqamt - ((amt & 0xfffff) -
	    (unsigned int) sp->cmd_curaddr & 0xfffff);
	PRINTF3("ncr_dma_cleanup: count= %x, data= %x, amt= %x, bcr= %x\n",
		reqamt, sp->cmd_curaddr, amt, ncr->n_lastbcr);

	if ((ncr->n_lastbcr != reqamt) && (!(N_SBC->tcr & NCR_TCR_LAST)) &&
	    (sp->cmd_flags & CFLAG_DMASEND)) {
		ncr->n_lastbcr++;
	}

	/*
	 * Now check for dma related errors
	 */

	if (ncr_cobra_dma_chkerr(ncr, (u_int)ncr->n_lastbcr)) {
		sp->cmd_pkt.pkt_reason = CMD_DMA_DERR;
		return (FAILURE);
	}

	/*
	 * okay, now figure out an adjustment for bytes
	 * left over in the or a byte pack register.
	 */

	if ((sp->cmd_flags & CFLAG_DMASEND) == 0) {
		if (ncr_cobra_dma_recv(ncr))
			return (FAILURE);
	}

	if (ncr->n_lastbcr < 0) {
		EPRINTF("ncr_dma_cleanup: dma overrun by %d bytes\n",
		    -ncr->n_lastbcr);
		ncr->n_lastbcr = 0;
	}

	/*
	 * Shut off dma engine
	 */

	SET_DMA_ADDR(ncr, 0);
	SET_DMA_COUNT(ncr, 0);

	/*
	 * mask off the extended lastbcr (partial word) for an
	 * odd-byte transfer count, done after dma_recv(),
	 * so it would not account for during "resid" update
	 */
	if (reqamt & 0x3) {
		PRINTF2("ncr_cobra_dma_cleanup: reqamt= %x, bcr= %x\n",
		    reqamt, ncr->n_lastbcr);
		ncr->n_lastbcr &= ~0x3;
	}

	/*
	 * And return result of common cleanup routine
	 */

	return (ncr_dma_cleanup(ncr));
}

static int
ncr_vme_dma_recv(register struct ncr *ncr)
{
	register amt, csr;
	register u_short bprmsw, bprlsw;
	u_long laddr;

	/*
	 * Grabs last few bytes which may not have been dma'd.  Worst
	 * case is when longword dma transfers are being done and there
	 * are 3 bytes leftover.  If BPCON bit is set then longword dma
	 * was being done, otherwise word dma was being done.
	 */

	csr = GET_CSR(ncr);
	if ((amt = NCR_CSR_LOB_CNT(csr)) == 0) {
		return (SUCCESS);
	}
	laddr = ncr->n_lastdma + (ncr->n_lastcount - ncr->n_lastbcr);

	PRINTF3("ncr_vme_dma_recv: flushing %d bytes to 0x%x\n", amt, laddr);

	/*
	 * It *may* be that the order that
	 * the byte pack register is read
	 * is significant.
	 *
	 */

	bprmsw = N_CTL->bpr.msw;
	bprlsw = N_CTL->bpr.lsw;
	if (csr & NCR_CSR_BPCON) {
		return (ncr_flushbyte(ncr,
		    (int)laddr-1, (u_char)Hibyte(bprlsw)));
	}
	switch (amt) {
	case 3:
		if (ncr_flushbyte(ncr, (int)laddr-3, (u_char) Hibyte(bprmsw)))
			return (FAILURE);
		if (ncr_flushbyte(ncr, (int)laddr-2, (u_char) Lobyte(bprmsw)))
			return (FAILURE);
		if (ncr_flushbyte(ncr, (int)laddr-1, (u_char) Hibyte(bprlsw)))
			return (FAILURE);
		break;
	case 2:
		if (ncr_flushbyte(ncr, (int)laddr-2, (u_char) Hibyte(bprmsw)))
			return (FAILURE);
		if (ncr_flushbyte(ncr, (int)laddr-1, (u_char) Lobyte(bprmsw)))
			return (FAILURE);
		break;
	case 1:
		if (ncr_flushbyte(ncr, (int)laddr-1, (u_char) Hibyte(bprmsw)))
			return (FAILURE);
		break;
	}
	return (SUCCESS);
}

static int
ncr_vme_dma_cleanup(register struct ncr *ncr)
{
	struct scsi_cmd *sp = CURRENT_CMD(ncr);
	register u_int bcr, reqamt, csr;

	DISABLE_DMA(ncr);

	csr = GET_CSR(ncr);
	if (csr & NCR_CSR_DMA_BUS_ERR) {
		printf("ncr%d: bus error during dma %s\n", CNUM,
		    (sp->cmd_flags & CFLAG_DMASEND)? "transmit" : "receive");
		return (FAILURE);
	}

	/*
	 * Now try and figure out how much actually transferred
	 */

	bcr = GET_BCR(ncr);
	reqamt = ncr->n_lastcount;


	/*
	 * bcr does not reflect how many bytes were actually
	 * transferred for VME.
	 *
	 * SCSI-3 VME interface is a little funny on writes:
	 * if we have a disconnect, the dma has overshot by
	 * one byte and needs to be incremented.  This is
	 * true if we have not transferred either all data
	 * or no data.
	 */

	if ((sp->cmd_flags & CFLAG_DMASEND) && bcr != reqamt && bcr) {
		if (ncr->n_lastbcr != 0)
			bcr = ncr->n_lastbcr + 1;
		else
			bcr++;
	} else if ((sp->cmd_flags & CFLAG_DMASEND) == 0) {
		bcr = ncr->n_lastbcr;
		if (ncr_vme_dma_recv(ncr)) {
			return (FAILURE);
		}
	}

	/*
	 * rewrite last bcr count- it might have been changed by circumstances.
	 */

	ncr->n_lastbcr = bcr;

	/*
	 * Shut off dma engine
	 */

	SET_DMA_ADDR(ncr, 0);
	SET_DMA_COUNT(ncr, 0);
	SET_BCR(ncr, 0);

	SET_CSR(ncr, (GET_CSR(ncr)) & ~NCR_CSR_SEND);

	/*
	 * reset fifo
	 */

	SET_CSR(ncr, ((GET_CSR(ncr)) & ~NCR_CSR_FIFO_RES));
	SET_CSR(ncr, ((GET_CSR(ncr)) | NCR_CSR_FIFO_RES));

	/*
	 * And return result of common cleanup routine
	 */

	PRINTF1("ncr_vme_dma_cleanup: bcr 0x%x reqamt 0x%x\n", bcr, reqamt);
	return (ncr_dma_cleanup(ncr));
}


/*
 * Cleanup operations, where the dma engine doesn't do it all.
 * This method might not work for all types of I/O caches.
 */

#define	HANDLE	sp->cmd_dmahandle

static int
ncr_flushbyte(struct ncr *ncr, u_int addr, u_char byte)
{
	struct scsi_cmd *sp = CURRENT_CMD(ncr);
	off_t off;
	caddr_t kvaddr;

	off = (off_t)(addr - sp->cmd_dmacookie.dmac_address);
	PRINTF1("ncr_flushbyte: off %x\n", off);
	if (ddi_dma_sync(HANDLE, off, 1, DDI_DMA_SYNC_FORCPU) != DDI_SUCCESS) {
		printf("ncr%d: ncr_flushbyte- ddi_dma_sync failure\n", CNUM);
		return (FAILURE);
	}
	if (ddi_dma_kvaddrp(HANDLE, off, 1, &kvaddr) != DDI_SUCCESS) {
		printf("ncr%d: ncr_flushbyte- ddi_dma_kvaddr failure\n", CNUM);
		printf("\tdata off 0x%x cookie off 0x%x\n", sp->cmd_data, off);
		return (FAILURE);
	}
	PRINTF1("ncr_flushbyte: kvaddr 0x%x\n", kvaddr);
	*kvaddr = byte;
	return (SUCCESS);
}

static int
ncr_fetchbyte(struct ncr *ncr, u_char *datap)
{
	struct scsi_cmd *sp = CURRENT_CMD(ncr);
	off_t off;
	caddr_t kvaddr;

	off = (off_t)(sp->cmd_curaddr - sp->cmd_dmacookie.dmac_address);
	PRINTF1("ncr_fetchbyte: off %x\n", off);
	if (ddi_dma_kvaddrp(HANDLE, off, 1, &kvaddr) != DDI_SUCCESS) {
		printf("ncr%d: ncr_fetchbyte- kvaddr failure\n", CNUM);
		return (FAILURE);
	}
	PRINTF1("ncr_fetchbyte: kvaddr 0x%x\n", kvaddr);
	*datap = (u_char) *kvaddr;
	return (SUCCESS);
}

/*
 * Common routine to enable the SBC for a dma operation.
 */

static void
ncr_dma_enable(register struct ncr *ncr, int direction)
{
	NCR_HIGH_IDECL;
	u_char junk;

	BLK_HIGH_INTR;	/* (possibly) time critical */
	if (ncr->n_type == IS_SCSI3) {
		/*
		 * Stuff count registers
		 */
		SET_DMA_COUNT(ncr, ncr->n_lastcount);
		SET_BCR(ncr, ncr->n_lastcount);
	}


	if (direction) {	/* 1 = recv */
		N_SBC->tcr = TCR_DATA_IN;
		junk = N_SBC->clr;
		N_SBC->mr |= NCR_MR_DMA;
		N_SBC->ircv = 0;
	} else {		/* 0 = send */
		N_SBC->tcr = TCR_DATA_OUT;
		junk = N_SBC->clr;	/* clear intr */
		N_SBC->icr = NCR_ICR_DATA;
		N_SBC->mr |= NCR_MR_DMA;
		N_SBC->send = 0;
	}
	UNBLK_HIGH_INTR;
	ENABLE_DMA(ncr);
#ifdef lint
	junk = junk;
#endif
}

/*
 * Common dma setup routine for all except 3/50, 3/60 architectures
 */

static void
ncr_dma_setup(struct ncr *ncr)
{
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);
	register u_int reqamt, align;
	u_int dmaaddr;
	u_char type = ncr->n_type;

	if ((sp->cmd_flags & CFLAG_DMAVALID) == 0) {
		cmn_err(CE_PANIC,
		    "ncr_dma_setup: unwanted data phase");
		/* NOTREACHED */
	}

	/*
	 * Make sure we don't overrun our data.
	 * If we've already transferred all data
	 * possible, then it is an error to try
	 * and go further. If we ever do full
	 * parity recovery in this driver (unlikely)
	 * this will have to be rethought.
	 *
	 * Remember- if you remove the following
	 * test, the stuff below bout CHKSEG is
	 * bogus in the case of a write to a device
	 * where the last portion is not 'latched'
	 * by the target sending a SAVE DATA POINTER
	 * message prior to a disconnect and then
	 * a reconnect to send the COMMAND COMPLETE
	 * message.
	 */
	if (sp->cmd_dsegs.sd_cnt >= sp->cmd_dmacount) {
		/*
		 * Setting lastcount to zero means that
		 * if we go to DATA IN or DATA OUT phase
		 * then ncr_{recv,send}data will se this
		 * as a data overrun condition.
		 */
		ncr->n_lastcount = 0;
		return;
	}

	/*
	 * Make sure our counts are in good shape
	 */
	if (sp->cmd_flags & CFLAG_CHKSEG) {
		if (sp->cmd_data > sp->cmd_dsegs.sd_cnt) {
			/*
			 * We don't support multiple segments yet, so
			 * this is a 'cannot happen'.
			 */
			cmn_err(CE_PANIC, "ncr%d: no multiple segs!", CNUM);
			/* NOTREACHED */
		} else {
			/*
			 * Make sure that the current segment's count
			 * field is no larger than the data offset
			 */
			sp->cmd_dsegs.sd_cnt = sp->cmd_data;
		}
		sp->cmd_flags ^= CFLAG_CHKSEG;
	}

	/*
	 * Figure out how much of the currently mapped
	 * data segment is valid for us. If none of it
	 * is, then set things up so that if we shift
	 * to a data phase, we'll die.
	 */

	/*
	 * Because SCSI is SCSI, the current DMA pointer has got to be
	 * greater than or equal to our DMA base address. All other cases
	 * that might have affected this always set curaddr to be >=
	 * to the DMA base address.
	 */
	ASSERT(sp->cmd_curaddr >= sp->cmd_dmacookie.dmac_address);
	reqamt = sp->cmd_dmacookie.dmac_address + sp->cmd_dmacookie.dmac_size;

	if ((sp->cmd_data >= sp->cmd_dmacount) ||
	    (sp->cmd_curaddr >= reqamt && sp->cmd_dmawinsize == 0)) {
		ncr->n_lastcount = 0;
		return;
	} else if (sp->cmd_curaddr >= reqamt && sp->cmd_dmawinsize) {
		u_long off;
		u_int len;

		if (ddi_dma_curwin(sp->Handle, (off_t *)&off, &len)) {
			ncr->n_lastcount = 0;
			return;
		}
		if (sp->cmd_data >= off + len || sp->cmd_data < off) {
			/*
			 * We can assume power of two for len.
			 */
			off = (sp->cmd_data & ~(len - 1));
			if (ddi_dma_movwin(sp->Handle, (off_t *)&off,
			    &len, &sp->Cookie)) {
				cmn_err(CE_WARN,
				    "ncr%d: cannot set new dma window", CNUM);
				ncr->n_lastcount = 0;
				return;
			}
		}
		sp->cmd_dmawinsize = len;
		sp->cmd_curaddr = sp->Cookie.dmac_address;
		reqamt = sp->cmd_curaddr + sp->Cookie.dmac_size;
		PRINTF1("Window shift to %x %x\n",
		    sp->Cookie.dmac_address, sp->Cookie.dmac_size);
	}

	/*
	 * Now that we've gone to all the work to make sure
	 * our data address is correct, blow it all off if
	 * we're doing things 'by hand'. This wasn't a waste
	 * because we still get a window shift out of all
	 * that work.
	 */

	if (sp->cmd_flags & CFLAG_NODMA) {
		ncr->n_lastcount = (u_long) -1;
		return;
	}

	/*
	 * If we don't meet our alignment requirements,
	 * we set things up such that we do it "by hand".
	 */
	if (type == IS_COBRA) {
		align = 0x3;
	} else {
		align = 0x1;
	}

	if (sp->cmd_curaddr & align) {
		PRINTF1("ncr_dma_setup: misaligned data 0%x align 0x%x\n",
		    sp->cmd_curaddr, align);
		ncr->n_lastcount = (u_long) -1;
		return;
	}


	reqamt -= sp->cmd_curaddr;
	ncr->n_lastcount = reqamt;
	PRINTF1("ncr_dma_setup: datap 0x%x amt 0x%x\n",
	    sp->cmd_curaddr, reqamt);
	if (reqamt == 0)
		return;

	if (sp->cmd_flags & CFLAG_DMASEND) {
		SET_CSR(ncr, (GET_CSR(ncr)) | NCR_CSR_SEND);
	} else {
		SET_CSR(ncr, (GET_CSR(ncr)) & ~NCR_CSR_SEND);
	}

	if (type != IS_COBRA) {
		/*
		 * reset fifo
		 */
		SET_CSR(ncr, ((GET_CSR(ncr)) & ~NCR_CSR_FIFO_RES));
		SET_CSR(ncr, ((GET_CSR(ncr)) | NCR_CSR_FIFO_RES));
	}

	ncr->n_lastdma = dmaaddr = sp->cmd_curaddr;
	if (type == IS_SCSI3) {
		if (dmaaddr & 0x2) {
			SET_CSR(ncr, ((GET_CSR(ncr)) | NCR_CSR_BPCON));
		} else {
			SET_CSR(ncr, ((GET_CSR(ncr)) & ~NCR_CSR_BPCON));
		}
	}

	SET_DMA_ADDR(ncr, dmaaddr);

	if (type == IS_SCSI3) {
		/*
		 * The SCSI-3 board dma engine should be set to zero
		 * to keep it from starting up when we don't want it to.
		 */
		SET_DMA_COUNT(ncr, 0);
	} else {
		SET_DMA_COUNT(ncr, reqamt);
	}
}


/*
 * Common DMA cleanup for all architectures
 */
static int
ncr_dma_cleanup(register struct ncr *ncr)
{
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);
	register u_long amt, reqamt;
	register u_char junk;

	reqamt = ncr->n_lastcount;

	/*
	 * Adjust dma pointer and xfr counter to reflect
	 * the amount of data actually transferred.
	 */

	amt = reqamt - ncr->n_lastbcr;
	PRINTF1("ncr_dma_cleanup: did 0x%x of 0x%x\n", amt, reqamt);
	sp->cmd_curaddr += amt;
	sp->cmd_data += amt;
	sp->cmd_dsegs.sd_cnt += amt;

	/*
	 * Acknowledge the interrupt, unlatch the SBC.
	 */

	/*
	 * Check for parity errors
	 */
	if (scsi_options & SCSI_OPTIONS_PARITY) {
		if (((sp != (struct scsi_cmd *)0) &&
		    ((sp->cmd_pkt.pkt_flags & FLAG_NOPARITY) == 0)) ||
		    (sp == (struct scsi_cmd *)0)) {
			if (N_SBC->bsr & NCR_BSR_PERR) {
				ncr_printstate(ncr);
				cmn_err(CE_PANIC,
				    "ncr_dma_cleanup: parity error");
			}
		}
	}
	junk = N_SBC->clr;
	N_SBC->tcr = TCR_UNSPECIFIED;
	if (scsi_options & SCSI_OPTIONS_PARITY) {
		N_SBC->mr = NCR_MR_EPC;
	} else {
		N_SBC->mr = 0;
	}

	/*
	 * Call the dma setup routine right away in hopes of beating
	 * the target to the next phase (in case it is a data phase)
	 */
	ncr_dma_setup(ncr);

	/*
	 * Clear any pending interrupt
	 */

	if (GET_CSR(ncr) & NCR_CSR_NCR_IP) {
		junk = N_SBC->clr;
	}

	if (ncr_debug > 2 ||
	    (ncr_debug && (reqamt > 0x200 && (amt & 0x1ff)))) {
		PRINTF2("%s%d: end of dma: lastcnt %d did %d\n",
			CNAME, CNUM, reqamt, amt);
	}
	sp->cmd_pkt.pkt_state |= STATE_XFERRED_DATA;
	UPDATE_STATE(ACTS_UNKNOWN);
#ifdef lint
	junk = junk;
#endif
	return (SUCCESS);
}

/*
 * Common dma wait routine for all architectures
 */

#define	WCOND (NCR_CSR_NCR_IP | NCR_CSR_DMA_CONFLICT | NCR_CSR_DMA_BUS_ERR)
/*
 * wait for dma completion or error
 */

static int
ncr_dma_wait(register struct ncr *ncr)
{

	drv_usecwait(10000);
	/*
	 * wait for indication of dma completion
	 */
	if (ncr_csrwait(ncr, WCOND, NCR_WAIT_COUNT, 1)) {
		printf("%s%d: polled dma never completed\n", CNAME, CNUM);
		ncr_do_abort(ncr, NCR_RESET_ALL, RESET_NOMSG);
		return (FAILURE);
	}
	return ((*ncr->n_dma_cleanup)(ncr));
}

/*
 * Miscellaneous subroutines
 */

static int
ncr_NACKmsg(register struct ncr *ncr)
{
	/*
	 * Attempt to send out a reject message along with ATN
	 */
	ncr->n_cur_msgout[0] = MSG_REJECT;
	ncr->n_omsglen = 1;
	ncr->n_omsgidx = 0;
	N_SBC->icr |= NCR_ICR_ATN;
	return (ncr_ACKmsg(ncr));
}

static int
ncr_ACKmsg(register struct ncr *ncr)
{
	NCR_HIGH_IDECL;
	register volatile struct ncrsbc *sbc = N_SBC;
	register u_char t = 0;
	register u_char junk;

	switch (ncr->n_state) {
	case ACTS_CLEARING_DONE:
	case ACTS_CLEARING_DISC:
		t = 1;
		break;
	case ACTS_SPANNING:
	case ACTS_RESELECTING:
		if (CURRENT_CMD(ncr)->cmd_flags & CFLAG_DMAVALID) {
			ncr_dma_setup(ncr);
		}
		break;
	default:
		break;
	}
	/*
	 * XXXX: FIX ME else if (last message was modify data pointer...)
	 */

	BLK_HIGH_INTR;
	sbc->icr |= NCR_ICR_ACK;
	sbc->tcr = TCR_UNSPECIFIED;
	sbc->mr &= ~NCR_MR_DMA;
	sbc->ser = 0;
	sbc->ser = NUM_TO_BIT(ncr->n_id);
	junk = sbc->clr;	/* clear int */
	UNBLK_HIGH_INTR;

	if (!REQ_DROPPED(ncr)) {
		sbc->icr = 0;
		return (FAILURE);
	}
	BLK_HIGH_INTR;	/* time critical */
	sbc->icr &= ~NCR_ICR_ACK;	/* drop ack */
	if (t) {
		/*
		 * If the state indicates that the target is
		 * clearing the bus, and we have the possibillity
		 * of an interrupt coming in, enable interrupts pronto.
		 * Also, clear the dma engine (in case it wants to
		 * take off for some reason).
		 */
		SET_DMA_ADDR(ncr, 0);
		SET_DMA_COUNT(ncr, 0);
		ENABLE_DMA(ncr);
		/*
		 * WE NEED TO WAIT FOR THE TARGET TO ACTUALLY DROP BSY*.
		 * OTHERWISE, IF WE ATTEMPT TO SELECT SOMEONE ELSE, WE'LL
		 * SIT IN ncr_select() AND AWAIT BUSY TO GO AWAY THERE
		 */
	}
#ifdef lint
	junk = junk;
#endif
	UNBLK_HIGH_INTR;
	return (SUCCESS);
}

/*
 * wait for a bit to be set or cleared in the NCR 5380
 */

static int
ncr_sbcwait(register u_char *reg, register int cond,
    register int wait_cnt, register int set)
{
	register int i;
	register u_char regval;

	for (i = 0; i < wait_cnt; i++) {
		regval = *reg;
		if ((set == 1) && (regval & cond)) {
			return (SUCCESS);
		}
		if ((set == 0) && !(regval & cond)) {
			return (SUCCESS);
		}
		drv_usecwait(10);
	}
	return (FAILURE);
}

static int
ncr_csrwait(register struct ncr *ncr, register int cond,
    register int wait_cnt, register int set)
{
	register int i;
	register u_int regval;

	/*
	 * Wait for a condition to be (de)asserted in the interface csr
	 */
	for (i = 0; i < wait_cnt; i++) {
		regval = GET_CSR(ncr);
		if ((set == 1) && (regval & cond)) {
			return (SUCCESS);
		}
		if ((set == 0) && !(regval & cond)) {
			return (SUCCESS);
		}
		drv_usecwait(10);
	}
	return (FAILURE);
}



/* ARGSUSED */
static void
ncr_watch(caddr_t arg)
{
	register struct scsi_cmd *sp;
	register struct ncr *ncr;
	register int slot;

	for (ncr = ncr_softc; ncr != (struct ncr *)0; ncr = ncr->n_nxt) {
		if (ncr->n_tran == 0)
			continue;
		mutex_enter(&ncr->n_mutex);
		if (ncr->n_ncmds == 0) {
			mutex_exit(&ncr->n_mutex);
			continue;
		}
		/* if current state is not FREE */
		if (ncr->n_state != STATE_FREE) {
			sp = CURRENT_CMD(ncr);
			/*
			 * if a valid job and its "watch_flag" is
			 * set, look at timeout flag, if dec to zero,
			 * then error, unless an existing interrupt
			 * pending, if so, break out; if not yet
			 * TIMEOUT, continue checking on next tick;
			 */
			if (sp && (sp->cmd_flags & CFLAG_WATCH)) {
				if (sp->cmd_timeout < 0) {
					if (INTPENDING(ncr)) {
						sp->cmd_timeout +=
						    scsi_watchdog_tick;
						mutex_exit(
						    &ncr->n_mutex);
						break;
					}
					ncr_curcmd_timeout(ncr);
					mutex_exit(&ncr->n_mutex);
					continue;
				} else {
					sp->cmd_timeout -= scsi_watchdog_tick;
				}
			}
		}
		if (ncr->n_ndisc == 0) {
			mutex_exit(&ncr->n_mutex);
			continue;
		}

		for (slot = 0; slot < NTARGETS * NLUNS_PER_TARGET; slot++) {
			if ((sp = ncr->n_slots[slot]) == 0) {
				continue;
			}
			/*
			 * if this job was not disconnected nor
			 * watching, cont; otherwise, checked timeout
			 * just exhaused, if so, ERROR, except an
			 * interrupt pending, break out; If timed
			 * out, call "upper level completion
			 * routines; if still time left, go on
			 */
			if ((sp->cmd_flags & CFLAG_CMDDISC | CFLAG_WATCH) !=
			    (CFLAG_CMDDISC | CFLAG_WATCH)) {
				continue;
			} else if (sp->cmd_timeout < 0) {
				if (INTPENDING(ncr)) {
					sp->cmd_timeout += scsi_watchdog_tick;
					/*
					 * A pending interrupt defers
					 * the sentence of death.
					 */
					break;
				}
				sp->cmd_pkt.pkt_reason = CMD_TIMEOUT;
				ncr->n_slots[slot] = 0;
				if (sp->cmd_pkt.pkt_comp) {
					mutex_exit(&ncr->n_mutex);
					(*sp->cmd_pkt.pkt_comp) (sp);
					mutex_enter(&ncr->n_mutex);
				}
			} else {
				sp->cmd_timeout -= scsi_watchdog_tick;
			}
		}
		mutex_exit(&ncr->n_mutex);
	}
	(void) timeout(ncr_watch, (caddr_t)0, ncr_watchdog_tick);
}

static void
ncr_curcmd_timeout(register struct ncr *ncr)
{
	register struct scsi_cmd *sp = CURRENT_CMD(ncr);
	printf("%s%d: current cmd timeout target %d\n", CNAME, CNUM, Tgt(sp));
	if (ncr_debug) {
		printf("State 0x%x Laststate 0x%x pkt_state 0x%x\n",
			ncr->n_state, ncr->n_laststate, sp->cmd_pkt.pkt_state);
		printf("Last dma 0x%x Last Count 0x%x\n",
			ncr->n_lastdma, ncr->n_lastcount);
		ncr_dump_datasegs(sp);
	}
	ncr_do_abort(ncr, NCR_RESET_ALL, RESET_NOMSG);
}


/*
 * Abort routines
 */

static void
ncr_internal_abort(register struct ncr *ncr)
{
	ncr_do_abort(ncr, NCR_RESET_ALL, RESET_NOMSG);
}

static void
ncr_do_abort(register struct ncr *ncr, int action, int msg)
{
	register struct scsi_cmd *sp;
	register short  slot, start_slot;
	auto struct scsi_cmd *tslots[NTARGETS * NLUNS_PER_TARGET];

	PRINTF3("ncr_do_abort:\n");
	if ((start_slot = ncr->n_cur_slot) == UNDEFINED)
		start_slot = 0;

	/* temporary store all the current scsi commands per all targets */
	bcopy((caddr_t)ncr->n_slots, (caddr_t)tslots,
		(sizeof (struct scsi_cmd *)) * NTARGETS * NLUNS_PER_TARGET);

	/* call "ncr_internal_reset()" to clear the host-adapter */
	ncr_internal_reset(ncr, action, msg);

	/* Set state to ABORTING */
	ncr->n_state = ACTS_ABORTING;

	/*
	 * start from current slot and around all slots, call "upper level
	 * completion routines to clear all jobs
	 */
	slot = start_slot;
	do {
		sp = tslots[slot];
		if (sp && sp->cmd_pkt.pkt_comp) {
			sp->cmd_pkt.pkt_reason = CMD_RESET;
			mutex_exit(&ncr->n_mutex);
			(*sp->cmd_pkt.pkt_comp) (sp);
			mutex_enter(&ncr->n_mutex);
		}
		slot = NEXTSLOT(slot);
	} while (slot != start_slot);

	/* close the state back to free */
	ncr->n_state = STATE_FREE;

	/* and if any new command queued, call "ncr_ustart()" to start it */
	if (ncr->n_ncmds) {
		(void) ncr_ustart(ncr, start_slot);
	}
}

/*
 * Hardware and Software internal reset routines
 */

static void
ncr_internal_reset(register struct ncr *ncr, int reset_action, int msg_enable)
{
	if (ncr_debug || msg_enable) {
		ncr_printstate(ncr);
	}

	ncr_hw_reset(ncr, reset_action);

	ncr->n_last_slot = ncr->n_cur_slot;
	ncr->n_cur_slot = UNDEFINED;
	ncr->n_state = STATE_FREE;
	bzero((caddr_t)ncr->n_slots,
		(sizeof (struct scsi_cmd *)) * NTARGETS * NLUNS_PER_TARGET);
	ncr->n_ncmds = ncr->n_npolling = ncr->n_ndisc = 0;
	ncr->n_omsgidx = ncr->n_omsglen = 0;
}

/* ARGSUSED */
static void
ncr_hw_reset(struct ncr *ncr, int action)
{
	u_char junk;

	/* reset scsi control logic */
	SET_CSR(ncr, 0);
	drv_usecwait(10);
	SET_CSR(ncr, NCR_CSR_SCSI_RES);

	SET_DMA_ADDR(ncr, 0);
	SET_DMA_COUNT(ncr, 0);

	/*
	 * issue scsi bus reset (make sure interrupts from sbc are disabled)
	 */
	N_SBC->icr = NCR_ICR_RST;
	drv_usecwait(100);
	N_SBC->icr = 0; 	/* clear reset */
	PRINTF3("ncr_hw_reset: RESETTING scsi_bus\n");

	/* give reset scsi devices time to recover (> 2 Sec) */
	drv_usecwait(NCR_RESET_DELAY);
	junk = N_SBC->clr;

	/* Disable sbc interrupts */
	if (scsi_options & SCSI_OPTIONS_PARITY) {
		N_SBC->mr = NCR_MR_EPC;
	} else {
		N_SBC->mr = 0;	/* clear phase int */
	}
	N_SBC->ser = 0;	/* disable (re)select interrupts */
	N_SBC->ser = NUM_TO_BIT(ncr->n_id);
	/*
	 * enable general interrupts
	 */
	ENABLE_INTR(ncr);
	DISABLE_DMA(ncr);
#ifdef lint
	junk = junk;
#endif
}

/*
 * Debugging/printing functions
 */

static void
ncr_dump_datasegs(struct scsi_cmd *sp)
{
	if ((sp->cmd_flags & CFLAG_DMAVALID) == 0)
		return;
	printf("Data Mapping: addr %x size %x; Curdma %x\n",
	    sp->cmd_dmacookie.dmac_address, sp->cmd_dmacookie.dmac_size,
	    sp->cmd_curaddr);
}

static void
ncr_printstate(register struct ncr *ncr)
{
	register int re_en = 0;
	u_char	cdr, icr, mr, tcr, cbsr, bsr;
	u_int 	csr;

	/*
	 * Print h/w information
	 */
	csr = GET_CSR(ncr);
	if (csr & NCR_CSR_DMA_EN) {
		re_en++;
		DISABLE_DMA(ncr);
	}
	cdr = N_SBC->cdr;
	icr = N_SBC->icr;
	mr = N_SBC->mr;
	tcr = N_SBC->tcr;
	cbsr = N_SBC->cbsr;
	bsr = N_SBC->bsr;
	printf("%s%d: host adapter for %s\n", CNAME, CNUM,
	    (ncr->n_type == IS_COBRA) ? "4/110" : "SCSI-3");
	printf("csr=0x%b; cbsr=0x%b\n", csr, CSR_BITS, cbsr, cbsr_bits);
	printf("bsr=0x%b; tcr=0x%b; mr=0x%b\n", bsr, BSR_BITS, tcr, TCR_BITS,
		mr, MR_BITS);
	printf("icr=0x%b; cdr=0x%x; ", icr, ICR_BITS, cdr);
	printf(" dma_addr=0x%x; dma_cnt=0x%x", GET_DMA_ADDR(ncr),
	    GET_DMA_COUNT(ncr));
	if (ncr->n_type == IS_SCSI3)
		printf(" bcr=0x%x", GET_BCR(ncr));
	printf("\n");

	/*
	 * Print s/w information
	 */

	printf("State=0x%x; Laststate=0x%x; last msgin: %x; last msgout: %x\n",
	    ncr->n_state, ncr->n_laststate, ncr->n_last_msgin,
	    ncr->n_last_msgout);

	if (ncr->n_cur_slot != UNDEFINED) {
		register struct scsi_cmd *sp = CURRENT_CMD(ncr);
		printf("Currently connected to %d.%d, pkt state=0x%x\n",
			Tgt(sp), Lun(sp), sp->cmd_pkt.pkt_state);
		printf("Current Command = 0x%x\n", *(sp->cmd_cdbp));
		ncr_dump_datasegs(sp);
	}

	/*
	 * leave...
	 */
	if (re_en)
		ENABLE_DMA(ncr);
}

static int
ncr_set_window(struct ncr *ncr, register struct scsi_cmd *sp, const char *fmt)
{
	u_long off;
	u_int len;

	if (ddi_dma_curwin(sp->Handle, (off_t *)&off, &len)) {
		cmn_err(CE_WARN, "ncr%d: no dma window on %s operation",
		    CNUM, fmt);
		return (-1);
	}
	if (sp->cmd_data >= off + len || sp->cmd_data < off) {
		/*
		 * We can assume power of two for len.
		 */
		off = (sp->cmd_data & (len - 1));
		if (ddi_dma_movwin(sp->Handle, (off_t *)&off, &len,
		    &sp->Cookie)) {
			cmn_err(CE_WARN,
			    "ncr%d: no new dma window at %x for %s operation",
			    CNUM, sp->cmd_data, fmt);
			return (-1);
		}
		sp->cmd_dmawinsize = len;
		sp->cmd_curaddr = sp->Cookie.dmac_address;
	}
	return (0);
}
