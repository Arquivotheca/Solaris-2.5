/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)pln.c 1.33	95/08/28 SMI"

/*
 * Pluto host adapter driver
 */

#include <note.h>
#include <sys/scsi/scsi.h>
#include <sys/fc4/fc.h>
#include <sys/fc4/fcp.h>
#include <sys/fc4/fc_transport.h>
#include <sys/scsi/adapters/plndef.h>
#include <sys/scsi/targets/pln_ctlr.h>	/* for pln structures */
#include <sys/scsi/adapters/plnvar.h>
#include <sys/stat.h>
#include <sys/varargs.h>
#include <sys/var.h>
#include <sys/thread.h>
#include <sys/proc.h>



#ifdef	TRACE
#include <sys/vtrace.h>
#endif	/* TRACE */

/*
 * Local function prototypes
 */

#ifdef  ON1093
static	int pln_bus_ctl(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t op,
	void *arg, void *result);
#else


static int pln_scsi_tgt_init(dev_info_t *, dev_info_t *,
	scsi_hba_tran_t *, struct scsi_device *);
static int pln_scsi_tgt_probe(struct scsi_device *, int (*)());
static void pln_scsi_tgt_free(dev_info_t *, dev_info_t *,
	scsi_hba_tran_t *, struct scsi_device *);

static struct scsi_pkt *pln_scsi_init_pkt(struct scsi_address *ap,
	struct scsi_pkt *pkt, struct buf *bp, int cmdlen, int statuslen,
	int tgtlen, int flags, int (*callback)(), caddr_t arg);
static void pln_scsi_destroy_pkt(struct scsi_address *ap,
	struct scsi_pkt *pkt);
void pln_scsi_dmafree(struct scsi_address *ap, struct scsi_pkt *pkt);

#endif  /* ON1093 */

static	int pln_get_int_prop(dev_info_t *dip, char *property, int *value);

#ifdef  ON1093
static	int pln_initchild(dev_info_t *dip, dev_info_t *child_dip,
	    pln_address_t *addr, int sleep_flag);
#else
static  int pln_initchild(dev_info_t *dip, dev_info_t *child_dip,
	scsi_hba_tran_t *hba_tran, pln_address_t *addr, int sleep_flag);
#endif  /* ON1093 */

static	int pln_identify(dev_info_t *dip);
static	int pln_probe(dev_info_t *dip);
static	int pln_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static	int pln_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static	struct pln *pln_softstate_alloc(dev_info_t *dip, int sleep_flag);
static	void pln_softstate_free(dev_info_t *dip, struct pln *pln);
static	void pln_softstate_unlink(struct pln *);
#ifdef  ON1093
static	int pln_start(struct pln_scsi_cmd *sp);
#else
static  int pln_start(struct scsi_address *ap, struct scsi_pkt *pkt);
#endif  /* ON1093 */

static	int pln_local_start(struct pln *, struct pln_scsi_cmd *,
	pln_fc_pkt_t *);
static	int pln_startcmd(struct pln *pln, struct pln_fc_pkt *fp);
static	int pln_dopoll(struct pln *, struct pln_fc_pkt *);
static	void pln_start_fcp(pln_fc_pkt_t *);
static	void pln_cmd_callback(struct fc_packet	*fpkt);
static	void pln_throttle(struct pln *);
static	void pln_throttle_start(struct pln *);
static	void pln_build_extended_sense(struct pln_scsi_cmd *, u_char);
static	void pln_restart_one(struct pln *, struct pln_scsi_cmd *);
static	void pln_uc_callback(void *arg);
static	void pln_statec_callback(void *arg, fc_statec_t);
static	int pln_init_scsi_pkt(struct pln *pln, struct pln_scsi_cmd *sp);
static	int pln_prepare_fc_packet(struct pln *pln,
	struct pln_scsi_cmd *sp, struct pln_fc_pkt *);
static	int pln_prepare_short_pkt(struct pln *,
	struct pln_fc_pkt *, void (*)(struct fc_packet *), int);
static	void pln_fpacket_dispose_all(struct pln *pln, struct pln_disk *pd);
static	void pln_fpacket_dispose(struct pln *pln, pln_fc_pkt_t *fp);
static	int pln_prepare_cmd_dma_seg(struct pln *pln, pln_fc_pkt_t *fp,
	struct pln_scsi_cmd *sp);
static	int pln_prepare_data_dma_seg(pln_fc_pkt_t *fp, struct pln_scsi_cmd *sp);
static	int pln_execute_cmd(struct pln *pln, int cmd, int arg1,
	int arg2, caddr_t datap, int datalen, int sleep_flag);
static	int pln_private_cmd(struct pln *, int, struct pln_scsi_cmd *,
	int, int, caddr_t, int, long, int);
static	int pln_abort(struct scsi_address *ap, struct scsi_pkt *pkt);
static	int _pln_abort(struct pln *pln, struct scsi_address *ap,
	struct scsi_pkt *pkt);
static	int pln_reset(struct scsi_address *ap, int level);
static	int pln_getcap(struct scsi_address *ap, char *cap, int whom);
static	int pln_setcap(struct scsi_address *ap, char *cap,
	int value, int whom);
static	int pln_commoncap(struct scsi_address *ap, char *cap,
	int val, int tgtonly, int doset);
static	void pln_watch(caddr_t arg);
static	void pln_transport_offline(struct pln *, int, int);
static	void pln_offline_callback(struct fc_packet *);
static	void pln_transport_reset(struct pln *, int, int);
static	void pln_reset_callback(struct fc_packet *);
static	int pln_build_disk_state(struct pln *pln, int sleep_flag);
static	int pln_alloc_disk_state(struct pln *pln, int sleep_flag);
static	void pln_init_disk_state_mutexes(struct pln *pln);
static	void pln_free_disk_state(struct pln *pln);
static	void pln_destroy_disk_state_mutexes(struct pln *pln);
static	struct scsi_pkt *pln_scsi_pktalloc(struct scsi_address *, int,
	int, int, int (*)(), caddr_t);
static	void pln_scsi_pktfree(struct scsi_pkt *);
static	int pln_cr_alloc(struct pln *, pln_fc_pkt_t *);
static	void pln_cr_free(struct pln *, pln_fc_pkt_t *);
static	int pln_cr_init(struct pln *);
static	void pln_disp_err(dev_info_t *, u_int, char *);

static void pln_callback(register struct pln *);
static void pln_add_callback(register struct pln *, struct pln_scsi_cmd *);

#ifdef	PLNDEBUG
static	void pln_printf(dev_info_t *dip, const char *format, ...);
static	char *pln_cdb_str(char *s, u_char *cdb, int cdblen);
static	void pln_dump(char *msg, u_char *addr, int len);
#endif	/* PLNDEBUG */


/*
 * Local static data
 */
static struct pln	*pln_softc		= NULL;
static kmutex_t		pln_softc_mutex;
static long		pln_watchdog_tick	= 0;
static int		pln_watchdog_id		= 0;
static u_long		pln_watchdog_time	= 1;
static u_long		pln_watchdog_init	= 0;
static char		*pln_label		= "pln";
static int		pln_initiator_id	= PLN_INITIATOR_ID;

static int		pln_disable_timeouts	= 0;
static int		pln_online_timeout	= PLN_ONLINE_TIMEOUT;
static int		pln_en_online_timeout	= 1;

#ifdef	PLN_LOCK_STATS
static int		pln_lock_stats		= 1;
extern int		lock_stats;
#endif	PLN_LOCK_STATS


#ifdef	PLNDEBUG
/*
 * PATCH this location to 0xffffffff to enable full
 * debugging.
 * The definition of each one of these mask bits is
 * in plnvar.h
 */
static u_int		plnflags		= 0x00000001;
static int		plndebug		= 1;
#ifdef	PLNLOGGING
static int		plnlog			= 0;
static int		plnlog_nmsgs		= PLNLOG_NMSGS;
static int		plnlog_msglen		= PLNLOG_MSGLEN;
static char		**plnlog_buf		= NULL;
static int		plnlog_ptr1		= 0;
static int		plnlog_ptr2		= 0;
#endif	/* PLNLOGGING */
#endif	/* PLNDEBUG */

/*
 * Number of FCP command/response structures to allocate in attach()
 * (establishes one of the constraints on the maximum queue depth)
 */
static int		pln_fcp_elements	= PLN_CR_POOL_DEPTH;

/*
 * Externals to pln_ctlr, which is actually linked together
 * with pln to form the final driver module.
 */
extern struct cb_ops	pln_ctlr_cb_ops;

/*
 * pln_ctlr interface functions
 */
extern int	pln_ctlr_init(void);
extern void	pln_ctlr_fini(void);
extern int	pln_ctlr_probe(dev_info_t *dip);
extern int	pln_ctlr_attach(dev_info_t *dip, struct pln *pln);
extern void	pln_ctlr_detach(dev_info_t *dip);


#ifdef	ON1093
/*
 * bus_ops for pln nexus driver
 */
static struct bus_ops pln_bus_ops = {
	nullbusmap,			/* bus_map */
	NULL,				/* bus_get_intrspec */
	NULL,				/* bus_add_intrspec */
	NULL,				/* bus_remove_intrspec */
	i_ddi_map_fault,		/* bus_map_fault */
	ddi_dma_map,			/* bus_dma_map */
	ddi_dma_mctl,			/* bus_dma_ctl */
	pln_bus_ctl,			/* bus_ctl */
	ddi_bus_prop_op			/* bus_prop_op */
};
#endif	/* ON1093 */

/*
 * dev_ops
 */
static struct dev_ops pln_dev_ops = {
	DEVO_REV,			/* devo_rev, */
	0,				/* refcnt  */
	ddi_no_info,			/* info */
	pln_identify,			/* identify */
	pln_probe,			/* probe */
	pln_attach,			/* attach */
	pln_detach,			/* detach */
	nodev,				/* reset */
	&pln_ctlr_cb_ops,		/* cb_ops */
#ifdef	ON1093
	&pln_bus_ops			/* bus_ops */
#else
	NULL				/* no bus ops */
#endif	/* ON1093 */
};

/*
 * Warlock directives
 *
 * "unshared" denotes fields that are written and read only when
 * a single thread is guaranteed to "own" the structure or variable
 */
_NOTE(SCHEME_PROTECTS_DATA("wr only by timer & attach", pln_watchdog_id))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", pln_watchdog_time))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach", pln_watchdog_tick))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach", scsi_hba_tran::tran_abort))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach",
		scsi_hba_tran::tran_destroy_pkt))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach", scsi_hba_tran::tran_dmafree))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach", scsi_hba_tran::tran_getcap))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach",
		scsi_hba_tran::tran_hba_private))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach",
	scsi_hba_tran::tran_init_pkt))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach", scsi_hba_tran::tran_reset))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach", scsi_hba_tran::tran_setcap))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach", scsi_hba_tran::tran_start))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach",
	scsi_hba_tran::tran_tgt_free))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach",
	scsi_hba_tran::tran_tgt_init))
_NOTE(SCHEME_PROTECTS_DATA("write only in attach",
		scsi_hba_tran::tran_tgt_probe))
_NOTE(SCHEME_PROTECTS_DATA("unshared", scsi_hba_tran::tran_tgt_private))
_NOTE(MUTEX_PROTECTS_DATA(pln_softc_mutex, pln_softc))
_NOTE(MUTEX_PROTECTS_DATA(pln_softc_mutex, pln_watchdog_init))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_cmd))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_comp))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_cookie))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_datap))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_flags))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_io_class))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_io_devdata))
_NOTE(SCHEME_PROTECTS_DATA("write only at pkt init", fc_packet::fc_pkt_private))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_rsp))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_statistics))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_status))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_pkt_timeout))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fc_packet::fc_frame_resp))

_NOTE(SCHEME_PROTECTS_DATA("unshared", fcp_cmd::fcp_cntl))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fcp_cmd::fcp_data_len))
_NOTE(SCHEME_PROTECTS_DATA("unshared", fcp_cmd::fcp_ent_addr))

_NOTE(SCHEME_PROTECTS_DATA("unshared", pln_scsi_cmd))
_NOTE(SCHEME_PROTECTS_DATA("unshared", scsi_extended_sense))
_NOTE(SCHEME_PROTECTS_DATA("unshared", scsi_arq_status))
_NOTE(SCHEME_PROTECTS_DATA("unshared", scsi_address))
_NOTE(SCHEME_PROTECTS_DATA("unshared", scsi_device))
_NOTE(SCHEME_PROTECTS_DATA("unshared", scsi_cdb))
_NOTE(SCHEME_PROTECTS_DATA("unshared", FC2_FRAME_HDR))

_NOTE(SCHEME_PROTECTS_DATA("unshared", scsi_pkt::pkt_comp))
_NOTE(SCHEME_PROTECTS_DATA("unshared", scsi_pkt::pkt_private))
_NOTE(SCHEME_PROTECTS_DATA("unshared", scsi_pkt::pkt_time))
_NOTE(SCHEME_PROTECTS_DATA("unshared", scsi_pkt::pkt_flags))

_NOTE(MUTEX_PROTECTS_DATA(pln_softc_mutex, pln::pln_next))
_NOTE(MUTEX_PROTECTS_DATA(pln_softc_mutex, pln::pln_ref_cnt))

_NOTE(SCHEME_PROTECTS_DATA("safe sharing", pln_address))

#ifdef	PLNDEBUG
#ifdef	PLNLOGGING
_NOTE(SCHEME_PROTECTS_DATA("garbled debug messages are okay", plnlog_ptr1))
_NOTE(SCHEME_PROTECTS_DATA("garbled debug messages are okay", plnlog_ptr2))
_NOTE(SCHEME_PROTECTS_DATA("garbled debug messages are okay", plnlog_buf))
#endif	PLNLOGGING
#endif	PLNDEBUG


/*
 * Return an integer property only
 */
static int
pln_get_int_prop(
	dev_info_t		*dip,
	char			*property,
	int			*value)
{
	int			len;

	len = sizeof (int);
	return ((ddi_prop_op(DDI_DEV_T_ANY, dip, PROP_LEN_AND_VAL_BUF,
		DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP,
		property, (caddr_t)value, &len)) == DDI_SUCCESS);
}


#ifdef	ON1093
static int
pln_bus_ctl(
	dev_info_t		*dip,
	dev_info_t		*rdip,
	ddi_ctl_enum_t		op,
	void			*arg,
	void			*result)
{

	P_B_PRINTF((dip,
		"pln_bus_ctl%d: dip=0x%x rdip=0x%x op=0x%x arg=0x%x\n",
		ddi_get_instance(dip), dip, rdip, op, arg));

	switch (op) {
	case DDI_CTLOPS_REPORTDEV:
	{
		struct scsi_device	*devp;
		pln_address_t		*pln_addr;

		devp = (struct scsi_device *)ddi_get_driver_private(rdip);
		pln_addr = (pln_address_t *)devp->sd_address.a_addr_ptr;


		if (pln_addr->pln_entity == PLN_ENTITY_CONTROLLER) {
			/*
			 * pln:ctlr
			 */
			cmn_err(CE_CONT, "?%s%d at %s%d\n",
				ddi_get_name(rdip), ddi_get_instance(rdip),
				ddi_get_name(dip), ddi_get_instance(dip));
		} else {
			/*
			 * Grouped or individual disks
			 */
			cmn_err(CE_CONT,
				"?%s%d at %s%d: port %d target %d\n",
				ddi_get_name(rdip), ddi_get_instance(rdip),
				ddi_get_name(dip), ddi_get_instance(dip),
				pln_addr->pln_port, pln_addr->pln_target);
		}
		return (DDI_SUCCESS);
	}

	case DDI_CTLOPS_IOMIN:
		P_B_PRINTF((dip, "pln_bus_ctl: iomin\n"));
		return (ddi_ctlops(dip, rdip, op, arg, result));

	case DDI_CTLOPS_INITCHILD:
	{
		dev_info_t		*child = (dev_info_t *)arg;
		pln_address_t		addr;
		int			port;
		int			target;

		P_1_PRINTF((dip, "pln_bus_ctl: INITCHILD %s\n",
			ddi_get_name(child)));

		if (!pln_get_int_prop(child, "port", &port)) {
		    P_B_PRINTF((dip, "init child: no port property\n"));
		    return (DDI_NOT_WELL_FORMED);
		}
		if (!pln_get_int_prop(child, "target", &target)) {
		    if (!pln_get_int_prop(child, "disk", &target)) {
			P_E_PRINTF((dip,
			    "init child: no target or disk property\n"));
			return (DDI_NOT_WELL_FORMED);
		    }
		}

		/*
		 * Set up addressing for this child
		 */
		addr.pln_entity = (u_short)PLN_ENTITY_DISK_SINGLE;
		addr.pln_port = (u_short)port;
		addr.pln_target = (u_short)target;
		addr.pln_reserved = 0;

		return (pln_initchild(dip, child, &addr, KM_SLEEP));
	}

	case DDI_CTLOPS_UNINITCHILD:
	{
		dev_info_t		*child = (dev_info_t *)arg;
		struct scsi_device	*sd;

		P_1_PRINTF((dip, "pln_bus_ctl: UNINITCHILD %s\n",
			ddi_get_name(child)));

		sd = (struct scsi_device *)ddi_get_driver_private(child);
		if (sd != (struct scsi_device *)0) {
			kmem_free((caddr_t)sd->sd_address.a_addr_ptr,
				sizeof (pln_address_t));
			mutex_destroy(&sd->sd_mutex);
			scsi_unslave(sd);
			kmem_free((caddr_t)sd, sizeof (*sd));
		}

		ddi_set_driver_private(child, NULL);
		ddi_set_name_addr(child, NULL);
		return (DDI_SUCCESS);
	}

	/*
	 * These ops correspond to functions that "shouldn't" be called
	 * by a SCSI target driver.  So we whinge when we're called.
	 */
	case DDI_CTLOPS_DMAPMAPC:
	case DDI_CTLOPS_REPORTINT:
	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
	case DDI_CTLOPS_NINTRS:
	case DDI_CTLOPS_SIDDEV:
	case DDI_CTLOPS_SLAVEONLY:
	case DDI_CTLOPS_AFFINITY:
	case DDI_CTLOPS_POKE_INIT:
	case DDI_CTLOPS_POKE_FLUSH:
	case DDI_CTLOPS_POKE_FINI:
	case DDI_CTLOPS_INTR_HILEVEL:
		P_B_PRINTF((dip, "%s%d: invalid op (%d) from %s%d\n",
			ddi_get_name(dip), ddi_get_instance(dip),
			op, ddi_get_name(rdip), ddi_get_instance(rdip)));
		return (DDI_FAILURE);

	/*
	 * Everything else (e.g. PTOB/BTOP/BTOPR requests) we pass up
	 */
	default:
		P_B_PRINTF((dip, "%s%d: op 0x%x from %s%d\n",
			ddi_get_name(dip), ddi_get_instance(dip),
			op, ddi_get_name(rdip), ddi_get_instance(rdip)));
		return (ddi_ctlops(dip, rdip, op, arg, result));
	}
}
#endif	ON1093



/*ARGSUSED*/
static int
pln_scsi_tgt_probe(
	struct scsi_device	*sd,
	int			(*callback)())
{
	int	rval;

	rval = scsi_hba_probe(sd, callback);

#ifdef PLN_DEBUG
	{
		char		*s;
		struct pln	*pln = SDEV2PLN(sd);

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
		cmn_err(CE_CONT, "pln%d: %s target %d lun %d %s\n",
			ddi_get_instance(pln->pln_dip),
			ddi_get_name(sd->sd_dev),
			sd->sd_address.a_target,
			sd->sd_address.a_lun, s);
	}
#endif	/* PLN_DEBUG */

	return (rval);
}


/*ARGSUSED*/
static void
pln_scsi_tgt_free(
	dev_info_t		*hba_dip,
	dev_info_t		*tgt_dip,
	scsi_hba_tran_t		*hba_tran,
	struct scsi_device	*sd)
{
	pln_address_t		*pln_addr;

#ifdef	PLN_DEBUG
	cmn_err(CE_CONT, "pln_scsi_tgt_free: %s%d %s%d <%d,%d>\n",
		ddi_get_name(hba_dip), ddi_get_instance(hba_dip),
		ddi_get_name(tgt_dip), ddi_get_instance(tgt_dip),
		targ, lun);
#endif	/* PLN_DEBUG */

	pln_addr = hba_tran->tran_tgt_private;
	kmem_free(pln_addr, sizeof (pln_address_t));
	hba_tran->tran_tgt_private = NULL;
}

/*ARGSUSED*/
static int
pln_scsi_tgt_init(
	dev_info_t		*hba_dip,
	dev_info_t		*tgt_dip,
	scsi_hba_tran_t		*hba_tran,
	struct scsi_device	*sd)
{
	pln_address_t		addr;
	int			port;
	int			target;

#ifdef PLN_DEBUG
	cmn_err(CE_CONT, "%s%d: %s%d <%d,%d>\n",
		ddi_get_name(hba_dip), ddi_get_instance(hba_dip),
		ddi_get_name(tgt_dip), ddi_get_instance(tgt_dip),
		sd->sd_address.a_target, sd->sd_address.a_lun);
#endif

