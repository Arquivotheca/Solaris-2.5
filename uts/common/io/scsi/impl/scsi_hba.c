/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident  "@(#)scsi_hba.c 1.36     95/02/13 SMI"

/*
 * Generic SCSI Host Bus Adapter interface implementation
 */
#include <sys/scsi/scsi.h>

static kmutex_t	scsi_hba_mutex;

kmutex_t scsi_log_mutex;


struct scsi_hba_inst {
	dev_info_t		*inst_dip;
	scsi_hba_tran_t		*inst_hba_tran;
	struct scsi_hba_inst	*inst_next;
	struct scsi_hba_inst	*inst_prev;
};

static struct scsi_hba_inst	*scsi_hba_list		= NULL;
static struct scsi_hba_inst	*scsi_hba_list_tail	= NULL;


/* READ-ONLY VARIABLES: dev_ops:: */

kmutex_t	scsi_flag_nointr_mutex;
kcondvar_t	scsi_flag_nointr_cv;

/*
 * Prototypes for static functions
 */
static int	scsi_hba_bus_ctl(
			dev_info_t		*dip,
			dev_info_t		*rdip,
			ddi_ctl_enum_t		op,
			void			*arg,
			void			*result);

static int	scsi_hba_map_fault(
			dev_info_t		*dip,
			dev_info_t		*rdip,
			struct hat		*hat,
			struct seg		*seg,
			caddr_t			addr,
			struct devpage		*dp,
			u_int			pfn,
			u_int			prot,
			u_int			lock);


/*
 * Busops vector for SCSI HBA's.
 */
static struct bus_ops scsi_hba_busops = {
	BUSO_REV,
	nullbusmap,			/* bus_map */
	NULL,				/* bus_get_intrspec */
	NULL,				/* bus_add_intrspec */
	NULL,				/* bus_remove_intrspec */
	scsi_hba_map_fault,		/* bus_map_fault */
	ddi_dma_map,			/* bus_dma_map */
	ddi_dma_allochdl,
	ddi_dma_freehdl,
	ddi_dma_bindhdl,
	ddi_dma_unbindhdl,
	ddi_dma_flush,
	ddi_dma_win,
	ddi_dma_mctl,			/* bus_dma_ctl */
	scsi_hba_bus_ctl,		/* bus_ctl */
	ddi_bus_prop_op			/* bus_prop_op */
};


/*
 * Called from _init() when loading scsi module
 */
void
scsi_initialize_hba_interface()
{
	mutex_init(&scsi_hba_mutex, "SCSI HBA Mutex",
		MUTEX_DRIVER, (void *) NULL);
	mutex_init(&scsi_flag_nointr_mutex, "SCSI FLAG_NOINTR Mutex",
		MUTEX_DRIVER, (void *) NULL);
	cv_init(&scsi_flag_nointr_cv, "scsi_flag_nointr_cv",
		CV_DRIVER, NULL);
	mutex_init(&scsi_log_mutex, "scsi_log Mutex",
		MUTEX_DRIVER, (void *) NULL);
}

#ifdef	NO_SCSI_FINI_YET
/*
 * Called from _fini() when unloading scsi module
 */
void
scsi_uninitialize_hba_interface()
{
	mutex_destroy(&scsi_hba_mutex);
	cv_destroy(&scsi_flag_nointr_cv);
	mutex_destroy(&scsi_flag_nointr_mutex);
	mutex_destroy(&scsi_log_mutex);
}
#endif	/* NO_SCSI_FINI_YET */



/*
 * Called by an HBA from _init()
 */
int
scsi_hba_init(struct modlinkage *modlp)
{
	struct dev_ops *hba_dev_ops;

	/*
	 * Get the devops structure of the hba,
	 * and put our busops vector in its place.
	 */
	hba_dev_ops = ((struct modldrv *)
		(modlp->ml_linkage[0]))->drv_dev_ops;
#ifndef __lock_lint
	ASSERT(hba_dev_ops->devo_bus_ops == NULL);
	hba_dev_ops->devo_bus_ops = &scsi_hba_busops;
#endif __lock_lint

	return (0);
}


/*
 * Implement this older interface in terms of the new.
 * This is hardly in the critical path, so avoiding
 * unnecessary code duplication is more important.
 */