	if (!pln_get_int_prop(tgt_dip, "target", &target)) {
		P_E_PRINTF((hba_dip, "no target property\n"));
		return (DDI_NOT_WELL_FORMED);
	}
	if (!pln_get_int_prop(tgt_dip, "port", &port)) {
		P_E_PRINTF((hba_dip, "no port property\n"));
		P_I_PRINTF((hba_dip, "Target property = 0x%x\n", target));
		return (DDI_NOT_WELL_FORMED);
	}

	/*
	 * Set up addressing for this child
	 */
	addr.pln_entity = (u_short)PLN_ENTITY_DISK_SINGLE;
	addr.pln_port = (u_short)port;
	addr.pln_target = (u_short)target;
	addr.pln_reserved = 0;

	P_I_PRINTF((hba_dip, "pln_scsi_tgt_init: entity %d, port %d"
		" target %d\n", addr.pln_entity, addr.pln_port,
		addr.pln_target));

	return (pln_initchild(hba_dip, tgt_dip, hba_tran,
		&addr, KM_SLEEP));
}


/*
 * Function name : pln_initchild()
 *
 * Return Values : DDI_SUCCESS
 *		   DDI_FAILURE
 */

static int
pln_initchild(
	dev_info_t		*my_dip,
	dev_info_t		*child_dip,
#ifndef	ON1093
	scsi_hba_tran_t		*hba_tran,
#endif	/* ON1093 */
	pln_address_t		*addr,
	int			sleep_flag)
{
	struct pln		*pln;
	pln_address_t		*pln_addr;
#ifdef	ON1093
	struct scsi_device	*sd;
	char			buf[80];
#endif	/* ON1093 */
	char			name[MAXNAMELEN];
	struct pln_disk		*pd;
#ifdef	PLN_LOCK_STATS
	int			i;
#endif	PLN_LOCK_STATS


	P_I_PRINTF((my_dip, "pln_initchild: pln dip 0x%x\n", my_dip));
	P_I_PRINTF((my_dip,
		"pln_initchild: child dip 0x%x\n", child_dip));

	mutex_enter(&pln_softc_mutex);

	for (pln = pln_softc; pln && (pln->pln_dip != my_dip);
		pln = pln->pln_next);

	mutex_exit(&pln_softc_mutex);

	if (!pln)
	    return (DDI_FAILURE);

	P_I_PRINTF((my_dip, "pln_initchild: pln structure at 0x%x\n", pln));

	P_I_PRINTF((my_dip, "pln_initchild: address 0x%x 0x%x 0x%x 0x%x\n",
		addr->pln_entity, addr->pln_port, addr->pln_target,
		addr->pln_reserved));

	/*
	 * Find the appropriate pln_disk structure for this child
	 */
	switch (addr->pln_entity) {
	case PLN_ENTITY_DISK_SINGLE:
		/*
		 * Child is an individual disk
		 */
		P_I_PRINTF((my_dip, "init child: Individual disk\n"));
		if (addr->pln_port >= pln->pln_nports ||
				addr->pln_target >= pln->pln_ntargets ||
					addr->pln_reserved != 0) {
			return (DDI_FAILURE);
		}
		pd = pln->pln_ids[addr->pln_port] + addr->pln_target;

		/* Check for duplicate calls to attach */
		if (pd->pd_state != PD_NOT_ATTACHED)
			return (DDI_FAILURE);

		sprintf(name, "%d,%d", addr->pln_port, addr->pln_target);
		pd->pd_state = PD_ATTACHED;
		break;

	default:
		P_E_PRINTF((my_dip, "init child: no such entity 0x%x\n",
			addr->pln_entity));
		return (DDI_FAILURE);
	}

	/*
	 * Define the name of this child
	 */
	ddi_set_name_addr(child_dip, name);

#ifdef	ON1093

	/*
	 * Allocate and initialize the scsi_device structure
	 */
	sd = (struct scsi_device *)
		kmem_zalloc(sizeof (*sd), sleep_flag);
	if (sd == (struct scsi_device *)0) {
		P_E_PRINTF((my_dip,
			"scsi_device alloc failed for %s\n", name));
		return (DDI_FAILURE);
	}
	P_I_PRINTF((my_dip,
		"init child: Allocated scsi_device struct at 0x%x\n",
		sd));
#endif	/* ON1093 */

	/*
	 * Allocate and initialize the address structure
	 */
	pln_addr = (pln_address_t *)
		kmem_zalloc(sizeof (pln_address_t), sleep_flag);
	if (pln_addr == NULL) {
		P_E_PRINTF((my_dip,
			"init child: pln_address alloc failed\n"));
#ifdef	ON1093
		kmem_free((caddr_t)sd, sizeof (*sd));
#endif	/* ON1093 */
		return (DDI_FAILURE);
	}
	P_I_PRINTF((my_dip,
		"init child: Allocated pln_addr struct at 0x%x size 0x%x\n",
		pln_addr, sizeof (pln_address_t)));
	bcopy((void *)addr, (void *)pln_addr, sizeof (pln_address_t));

	pd->pd_dip = child_dip;
#ifdef	ON1093
	sd->sd_dev = child_dip;
	sd->sd_address.a_cookie = (int)&pln->pln_tran;
	sd->sd_address.a_addr_ptr = pln_addr;
	sd->sd_lkarg = pln->pln_iblock;

	/*
	 * Initialize the scsi_device mutex
	 */
#ifdef	PLN_LOCK_STATS
	i = lock_stats;
	lock_stats |= pln_lock_stats;
#endif	PLN_LOCK_STATS
	(void) sprintf(buf, "pln %d sd mutex %s", ddi_get_instance(my_dip),
				name);
	mutex_init(&sd->sd_mutex, buf, MUTEX_DRIVER, sd->sd_lkarg);
#ifdef	PLN_LOCK_STATS
	lock_stats = i;
#endif	PLN_LOCK_STATS

	ddi_set_driver_private(child_dip, (caddr_t)sd);

#else
	/*
	 * Set the target-private field of the transport
	 * structure to point to our extended address structure.
	 */
	hba_tran->tran_tgt_private = pln_addr;

#endif	/* ON1093 */

	return (DDI_SUCCESS);
}


#ifndef	lint
static char _depends_on[] = "misc/scsi";
#endif	/* lint */

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

static struct modldrv modldrv = {
	&mod_driverops,			/* This module is a driver */
	"SPARCstorage Array Nexus Driver",	/* Name of the module. */
	&pln_dev_ops,			/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

int	_init(void);
int	_fini(void);
int	_info(struct modinfo *modinfop);


static char pln_initmsg[] = "pln _init: pln.c\t1.33\t95/08/28\n";


int
_init(void)
{
	int	i;
#ifdef	PLNDEBUG
#ifdef	PLNLOGGING
	char	*msgs;

	if (plnlog)
		cmn_err(CE_CONT, pln_initmsg);

	plnlog_buf = (char **)
		kmem_zalloc(sizeof (char *) * plnlog_nmsgs, KM_NOSLEEP);
	if (plnlog_buf == NULL) {
		cmn_err(CE_WARN, "pln: log alloc(1) failed\n");
		return (0);
	}

	msgs = (char *)
		kmem_zalloc(sizeof (char) * plnlog_msglen * plnlog_nmsgs,
				KM_NOSLEEP);
	if (msgs == NULL) {
		kmem_free((void *)plnlog_buf,
			sizeof (char *)*plnlog_nmsgs);
		cmn_err(CE_WARN, "pln: log alloc(2) failed\n");
		return (0);
	}

	for (i = 0; i < plnlog_nmsgs; i++) {
		plnlog_buf[i] = msgs;
		msgs += plnlog_msglen;
	}
#endif	/* PLNLOGGING */
#endif	/* PLNDEBUG */

	mutex_init(&pln_softc_mutex, "pln global mutex", MUTEX_DRIVER, NULL);


#ifndef	ON1093
	if ((i = scsi_hba_init(&modlinkage)) != 0) {
		return (i);
	}
#endif	/* ON1093 */
	if ((i = mod_install(&modlinkage)) != 0) {
		cmn_err(CE_CONT,
			"?pln _init: mod_install failed error=%d\n", i);
#ifndef	ON1093
		scsi_hba_fini(&modlinkage);
#endif	/* ON1093 */
		mutex_destroy(&pln_softc_mutex);
		return (i);
	}

	/*
	 * Initialize pln_ctlr
	 */
	return (pln_ctlr_init());
}


int
_fini(void)
{
	int	i;

	mutex_destroy(&pln_softc_mutex);

	if ((i = mod_remove(&modlinkage)) != 0) {
		return (i);
	}
#ifndef	ON1093
		scsi_hba_fini(&modlinkage);
#endif	/* ON1093 */
	pln_ctlr_fini();

#ifdef	PLNDEBUG
#ifdef	PLNLOGGING
	kmem_free((void *) plnlog_buf[0],
			sizeof (char) * plnlog_msglen * plnlog_nmsgs);
	kmem_free((void *) plnlog_buf, sizeof (char *) * plnlog_nmsgs);
#endif	/* PLNLOGGING */
#endif	/* PLNDEBUG */

	return (i);
}

int
_info(
	struct modinfo	*modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


static int
pln_identify(
	dev_info_t	*dip)
{
	char		*dname = ddi_get_name(dip);

	/*
	 * Note the octal literal for a comma.
	 * Avoids Shannon's C-style awk script's complaints
	 */
	if (strcmp(dname, "pln") == 0 ||
			strcmp(dname, "SUNW\054pln") == 0) {
		P_1_PRINTF((dip,
		    "DDI_IDENTIFIED: %s\n", dname));
		return (DDI_IDENTIFIED);
	} else {
		P_E_PRINTF((dip,
		    "DDI_NOT_IDENTIFIED: %s\n", dname));
		return (DDI_NOT_IDENTIFIED);
	}
}

static int
pln_probe(
	dev_info_t			*dip)
{
	int				i;
	struct pln			*pln = NULL;
	struct p_inquiry		*inquiry = NULL;
	int				rval = DDI_PROBE_FAILURE;
	struct pln_scsi_cmd		*sp;

	P_1_PRINTF((dip, "pln_probe:\n"));

	/*
	 * Is device self-identifying?
	 *
	 * If Yes - fail because pln is not self identifying -
	 *
	 */
	if (ddi_dev_is_sid(dip) == DDI_SUCCESS) {
		P_E_PRINTF((dip, "%s%d: self-identifying",
			pln_label, ddi_get_instance(dip)));
		goto done;
	}

	/*
	 * XXX - All of this messing around in probe should really go away.
	 * We can probably always return probe as successful, then
	 * do the real work in attach, since we need to do it there
	 * anyway.  This would make things (especially the error recovery)
	 * much cleaner.
	 */

	/*
	 * Allocate an instance of our private data
	 */
	if ((pln = pln_softstate_alloc(dip, KM_SLEEP)) == NULL) {
		P_E_PRINTF((dip, "pln_probe: softstate alloc failed\n"));
		goto done;
	}
	P_PA_PRINTF((dip, "pln_probe: softstate alloc ok\n"));

	/*
	 * Get some iopb space for the fcp cmd/rsp packets
	 */
	if (!pln_cr_init(pln))
		goto done;

	/*
	 * Allocate space for Inquiry data
	 */
	if ((inquiry = (struct p_inquiry *)
			kmem_zalloc(sizeof (struct p_inquiry),
				KM_NOSLEEP)) == NULL) {
		goto done;
	}
	P_PA_PRINTF((dip, "pln_probe: inquiry buffer 0x%x\n", inquiry));

	/*
	 * Grab a couple of fc packet structures for error recovery
	 */
	if ((sp = (struct pln_scsi_cmd *)
		pln_scsi_pktalloc(&pln->pln_scsi_addr,
		sizeof (union scsi_cdb), sizeof (struct scsi_arq_status),
		NULL, SLEEP_FUNC, NULL)) == NULL)
		goto done;
	pln->pkt_offline = (struct pln_fc_pkt *)sp->cmd_fc_pkt;

	if ((sp = (struct pln_scsi_cmd *)
		pln_scsi_pktalloc(&pln->pln_scsi_addr,
		sizeof (union scsi_cdb), sizeof (struct scsi_arq_status),
		NULL, SLEEP_FUNC, NULL)) == NULL)
		goto done;
	pln->pkt_reset = (struct pln_fc_pkt *)sp->cmd_fc_pkt;

	/*
	 * Give a command to the pluto controller, see if it responds.
	 * We only do Inquiry and not Test Unit Ready in case the
	 * SSA is reserved. If we did a TUR then it would fail but
	 * Inquiry executes even on a reserved unit.
	 *
	 */

	i = pln_execute_cmd(pln, SCMD_INQUIRY, 0, 0,
		(caddr_t)inquiry, sizeof (struct p_inquiry),
		KM_NOSLEEP);
	if (i == 0) {
		P_E_PRINTF((dip, "inquiry failed\n"));
		goto done;
	}

	/*
	 * Verify inquiry information is reasonable
	 */
	if (inquiry->inq_dtype != DTYPE_PROCESSOR) {
		P_E_PRINTF((dip,
		"pln_probe: WARNING - invalid Device Type Modifier %d\n",
		inquiry->inq_qual));
	}
	if (strncmp(inquiry->inq_vid, "SUN     ", 8) != 0) {
		P_E_PRINTF((dip,
		"pln_probe: WARNING - Invalid vendor field \n"));
		for (i = 0; i < 8; i++) {
		    P_E_PRINTF((dip, "%c",
		    inquiry->inq_vid[i]));
		}
	}


	/*
	 * We have managed to probe successfully.
	 */
	rval = DDI_PROBE_SUCCESS;

done:
	/*
	 * Clean everything up
	 */
	if (pln) {
		pln_softstate_free(dip, pln);
	}
	if (inquiry) {
		kmem_free((char *)inquiry, sizeof (struct p_inquiry));
	}


	return (rval);
}


static int
pln_attach(
	dev_info_t		*dip,
	ddi_attach_cmd_t	cmd)
{
	struct pln		*pln;
	struct fc_transport	*fc;
	struct pln_scsi_cmd		*sp;

	P_1_PRINTF((dip, "pln_attach:\n"));

	switch (cmd) {
	case DDI_ATTACH:
		P_PA_PRINTF((dip, "attaching instance 0x%x\n",
			ddi_get_instance(dip)));
		if ((pln = pln_softstate_alloc(dip, KM_SLEEP)) == NULL) {
		    P_E_PRINTF((dip, "pln_attach: softstate alloc failed\n"));
		    return (DDI_FAILURE);
		}

		/*
		 * Attach this instance of the hba
		 *
		 * We only do scsi_hba_attach in pln_attach not pln_probe
		 * because pointer to parents transport structure gets modified
		 * by scsi_hba_attach.
		 */
		if (scsi_hba_attach(dip, pln->pln_fc_tran->fc_dmalimp,
				pln->pln_tran, SCSI_HBA_TRAN_CLONE, NULL) !=
				DDI_SUCCESS) {
			P_E_PRINTF((dip, "attach: scsi_hba_attach failed\n"));
			pln_softstate_free(pln->pln_dip, pln);
			return (DDI_FAILURE);
		}

		/*
		 * Link pln into the list of pln structures
		 */
		mutex_enter(&pln_softc_mutex);

		if (pln_softc == (struct pln *)NULL) {
			pln_softc = pln;
		} else {
			struct pln	*p = pln_softc;
			while (p->pln_next != NULL) {
				p = p->pln_next;
			}
			p->pln_next = pln;
		}
		pln->pln_next = (struct pln *)NULL;

		mutex_exit(&pln_softc_mutex);

		/*
		 * Get some iopb space for the fcp cmd/rsp packets
		 */
		if (!pln_cr_init(pln)) {
			scsi_hba_detach(dip);
			pln_softstate_unlink(pln);
			pln_softstate_free(pln->pln_dip, pln);
			return (DDI_FAILURE);
		}

		/*
		 * Get the pluto configuration info.  This
		 * is used to build the pln_disk structure for each
		 * possible disk on the pluto.
		 */
		if (pln_build_disk_state(pln, KM_NOSLEEP)) {
			scsi_hba_detach(dip);
			pln_softstate_unlink(pln);
			pln_softstate_free(pln->pln_dip, pln);
			return (DDI_FAILURE);
		}

		/*
		 * Register an unsolicited cmd callback
		 */
		P_PA_PRINTF((dip, "pln_attach: uc register\n"));
		fc = pln->pln_fc_tran;
		pln->pln_uc_cookie = fc->fc_uc_register(fc->fc_cookie,
			TYPE_SCSI_FCP,
			pln_uc_callback, (void *) pln);
		if (pln->pln_uc_cookie == NULL) {
			scsi_hba_detach(dip);
			pln_softstate_unlink(pln);
			pln_softstate_free(pln->pln_dip, pln);
			return (DDI_FAILURE);
		}

		/*
		 * Register the routine to handle port state changes
		 */
		pln->pln_statec_cookie = fc->fc_statec_register(fc->fc_cookie,
			pln_statec_callback, (void *) pln);
		if (pln->pln_statec_cookie == NULL) {
			scsi_hba_detach(dip);
			pln_softstate_unlink(pln);
			pln_softstate_free(pln->pln_dip, pln);
			return (DDI_FAILURE);
		}

		/*
		 * Grab a couple of fc packet structures for error recovery
		 */
		if ((sp = (struct pln_scsi_cmd *)
			pln_scsi_pktalloc(&pln->pln_scsi_addr,
			sizeof (union scsi_cdb),
			sizeof (struct scsi_arq_status),
			NULL, SLEEP_FUNC, NULL)) == NULL) {
			scsi_hba_detach(dip);
			pln_softstate_unlink(pln);
			pln_softstate_free(pln->pln_dip, pln);
			return (DDI_FAILURE);
		}
		pln->pkt_offline = (struct pln_fc_pkt *)sp->cmd_fc_pkt;

		if ((sp = (struct pln_scsi_cmd *)
			pln_scsi_pktalloc(&pln->pln_scsi_addr,
			sizeof (union scsi_cdb),
			sizeof (struct scsi_arq_status),
			NULL, SLEEP_FUNC, NULL)) == NULL) {
			scsi_hba_detach(dip);
			pln_softstate_unlink(pln);
			pln_softstate_free(pln->pln_dip, pln);
			return (DDI_FAILURE);
		}
		pln->pkt_reset = (struct pln_fc_pkt *)sp->cmd_fc_pkt;


		P_1_PRINTF((dip, "pln_attach: OK\n\n"));

		/*
		 * Probe pln_ctlr
		 */
		if (pln_ctlr_probe(dip) != DDI_SUCCESS) {
			scsi_hba_detach(dip);
			pln_softstate_unlink(pln);
			pln_softstate_free(pln->pln_dip, pln);
			return (DDI_FAILURE);
		}

		/*
		 * Attach pln_ctlr
		 */
		if (pln_ctlr_attach(dip, pln) != DDI_SUCCESS) {
			scsi_hba_detach(dip);
			pln_softstate_unlink(pln);
			pln_softstate_free(pln->pln_dip, pln);
			return (DDI_FAILURE);
		}

		/*
		 * Start the callback thread.
		 *
		 * The purpose of this thread is to limit the amount of
		 * time we spend in the fc transport (soc) interrupt
		 * thread.
		 */

		if ((pln->pln_callback_thread = thread_create((caddr_t)NULL, 0,
		    pln_callback, (caddr_t)pln, 0,
		    &p0, TS_RUN, v.v_maxsyspri - 2)) == NULL) {
			cmn_err(CE_NOTE,
				"pln%d: attach failed:"
				"could not create callback thread",
				ddi_get_instance(dip));
			scsi_hba_detach(dip);
			pln_softstate_unlink(pln);
			pln_softstate_free(pln->pln_dip, pln);
			return (DDI_FAILURE);

		}


		/*
		 * Start off watchdog now we are fully initialized
		 */
		mutex_enter(&pln_softc_mutex);
		if (!pln_watchdog_init) {
			pln_watchdog_init = 1;
			mutex_exit(&pln_softc_mutex);

			pln_watchdog_tick = drv_usectohz((clock_t)1000000);
			pln_watchdog_id = timeout(pln_watch,
				(caddr_t)0, pln_watchdog_tick);
			P_PA_PRINTF((dip, "pln_attach: watchdog ok\n"));

			mutex_enter(&pln_softc_mutex);
		}

		/* Indicate we're attached */
		pln->pln_ref_cnt++;
		mutex_exit(&pln_softc_mutex);

		ddi_report_dev(dip);
		return (DDI_SUCCESS);

	default:
		P_E_PRINTF((dip, "pln_attach: unknown cmd 0x%x\n", cmd));
		return (DDI_FAILURE);
	}
}


/*
 * PLEASE NOTE:  detach operations are not yet supported!
 * The code within the PLN_DETACH #ifdefs is incomplete!
 */
/*ARGSUSED*/
static int
pln_detach(
	dev_info_t		*dip,
	ddi_detach_cmd_t	cmd)
{
#ifdef	PLN_DETACH
	struct pln		*pln;
	int			i;
#endif	PLN_DETACH

	switch (cmd) {
	case DDI_DETACH:
#ifdef	PLN_DETACH
		P_PA_PRINTF((dip, "detaching instance %d\n",
			ddi_get_instance(dip)));

		/*
		 * Find the pln structure corresponding to this dip
		 */
		mutex_enter(&pln_softc_mutex);
		for (pln = pln_softc; pln && (pln->pln_dip != dip);
			pln = pln->pln_next);
		pln->pln_ref_cnt--;
		mutex_exit(&pln_softc_mutex);

		if (!pln)
		    return (DDI_FAILURE);

		/*
		 * Unlink pln from the list of pln's.
		 */
		pln_softstate_unlink(pln);

		/*
		 * Kill off the watchdog if we're the last pln
		 */
		mutex_enter(&pln_softc_mutex);
		if (pln_softc == NULL) {
			pln_watchdog_init = 0;
			i = pln_watchdog_id;
			mutex_exit(&pln_softc_mutex);
			P_PA_PRINTF((dip, "pln_detach: untimeout\n"));
			(void) untimeout(i);
		} else {
			mutex_exit(&pln_softc_mutex);
		}

		/*
		 * Free all remaining memory allocated to this instance.
		 */
		pln_ctlr_detach(dip);

		P_PA_PRINTF((dip, "pln_detach: softstate free\n"));
		pln_softstate_free(dip, pln);

		P_PA_PRINTF((dip, "pln_detach: ok\n"));
		return (DDI_SUCCESS);

#else	PLN_DETACH
#ifdef	lint
		pln_watchdog_id = pln_watchdog_id;
#endif	lint
#endif	PLN_DETACH
	/* FALLTHROUGH */
	default:
		return (DDI_FAILURE);
	}
}


/*
 * pln_softstate_alloc() - build the structures we'll use for
 * an instance of pln
 */
static struct pln *
pln_softstate_alloc(
	dev_info_t		*dip,
	int			sleep_flag)
{
	struct pln		*pln;
	char			name[32];
	struct pln_address	*addr;
	struct scsi_address	*saddr;
#ifdef	PLN_LOCK_STATS
	int			i;
#endif	PLN_LOCK_STATS

#ifndef	ON1093
	scsi_hba_tran_t		*tran;
#endif	/* ON1093 */


	/*
	 * Allocate softc information.
	 */
	pln = (struct pln *)kmem_zalloc(sizeof (struct pln), sleep_flag);
	if (pln == (struct pln *)NULL) {
		P_E_PRINTF((dip, "attach: pln alloc failed\n"));
		return (NULL);
	}


	pln->pln_fc_tran =
		(struct fc_transport *)ddi_get_driver_private(dip);

#ifdef	PLN_LOCK_STATS
	i = lock_stats;
	lock_stats |= pln_lock_stats;
#endif	PLN_LOCK_STATS

	sprintf(name, "pln%d mutex", ddi_get_instance(dip));
	mutex_init(&pln->pln_mutex, name, MUTEX_DRIVER, pln->pln_iblock);
	cv_init(&pln->pln_private_cv, "pln_private_cv",
		CV_DRIVER, NULL);

#ifdef	PLN_LOCK_STATS
	lock_stats = i;
#endif	PLN_LOCK_STATS

#ifdef	ON1093
	/*
	 * Fill in the scsi_transport structure that SCSA will use
	 * to get us to do something.
	 */
	pln->pln_tran.tran_dev		= dip;
	pln->pln_tran.tran_lkarg	= (void *) pln->pln_iblock;

	pln->pln_tran.tran_start	= pln_start;
	pln->pln_tran.tran_abort	= pln_abort;
	pln->pln_tran.tran_reset	= pln_reset;
	pln->pln_tran.tran_getcap	= pln_getcap;
	pln->pln_tran.tran_setcap	= pln_setcap;
	pln->pln_tran.tran_pktalloc	= (struct scsi_pkt *(*)())
						pln_scsi_pktalloc;
	pln->pln_tran.tran_dmaget	= scsi_impl_dmaget;
	pln->pln_tran.tran_pktfree	= pln_scsi_pktfree;
	pln->pln_tran.tran_dmafree	= scsi_impl_dmafree;
#else

	/*
	 * Allocate and initialize transport structure
	 */
	tran = scsi_hba_tran_alloc(dip, 0);
	if (tran == NULL) {
		P_E_PRINTF((dip, "attach: hba_tran_alloc failed\n"));
		mutex_destroy(&pln->pln_mutex);
		cv_destroy(&pln->pln_private_cv);
		kmem_free(pln, sizeof (struct pln));
		return (NULL);
	}

	pln->pln_tran			= tran;
	pln->pln_dip			= dip;

	tran->tran_hba_private		= pln;
	/*
	 * Set pointer to controller address space for internal commands
	 */
	tran->tran_tgt_private		= &pln->pln_ctlr_addr;

	tran->tran_tgt_init		= pln_scsi_tgt_init;
	tran->tran_tgt_probe		= pln_scsi_tgt_probe;
	tran->tran_tgt_free		= pln_scsi_tgt_free;

	tran->tran_start		= pln_start;
	tran->tran_abort		= pln_abort;
	tran->tran_reset		= pln_reset;
	tran->tran_getcap		= pln_getcap;
	tran->tran_setcap		= pln_setcap;
	tran->tran_init_pkt		= pln_scsi_init_pkt;
	tran->tran_destroy_pkt		= pln_scsi_destroy_pkt;
	tran->tran_dmafree		= pln_scsi_dmafree;

#endif	/* ON1093 */
	/*
	 * Allocate and initialize resources for pln:ctlr
	 */
	pln->pln_ctlr = (struct pln_disk *)
		kmem_zalloc(sizeof (struct pln_disk), sleep_flag);
	if (pln->pln_ctlr == NULL) {
#ifndef	ON1093
		scsi_hba_tran_free(tran);
#endif	/* ON1093 */
		cv_destroy(&pln->pln_private_cv);
		mutex_destroy(&pln->pln_mutex);
		P_E_PRINTF((dip, "attach: pln_disk alloc failed\n"));
		return (NULL);
	}
	pln->pln_ctlr->pd_dip = dip;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(pln->pln_state))
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(pln->pln_maxcmds))

	/*
	 * Build the address of the pluto controller itself.
	 */
	addr = &pln->pln_ctlr_addr;
	addr->pln_entity = PLN_ENTITY_CONTROLLER;
	addr->pln_port = 0;
	addr->pln_target = 0;
	addr->pln_reserved = 0;
	saddr = &pln->pln_scsi_addr;
#ifdef	ON1093
	saddr->a_cookie = (int)pln;
	saddr->a_addr_ptr = &pln->pln_ctlr_addr;
#else	ON1093
	saddr->a_hba_tran = pln->pln_tran;
#endif	/* ON1093 */

#ifdef	PLN_LOCK_STATS
	i = lock_stats;
	lock_stats |= pln_lock_stats;
#endif	PLN_LOCK_STATS
	(void) sprintf(name, "pln%d ctlr", ddi_get_instance(dip));
	mutex_init(&pln->pln_ctlr->pd_pkt_alloc_mutex, name, MUTEX_DRIVER,
		pln->pln_iblock);
#ifdef	PLN_LOCK_STATS
	lock_stats = i;
#endif	PLN_LOCK_STATS

	/*
	 * initialize mutex for throttling
	 */
	pln->pln_ncmd_ref = 0;
	pln->pln_maxcmds = PLN_MAX_DEFAULT;
#ifdef	PLN_LOCK_STATS
	i = lock_stats;
	lock_stats |= pln_lock_stats;
#endif	PLN_LOCK_STATS
	sprintf(name, "pln%d throttle", ddi_get_instance(dip));
	mutex_init(&pln->pln_throttle_mtx, name, MUTEX_DRIVER, pln->pln_iblock);
#ifdef	PLN_LOCK_STATS
	lock_stats = i;
#endif	PLN_LOCK_STATS

#ifdef	PLN_LOCK_STATS
	i = lock_stats;
	lock_stats |= pln_lock_stats;
#endif	PLN_LOCK_STATS
	sprintf(name, "pln%d state", ddi_get_instance(dip));
	mutex_init(&pln->pln_state_mutex, name, MUTEX_DRIVER, pln->pln_iblock);
#ifdef	PLN_LOCK_STATS
	lock_stats = i;
#endif	PLN_LOCK_STATS

	/*
	 * Initialize the pln device state
	 */
	pln->pln_state = PLN_STATE_ONLINE;
	pln->pln_en_online_timeout = pln_en_online_timeout;

	/*
	 * Initialize callback thread mutex and cv
	 */
	mutex_init(&pln->pln_callback_mutex, "pln callback mutex",
		MUTEX_DRIVER, pln->pln_iblock);
	cv_init(&pln->pln_callback_cv, "pln callback cv",
		CV_DRIVER, NULL);


	/*
	 * Initialize the fcp command/response pool mutex
	 */
	sprintf(name, "pln%d cmd/rsp pool", ddi_get_instance(dip));
	mutex_init(&pln->pln_cr_mutex, name, MUTEX_DRIVER, pln->pln_iblock);

	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(pln->pln_state))
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(pln->pln_maxcmds))

	return (pln);
}


/*
 * pln_softstate_free() - release resources associated with a pln device
 */
/*ARGSUSED*/
static void
pln_softstate_free(
	dev_info_t		*dip,
	struct pln		*pln)
{
	struct fc_transport	*fc;
	pln_cr_pool_t		*cp;

	/*
	 * Delete callback routines
	 */
	fc = pln->pln_fc_tran;
	if (pln->pln_uc_cookie)
	    fc->fc_uc_unregister(fc->fc_cookie, pln->pln_uc_cookie);
	if (pln->pln_statec_cookie)
	    fc->fc_statec_unregister(fc->fc_cookie, pln->pln_statec_cookie);

	if (pln->pkt_offline)
	    pln_fpacket_dispose(pln, pln->pkt_offline);
	if (pln->pkt_reset)
	    pln_fpacket_dispose(pln, pln->pkt_reset);

	/*
	 * Free pln_ctlr and disk mutex and state
	 */
	pln_fpacket_dispose_all(pln, pln->pln_ctlr);
	pln_free_disk_state(pln);
	mutex_destroy(&pln->pln_ctlr->pd_pkt_alloc_mutex);
	if (pln->pln_disk_mtx_init)
	    pln_destroy_disk_state_mutexes(pln);
	kmem_free((void *) pln->pln_ctlr, sizeof (struct pln_disk));

	/*
	 * Get rid of the pools for fcp commands/responses
	 */
	cp = &pln->pln_cmd_pool;
	if (cp->cmd_handle)
	    ddi_dma_free(cp->cmd_handle);

	if (cp->cmd_base)
	    ddi_iopb_free(cp->cmd_base);

	if (cp->rsp_handle)
	    ddi_dma_free(cp->rsp_handle);

	if (cp->rsp_base)
	    ddi_iopb_free(cp->rsp_base);

	/*
	 * Free mutexes/condition variables
	 */
	mutex_destroy(&pln->pln_mutex);
	mutex_destroy(&pln->pln_throttle_mtx);
	mutex_destroy(&pln->pln_state_mutex);
	cv_destroy(&pln->pln_private_cv);
	mutex_destroy(&pln->pln_cr_mutex);
	mutex_destroy(&pln->pln_callback_mutex);
	cv_destroy(&pln->pln_callback_cv);

#ifdef	ON1093
	scsi_hba_tran_free(pln->pln_tran);
#endif	/* ON1093 */

	/*
	 * Free the pln structure itself
	 */
	kmem_free((caddr_t)pln, sizeof (struct pln));
}

/*
 * Delete a pln instance from the list of controllers
 */
static void
pln_softstate_unlink(
	struct pln		*pln)
{
	int i = 0;

	mutex_enter(&pln_softc_mutex);

	/*
	 * If someone is looking at this structure now, we'll spin until
	 * they are done
	 */
	while (pln->pln_ref_cnt) {
	    mutex_exit(&pln_softc_mutex);

#ifndef	__lock_lint
	    while (pln->pln_ref_cnt)
		i++;
#endif	__lock_lint

	    mutex_enter(&pln_softc_mutex);
	}

	if (pln == pln_softc) {
		pln_softc = pln->pln_next;
	} else {
		struct pln	*p = pln_softc;
		struct pln	*v = NULL;
		while (p != pln) {
			ASSERT(p != NULL);
			v = p;
			p = p->pln_next;
		}
		ASSERT(v != NULL);
		v->pln_next = p->pln_next;
	}

	mutex_exit(&pln_softc_mutex);
}


/*
 * Called by target driver to start a command
 */
static int
pln_start(
#ifdef	ON1093
	struct pln_scsi_cmd			*sp)
#else	ON1093
	struct scsi_address		*ap,
	struct scsi_pkt			*pkt)
#endif	/* ON1093 */
{
	struct pln			*pln;
	int				rval;
	struct pln_fc_pkt		*fp;
	int				i;

#ifndef	ON1093
	struct pln_scsi_cmd	*sp = (struct pln_scsi_cmd *)pkt;
#endif	/* ON1093 */
	/*
	 * Get the pln instance and fc4 address out of the pkt
	 */
#ifdef	ON1093
	pln = (struct pln *)sp->cmd_pkt.pkt_address.a_cookie;
#else	ON1093
	pln = ADDR2PLN(ap);
#endif	/* ON1093 */
	fp = (struct pln_fc_pkt *)sp->cmd_fc_pkt;

	ASSERT(&fp->fp_scsi_cmd == sp);
	ASSERT(fp->fp_pln == pln);
#ifndef	__lock_lint
	ASSERT(fp->fp_state == FP_STATE_IDLE);
#endif	__lock_lint

	fp->fp_retry_cnt = PLN_NRETRIES;

	/*
	 * Basic packet initialization and sanity checking
	 */
	if ((rval = pln_init_scsi_pkt(pln, sp)) != TRAN_ACCEPT) {
		return (rval);
	}

	/*
	 * Get FCP cmd/rsp packets (iopbs)
	 */
	i = (sp->cmd_pkt.pkt_flags & FLAG_NOINTR);
	fp->fp_cr_callback = (i) ? NULL : pln_start_fcp;
	/*
	 * There are 3 return values possible from cr_alloc
	 * Here we fall through if we get an fcp structure from
	 * the free list.
	 */
	if (!(rval = pln_cr_alloc(pln, fp))) {
	    if (i)
		return (TRAN_BADPKT);
	    return (TRAN_ACCEPT);
	} else if (rval < 0)
		return (TRAN_FATAL_ERROR);

	/*
	 * Build the resources necessary to transport the pkt
	 */
	rval = pln_prepare_fc_packet(pln, sp, fp);
	if (rval == TRAN_ACCEPT) {
		rval = pln_local_start(pln, sp, fp);
	}
	if (fp->fp_cmd && (rval != TRAN_ACCEPT)) {
		pln_cr_free(pln, fp);
	}

	return (rval);
}


/*
 * Start a command.  This can be called either to start the command initially,
 * or to retry in case of link errors.
 *
 */
/* ARGSUSED */
static int
pln_local_start(
	struct pln		*pln,
	struct pln_scsi_cmd	*sp,
	struct pln_fc_pkt	*fp)
{
	int			pkt_time;

	P_X_PRINTF((pln->pln_dip, "pln_local_start:\n"));

	ASSERT(sp->cmd_fc_pkt);

	/*
	 * Get the pln_fc_packet
	 */
	fp = (struct pln_fc_pkt *)sp->cmd_fc_pkt;

	/* XXX -- add R_A_TOV to the timeout value */
	pkt_time = fp->fp_scsi_cmd.cmd_pkt.pkt_time;

	fp->fp_timeout = (pkt_time) ?
			    pln_watchdog_time + pkt_time + PLN_TIMEOUT_PAD
			    : 0;


	/*
	 * Is the link dead?  If so, fail the command immediately,
	 * so that it doesn't take so long for commands to fail
	 * (otherwise detected at the poll rate of pln_watch() or pln_dopoll())
	 */
	if (pln->pln_state & PLN_STATE_LINK_DOWN) {
		return (TRAN_FATAL_ERROR);
	}

	/*
	 * Polled command treatment
	 */
	if (sp->cmd_pkt.pkt_flags & FLAG_NOINTR) {
	    return (pln_dopoll(pln, fp));
	}


	if ((pln->pln_state == PLN_STATE_ONLINE) &&
		(!pln->pln_throttle_flag) &&
		((pln->pln_ncmd_ref - pln->pln_maxcmds) < 0)) {
#ifndef	__lock_lint
	    fp->fp_state = FP_STATE_ISSUED;
#endif	__lock_lint

	    /* Not mutex protected, so this value is just a reference */
	    pln->pln_ncmd_ref++;

	} else {
	/*
	 * Throttling...
	 * Later, when looking for fp's that are in the "on hold" state,
	 * we'll scan the list of commands only if the pd_onhold_flag
	 * is set.  To avoid missing an "on hold" fp, then, we need
	 * make sure we set the flag *after* marking the state.
	 */
#ifndef	__lock_lint
	    fp->fp_state = FP_STATE_ONHOLD;
#endif	__lock_lint
	    fp->fp_pd->pd_onhold_flag = 1;
	    pln->pln_throttle_flag = 1;
	    return (TRAN_ACCEPT);
	}

	/*
	 * Run the command
	 */
	switch (pln_startcmd(pln, fp)) {

		case FC_TRANSPORT_SUCCESS:
			return (TRAN_ACCEPT);

		case FC_TRANSPORT_QFULL:
		case FC_TRANSPORT_UNAVAIL:
#ifndef	__lock_lint
			fp->fp_state = FP_STATE_ONHOLD;
#endif	__lock_lint
			fp->fp_pd->pd_onhold_flag = 1;
			pln->pln_throttle_flag = 1;
			return (TRAN_ACCEPT);

		case FC_TRANSPORT_FAILURE:
		case FC_TRANSPORT_TIMEOUT:
#ifndef	__lock_lint
			fp->fp_state = FP_STATE_IDLE;
#endif	__lock_lint
			pln->pln_ncmd_ref--;
			return (TRAN_BADPKT);

		default:
			pln_disp_err(pln->pln_dip, CE_PANIC,
				"Invalid transport status\n");
		_NOTE(NOT_REACHED);
		/* NOTREACHED */
	}

}


/*
 * Fire off the command to the lower level driver.
 */
static int
pln_startcmd(
	struct pln		*pln,
	struct pln_fc_pkt	*fp)
{
	struct fc_transport	*fc;
	int			rval;

	P_X_PRINTF((pln->pln_dip, "pln_startcmd\n"));

	fc = pln->pln_fc_tran;
	rval = fc->fc_transport(fp->fp_pkt, FC_SLEEP);

	P_X_PRINTF((pln->pln_dip, "fc_transport returns %d\n", rval));

	if (rval == FC_TRANSPORT_FAILURE) {
		P_E_PRINTF((pln->pln_dip, "pln: fc_transport failure\n"));
	}

	return (rval);
}

/*
 * Run a command in polled mode
 */
static int
pln_dopoll(
	struct pln		*pln,
	struct pln_fc_pkt	*fp)
{
	struct fc_transport	*fc = pln->pln_fc_tran;
	int			timer;
	int			timeout_flag;

	for (;;) {
	    pln->pln_ncmd_ref++;
	    timeout_flag = 0;
#ifndef	__lock_lint
	    fp->fp_state = FP_STATE_ISSUED;
#endif	__lock_lint

	    switch (pln_startcmd(pln, fp)) {

		case FC_TRANSPORT_SUCCESS:
		    pln_cmd_callback(fp->fp_pkt);
#ifndef	__lock_lint
		    if (fp->fp_state == FP_STATE_IDLE)
			return (TRAN_ACCEPT);
		    if ((fp->fp_state == FP_STATE_PRETRY) &&
			(fp->fp_pkt->fc_pkt_status != FC_STATUS_ERR_OFFLINE)) {
			pln_transport_offline(pln, PLN_STATE_ONLINE, 1);
		    }
		    fp->fp_state = FP_STATE_IDLE;
#endif	__lock_lint

		/* FALLTHROUGH */
		case FC_TRANSPORT_QFULL:
		case FC_TRANSPORT_UNAVAIL:
		    pln->pln_ncmd_ref--;
		    break;

		case FC_TRANSPORT_TIMEOUT:
		    pln_transport_offline(pln, PLN_STATE_ONLINE, 1);
		    timeout_flag = 1;
		    pln->pln_ncmd_ref--;
		    break;

		case FC_TRANSPORT_FAILURE:
		    pln->pln_ncmd_ref--;
		    return (TRAN_BADPKT);

		default:
		    pln_disp_err(pln->pln_dip, CE_PANIC,
			    "Invalid transport status\n");
		_NOTE(NOT_REACHED);
		/* NOTREACHED */
	    }

	/*
	 * Error recovery loop
	 */
	    timer = (PLN_ONLINE_TIMEOUT * 1000000) / PLN_POLL_DELAY;
	    do {
		drv_usecwait(PLN_POLL_DELAY);
		fc->fc_interface_poll(fc->fc_cookie);

		/* Check for a timeout waiting for an online */
		if (pln->pln_en_online_timeout && (--timer <= 0)) {
#ifndef	__lock_lint
		    fp->fp_state = FP_STATE_ISSUED;
#endif	__lock_lint
		    pln->pln_ncmd_ref++;
		    fp->fp_pkt->fc_pkt_status = (timeout_flag) ?
				FC_STATUS_TIMEOUT : FC_STATUS_ERR_OFFLINE;
		    fp->fp_retry_cnt = 1;
		    pln_cmd_callback(fp->fp_pkt);
		    return (TRAN_ACCEPT);
		}
	    } while (!(fp->fp_pkt->fc_pkt_flags & FCFLAG_COMPLETE) ||
			!(pln->pln_state & PLN_STATE_ONLINE));
	}
}