/*ARGSUSED*/
int
scsi_hba_attach(
	dev_info_t		*dip,
	ddi_dma_lim_t		*hba_lim,
	scsi_hba_tran_t		*hba_tran,
	int			flags,
	void			*hba_options)
{
	ddi_dma_attr_t		hba_dma_attr;

	bzero((caddr_t)&hba_dma_attr, sizeof (ddi_dma_attr_t));

	hba_dma_attr.dma_attr_burstsizes = hba_lim->dlim_burstsizes;
	hba_dma_attr.dma_attr_minxfer = hba_lim->dlim_minxfer;

	return (scsi_hba_attach_setup(dip, &hba_dma_attr, hba_tran, flags));
}


/*
 * Called by an HBA to attach an instance of the driver
 */
int
scsi_hba_attach_setup(
	dev_info_t		*dip,
	ddi_dma_attr_t		*hba_dma_attr,
	scsi_hba_tran_t		*hba_tran,
	int			flags)
{
	struct scsi_hba_inst	*elem;
	int			value;
	int			len;
	char			*prop_name;
	char			*errmsg =
		"scsi_hba_attach: cannot create property '%s' for %s%d\n";

	/*
	 * Link this instance into the scsi_hba_list
	 */
	elem = kmem_alloc(sizeof (struct scsi_hba_inst), KM_SLEEP);

	elem->inst_dip = dip;
	elem->inst_hba_tran = hba_tran;

	mutex_enter(&scsi_hba_mutex);
	elem->inst_next = NULL;
	elem->inst_prev = scsi_hba_list_tail;
	if (scsi_hba_list == NULL) {
		scsi_hba_list = elem;
	}
	if (scsi_hba_list_tail) {
		scsi_hba_list_tail->inst_next = elem;
	}
	scsi_hba_list_tail = elem;
	mutex_exit(&scsi_hba_mutex);

	/*
	 * Save all the important HBA information that must be accessed
	 * later by scsi_hba_bus_ctl(), and scsi_hba_map().
	 */
	hba_tran->tran_hba_dip = dip;
	hba_tran->tran_hba_flags = flags;

	/*
	 * Note: we only need dma_attr_minxfer and dma_attr_burstsizes
	 * from the DMA attributes.  scsi_hba_attach(9f) only
	 * guarantees that these two fields are initialized properly.
	 * If this changes, be sure to revisit the implementation
	 * of scsi_hba_attach(9F).
	 */
	hba_tran->tran_min_xfer = hba_dma_attr->dma_attr_minxfer;
	hba_tran->tran_min_burst_size =
		(1<<(ddi_ffs(hba_dma_attr->dma_attr_burstsizes)-1));
	hba_tran->tran_max_burst_size =
		(1<<(ddi_fls(hba_dma_attr->dma_attr_burstsizes)-1));

	/*
	 * Attach scsi configuration property parameters
	 * to this instance of the hba.
	 */
	prop_name = "scsi-reset-delay";
	len = 0;
	if (ddi_prop_op(DDI_DEV_T_ANY, dip, PROP_LEN, 0, prop_name,
			NULL, &len) == DDI_PROP_NOT_FOUND) {
		value = scsi_reset_delay;
		if (ddi_prop_create(DDI_MAJOR_T_UNKNOWN, dip,
			DDI_PROP_CANSLEEP, prop_name, (caddr_t)&value,
				sizeof (int)) != DDI_PROP_SUCCESS) {
			cmn_err(CE_CONT, errmsg, prop_name,
				ddi_get_name(dip), ddi_get_instance(dip));
		}
	}

	prop_name = "scsi-tag-age-limit";
	len = 0;
	if (ddi_prop_op(DDI_DEV_T_ANY, dip, PROP_LEN, 0, prop_name,
			NULL, &len) == DDI_PROP_NOT_FOUND) {
		value = scsi_tag_age_limit;
		if (ddi_prop_create(DDI_MAJOR_T_UNKNOWN, dip,
			DDI_PROP_CANSLEEP, prop_name, (caddr_t)&value,
				sizeof (int)) != DDI_PROP_SUCCESS) {
			cmn_err(CE_CONT, errmsg, prop_name,
				ddi_get_name(dip), ddi_get_instance(dip));
		}
	}

	prop_name = "scsi-watchdog-tick";
	len = 0;
	if (ddi_prop_op(DDI_DEV_T_ANY, dip, PROP_LEN, 0, prop_name,
			NULL, &len) == DDI_PROP_NOT_FOUND) {
		value = scsi_watchdog_tick;
		if (ddi_prop_create(DDI_MAJOR_T_UNKNOWN, dip,
			DDI_PROP_CANSLEEP, prop_name, (caddr_t)&value,
				sizeof (int)) != DDI_PROP_SUCCESS) {
			cmn_err(CE_CONT, errmsg, prop_name,
				ddi_get_name(dip), ddi_get_instance(dip));
		}
	}

	prop_name = "scsi-options";
	len = 0;
	if (ddi_prop_op(DDI_DEV_T_ANY, dip, PROP_LEN, 0, prop_name,
			NULL, &len) == DDI_PROP_NOT_FOUND) {
		value = scsi_options;
		if (ddi_prop_create(DDI_MAJOR_T_UNKNOWN, dip,
			DDI_PROP_CANSLEEP, prop_name, (caddr_t)&value,
				sizeof (int)) != DDI_PROP_SUCCESS) {
			cmn_err(CE_CONT, errmsg, prop_name,
				ddi_get_name(dip), ddi_get_instance(dip));
		}
	}

	ddi_set_driver_private(dip, (caddr_t)hba_tran);

	return (DDI_SUCCESS);
}


/*
 * Called by an HBA to detach an instance of the driver
 */
int
scsi_hba_detach(dev_info_t *dip)
{
	scsi_hba_tran_t		*hba;

	/*
	 * XXX To do: remove instance from scsi_hba_list
	 */

	hba = (scsi_hba_tran_t *)ddi_get_driver_private(dip);
	ddi_set_driver_private(dip, NULL);
	ASSERT(hba != NULL);

	return (DDI_SUCCESS);
}


#ifdef	__lock_lint
/*
 *	dummy function to shut warlock warnings
 */
int
func_to_shutup_warlock()
{
}
#endif __lock_lint



/*
 * Called by an HBA from _fini()
 */
void
scsi_hba_fini(struct modlinkage *modlp)
{
	struct dev_ops *hba_dev_ops;

	/*
	 * Get the devops structure of this module
	 * and clear bus_ops vector.
	 */
	hba_dev_ops = ((struct modldrv *)
		(modlp->ml_linkage[0]))->drv_dev_ops;
#ifndef __lock_lint
	hba_dev_ops->devo_bus_ops = (struct bus_ops *)NULL;
#endif __lock_lint
}


/*
 * Generic bus_ctl operations for SCSI HBA's,
 * hiding the busctl interface from the HBA.
 */
/*ARGSUSED*/
static int
scsi_hba_bus_ctl(
	dev_info_t		*dip,
	dev_info_t		*rdip,
	ddi_ctl_enum_t		op,
	void			*arg,
	void			*result)
{

	switch (op) {
	case DDI_CTLOPS_REPORTDEV:
	{
		struct scsi_device	*devp;

		devp = (struct scsi_device *)ddi_get_driver_private(rdip);
		cmn_err(CE_CONT, "?%s%d at %s%d:",
		    ddi_get_name(rdip), ddi_get_instance(rdip),
		    ddi_get_name(dip), ddi_get_instance(dip));
		cmn_err(CE_CONT, "? target %x lun %x\n",
		    devp->sd_address.a_target, devp->sd_address.a_lun);
		return (DDI_SUCCESS);
	}

	case DDI_CTLOPS_IOMIN:
	{
		int		val;
		scsi_hba_tran_t	*hba;

		hba = (scsi_hba_tran_t *)ddi_get_driver_private(dip);
		ASSERT(hba != NULL);

		val = *((int *)result);
		val = maxbit(val, hba->tran_min_xfer);
		/*
		 * The 'arg' value of nonzero indicates 'streaming'
		 * mode.  If in streaming mode, pick the largest
		 * of our burstsizes available and say that that
		 * is our minimum value (modulo what minxfer is).
		 */
		*((int *)result) = maxbit(val, ((int)arg ?
			hba->tran_max_burst_size :
			hba->tran_min_burst_size));

		return (ddi_ctlops(dip, rdip, op, arg, result));
	}

	case DDI_CTLOPS_INITCHILD:
	{
		dev_info_t		*child_dip = (dev_info_t *)arg;
		struct scsi_device	*sd;
		int			t_len;
		int			targ;
		int			lun;
		char			name[MAXNAMELEN];
		scsi_hba_tran_t		*hba;

		hba = (scsi_hba_tran_t *)ddi_get_driver_private(dip);
		ASSERT(hba != NULL);

		t_len = sizeof (targ);
		if (ddi_prop_op(DDI_DEV_T_ANY, child_dip, PROP_LEN_AND_VAL_BUF,
		    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "target",
		    (caddr_t)&targ, &t_len) != DDI_SUCCESS)
			return (DDI_NOT_WELL_FORMED);

		if (ddi_prop_op(DDI_DEV_T_ANY, child_dip, PROP_LEN_AND_VAL_BUF,
		    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "lun",
		    (caddr_t)&lun, &t_len) != DDI_SUCCESS) {
			lun = 0;
		}

		sprintf(name, "%x,%x", targ, lun);
		ddi_set_name_addr(child_dip, name);

		sd = kmem_zalloc(sizeof (struct scsi_device), KM_SLEEP);

		/*
		 * Clone transport structure if requested, so
		 * the HBA can maintain target-specific info,
		 * if necessary.  Most HBA's won't require this.
		 */
		if (hba->tran_hba_flags & SCSI_HBA_TRAN_CLONE) {
			scsi_hba_tran_t	*clone =
				kmem_alloc(sizeof (scsi_hba_tran_t),
					KM_SLEEP);
			bcopy((caddr_t)hba, (caddr_t)clone,
				sizeof (scsi_hba_tran_t));
			hba = clone;
#ifndef __lock_lint
			hba->tran_sd = sd;
#endif
		} else {
			ASSERT(hba->tran_sd == NULL);
		}

		sd->sd_dev = child_dip;
		sd->sd_address.a_hba_tran = hba;
		sd->sd_address.a_target = (u_short)targ;
		sd->sd_address.a_lun = (u_char)lun;

		/*
		 * This is a grotty hack that allows direct-access
		 * (non-scsi) drivers using this interface to
		 * put its own vector in the 'a_hba_tran' field.
		 * When the drivers are fixed, remove this hack.
		 */
		sd->sd_reserved = hba;

		if (hba->tran_tgt_init != NULL) {
			if ((*hba->tran_tgt_init)
			    (dip, child_dip, hba, sd) != DDI_SUCCESS) {
				kmem_free(sd, sizeof (struct scsi_device));
				if (hba->tran_hba_flags & SCSI_HBA_TRAN_CLONE) {
					kmem_free(hba,
						sizeof (scsi_hba_tran_t));
				}
				ddi_set_name_addr(child_dip, NULL);
				return (DDI_FAILURE);
			}

			/*
			 * Another grotty hack to undo initialization
			 * some hba's think they have authority to
			 * perform.
			 *
			 * XXX - Pending dadk_probe() semantics
			 *       change.  (Re: 1171432)
			 */
			if (hba->tran_tgt_probe != NULL)
				sd->sd_inq = NULL;
		}

		sprintf(name, "sd mutex %s%d:%x.%x", ddi_get_name(dip),
		    ddi_get_instance(dip), targ, lun);
		mutex_init(&sd->sd_mutex, name, MUTEX_DRIVER, NULL);

		ddi_set_driver_private(child_dip, (caddr_t)sd);

		return (DDI_SUCCESS);
	}

	case DDI_CTLOPS_UNINITCHILD:
	{
		struct scsi_device	*sd;
		dev_info_t		*child_dip = (dev_info_t *)arg;
		scsi_hba_tran_t		*hba;

		hba = (scsi_hba_tran_t *)ddi_get_driver_private(dip);
		ASSERT(hba != NULL);

		sd = (struct scsi_device *)ddi_get_driver_private(child_dip);
		ASSERT(sd != NULL);

		if (hba->tran_hba_flags & SCSI_HBA_TRAN_CLONE) {
			/*
			 * This is a grotty hack, continued.  This
			 * should be:
			 *     hba = sd->sd_address.a_hba_tran;
			 */
			hba = sd->sd_reserved;
			ASSERT(hba->tran_hba_flags & SCSI_HBA_TRAN_CLONE);
			ASSERT(hba->tran_sd == sd);
		} else {
			ASSERT(hba->tran_sd == NULL);
		}

		scsi_unprobe(sd);
		if (hba->tran_tgt_free != NULL) {
			(*hba->tran_tgt_free) (dip, child_dip, hba, sd);
		}
		mutex_destroy(&sd->sd_mutex);
		if (hba->tran_hba_flags & SCSI_HBA_TRAN_CLONE) {
			kmem_free(hba, sizeof (scsi_hba_tran_t));
		}
		kmem_free((caddr_t)sd, sizeof (*sd));

		ddi_set_driver_private(child_dip, NULL);
		ddi_set_name_addr(child_dip, NULL);

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
	case DDI_CTLOPS_XLATE_INTRS:
		cmn_err(CE_CONT, "%s%d: invalid op (%d) from %s%d\n",
			ddi_get_name(dip), ddi_get_instance(dip),
			op, ddi_get_name(rdip), ddi_get_instance(rdip));
		return (DDI_FAILURE);

	/*
	 * Everything else (e.g. PTOB/BTOP/BTOPR requests) we pass up
	 */
	default:
		return (ddi_ctlops(dip, rdip, op, arg, result));
	}
}


/*
 * Called by an HBA to allocate a scsi_hba_tran structure
 */
/*ARGSUSED*/
scsi_hba_tran_t *
scsi_hba_tran_alloc(
	dev_info_t		*dip,
	int			flags)
{
	return (kmem_zalloc(sizeof (scsi_hba_tran_t),
		(flags & SCSI_HBA_CANSLEEP) ? KM_SLEEP : KM_NOSLEEP));
}



/*
 * Called by an HBA to free a scsi_hba_tran structure
 */
void
scsi_hba_tran_free(
	scsi_hba_tran_t		*hba_tran)
{
	kmem_free(hba_tran, sizeof (scsi_hba_tran_t));
}



/*
 * Private wrapper for scsi_pkt's allocated via scsi_hba_pkt_alloc()
 */
struct scsi_pkt_wrapper {
	struct scsi_pkt		scsi_pkt;
	int			pkt_wrapper_len;
};

/* ALL MEMBERS PROTECTED BY "unique per thread" */

/*
 * Round up all allocations so that we can guarantee
 * long-long alignment.  This is the same alignment
 * provided by kmem_alloc().
 */
#define	ROUNDUP(x)	(((x) + 0x07) & ~0x07)

/*
 * Called by an HBA to allocate a scsi_pkt
 */
/*ARGSUSED*/
struct scsi_pkt *
scsi_hba_pkt_alloc(
	dev_info_t		*dip,
	struct scsi_address	*ap,
	int			cmdlen,
	int			statuslen,
	int			tgtlen,
	int			hbalen,
	int			(*callback)(caddr_t arg),
	caddr_t			arg)
{
	struct scsi_pkt		*pkt;
	struct scsi_pkt_wrapper	*hba_pkt;
	caddr_t			p;
	int			pktlen;

	/*
	 * Sanity check
	 */
	if (callback != SLEEP_FUNC && callback != NULL_FUNC) {
		cmn_err(CE_PANIC, "scsi_hba_pkt_alloc: callback must be"
			" either SLEEP or NULL\n");
	}

	/*
	 * Round up so everything gets allocated on long-word boundaries
	 */
	cmdlen = ROUNDUP(cmdlen);
	tgtlen = ROUNDUP(tgtlen);
	hbalen = ROUNDUP(hbalen);
	statuslen = ROUNDUP(statuslen);
	pktlen = sizeof (struct scsi_pkt_wrapper)
		+ cmdlen + tgtlen + hbalen + statuslen;

	hba_pkt = kmem_zalloc(pktlen,
		(callback == SLEEP_FUNC) ? KM_SLEEP : KM_NOSLEEP);
	if (hba_pkt == NULL) {
		ASSERT(callback == NULL_FUNC);
		return (NULL);
	}

	/*
	 * Set up our private info on this pkt
	 */
	hba_pkt->pkt_wrapper_len = pktlen;
	pkt = &hba_pkt->scsi_pkt;
	p = (caddr_t)(hba_pkt + 1);

	/*
	 * Set up pointers to private data areas, cdb, and status.
	 */
	if (hbalen > 0) {
		pkt->pkt_ha_private = (opaque_t)p;
		p += hbalen;
	}
	if (tgtlen > 0) {
		pkt->pkt_private = (opaque_t)p;
		p += tgtlen;
	}
	if (statuslen > 0) {
		pkt->pkt_scbp = (u_char *)p;
		p += statuslen;
	}
	if (cmdlen > 0) {
		pkt->pkt_cdbp = (u_char *)p;
	}

	/*
	 * Initialize the pkt's scsi_address
	 */
	pkt->pkt_address = *ap;

	return (pkt);
}


/*
 * Called by an HBA to free a scsi_pkt
 */
/*ARGSUSED*/
void
scsi_hba_pkt_free(
	struct scsi_address	*ap,
	struct scsi_pkt		*pkt)
{
	kmem_free((struct scsi_pkt_wrapper *)pkt,
		((struct scsi_pkt_wrapper *)pkt)->pkt_wrapper_len);
}



/*
 * Called by an HBA to map strings to capability indices
 */
int
scsi_hba_lookup_capstr(
	char			*capstr)
{
	/*
	 * Capability strings, masking the the '-' vs. '_' misery
	 */
	static struct cap_strings {
		char	*cap_string;
		int	cap_index;
	} cap_strings[] = {
		{ "dma_max",		SCSI_CAP_DMA_MAX		},
		{ "dma-max",		SCSI_CAP_DMA_MAX		},
		{ "msg_out",		SCSI_CAP_MSG_OUT		},
		{ "msg-out",		SCSI_CAP_MSG_OUT		},
		{ "disconnect",		SCSI_CAP_DISCONNECT		},
		{ "synchronous",	SCSI_CAP_SYNCHRONOUS		},
		{ "wide_xfer",		SCSI_CAP_WIDE_XFER		},
		{ "wide-xfer",		SCSI_CAP_WIDE_XFER		},
		{ "parity",		SCSI_CAP_PARITY			},
		{ "initiator-id",	SCSI_CAP_INITIATOR_ID		},
		{ "untagged-qing",	SCSI_CAP_UNTAGGED_QING		},
		{ "tagged-qing",	SCSI_CAP_TAGGED_QING		},
		{ "auto-rqsense",	SCSI_CAP_ARQ			},
		{ "linked-cmds",	SCSI_CAP_LINKED_CMDS		},
		{ "sector-size",	SCSI_CAP_SECTOR_SIZE		},
		{ "total-sectors",	SCSI_CAP_TOTAL_SECTORS		},
		{ "geometry",		SCSI_CAP_GEOMETRY		},
		{ "reset-notification",	SCSI_CAP_RESET_NOTIFICATION	},
		{ "qfull-retries",	SCSI_CAP_QFULL_RETRIES		},
		{ "qfull-retry-interval", SCSI_CAP_QFULL_RETRY_INTERVAL	},
		{ NULL,			0			}
	};
	struct cap_strings	*cp;

	for (cp = cap_strings; cp->cap_string != NULL; cp++) {
		if (strcmp(cp->cap_string, capstr) == 0) {
			return (cp->cap_index);
		}
	}

	return (-1);
}


/*
 * Called by an HBA to determine if the system is in 'panic' state.
 */
int
scsi_hba_in_panic()
{
	return (panicstr != NULL);
}



/*
 * If a SCSI target driver attempts to mmap memory,
 * the buck stops here.
 */
/*ARGSUSED*/
static int
scsi_hba_map_fault(
	dev_info_t		*dip,
	dev_info_t		*rdip,
	struct hat		*hat,
	struct seg		*seg,
	caddr_t			addr,
	struct devpage		*dp,
	u_int			pfn,
	u_int			prot,
	u_int			lock)
{
	return (DDI_FAILURE);
}