/*
 * Start a command after having waited for FCP cmd/response pkts
 */
static void
pln_start_fcp(
	pln_fc_pkt_t		*fp)
{
	struct pln_scsi_cmd	*sp;
	struct pln		*pln;
	int			failure = 0;

	/*
	 * Build the resources necessary to transport the pkt
	 */
	sp = &fp->fp_scsi_cmd;
	pln = fp->fp_pln;
	if (pln_prepare_fc_packet(pln, sp, fp) != TRAN_ACCEPT)
	    failure++;

	if (!failure)
	    if (pln_local_start(pln, sp, fp) != TRAN_ACCEPT)
		failure++;

	if (failure) {

	    /* Failed to start, fake up some status information */
	    sp->cmd_pkt.pkt_state = 0;
	    sp->cmd_pkt.pkt_statistics = 0;

	    pln_build_extended_sense(sp, KEY_HARDWARE_ERROR);

	    pln_cr_free(pln, fp);

	    if (sp->cmd_pkt.pkt_comp) {
		pln_add_callback(pln, sp);
	    }
	}
}

/*
 * Command completion callback
 */
static void
pln_cmd_callback(
	struct fc_packet	*fpkt)
{
	struct pln_fc_pkt	*fp;
	struct pln		*pln;
	struct pln_scsi_cmd	*sp;
	struct fcp_rsp		*rsp;
	struct scsi_arq_status	*arq;
	struct fcp_scsi_bus_err *bep;
	int			i;
	u_char			key = KEY_RESERVED;
	caddr_t			msg1 = NULL;
	char			msg[80];
	int			new_state = FP_STATE_IDLE;

	fp = (struct pln_fc_pkt *)fpkt->fc_pkt_private;
	sp = &fp->fp_scsi_cmd;
	pln = fp->fp_pln;

	ASSERT(fp == (struct pln_fc_pkt *)sp->cmd_fc_pkt);

#ifndef	__lock_lint
	ASSERT(fp->fp_state == FP_STATE_ISSUED);
#endif	__lock_lint

	P_X_PRINTF((pln->pln_dip,
		"pln_cmd_callback:  Transport status=0x%x  statistics=0x%x\n",
		fpkt->fc_pkt_status, fpkt->fc_pkt_statistics));

	/*
	 * Decode fc transport (SOC) status
	 * and map into reason codes
	 *
	 * If Transport ok then use scsi status to
	 * update scsi packet information
	 */
	switch (fpkt->fc_pkt_status) {
	case FC_STATUS_OK:
		/*
		 * At least command came back from SOC OK
		 * May have had a transport error in SSA
		 * or command may have failed in the disk
		 * in which case we should have scsi sense data.
		 */

		/* Default to command completed normally */
		sp->cmd_pkt.pkt_reason = CMD_CMPLT;

		i = ddi_dma_sync(pln->pln_cmd_pool.rsp_handle,
			(caddr_t)fp->fp_rsp - pln->pln_cmd_pool.rsp_base,
			(u_int)(sizeof (struct pln_rsp)),
			DDI_DMA_SYNC_FORKERNEL);
		if (i != DDI_SUCCESS) {
			P_E_PRINTF((pln->pln_dip,
				"ddi_dma_sync failed (rsp)\n"));
			sp->cmd_pkt.pkt_reason = CMD_STS_OVR;
			pln_build_extended_sense(sp, (u_char)KEY_NO_SENSE);
			break;
		}

		/*
		 * Ptr to the FCP response area
		 */
		rsp = (struct fcp_rsp *)fp->fp_rsp;

		/*
		 * Update the command status
		 */

		/*
		 * Default to all OK which is what we report
		 * unless there was a problem.
		 */
		sp->cmd_pkt.pkt_state = STATE_GOT_BUS |
			STATE_GOT_TARGET |
			STATE_SENT_CMD |
			STATE_GOT_STATUS;
		if (sp->cmd_flags & P_CFLAG_DMAVALID) {
			sp->cmd_pkt.pkt_state |= STATE_XFERRED_DATA;
		}

		/*
		 * Check to see if we got a status byte but
		 * no Request Sense information.
		 * Can happen on busy or reservation conflict.
		 *
		 * Do it the old way for 1093 until we prove
		 * new way works on 1093!!!!
		 *
		 *
		 */
		if (sp->cmd_pkt.pkt_scbp && ((*(sp->cmd_pkt.pkt_scbp) =
			rsp->fcp_u.fcp_status.scsi_status) != STATUS_GOOD)) {
			    if (!rsp->fcp_u.fcp_status.rsp_len_set &&
			    !rsp->fcp_u.fcp_status.sense_len_set) {
#ifdef	ON1093
				pln_build_extended_sense(sp,
					(u_char)KEY_NO_SENSE);
#else	ON1093
				sp->cmd_pkt.pkt_state &= ~STATE_XFERRED_DATA;
				sp->cmd_pkt.pkt_resid = sp->cmd_dmacount;
				break;
#endif	ON1093
			    }
		}

		/*
		 * scsi pkt_statistics aren't particularly meaningful
		 * to a fibre channel interface
		 */
		sp->cmd_pkt.pkt_statistics = 0;

		/*
		 * Update the transfer resid, if appropriate
		 */
		sp->cmd_pkt.pkt_resid = 0;
		if (rsp->fcp_u.fcp_status.resid_len_set) {
			sp->cmd_pkt.pkt_resid = rsp->fcp_resid;
			P_X_PRINTF((pln->pln_dip,
			"All data NOT transfered: resid: 0x%x\n",
				rsp->fcp_resid));
		}

		/*
		 * Check to see if the SCSI command failed.
		 *
		 * If it did then update the request sense info
		 * and state.
		 *
		 * The target driver should always enable automatic
		 * request sense when interfacing to a Pluto...
		 */

		/*
		 * First see if we got a transport
		 * error in the SSA.  If so, we print a message,
		 * and then fake up an autosense response
		 * so that the drivers above us can interpret
		 * the error.
		 */
		if (rsp->fcp_u.fcp_status.rsp_len_set) {

		/*
		 * Transport information
		 */
		    bep = (struct fcp_scsi_bus_err *)
			    (&rsp->fcp_response_len +
			    1 +
			    rsp->fcp_sense_len);
		    switch (bep->rsp_info_type) {
		    case FCP_RSP_SCSI_BUS_ERR:
			switch (bep->isp_status) {
			    case FCP_RSP_CMD_COMPLETE:
				key = KEY_NO_SENSE;
				msg1 = "FCP_RSP_CMD_COMPLETE";
				break;
			    case FCP_RSP_CMD_INCOMPLETE:
				key = KEY_HARDWARE_ERROR;
				msg1 = "FCP_RSP_CMD_INCOMPLETE";
				break;
			    case FCP_RSP_CMD_DMA_ERR:
				key = KEY_HARDWARE_ERROR;
				msg1 = "FCP_RSP_CMD_DMA_ERR";
				break;
			    case FCP_RSP_CMD_TRAN_ERR:
				key = KEY_HARDWARE_ERROR;
				msg1 = "FCP_RSP_CMD_TRAN_ERR";
				break;
			    case FCP_RSP_CMD_RESET:
				key = KEY_HARDWARE_ERROR;
				msg1 = "FCP_RSP_CMD_RESET";
				break;
			    case FCP_RSP_CMD_ABORTED:
				key = KEY_ABORTED_COMMAND;
				msg1 = "FCP_RSP_CMD_ABORTED";
				break;
			    case FCP_RSP_CMD_TIMEOUT:
				key = KEY_HARDWARE_ERROR;
				msg1 = "FCP_RSP_CMD_TIMEOUT";
				break;
			    case FCP_RSP_CMD_OVERRUN:
				key = KEY_HARDWARE_ERROR;
				msg1 = "FCP_RSP_CMD_OVERRUN";
				break;
			    default:
				key = KEY_HARDWARE_ERROR;
				msg1 = "FCP_RSP_CMD: UNKNOWN";
				break;
			}
			break;
		    case FCP_RSP_SCSI_PORT_ERR:
			key = KEY_HARDWARE_ERROR;
			msg1 = "FCP_RSP_SCSI_PORT_ERR";
			break;
		    case FCP_RSP_SOC_ERR:
			key = KEY_HARDWARE_ERROR;
			msg1 = "FCP_RSP_SOC_ERR";
			break;
		    default:
			key = KEY_HARDWARE_ERROR;
			msg1 = "Response type: UNKNOWN";
			break;
		    }

		/*
		 * make state say no data transfered.
		 */
		    sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		}

		/*
		 * See if we got a SCSI error with sense data
		 */
		if (rsp->fcp_u.fcp_status.sense_len_set) {
		    u_char rqlen = min(rsp->fcp_sense_len,
			    sizeof (struct scsi_extended_sense));
		    caddr_t sense = (caddr_t)rsp +
			    sizeof (struct fcp_rsp);
		    if (sp->cmd_pkt.pkt_scbp)
			*(sp->cmd_pkt.pkt_scbp) = STATUS_CHECK;
#ifdef	PLNDEBUG
		    sprintf(msg, "Request Sense Info: len=0x%x\n", rqlen);
		    pln_disp_err(fp->fp_pd->pd_dip, CE_WARN, msg);
		    pln_dump("sense data: ",
			    (u_char *)sense, rqlen);
#endif	PLNDEBUG
		    if ((sp->cmd_senselen >= sizeof (struct scsi_arq_status)) &&
				(sp->cmd_pkt.pkt_scbp)) {
			/*
			 * Automatic Request Sense enabled.
			 */
			sp->cmd_pkt.pkt_state |= STATE_ARQ_DONE;

			arq = (struct scsi_arq_status *)
			    sp->cmd_pkt.pkt_scbp;
			/*
			 * copy out sense information
			 */
			bcopy(sense, (caddr_t)&arq->sts_sensedata,
				rqlen);
			arq->sts_rqpkt_resid =
				sizeof (struct scsi_extended_sense) -
					rqlen;
			/*
			 * Set up the flags for the auto request sense
			 * command like we really did it even though
			 * we didn't.
			 */
			*((u_char *)&arq->sts_rqpkt_status) = STATUS_GOOD;
			arq->sts_rqpkt_reason = 0;
			arq->sts_rqpkt_statistics = 0;
			arq->sts_rqpkt_state = STATE_GOT_BUS |
			STATE_GOT_TARGET |
			STATE_SENT_CMD |
			STATE_GOT_STATUS |
			STATE_ARQ_DONE |
			STATE_XFERRED_DATA;

			/* Make sure we don't overwrite the status below */
			key = KEY_RESERVED;
		    }
		}
		P_X_PRINTF((pln->pln_dip,
			"pln_cmd_callback: pkt_state: 0x%x\n",
			sp->cmd_pkt.pkt_state));
		break;

	case FC_STATUS_ERR_OFFLINE:
		/* Note that we've received an offline response */
		mutex_enter(&pln->pln_state_mutex);
		if (pln->pln_state == PLN_STATE_ONLINE)
		    pln->pln_state |= PLN_STATE_OFFLINE_RSP;

		mutex_exit(&pln->pln_state_mutex);

		if (--fp->fp_retry_cnt > 0) {

		    if (sp->cmd_pkt.pkt_flags & FLAG_NOINTR)
			new_state = FP_STATE_PRETRY;

		    /* Wait for a state change to online */
		    else
			new_state = FP_STATE_ONHOLD;
		} else {
		    msg1 = "Fibre Channel Offline";
		    key = KEY_HARDWARE_ERROR;
		    sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		}
		break;

	case FC_STATUS_MAX_XCHG_EXCEEDED:
		if (sp->cmd_pkt.pkt_flags & FLAG_NOINTR)
		    new_state = FP_STATE_PTHROT;

		else {
		    pln_throttle(pln);
		    new_state = FP_STATE_ONHOLD;
		}
		break;

	case FC_STATUS_P_RJT:
		if (!fpkt->fc_frame_resp) {
		    msg1 = "Received P_RJT status, but no header";
		} else if (
		((aFC2_RJT_PARAM *)&fpkt->fc_frame_resp->ro)->rjt_reason ==
			CANT_ESTABLISH_EXCHANGE) {

		    if (sp->cmd_pkt.pkt_flags & FLAG_NOINTR)
			new_state = FP_STATE_PTHROT;

		    /* Need to throttle... */
		    else {
			pln_throttle(pln);
			new_state = FP_STATE_ONHOLD;
		    }
		    break;
		} else {
		    msg1 = "Fibre Channel P_RJT";
		}
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_TIMEOUT:
		if (--fp->fp_retry_cnt > 0) {

		    if (sp->cmd_pkt.pkt_flags & FLAG_NOINTR)
			new_state = FP_STATE_PRETRY;

		    else {
			pln_transport_offline(pln, PLN_STATE_ONLINE, 0);
			new_state = FP_STATE_ONHOLD;
		    }
		} else {
		    msg1 = "Fibre Channel Timeout";
		    key = KEY_HARDWARE_ERROR;
		    sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		}
		break;

	case FC_STATUS_ERR_OVERRUN:
		msg1 = "CMD_DATA_OVR";
		sp->cmd_pkt.pkt_reason = CMD_DATA_OVR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_P_BSY:
		msg1 = "Fibre Channel P_BSY";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_UNKNOWN_CQ_TYPE:
		msg1 = "Unknown CQ type";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_BAD_SEG_CNT:
		msg1 = "Bad SEG CNT";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_BAD_XID:
		msg1 = "Fibre Channel Invalid X_ID";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_XCHG_BUSY:
		msg1 = "Fibre Channel Exchange Busy";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_INSUFFICIENT_CQES:
		msg1 = "Insufficient CQEs";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_ALLOC_FAIL:
		msg1 = "ALLOC FAIL";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_BAD_SID:
		msg1 = "Fibre Channel Invalid S_ID";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_NO_SEQ_INIT:
		msg1 = "Fibre Channel Seq Init Error";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	case FC_STATUS_ONLINE_TIMEOUT:
		msg1 = "Fibre Channel Online Timeout";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;

	default:
		msg1 = "Unknown FC Status";
		sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;
		key = KEY_HARDWARE_ERROR;
		break;
	}


	/*
	 * msg1 will be non-NULL if we've detected some sort of error
	 */
	if (msg1) {
	    sprintf(msg, "!Transport error:  %s", msg1);
	    pln_disp_err(fp->fp_pd->pd_dip, CE_WARN, msg);

	/*
	 * Build an auto-request-sense structure for our
	 * friends above us
	 */
	    if (key != KEY_RESERVED)
		pln_build_extended_sense(sp, key);
	}


	pln->pln_ncmd_ref--;

	/*
	 * Update the command state.  We must do this before checking
	 * the fp_timeout_flag, since pln_watch could be setting this
	 * flag at the same time as we get here.
	 */
#ifndef	__lock_lint
	fp->fp_state = new_state;
#endif	__lock_lint


	/*
	 * Check to see if we're waiting for the queue to empty following
	 * a timeout detection.
	 */
	if (fp->fp_timeout_flag) {
	    mutex_enter(&pln->pln_state_mutex);

	    if (fp->fp_timeout_flag) {
		fp->fp_timeout_flag = 0;

		if (--(pln->pln_timeout_count) == 0) {
		    pln->pln_state &= ~PLN_STATE_TIMEOUT;
		    if (pln->pln_state == PLN_STATE_ONLINE)
			P_W_PRINTF((pln->pln_dip,
			    "pln timeout recovery not required.\n"));
		}
	    }

	    mutex_exit(&pln->pln_state_mutex);
	}

	if (new_state == FP_STATE_IDLE) {

	/*
	 * Hand back the fcp cmd/rsp packets (iopbs)
	 */
	    pln_cr_free(pln, fp);

	/*
	 * Let the callback thread handle the callback.
	 */
	    if (sp->cmd_pkt.pkt_comp) {
		pln_add_callback(pln, sp);
	    }
	} else if (new_state == FP_STATE_ONHOLD) {
	    fp->fp_pd->pd_onhold_flag = 1;
	    pln->pln_throttle_flag = 1;
	}

	/*
	 * Try to start any "throttled" commands
	 */
	if (pln->pln_throttle_flag &&
		(pln->pln_state == PLN_STATE_ONLINE) &&
		((pln->pln_maxcmds - pln->pln_ncmd_ref) > PLN_THROTTLE_SWING)) {
	    pln_throttle_start(pln);
	}
}

static void
pln_add_callback(register struct pln *pln, struct pln_scsi_cmd *sp)
{
	mutex_enter(&pln->pln_callback_mutex);

	if (pln->pln_callback_tail != NULL) {
		pln->pln_callback_tail->cmd_next = sp;
	} else {
		pln->pln_callback_head = sp;
	}
	pln->pln_callback_tail = sp;
	cv_signal(&pln->pln_callback_cv);

	mutex_exit(&pln->pln_callback_mutex);
}

static void
pln_callback(register struct pln *pln)
{
	struct pln_scsi_cmd *sp1, *sp2;

	mutex_enter(&pln->pln_callback_mutex);

	for (;;) {
		if ((sp1 = pln->pln_callback_head) != NULL) {
			pln->pln_callback_head = NULL;
			pln->pln_callback_tail = NULL;
			mutex_exit(&pln->pln_callback_mutex);
			while (sp1 != NULL) {
				sp2 = sp1->cmd_next;
				sp1->cmd_next = NULL;
				(*sp1->cmd_pkt.pkt_comp)(sp1);
				sp1 = sp2;
			}
			mutex_enter(&pln->pln_callback_mutex);
		} else {
			cv_wait(&pln->pln_callback_cv,
			    &pln->pln_callback_mutex);
		}
	}
}

/*
 * Establish a new command throttle
 *
 * We dampen the response by waiting for a number of "suggestions"
 * as to the throttle position, recording the lowest value of
 * pln_ncmd_ref when a throttle request occurs into pln_throttle_ncmds.
 * When we've received PLN_MAX_T_RESP responses indicating we should
 * throttle, we'll set the throttle point [in pln_maxcmds] a bit
 * below the lowest mark we recorded.
 *
 * Values in pln_throttle_cnt, pln_ncmd_ref, and pln_throttle_ncmds are
 * approximate because they are not mutex protected.  This should be
 * okay, since we're just interested in getting a [dynamic] throttle
 * level that's roughly correct [we'll readjust soon, anyway...].
 */
static void
pln_throttle(struct pln *pln)
{
	if (pln->pln_throttle_cnt++ < PLN_MAX_T_RESP) {
	    if (!pln->pln_throttle_ncmds ||
			((pln->pln_ncmd_ref - pln->pln_throttle_ncmds) < 0))
		pln->pln_throttle_ncmds = pln->pln_ncmd_ref;
	    return;
	}

	mutex_enter(&pln->pln_throttle_mtx);
	if (pln->pln_throttle_cnt >= PLN_MAX_T_RESP) {
	    pln->pln_maxcmds = pln->pln_throttle_ncmds - PLN_THROTTLE_BACKOFF;
	    pln->pln_throttle_ncmds = 0;
	    pln->pln_throttle_cnt = 0;
	}
	mutex_exit(&pln->pln_throttle_mtx);
}

/*
 * Try to start any "throttled" commands
 *
 * Hopefully, most calls to this routine will cause an already-built
 * list of "throttled" commands per pln_disk (the pd_onhold_head linkage)
 * to be scanned, looking for commands to start.  When this list is
 * exhausted, we need to scan through the "inuse" list of all pln_disk
 * structures, constructing a new "on hold" list.
 */
static void
pln_throttle_start(struct pln *pln)
{
	struct pln_disk		*pd;
	pln_fc_pkt_t		*fp,
				*fpn;
	int			j;
	int			build_flag = 0;

	mutex_enter(&pln->pln_throttle_mtx);

	if (!pln->pln_throttle_flag) {
	    mutex_exit(&pln->pln_throttle_mtx);
	    return;
	}

	pln->pln_throttle_flag = 0;
	pd = pln->cur_throttle;
	j = 1;

	/*
	 * The outer loop looks through the already-built lists
	 * of "on hold" commands
	 */
	while ((pln->pln_maxcmds - pln->pln_ncmd_ref) > 0) {
	    if (pd == pln->cur_throttle) {
		if (!j) {

		/*
		 * This inner loop scans through all pln_disks
		 * to build new "on hold" command lists.
		 * Note that the "on hold" list for all pln_disks
		 * is protected by the pln_throttle_mtx.
		 */
		    if (!build_flag) {
			pd = pln->pln_ids[0];
			do {
			    if (pd->pd_onhold_flag) {
				pd->pd_onhold_flag = 0;
				fpn = NULL;
				mutex_enter(&pd->pd_pkt_alloc_mutex);
				for (fp = pd->pd_inuse_head; fp;
					fp = fp->fp_next) {
				    if (fp->fp_state == FP_STATE_ONHOLD) {
					j++;
					fp->fp_onhold = NULL;
					if (!fpn) {
					    fpn = fp;
					    pd->pd_onhold_head = fp;
					} else {
					    fpn->fp_onhold = fp;
					    fpn = fp;
					}
				    }
				}
				mutex_exit(&pd->pd_pkt_alloc_mutex);
			    }
			} while ((pd = pd->pd_next) != pln->pln_ids[0]);

			pd = pln->cur_throttle;
			build_flag = 1;
		    }

		    if (!j) {
			mutex_exit(&pln->pln_throttle_mtx);
			return;
		    }
		}
		j = 0;
	    }

	/*
	 * If there's a throttled command for this device,
	 * start it.
	 */
	    if ((fp = pd->pd_onhold_head) != NULL) {
		j++;
		pd->pd_onhold_head = fp->fp_onhold;
#ifndef	__lock_lint
		fp->fp_state = FP_STATE_IDLE;
#endif	__lock_lint
		mutex_exit(&pln->pln_throttle_mtx);
		pln_restart_one(pln, &fp->fp_scsi_cmd);
		mutex_enter(&pln->pln_throttle_mtx);
	    }
	    pd = pd->pd_next;
	}

	pln->pln_throttle_flag = 1;

	pln->cur_throttle = pd;

	mutex_exit(&pln->pln_throttle_mtx);
}


/*
 * Restart a single command.
 *
 * We use this routine to resume command processing after throttling of
 * the command or to retry after link error detection.
 */
static void
pln_restart_one(
	struct pln	*pln,
	struct pln_scsi_cmd	*sp)
{
	pln_fc_pkt_t	*fp;

	fp = (struct pln_fc_pkt *)sp->cmd_fc_pkt;

	if (pln_local_start(pln, sp, fp) != TRAN_ACCEPT) {

	/*
	 * Give up if we've encountered a hard transport
	 * failure...
	 */
	    sp->cmd_pkt.pkt_reason = CMD_TRAN_ERR;

#ifndef	__lock_lint
	    fp->fp_state = FP_STATE_IDLE;
#endif	__lock_lint
	    pln_cr_free(pln, fp);
	    if (sp->cmd_pkt.pkt_comp) {
		pln_add_callback(pln, sp);
	    }
	}
}

/*
 * Build an extended sense structure, if possible
 * (usually to pass to an upper level driver)
 *
 * Note that we specify "No additional sense information"
 * in the "Additional Sense" fields by leaving these fields
 * all zeroes...
 */
static void
pln_build_extended_sense(
	struct pln_scsi_cmd *sp,
	u_char key)
{
	struct scsi_arq_status		*arq;
	struct scsi_extended_sense 	*es;

	/*
	 * We should only be here because of fatal errors
	 */
	sp->cmd_pkt.pkt_state &= ~STATE_XFERRED_DATA;
	sp->cmd_pkt.pkt_resid = sp->cmd_dmacount;
	if (sp->cmd_pkt.pkt_scbp)
	    *(sp->cmd_pkt.pkt_scbp) = STATUS_CHECK;

	/*
	 * Make sure we can really build the sense structure
	 */
	if ((sp->cmd_senselen < sizeof (struct scsi_arq_status)) ||
		((arq = (struct scsi_arq_status *)sp->cmd_pkt.pkt_scbp)
			== NULL))
	    return;

	sp->cmd_pkt.pkt_state |= STATE_ARQ_DONE;
	sp->cmd_pkt.pkt_reason = CMD_CMPLT;

	arq->sts_rqpkt_resid = sp->cmd_senselen -
				sizeof (struct scsi_extended_sense);
	*((u_char *)&arq->sts_rqpkt_status) = STATUS_GOOD;
	arq->sts_rqpkt_reason = 0;
	arq->sts_rqpkt_statistics = 0;
	arq->sts_rqpkt_state = STATE_GOT_BUS | STATE_GOT_TARGET |
		STATE_SENT_CMD | STATE_GOT_STATUS | STATE_ARQ_DONE |
		STATE_XFERRED_DATA;
	es = &arq->sts_sensedata;
	bzero((caddr_t)es, sizeof (struct scsi_extended_sense));
	es->es_valid = 0;
	es->es_class = CLASS_EXTENDED_SENSE;
	es->es_key = key;
	es->es_info_1 = 0;
	es->es_info_2 = 0;
	es->es_info_3 = 0;
	es->es_info_4 = 0;
}

/*
 * Interface state changes are communicated back to us through
 * this routine
 */
static void
pln_statec_callback(
	void		*arg,
	fc_statec_t	msg)
{
	struct pln	*pln = (struct pln *)arg;
	pln_fc_pkt_t	*fp;
	struct pln	*p;
	struct pln_disk	*pd;

	/*
	 * Make sure we're still attached
	 */
	mutex_enter(&pln_softc_mutex);
	for (p = pln_softc; p; p = p->pln_next)
	    if (p == pln) break;

	/* If we're not completely attached, forget it */
	if (!pln->pln_ref_cnt) {
	    mutex_exit(&pln_softc_mutex);
	    return;
	}

	mutex_exit(&pln_softc_mutex);

	if (p != pln)
	    return;

	P_UC_PRINTF((pln->pln_dip, "pln: state change callback\n"));

	mutex_enter(&pln->pln_state_mutex);

	switch (msg) {
	    case FC_STATE_ONLINE:
		if (pln->pln_state == PLN_STATE_ONLINE) {
		    mutex_exit(&pln->pln_state_mutex);
		    return;
		}

		/*
		 * We're transitioning from offline to online, so
		 * reissue all commands in the "on hold" state
		 */
		pln->pln_state = PLN_STATE_ONLINE;
		mutex_exit(&pln->pln_state_mutex);

		pln_throttle_start(pln);

		return;

	    case FC_STATE_OFFLINE:
		/*
		 * The link went offline.  Set the timer in case
		 * we're timing out transitions back to an online
		 * state.
		 */
		pln->pln_state = PLN_STATE_OFFLINE;
		pln->pln_timer = pln_watchdog_time + pln_online_timeout;
		mutex_exit(&pln->pln_state_mutex);
		break;

	    case FC_STATE_RESET:
		/* PLN_STATE_RESET is for debugging */
		pln->pln_state |= PLN_STATE_RESET | PLN_STATE_OFFLINE;

		mutex_exit(&pln->pln_state_mutex);

		/*
		 * We assume the lower level has taken care of passing
		 * any completed commands back to us before returning
		 * this status.  Thus, we'll mark everything on the
		 * "in use" list as "idle", and
		 * wait for an online state change.
		 */
		pd = pln->pln_disk_list;
		do {
		    mutex_enter(&pd->pd_pkt_alloc_mutex);

		    for (fp = pd->pd_inuse_head; fp; fp = fp->fp_next)
			if (fp->fp_state == FP_STATE_ISSUED) {
			    fp->fp_state = FP_STATE_ONHOLD;
			    pd->pd_onhold_flag = 1;
			    pln->pln_throttle_flag = 1;
			    if (fp->fp_timeout_flag) {
				fp->fp_timeout_flag = 0;
				mutex_enter(&pln->pln_state_mutex);
				pln->pln_timeout_count--;
				mutex_exit(&pln->pln_state_mutex);
			    }
			}

		    mutex_exit(&pd->pd_pkt_alloc_mutex);

		} while ((pd = pd->pd_next) != pln->pln_disk_list);

		/*
		 * Reset state change indications from the lower level
		 * must guarantee that all cmds, including the reset,
		 * have been returned, and that any commands not yet
		 * returned are lost in the hardware.
		 */
#ifndef	__lock_lint
		pln->pkt_offline->fp_state = FP_STATE_IDLE;
#endif	__lock_lint
		break;

	    default:
		mutex_exit(&pln->pln_state_mutex);
		pln_disp_err(pln->pln_dip, CE_WARN,
			    "Unknown state change\n");
		break;

	}

}

/*
 * Asynchronous callback
 */
static void
pln_uc_callback(
	void		*arg)
{
	struct pln	*pln = (struct pln *)arg;

	P_UC_PRINTF((pln->pln_dip, "pln: unsolicited callback\n"));
}


/*
 * Initialize scsi packet and do some sanity checks
 * before starting a command.
 */
static int
pln_init_scsi_pkt(
	struct pln	*pln,
	struct pln_scsi_cmd	*sp)
{

	P_X_PRINTF((pln->pln_dip, "pln_init_scsi_pkt: scsi_cmd: 0x%x\n",
		sp));

	/*
	 * Clear out SCSI cmd LUN.
	 * We dont support LUN's.
	 */
	sp->cmd_pkt.pkt_cdbp[1] &= 0x1f;

#ifdef	PLNDEBUG
	if (plnflags & P_S_FLAG) {
		char	cdb[128];
		if (sp->cmd_flags & P_CFLAG_DMAVALID) {
			pln_printf(pln->pln_dip,
				"cdb=%s %s 0x%x\n",
				pln_cdb_str(cdb, sp->cmd_pkt.pkt_cdbp,
				sp->cmd_cdblen),
				(sp->cmd_flags & P_CFLAG_DMAWRITE) ?
				"write" : "read", sp->cmd_dmacount);
		} else {
			pln_printf(pln->pln_dip,
				"cdb=%s\n",
				pln_cdb_str(cdb, sp->cmd_pkt.pkt_cdbp,
				sp->cmd_cdblen));
		}
		pln_printf(pln->pln_dip,
		"pkt 0x%x timeout 0x%x flags 0x%x status len 0x%x\n",
		sp->cmd_pkt,
		sp->cmd_pkt.pkt_time,
		sp->cmd_flags,
		sp->cmd_senselen);
	}
#endif	/* PLNDEBUG */


	/*
	 * Initialize the command
	 */
	sp->cmd_pkt.pkt_reason = CMD_CMPLT;
	sp->cmd_pkt.pkt_state = 0;
	sp->cmd_pkt.pkt_statistics = 0;

	if (sp->cmd_flags & P_CFLAG_DMAVALID) {
		sp->cmd_pkt.pkt_resid = sp->cmd_dmacount;
	} else {
		sp->cmd_pkt.pkt_resid = 0;
	}

	/*
	 * Check for an out-of-limits cdb length
	 */
	if (sp->cmd_cdblen > FCP_CDB_SIZE) {
		P_E_PRINTF((pln->pln_dip,
			"cdb size %d exceeds maximum %d\n",
			sp->cmd_cdblen, FCP_CDB_SIZE));
		return (TRAN_BADPKT);
	}

	/*
	 * the scsa spec states that it is an error to have no
	 * completion function when FLAG_NOINTR is not set
	 */
	if ((sp->cmd_pkt.pkt_comp == NULL) &&
			((sp->cmd_pkt.pkt_flags & FLAG_NOINTR) == 0)) {
		P_E_PRINTF((pln->pln_dip, "intr packet with pkt_comp == 0\n"));
		return (TRAN_BADPKT);
	}

	/*
	 * We don't allow negative command timeouts
	 */
	if (sp->cmd_pkt.pkt_time < (long)NULL) {
		P_E_PRINTF((pln->pln_dip, "Invalid cmd timeout\n"));
		return (TRAN_BADPKT);
	}

	return (TRAN_ACCEPT);
}


/*
 * Prepare an fc_transport pkt for the command
 */
static int
pln_prepare_fc_packet(
	struct pln		*pln,
	struct pln_scsi_cmd	*sp,
	struct pln_fc_pkt	*fp)
{
	fc_packet_t		*fpkt;
	fc_frame_header_t	*hp;

	fpkt = fp->fp_pkt;

	/*
	 * Initialize the cmd data segment
	 */
	if (pln_prepare_cmd_dma_seg(pln, fp, sp) == 0) {
		P_E_PRINTF((pln->pln_dip, "cmd alloc failed\n"));
		return (TRAN_BUSY);
	}

	/*
	 * Initialize the response data segment
	 */
	fpkt->fc_pkt_rsp = &fp->fp_rspseg;

	/*
	 * Initialize the data packets segments, if
	 * this command involves data transfer.
	 */
	if (sp->cmd_flags & P_CFLAG_DMAVALID) {
		if (pln_prepare_data_dma_seg(fp, sp) == 0) {
			P_E_PRINTF((pln->pln_dip, "data dma_seg failed\n"));
			return (TRAN_BUSY);
		}
		fpkt->fc_pkt_io_class = (sp->cmd_flags & P_CFLAG_DMAWRITE) ?
			FC_CLASS_IO_WRITE : FC_CLASS_IO_READ;
	} else {
		fpkt->fc_pkt_datap = NULL;
		fpkt->fc_pkt_io_class = FC_CLASS_SIMPLE;
	}

	/*
	 * Initialize other fields of the packet
	 */
	fpkt->fc_pkt_cookie = (pln->pln_fc_tran)->fc_cookie;
	fpkt->fc_pkt_comp = pln_cmd_callback;
	fpkt->fc_pkt_timeout = sp->cmd_pkt.pkt_time;
	fpkt->fc_pkt_io_devdata = TYPE_SCSI_FCP;
	fpkt->fc_pkt_status = 0;
	fpkt->fc_pkt_statistics = 0;

	fpkt->fc_pkt_flags = 0;
	/* pass flag to transport routine */
	if (sp->cmd_pkt.pkt_flags & FLAG_NOINTR) {
		fpkt->fc_pkt_flags |= FCFLAG_NOINTR;	/* poll for intr */
		fpkt->fc_pkt_comp = NULL;
	}

	/*
	 * Fill in the fields of the command's FC header
	 */
	hp = fpkt->fc_frame_cmd;
	hp->r_ctl = R_CTL_COMMAND;
	hp->type = TYPE_SCSI_FCP;
	hp->f_ctl = F_CTL_FIRST_SEQ | F_CTL_SEQ_INITIATIVE;
	hp->seq_id = 0;
	hp->df_ctl = 0;
	hp->seq_cnt = 0;
	hp->ox_id = 0xffff;
	hp->rx_id = 0xffff;
	hp->ro = 0;

	return (TRAN_ACCEPT);
}


/*
 * Prepare an fc_transport pkt for a command that involves no SCSI command
 */
static int
pln_prepare_short_pkt(
	struct pln		*pln,
	struct pln_fc_pkt	*fp,
	void			(*callback)(struct fc_packet *),
	int			cmd_timeout)
{
	fc_packet_t		*fpkt;
	fc_frame_header_t	*hp;

	fpkt = fp->fp_pkt;

	/*
	 * Initialize other fields of the packet
	 */
	fpkt->fc_pkt_cookie = (pln->pln_fc_tran)->fc_cookie;
	fpkt->fc_pkt_comp = callback;
	fpkt->fc_pkt_timeout = cmd_timeout;
	fpkt->fc_pkt_io_devdata = TYPE_SCSI_FCP;
	fpkt->fc_pkt_status = 0;
	fpkt->fc_pkt_statistics = 0;
	fpkt->fc_pkt_flags = 0;

	/*
	 * Fill in the fields of the command's FC header
	 */
	hp = fpkt->fc_frame_cmd;
	hp->r_ctl = R_CTL_COMMAND;
	hp->type = TYPE_SCSI_FCP;
	hp->f_ctl = F_CTL_FIRST_SEQ | F_CTL_SEQ_INITIATIVE;
	hp->seq_id = 0;
	hp->df_ctl = 0;
	hp->seq_cnt = 0;
	hp->ox_id = 0xffff;
	hp->rx_id = 0xffff;
	hp->ro = 0;

	return (TRAN_ACCEPT);
}


/*
 *
 * Dispose of all cached pln_fc_pkt resources for a pln_disk
 */
static void
pln_fpacket_dispose_all(
	struct pln		*pln,
	struct pln_disk		*pd)
{
	struct pln_fc_pkt	*fp;
	struct pln_fc_pkt	*fp2;

	P_RD_PRINTF((pln->pln_dip, "pln_fpacket_dispose_all\n"));

	mutex_enter(&pd->pd_pkt_alloc_mutex);

	fp = pd->pd_pkt_pool;
	while (fp != NULL) {
		fp2 = fp->fp_next;
		pln_fpacket_dispose(pln, fp);
		fp = fp2;
	}

	pd->pd_pkt_pool = NULL;

	mutex_exit(&pd->pd_pkt_alloc_mutex);
}


/*
 * Dispose of a pln_fc_pkt and all associated resources.
 */
static void
pln_fpacket_dispose(
	struct pln		*pln,
	struct pln_fc_pkt	*fp)
{
	struct fc_transport	*fc = pln->pln_fc_tran;

	P_RD_PRINTF((pln->pln_dip, "pln_fpacket_dispose\n"));

	/*
	 * Free the lower level's fc_packet
	 */
	fc->fc_pkt_free(fc->fc_cookie, fp->fp_pkt);

	/*
	 * Free the pln_fc_pkt itself
	 */
	kmem_free((char *)fp, sizeof (struct pln_fc_pkt));
}


/*
 * Fill in various fields in the fcp command packet before sending
 * off a new command
 */
static int
pln_prepare_cmd_dma_seg(
	struct pln		*pln,
	struct pln_fc_pkt	*fp,
	struct pln_scsi_cmd	*sp)
{
	fc_packet_t		*fpkt = fp->fp_pkt;
	struct fcp_cmd		*cmd;
	fcp_ent_addr_t		*f0;

	P_RA_PRINTF((pln->pln_dip, "pln_prepare_cmd_dma_seg\n"));

	ASSERT(fp->fp_cmd != NULL);

	cmd = fp->fp_cmd;

	/*
	 * Zero everything in preparation to build the command
	 */
	bzero((caddr_t)cmd, sizeof (struct fcp_cmd));

	/*
	 * Prepare the entity address
	 */
#ifdef	ON1093
	f0 = (fcp_ent_addr_t *)sp->cmd_pkt.pkt_address.a_addr_ptr;
#else	ON1093
	f0 =
	(fcp_ent_addr_t *)sp->cmd_pkt.pkt_address.a_hba_tran->tran_tgt_private;
#endif	/* ON1093 */
	cmd->fcp_ent_addr.ent_addr_0 = f0->ent_addr_0;
	cmd->fcp_ent_addr.ent_addr_1 = f0->ent_addr_1;
	cmd->fcp_ent_addr.ent_addr_2 = f0->ent_addr_2;
	cmd->fcp_ent_addr.ent_addr_3 = f0->ent_addr_3;

	/*
	 * Prepare the SCSI control options
	 */
	if (sp->cmd_flags & P_CFLAG_DMAVALID) {
		if (sp->cmd_flags & P_CFLAG_DMAWRITE) {
			cmd->fcp_cntl.cntl_read_data = 0;
			cmd->fcp_cntl.cntl_write_data = 1;
		} else {
			cmd->fcp_cntl.cntl_read_data = 1;
			cmd->fcp_cntl.cntl_write_data = 0;
		}
	} else {
		cmd->fcp_cntl.cntl_read_data = 0;
		cmd->fcp_cntl.cntl_write_data = 0;
	}
	cmd->fcp_cntl.cntl_reset = 0;
	cmd->fcp_cntl.cntl_qtype = FCP_QTYPE_SIMPLE;

	/*
	 * Total transfer length
	 */
	cmd->fcp_data_len = (sp->cmd_flags & P_CFLAG_DMAVALID) ?
		sp->cmd_dmacount : 0;

	/*
	 * Copy the SCSI command over to fc packet
	 */
	ASSERT(sp->cmd_cdblen <= FCP_CDB_SIZE);
	bcopy((caddr_t)sp->cmd_pkt.pkt_cdbp, (caddr_t)cmd->fcp_cdb,
		sp->cmd_cdblen);

	/*
	 * Set the command dma segment in the fc_transport structure
	 */
	fpkt->fc_pkt_cmd = &fp->fp_cmdseg;

	/*
	 * Sync the cmd segment
	 */
	if (ddi_dma_sync(pln->pln_cmd_pool.cmd_handle,
		(caddr_t)fp->fp_cmd - pln->pln_cmd_pool.cmd_base,
		sizeof (struct fcp_cmd), DDI_DMA_SYNC_FORDEV) ==
		    DDI_FAILURE)
		return (0);

	return (1);
}


/*
 * Do some setup so that our parent can figure out where the
 * data is that we're to operate upon
 */
static int
pln_prepare_data_dma_seg(
	struct pln_fc_pkt	*fp,
	struct pln_scsi_cmd	*sp)
{
	fc_packet_t		*fpkt = fp->fp_pkt;

	ASSERT(sp->cmd_flags & P_CFLAG_DMAVALID);

	/*
	 * Initialize list of data dma_segs: only
	 * one segment, and null-terminate the list.
	 */
	fp->fp_datasegs[0] = &fp->fp_dataseg;
	fp->fp_datasegs[1] = NULL;

	/*
	 * Set up the data dma_seg in the fc_packet to
	 * point to our list of data segments.
	 */
	fpkt->fc_pkt_datap = &fp->fp_datasegs[0];

	/*
	 * Set up the data dma segment
	 */
	if (ddi_dma_htoc(sp->cmd_dmahandle, 0, &sp->cmd_dmacookie) ==
			DDI_FAILURE)
		return (0);

	fp->fp_dataseg.fc_count = sp->cmd_dmacookie.dmac_size;
	fp->fp_dataseg.fc_base = sp->cmd_dmacookie.dmac_address;

	if (sp->cmd_flags & P_CFLAG_CONSISTENT) {
		if (ddi_dma_sync(sp->cmd_dmahandle, 0, 0, DDI_DMA_SYNC_FORDEV)
				!= DDI_SUCCESS)
			return (0);
	}

	return (1);
}


/*
 * Execute a command on the pluto controller with retries if necessary.
 * return 0 if failure, 1 if successful.
 */
static int
pln_execute_cmd(
	struct pln	*pln,
	int		cmd,
	int		arg1,
	int		arg2,
	caddr_t		datap,
	int		datalen,
	int		sleep_flag)
{
	int			i;
	struct scsi_arq_status	*status;
	struct pln_scsi_cmd	*sp;
	int			rval = 0;
	int			sts;
	int			retry;

	/*
	 * Allocate space for the scsi_cmd.
	 */
	if ((sp = (struct pln_scsi_cmd *)pln_scsi_pktalloc(&pln->pln_scsi_addr,
			sizeof (union scsi_cdb),
			sizeof (struct scsi_arq_status), NULL,
			(sleep_flag == KM_SLEEP) ? SLEEP_FUNC : NULL_FUNC,
			NULL)) == NULL) {
		P_E_PRINTF((pln->pln_dip,
			"pln_private_cmd: cmd alloc failed\n"));
		return (0);
	}

	status = (struct scsi_arq_status *)sp->cmd_pkt.pkt_scbp;

	/*
	 * Execute the command
	 */
	for (retry = 0; retry < PLN_NRETRIES; retry++) {

		i = pln_private_cmd(pln, cmd, sp, arg1, arg2, datap,
			datalen, PLN_INTERNAL_CMD_TIMEOUT, sleep_flag);

		/*
		 * If the command simply failed, we give up
		 */
		if (i != 0) {
			P_E_PRINTF((pln->pln_dip,
				"pln_execute_cmd: failed %d\n", i));
			goto done;
		}

		/*
		 * Check the status, figure out what next
		 */
		sts = *((u_char *)&status->sts_status);
		switch (sts & STATUS_MASK) {
		case STATUS_GOOD:
			rval = 1;
			goto done;
		case STATUS_CHECK:
			P_PC_PRINTF((pln->pln_dip, "status: check\n"));
#ifdef	PLNDEBUG
			if (plnflags & P_PC_FLAG) {
				pln_dump("sense data: ",
					(u_char *)&status->sts_sensedata,
					sizeof (struct scsi_extended_sense) -
						status->sts_rqpkt_resid);
			}
#endif	PLNDEBUG
			break;
		case STATUS_BUSY:
			P_PC_PRINTF((pln->pln_dip, "status: busy\n"));
			break;
		}
	}

done:
	/*
	 * Free the memory we've allocated.
	 */
	pln_scsi_pktfree((struct scsi_pkt *)sp);

	return (rval);
}



/*
 * Transport a private cmd to the pluto controller.
 * Return 0 for success, or non-zero error indication
 */
static int
pln_private_cmd(
	struct pln			*pln,
	int				cmd,
	struct pln_scsi_cmd		*sp,
	int				arg1,
	int				arg2,
	caddr_t				datap,
	int				datalen,
	long				nticks,
	int				sleep_flag)
{
	int				rval = 0;
	struct scsi_pkt			*pkt;
	union scsi_cdb			*cdb;
	int				i;
	pln_fc_pkt_t			*fp;

	pkt = &sp->cmd_pkt;

	/* Set the number of retries */
	((struct pln_fc_pkt *)sp->cmd_fc_pkt)->fp_retry_cnt = PLN_NRETRIES;

	/*
	 * Misc
	 */
	pkt->pkt_comp = NULL;
	pkt->pkt_time = nticks;

	/* Run all internal commands in polling mode */
	pkt->pkt_flags = FLAG_NOINTR;

	/*
	 * Build the cdb
	 */
	cdb = (union scsi_cdb *)sp->cmd_pkt.pkt_cdbp;
	switch (cmd) {

	/*
	 * Build a Test Unit Ready cmd
	 */
	case SCMD_TEST_UNIT_READY:
		ASSERT(datalen == 0);
		sp->cmd_cdblen = CDB_GROUP0;
		cdb->scc_cmd = (u_char) cmd;
		break;

	/*
	 * Build an Inquiry cmd
	 */
	case SCMD_INQUIRY:
		ASSERT(datalen > 0);
		sp->cmd_cdblen = CDB_GROUP0;
		cdb->scc_cmd = (u_char) cmd;
		FORMG0COUNT(cdb, (u_char) datalen);
		break;

	/*
	 * Build a Group0 Mode Sense cmd.
	 * arg1 is the mode sense page number, arg2 is the
	 * page control (current, saved, etc.)
	 */
	case SCMD_MODE_SENSE:
		ASSERT(datalen > 0);
		sp->cmd_cdblen = CDB_GROUP0;
		cdb->scc_cmd = (u_char) cmd;
		FORMG0COUNT(cdb, (u_char) datalen);
		cdb->cdb_opaque[2] = arg1 | arg2;
		break;

	/*
	 * Build a Group1 Mode Sense cmd.
	 * arg1 is the mode sense page number, arg2 is the
	 * page control (current, saved, etc.)
	 */
	case SCMD_MODE_SENSE | SCMD_MS_GROUP1:
		ASSERT(datalen > 0);
		sp->cmd_cdblen = CDB_GROUP1;
		cdb->scc_cmd = (u_char) cmd;
		FORMG1COUNT(cdb, datalen);
		cdb->cdb_opaque[2] = arg1 | arg2;
		break;

	default:
		P_E_PRINTF((pln->pln_dip,
			"pln: no such private cmd 0x%x\n", cmd));
		return (0);
	}

	/*
	 * Clear flags in preparation for what we really need
	 */
	sp->cmd_flags &= ~(P_CFLAG_DMAWRITE | P_CFLAG_DMAVALID);

	/*
	 * Allocate dvma resources for the data.
	 * Note we only handle read transfers.
	 */
	if (datalen > 0) {
		ASSERT(datap != NULL);
		i = ddi_dma_addr_setup(pln->pln_dip, (struct as *)NULL,
			datap, datalen, DDI_DMA_READ,
			(sleep_flag == KM_SLEEP) ?
				DDI_DMA_SLEEP : DDI_DMA_DONTWAIT,
			NULL, pln->pln_fc_tran->fc_dmalimp, &sp->cmd_dmahandle);
		switch (i) {
		case DDI_DMA_MAPPED:
			break;
		case DDI_DMA_NORESOURCES:
			P_E_PRINTF((pln->pln_dip,
				"pc ddi_dma_setup: no resources\n"));
			rval = ENOMEM;
			goto failed;
		case DDI_DMA_PARTIAL_MAP:
		case DDI_DMA_NOMAPPING:
		case DDI_DMA_TOOBIG:
			P_E_PRINTF((pln->pln_dip,
				"pc ddi_dma_setup: 0x%x\n", i));
			rval = ENOMEM;
			goto failed;
		default:
			P_E_PRINTF((pln->pln_dip,
				"pc ddi_dma_setup: 0x%x\n", i));
			rval = ENOMEM;
			goto failed;
		}
		sp->cmd_flags |= P_CFLAG_DMAVALID;
		sp->cmd_dmacount = datalen;
	} else {
		sp->cmd_dmacount = 0;
		ASSERT(datap == NULL);
	}

	/*
	 * Allocate the FCP cmd/response pkts (iopbs)
	 */
	fp = (struct pln_fc_pkt *)sp->cmd_fc_pkt;
	if (pln_cr_alloc(pln, fp) <= 0)
		goto failed;

	/*
	 * 'Transport' the command...
	 */
	if ((pln_init_scsi_pkt(pln, sp) != TRAN_ACCEPT) ||
			(pln_prepare_fc_packet(pln, sp, fp) !=
				TRAN_ACCEPT)) {
		goto failed;
	}
	if (pln_local_start(pln, sp, fp) != TRAN_ACCEPT) {
		rval = EINVAL;
	}

failed:
	/*
	 * Free the FCP cmd/rsp pkts, if required
	 */
	if (fp->fp_cmd)
		pln_cr_free(pln, fp);

	/*
	 * Free the data dma segment, if we allocated one
	 */
	if (datalen > 0) {
		ASSERT(datap != NULL);
		ASSERT(sp->cmd_dmahandle != NULL);
		P_PC_PRINTF((pln->pln_dip, "pc ddi_dma_free\n"));
		ddi_dma_free(sp->cmd_dmahandle);
	}

	return (rval);
}



/*
 * Called by target driver to abort a command
 */
static int
pln_abort(
	struct scsi_address	*ap,
	struct scsi_pkt		*pkt)
{
	struct pln		*pln = ADDR2PLN(ap);
	int			rval;

	P_A_PRINTF((pln->pln_dip, "pln_abort\n"));

	mutex_enter(&pln->pln_mutex);
	rval =	_pln_abort(pln, ap, pkt);

	mutex_exit(&pln->pln_mutex);

	return (rval);
}

/*
 * Internal abort command handling
 */
/*ARGSUSED*/
static int
_pln_abort(
	struct pln		*pln,
	struct scsi_address	*ap,
	struct scsi_pkt		*pkt)
{
	P_A_PRINTF((pln->pln_dip, "pln_abort\n"));

	ASSERT(MUTEX_HELD(&pln->pln_mutex));
	return (0);
}


/*
 * Called by target driver to reset bus
 */
static int
pln_reset(
	struct scsi_address	*ap,
	int			level)
{
	struct pln		*pln = ADDR2PLN(ap);

	P_R_PRINTF((pln->pln_dip, "pln_reset: %d\n", level));

	switch (level) {
		/* to do */
	}

	return (0);
}


/*
 * Get capability
 */
static int
pln_getcap(
	struct scsi_address	*ap,
	char			*cap,
	int			whom)
{
	return (pln_commoncap(ap, cap, 0, whom, 0));
}


/*
 * Set capability
 */
static int
pln_setcap(
	struct scsi_address	*ap,
	char			*cap,
	int			value,
	int			whom)
{
	return (pln_commoncap(ap, cap, value, whom, 1));
}


/*
 * The core of capability handling.
 *
 * XXX - clean this up!
 */
static int
pln_commoncap(
	struct scsi_address	*ap,
	char			*cap,
	int			val,
	int			tgtonly,
	int			doset)
{
	struct pln		*pln = ADDR2PLN(ap);
	int			cidx;
	int			rval = 0;

	P_C_PRINTF((pln->pln_dip,
	    "%s capability: %s value=%d\n",
	    doset ? "Set" : "Get", cap, val));

	mutex_enter(&pln->pln_mutex);

	if ((tgtonly != 0 && tgtonly != 1) || cap == (char *)0) {
		goto exit;
	}
#ifdef	ON1093
	cidx = scsi_lookup_capstring(cap);
#else	ON1093
	cidx = scsi_hba_lookup_capstr(cap);
#endif	/* ON1093 */

	if (cidx < 0) {
		P_C_PRINTF((pln->pln_dip,
			"capability not defined: %s\n", cap));
		rval = CAP_UNDEFINED;
	} else if (doset && (val == 0 || val == 1)) {
		/*
		 * At present, we can only set binary (0/1) values
		 */

		P_C_PRINTF((pln->pln_dip,
			"capability %s set to %d\n", cap, val));

		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
		case SCSI_CAP_MSG_OUT:
		case SCSI_CAP_PARITY:
		case SCSI_CAP_INITIATOR_ID:
		case SCSI_CAP_DISCONNECT:
		case SCSI_CAP_SYNCHRONOUS:

			/*
			 * None of these are settable via
			 * the capability interface.
			 */
			break;

		case SCSI_CAP_TAGGED_QING:
			rval = 1;
			break;

		case SCSI_CAP_ARQ:
			/*
			 * We ALWAYS do automatic Request Sense
			 */
			rval = 1;
			break;

		case SCSI_CAP_WIDE_XFER:
		case SCSI_CAP_UNTAGGED_QING:
		default:
			rval = CAP_UNDEFINED;
			break;
		}

	} else if (doset == 0) {
		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
			break;
		case SCSI_CAP_MSG_OUT:
			rval = 1;
			break;
		case SCSI_CAP_DISCONNECT:
			break;
		case SCSI_CAP_SYNCHRONOUS:
			break;
		case SCSI_CAP_PARITY:
			if (scsi_options & SCSI_OPTIONS_PARITY)
				rval = 1;
			break;
		case SCSI_CAP_INITIATOR_ID:
			rval = pln_initiator_id;
			break;
		case SCSI_CAP_TAGGED_QING:
			rval = 1;
			break;
		case SCSI_CAP_UNTAGGED_QING:
			rval = 1;
			break;
		case SCSI_CAP_ARQ:
			rval = 1;
			break;

		default:
			rval = CAP_UNDEFINED;
			break;
		}
		P_C_PRINTF((pln->pln_dip,
			"capability %s is %d\n", cap, rval));
	} else {
		P_C_PRINTF((pln->pln_dip,
			"capability: cannot set %s to %d\n", cap, val));
	}
exit:

	mutex_exit(&pln->pln_mutex);
	return (rval);
}


/*
 * pln_watch() - timer routine used to check for command timeouts, etc.
 */
/*ARGSUSED*/
static void
pln_watch(
	caddr_t		arg)
{
	struct pln		*pln;
	pln_fc_pkt_t		*fp;
	fc_packet_t		*fcpkt;
	struct pln_disk		*pd;
	pln_cr_pool_t		*cp;

	/* This is our current time... */
	pln_watchdog_time++;

	/*
	 * Adjust the throttle positions.
	 *
	 * This has a couple of effects:
	 *	- if pln_ncmd_ref has drifted far from the real number of
	 *	 outstanding commands, adjustment of pln_maxcmds here
	 *	 will allow pln_maxcmds to track pln_ncmd_ref.  Statistically,
	 *	 pln_ncmd_ref won't move too far in one second, the
	 *	 interval between adjustments of pln_maxcmds.
	 *	- as we continue to adjust pln_maxcmds upward, we will
	 *	 ensure we have as many commands queued in the hardware
	 *	 as is possible.  This is especially desirable for
	 *	 multiple-hosts-to-one-controller configurations, where
	 *	 we can't directly measure the (dynamic) load imposed by
	 *	 the other host.
	 */
	mutex_enter(&pln_softc_mutex);
	for (pln = pln_softc; pln; pln = pln->pln_next) {

	    /* Don't process this one if attach() isn't complete */
	    if (!pln->pln_ref_cnt)
		continue;

	    mutex_exit(&pln_softc_mutex);

	    mutex_enter(&pln->pln_throttle_mtx);
	    if ((pln->pln_maxcmds - pln->pln_ncmd_ref) < PLN_MAXCMDS_DELTA)
		pln->pln_maxcmds += PLN_MAXCMDS_DELTA;
	    mutex_exit(&pln->pln_throttle_mtx);

	/*
	 * Try to start any "throttled" commands
	 */
	    if (pln->pln_throttle_flag &&
		    (pln->pln_state == PLN_STATE_ONLINE) &&
		    ((pln->pln_maxcmds - pln->pln_ncmd_ref) >
			PLN_THROTTLE_SWING)) {
		pln_throttle_start(pln);
	    }
	    mutex_enter(&pln_softc_mutex);
	}
	mutex_exit(&pln_softc_mutex);

	/*
	 * Don't do timeout checking each time, to save cycles...
	 */
	if (pln_watchdog_time & PLN_TIME_CHECK_MSK) {
	    pln_watchdog_id = timeout(pln_watch, (caddr_t)0,
					pln_watchdog_tick);
	    return;
	}

	/*
	 * Search through the queues of all devices, looking for timeouts...
	 */
	mutex_enter(&pln_softc_mutex);
	for (pln = pln_softc; pln; pln = pln->pln_next) {

	    /* Don't process this one if attach() isn't complete */
	    if (!pln->pln_ref_cnt)
		continue;
	    mutex_exit(&pln_softc_mutex);

	    if ((pln->pln_state & ~PLN_STATE_OFFLINE_RSP) == PLN_STATE_ONLINE) {

		pd = pln->pln_disk_list;
		do {

		    mutex_enter(&pd->pd_pkt_alloc_mutex);

		    for (fp = pd->pd_inuse_head; fp; fp = fp->fp_next) {
			if ((fp->fp_state == FP_STATE_ISSUED) &&
			    (fp->fp_timeout != 0) &&
			    (fp->fp_timeout < pln_watchdog_time)) {

			/*
			 * Process this command's timeout.
			 * By setting the command's timeout flag
			 * and incrementing the pln_disk's timeout
			 * counter under mutex protection, we can
			 * easily check to see if all timed out commands
			 * have actually completed in pln_cmd_callback.
			 */
			    mutex_enter(&pln->pln_state_mutex);

			    fp->fp_timeout_flag = 1;
			    pln->pln_timeout_count++;

			/*
			 * Cover the race with pln_cmd_callback.
			 * We must do this check *after* setting
			 * the timeout flag.
			 */
			    if (fp->fp_state != FP_STATE_ISSUED) {
				fp->fp_timeout_flag = 0;
				pln->pln_timeout_count--;
			    } else {

				/*
				 * Put ourselves in the first level of timeout
				 * recovery.  In this state, we don't
				 * issue additional commands to the lower
				 * levels, hoping any commands that timed out
				 * will complete before we need to take
				 * more drastic measures.
				 */
				if ((pln->pln_state & ~PLN_STATE_OFFLINE_RSP) ==
					PLN_STATE_ONLINE) {
				    P_W_PRINTF((pln->pln_dip,
					"pln command timeout!\n"));
				    pln->pln_state |= PLN_STATE_TIMEOUT;
				    pln->pln_timer = pln_watchdog_time +
							PLN_TIMEOUT_RECOVERY;
				    P_W_PRINTF((pln->pln_dip,
					"timeout value for cmd = %d\n",
					fp->fp_scsi_cmd.cmd_pkt.pkt_time));
				}
			    }
			    mutex_exit(&pln->pln_state_mutex);
			}
		    }
		    mutex_exit(&pd->pd_pkt_alloc_mutex);
		} while ((pd = pd->pd_next) != pln->pln_disk_list);

	    } else if (pln->pln_state & PLN_STATE_TIMEOUT) {

		if (pln->pln_timer < pln_watchdog_time) {

		/*
		 * Our first level of timeout recovery failed.
		 * Force the link offline to flush all commands
		 * from the hardware, so that we may try
		 * them again.
		 */
		    pln_disp_err(pln->pln_dip, CE_WARN,
			    "Timeout recovery being invoked...\n");

		    if (pln_disable_timeouts) {
			pln_disp_err(pln->pln_dip, CE_WARN,
			    "Timeout recovery disabled!\n");
			mutex_enter(&pln->pln_state_mutex);
			pln->pln_state |= PLN_STATE_DO_RESET;
			pln->pln_state &= ~PLN_STATE_TIMEOUT;
			mutex_exit(&pln->pln_state_mutex);
		    } else
			pln_transport_offline(pln, PLN_STATE_TIMEOUT, 0);
		}

	    } else if (pln->pln_state & PLN_STATE_DO_OFFLINE) {

		/*
		 * Now we're really in trouble.  The offline timeout
		 * recovery didn't work, so let's try to reset the
		 * hardware.
		 */
		if (pln->pln_timer < pln_watchdog_time) {
		    pln_disp_err(pln->pln_dip, CE_WARN,
			"Timeout recovery failed, resetting\n");
		    pln_transport_reset(pln, PLN_STATE_DO_OFFLINE, 0);
		}

	    } else if (pln->pln_state & PLN_STATE_OFFLINE) {

		/*
		 * Online timeouts are enabled only if
		 * pln->pln_en_online_timeout is nonzero
		 */
		if (pln->pln_en_online_timeout &&
			(pln->pln_timer < pln_watchdog_time)) {
		    mutex_enter(&pln->pln_state_mutex);
		    pln->pln_state |= PLN_STATE_LINK_DOWN;
		    mutex_exit(&pln->pln_state_mutex);

		    /* blow away waiters list */
		    mutex_enter(&pln->pln_cr_mutex);
		    cp = &pln->pln_cmd_pool;
		    fp = cp->waiters_head;
		    cp->waiters_head = cp->waiters_tail = NULL;
		    mutex_exit(&pln->pln_cr_mutex);

#ifndef	__lock_lint
		    while (fp != NULL) {
			struct pln_scsi_cmd	*sp;

			sp = &fp->fp_scsi_cmd;
			/* Failed to start, fake up some status information */
			sp->cmd_pkt.pkt_state = 0;
			sp->cmd_pkt.pkt_statistics = 0;
			pln_build_extended_sense(sp, KEY_HARDWARE_ERROR);

			fp = fp->fp_cr_next;
			if (sp->cmd_pkt.pkt_comp) {
				pln_add_callback(pln, sp);
			}
		    }
#endif	__lock_lint

		    pd = pln->pln_disk_list;
		    do {

			/*
			 * pln_fc_pkt transitions out of the FP_STATE_ONHOLD
			 * state are protected by the pln_throttle_mutex.
			 * We also grab the pd_pkt_alloc_mutex so that
			 * we may safely traverse the list.
			 */
			mutex_enter(&pln->pln_throttle_mtx);
			mutex_enter(&pd->pd_pkt_alloc_mutex);

			/*
			 * Capture all packets in the "on hold" state
			 * so that we may fail them.  We can't just leave
			 * them in the "on hold" state while doing
			 * the command completions, because they may
			 * be returned to the in use list in the "on hold"
			 * list if an upper layer should decide to retry.
			 *
			 * Also blast the "onhold" list anchor for the pd, so
			 * that the throttling routines don't try to start
			 * these commands either.
			 */
			for (fp = pd->pd_inuse_head; fp; fp = fp->fp_next)
			    if (fp->fp_state == FP_STATE_ONHOLD)
				fp->fp_state = FP_STATE_OFFLINE;

			pd->pd_onhold_head = NULL;

			/*
			 * Spin through the list of commands, calling
			 * their completion routines.
			 */
			fp = pd->pd_inuse_head;
			while (fp) {
			    if (fp->fp_state == FP_STATE_OFFLINE) {

				/*
				 * Fake up some fields to make it look like
				 * this command was processed by our parent
				 */
				fp->fp_state = FP_STATE_ISSUED;
				pln->pln_ncmd_ref++;
				fcpkt = fp->fp_pkt;
				fcpkt->fc_pkt_status = FC_STATUS_ERR_OFFLINE;
				fp->fp_retry_cnt = 1;

				/*
				 * Give up the mutexes to avoid a potential
				 * deadlock in the completion routine
				 */
				mutex_exit(&pd->pd_pkt_alloc_mutex);
				mutex_exit(&pln->pln_throttle_mtx);

				if (fcpkt->fc_pkt_comp)
				    (*fcpkt->fc_pkt_comp)(fcpkt);

				mutex_enter(&pln->pln_throttle_mtx);
				mutex_enter(&pd->pd_pkt_alloc_mutex);

				/*
				 * We have to start from the top since
				 * we gave up the mutex.  Good thing
				 * this isn't a performance-sensitive path.
				 */
				fp = pd->pd_inuse_head;
				continue;
			    }
			    fp = fp->fp_next;
			}

			mutex_exit(&pd->pd_pkt_alloc_mutex);
			mutex_exit(&pln->pln_throttle_mtx);
		    } while ((pd = pd->pd_next) != pln->pln_disk_list);
		}
	    }
	    mutex_enter(&pln_softc_mutex);
	}
	mutex_exit(&pln_softc_mutex);

	/* Start the timer again... */
	pln_watchdog_id = timeout(pln_watch, (caddr_t)0, pln_watchdog_tick);
}

/*
 * Force the interface offline
 */
static void
pln_transport_offline(
	struct pln	*pln,
	int		state_flag,
	int		poll)
{
	pln_fc_pkt_t	*fp;


	fp = pln->pkt_offline;

	mutex_enter(&pln->pln_state_mutex);

#ifndef	__lock_lint
	if (!(pln->pln_state & state_flag) || (fp->fp_state != FP_STATE_IDLE)) {
	    mutex_exit(&pln->pln_state_mutex);
	    return;
	}
#endif	__lock_lint

	pln->pln_state &= ~PLN_STATE_TIMEOUT;
	pln->pln_state |= PLN_STATE_DO_OFFLINE;
	pln->pln_timer = pln_watchdog_time + PLN_OFFLINE_TIMEOUT;

	(void) pln_prepare_short_pkt(pln, fp,
		(poll) ? NULL : pln_offline_callback, PLN_OFFLINE_TIMEOUT);
	fp->fp_pkt->fc_pkt_io_class = FC_CLASS_OFFLINE;

#ifndef	__lock_lint
	fp->fp_state = FP_STATE_NOTIMEOUT;
#endif	__lock_lint
	mutex_exit(&pln->pln_state_mutex);

	if (poll)
	    fp->fp_pkt->fc_pkt_flags |= FCFLAG_NOINTR;

	/*
	 * Our parent does the real work...
	 */
	if (pln->pln_fc_tran->fc_transport(fp->fp_pkt,
		FC_NOSLEEP) != FC_TRANSPORT_SUCCESS) {
#ifndef	__lock_lint
	    fp->fp_state = FP_STATE_IDLE;
#endif	__lock_lint
	    pln_transport_reset(pln, PLN_STATE_DO_OFFLINE, poll);
	    return;
	} else if (poll) {
#ifndef	__lock_lint
	    fp->fp_state = FP_STATE_IDLE;
#endif	__lock_lint
	}
}

/*
 * pln_offline_callback() - routine called when a request to send the soc
 * offline has completed
 */
static void
pln_offline_callback(
	struct fc_packet	*fpkt)
{
	pln_fc_pkt_t		*fp = (pln_fc_pkt_t *)fpkt->fc_pkt_private;
	struct pln		*pln = fp->fp_pln;


	mutex_enter(&pln->pln_state_mutex);
#ifndef	__lock_lint
	fp->fp_state = FP_STATE_IDLE;
#endif	__lock_lint
	pln->pln_state &= ~PLN_STATE_DO_OFFLINE;
	mutex_exit(&pln->pln_state_mutex);
}

/*
 * Reset the transport interface
 */
static void
pln_transport_reset(
	struct pln	*pln,
	int		state_flag,
	int		poll)
{
	pln_fc_pkt_t	*fp;

	fp = pln->pkt_reset;


	mutex_enter(&pln->pln_state_mutex);

#ifndef	__lock_lint
	if ((fp->fp_state != FP_STATE_IDLE) || !(pln->pln_state & state_flag)) {
	    mutex_exit(&pln->pln_state_mutex);
	    return;
	}
#endif	__lock_lint

	(void) pln_prepare_short_pkt(pln, fp,
		(poll) ? NULL : pln_reset_callback, 0);

	pln->pln_state |= PLN_STATE_DO_RESET;
	pln->pln_state &= ~PLN_STATE_DO_OFFLINE;
#ifndef	__lock_lint
	fp->fp_state = FP_STATE_NOTIMEOUT;
#endif	__lock_lint

	mutex_exit(&pln->pln_state_mutex);

	if (poll)
	    fp->fp_pkt->fc_pkt_flags |= FCFLAG_NOINTR;

	if ((*pln->pln_fc_tran->fc_reset)(fp->fp_pkt) == 0) {
		pln_disp_err(pln->pln_dip, CE_WARN, "reset recovery failed\n");
#ifndef	__lock_lint
		fp->fp_state = FP_STATE_IDLE;
#endif	__lock_lint
	} else if (poll) {
#ifndef	__lock_lint
	    fp->fp_state = FP_STATE_IDLE;
#endif	__lock_lint
	}
}

/*
 * Callback routine used after resetting the transport interface
 */
static void
pln_reset_callback(
	struct fc_packet	*fpkt)
{
	pln_fc_pkt_t		*fp = (pln_fc_pkt_t *)fpkt->fc_pkt_private;


#ifndef	__lock_lint
	fp->fp_state = FP_STATE_IDLE;
#endif	__lock_lint
}



/*
 * Set up the disk state info according to the configuration
 * of the pluto.
 * The Inquiry command is used to get the configuration in
 * case the pluto is reserved.
 *
 * Also set some properties based on the firmware revision level.
 *
 * Return 0 if ok, 1 if failure.
 */
static int
pln_build_disk_state(
	struct pln		*pln,
	int			sleep_flag)
{
	struct p_inquiry	*inquiry = NULL;
	char		string_buf[256];
	u_char		n_ports, n_tgts;
	int		i;
	int		rval = 1;
	int		priority_res = 1;
	int		fast_wrt = 1;
	int		rev_num = 0;
	int		sub_num = 0;

	P_C_PRINTF((pln->pln_dip, "pln_build_disk_state:\n"));

	if ((inquiry = (struct p_inquiry *)
		    kmem_zalloc(sizeof (struct p_inquiry),
		    sleep_flag)) == NULL) {
		return (rval);
	}
	i = pln_execute_cmd(pln, SCMD_INQUIRY, 0, 0,
		(caddr_t)inquiry, sizeof (struct p_inquiry),
		sleep_flag);
	if (i == 0) {
		P_E_PRINTF((pln->pln_dip,
			"pln_build_disk_state:Inquiry failed\n"));
		goto done;
	}

	/*
	 * First check to see if # ports and Targets are 0
	 */
	if ((inquiry->inq_ports == 0) || (inquiry->inq_tgts == 0)) {
		bzero((caddr_t)string_buf, sizeof (string_buf));
		for (i = 0; i < sizeof (inquiry->inq_firmware_rev); i++) {
			string_buf[i] = inquiry->inq_firmware_rev[i];
		}

		cmn_err(CE_NOTE,
		"pln%d: Old SSA firmware has been"
		" detected (Ver:%s) - Please upgrade\n",
		ddi_get_instance(pln->pln_dip), string_buf);

		/*
		 * Make sure we really are talking to an SSA
		 *
		 * If we are then use default number of ports
		 * and targets.
		 * If not then fail.
		 */
		if (strncmp(inquiry->inq_pid, "SSA", 3) != 0) {
			P_E_PRINTF((pln->pln_dip, "Device not SSA\n"));
		    goto done;
		}
		/*
		 * Default number of ports and targets
		 * to match SSA100.
		 */
		n_ports = 6;
		n_tgts = 5;
	} else {
		n_ports = inquiry->inq_ports;
		n_tgts = inquiry->inq_tgts;
	}

	/*
	 * This is where we look at the firmware revision and
	 * set the appropriate properties that our child (ssd)
	 * uses.
	 *
	 * This requires the inq_firmware_rev field in the Inquiry
	 * to be in the format x.xs or x.xx in ascii, which I have been
	 * assured it will be. (x = ascii 0-9 and s = space).
	 *
	 * Set priority-reserve and fast-writes properties if
	 * firmware rev >= 1.9.
	 */
	rev_num = inquiry->inq_firmware_rev[0] & 0xf;
	sub_num = inquiry->inq_firmware_rev[2] & 0xf;
	if (inquiry->inq_firmware_rev[3] != 0x20) {
		sub_num = (sub_num * 10) + (inquiry->inq_firmware_rev[3] & 0xf);
	}

	if ((rev_num > 1) || ((rev_num == 1) && (sub_num >= 9))) {
		(void) ddi_prop_create(DDI_DEV_T_NONE,
			pln->pln_dip,
			DDI_PROP_CANSLEEP,
			"priority-reserve",
			(caddr_t)&priority_res,
			sizeof (priority_res));
		(void) ddi_prop_create(DDI_DEV_T_NONE,
			pln->pln_dip,
			DDI_PROP_CANSLEEP,
			"fast-writes",
			(caddr_t)&fast_wrt,
			sizeof (fast_wrt));
	}


	pln->pln_nports = n_ports;
	pln->pln_ntargets = n_tgts;

	if (pln_alloc_disk_state(pln, sleep_flag) == 0) {
		pln_free_disk_state(pln);
		goto done;
	}
	pln_init_disk_state_mutexes(pln);
	rval = 0;
done:
	if (inquiry) {
		kmem_free((char *)inquiry, sizeof (struct p_inquiry));
	}

	return (rval);
}


/*
 * Allocate state and initialize mutexes for the full set
 * of disks reported by the pluto.
 */
static int
pln_alloc_disk_state(
	struct pln		*pln,
	int			sleep_flag)
{
	u_short			port;
	u_short			target;
	struct pln_disk		*pd,
				*pd_prev,
				*pd_first = NULL;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(pln->cur_throttle))
	ASSERT(pln->pln_nports != 0);
	ASSERT(pln->pln_ntargets != 0);
	ASSERT(pln->pln_ids == NULL);
	ASSERT(pln->pln_ctlr != NULL);


	/*
	 * Allocate pln_disk structures for individual disks
	 */
	pln->pln_ids = (struct pln_disk **)
		kmem_zalloc(sizeof (struct pln_disk *) * pln->pln_nports,
			sleep_flag);
	if (pln->pln_ids == NULL) {
		return (0);
	}

	for (port = 0; port < pln->pln_nports; port++) {
		pln->pln_ids[port] = pd = (struct pln_disk *)kmem_zalloc(
			sizeof (struct pln_disk) * pln->pln_ntargets,
				sleep_flag);
		if (pd == NULL) {
			return (0);
		}
		if (!pd_first) {
			pd_first = pd;
			pd_prev = pd;
		} else {
			pd_prev->pd_next = pd;
			pd_prev = pd;
		}
		for (target = 1; target < pln->pln_ntargets; target++) {
			pd = pd_prev + 1;
			pd_prev->pd_next = pd;
			pd_prev = pd;
		}
	}

	pd->pd_next = pln->pln_ctlr;
	pln->pln_ctlr->pd_next = pd_first;
	pln->pln_disk_list = pd_first;
	pln->cur_throttle = pd_first;

	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(pln->cur_throttle))

	return (1);
}



/*
 * Initialize mutexes for disk state
 */
static void
pln_init_disk_state_mutexes(
	struct pln		*pln)
{
	u_short			port;
	u_short			tgt;
	struct pln_disk		*pd;
	char			name[256];
	int			instance;
#ifdef	PLN_LOCK_STATS
	int			i;
#endif	PLN_LOCK_STATS

	instance = ddi_get_instance(pln->pln_dip);

	/*
	 * Initialize mutexes for individual disks
	 */
#ifdef	PLN_LOCK_STATS
	i = lock_stats;
	lock_stats |= pln_lock_stats;
#endif	PLN_LOCK_STATS
	for (port = 0; port < pln->pln_nports; port++) {
		pd = pln->pln_ids[port];
		for (tgt = 0; tgt < pln->pln_ntargets; tgt++, pd++) {
			(void) sprintf(name, "pln%d port%d target%d",
				instance, port, tgt);
			mutex_init(&pd->pd_pkt_alloc_mutex, name, MUTEX_DRIVER,
				pln->pln_iblock);
		}
	}

#ifdef	PLN_LOCK_STATS
	lock_stats = i;
#endif	PLN_LOCK_STATS

	pln->pln_disk_mtx_init = 1;
}


/*
 * Destroy all disk state mutexes.  This assumes that
 * a full allocation of the disk state was successful.
 */
static void
pln_destroy_disk_state_mutexes(
	struct pln		*pln)
{
	u_short			port;
	u_short			tgt;
	struct pln_disk		*pd;

	ASSERT(pln->pln_nports != 0);
	ASSERT(pln->pln_ntargets != 0);

	/*
	 * Free individual disk mutexes
	 */
	for (port = 0; port < pln->pln_nports; port++) {
		pd = pln->pln_ids[port];
		for (tgt = 0; tgt < pln->pln_ntargets; tgt++, pd++) {
			mutex_destroy(&pd->pd_pkt_alloc_mutex);
		}
	}

	/*
	 * clean up global info
	 */
	pln->pln_nports = 0;
	pln->pln_ntargets = 0;
}


/*
 * Free up all individual disk state allocated.
 * Note that this is structured so as to be able to free
 * up a partially completed allocation.
 */
static void
pln_free_disk_state(
	struct pln		*pln)
{
	u_short			port;
	struct pln_disk		*pd;

	/*
	 * Free the lists of command packets
	 */
	pd = pln->pln_disk_list;
	if (pd)
		do {
			pln_fpacket_dispose_all(pln, pd);
		} while ((pd = pd->pd_next) != pln->pln_disk_list);

	/*
	 * Free individual disk state
	 */
	if (pln->pln_ids != NULL) {
		for (port = 0; port < pln->pln_nports; port++) {
			if ((pd = pln->pln_ids[port]) != NULL) {
				kmem_free((void *) pd,
					sizeof (struct pln_disk) *
						pln->pln_ntargets);
			}
		}
		kmem_free((void *) pln->pln_ids,
			sizeof (struct pln_disk *) * pln->pln_nports);
		pln->pln_ids = NULL;
	}
}

#ifndef	ON1093



/*ARGSUSED*/
static void
pln_scsi_destroy_pkt(
	struct scsi_address	*ap,
	struct scsi_pkt		*pkt)
{
	pln_scsi_dmafree(ap, pkt);
	pln_scsi_pktfree(pkt);
}




/*ARGSUSED*/
void
pln_scsi_dmafree(
	struct scsi_address	*ap,
	struct scsi_pkt		*pkt)
{
	register struct	pln_scsi_cmd *cmd = (struct pln_scsi_cmd *)pkt;

	if (cmd->cmd_flags & P_CFLAG_DMAVALID) {
		/*
		 * Free the mapping.
		 */
		ddi_dma_free(cmd->cmd_dmahandle);
		cmd->cmd_flags ^= P_CFLAG_DMAVALID;
	}
}

/*ARGSUSED*/
static struct scsi_pkt *
pln_scsi_init_pkt(
	struct scsi_address	*ap,
	struct scsi_pkt		*pkt,
	struct buf		*bp,
	int			cmdlen,
	int			statuslen,
	int			tgtlen,
	int			flags,
	int			(*callback)(),
	caddr_t			arg)
{
	struct scsi_pkt		*new_pkt = NULL;
	struct	pln_scsi_cmd *cmd;
	int	rval;
	u_int	dma_flags;

	/*
	 * Allocate a pkt
	 */
	if (!pkt) {
		pkt = pln_scsi_pktalloc(ap, cmdlen, statuslen, tgtlen,
			callback, arg);
		if (pkt == NULL)
			return (NULL);
		new_pkt = pkt;
	}

	/*
	 * Set up dma info
	 */
	if (bp) {

		cmd = (struct pln_scsi_cmd *)pkt;

		/*
		 * clear any stale flags
		 */
		cmd->cmd_flags &= ~(P_CFLAG_DMAWRITE | P_CFLAG_DMAVALID);

		/*
		 * Get the host adapter's dev_info pointer
		 */
		if (bp->b_flags & B_READ) {
			dma_flags = DDI_DMA_READ;
			cmd->cmd_flags |= P_CFLAG_DMAVALID;
		} else {
			cmd->cmd_flags |= P_CFLAG_DMAWRITE | P_CFLAG_DMAVALID;
			dma_flags = DDI_DMA_WRITE;
		}
		if (flags & PKT_CONSISTENT) {
			dma_flags |= DDI_DMA_CONSISTENT;
			cmd->cmd_flags |= P_CFLAG_CONSISTENT;
		}
		rval = ddi_dma_buf_setup((dev_info_t *)(((struct pln *)
			ap->a_hba_tran->tran_hba_private)->pln_dip),
			bp, dma_flags, callback,
			arg, 0, &cmd->cmd_dmahandle);

		if (rval != DDI_DMA_MAPPED) {
			switch (rval) {
			case DDI_DMA_NORESOURCES:
				ASSERT(bp->b_error == 0);
				break;
			case DDI_DMA_PARTIAL_MAP:
				cmn_err(CE_PANIC, "ddi_dma_buf_setup "
				"returned DDI_DMA_PARTIAL_MAP\n");
				break;
			case DDI_DMA_NOMAPPING:
				bp->b_error = EFAULT;
				bp->b_flags |= B_ERROR;
				break;
			case DDI_DMA_TOOBIG:
				bp->b_error = EFBIG;	/* ??? */
				bp->b_flags |= B_ERROR;
				break;
			}
			if (new_pkt) {
				pln_scsi_pktfree(new_pkt);
			}
			return ((struct scsi_pkt *)NULL);
		}
		cmd->cmd_dmacount = bp->b_bcount;
	}

	P_T_PRINTF(((dev_info_t *)(((struct pln *)
		ap->a_hba_tran->tran_hba_private)->pln_dip),
		"pln_scsi_init_pkt: pkt 0x%x\n", pkt));
	return (pkt);
}
#endif	/* ON1093 */

/*
 * Function name : pln_scsi_pktalloc()
 *
 * Return Values : NULL or a scsi_pkt
 * Description	 : allocate a pkt
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
struct scsi_pkt *
pln_scsi_pktalloc(struct scsi_address *addr, int cmdlen, int statuslen,
    int tgtlen, int (*callback)(), caddr_t callback_arg)
{
	int kf;
	register int			failure = 0;
	register struct pln_scsi_cmd 	*sp;
	register struct pln_fc_pkt 	*fp;
	register caddr_t		tgtp = NULL,
					cdbp = NULL,
					scbp = NULL;

	register struct pln		*pln = ADDR2PLN(addr);
#ifdef	ON1093
	pln_address_t			*ap = (pln_address_t *)addr->a_addr_ptr;
#else	ON1093
	pln_address_t	*ap =
			(pln_address_t *)addr->a_hba_tran->tran_tgt_private;
#endif	/* ON1093 */
	register struct pln_disk 	*pd;
	register int			fp_failure = 0;
	struct fc_transport		*fc;

	/*
	 * Get a ptr to the disk-specific structure
	 */
	switch (ap->pln_entity) {
	    case PLN_ENTITY_CONTROLLER:
		if (ap->pln_port != 0 || ap->pln_target != 0 ||
				ap->pln_reserved != 0) {
			return (NULL);
		}
		pd = pln->pln_ctlr;
		break;
	    case PLN_ENTITY_DISK_SINGLE:
		if (ap->pln_port >= pln->pln_nports ||
				ap->pln_target >= pln->pln_ntargets ||
					ap->pln_reserved != 0) {
			return (NULL);
		}
		pd = pln->pln_ids[ap->pln_port] + ap->pln_target;
		break;
	    default:
		return (NULL);
	}

	kf = (callback == SLEEP_FUNC) ? KM_SLEEP: KM_NOSLEEP;

	/*
	 * Do our own allocation of cmd, status, and tgt areas.
	 */

	if (cmdlen > sizeof (union scsi_cdb)) {
		if ((cdbp = kmem_alloc((size_t)cmdlen, kf)) == NULL) {
			failure++;
		}
	}
	if (statuslen > sizeof (struct scsi_arq_status)) {
		if ((scbp = kmem_zalloc((size_t)statuslen, kf)) == NULL) {
			failure++;
		}
	}
	if (tgtlen) {
		if ((tgtp = kmem_zalloc((size_t)tgtlen, kf)) == NULL) {
			failure++;
		}
	}

	/*
	 * Get a pln_fc_pkt, all threaded together with the necessary stuff
	 */
	if (!failure) {

	    mutex_enter(&pd->pd_pkt_alloc_mutex);

	    if (pd->pd_pkt_pool) {
		fp = pd->pd_pkt_pool;
		pd->pd_pkt_pool = fp->fp_next;
		ASSERT(fp->fp_state == FP_STATE_FREE);
		fp->fp_state = FP_STATE_IDLE;

		fp->fp_next = NULL;
		if ((fp->fp_prev = pd->pd_inuse_tail) != NULL)
		    pd->pd_inuse_tail->fp_next = fp;
		else
		    pd->pd_inuse_head = fp;
		pd->pd_inuse_tail = fp;

	    } else {
		struct fc_packet *fpkt;
		fc = pln->pln_fc_tran;

		fp = kmem_zalloc(sizeof (struct pln_fc_pkt), kf);
		if (!fp) {
		    fp_failure++;
		    failure++;
		} else {
		    fp->fp_pd = pd;
		    fp->fp_pln = pln;
		    fp->fp_state = FP_STATE_IDLE;

		/*
		 * Grab an fc packet from our parent
		 */
		    fpkt = fc->fc_pkt_alloc(fc->fc_cookie,
				(kf == KM_SLEEP) ? FC_SLEEP : FC_NOSLEEP);
		    fp->fp_pkt = fpkt;
		    if (!fpkt) {
			fp_failure++;
			failure++;
		    } else
			fpkt->fc_pkt_private = (void *) fp;

		    if (!fp_failure) {
			fp->fp_next = NULL;
			if ((fp->fp_prev = pd->pd_inuse_tail) != NULL)
			    pd->pd_inuse_tail->fp_next = fp;
			else
			    pd->pd_inuse_head = fp;
			pd->pd_inuse_tail = fp;
		    }
		}
	    }

	    mutex_exit(&pd->pd_pkt_alloc_mutex);
	}

	sp = &fp->fp_scsi_cmd;

	/*
	 * If all went well, set up various fields in the structures
	 * for the next I/O operation.  The pln_fc_pkt, scsi_pkt,
	 * fc_packet, etc. are all already threaded together.
	 */
	if (!failure) {
		bzero((caddr_t)sp, sizeof (struct pln_scsi_cmd));
		if (cdbp != (caddr_t)0) {
			sp->cmd_pkt.pkt_cdbp = (opaque_t)cdbp;
			sp->cmd_flags |= P_CFLAG_CDBEXTERN;
		} else {
			sp->cmd_pkt.pkt_cdbp = (opaque_t)&sp->cmd_cdb_un;
		}
		if (tgtp != (caddr_t)0) {
			sp->cmd_pkt.pkt_private = (opaque_t)tgtp;
			sp->cmd_flags |= P_CFLAG_TGTEXTERN;
		}
		if (scbp != (caddr_t)0) {
			sp->cmd_pkt.pkt_scbp = (opaque_t)scbp;
			sp->cmd_flags |= P_CFLAG_SCBEXTERN;
		} else {
/*
 * This looks broken to me in 1093 fcs code
 * Shouldn't CFLAG_EXTCMDS_ALLOC only be set if either cmd or scb
 * allocated?
 */
			sp->cmd_pkt.pkt_scbp = (opaque_t)&sp->cmd_scsi_scb;
		}
		if (!cdbp && !tgtp && !scbp) {
			sp->cmd_flags = P_CFLAG_EXTCMDS_ALLOC;
		}
		sp->cmd_cdblen = (u_char) cmdlen;
		sp->cmd_senselen = (u_char)statuslen;
		sp->cmd_tgtlen = (u_char) tgtlen;
#ifdef	ON1093
		sp->cmd_pkt.pkt_address.a_cookie = addr->a_cookie;
		sp->cmd_pkt.pkt_address.a_addr_ptr = addr->a_addr_ptr;
#else	ON1093
		sp->cmd_pkt.pkt_address.a_hba_tran = pln->pln_tran;
		sp->cmd_pkt.pkt_ha_private = sp;
#endif	/* ON1093 */
		sp->cmd_fc_pkt = (caddr_t)fp;
		fp->fp_timeout = 0;
		fp->fp_timeout_flag = 0;
	} else {

	/*
	 * We couldn't allocate everything we needed this time.
	 * Free the pieces we did allocate, and set up a callback,
	 * if required.
	 */
		if (cdbp) {
			kmem_free(cdbp, (size_t)cmdlen);
		}
		if (scbp) {
			kmem_free(scbp, (size_t)statuslen);
		}
		if (tgtp) {
			kmem_free(tgtp, (size_t)tgtlen);
		}
		if (fp_failure) {
		    if (fp) {
			if (fp->fp_pkt) {
			    fc = pln->pln_fc_tran;
			    fc->fc_pkt_free(fc->fc_cookie, fp->fp_pkt);
			}
			kmem_free(fp, (size_t)(sizeof (struct pln_fc_pkt)));
		    }
		}

		if (callback != NULL_FUNC) {
			ddi_set_callback(callback, callback_arg,
				&pd->pd_resource_cb_id);
		}
		sp = NULL;
	}
	return ((struct scsi_pkt *)sp);
}

/*
 * Function name : pln_scsi_pktfree()
 *
 * Return Values : none
 * Description	 : return pkt to the free pool
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
pln_scsi_pktfree(struct scsi_pkt *pkt)
{
	struct pln_scsi_cmd *sp = (struct pln_scsi_cmd *)pkt;
	register struct pln_disk *pd;
	register struct pln_fc_pkt *fp;
	register struct pln *pln = (struct pln *)PKT2TRAN(pkt);

	fp = (struct pln_fc_pkt *)sp->cmd_fc_pkt;
	pd = fp->fp_pd;

#ifndef	__lock_lint
	ASSERT(fp->fp_state == FP_STATE_IDLE);
#endif	__lock_lint
	ASSERT(!fp->fp_timeout_flag);

	/*
	 * See if there's something extra to free (from an extra-large
	 * cdb or response or target area allocated)
	 */
	if ((sp->cmd_flags & (P_CFLAG_FREE | P_CFLAG_CDBEXTERN |
	    P_CFLAG_SCBEXTERN | P_CFLAG_EXTCMDS_ALLOC)) !=
		P_CFLAG_EXTCMDS_ALLOC) {
		if (sp->cmd_flags & P_CFLAG_FREE) {
			pln_disp_err(pln->pln_dip, CE_PANIC,
			    "pln_scsi_pktfree: freeing free packet");
			_NOTE(NOT_REACHED);
			/* NOTREACHED */
		}
		if (sp->cmd_flags & P_CFLAG_CDBEXTERN) {
			kmem_free((caddr_t)sp->cmd_pkt.pkt_cdbp,
			    (size_t)sp->cmd_cdblen);
		}
		if (sp->cmd_flags & P_CFLAG_TGTEXTERN) {
			kmem_free((caddr_t)sp->cmd_pkt.pkt_private,
			    (size_t)sp->cmd_tgtlen);
		}
		if (sp->cmd_flags & P_CFLAG_SCBEXTERN) {
			kmem_free((caddr_t)sp->cmd_pkt.pkt_scbp,
			    (size_t)sp->cmd_senselen);
		}
	}

	mutex_enter(&pd->pd_pkt_alloc_mutex);
	sp->cmd_flags = P_CFLAG_FREE;


	/* Delete from the "inuse" list */
	if (fp->fp_next)
	    fp->fp_next->fp_prev = fp->fp_prev;
	else
	    pd->pd_inuse_tail = fp->fp_prev;

	if (fp->fp_prev)
	    fp->fp_prev->fp_next = fp->fp_next;
	else
	    pd->pd_inuse_head = fp->fp_next;

	/* Stick it on the "free" list */
	fp->fp_state = FP_STATE_FREE;
	fp->fp_next = pd->pd_pkt_pool;
	pd->pd_pkt_pool = fp;
	mutex_exit(&pd->pd_pkt_alloc_mutex);

	/* Call those folks that are waiting for resources */
	if (pd->pd_resource_cb_id) {
		ddi_run_callback(&pd->pd_resource_cb_id);
	}
}

/*
 * A routine to allocate an fcp command or response structure and
 * the associated dvma mapping
 *
 * Return Values : -1   Could not queue cmd or allocate structure
 *		    1   fcp structure allocated
 *		    0   request queued
 *
 */
static int
pln_cr_alloc(
	struct pln	*pln,
	pln_fc_pkt_t	*fp)
{
	pln_cr_pool_t			*cp = &pln->pln_cmd_pool;
	pln_cr_free_t			*pp;
	pln_fc_pkt_t			*fp_free;
	int				rval;

	mutex_enter(&pln->pln_state_mutex);
	mutex_enter(&pln->pln_cr_mutex);

	/*
	 * If the free list is empty, queue up the request
	 */
	if ((pp = cp->free) == NULL) {

	    if (fp->fp_cr_callback && !(pln->pln_state & PLN_STATE_LINK_DOWN)) {
		if ((fp_free = cp->waiters_tail) != NULL) {
		    fp_free->fp_cr_next = fp;
		} else {
		    cp->waiters_head = fp;
		}
		cp->waiters_tail = fp;
		fp->fp_cr_next = NULL;
		rval = 0;
	    } else {
		rval = -1;
	    }
	    mutex_exit(&pln->pln_cr_mutex);
	    mutex_exit(&pln->pln_state_mutex);

	    return (rval);
	}

	/*
	 * Grab the first thing from the free list
	 */
	cp->free = pp->next;
	mutex_exit(&pln->pln_cr_mutex);
	mutex_exit(&pln->pln_state_mutex);

	/*
	 * Fill in the fields in the pln_fc_pkt for the fcp cmd/rsp
	 */
	fp->fp_cmd = (struct fcp_cmd *)pp;
	fp->fp_rsp = (struct pln_rsp *)pp->rsp;
	fp->fp_cmdseg.fc_count = sizeof (struct fcp_cmd);
	fp->fp_cmdseg.fc_base = pp->cmd_dmac;
	fp->fp_rspseg.fc_count = sizeof (struct pln_rsp);
	fp->fp_rspseg.fc_base = pp->rsp_dmac;

	return (1);
}

/*
 * Free an fcp command or response buffer
 */
static void
pln_cr_free(
	struct pln	*pln,
	pln_fc_pkt_t	*fp)
{
	pln_cr_free_t			*pp;
	pln_fc_pkt_t			*fp_wait;
	pln_cr_pool_t			*cp;

	cp = &pln->pln_cmd_pool;

	pp = (pln_cr_free_t *)fp->fp_cmd;
	ASSERT(pp != NULL);

	/* Fill in the free element's fields */
	pp->rsp = (caddr_t)fp->fp_rsp;
	pp->cmd_dmac = fp->fp_cmdseg.fc_base;
	pp->rsp_dmac = fp->fp_rspseg.fc_base;

	/* For freeing-free-iopb detection... */
	fp->fp_cmd = NULL;
	fp->fp_rsp = NULL;

	mutex_enter(&pln->pln_cr_mutex);

	/*
	 * If someone is waiting for a cmd/rsp, then give this one to them.
	 * Otherwise, add it to the free list.
	 */
	if ((fp_wait = cp->waiters_head) != NULL) {

	    if ((cp->waiters_head = fp_wait->fp_cr_next) == NULL)
		cp->waiters_tail = NULL;

	    mutex_exit(&pln->pln_cr_mutex);

	    fp_wait->fp_cmd = (struct fcp_cmd *)pp;
	    fp_wait->fp_rsp = (struct pln_rsp *)pp->rsp;
	    fp_wait->fp_cmdseg.fc_count = sizeof (struct fcp_cmd);
	    fp_wait->fp_cmdseg.fc_base = pp->cmd_dmac;
	    fp_wait->fp_rspseg.fc_count = sizeof (struct pln_rsp);
	    fp_wait->fp_rspseg.fc_base = pp->rsp_dmac;

	    ASSERT(fp_wait->fp_cr_callback);

	    (*fp_wait->fp_cr_callback)(fp_wait);

	    return;

	} else {
	    pp->next = cp->free;
	    cp->free = pp;
	}

	mutex_exit(&pln->pln_cr_mutex);
}

/*
 * Allocate IOPB and DVMA space for FCP command/responses
 * Should only be called from user context.
 *
 * We allocate a static amount of this stuff at attach() time, since
 * it's too expensive to allocate/free space from the system-wide pool
 * for each command, and, as of this writing, ddi_iopb_alloc() will
 * always cv_wait() when it runs out, so we can't even get an
 * affirmative indication back when the system is out of iopb space.
 */
static int
pln_cr_init(
	struct pln	*pln)
{
	pln_cr_pool_t		*cp;
	int			cmd_buf_size,
				rsp_buf_size;
	pln_cr_free_t		*cptr;
	caddr_t			dptr,
				eptr;
	struct fc_transport	*fc = pln->pln_fc_tran;
	ddi_dma_cookie_t		cookie;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(cptr->next))
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(cp->free))

	cmd_buf_size = sizeof (struct fcp_cmd) * pln_fcp_elements;
	rsp_buf_size = sizeof (struct pln_rsp) * pln_fcp_elements;

	cp = &pln->pln_cmd_pool;

	/*
	 * Get a piece of memory in which to put commands
	 */
	if (ddi_iopb_alloc(pln->pln_dip, fc->fc_dmalimp, cmd_buf_size,
		(caddr_t *)&cp->cmd_base) != DDI_SUCCESS) {
	    cp->cmd_base = NULL;
	    goto fail;
	}

	/*
	 * Allocate dma resources for the payload
	 */
	if (ddi_dma_addr_setup(pln->pln_dip, (struct as *)NULL,
		cp->cmd_base, cmd_buf_size,
		DDI_DMA_WRITE | DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT, NULL,
		fc->fc_dmalimp, &cp->cmd_handle) !=
			DDI_DMA_MAPPED) {
	    cp->cmd_handle = NULL;
	    goto fail;
	}

	/*
	 * Get a piece of memory in which to put responses
	 */
	if (ddi_iopb_alloc(pln->pln_dip, fc->fc_dmalimp, rsp_buf_size,
		(caddr_t *)&cp->rsp_base) != DDI_SUCCESS) {
	    cp->rsp_base = NULL;
	    goto fail;
	}

	/*
	 * Allocate dma resources for the payload
	 */
	if (ddi_dma_addr_setup(pln->pln_dip, (struct as *)NULL,
		cp->rsp_base, rsp_buf_size,
		DDI_DMA_READ | DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT, NULL,
		fc->fc_dmalimp, &cp->rsp_handle) !=
			DDI_DMA_MAPPED) {
	    cp->rsp_handle = NULL;
	    goto fail;
	}

	/*
	 * Generate a (cmd/rsp structure) free list
	 */
	dptr = cp->cmd_base;
	eptr = cp->rsp_base;

	cp->free = (pln_cr_free_t *)cp->cmd_base;

	while (dptr <=
		(cp->cmd_base + cmd_buf_size - sizeof (struct fcp_cmd))) {
	    cptr = (pln_cr_free_t *)dptr;
	    dptr += sizeof (struct fcp_cmd);

	    cptr->next = (pln_cr_free_t *)dptr;
	    cptr->rsp = eptr;

	    /* Get the dvma cookies for this piece */
	    if (ddi_dma_htoc(cp->cmd_handle,
		    (off_t)((caddr_t)cptr - cp->cmd_base), &cookie) !=
		    DDI_SUCCESS) {
		goto fail;
	    }
	    cptr->cmd_dmac = cookie.dmac_address;

	    if (ddi_dma_htoc(cp->rsp_handle,
		    (off_t)(eptr - cp->rsp_base), &cookie) !=
		    DDI_SUCCESS) {
		goto fail;
	    }
	    cptr->rsp_dmac = cookie.dmac_address;

	    eptr += sizeof (struct pln_rsp);
	}

	/* terminate the list */
	cptr->next = NULL;

	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(cptr->next))
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(cp->free))

	return (1);

fail:
	if (cp->cmd_handle)
	    ddi_dma_free(cp->cmd_handle);

	if (cp->cmd_base)
	    ddi_iopb_free(cp->cmd_base);

	if (cp->rsp_handle)
	    ddi_dma_free(cp->rsp_handle);

	if (cp->rsp_base)
	    ddi_iopb_free(cp->rsp_base);

	cp->free = NULL;
	cp->cmd_base = NULL;
	cp->rsp_base = NULL;
	cp->cmd_handle = NULL;
	cp->rsp_handle = NULL;

	return (0);
}


/*
 * A common routine used to display error messages
 *
 * NOTE:  this routine should be called only when processing commands,
 *	  and not for unsolicited messages if no commands are queued
 *	  (to avoid a dev_ops.devo_revcnt usage count assertion inside
 *	  of scsi_log())
 */
static void
pln_disp_err(
	dev_info_t	*dip,
	u_int		level,
	char		*msg)
{
	scsi_log(dip, ddi_get_name(dip), level, msg);
}

#ifdef	PLNDEBUG
static void
pln_printf(
	dev_info_t	*dip,
	const char	*format,
			...)
{
	va_list		ap;
	char		buf[256];

	sprintf(buf, "%s%d: ", ddi_get_name(dip), ddi_get_instance(dip));

	va_start(ap, format);
	vsprintf(&buf[strlen(buf)], format, ap);
	va_end(ap);

#ifdef	PLNLOGGING
	if (plnlog) {
		char *dst = plnlog_buf[plnlog_ptr1];
		if (++plnlog_ptr1 == plnlog_nmsgs) {
			plnlog_ptr1 = 0;
		}
		if (plnlog_ptr1 == plnlog_ptr2) {
			if (++plnlog_ptr2 == plnlog_nmsgs) {
				plnlog_ptr2 = 0;
			}
		}
		(void) strncpy(dst, buf, plnlog_msglen-1);
	}
#endif	/* PLNLOGGING */

	if (plndebug) {
		cmn_err(CE_CONT, buf);
	}
}
#endif	/* PLNDEBUG */


#ifdef	PLNDEBUG
static char *
pln_cdb_str(
	char		*s,
	u_char		*cdb,
	int		cdblen)
{
	static char	hex[] = "0123456789abcdef";
	char		*p;
	int		i;

	p = s;
	*p++ = '[';
	for (i = 0; i < cdblen; i++, cdb++) {
		*p++ = hex[(*cdb >> 4) & 0x0f];
		*p++ = hex[*cdb & 0x0f];
		*p++ = (i == cdblen-1) ? ']' : ' ';
	}
	*p = 0;

	return (s);
}


/*
 * Dump data in hex for debugging
 */
static void
pln_dump(
	char	*msg,
	u_char	*addr,
	int	len)
{
	static char	hex[] = "0123456789abcdef";
	char		buf[256];
	char		*p;
	int		i;
	int		n;
	int		msglen = -1;

	while (len > 0) {
		p = buf;
		if (msglen == -1) {
			msglen = strlen(msg);
			while (*msg) {
				*p++ = *msg++;
			}
		} else {
			for (i = 0; i < msglen; i++) {
				*p++ = ' ';
			}
		}
		n = min(len, 16);
		for (i = 0; i < n; i++, addr++) {
			*p++ = hex[(*addr >> 4) & 0x0f];
			*p++ = hex[*addr & 0x0f];
			*p++ = ' ';
		}
		*p++ = '\n';
		*p = 0;

		cmn_err(CE_CONT, buf);
		len -= n;
	}
}

#endif	/* PLNDEBUG */
