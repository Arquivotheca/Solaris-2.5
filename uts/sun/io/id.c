/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)id.c	1.41 95/02/20 SMI"

/*
 * IPI disk driver.
 *
 * Handles the following string controllers and disks:
 *	Sun Panther	VME-based IPI-2 String Controller
 */

#include <sys/types.h>
#include <sys/devops.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/cmn_err.h>
#include <sys/stropts.h>
#include <sys/kmem.h>

#include <sys/errno.h>
#include <sys/open.h>
#include <sys/varargs.h>
#include <sys/debug.h>
#include <sys/autoconf.h>
#include <sys/conf.h>
#include <sys/stat.h>

#include <sys/dkio.h>
#include <sys/hdio.h>
#include <sys/dkbad.h>
#include <sys/vtoc.h>

#include <sys/map.h>
#include <sys/vmmac.h>
#include <sys/file.h>

#include <sys/syslog.h>

#define	IDDEBUGSVR4

#include <sys/ipi_driver.h>
#include <sys/ipi3.h>
#include <sys/idvar.h>
#include <sys/ipi_error.h>

#include <sys/modctl.h>
#include <sys/kstat.h>
#include <sys/var.h>
#include <sys/mhd.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

/*
 *		Table Of Contents
 *
 * Section 1.0:	Function Prototype Definitions
 * Section 2.0:	Autoconfiguration Data
 * Section 2.1:	Local Static Data
 * Section 3.0:	Loadable Driver Routines and Data
 * Section 4.0:	Autoconfiguration Entry Points
 * Section 4.1:	Autoconfiguration Second Level Support Routines
 * Section 5.0:	Unix Entry Points
 * Section 6.0: Ioctl Support Functions
 * Section 6.1: Format Support Functions
 * Section 7.0:	Miscellaneous Support Functions
 * Section 8.0:	I/O Start Routines
 * Section 9.0: I/O Completion Routines
 * Section 10.0:	Slave Error Recovery Definitions
 * Section 10.1:	Slave Error Recovery Data
 * Section 10.2:	Slave Error Recovery Routines
 * Section 11.0:	Error Handler Routines
 * Section 12.0:	IPI Attribute Routines (Autoconfiguration 3rd Level)
 */

/*
 * Section 1.0:	Function prototype Definintions
 */
static int id_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int id_identify(dev_info_t *);
static int id_probe(dev_info_t *);
static int id_attach(dev_info_t *, ddi_attach_cmd_t);

static void id_init_slave(id_ctlr_t *);

static void id_init_unit(id_unit_t *);
static int id_stat_unit(id_unit_t *);
static int id_get_attributes(id_unit_t *);
static void id_read_label(id_unit_t *);

static int idopen(dev_t *, int, int, cred_t *);
static int idclose(dev_t, int, int, cred_t *);
static int idstrategy(struct buf *);
static int idprint(dev_t, char *);
static int iddump(dev_t, caddr_t, daddr_t, int);
static int idread(dev_t, struct uio *, cred_t *);
static int idwrite(dev_t, struct uio *, cred_t *);
static int idaread(dev_t, struct aio_req *, cred_t *);
static int idawrite(dev_t, struct aio_req *, cred_t *);
static int idioctl(dev_t, int, int, int, cred_t *, int *);
static int
id_prop_op(dev_t, dev_info_t *, ddi_prop_op_t, int, char *, caddr_t, int *);

static int id_ioctl_cmd(dev_t, struct hdk_cmd *, int);
static void id_build_user_vtoc(id_unit_t *, struct vtoc *);
static int id_write_label_from_vtoc(id_unit_t *, struct vtoc *, dev_t);

static int id_format(id_unit_t *, struct hdk_cmd *);
static int id_realloc(id_unit_t *, struct hdk_cmd *);
static int id_rdwr_deflist(dev_t, struct hdk_cmd *, caddr_t ua, int, int *);
static int id_rdwr(dev_t, struct hdk_cmd *, caddr_t ua, int, int *);

static void id_update_map(id_unit_t *);
static int id_islabel(id_unit_t *, struct dk_label *);
static int id_lcksum(struct dk_label *);
static void id_uselabel(id_unit_t *, struct dk_label *);
static void id_minphys(struct buf *);
static int id_getsbuf(id_unit_t *, int, caddr_t, int, int);
static void id_build_rdwr(ipiq_t *, daddr_t, int, int);
static void idqretry(id_ctlr_t *, ipiq_t *);

static int idrestart(caddr_t);
static int idstart(id_ctlr_t *);

static void idintr(ipiq_t *);
static void idasync(ipiq_t *);

static void id_req_missing(id_ctlr_t *, ipiq_t *);
static void id_req_reset(id_ctlr_t *, ipiq_t *);
static void id_recover_cbt(caddr_t);
static void id_recover(id_ctlr_t *);
static void id_recover_intr(id_ctlr_t *);

static int id_error(ipiq_t *, int, int, char *, u_char *);
static int id_respx(ipiq_t *, int, struct respx_parm *, int, struct buf *);
static int
id_deflist_parmlen(ipiq_t *, int, struct parmlen_parm *, int, struct buf *);
static int id_error_parse(ipiq_t *, ipi_errtab_t *, int);
static void id_printerr(ipiq_t *, int, int, char *);
static void id_whoinerr(ipiq_t *, int, char *);

static void id_build_attr_cmd(ipiq_t *, rtable_t *);
static void id_build_set_attr_cmd(ipiq_t *, rtable_t *, void *);

static int id_attr_vendor(ipiq_t *, int, u_char *, int, void *);
static int id_attr_port(ipiq_t *, int, u_char *, int, void *);
static int id_ctlr_conf(ipiq_t *, int, u_char *, int, void *);
static int id_ctlr_reconf(ipiq_t *, int, u_char *, int, void *);
static int id_set_ctlr_reconf(ipiq_t *, int, u_char *, int, void *);
static int id_ctlr_fat_attr(ipiq_t *, int, u_char *, int, void *);

static int id_attr_physdk(ipiq_t *, int, u_char *, int, void *);
static int id_phys_bsize(ipiq_t *, int, u_char *, int, void *);
static int id_log_bsize(ipiq_t *, int, u_char *, int, void *);
static int id_attr_nblks(ipiq_t *, int, u_char *, int, void *);

static int id_reserve(dev_t, u_char);
static int id_release(dev_t);
static int id_ping_drive(dev_t);
static int id_facility_reset(dev_t);
static int idha_failfast_request(dev_t, int);
static void idha_watch_init();
static void idha_watch_fini();
static void idha_watch_thread();
static int idha_watch_submit(dev_t, int, int (*)(), caddr_t);
static int idha_debug = 0;
static int idha_failfast_enable = 1;

/*
 * Section 2.0:	Autoconfiguration Data
 */

static struct cb_ops id_cb_ops = {
	idopen,			/* open */
	idclose,		/* close */
	idstrategy,		/* strategy */
	idprint,		/* print */
	iddump,			/* dump */
	idread,			/* read */
	idwrite,		/* write */
	idioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	id_prop_op,		/* cb_prop_op */
	0,			/* streamtab */
	D_NEW|D_MP,		/* Driver compatibility flag */
	CB_REV,			/* cb_rev */
	idaread, 		/* async I/O read entry point */
	idawrite		/* async I/O write entry point */
};

static struct dev_ops id_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt */
	id_info,		/* get_dev_info */
	id_identify,		/* identify */
	id_probe,		/* probe */
	id_attach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	&id_cb_ops,		/* driver operations */
	0			/* bus operations */
};

static struct driver_minor_data {
	char	*name;
	int	minor;
	int	type;
} id_minor_data[] = {
	{ "a", 0, S_IFBLK },
	{ "b", 1, S_IFBLK },
	{ "c", 2, S_IFBLK },
	{ "d", 3, S_IFBLK },
	{ "e", 4, S_IFBLK },
	{ "f", 5, S_IFBLK },
	{ "g", 6, S_IFBLK },
	{ "h", 7, S_IFBLK },
	{ "a,raw", 0, S_IFCHR },
	{ "b,raw", 1, S_IFCHR },
	{ "c,raw", 2, S_IFCHR },
	{ "d,raw", 3, S_IFCHR },
	{ "e,raw", 4, S_IFCHR },
	{ "f,raw", 5, S_IFCHR },
	{ "g,raw", 6, S_IFCHR },
	{ "h,raw", 7, S_IFCHR },
	{ 0 }
};

/*
 * Section 2.1:	Local Static Data
 */
static id_ctlr_t *id_ctlr[IPI_NSLAVE];
static int id_dumb_limit = 16;
static int id_timeout = ID_TIMEOUT;
static int iddebug = 0;
static int id_hz;

static rtable_t vendor_id_table[] = {
	/* parm-id	minimum-len			function */
	ATTR_VENDOR,	28,				id_attr_vendor,
	0,		0,				NULL,
};

static rtable_t get_conf_table[] = {
	/* parm-id	minimum-len			function */
	ATTR_ADDR_CONF,	sizeof (struct addr_conf_parm),	id_ctlr_conf,
	ATTR_SLVCNF_BIT, sizeof (struct reconf_bs_parm), id_ctlr_reconf,
	ATTR_FAC_ATTACH, 0,				id_ctlr_fat_attr,
	0,		0,				NULL
};

static rtable_t set_conf_table[] = {
	/* parm-id	minimum-len			function */
	ATTR_SLVCNF_BIT, sizeof (struct reconf_bs_parm), id_set_ctlr_reconf,
	0,		0,				NULL
};

static rtable_t attach_resp_table[] = {
	/* parm-id	minimum-len			function */
	ATTR_PHYSDK,	sizeof (struct physdk_parm),	id_attr_physdk,
	ATTR_PHYS_BSIZE, sizeof (struct physbsize_parm), id_phys_bsize,
	ATTR_LOG_BSIZE,	sizeof (struct datbsize_parm),	id_log_bsize,
	ATTR_LOG_GEOM,	sizeof (struct numdatblks_parm), id_attr_nblks,
	0,		0,		NULL
};

static rtable_t id_deflist_table[] = {
	/* parm-id	minimum-len			function */
	RESP_EXTENT,	sizeof (struct respx_parm),	id_respx,
	ATTR_PARMLEN,	sizeof (struct parmlen_parm),	id_deflist_parmlen,
	0,		0,		NULL
};

/*
 * Macros to initialize table.  Used by IPI_ERR_TABLE_INIT
 */
#define	IPI_ERR_PARM(sid, fid, flags, msg) \
	{ 0,		(sid),		(flags), 	(msg) },
#define	IPI_ERR_BIT(byte, bit, flags, msg) \
	{ (byte),	1<<(bit),	(flags),	(msg) },

static ipi_errtab_t id_errtab[] = {
	IPI_ERR_TABLE_INIT()
	{ 0, 		0, 		0,		NULL }
};

static ipi_errtab_t id_condtab[] = {
	IPI_COND_TABLE_INIT()
	{ 0, 		0, 		0,		NULL }
};

/*
 * Section 3.0:	Loadable Driver Routines and Data
 */

/*
 * Module linkage information for the kernel.
 */
static struct modldrv modldrv = {
	&mod_driverops, "IPI Disk Controller", &id_ops
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

/*
 * This is the module initialization routine.
 */

int
_init(void)
{
	int ret;

	ret = mod_install(&modlinkage);
	if (ret != 0)
		return (ret);
	idha_watch_init();
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	int ret;

	ret = mod_remove(&modlinkage);
	if (ret != 0)
		return (ret);
	idha_watch_fini();
	return (0);
}

/*
 * Section 4.0:	Autoconfiguration Entry Points
 */

/*
 * Given the device number, return the devinfo pointer
 * or the instance number.  Note: this routine must be
 * successful on DDI_INFO_DEVT2INSTANCE even before attach.
 */
/* ARGSUSED */
static int
id_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	id_ctlr_t *c;
	id_unit_t *un;
	register dev_t dev = (dev_t) arg;
	register u_int instance;

	instance = ID_INST(dev);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if ((instance / ID_NUNIT) > IPI_NSLAVE) {
			return (DDI_FAILURE);
		}
		c = id_ctlr[instance / ID_NUNIT];
		if (c == (id_ctlr_t *) NULL)
			return (DDI_FAILURE);
		if ((un = c->c_un[instance % ID_NUNIT]) == (id_unit_t *) NULL)
			return (DDI_FAILURE);
		*result = (void *) un->un_dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)instance;
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

static int
id_identify(dev_info_t *dip)
{
	if (strcmp(ddi_get_name(dip), "id") == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

/*
 * Determine existence of a facility.
 *
 * Note that the existence and readiness of the slave is
 * established here also. In order to maintain 'statelessness'
 * of the probe function, we deallocate the slave structure
 * if we entered, allocated, but didn't find the facility
 * we were probing for.
 *
 */

static int
id_probe(dev_info_t *dip)
{
	id_ctlr_t *c;
	id_unit_t *un;
	ipi_config_t *icp;
	int slv, fac, did_alloc_slave;

	icp = (ipi_config_t *) ddi_get_driver_private(dip);
	if (icp == (ipi_config_t *) NULL) {
		return (DDI_PROBE_FAILURE);
	}

	slv = IPI_SLAVE(icp->ic_addr);
	fac = IPI_FAC(icp->ic_addr);

	if (fac == IPI_NO_ADDR || fac >= ID_NUNIT || slv >= IPI_NSLAVE)
		return (DDI_PROBE_FAILURE);

	c = id_ctlr[slv];

	/*
	 * If the slave hasn't been seen yet, test for its existence
	 */

	if (c == NULL) {
		did_alloc_slave = 1;
		id_hz = drv_usectohz(1000000);

		c = (id_ctlr_t *) kmem_zalloc(sizeof (*c), KM_SLEEP);
		mutex_init(&c->c_lock, "id", MUTEX_DRIVER, icp->ic_lkinfo);
		cv_init(&c->c_cv, "c_cv", CV_DRIVER, icp->ic_lkinfo);

		/*
		 * Pick up configuration IPI address vector.
		 *
		 * Note that the adresss contained is the facility's address,
		 * not the address of the slave.
		 */
		c->c_icp = *icp;

		/*
		 * This call registers the facility interrupt handler.
		 * Note that we do this prior to turning the configuration
		 * address into a facility address.
		 */
		IDC_CONTROL(c, IPI_CTRL_REGISTER_IFUNC, idintr, 0);

		/*
		 * Turn facility address into a slave address.
		 */
		c->c_icp.ic_addr |= IPI_NO_ADDR;

		/*
		 * This call registers the slave interrupt handler.
		 */
		IDC_CONTROL(c, IPI_CTRL_REGISTER_IFUNC, idintr, 0);

		c->c_dip = ddi_get_parent(dip);
		c->c_name = ddi_get_name(c->c_dip);
		c->c_instance = ddi_get_instance(c->c_dip);
		c->c_crashbuf = getrbuf(KM_SLEEP);
		c->c_flags = IE_INIT_STAT|IE_RECOVER;

		if (IDC_ALLOC(c, 0, DDI_DMA_SLEEP, 0, &c->c_rqp) == 0) {
			id_ctlr[slv] = c;
			id_init_slave(c);
		}

		if (!IE_STAT_PRESENT(c->c_flags)) {
c_free:
			id_ctlr[slv] = (id_ctlr_t *) 0;
			if (c->c_rqp)
				IDC_RELSE(c, c->c_rqp);
			IDC_CONTROL(c, IPI_CTRL_REGISTER_IFUNC, 0, 0);
			c->c_icp.ic_addr = icp->ic_addr;
			IDC_CONTROL(c, IPI_CTRL_REGISTER_IFUNC, 0, 0);
			mutex_destroy(&c->c_lock);
			cv_destroy(&c->c_cv);
			freerbuf(c->c_crashbuf);
			kmem_free((caddr_t) c, sizeof (*c));
			return (DDI_PROBE_FAILURE);
		}
	} else {
		did_alloc_slave = 0;
	}

	/*
	 * If we've made it this far, the slave exists.
	 * Now see if the facility is attached. We should
	 * just cut to the chase and check c->c_fac_flags.
	 */

	un = c->c_un[fac];
	if (un == NULL) {
		un = (id_unit_t *) kmem_zalloc(sizeof (*un), KM_SLEEP);
		un->un_cfg = *icp;
		un->un_sbufp = getrbuf(KM_SLEEP);
		mutex_init(&un->un_qlock, "idl", MUTEX_DRIVER, icp->ic_lkinfo);
		sema_init(&un->un_sbs, 1, "ids", SEMA_DRIVER, icp->ic_lkinfo);
		sema_init(&un->un_ocsema, 1, "ido",
		    SEMA_DRIVER, icp->ic_lkinfo);
		un->un_dip = dip;
		un->un_flags = IE_INIT_STAT;
		c->c_un[fac] = un;

	}

	id_init_unit(un);

	if (!IE_STAT_PRESENT(un->un_flags)) {
		c->c_un[fac] = (id_unit_t *) 0;
		mutex_destroy(&un->un_qlock);
		sema_destroy(&un->un_sbs);
		sema_destroy(&un->un_ocsema);
		freerbuf(un->un_sbufp);
		kmem_free((caddr_t) un, sizeof (*un));
		if (did_alloc_slave)
			goto c_free;
		return (DDI_PROBE_FAILURE);
	}

	/*
	 * If we make it here, the facility exists. It may not
	 * be ready for use (i.e., it may not be labelled or
	 * formatted, but it at least reports itself present).
	 */
	return (DDI_PROBE_SUCCESS);
}

/*ARGSUSED*/
static int
id_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	struct driver_minor_data *dmdp;
	id_ctlr_t *c;
	id_unit_t *un;
	int instance, uinst, cinst;

	instance = ddi_get_instance(dip);
	uinst = instance % ID_NUNIT;
	cinst = instance / ID_NUNIT;
	if ((cinst >= IPI_NSLAVE) ||
	    (c = id_ctlr[cinst]) == (id_ctlr_t *) NULL ||
	    (un = c->c_un[uinst]) == (id_unit_t *) NULL) {
		return (ENXIO);
	}

	for (dmdp = id_minor_data; dmdp->name != NULL; dmdp++) {
		if (ddi_create_minor_node(dip, dmdp->name, dmdp->type,
		    (instance << 3) | dmdp->minor,
		    DDI_NT_BLOCK_CHAN, NULL) == DDI_FAILURE) {
			ddi_remove_minor_node(dip, NULL);
			return (DDI_FAILURE);
		}
	}

	/*
	 * report the existance of this device.
	 */
	if (un->un_flags & ID_FORMATTED) {
		/*
		 * Read label.
		 */
		id_read_label(un);
	}

	/*
	 * Add a zero-length attribute to tell the world we support
	 * kernel ioctls (for layered drivers)
	 */
	(void) ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    DDI_KERNEL_IOCTL, NULL, 0);

	ddi_report_dev(dip);
	if (un->un_flags & ID_LABEL_VALID) {
		/*
		 * Print out the disk description.
		 */
		cmn_err(CE_CONT,
		    "?id%d:	<%s>\n", instance, un->un_asciilabel);
	}
	return (DDI_SUCCESS);
}

/*
 * Section 4.1:	Autoconfiguration Second Level Support Routines.
 */

/*
 * id_init_ctlr belongs here, but uses definitions only found in section
 * X.X (Slave Recovery Routines), so it is down at the head of that area.
 */

/*
 * id_init_unit
 *
 * Attempt to get the status of the named facility and get its attributes.
 */

static void
id_init_unit(id_unit_t *un)
{
	id_ctlr_t *c = id_ctlr[IPI_SLAVE(un->un_ipi_addr)];
	/*
	 * First, set some flags
	 */

	mutex_enter(&c->c_lock);
	un->un_flags = IE_INIT_STAT;
	mutex_exit(&c->c_lock);

	/*
	 * Second, stat the unit
	 */

	if (id_stat_unit(un) != DDI_SUCCESS) {
		return;
	}

	/*
	 * Third, collect all attributes from the unit
	 */

	if (id_get_attributes(un) != DDI_SUCCESS) {
		return;
	}
}

static int
id_stat_unit(id_unit_t *un)
{
	int err;
	id_ctlr_t *c;
	struct buf *bp;
	ipiq_t *q;

	c = id_ctlr[IPI_SLAVE(un->un_ipi_addr)];

	if (IDU_ALLOC(un, 0, DDI_DMA_SLEEP, 0, &q) != 0) {
		return (DDI_FAILURE);
	}

	(void) id_getsbuf(un, 0, 0, B_READ, 0);
	bp = un->un_sbufp;
	bp->b_forw = (struct buf *) q;

	/*
	 * Send REPORT STATUS command to the facility.
	 */
	q->q_cmd->hdr_opcode = IP_REPORT_STAT;
	q->q_cmd->hdr_mods = IP_OM_CONDITION;
	q->q_cmd->hdr_pktlen = IPI_HDRLEN;
	q->q_time = ID_REC_TIMEOUT;
	q->q_flag = IP_NO_RETRY|IP_SILENT;
	q->q_private[Q_BUF] = (u_long)bp;

	idstrategy(bp);
	if (biowait(bp) != 0) {
		q->q_result = IP_ERROR;
		goto out;
	}

	mutex_enter(&c->c_lock);
	if (q->q_result == IP_SUCCESS) {
		(void) id_error_parse(q, id_condtab, IE_FAC);
	}

	/*
	 * Check flags. If the facility isn't ready, consider the
	 * status operation a failure.
	 *
	 * Ignore IE_RECOVER and IE_RE_INIT flags for this test.
	 */
	if (!IE_STAT_READY(un->un_flags & ~(IE_RECOVER | IE_RE_INIT))) {
		q->q_result = IP_ERROR;
	}
	un->un_flags &= ~IE_INIT_STAT;
	mutex_exit(&c->c_lock);

	if (q->q_result != IP_SUCCESS) {
		q->q_result = IP_ERROR;
	}
out:
	if (q->q_result == IP_ERROR) {
		err = DDI_FAILURE;
	} else {
		err = DDI_SUCCESS;
	}
	IDU_RELSE(un, q);
	sema_v(&un->un_sbs);
	return (err);
}

static int
id_get_attributes(id_unit_t *un)
{
	struct buf *bp;
	id_ctlr_t *c;
	rtable_t *rt;
	ipiq_t *q;
	int err = DDI_SUCCESS;

	c = id_ctlr[IPI_SLAVE(un->un_ipi_addr)];
	if (IDU_ALLOC(un, 0, DDI_DMA_SLEEP, 0, &q) != 0) {
		return (DDI_FAILURE);
	}

	(void) id_getsbuf(un, 0, 0, B_READ, 0);
	bp = un->un_sbufp;
	bp->b_forw = (struct buf *) q;

	for (rt = attach_resp_table; rt->rt_parm_id != 0; rt++) {
		struct icprarg r;
		id_build_attr_cmd(q, rt);
		q->q_time = ID_REC_TIMEOUT;
		q->q_flag = IP_NO_RETRY;
		q->q_private[Q_BUF] = (u_long)bp;
		bp->b_flags &= ~B_ERROR;
		idstrategy(bp);
		err = biowait(bp);
		if (err || q->q_result != IP_SUCCESS) {
			err = DDI_FAILURE;
			break;
		}
		r.q = q;
		r.rt = rt;
		r.a = (caddr_t) un;
		mutex_enter(&c->c_lock);
		IDC_CONTROL(c, IPI_CTRL_PARSERESP, &r, 0);
		mutex_exit(&c->c_lock);
	}
	IDU_RELSE(un, q);
	sema_v(&un->un_sbs);
	return (err);
}

static void
id_read_label(id_unit_t *un)
{
	ipiq_t *q;
	struct buf *bp;
	id_ctlr_t *c;
	caddr_t addr;
	int errno;

	c = id_ctlr[IPI_SLAVE(un->un_ipi_addr)];
	addr = NULL;
	q = NULL;
	bp = NULL;

	addr = kmem_alloc(un->un_log_bsize, KM_SLEEP);

	(void) id_getsbuf(un, un->un_log_bsize, addr, B_READ|B_KERNBUF, 0);
	bp = un->un_sbufp;

	if (IDU_ALLOC(un, bp, DDI_DMA_SLEEP, 0, &q) != 0) {
		goto out;
	}
	bp->b_forw = (struct buf *) q;
	id_build_rdwr(q, (daddr_t) 0, 1, IP_READ);

	q->q_time = id_timeout;
	q->q_flag = IP_SILENT;
	q->q_retry = ID_NRETRY;
	q->q_private[Q_BUF] = (u_long)bp;

	idstrategy(bp);
	errno = biowait(bp);

	if (errno || q->q_result != IP_SUCCESS) {
		goto out;
	}

	/*
	 * Have to release the q here
	 */
	IDU_RELSE(un, q);
	q = NULL;
	mutex_enter(&c->c_lock);
	if (id_islabel(un, (struct dk_label *) addr)) {
		id_uselabel(un, (struct dk_label *) addr);
	}
	mutex_exit(&c->c_lock);

out:
	if (addr) {
		kmem_free(addr, un->un_log_bsize);
	}
	if (q) {
		IDU_RELSE(un, q);
	}
	if (bp) {
		sema_v(&un->un_sbs);
	}
}



/*
 * Section 5.0:	Unix Entry Points
 */

/* ARGSUSED3 */
static int
idopen(dev_t *dev_p, int flag, int otyp, cred_t *cred_p)
{
	dev_t dev;
	int cinst, uinst, part;
	id_unit_t *un;
	id_ctlr_t *c;

	dev = *dev_p;
	cinst = ID_CINST(dev);
	uinst = ID_UINST(dev);
	if ((cinst >= IPI_NSLAVE) ||
	    (c = id_ctlr[cinst]) == (id_ctlr_t *) NULL ||
	    (un = c->c_un[uinst]) == (id_unit_t *) NULL) {
		return (ENXIO);
	}

	/*
	 * Serialize opens and closes
	 */

	if (otyp >= OTYPCNT) {
		return (EINVAL);
	}
	part = ID_LPART(dev);

	sema_p(&un->un_ocsema);

	mutex_enter(&c->c_lock);
	if (!IE_STAT_PRESENT(un->un_flags) || !IE_STAT_PRESENT(c->c_flags)) {
		mutex_exit(&c->c_lock);
		sema_v(&un->un_ocsema);
		return (ENXIO);
	}

	/*
	 * If the disk label is not read yet or not valid, try reading it.
	 * The drive may have become ready now.
	 * This is also possible if the disk was earlier reserved by alternate
	 * port (read cmd would have failed with alternate port exception).
	 */
	if (!(un->un_flags & ID_LABEL_VALID)) {
		un->un_flags = IE_INIT_STAT;
		mutex_exit(&c->c_lock);
		id_init_unit(un);
		if (!IE_STAT_PRESENT(un->un_flags)) {
			if (otyp != OTYP_CHR ||
				(flag & (FNDELAY | FNONBLOCK)) == 0) {
				sema_v(&un->un_ocsema);
				return (ENXIO);
			}
		} else if (un->un_flags & ID_FORMATTED) {
			id_read_label(un);
		}
		mutex_enter(&c->c_lock);
	}

	if (un->un_lpart[part].un_map.dkl_nblk == 0) {
		if (otyp != OTYP_CHR || (flag & (FNDELAY | FNONBLOCK)) == 0) {
			mutex_exit(&c->c_lock);
			sema_v(&un->un_ocsema);
			return (ENXIO);
		}
	}
	mutex_exit(&c->c_lock);

	if (un->un_stats == (struct kstat *) 0) {
		struct kstat *ksp = kstat_create("id",
		    ddi_get_instance(un->un_dip), NULL, "disk",
		    KSTAT_TYPE_IO, 1, KSTAT_FLAG_PERSISTENT);
		if (ksp) {
			mutex_init(&un->un_slock, "idS",
			    MUTEX_DRIVER, (void *) -1);
			ksp->ks_lock = &un->un_slock;
			un->un_stats = ksp;
			kstat_install(ksp);
		}
	}

	if (otyp == OTYP_LYR) {
		un->un_ocmap.lyropen[part]++;
	} else {
		un->un_ocmap.regopen[otyp] |= 1 << part;
	}
	sema_v(&un->un_ocsema);

	return (0);
}

/*
 * close routine
 */

/* ARGSUSED2 */
static int
idclose(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	u_char *cp;
	int cinst, uinst, part;
	id_ctlr_t *c;
	id_unit_t *un;

	cinst = ID_CINST(dev);
	uinst = ID_UINST(dev);
	if ((cinst >= IPI_NSLAVE) ||
	    (c = id_ctlr[cinst]) == (id_ctlr_t *) NULL ||
	    (un = c->c_un[uinst]) == (id_unit_t *) NULL) {
		return (ENXIO);
	}
	if (otyp >= OTYPCNT)
		return (ENXIO);
	part = ID_LPART(dev);

	sema_p(&un->un_ocsema);
	if (otyp == OTYP_LYR) {
		un->un_ocmap.lyropen[part] -= 1;
	} else {
		un->un_ocmap.regopen[otyp] &= ~(1<<part);
	}
	cp = &un->un_ocmap.chkd[0];
	while (cp < &un->un_ocmap.chkd[OCSIZE]) {
		if (*cp != (u_char) 0) {
			break;
		}
		cp++;
	}
	if (cp == &un->un_ocmap.chkd[OCSIZE]) {
		struct kstat *ksp = un->un_stats;
		if (ksp != (struct kstat *) 0) {
			un->un_stats = (struct kstat *) 0;
			kstat_delete(ksp);
			mutex_destroy(&un->un_slock);
		}
	}
	sema_v(&un->un_ocsema);
	return (0);
}


/*
 * Queue a request and and call start routine.
 *
 * If the request is not a special buffer request,
 * do validation on it and generate both an absolute
 * block number (which we will leave in b_resid),
 * and a actual block count value (which we will
 * leave in av_back).
 */

static int
idstrategy(struct buf *bp)
{
	register id_unit_t *un;
	register id_ctlr_t *c;
	u_int cinst, uinst;

	bp->b_flags &= ~(B_DONE|B_ERROR);
	bp->av_forw = NULL;
	bp->b_resid = bp->b_bcount;

	cinst = ID_CINST(bp->b_edev);
	uinst = ID_UINST(bp->b_edev);
	if ((cinst >= IPI_NSLAVE) || (c = id_ctlr[cinst]) == NULL ||
	    (un = c->c_un[uinst]) == NULL) {
		bp->b_flags |= B_ERROR;
		bp->b_error = ENXIO;
		biodone(bp);
		return (0);
	}

	if (bp != un->un_sbufp) {
		struct un_lpart *lp;
		daddr_t blkno;
		int nblks, err;

		/*
		 * A normal read/write command.
		 *
		 * If the transfer size would take it past the end of the
		 * partition, trim it down. Also trim it down to a multiple
		 * of the block size.
		 *
		 * The argument here to be made for not locking while
		 * looking at partition information is that this information
		 * is utterly stable for very long periods of time, and
		 * that if it changes while a device has I/O occurring
		 * to or from it that the filesystem will be mangled
		 * anyway (in other words, don't do that, and punt the
		 * issue back up to the administrative level (e.g.,
		 * format) to handle).
		 *
		 * The reason that it is important to not lock is that
		 * that lock collisions are expensive, so lock as little
		 * as possible.
		 */

		blkno = dkblock(bp);
		lp = &un->un_lpart[ID_LPART(bp->b_edev)];
		err = 0;

		if (lp->un_map.dkl_nblk == 0 || blkno > lp->un_map.dkl_nblk) {
			err = EINVAL;
		} else if (blkno == lp->un_map.dkl_nblk) {
			if ((bp->b_flags & B_READ) == 0) {
				err = EINVAL;
			} else {
				err = -1;
			}
		} else if (un->un_log_bsize == 0) {
			err = -1;
		} else if (bp->b_bcount < un->un_log_bsize) {
			err = EINVAL;
		}

		if (err != 0) {
			if (err > 0) {
				bp->b_flags |= B_ERROR;
				bp->b_error = err;
			}
			bp->b_resid = bp->b_bcount;
			biodone(bp);
			return (0);
		}

		nblks = NBLKS(bp->b_bcount, un);
		if (blkno + nblks > lp->un_map.dkl_nblk) {
			nblks = lp->un_map.dkl_nblk - blkno;
		}

		/*
		 * Map block number within partition to absolute
		 * block number.
		 */
		blkno += lp->un_blkno;

		/*
		 * Now stash the absolute block number into b_resid
		 * and the number of blocks into av_back.
		 */
		bp->b_resid = (int) blkno;
		bp->av_back = (struct buf *) nblks;
	} else {
		bp->b_resid = 0;
	}

	/*
	 * If it is a special buffer, put it at the tail of the queue,
	 * otherwise 'sort' it into the disk queue.
	 */

	mutex_enter(&un->un_qlock);
	if (bp == un->un_sbufp) {
		if (un->un_bufs.av_forw) {
			un->un_bufs.av_back->av_forw = bp;
		} else {
			un->un_bufs.av_forw = bp;
		}
		un->un_bufs.av_back = bp;
	} else {
		/*
		 * Mark entry to wait queue.
		 */
		if (un->un_stats) {
			mutex_enter(&un->un_slock);
			kstat_waitq_enter(IOSP);
			mutex_exit(&un->un_slock);
		}
		disksort(&un->un_bufs, bp);
	}
	mutex_exit(&un->un_qlock);
	(void) idstart(c);
	return (0);
}

/* ddi print */
static int
idprint(dev_t dev, char *str)
{

	cmn_err(CE_CONT, "?id%d: %s\n", ID_INST(dev), str);
	return (0);
}

static int
iddump(dev_t dev, caddr_t addr, daddr_t blkno, int nblk)
{
	id_ctlr_t *c = id_ctlr[ID_CINST(dev)];
	int err;
	struct buf *bp;

	if (!c)
		return (ENXIO);
	bp = c->c_crashbuf;
	bp->b_un.b_addr = addr;
	bp->b_edev = dev;
	bp->b_dev = cmpdev(dev);
	bp->b_bcount = nblk * DEV_BSIZE;
	bp->b_flags = B_WRITE|B_KERNBUF;
	bp->b_blkno = blkno;
	idstrategy(bp);
	err = biowait(bp);
	return (err);
}

static int
idread(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	if (uio->uio_offset % DEV_BSIZE != 0)
		return (EINVAL);
	return (physio(idstrategy, NULL, dev, B_READ, id_minphys, uio));
}

static int idwrite(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	if (uio->uio_offset % DEV_BSIZE != 0)
		return (EINVAL);
	return (physio(idstrategy, NULL, dev, B_WRITE, id_minphys, uio));
}

static int
idaread(dev_t dev, struct aio_req *aio, cred_t *cred_p)
{
	if (aio->aio_uio->uio_offset % DEV_BSIZE != 0)
		return (EINVAL);
	return (aphysio(idstrategy, anocancel, dev, B_READ, id_minphys, aio));
}

static int idawrite(dev_t dev, struct aio_req *aio, cred_t *cred_p)
{
	if (aio->aio_uio->uio_offset % DEV_BSIZE != 0)
		return (EINVAL);
	return (aphysio(idstrategy, anocancel, dev, B_WRITE, id_minphys, aio));
}

/*
 * This routine implements the ioctl calls for the disk.
 * It is called from the device switch at normal priority.
 */
/* ARGSUSED */
static int
idioctl(dev_t dev, int cmd, int arg, int flag, cred_t *cred_p, int *rval_p)
{
	union {
		struct vtoc _Vtoc;
		struct dk_map _Dkmap[NDKMAP];
		struct dk_cinfo	_cinf;
		struct hdk_type	_typ;
		struct hdk_cmd	_com;
		struct hdk_diag	_diag;
		struct dk_geom	_geom;
	} _ud;
#define	Vtoc	_ud._Vtoc
#define	Dkmap	_ud._Dkmap

	union id_ptr {
		struct dk_cinfo	*cinf;
		struct hdk_type	*typ;
		struct hdk_cmd	*com;
		struct hdk_diag	*diag;
		struct dk_geom	*geom;
		struct dk_map	*map;
		caddr_t	data;
	} p;
	id_unit_t *un;
	id_ctlr_t *c;
	int i, err, nbi, nbo;

	if (ID_CINST(dev) >= IPI_NSLAVE ||
	    (c = id_ctlr[ID_CINST(dev)]) == NULL ||
	    (un = c->c_un[ID_UINST(dev)]) == NULL) {
		return (ENXIO);
	}

	/*
	 * Set generic pointer to local data.
	 * Note that in a couple of instances
	 * this will end up pointing somewhere
	 * else entirely at copyout time.
	 */
	p.data = (caddr_t) &_ud;
	nbi = nbo = err = 0;

	/*
	 * First, set sizes of what we need to copy in or out.
	 */

	switch (cmd) {
	case DKIOCINFO:
		nbo = sizeof (struct dk_cinfo);
		break;
	case HDKIOCGTYPE:
		nbo = sizeof (struct hdk_type);
		break;
	case DKIOCGGEOM:
		nbo = sizeof (struct dk_geom);
		break;
	case DKIOCSGEOM:
		nbi = sizeof (struct dk_geom);
		break;
	case DKIOCGVTOC:
		nbo = sizeof (struct vtoc);
		break;
	case DKIOCSVTOC:
		nbi = sizeof (struct vtoc);
		break;
	case DKIOCGAPART:
		nbo = sizeof (Dkmap);
		break;
	case DKIOCSAPART:
		nbi = sizeof (Dkmap);
		break;
	case HDKIOCSCMD:
		nbi = sizeof (struct hdk_cmd);
		break;
	case HDKIOCGDIAG:
		nbo = sizeof (struct hdk_diag);
		break;

	default:
		break;
	}

	if (nbi || nbo) {
		bzero(p.data, sizeof (_ud));
	}

	if (nbi != 0) {
		if (ddi_copyin((caddr_t) arg, p.data, nbi, flag))
			return (EFAULT);
	}

	/*
	 * Now parse the command
	 */

	switch (cmd) {
	case DKIOCINFO:
		/*
		 * Return info concerning the device (slave and facility)
		 */
		(void) strncpy(p.cinf->dki_cname, c->c_name, DK_DEVLEN);
		p.cinf->dki_ctype = c->c_ctype;
		p.cinf->dki_flags = DKI_FMTCYL;
		p.cinf->dki_cnum = c->c_instance;
		p.cinf->dki_addr = c->c_ipi_addr;
		(void) strncpy(p.cinf->dki_dname, "id", 3);
		p.cinf->dki_unit = ID_UINST(dev);
		p.cinf->dki_slave = un->un_ipi_addr;
		p.cinf->dki_partition = ID_LPART(dev);
		p.cinf->dki_maxtransfer = ID_MAXPHYS / DEV_BSIZE;
		break;

	case HDKIOCGTYPE:
		/*
		 * Return drive info.
		 * The only non-zero value we set is hdkt_hsect.
		 */
		p.typ->hdkt_hsect = un->un_g.dkg_nsect;
		break;

	case HDKIOCSTYPE:
		/*
		 * Set drive info -- only affects drive type.
		 * 	This doesn't make sense for IPI disks.  The disk
		 *	identifies itself.  Ignore without returning error.
		 */
		break;

	case DKIOCGGEOM:
		/*
		 * Return the geometry of the specified unit.
		 */
		mutex_enter(&c->c_lock);
		_ud._geom = un->un_g;
		mutex_exit(&c->c_lock);
		break;
	case DKIOCSGEOM:
		/*
		 * Set the geometry of the specified unit.
		 * Currently only the number of cylinders is expected to
		 * change from what was given in the label or attributes.
		 */

		mutex_enter(&c->c_lock);
		un->un_g = *p.geom;	/* set geometry */
		id_update_map(un);	/* fix partition table */
		mutex_exit(&c->c_lock);
		break;

	case DKIOCGVTOC:

		if (!(un->un_flags & ID_LABEL_VALID)) {
			err = EINVAL;
			break;
		}
		mutex_enter(&c->c_lock);
		id_build_user_vtoc(un, &Vtoc);
		mutex_exit(&c->c_lock);
		break;

	case DKIOCSVTOC:
		if (un->un_g.dkg_ncyl == 0) {
			err = EINVAL;
		} else {
			err = id_write_label_from_vtoc(un, &Vtoc, dev);
		}
		break;

	case DKIOCGAPART:
		/*
		 * Return the map for all logical partitions.
		 */
		mutex_enter(&c->c_lock);
		for (i = 0; i < NDKMAP; i++) {
			Dkmap[i] = un->un_lpart[i].un_map;
		}
		mutex_exit(&c->c_lock);
		break;

	case DKIOCSAPART:
		/*
		 * Set the map for all logical partitions.
		 */

		mutex_enter(&c->c_lock);
		for (i = 0; i < NDKMAP; i++) {
			un->un_lpart[i].un_map = Dkmap[i];
		}
		id_update_map(un);
		mutex_exit(&c->c_lock);
		break;

	case HDKIOCSCMD:
		/*
		 * Generic IPI command
		 *
		 * We'll clear the HDK_KBUF flag here, but this
		 * may get overridden (correctly) in case FKIOCTL
		 * is set in flag.
		 */
		p.com->hdkc_flags &= ~HDK_KBUF;
		err = id_ioctl_cmd(dev, p.com, flag);
		break;

	case HDKIOCGDIAG:
		/*
		 * Get diagnostics
		 */
		mutex_enter(&c->c_lock);
		_ud._diag = un->un_diag;
		bzero((caddr_t) &un->un_diag, sizeof (struct hdk_diag));
		mutex_exit(&c->c_lock);
		break;

	/*
	 * The following MHIO ioctls are needed by HADF folks.
	 */
	/*
	 * case MHIOCRESET:
	 *	if ((i = drv_priv(cred_p)) != EPERM) {
	 *		if ((i = id_facility_reset(dev)) != 0) {
	 *			return (EIO);
	 *		}
	 *
	 *		Reset the facility. Remove the RESERVE bit of status
	 *		flag because reset clears facility reservation.
	 *
	 *		un->un_idha_status &= ~IDHA_RESERVE;
	 *		if (idha_debug)
	 *			cmn_err(CE_CONT, "IDHA_RESET done\n");
	 *	}
	 *	return (i);
	 */

	case MHIOCTKOWN:
		if ((i = drv_priv(cred_p)) != EPERM) {
			/*
			 * Reserve (own) the facility, try a priority
			 * reserve in case the facility is already reserved
			 * by an alternate port.
			 */
			if ((i = id_reserve(dev, (u_char) IP_OM_PRESRV)) == 0) {
				un->un_idha_status |= IDHA_RESERVE;
				un->un_idha_status &= ~IDHA_LOST_RESERVE;
				if (idha_debug) {
					cmn_err(CE_CONT,
					"id%d: IDHA_RESERVE done\n",
					ID_INST(dev));
				}
			}
		}
		return (i);

	case MHIOCRELEASE:
		if ((i = drv_priv(cred_p)) != EPERM) {
			/*
			 * Release facility reservation.
			 * Do we check for IDHA_RESERVE beforehand ?
			 */
			if ((i = id_release(dev)) == 0) {
				un->un_idha_status &= ~IDHA_RESERVE;
				if (idha_debug) {
					cmn_err(CE_CONT,
					"id%d: IDHA_RELEASE done\n",
					ID_INST(dev));
				}
			}
		}
		return (i);

	case MHIOCSTATUS:
		if ((i = drv_priv(cred_p)) != EPERM) {
			i = id_ping_drive(dev);
			/*
			 * Return 0 if facility is accessible and ready.
			 * return 1 if facility reports busy because of
			 * alternate port reservation.
			 * Otherwise it is an error
			 */
			if (i != 0 && i != EACCES)
				return (i);
			else if (i == EACCES)
				*rval_p = 1;
			if (idha_debug) {
				cmn_err(CE_CONT, "id%d: IDHA_STATUS done %d\n",
					ID_INST(dev), (i == EACCES) ? 1 : i);
			}
			return (0);
		}
		return (i);

	case MHIOCENFAILFAST:
		if ((i = drv_priv(cred_p)) != EPERM) {
			int	mh_time;
			if (ddi_copyin((caddr_t) arg, (caddr_t) &mh_time,
			    sizeof (int), flag))
				return (EFAULT);
			/*
			 * Submit an failfast request, so that the facility
			 * can be checked periodically at "mh_time" interval.
			 * Watch thread and callback functions handle the
			 * probing of the facility for ready status and any
			 * lost reservations.
			 */
			if (mh_time > 0) {
				un->un_idha_status |= IDHA_FAILFAST;
				i = idha_failfast_request(dev, mh_time);
				if (idha_debug && i == 0) {
					cmn_err(CE_CONT,
					"id%d: IDHA_FAILFAST enabled\n",
					ID_INST(dev));
				}
			} else {
				i = idha_failfast_request(dev, 0);
				/*
				 * Note that 0 interval removes the request.
				 */
				un->un_idha_status &= ~IDHA_FAILFAST;
				if (idha_debug && i == 0) {
					cmn_err(CE_CONT,
					"id%d: IDHA_FAILFAST disabled\n",
					ID_INST(dev));
				}
			}
		}
		return (i);

	default:
		err = ENOTTY;
		break;
	}

	/*
	 * If there was no error and there is stuff to copy out,
	 * copy it out.
	 */

	if (err == 0 && nbo != 0) {
		if (ddi_copyout(p.data, (caddr_t) arg, nbo, flag)) {
			err = EFAULT;
		}
	}
	return (err);
}

/*
 * property operation routine.  return the number of blocks for the partition
 * in question or forward the request to the property facilities.
 */

static int
id_prop_op(dev_t dev, dev_info_t *dip,
	ddi_prop_op_t prop_op, int mod_flags,
	char *name, caddr_t valuep, int *lengthp)
{
	int nblocks, length, km_flags;
	caddr_t buffer;
	id_unit_t *un;
	id_ctlr_t *c;
	int uinst, cinst;

	if (strcmp(name, "nblocks") != 0) {
		/* not mine.  pass it on.  */
		return (ddi_prop_op(dev, ddi_get_parent(dip), prop_op,
		    mod_flags, name, valuep, lengthp));
	}

	cinst = ID_CINST(dev);
	uinst = ID_UINST(dev);
	if ((cinst >= IPI_NSLAVE) ||
	    (c = id_ctlr[cinst]) == (id_ctlr_t *) NULL ||
	    (un = c->c_un[uinst]) == (id_unit_t *) NULL) {
		return (DDI_PROP_NOT_FOUND);
	}

	if (!IE_STAT_PRESENT(un->un_flags)) {
		return (DDI_PROP_NOT_FOUND);
	}

	if (!(un->un_flags & ID_LABEL_VALID)) {
		return (DDI_PROP_NOT_FOUND);
	}


	nblocks = (int)un->un_lpart[ID_LPART(dev)].un_map.dkl_nblk;

	/*
	 * get caller's length; set return length.
	 */
	length = *lengthp;		/* Get callers length */
	*lengthp = sizeof (int);	/* Set callers length */

	/*
	 * If length only request or prop length == 0, get out now.
	 * (Just return length, no value at this level.)
	 */
	if (prop_op == PROP_LEN)  {
		*lengthp = sizeof (int);
		return (DDI_PROP_SUCCESS);
	}

	/*
	 * Allocate buffer, if required.  Either way, set `buffer' variable.
	 */

	switch (prop_op)  {
	case PROP_LEN_AND_VAL_ALLOC:

		km_flags = KM_NOSLEEP;

		if (mod_flags & DDI_PROP_CANSLEEP)
			km_flags = KM_SLEEP;

		buffer = kmem_alloc((size_t)sizeof (int), km_flags);
		if (buffer == NULL)  {
			return (DDI_PROP_NO_MEMORY);
		}
		*(caddr_t *)valuep = buffer; /* Set caller's buf ptr */
		break;

	case PROP_LEN_AND_VAL_BUF:

		if (sizeof (int) > (length))
			return (DDI_PROP_BUF_TOO_SMALL);
		buffer = valuep; /* get callers buf ptr */
		break;
	default:
		return (DDI_PROP_INVAL_ARG);
	}

	*((int *)buffer) = nblocks;
	return (DDI_PROP_SUCCESS);
}

/*
 * Section 6.0:	Ioctl support Functions
 */

/*
 * IPI commands via ioctl interface.
 */
static int
id_ioctl_cmd(dev_t dev, struct hdk_cmd *com, int flag)
{
	id_ctlr_t *c;
	id_unit_t *un;
	int flags, ipi_flags, err, resid;
	caddr_t uaddr;
	enum uio_seg uioseg;

	c = id_ctlr[ID_CINST(dev)];
	un = c->c_un[ID_UINST(dev)];

	resid = err = flags = 0;
	if ((flag & FKIOCTL) || (com->hdkc_flags & HDK_KBUF))
		uioseg = UIO_SYSSPACE;
	else
		uioseg = UIO_USERSPACE;

	/*
	 * Check the parameters.
	 */
	switch (com->hdkc_cmd & 0xff) {
	case IP_READ:
		flags = B_READ;
		if (com->hdkc_buflen != (com->hdkc_secnt << un->un_log_bshift))
			return (EINVAL);
		break;
	case IP_WRITE:
		flags = B_WRITE;
		if (com->hdkc_buflen != (com->hdkc_secnt << un->un_log_bshift))
			return (EINVAL);
		break;

	case IP_READ_BUF:
		flags = B_READ;
		if (com->hdkc_buflen != (com->hdkc_secnt << un->un_log_bshift))
			return (EINVAL);
		break;

	case IP_WRITE_BUF:
		flags = B_WRITE;
		if (com->hdkc_buflen != (com->hdkc_secnt << un->un_log_bshift))
			return (EINVAL);
		break;

	case IP_READ_PHECC:
		flags = B_READ;
		if (com->hdkc_buflen != (com->hdkc_secnt << un->un_log_bshift))
			return (EINVAL);
		break;
	case IP_WRITE_PHECC:
		flags = B_WRITE;
		if (com->hdkc_buflen != (com->hdkc_secnt << un->un_log_bshift))
			return (EINVAL);
		break;


	case IP_READ_DEFLIST:
		flags = B_READ;
		if (com->hdkc_buflen == 0)
			return (EINVAL);
		break;

	case IP_WRITE_DEFLIST:
		flags = B_WRITE;
		if (com->hdkc_buflen == 0)
			return (EINVAL);
		break;

	case IP_FORMAT:
	case IP_REALLOC:
		if (com->hdkc_buflen != 0)
			return (EINVAL);
		break;
	default:
		return (EINVAL);
	}


	/*
	 * If we have data to move, fill in a uio structure.
	 */

	if (com->hdkc_buflen > 0) {
		/*
		 * Don't allow more than max at once.
		 */

		if (com->hdkc_buflen > ID_MAXBUFSIZE)
			return (EINVAL);

		if (uioseg == UIO_SYSSPACE) {
			uaddr = com->hdkc_bufaddr;
		} else {
			uaddr = kmem_alloc(com->hdkc_buflen, KM_SLEEP);
			if ((flags & B_READ) == 0) {
				if (ddi_copyin(com->hdkc_bufaddr, uaddr,
				    com->hdkc_buflen, 0)) {
					kmem_free(uaddr, com->hdkc_buflen);
					return (EFAULT);
				}
			}
		}
	}

	/*
	 * Execute the command.
	 */
	if (err == 0) {
		switch (com->hdkc_cmd & 0xff) {
		case IP_FORMAT:
			err = id_format(un, com);
			break;

		case IP_REALLOC:
			err = id_realloc(un, com);
			break;		/* Bug ID: 1116720 */

		case IP_READ_DEFLIST:
		case IP_WRITE_DEFLIST:

			err = id_rdwr_deflist(dev, com, uaddr, flags, &resid);
			break;

		case IP_READ:
		case IP_WRITE:
		case IP_READ_BUF:
		case IP_WRITE_BUF:
		case IP_READ_PHECC:
		case IP_WRITE_PHECC:
			err = id_rdwr(dev, com, uaddr, flags, &resid);
			break;



		case IP_ATTRIBUTES:
		default:
			err = EINVAL;
			break;
		}
	}


	if (err && (com->hdkc_flags & HDK_DIAGNOSE)) {
		mutex_enter(&c->c_lock);
		un->un_diag.hdkd_errcmd = com->hdkc_cmd;
#ifdef	NOT
		/*
		 * following fields are already filled in, see idintr().
		 * also bp is not valid at this point, check id_rdwr()
		 */
		if (bp && (bp->b_flags & B_ERROR)) {
			un->un_diag.hdkd_errsect = bp->b_blkno;
			un->un_diag.hdkd_severe = IDE_SEV(bp->b_error);
			un->un_diag.hdkd_errno = IDE_ERRNO(bp->b_error);
		} else {
			un->un_diag.hdkd_errsect = com->hdkc_blkno;
			un->un_diag.hdkd_severe = IDE_SEV(IDE_FATAL);
			un->un_diag.hdkd_errno = IDE_ERRNO(IDE_FATAL);
		}
#endif
		mutex_exit(&c->c_lock);
	}
	if (com->hdkc_buflen > 0) {
		if (err == 0 && uioseg != UIO_SYSSPACE && (flags & B_READ)) {
			if (ddi_copyout(uaddr, com->hdkc_bufaddr,
			    com->hdkc_buflen - resid, 0)) {
				err = EFAULT;
			}
		}
		if (uioseg != UIO_SYSSPACE) {
			kmem_free(uaddr, com->hdkc_buflen);
		}
	}
	return (err);
}

static void
id_build_user_vtoc(id_unit_t *un, struct vtoc *vtoc)
{

	int i;
	long nblks;
	struct dk_map2 *lpart;
	struct un_lpart	*lmap;
	struct partition *vpart;


	/*
	 * Return vtoc structure fields in the provided VTOC area,
	 * addressed by *vtoc.
	 *
	 */

	bzero((caddr_t) vtoc, sizeof (struct vtoc));

	bcopy((caddr_t) un->un_vtoc.v_bootinfo,
	    (caddr_t) vtoc->v_bootinfo, sizeof (vtoc->v_bootinfo));

	vtoc->v_sanity		= VTOC_SANE;
	vtoc->v_version		= un->un_vtoc.v_version;

	bcopy((caddr_t) un->un_vtoc.v_volume,
	    (caddr_t) vtoc->v_volume, LEN_DKL_VVOL);

	vtoc->v_sectorsz = DEV_BSIZE;
	vtoc->v_nparts = un->un_vtoc.v_nparts;

	bcopy((caddr_t) un->un_vtoc.v_reserved,
	    (caddr_t) vtoc->v_reserved, sizeof (vtoc->v_reserved));
	/*
	 * Convert partitioning information.
	 *
	 * Note the conversion from starting cylinder number
	 * to starting sector number.
	 */
	lmap = un->un_lpart;
	lpart = un->un_vtoc.v_part;
	vpart = vtoc->v_part;

	nblks = un->un_g.dkg_nhead * un->un_g.dkg_nsect;

	for (i = 0; i < V_NUMPAR; i++) {
		vpart->p_tag	= lpart->p_tag;
		vpart->p_flag	= lpart->p_flag;
		vpart->p_start	= lmap->un_map.dkl_cylno * nblks;
		vpart->p_size	= lmap->un_map.dkl_nblk;

		lmap++;
		lpart++;
		vpart++;
	}

	bcopy((caddr_t) un->un_vtoc.v_timestamp,
	    (caddr_t) vtoc->timestamp, sizeof (vtoc->timestamp));

	bcopy((caddr_t) un->un_asciilabel,
	    (caddr_t) vtoc->v_asciilabel, LEN_DKL_ASCII);

}


/*
 * Disk geometry macros
 *
 *	spc:		sectors per cylinder
 *	chs2bn:		cyl/head/sector to block number
 */
#define	spc(l)		(((l)->dkl_nhead*(l)->dkl_nsect)-(l)->dkl_apc)

#define	chs2bn(l, c, h, s)	\
			((daddr_t)((c)*spc(l)+(h)*(l)->dkl_nsect+(s)))


static int
id_write_label_from_vtoc(id_unit_t *un, struct vtoc *vtoc, dev_t dev)
{
	id_ctlr_t *c;
	struct un_lpart		*lmap;
	struct dk_map2		*lpart;
	struct partition	*vpart;
	long			nblks;
	long			ncyl;
	int			i;
	short sum, *sp;
	struct dk_label	*l;
	struct hdk_cmd	cmdblk;
	int			cyl, head;
	int			err;

	c = id_ctlr[ID_CINST(dev)];

	/*
	 * Sanity-check the vtoc
	 */
	if (vtoc->v_sanity != VTOC_SANE || vtoc->v_nparts != V_NUMPAR) {
		return (EINVAL);
	}

	nblks = un->un_g.dkg_nhead * un->un_g.dkg_nsect;

	vpart = vtoc->v_part;
	for (i = 0; i < V_NUMPAR; i++) {
		if ((vpart->p_start % nblks) != 0)
			return (EINVAL);
		ncyl = vpart->p_start % nblks;
		ncyl += vpart->p_size % nblks;
		if ((vpart->p_size % nblks) != 0)
			ncyl++;
		if (ncyl > (long) un->un_g.dkg_ncyl)
			return (EINVAL);
		vpart++;
	}


	l = (struct dk_label *) kmem_zalloc(sizeof (*l), KM_SLEEP);

	mutex_enter(&c->c_lock);

	bcopy((caddr_t) un->un_asciilabel, (caddr_t) l->dkl_asciilabel,
	    LEN_DKL_ASCII);
	bcopy((caddr_t) &un->un_vtoc, (caddr_t) &(l->dkl_vtoc),
	    sizeof (struct dk_vtoc));

	/*
	 * Put appropriate vtoc structure fields into the disk label
	 */

	bcopy((caddr_t) vtoc->v_bootinfo,
	    (caddr_t) un->un_vtoc.v_bootinfo,
	    sizeof (vtoc->v_bootinfo));

	un->un_vtoc.v_sanity = vtoc->v_sanity;
	un->un_vtoc.v_version = vtoc->v_version;

	bcopy((caddr_t) vtoc->v_volume,
	    (caddr_t) un->un_vtoc.v_volume, LEN_DKL_VVOL);

	un->un_vtoc.v_nparts = vtoc->v_nparts;

	bcopy((caddr_t) vtoc->v_reserved,
	    (caddr_t) un->un_vtoc.v_reserved,
	    sizeof (vtoc->v_reserved));

	/*
	 * Note the conversion from starting sector number
	 * to starting cylinder number.
	 * Return error if division results in a remainder.
	 */
	lmap = un->un_lpart;
	lpart = un->un_vtoc.v_part;
	vpart = vtoc->v_part;


	for (i = 0; i < (int)vtoc->v_nparts; i++) {
		lpart->p_tag  = vtoc->v_part[i].p_tag;
		lpart->p_flag = vtoc->v_part[i].p_flag;
		lmap->un_map.dkl_cylno = vpart->p_start / nblks;
		lmap->un_map.dkl_nblk = vpart->p_size;

		lmap++;
		lpart++;
		vpart++;
	}

	id_update_map(un);	/* update map with geometry info */

	bcopy((caddr_t) vtoc->timestamp,
	    (caddr_t) un->un_vtoc.v_timestamp,
	    sizeof (vtoc->timestamp));

	bcopy((caddr_t) vtoc->v_asciilabel,
	    (caddr_t) un->un_asciilabel, LEN_DKL_ASCII);

	l->dkl_rpm	= un->un_g.dkg_rpm;
	l->dkl_pcyl	= un->un_g.dkg_pcyl;
	l->dkl_apc	= un->un_g.dkg_apc;
	l->dkl_obs1	= un->un_g.dkg_obs1;
	l->dkl_obs2	= un->un_g.dkg_obs2;
	l->dkl_intrlv	= un->un_g.dkg_intrlv;
	l->dkl_ncyl	= un->un_g.dkg_ncyl;
	l->dkl_acyl	= un->un_g.dkg_acyl;
	l->dkl_nhead	= un->un_g.dkg_nhead;
	l->dkl_nsect	= un->un_g.dkg_nsect;
	l->dkl_obs3	= un->un_g.dkg_obs3;

	for (i = 0; i < NDKMAP; i++)
		l->dkl_map[i] = un->un_lpart[i].un_map;

	l->dkl_magic			= DKL_MAGIC;
	l->dkl_write_reinstruct	= un->un_g.dkg_write_reinstruct;
	l->dkl_read_reinstruct	= un->un_g.dkg_read_reinstruct;

	sum = 0;
	sp = (short *) l;
	i = sizeof (struct dk_label)/sizeof (short);
	while (i--) {
		sum ^= *sp++;
	}
	l->dkl_cksum = sum;
	/*
	 * Write and verify the labels including backup
	 */

	cyl = l->dkl_ncyl + l->dkl_acyl - 1;
	head = l->dkl_nhead-1;

	/*
	 * Write absolute block zero and
	 * 5 copies in a bizarre place.
	 */
	un->un_flags &= ~ID_LABEL_VALID;
	mutex_exit(&c->c_lock);

	for (i = 0; i < 5 * 2 + 1; i += 2) {
		if (i > 0) {
			cmdblk.hdkc_blkno = chs2bn(l, cyl, head, i);
		} else {
			cmdblk.hdkc_blkno = 0;
			i = -1;
			/*
			 * The += 2 will save us
			 */
		}
		cmdblk.hdkc_secnt = 1;
		cmdblk.hdkc_bufaddr = (caddr_t) l;
		cmdblk.hdkc_buflen = DEV_BSIZE;
		cmdblk.hdkc_flags = HDK_KBUF;
		cmdblk.hdkc_cmd = (1 << 8) | IP_WRITE;
		if ((err = id_ioctl_cmd(dev, &cmdblk, FKIOCTL)) != 0)
			break;

	}
	if (err == 0) {
		mutex_enter(&c->c_lock);
		un->un_flags |= ID_LABEL_VALID;
		mutex_exit(&c->c_lock);
	}
	kmem_free((caddr_t)l, sizeof (struct dk_label));
	return (err);
}

/*
 * Section 6.1:	Format Support Functions
 */

/*
 * Format disk.
 * If first_block is less than zero, format entire disk.
 * format up to nblocks, or all if block count < 0.
 */
static int
id_format(register id_unit_t *un, struct hdk_cmd *com)
{
	struct buf *bp;
	int nblocks, opmod, errno, i, ipi_flags;
	daddr_t first_block;
	ipiq_t *q;
	struct ipi3header *ip;
	char *cp;


	errno = 0;
	nblocks = com->hdkc_secnt;
	first_block = com->hdkc_blkno;
	opmod = (com->hdkc_cmd >> 8) & 0xff;
	ipi_flags = 0;
	if (com->hdkc_flags & HDK_DIAGNOSE)
		ipi_flags |= IP_DIAGNOSE;
	if (com->hdkc_flags & HDK_SILENT)
		ipi_flags |= IP_SILENT;

	/*
	 * If a block number range is specified, make sure it is
	 * on a cylinder boundary.
	 */
	if (nblocks > 0 || first_block != 0) {
		int blk_per_cyl = un->un_g.dkg_nsect;
		if ((first_block % blk_per_cyl) || (nblocks % blk_per_cyl))
			return (EINVAL);
	}

	errno = IDU_ALLOC(un, 0, DDI_DMA_SLEEP, 0, &q);
	if (errno)
		return (errno);
	errno = id_getsbuf(un, 0, (caddr_t) 0, B_READ, 0);
	if (errno) {
		IDU_RELSE(un, q);
		return (errno);
	}
	bp = un->un_sbufp;

	/*
	 * Form IPI-3 Command packet.
	 */
	ip = q->q_cmd;
	ip->hdr_opcode = IP_FORMAT;
	ip->hdr_mods = opmod | IP_OM_BLOCK;
	cp = (char *)(ip + 1);				/* point past header */

	/*
	 * Insert block size parameter.
	 */
	for (i = (int)(cp + 2); (i % sizeof (long)) != 0; i++)
		*cp++ = 0;			/* pad */
	*cp++ = sizeof (struct bsize_parm) + 1;	/* parm + id */
	*cp++ = BSIZE_PARM;
	((struct bsize_parm *) cp)->blk_size = DEV_BSIZE;
	cp += sizeof (struct bsize_parm);

	/*
	 * Insert extent parameter, if needed.
	 */
	if (nblocks > 0 || first_block != 0) {
		for (i = (int)(cp + 2); (i % sizeof (long)) != 0; i++)
			*cp++ = 0;	/* pad */
		*cp++ = sizeof (struct cmdx_parm) + 1;	/* parm + id */
		*cp++ = CMD_EXTENT;
		((struct cmdx_parm *) cp)->cx_count = nblocks;
		((struct cmdx_parm *) cp)->cx_addr = first_block;
		cp += sizeof (struct cmdx_parm);
	}
	ip->hdr_pktlen = (cp - (char *)ip) - sizeof (ip->hdr_pktlen);

	/*
	 * Send command, no timeout.
	 */
	q->q_flag = IP_ABS_BLOCK | ipi_flags;
	q->q_private[Q_BUF] = (long) bp;
	q->q_private[Q_ERRBLK] = first_block;
	bp->b_forw = (struct buf *) q;

	idstrategy(bp);
	errno = biowait(bp);

	if (errno || q->q_result != IP_SUCCESS) {
		cmn_err(CE_WARN, "id%d: id_format failed. errno %d result %d",
		    ddi_get_instance(un->un_dip), errno, q->q_result);
		errno = EIO;
	}
	IDU_RELSE(un, q);
	sema_v(&un->un_sbs);
	return (errno);
}

/*
 * Reallocate a defective block.
 */
static int
id_realloc(id_unit_t *un, struct hdk_cmd *com)
{
	struct buf *bp;
	daddr_t first_block;
	int nblocks, opmod, errno, i, ipi_flags;
	ipiq_t *q;
	struct ipi3header *ip;
	char *cp;

	errno = 0;
	nblocks = com->hdkc_secnt;
	first_block = com->hdkc_blkno;
	opmod = (com->hdkc_cmd >> 8) & 0xff;
	ipi_flags = 0;
	if (com->hdkc_flags & HDK_DIAGNOSE)
		ipi_flags |= IP_DIAGNOSE;
	if (com->hdkc_flags & HDK_SILENT)
		ipi_flags |= IP_SILENT;

	if (nblocks <= 0)
		return (EINVAL);

	/* Bug ID: 1116720 - Re-ordered this section to make more sense */
	errno = IDU_ALLOC(un, 0, DDI_DMA_SLEEP, 0, &q);
	if (errno) {
		sema_v(&un->un_sbs);
		return (errno);
	}
	errno = id_getsbuf(un, 0, (caddr_t) 0, B_READ, 0);
	if (errno) {
		IDU_RELSE(un, q);
		return (errno);
	}
	bp = un->un_sbufp;

	ip = q->q_cmd;
	ip->hdr_opcode = IP_REALLOC;
	ip->hdr_mods = opmod | IP_OM_BLOCK;
	cp = (char *)(ip + 1);				/* point past header */

	/*
	 * Insert extent parameter, if needed.
	 */
	if (nblocks > 0 || first_block != 0) {
		for (i = (int)(cp + 2); (i % sizeof (long)) != 0; i++)
			*cp++ = 0;			/* pad */
		*cp++ = sizeof (struct cmdx_parm) + 1;	/* parm + id */
		*cp++ = CMD_EXTENT;
		((struct cmdx_parm *) cp)->cx_count = nblocks;
		((struct cmdx_parm *) cp)->cx_addr = first_block;
		cp += sizeof (struct cmdx_parm);
	}
	ip->hdr_pktlen = (cp - (char *)ip) - sizeof (ip->hdr_pktlen);

	/*
	 * Send command.
	 */
	q->q_flag = IP_ABS_BLOCK | ipi_flags;
	q->q_time = id_timeout;
	q->q_private[Q_BUF] = (long) bp;
	q->q_private[Q_ERRBLK] = first_block;
	bp->b_forw = (struct buf *) q;	/* Bug ID: 1116720 */

	idstrategy(bp);
	errno = biowait(bp);

	if (errno || q->q_result != IP_SUCCESS) {
		cmn_err(CE_WARN, "id%d: id_realloc failed. errno %d result %d",
		    ddi_get_instance(un->un_dip), errno, q->q_result);
		errno = EIO;
	}
	IDU_RELSE(un, q);
	sema_v(&un->un_sbs);
	return (errno);
}


/*
 * Read or write defect list.
 */
static int
id_rdwr_deflist(dev_t dev, struct hdk_cmd *com, caddr_t addr, int rw, int *rsp)
{
	id_ctlr_t *c;
	id_unit_t *un;
	struct buf *bp;
	int i, opmode, ipi_flags, errno, opcode;
	char *cp;
	ipiq_t *q;

	c = id_ctlr[ID_CINST(dev)];
	un = c->c_un[ID_UINST(dev)];

	errno = 0;
	opcode = (rw & B_READ)? IP_READ_DEFLIST : IP_WRITE_DEFLIST;
	opmode = (com->hdkc_cmd >> 8) & 0xff;
	ipi_flags = 0;
	if (com->hdkc_flags & HDK_DIAGNOSE)
		ipi_flags |= IP_DIAGNOSE;
	if (com->hdkc_flags & HDK_SILENT)
		ipi_flags |= IP_SILENT;

	bp = un->un_sbufp;
	(void) id_getsbuf(un, com->hdkc_buflen, addr, rw|B_KERNBUF, 0);
	errno = IDU_ALLOC(un, bp, DDI_DMA_SLEEP, 0, &q);
	if (errno) {
		sema_v(&un->un_sbs);
		return (errno);
	}
	bp->b_forw = (struct buf *) q;

	/*
	 * Form IPI-3 Command packet.
	 */
	q->q_cmd->hdr_opcode = opcode;
	q->q_cmd->hdr_mods = opmode;
	cp = (char *)(q->q_cmd + 1);		/* point past header */

	/*
	 * Add Transfer notification if required.
	 */
	if (q->q_tnp_len == sizeof (caddr_t)) {
		for (i = (int)(cp + 2); (i % sizeof (caddr_t)) != 0; i++)
			*cp++ = 0;			/* pad */
		*cp++ = sizeof (caddr_t) + 1;
		*cp++ = XFER_NOTIFY;
		bcopy(q->q_tnp, cp, sizeof (caddr_t));
		cp += sizeof (caddr_t);
	} else if (q->q_tnp_len > 0) {
		*cp++ = q->q_tnp_len + 1;
		*cp++ = XFER_NOTIFY;
		bcopy(q->q_tnp, cp, q->q_tnp_len);
		cp += q->q_tnp_len;
	}

	/*
	 * Insert Request Parameters as Data Parameter.
	 */
	*cp++ = 3;				/* parameter length */
	*cp++ = ATTR_REQPARM;			/* request parm parameter */
	*cp++ = RESP_AS_NDATA;
	*cp++ = DEFLIST_TRACK;			/* track defects list parm */

	/*
	 * Add command extent parameter for controllers that need it.
	 * (That's us).
	 */
	for (i = (int)(cp + 2); (i % sizeof (long)) != 0; i++)
		*cp++ = 0;			/* pad */
	*cp++ = sizeof (struct cmdx_parm) + 1;	/* len including ID */
	*cp++ = CMD_EXTENT;
	((struct cmdx_parm *) cp)->cx_count = bp->b_bcount;
	((struct cmdx_parm *) cp)->cx_addr = 0;
	cp += sizeof (struct cmdx_parm);
	q->q_cmd->hdr_pktlen =
	    (cp - (char *)q->q_cmd) - sizeof (q->q_cmd->hdr_pktlen);

	bp->b_resid = bp->b_bcount;
	bp->b_forw = (struct buf *) q;

	q->q_flag = ipi_flags | IP_ABS_BLOCK | IP_BYTE_EXT;
	q->q_private[Q_BUF] = (long) bp;
	q->q_time = id_timeout;

	/*
	 * Send command.
	 */

	idstrategy(bp);
	errno = biowait(bp);
	if (errno == 0) {
		if (q->q_result != IP_SUCCESS) {
			bp->b_blkno = (daddr_t)q->q_private[Q_ERRBLK];
			cmn_err(CE_WARN, "id%d: id_rdwr_deflist failed. "
			    "result %d", ddi_get_instance(un->un_dip),
			    q->q_result);
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_ERROR;
			bp->b_error = EIO;
		} else if (q->q_resp) {
			struct icprarg r;
			/*
			 * Handle response extent or parameter length
			 * parameter. This will set the residual count
			 * in the buffer.
			 */
			r.q = q;
			r.rt = id_deflist_table;
			r.a = (caddr_t) bp;
			IDC_CONTROL(c, IPI_CTRL_PARSERESP, &r, 0);
		}
	}
	IDU_RELSE(un, q);
	*rsp = bp->b_resid;
	sema_v(&un->un_sbs);
	return (errno);
}

/*
 * Read or write disk data.
 *
 * The block number is absolute:  no partition mapping is needed.
 */

static int
id_rdwr(dev_t dev, struct hdk_cmd *com, caddr_t addr, int rw, int *rsp)
{
	id_ctlr_t *c;
	id_unit_t *un;
	struct buf *bp;
	ipiq_t *q;
	int i, opcode, ipi_flags, nblks, errno;

	c = id_ctlr[ID_CINST(dev)];
	un = c->c_un[ID_UINST(dev)];

	errno = 0;
	opcode = com->hdkc_cmd & 0xff;
	ipi_flags = 0;
	if (com->hdkc_flags & HDK_DIAGNOSE)
		ipi_flags |= IP_DIAGNOSE | IP_NO_RETRY;
	if (com->hdkc_flags & HDK_SILENT)
		ipi_flags |= IP_SILENT;


	(void) id_getsbuf(un, com->hdkc_buflen, addr, rw|B_KERNBUF, 0);
	bp = un->un_sbufp;
	errno = IDU_ALLOC(un, bp, DDI_DMA_SLEEP, 0, &q);
	if (errno) {
		sema_v(&un->un_sbs);
		return (errno);
	}
	bp->b_resid = bp->b_bcount;
	bp->b_forw = (struct buf *) q;

	nblks = NBLKS(bp->b_bcount, un);
	/*
	 * Construct IPI-3 packet.
	 */
	id_build_rdwr(q, com->hdkc_blkno, nblks, opcode);

	/*
	 * Stuff values into q
	 */
	q->q_private[Q_BUF] = (long) bp;
	q->q_private[Q_ERRBLK] = (long) com->hdkc_blkno;
	q->q_flag = ipi_flags | IP_ABS_BLOCK;

	idstrategy(bp);
	errno = biowait(bp);
	if (errno == 0 && q->q_result != IP_SUCCESS) {
		errno = bp->b_error = EIO;
		bp->b_flags |= B_ERROR;
		bp->b_resid = bp->b_bcount;
		/*
		 * XXX: How does this propagate back? See idintr().
		 */
	}
	IDU_RELSE(un, q);
	sema_v(&un->un_sbs);
	*rsp = bp->b_resid;
	return (errno);
}

/*
 * Section 7.0: Miscellaneous support routines
 */

/*
 * Update starting block numbers for each of the partitions.
 * This saves converting a cylinder offset to a block offset on each command.
 * Must be called when changing partition map or geometry.
 */
static void
id_update_map(id_unit_t *un)
{
	struct un_lpart	*lp;
	for (lp = un->un_lpart; lp < &un->un_lpart[NDKMAP]; lp++) {
		lp->un_blkno = (lp->un_map.dkl_cylno + un->un_g.dkg_bcyl) *
		    un->un_g.dkg_nhead * un->un_g.dkg_nsect +
		    un->un_first_block;
	}
}

/*
 * This routine verifies that the block read is indeed a disk label.
 */
static int
id_islabel(id_unit_t *un, register struct dk_label *l)
{
	struct dk_geom *g;
	unsigned short acyl;
	int inst;

	if (l->dkl_magic != DKL_MAGIC)
		return (0);

	inst = ddi_get_instance(un->un_dip);

	if (!id_lcksum(l)) {
		cmn_err(CE_WARN, "id%d: corrupt label", inst);
		return (0);
	}
	g = &un->un_g;

	/*
	 * Check the label geometry against the geometry values read
	 * from the physical disk configuration attribute parameter.
	 *
	 * If they don't match, the disk is screwed up and needs to be
	 * reformatted.
	 */

	acyl = g->dkg_pcyl - g->dkg_ncyl;	/* for checking */

	if (l->dkl_ncyl > g->dkg_ncyl || l->dkl_pcyl > g->dkg_pcyl ||
	    l->dkl_acyl < acyl || g->dkg_nhead != l->dkl_nhead ||
	    g->dkg_nsect != l->dkl_nsect || g->dkg_rpm != l->dkl_rpm) {

		cmn_err(CE_WARN, "id%d: label says ncyl %d pcyl %d acyl %d "
		    "head %d sect %d rpm %d", inst, l->dkl_ncyl, l->dkl_pcyl,
		    l->dkl_acyl, l->dkl_nhead, l->dkl_nsect, l->dkl_rpm);

		cmn_err(CE_WARN, "id%d: IPI attributes say ncyl %d pcyl %d "
		    "acyl %d head %d sect %d rpm %d", inst, g->dkg_ncyl,
		    g->dkg_pcyl, acyl, g->dkg_nhead, g->dkg_nsect, g->dkg_rpm);

		cmn_err(CE_WARN,
		    "id%d: label doesn't match IPI attribute info", inst);
		return (0);
	}
	return (1);
}

/*
 * This routine checks the checksum of the disk label.
 */
static int
id_lcksum(register struct dk_label *l)
{
	register short *sp, sum = 0;
	register short count = sizeof (struct dk_label)/sizeof (short);

	sp = (short *)l;
	while (count--)
		sum ^= *sp++;
	return (sum ? 0 : 1);
}

/*
 * This routine puts the label information into the various parts of
 * the unit structure.  It is always called at disk interrupt priority.
 */
static void
id_uselabel(register id_unit_t *un, register struct dk_label *l)
{
	register struct dk_geom	*g = &un->un_g;
	int i, intrlv;

	g->dkg_intrlv = l->dkl_intrlv;
	g->dkg_nhead = l->dkl_nhead;
	g->dkg_nsect = l->dkl_nsect;
	g->dkg_rpm = l->dkl_rpm;
	g->dkg_ncyl = l->dkl_ncyl;
	g->dkg_pcyl = l->dkl_pcyl;
	g->dkg_acyl = l->dkl_acyl;
	g->dkg_write_reinstruct = l->dkl_write_reinstruct;
	g->dkg_read_reinstruct = l->dkl_read_reinstruct;
	/*
	 * Fill in the logical partition map.
	 */
	for (i = 0; i < NDKMAP; i++)
		un->un_lpart[i].un_map = l->dkl_map[i];
	id_update_map(un);	/* update map with geometry info */
	bcopy((caddr_t) l->dkl_asciilabel,
	    (caddr_t) un->un_asciilabel, LEN_DKL_ASCII);
	un->un_vtoc = l->dkl_vtoc;
	un->un_flags |= ID_LABEL_VALID;
}

static void
id_minphys(struct buf *bp)
{
	if (bp->b_bcount > ID_MAXPHYS)
		bp->b_bcount = ID_MAXPHYS;
}

/* ARGSUSED4 */
static int
id_getsbuf(id_unit_t *un, int bcount, caddr_t addr, int bflags, int intr)
{
	struct buf *bp;
	unsigned int minbits;

	minbits = IPI_SLAVE(un->un_ipi_addr) * ID_NUNIT;
	minbits |= IPI_FAC(un->un_ipi_addr);
	minbits *= NDKMAP;
	bp = un->un_sbufp;

	sema_p(&un->un_sbs);
	bp->b_resid = bp->b_bcount = bcount;
	bp->b_un.b_addr = addr;
	bp->b_flags = B_BUSY|bflags;
	bp->b_edev = makedevice(0, minbits);
	bp->b_dev = cmpdev(bp->b_edev);
	bp->b_forw = (struct buf *) 0;
	return (0);
}

/*
 * Build a read/write command for request.
 */
static void
id_build_rdwr(ipiq_t *q, daddr_t blkno, int nblks, int opcode)
{
	register struct ipi3header *ip;
	union packet {
		u_char	*pp;		/* pointer to next packet byte */
		u_long	*lp;		/* pointer parm len, id, and pad) */
		struct cmdx_parm *cxp;	/* pointer to command extent parm */
	} pu;

	/*
	 * Form IPI-3 Command packet.
	 */
	ip = (struct ipi3header *)q->q_cmd;
	ip->hdr_opcode = opcode;
	ip->hdr_mods = IP_OM_BLOCK;

	/*
	 * fill in command extent (block count and block address).
	 */
	pu.pp = (u_char *)ip + sizeof (struct ipi3header);
	*pu.lp++ = (sizeof (struct cmdx_parm)+1) << 8 | CMD_EXTENT;
	pu.cxp->cx_count = nblks;
	pu.cxp->cx_addr = blkno;
	pu.pp += sizeof (struct cmdx_parm);

	/*
	 * Fill in the transfer notification parameter if needed.
	 * Note that this may leave pu.pp non-word-aligned.
	 */
	if (q->q_tnp_len == sizeof (caddr_t)) {
		*pu.lp++ = (sizeof (caddr_t)+1) << 8 | XFER_NOTIFY;
		*pu.lp++ = * (u_long *) q->q_tnp;
	} else if (q->q_tnp_len > 0) {
		*pu.pp++ = q->q_tnp_len + 1;	/* parameter length */
		*pu.pp++ = XFER_NOTIFY;		/* parameter ID */
		bcopy(q->q_tnp, (caddr_t)pu.pp, q->q_tnp_len);
		pu.pp += q->q_tnp_len;
	}

	if (opcode == IP_READ_BUF || opcode == IP_WRITE_BUF) {
		*pu.pp++ = 3;
		*pu.pp++ = BUFMODE_ID;
		*pu.pp++ = GENERIC_BUFFER;
		*pu.pp++ = 0;

	}

	/*
	 * Form packet length based on where the pointer ended up.
	 */
	ip->hdr_pktlen = (pu.pp - (u_char *)ip) - sizeof (ip->hdr_pktlen);
}

/*
 * Queue a command for eventual or immediate retry.
 */
static void
idqretry(id_ctlr_t *c, ipiq_t *q)
{
	if ((c->c_flags & (IE_RECOVER | IE_RE_INIT))) {
		q->q_next = c->c_retry_q;
		c->c_retry_q = q;
	} else {
		IDC_START(c, q);
	}
}

/*
 * Section 8.0:	I/O Start routines
 */

/*
 * idrestart - callback completion point
 */

static int
idrestart(caddr_t arg)
{
	id_ctlr_t *c = (id_ctlr_t *) arg;
	/*
	 * Do the lock acquire/release to serialize with the code below
	 */
	mutex_enter(&c->c_lock);
	mutex_exit(&c->c_lock);
	return (idstart(c));
}

/*
 * idstart- start command(s) for facilities on this slave.
 * we enter with no locks held.
 */

static int
idstart(register id_ctlr_t *c)
{
	register id_unit_t *un;
	register struct buf *bp;
	auto ipiq_t *q;
	daddr_t blkno;
	int nblks, i, err, sfac;

	sfac = c->c_nextfac++;
	sfac &= (ID_NUNIT - 1);

	/*
	 * Check for the continued existence and readiness of the slave.
	 */
again:
	if (!IE_STAT_PRESENT(c->c_flags) || !IE_STAT_READY(c->c_flags)) {
		/*
		 * The first tests were lock unsafe.
		 * These are the real tests. It will
		 * be unfortunate if IE_STAT_PRESENT
		 * goes south while we're busy trying
		 * to send a command, but that's just
		 * too bloody bad. We have to do this
		 * this way because otherwise we're
		 * back to a single slave lock being
		 * acquired for each command.
		 */

		mutex_enter(&c->c_lock);
		if (!IE_STAT_PRESENT(c->c_flags)) {
			/*
			 * The slave is (no longer/not) present.
			 * Give an error for all queued up buffers.
			 */
			for (i = 0; i < ID_NUNIT; i++) {
				if ((un = c->c_un[i]) == NULL)
					continue;
				mutex_enter(&un->un_qlock);
				while ((bp = un->un_bufs.av_forw) != NULL) {
					un->un_bufs.av_forw = bp->av_forw;
					bp->b_error = ENXIO;
					bp->b_flags |= B_ERROR;
					bp->b_resid = bp->b_bcount;
					biodone(bp);
					if (un->un_stats) {
						mutex_enter(&un->un_slock);
						kstat_waitq_exit(IOSP);
						mutex_exit(&un->un_slock);
					}
				}
				mutex_exit(&un->un_qlock);
			}
			mutex_exit(&c->c_lock);
			return (1);
		}

		/*
		 * The slave is still present. Check to see
		 * whether it is in a "ready" state.
		 */

		if (!IE_STAT_READY(c->c_flags)) {
			if (iddebug > 1) {
				cmn_err(CE_CONT, "%s%d: idstart, controller "
				    "not ready (0x%x)\n", c->c_name,
				    c->c_instance, c->c_flags);
			}
			/*
			 * Let error recovery restart us
			 */
			mutex_exit(&c->c_lock);
			return (1);
		}
		mutex_exit(&c->c_lock);
	}


	bp = NULL;

	/*
	 * Search for a facility that need service. It is nicest to try
	 * and round-robin amongst the present facilities, but doing
	 * any lock acquisition for that purpose is not worth the cost.
	 * Instead we'll use the field in the slave structure c_nextfac
	 * as a starter value for our search, with the full knowledge
	 * that we may end up with N threads starting with the same
	 * value. The chances are good, though, that this won't happen
	 * very often.
	 */

	for (i = 0; i < ID_NUNIT; i++) {
		un = c->c_un[sfac++];
		sfac &= (ID_NUNIT - 1);
		if (un == (id_unit_t *) NULL) {
			continue;
		} else if (un->un_bufs.av_forw == NULL) {
			continue;
		} else if (mutex_tryenter(&un->un_qlock) == 0) {
			continue;
		}
		/*
		 * We got a live one. Pull it off the queue
		 * immediately and drop the lock. Reload bp
		 * and check again because the first check
		 * wasn't lock-safe.
		 */
		if ((bp = un->un_bufs.av_forw) != NULL) {
			un->un_bufs.av_forw = bp->av_forw;
			mutex_exit(&un->un_qlock);
			break;
		} else {
			mutex_exit(&un->un_qlock);
		}
	}

	/*
	 * If no bp, then either no work to do or we couldn't
	 * get the unit queue lock. In either case, return saying
	 * that there is no more work to do. In the case where
	 * we couldn't get the unit queue lock, we depend upon the
	 * thread who had the unit queue lock held to leave
	 * things in a state with either commands started or
	 * set up to be restarted in the future.
	 */

	if (bp == NULL) {
		return (1);
	}

	/*
	 * If resources are ready to go with this particular
	 * request, known by this being a 'special' buffer
	 * request, send the request off and loop around
	 * searching for more work. The status of PRESENT
	 * or READY (for the facility) is ignored for
	 * 'special' command.
	 */

	if (bp == un->un_sbufp) {
		bp->b_resid = 0;	/* See also id_respx */
		IDU_START(un, (ipiq_t *) bp->b_forw);
		goto again;
	}

	/*
	 * Check for presence and availability of the facility.
	 * The first test is a quick and dirty. If you spot
	 * something, do the real test after acquiring the
	 * lock that covers the thing you are testing.
	 */

	if (!IE_STAT_PRESENT(un->un_flags) || !IE_STAT_READY(un->un_flags)) {
		mutex_enter(&c->c_lock);

		if (!IE_STAT_PRESENT(un->un_flags)) {
			/*
			 * This unit has gone away. Give
			 * an error to the current request
			 * and give errors to all queued
			 * buffers.
			 */
			mutex_enter(&un->un_qlock);
			if (un->un_stats) {
				mutex_enter(&un->un_slock);
				kstat_waitq_exit(IOSP);
				mutex_exit(&un->un_slock);
			}
			bp->b_error = ENXIO;
			bp->b_flags |= B_ERROR;
			bp->b_resid = bp->b_bcount;
			biodone(bp);
			while ((bp = un->un_bufs.av_forw) != NULL) {
				un->un_bufs.av_forw = bp->av_forw;
				bp->b_error = ENXIO;
				bp->b_flags |= B_ERROR;
				bp->b_resid = bp->b_bcount;
				biodone(bp);
				if (un->un_stats) {
					mutex_enter(&un->un_slock);
					kstat_waitq_exit(IOSP);
					mutex_exit(&un->un_slock);
				}
			}
			mutex_exit(&un->un_qlock);
			mutex_exit(&c->c_lock);
			goto again;
		}

		if (!IE_STAT_READY(un->un_flags)) {
			if (iddebug >= 0) {
				cmn_err(CE_CONT,
				    "id%d: idstart, unit not ready (0x%x)\n",
				    ddi_get_instance(un->un_dip), un->un_flags);
			}

			/*
			 * If the unit isn't "ready",
			 * punt on doing a command for
			 * it.
			 *
			 * Is this the right thing to do?
			 */
			mutex_enter(&un->un_qlock);
			if (un->un_bufs.av_forw == NULL)
				un->un_bufs.av_back = bp;
			bp->av_forw = un->un_bufs.av_forw;
			un->un_bufs.av_forw = bp;
			mutex_exit(&un->un_qlock);
			mutex_exit(&c->c_lock);
			/*
			 * We could get stuck in an infinite loop
			 * if we went back to the top to try and
			 * start things for another facility, so
			 * we'll punt on starting altogether, and
			 * pray that someone else will start us.
			 */
			return (1);
		}
		mutex_exit(&c->c_lock);
	}

	/*
	 * Get an ipiq to work with. If we run out of resources,
	 * leave things so that we restart when resources might
	 * be available.
	 *
	 * We used to do this by either being reentrant and calling
	 * idstart directly after sticking the buffer back on the
	 * wait queue. However, there is a slight race condition
	 * about having the callback occur prior to sticking the
	 * buffer back on the wait queue without a lock being held
	 * throughout the resource call. Therefore, the way we'll
	 * do this is to first make an attempt to get a resources
	 * without any waiting, period. If that fails, we acquire
	 * the slave lock and try again with callback function
	 * registered, so that if we fail again, we can stick the
	 * buffer back on the queue without the race condition of
	 * the callback occurring. This use of c_lock is awkward
	 * as a serialization for the callback, but this really
	 * reduces the possible number of callback frames that
	 * ddi_setcallback will then try and allocate, which will
	 * avoid a stupid bug for failed kmem_fast_allocs.
	 */

	err = IDU_ALLOC(un, bp, DDI_DMA_DONTWAIT, 0, &q);

	if (err < 0) {
		mutex_enter(&c->c_lock);
		err = IDU_ALLOC(un, bp, idrestart, c, &q);
		if (err < 0) {
			mutex_enter(&un->un_qlock);
			/*
			 * We ran out of resources. Stick the buffer back
			 * on the head of the queue and wait to be called
			 * back later. Since we really didn't start the
			 * command and hadn't marked it's exit from the
			 * wait queue, we don't have to call the kstat
			 * routine to mark it's "reentrance" back to the
			 * wait queue.
			 */
			if (un->un_bufs.av_forw == NULL)
				un->un_bufs.av_back = bp;
			bp->av_forw = un->un_bufs.av_forw;
			un->un_bufs.av_forw = bp;
			mutex_exit(&un->un_qlock);
			mutex_exit(&c->c_lock);
			return (0);
		}
		mutex_exit(&c->c_lock);
	}

	if (err != 0) {
		bp->b_flags |= B_ERROR;
		bp->b_error = err;
		biodone(bp);
		if (un->un_stats) {
			mutex_enter(&un->un_slock);
			kstat_waitq_exit(IOSP);
			mutex_exit(&un->un_slock);
		}
		goto again;
	}

	/*
	 * The absolute block number is in b_resid and the number
	 * of blocks to transfer is in av_back. Pull them out
	 * (now that we have an ipiq to stick them into), and
	 * re-set resid to something appropriate.
	 */

	blkno = (daddr_t) bp->b_resid;
	nblks = (int) bp->av_back;
	bp->b_resid = bp->b_bcount - (nblks << un->un_log_bshift);

	/*
	 * Build the IPI command packet.
	 */
	q->q_private[Q_ERRBLK] = (u_long) blkno;
	q->q_private[Q_BUF] = (u_long) bp;
	q->q_time = id_timeout;
	id_build_rdwr(q, blkno, nblks,
	    (bp->b_flags & B_READ)? IP_READ: IP_WRITE);

	/*
	 * If measuring stats, mark exit from
	 * wait queue and entrance into run queue.
	 */
	if (un->un_stats) {
		mutex_enter(&un->un_slock);
		kstat_waitq_to_runq(IOSP);
		mutex_exit(&un->un_slock);
	}

	/*
	 * Send command.
	 */
	IDU_START(un, q);

	/*
	 * Go back for more.
	 */
	goto again;
}

/*
 * Section 9.0:	I/O Completion Routines
 */

/*
 * IPI Completion handler
 */
static void
idintr(register ipiq_t *q)
{
	register id_unit_t *un;
	register id_ctlr_t *c;
	register struct buf *bp;
	int qretry, started, done, slv, fac, flags;

	/*
	 * This could be optimized better.
	 */

	/*
	 * If this is an asynchronous response completion,
	 * peel off now and deal with this separately.
	 */

	if (q->q_result == IP_ASYNC) {
		idasync(q);
		return;
	}
	slv = IPI_SLAVE(q->q_addr);
	if (slv <= IPI_NSLAVE)
		c = id_ctlr[slv];
	else
		c = (id_ctlr_t *) 0;

	if (c == (id_ctlr_t *) 0) {
		cmn_err(CE_WARN, "idintr: bogus completion, null controller");
		return;
	}

	/*
	 * Check for doing slave recovery.
	 * If we are, peel off and deal with
	 * the result.
	 *
	 * The only time that this should
	 * be happening is after a slave
	 * reset or a missing request.
	 */

	if (q == c->c_rqp) {
		mutex_enter(&c->c_lock);
		id_recover_intr(c);
		mutex_exit(&c->c_lock);
		return;
	}

	/*
	 * Otherwise, find the facility, if any,
	 * for which this command applies to.
	 */

	un = (id_unit_t *) NULL;
	fac = IPI_FAC(q->q_addr);
	if (fac != IPI_NO_ADDR) {
		if (fac > ID_NUNIT || c->c_un[fac] == (id_unit_t *) NULL) {
			cmn_err(CE_WARN,
			    "%s%d: completion for unknown facility %d",
			    c->c_name, c->c_instance, fac);
		} else {
			un = c->c_un[fac];
		}
	}

	bp = (struct buf *) q->q_private[Q_BUF];
	started = done = 0;

	switch (q->q_result) {
	case IP_SUCCESS:
		done = 1;
		break;

	case IP_ERROR:
	case IP_COMPLETE:
		mutex_enter(&c->c_lock);
		flags = id_error_parse(q, id_errtab, 0);
		if (flags & IE_REQUEUE) {
			if (bp && un) {
				IDC_RELSE(c, q);
				mutex_enter(&un->un_qlock);
				bp->av_forw = un->un_bufs.av_forw;
				if (bp->av_forw == (struct buf *) 0)
					un->un_bufs.av_back = bp;
				un->un_bufs.av_forw = bp;
				mutex_exit(&un->un_qlock);
				if (un->un_stats) {
					mutex_enter(&un->un_slock);
					kstat_runq_back_to_waitq(IOSP);
					mutex_exit(&un->un_slock);
				}
			} else {
				idqretry(c, q);
				started = 1;
			}
		} else if (flags & IE_RETRY) {
			idqretry(c, q);
			started = 1;
		} else if (flags & IE_CMD) {
			if (bp) {
				bp->b_flags |= B_ERROR;
				bp->b_error = EIO;
			}
			done = 1;
		} else {
			if ((q->q_flag & IP_DIAGNOSE) == 0)
				q->q_result = IP_SUCCESS;
			done = 1;
		}

		/*
		 * if DIAGNOSE flag is set, fill in the hdk_diag structure
		 * in the un. IP_DIAGNOSE is set based on HDK_DIAGNOSE
		 * flag passed down in HDKIOCSCMD ioctl. This is primarily
		 * used by format utility (surface analysis tests) to detect
		 * and repair medium errors/bad blocks.
		 */
		if ((flags & (IE_CORR|IE_UNCORR|IE_CMD)) &&
		    (q->q_flag & IP_DIAGNOSE)) {
			char *message;
			un->un_diag.hdkd_errsect = q->q_private[Q_ERRBLK];
			if (flags & IE_CORR) {
				message = "corrected";
				un->un_diag.hdkd_severe = IDE_SEV(IDE_CORR);
				un->un_diag.hdkd_errno = IDE_ERRNO(IDE_CORR);
			} else if (flags & IE_UNCORR) {
				message = "uncorrected";
				un->un_diag.hdkd_severe = IDE_SEV(IDE_UNCORR);
				un->un_diag.hdkd_errno = IDE_ERRNO(IDE_UNCORR);
			} else {
				message = "fatal";
				un->un_diag.hdkd_severe = IDE_SEV(IDE_FATAL);
				un->un_diag.hdkd_errno = IDE_ERRNO(IDE_FATAL);
			}
			if ((q->q_flag & IP_SILENT) == 0) {
				cmn_err(CE_WARN, "id%d: %s error at sector"
				    " %d\n", ddi_get_instance(un->un_dip),
					message, un->un_diag.hdkd_errsect);
			}
		}

		mutex_exit(&c->c_lock);
		break;


	case IP_MISSING:

		mutex_enter(&c->c_lock);
		id_req_missing(c, q);
		mutex_exit(&c->c_lock);
		started = 1;
		break;

	case IP_RESET:

		mutex_enter(&c->c_lock);
		id_req_reset(c, q);
		mutex_exit(&c->c_lock);
		started = 1;
		break;

	default:
		if (bp) {
			if (q->q_result == IP_OFFLINE) {
				bp->b_error = ENXIO;
			} else {
				bp->b_error = EIO;
			}
			bp->b_flags |= B_ERROR;
		}
		done = 1;
		break;
	}

	if (done) {
		if (un && bp && bp != un->un_sbufp)
			IDC_RELSE(c, q);
		if (bp) {
			u_long n_done = bp->b_bcount - bp->b_resid;
			int b_flags = bp->b_flags;
			biodone(bp);
			if (un && un->un_stats && bp != un->un_sbufp) {
				mutex_enter(&un->un_slock);
				if (b_flags & B_READ) {
					IOSP->reads++;
					IOSP->nread += n_done;
				} else {
					IOSP->writes++;
					IOSP->nwritten += n_done;
				}
				kstat_runq_exit(IOSP);
				mutex_exit(&un->un_slock);
			}
		} else {
			/*
			 * There needs to be another synchronization
			 * mechanism here for non-bp IPI commands.
			 * Right now there isn't one, so this is a
			 * panic. In the future, say for multiport,
			 * we can use, say, the condition variable
			 * in the controller structure.
			 */
			cmn_err(CE_PANIC, "idintr: no bp structure");
			/* NOTREACHED */
		}
	}

	if (!started) {
		(void) idstart(c);
	}
}

/*
 * Asynchronous interrupt handler
 */
static void
idasync(register ipiq_t *q)	/* interrupt info */
{
	register unsigned int fac, slave;
	register id_ctlr_t *c;
	register id_unit_t *un;
	struct icparg icp;

	slave = IPI_SLAVE(q->q_addr);
	c = id_ctlr[slave];
	if (slave >= IPI_NSLAVE || !c) {
		icp.arg = (void *) q->q_resp;
		icp.msg = "async response for unknown slave";
		IDC_CONTROL(c, IPI_CTRL_PRINTRESP, &icp, 0);
	} else if ((fac = q->q_resp->hdr_facility) != IPI_NO_ADDR) {
		if (fac < ID_NUNIT && (un = c->c_un[fac])) {
			mutex_enter(&c->c_lock);
			(void) id_error_parse(q, id_errtab, 0);
			mutex_exit(&c->c_lock);
		} else {
			/*
			 * XXXX: This point is where facilities powered
			 * XXXX: off at probe time show up when they get
			 * XXXX: powered on.
			 */
			icp.arg = (void *) q->q_resp;
			icp.msg = "async response for unknown facility";
			IDC_CONTROL(c, IPI_CTRL_PRINTRESP, &icp, 0);
		}
	} else {
		mutex_enter(&c->c_lock);
		(void) id_error_parse(q, id_errtab, 0);
		mutex_exit(&c->c_lock);
	}
}

/*
 * Section 10.0: Error recovery Definitions
 *
 * In order to cut down on the noise and verbiage in both
 * idvar.h and elsewhere in this driver, all the definitions
 * routines pertaining to slave error recovery are contained
 * here.
 */

/*
 * Recovery states.
 */
#define	IDR_NORMAL	0	/* not in recovery mode */
/*
 * The first eight states are for missing interrupt recovery
 */
#define	IDR_TST_SLAVE	1	/* testing slave */
#define	IDR_RST_SLAVE	2	/* resetting slave */
#define	IDR_STAT_SLAVE	3	/* report status of slave */
#define	IDR_ID_SLAVE	4	/* get vendor ID of slave */
#define	IDR_ATTR_SLAVE	5	/* get attributes of slave */
#define	IDR_SATTR_SLV	6	/* set attributes of slave */
#define	IDR_IDLE_SLAVE	7	/* wait for slave to go idle */
#define	IDR_DUMP_SLAVE	8	/* dump controller firmware to disk */

/*
 * The next four states are for slave (re)initialization
 */
#define	IDR_RST_SLV1	9	/* reset slave */
#define	IDR_STAT_SLV1	10	/* report addressee status */
#define	IDR_ID_SLV1	11	/* get vendor ID from slave */
#define	IDR_ATTR_SLV1	12	/* get slave attributes */
#define	IDR_SATTR_SLV2	13	/* set slave attributes ((re)initialization) */
/*
 * Retry or complete failure
 */
#define	IDR_RETRY	14	/* re-issuing commands */
#define	IDR_REC_FAIL	15	/* recovery failed */


/*
 * Section 10.1:	Slave Error Recovery Data
 */
/*
 * State transition table for recovery.
 */
static struct id_next_state {
	char	ns_success;	/* next state if command successful */
	char	ns_fail;	/* next state if command failed except... */
	char	ns_fail2;	/* next state if ns_fail path already taken */
	char	*ns_msg;	/* message */
} id_next_state[] = {
/*    present		next state	next state 	next state */
/*    state		on success	on failure 	on 2nd fail */
/*  0 IDR_NORMAL */	IDR_NORMAL,	IDR_NORMAL,	IDR_NORMAL,
	"recovery complete",
/*
 * Recovery from missing interrupt.
 */

/*  1 IDR_TST_SLAVE */	IDR_IDLE_SLAVE,	IDR_RST_SLAVE,	IDR_REC_FAIL,
	"test slave",

/*  2 IDR_RST_SLAVE */	IDR_STAT_SLAVE,	IDR_RST_SLAVE,	IDR_REC_FAIL,
	"reset slave",

/*  3 IDR_STAT_SLAVE */	IDR_ID_SLAVE,	IDR_RST_SLAVE,	IDR_REC_FAIL,
	"stat ctlr",

/*  4 IDR_ID_SLAVE */	IDR_ATTR_SLAVE,	IDR_RST_SLAVE,	IDR_REC_FAIL,
	"id slave",

/*  5 IDR_ATTR_SLAVE */	IDR_SATTR_SLV,	IDR_RST_SLAVE,	IDR_REC_FAIL,
	"slave attr",

/*  6 IDR_SATTR_SLV */	IDR_RETRY,	IDR_REC_FAIL,	IDR_REC_FAIL,
	"set slave attr",

/*  7 IDR_IDLE_SLAVE */	IDR_NORMAL,	IDR_DUMP_SLAVE,	IDR_RST_SLAVE,
	"idle slave",

/*  8 IDR_DUMP_SLAVE */	IDR_RST_SLAVE,	IDR_RST_SLAVE,	IDR_RST_SLAVE,
	"dump slave",

/*
 * Controller (Re)initialization.
 *
 * We enter at IDR_STAT_SLV1 if it is the initial run at this,
 * otherwise we're in error recovery mode.
 */

/*  9 IDR_RST_SLV1 */	IDR_STAT_SLV1,	IDR_REC_FAIL,	IDR_REC_FAIL,
	"reset slave",

/* 10 IDR_STAT_SLV1 */	IDR_ID_SLV1,	IDR_REC_FAIL,	IDR_REC_FAIL,
	"stat slave(1)",

/* 11 IDR_ID_SLV1 */	IDR_ATTR_SLV1,	IDR_REC_FAIL,	IDR_REC_FAIL,
	"id slave(1)",

/* 12 IDR_ATTR_SLV1 */	IDR_SATTR_SLV2, IDR_REC_FAIL,	IDR_REC_FAIL,
	"get attr slave(1)",

/* 13 IDR_SATTR_SLV2 */ IDR_NORMAL,	IDR_REC_FAIL,	IDR_REC_FAIL,
	"set slave attr(2)",

/*
 * Retry or complete failure
 */

/* 14 IDR_RETRY */	IDR_NORMAL,	IDR_RST_SLAVE,	IDR_REC_FAIL,
	"retry",

/* 15 IDR_REC_FAIL */	IDR_REC_FAIL,	IDR_REC_FAIL,	IDR_REC_FAIL,
	"recovery failed",

};

/*
 * Recovery control structure.
 *
 * Locking provided by the lock in the buddy id_ctlr_t.
 */

struct rstate {
	char		rec_state;	/* recovery states */
	char		rec_substate;	/* recovery substates */
	short		rec_data;	/* data for current recovery state */
	long		rec_history;	/* recovery history: a bit per state */
	long		rec_flags;	/* flags for recovery requests */
} rstates[IPI_NSLAVE];

/*
 * Section 10.2:	Slave Errror Recovery Routines
 */

static void
id_init_slave(id_ctlr_t *c)
{
	struct rstate *rp = &rstates[IPI_SLAVE(c->c_ipi_addr)];

	IDC_CONTROL(c, IPI_CTRL_LIMIT_SQ, (void *) 1, 0);

	mutex_enter(&c->c_lock);
	c->c_flags = IE_INIT_STAT|IE_RECOVER;
	c->c_fac_flags = (1 << IPI_NFAC) - 1;
	c->c_ctype = DKC_UNKNOWN;

	rp->rec_state = IDR_STAT_SLV1;
	rp->rec_substate = 0;
	rp->rec_history = 0;
	rp->rec_flags = IP_NO_RETRY|IP_SILENT;

	id_recover(c);
	while (c->c_flags & IE_RECOVER) {
		c->c_flags |= ID_RECOVER_WAIT;
		cv_wait(&c->c_cv, &c->c_lock);
	}
	mutex_exit(&c->c_lock);
}

/*
 * Missing interrupt recovery.
 */

static void
id_req_missing(id_ctlr_t *c, register ipiq_t *q)
{
	/*
	 * If recovery is already in progress, return.
	 */
	if (c->c_flags & IE_RECOVER) {
		if (iddebug) {
			id_printerr(q, CE_CONT, 0,
			    "missing interrupt - recovery in progress");
		}
	} else {
		struct rstate *rp = &rstates[IPI_SLAVE(c->c_ipi_addr)];

		if (iddebug) {
			id_printerr(q, CE_CONT, 0,
			    "missing interrupt - attempting recovery");
		}

		/*
		 * Setup slave in recovery mode.
		 */
		c->c_flags |= IE_RECOVER;
		if (iddebug > 1)
			c->c_flags |= IE_TRACE_RECOV;
		else
			c->c_flags &= ~IE_TRACE_RECOV;
		rp->rec_state = IDR_TST_SLAVE;
		rp->rec_substate = 0;
		rp->rec_history = 0;
		rp->rec_flags = IP_NO_RETRY;
		id_recover(c);
	}
	/*
	 * This will put it on a queue for eventual retry.
	 */
	idqretry(c, q);
}

static void
id_req_reset(id_ctlr_t *c, register ipiq_t *q)
{

	if (iddebug) {
		id_printerr(q, CE_CONT, 0,
		    "request destroyed by reset - will be retried");
	}

	if ((c->c_flags & IE_RECOVER) == 0) {
		struct rstate *rp = &rstates[IPI_SLAVE(c->c_ipi_addr)];
		/*
		 * Setup slave in recovery mode.
		 */
		c->c_flags |= IE_RECOVER;
		if (iddebug > 1)
			c->c_flags |= IE_TRACE_RECOV;
		else
			c->c_flags &= ~IE_TRACE_RECOV;
		rp->rec_state = IDR_RST_SLV1;
		rp->rec_substate = 0;
		rp->rec_history = 0;
		rp->rec_flags = IP_NO_RETRY;
		id_recover(c);
	}
	idqretry(c, q);
}

/*
 * Wrapper for calling id_recover through timeout().
 */

static void
id_recover_cbt(caddr_t arg)
{
	id_ctlr_t *c = (id_ctlr_t *) arg;
	mutex_enter(&c->c_lock);
	id_recover(c);
	mutex_exit(&c->c_lock);
}

/*
 * This routine does the real work of error recovery
 * and initialization for a slave.
 */

static void
id_recover(register id_ctlr_t *c)
{
	id_unit_t *un;
	register ipiq_t	*q;
	struct rstate *rp;
	rtable_t *rtp;
	int state, next_state, run_cmd, fac;

	q = c->c_rqp;
	rp = &rstates[IPI_SLAVE(c->c_ipi_addr)];

again:
	state = rp->rec_state;
	run_cmd = 0;
	next_state = id_next_state[state].ns_success;
	if (c->c_flags & IE_TRACE_RECOV) {
		cmn_err(CE_CONT, "?%s%d: id_recover (%d) %s\n", c->c_name,
		    c->c_instance, state, id_next_state[state].ns_msg);
	}

	switch (state) {
	case IDR_TST_SLAVE:
		q->q_cmd->hdr_opcode = IP_NOP;
		q->q_cmd->hdr_mods = 0;
		q->q_cmd->hdr_pktlen = IPI_HDRLEN;
		run_cmd = ID_REC_TIMEOUT;
		break;

	case IDR_RST_SLAVE:
	case IDR_RST_SLV1:
		/*
		 * We have to drop c_lock when we call this because
		 * this may blow away queued up commands at the
		 * transport level which will then get dropkicked
		 * back our direction. We depend upon c_flags being
		 * set that we are in the middle of error recovery
		 * in order to ensure that the right thing happens.
		 */
		mutex_exit(&c->c_lock);
		IDC_CONTROL(c, IPI_CTRL_RESET_SLAVE, 0, 0);
		mutex_enter(&c->c_lock);
		break;

	case IDR_STAT_SLAVE:
	case IDR_STAT_SLV1:
		q->q_cmd->hdr_opcode = IP_REPORT_STAT;
		q->q_cmd->hdr_mods = IP_OM_CONDITION;
		q->q_cmd->hdr_pktlen = IPI_HDRLEN;
		run_cmd = ID_REC_TIMEOUT;
		break;

	case IDR_ID_SLAVE:
	case IDR_ID_SLV1:
		c->c_ctype = DKC_UNKNOWN;
		id_build_attr_cmd(q, &vendor_id_table[rp->rec_substate]);
		run_cmd = ID_REC_TIMEOUT;
		break;

	case IDR_ATTR_SLAVE:
	case IDR_ATTR_SLV1:
		rtp = &get_conf_table[rp->rec_substate];
		if (rtp->rt_parm_id == ATTR_SLVCNF_BIT &&
		    (c->c_caplim & ID_NO_RECONF)) {
			rtp++;
			rp->rec_substate++;
			/*
			 * If we've hit the end of the table,
			 * go on to the next state.
			 */
			if (rtp->rt_parm_id == 0) {
				break;
			}
		}
		id_build_attr_cmd(q, rtp);
		run_cmd = ID_REC_TIMEOUT;
		break;

	case IDR_SATTR_SLV:
	case IDR_SATTR_SLV2:
		if ((c->c_caplim & ID_NO_RECONF) == 0) {
			rtp = &set_conf_table[rp->rec_substate];
			id_build_set_attr_cmd(q, rtp, (void *) c);
			run_cmd = ID_REC_TIMEOUT;
		}
		break;

	case IDR_RETRY:			/* re-issue commands */
		/*
		 * This is a null state, since we should be
		 * transitioning directly to IDR_NORMAL.
		 */
		break;

	case IDR_IDLE_SLAVE:		/* wait for cmds to finish */
	{
		int nact;
		/*
		 * Wait in this state in case the missing interrupt was
		 * because the controller was just busy with other commands.
		 * No new commands are sent in the hope that things can get
		 * cleaned up.
		 *
		 * rp->rec_substate is time (seconds) in this state.
		 */

		IDC_CONTROL(c, IPI_CTRL_NACTSLV, 0, &nact);
		if (nact == 0) {
			break;
		}

		/*
		 * Have we waited long enough?
		 */
		if (rp->rec_substate++ >= ID_IDLE_TIME) {
			next_state = id_next_state[state].ns_fail;
			break;
		}
		(void) timeout(id_recover_cbt, (caddr_t)c,  id_hz);
		break;
	}

	case IDR_DUMP_SLAVE:		/* take firmware dump */
	{
		u_char *cp;
		if (c->c_ctype != DKC_SUN_IPI1) {
			rp->rec_history |= 1 << state;
			rp->rec_state = next_state;
			goto again;
		}
		cmn_err(CE_NOTE, "%s%d: requesting controller dump",
		    c->c_name, c->c_instance);

		q->q_cmd->hdr_opcode = IP_DIAG_CTL;
		q->q_cmd->hdr_mods = 0;
		cp = (u_char *)(q->q_cmd + 1);	/* past header */
		*cp++ = sizeof (struct ipi_diag_num)+1;	/* parm + id */
		*cp++ = IPI_DIAG_NUM;		/* parameter ID */
		((struct ipi_diag_num *)cp)->dn_diag_num = IPDN_DUMP;
		q->q_cmd->hdr_pktlen = cp + sizeof (struct ipi_diag_num) -
		    (u_char *)q->q_cmd - sizeof (u_short);
		run_cmd = ID_DUMP_TIMEOUT;
		break;
	}
	case IDR_NORMAL:		/* recovery complete */

		/*
		 * If a re-initialization became necessary during
		 * recovery, switch over to the right state to do it.
		 */
		if (c->c_flags & IE_RE_INIT) {
			c->c_flags &= ~IE_RE_INIT;
			rp->rec_history = 0;
			rp->rec_state = IDR_RST_SLV1;
			rp->rec_substate = 0;
			goto again;
		}

		if ((c->c_flags & ID_C_PRESENT) && iddebug != 0) {
			cmn_err(CE_NOTE, "%s%d: Recovery complete.",
			    c->c_name, c->c_instance);
		}

		/*
		 * If there is no seek algorithm, allow a smaller
		 * number of commands to be sent to the controller
		 * so that the rest can be sorted in the host.
		 */
		if ((c->c_caplim & ID_SEEK_ALG) == 0) {
			IDC_CONTROL(c, IPI_CTRL_LIMIT_SQ, (void *) 4, 0);
		} else if (c->c_caplim & ID_LIMIT_CMDS) {
			if (id_dumb_limit > 16)
				id_dumb_limit--;
			IDC_CONTROL(c, IPI_CTRL_LIMIT_SQ,
			    (void *) id_dumb_limit, 0);
		}
		c->c_flags &= ~IE_RECOVER;
		c->c_flags |= ID_C_PRESENT;

		run_cmd = -1;
		break;

	case IDR_REC_FAIL:		/* recovery failed miserably */
		/*
		 * Mark controller unusable.
		 */
		if (c->c_flags & ID_C_PRESENT) {
			cmn_err(CE_WARN,
			    "%s%d: Slave Recovery failed. Slave marked "
			    "unavailable.", c->c_name, c->c_instance);
			c->c_flags ^= ID_C_PRESENT;
		}
		c->c_flags &= ~(IE_RECOVER | IE_PAV);
		run_cmd = -1;
		break;


	default:
		cmn_err(CE_PANIC, "%s%d: id_recover: bad state %d",
		    c->c_name, c->c_instance, rp->rec_state);
		/* NOTREACHED */
		break;
	}

	if (run_cmd > 0) {
		q->q_time = run_cmd;
		q->q_flag |= rp->rec_flags|IP_PRIORITY_CMD;
		IDC_START(c, q);
		return;
	} else if (run_cmd == 0) {
		rp->rec_history |= 1 << state;
		rp->rec_state = next_state;
		rp->rec_substate = 0;
		goto again;
	}
	if (c->c_flags & ID_RECOVER_WAIT) {
		c->c_flags ^= ID_RECOVER_WAIT;
		cv_broadcast(&c->c_cv);
	}

	q = c->c_retry_q;
	c->c_retry_q = NULL;

	/*
	 * Restart the waiting commands
	 */
	if (IE_STAT_READY(c->c_flags)) {
		mutex_exit(&c->c_lock);
		while (q) {
			ipiq_t *nq = q->q_next;
			IDC_START(c, q);
			q = nq;
		}
		/*
		 * And, just for grins, try and restart things
		 */
		(void) idstart(c);
		mutex_enter(&c->c_lock);
		return;
	}

	/*
	 * Blow away waiting commands awaiting retry.
	 */
	while (q) {
		struct buf *bp;
		ipiq_t *nq;

		nq = q->q_next;

		fac = IPI_FAC(q->q_addr);
		if (fac != IPI_NO_ADDR && fac < ID_NUNIT)
			un = c->c_un[fac];
		else
			un = (id_unit_t *) NULL;
		bp = (struct buf *) q->q_private[Q_BUF];

		if (bp) {
			bp->b_flags |= B_ERROR;
			bp->b_error = EIO;
			biodone(bp);
			if (un && un->un_stats && bp != un->un_sbufp) {
				mutex_enter(&un->un_slock);
				kstat_runq_exit(IOSP);
				mutex_exit(&un->un_slock);
			}
			if (un && bp != un->un_sbufp) {
				IDC_RELSE(c, q);
			}
		} else {
			/*
			 * XXXX: See above in idintr.
			 */
			cmn_err(CE_PANIC, "id_recover: no bp structure");
			/* NOTREACHED */
		}
		q = nq;
	}
	mutex_exit(&c->c_lock);
	(void) idstart(c);
	mutex_enter(&c->c_lock);
}


/*
 * Interrupt handler for slave recovery actions.
 * Called with c_lock held.
 */

static void
id_recover_intr(id_ctlr_t *c)
{
	register ipiq_t *q = c->c_rqp;
	struct rstate *rp;
	struct id_next_state *nsp;
	int state, stay_in_state;

	stay_in_state = 0;
	rp = &rstates[IPI_SLAVE(c->c_ipi_addr)];
	state = rp->rec_state;

	/*
	 * Explicitly check IP_ERROR/IP_COMPLETE cases here.
	 */
	if (q->q_result == IP_ERROR || q->q_result == IP_COMPLETE) {
		if (q->q_resp) {
			int flags = id_error_parse(q, id_errtab, IE_MSG);
			/*
			 * XXX: Should check against IE_RETRY|IE_REQUEUE
			 */
			if ((flags & IE_CMD) == 0)	/* non fatal */
				q->q_result = IP_SUCCESS;
		}
	}

	switch (state) {
	case IDR_STAT_SLAVE:
	case IDR_STAT_SLV1:
		/*
		 * Evaluate the response from Report Addressee Status.
		 */
		if (q->q_result == IP_SUCCESS) {
			(void) id_error_parse(q, id_condtab, IE_INIT_STAT);
		} else if (q->q_result == IP_MISSING) {
			break;
		}
		/*
		 * Check flags.  If the controller isn't ready,
		 * consider the status operation a failure for the
		 * purposes of selecting the next state. Ignore the
		 * IE_RECOVER and IE_RE_INIT flags for this test.
		 */
		if (!IE_STAT_READY(c->c_flags & ~(IE_RECOVER | IE_RE_INIT))) {
			q->q_result = IP_ERROR;
		}
		c->c_flags &= ~IE_INIT_STAT;
		break;

	case IDR_ID_SLAVE:
	case IDR_ID_SLV1:
		if (q->q_result == IP_SUCCESS) {
			struct icprarg r;
			r.q = q;
			r.rt = vendor_id_table;
			r.a = (caddr_t) c;
			IDC_CONTROL(c, IPI_CTRL_PARSERESP, (void *) &r, 0);
			if (c->c_ctype == DKC_UNKNOWN) {
				q->q_result = IP_ERROR;
			}
		}
		break;

	case IDR_ATTR_SLAVE:
	case IDR_ATTR_SLV1:
		if (q->q_result == IP_SUCCESS) {
			struct icprarg r;
			r.q = q;
			r.rt = &get_conf_table[rp->rec_substate];
			r.a = (caddr_t) c;
			IDC_CONTROL(c, IPI_CTRL_PARSERESP, &r, 0);
			rp->rec_substate++;
			/*
			 * Stay in this state if not done with table.
			 */
			stay_in_state =
			    get_conf_table[rp->rec_substate].rt_parm_id;
			if (stay_in_state == 0)
				c->c_flags &= ~IE_RE_INIT;
		}
		break;

	case IDR_SATTR_SLV:
	case IDR_SATTR_SLV2:
		if (q->q_result == IP_SUCCESS) {
			rp->rec_substate++;
			stay_in_state =
			    set_conf_table[rp->rec_substate].rt_parm_id;
		}
		break;

	}

	nsp = &id_next_state[state];
	if (!stay_in_state) {
		if (q->q_result == IP_SUCCESS)
			rp->rec_state = nsp->ns_success;
		else if (rp->rec_history & (1 << state))
			rp->rec_state = nsp->ns_fail2;
		else
			rp->rec_state = nsp->ns_fail;
		rp->rec_history |= (1 << state);
		rp->rec_substate = 0;
		rp->rec_data = 0;
	}

	if (c->c_flags & IE_TRACE_RECOV) {
		cmn_err(CE_CONT, "?%s%d: id_recover_intr %d %s result %d\n",
		    c->c_name, c->c_instance, state, nsp->ns_msg, q->q_result);
	}
	id_recover(c);
}


/*
 * Section 11.0:	Error Parsing Routines
 */

static int
id_error(ipiq_t	*q, int code, int flags, char *msg, u_char *parm)
{
	id_ctlr_t *c;
	id_unit_t *un;
	int new_flags;
	u_int	old_stat, dummy, change_mask, *fp;


	c = id_ctlr[IPI_SLAVE(q->q_addr)];
	if (IPI_FAC(q->q_addr) != IPI_NO_ADDR)
		un = c->c_un[IPI_FAC(q->q_addr)];
	else
		un = (id_unit_t *) 0;

	new_flags = 0;
	if ((flags & IE_FAC) && un == NULL) {
		cmn_err(CE_CONT, "?%s%d: facility status for unknown unit\n",
		    c->c_name, c->c_instance);
		new_flags |= IE_PRINT;
	}

	switch (code) {
	case 0:			/* nothing */
		break;
	case IE_RESP_EXTENT:	/* response extent parameter */
	{
		struct respx_parm px;
		bcopy((caddr_t)(parm + 2), (caddr_t)&px,
			sizeof (struct respx_parm));
		new_flags |= id_respx(q, 0, &px, 0,
		    (struct buf *) q->q_private[Q_BUF]);
		break;
	}
	case IE_MESSAGE:	/* for microcode messages */
	{
		struct msg_ucode_parm *p;
		char	m, *cp;
		int i;

		p = (struct msg_ucode_parm *) &parm[2];
		if (p->mu_flags & IPMU_FAIL_MSG) {
			cmn_err(CE_WARN, "%s%d: slave failure, FRU 0x%x",
			    c->c_name, c->c_instance, p->mu_u.mu_m.mu_fru);
			new_flags = IE_CMD;
		} else {
			cmn_err(CE_CONT, "?%s%d: ctlr message: '",
			    c->c_name, c->c_instance);

			cp = p->mu_u.mu_m.mu_msg;	/* first char of msg */
			i = parm[0] + 1 - (cp - (char *)parm);	/* msg len */
			while (i-- > 0) {
				m = *cp++;
				cmn_err(CE_CONT, "?%c", m == '\n' ? ' ' : m);
			}
			cmn_err(CE_CONT, "?'\n");
		}
		break;
	}
	case IE_STAT_OFF:	/* facility or slave went offline */
	case IE_STAT_ON:	/* facility or slave went online */

		change_mask = (flags & IE_STAT_MASK);
		if (flags & IE_FAC) {
			if (un) {
				fp = &un->un_flags;
			} else {
				fp = &dummy;
				dummy = 0;
			}
		} else {
			fp = &c->c_flags;
		}
		old_stat = *fp;
		if (code == IE_STAT_OFF)
			*fp &= ~change_mask;
		else
			*fp |= change_mask;
		/*
		 * Print a message if the status has changed and a message
		 * hasn't been already printed due to the flags settings.
		 * However, don't print this message if the unit/ctlr flags
		 * or the passed-in flags indicate that this is the
		 * initial status for the unit/ctlr.
		 */
		if (!((flags | old_stat) & IE_INIT_STAT) &&
		    old_stat != *fp && !(flags & (IE_MSG | IE_PRINT))) {
			id_printerr(q, CE_NOTE, ID_EP_UN, msg);
		}
		break;
	case IE_IML:		/* reload slave */
		id_printerr(q, CE_WARN, 0, "slave IML not supported");
		new_flags |= IE_RETRY;			/* retry request */
		break;
	case IE_RESET:		/* reset slave */
		id_printerr(q, CE_WARN, 0, "slave reset not supported");
		new_flags |= IE_RETRY;			/* retry request */
		break;
	case IE_QUEUE_FULL:	/* buffer limits exceeded */
		id_printerr(q, CE_WARN, 0, "full queue: not handled");
		new_flags |= IE_RETRY;			/* retry request */
		break;
	case IE_READ_LOG:	/* log full */
		id_printerr(q, CE_WARN, 0, "log read not supported");
		new_flags |= IE_RETRY;			/* retry request */
		break;
	case IE_ABORTED:	/* command aborted */
		id_printerr(q, CE_WARN, 0, "aborted command not supported");
		new_flags |= IE_DUMP | IE_CMD;		/* dump cmd/resp */
		break;
	case IE_MCH:		/* handle machine exception */
		/*
		 * XXX Do this based on VME bus faults only?
		 */
		id_printerr(q, CE_NOTE, 0, msg);
		new_flags |= IE_REQUEUE;
		break;
	default:
		break;
	}
	return (new_flags);
}

/*
 * Handle response extent parameter from read/write defect list,
 * or possibly a normal read/write command.
 */
/* ARGSUSED */
static int
id_respx(ipiq_t *q, int id, struct respx_parm *rp, int len, struct buf *bp)
{
	register struct ipi3header *ip = (struct ipi3header *)q->q_cmd;
	int rval, resid, fac;
	id_unit_t *un;
	id_ctlr_t *c;

	c = id_ctlr[IPI_SLAVE(q->q_addr)];
	fac = IPI_FAC(q->q_addr);
	if (fac < ID_NUNIT)
		un = c->c_un[fac];
	else
		un = (id_unit_t *) NULL;

	rval = resid = 0;

	if (bp != NULL) {
		q->q_private[Q_ERRBLK] = rp->rx_addr;
		if (q->q_flag & IP_BYTE_EXT) {
			resid = rp->rx_resid;
		} else if (un != NULL) {
			resid = rp->rx_resid << un->un_log_bshift;
		}

		if (resid) {
			if (ip->hdr_opcode == IP_READ ||
			    ip->hdr_opcode == IP_WRITE) {
				rval = IE_RETRY;
			} else {
				bp->b_resid = resid;
			}
		}
	}
	return (rval);
}


/*
 * Handle parameter length parameter from read/write defect list.
 */
/* ARGSUSED */
static int
id_deflist_parmlen(ipiq_t *q, int id, struct parmlen_parm *rp,
	int len, struct buf *bp)
{
	bp->b_resid = bp->b_bcount - rp->parmlen;
	return (0);
}


/*
 * Walk the table for each parameter in the response given.
 *
 * In general, the whole object of this exercise is to take the
 * initial conditions as reflected in the flags argument and traverse
 * the error parameters passed and join that with the the passed error
 * table to (possibly) print error messages as well as set new conditions
 * (and return them in the (modified) flags argument).
 *
 * The exception to this rule (side effects) are when the slave
 * or facility changes state wrt online/offline status, or when
 * response extent parameter is encountered. In the former case,
 * the appropriate state flags are set (in an id_ctlr_t or id_unit_t
 * structure, as appropriate). In the latter case, the Q_ERRBLK
 * private information is set from the fields in the response
 * extent parameter.
 *
 * We could be called without the controller lock held, but since this
 * is a non-optimizable "rare" error path, we state that we must be
 * called with the controller lock held.
 */
static int
id_error_parse(ipiq_t *q, ipi_errtab_t *err_table, int flags)
{
	id_ctlr_t	*c;
	ipi_errtab_t	*etp;		/* error table pointer */
	struct ipi3resp	*rp;		/* response pointer */
	u_char		*pp;		/* parameter pointer */
	int		id;		/* parameter id */
	int		plen;		/* parameter length */
	int		rlen;		/* response length */
	int		pflags;		/* flags on parameter */
	int		bflags;		/* flags on bit */
	int		result;		/* result for current check */
	int		dryrun;		/* set during dry run */
	int		silent;		/* set from q_flags */
	int		printstate;	/* sporadic attempt to control print */
	int		clev;		/* complaint level to id_printerr */


	if ((rp = q->q_resp) == NULL) {
		id_printerr(q, CE_WARN, ID_EP_FAC,
		    "no response packet available");
		return (IE_CMD);
	}

	c = id_ctlr[IPI_SLAVE(q->q_addr)];
	/*
	 * The table is read twice, once to see if any print
	 * flags are set and again to do the real work.
	 */

	dryrun = 1;
	if (iddebug)
		flags |= IE_DBG;
	silent = (q->q_flag & IP_SILENT);

loop:
	rlen = rp->hdr_pktlen + sizeof (rp->hdr_pktlen) -  sizeof (*rp);

	for (pp = (u_char *) rp + sizeof (*rp); rlen > 0; pp += plen) {

		if ((plen = pp[0] + 1) > rlen)
			break;
		rlen -= plen;
		if (plen <= 1)
			continue;	/* padding byte */

		/*
		 * Find parameter ID in the table.
		 */
		id = pp[1];
		pflags = 0;
		for (etp = err_table; etp->et_mask; etp++) {
			if (etp->et_byte != 0)
				continue;
			if (etp->et_parmid == id)
				break;
			if (etp->et_parmid + IPI_FAC_PARM == id) {
				pflags |= IE_FAC;
				break;
			}
		}

		/*
		 * If parameter not found in table, skip it, but complain.
		 */
		if (etp->et_mask == 0) {
			flags |= IE_DUMP;
			silent = 0;
			continue;
		}

		/*
		 * Set flags according to flags found in ID entry.
		 */
		pflags |= (etp->et_flags & ~IE_MASK) | flags;
		flags |= (pflags & IE_RESP_MASK);
		result = etp->et_flags & IE_MASK;
		printstate = 0;

		if (!dryrun) {
			if (flags & (IE_CMD|IE_RETRY|IE_REQUEUE))
				clev = CE_WARN;
			else
				clev = CE_NOTE;
		}

		if (!dryrun || (pflags & IE_FIRST_PASS)) {
			if (!dryrun && (pflags & (IE_MSG | IE_PRINT))) {
				id_printerr(q, clev, ID_EP_FAC, etp->et_msg);
				printstate = 1;
			}
			/*
			 * run handler for parameter.
			 */
			flags |= id_error(q, result, pflags, etp->et_msg, pp);
		}

		/*
		 * Test each condition mentioned in the table until
		 * the next parameter ID entry.
		 */
		bflags = 0;			/* in case no bit matches */
		while ((++etp)->et_byte) {
			if (etp->et_byte + 1 < plen &&
			    (etp->et_mask & pp[etp->et_byte + 1])) {
				char *m = etp->et_msg;

				bflags = (etp->et_flags & ~IE_MASK) | pflags;
				flags |= (bflags & IE_RESP_MASK);
				result = etp->et_flags & IE_MASK;
				if (dryrun)
					continue;
				if (((pflags | bflags) & (IE_MSG | IE_PRINT)) &&
				    !silent) {
					if (printstate == 0) {
						id_printerr(q, clev,
						    ID_EP_FAC, m);
						printstate = 1;
					} else if (printstate == 1) {
						cmn_err(CE_CONT, "\t%s", m);
						printstate = 2;
					} else {
						cmn_err(CE_CONT, ", %s", m);
					}
				}
				/*
				 * run handler for this match.
				 */
				flags |= id_error(q, result, bflags, m, pp);
			}
		}
		if (!dryrun && ((pflags | bflags) & (IE_MSG | IE_PRINT)) &&
		    silent == 0 && printstate > 0) {
			cmn_err(CE_CONT, ".\n");
		}
	}

	if (dryrun) {
		dryrun = 0;
		goto loop;
	}
	if ((flags & IE_DUMP) && !silent) {
		struct icparg icp;
		icp.arg = (void *) q->q_cmd;
		icp.msg = "command in error";
		IDC_CONTROL(c, IPI_CTRL_PRINTCMD, &icp, 0);
		icp.arg = (void *) q->q_resp;
		icp.msg = "response to command in error";
		IDC_CONTROL(c, IPI_CTRL_PRINTRESP, &icp, 0);
	}

	/*
	 * If the result of error parsing was to retry or requeue
	 * the command, but the command was marked as not retryable,
	 * convert the result to an error for the command.
	 */
	if ((flags & (IE_REQUEUE|IE_RETRY)) && (q->q_flag & IP_NO_RETRY)) {
		flags &= ~(IE_REQUEUE|IE_RETRY);
		flags |= IE_CMD;
	}
	/*
	 * If the result of error parsing was to retry the command,
	 * check to make sure that we haven't run through our limit
	 * for retrying the command.
	 */
	if (flags & IE_RETRY) {
		if (q->q_retry != 0) {
			q->q_retry--;
		} else {
			flags &= ~IE_RETRY;
			flags |= IE_CMD;
		}
	}
	return (flags);
}


/*
 * This routine prints out an error message with the appropriate device
 * or controller number and the block number if known.
 */

static void
id_printerr(ipiq_t *q, int level, int how, char *omsg)
{
	auto char buf[128];

	if ((q->q_flag & IP_SILENT) && level != CE_PANIC)
		return;

	id_whoinerr(q, how, buf);

	if (omsg == NULL)
		omsg = " ";

	if (level == CE_CONT) {
		cmn_err(level, "?%s: %s\n", buf, omsg);
	} else {
		cmn_err(level, "%s: %s", buf, omsg);
	}
}

/*
 * Given an ipiq_t and a flag, generate the location for an error.
 */

static void
id_whoinerr(ipiq_t *q, int flag, char *output)
{
	static struct opcode_name {
		u_char	opcode;
		char	*name;
	} opcode_name[] = {
		{ IP_NOP,		"no-op",	},
		{ IP_ATTRIBUTES,	"Attributes"	},
		{ IP_REPORT_STAT,	"Report Status"	},
		{ IP_PORT_ADDR,		"Port Address " },
		{ IP_ABORT,		"Abort"		},
		{ IP_READ,		"read"		},
		{ IP_WRITE,		"write"		},
		{ IP_FORMAT,		"format"	},
		{ IP_READ_DEFLIST,	"read defects list"	},
		{ IP_WRITE_DEFLIST,	"write defect list"	},
		{ IP_SLAVE_DIAG,	"Slave Diagnostics"	},
		{ IP_READ_BUF,		"Read Buffer"	},
		{ IP_WRITE_BUF,		"Write Buffer"	},
		{ IP_POS_CTL,		"Position Control"	},
		{ IP_REPORT_POS,	"Report Position"	},
		{ IP_REALLOC,		"reallocate"	},
		{ IP_ALLOC_RESTOR,	"allocate restore"	},
		{ IP_DIAG_CTL,		"diag ctl"	},
		{ 0,			NULL		}	/* table end */
	};
	register struct opcode_name *np;
	char lbuf[64];
	id_unit_t *un;
	id_ctlr_t *c;

	c = id_ctlr[IPI_SLAVE(q->q_addr)];
	if (IPI_FAC(q->q_addr) != IPI_NO_ADDR)
		un = c->c_un[IPI_FAC(q->q_addr)];
	else
		un = (id_unit_t *) NULL;

	if ((flag & ID_EP_FAC) && un != NULL) {
		register struct buf *bp;
		int inst;

		inst = ddi_get_instance(un->un_dip);
		bp = (struct buf *) q->q_private[Q_BUF];

		if ((flag & ID_EP_PART) && bp != (struct buf *) NULL) {
			int part;
			daddr_t blk;
			struct un_lpart	*lp;

			part = ID_LPART(bp->b_edev);
			lp = &un->un_lpart[part];
			blk = (daddr_t) q->q_private[Q_ERRBLK];

			if ((un->un_flags & ID_LABEL_VALID) &&
			    !(q->q_flag & IP_ABS_BLOCK)) {
				sprintf(lbuf, "id%d%c, block %d (%d abs), ",
				    inst, part + 'a', blk - lp->un_blkno, blk);
			} else {
				sprintf(lbuf, "id%d, block %d, ", inst, blk);
			}
		} else {
			sprintf(lbuf, "id%d, ", inst);
		}
	} else {
		sprintf(lbuf, "%s%d, ", c->c_name, c->c_instance);
	}

	if (q->q_result == IP_ASYNC) {
		/*
		 * No valid q_cmd
		 */
		sprintf(output, "%s<async response>", lbuf);
	} else {
		for (np = opcode_name; np->name; np++) {
			if (np->opcode == q->q_cmd->hdr_opcode) {
				break;
			}
		}
		if (np->name == NULL) {
			sprintf(output, "%sop=<0x%x,0x%x>", lbuf,
			    q->q_cmd->hdr_opcode, q->q_cmd->hdr_mods);
		} else {
			sprintf(output, "%sop=<%s>", lbuf, np->name);
		}
	}

}

/*
 * Section 12.0: IPI Attribute Routines (Autoconfiguration 3rd Level)
 */

/*
 * Build an IPI Report Attribute command, one parameter at a time.
 */
static void
id_build_attr_cmd(ipiq_t *q, rtable_t *rtp)
{
	u_char *cp, *pp;
	struct ipi3header *ip;

	ip = (struct ipi3header *)q->q_cmd;
	bzero((caddr_t)ip, sizeof (struct ipi3header) + 3 + 1);

	ip->hdr_opcode = IP_ATTRIBUTES;
	ip->hdr_mods = IP_OM_REPORT;
	pp = cp = (u_char *)(ip + 1);	/* point to start of parameters */
	cp++;				/* skip parameter length */
	*cp++ = ATTR_REQPARM;		/* request parm parameter */
	*cp++ = RESP_AS_PKT;		/* parameters in response */
	*cp++ = rtp->rt_parm_id;
	*pp = (cp - pp) - sizeof (*pp);
	ip->hdr_pktlen = (cp - (u_char *)ip) - sizeof (ip->hdr_pktlen);
}

/*
 * Build an IPI Load Attribute command, one parameter at a time.
 *
 * Side effect is to call the function in rtable_t prior to returning.
 */
static void
id_build_set_attr_cmd(ipiq_t *q, rtable_t *rtp, void *arg)
{
	u_char *cp;
	struct ipi3header *ip;
	int len;

	/*
	 * Figure length of command packet from parameter size.
	 * Length is header + parm len, id, flags, + one.
	 */
	len = sizeof (struct ipi3header) + 3 + rtp->rt_min_len;

	/*
	 * Form IPI-3 Command packet.
	 */
	ip = (struct ipi3header *)q->q_cmd;
	bzero((caddr_t)ip, (u_int)len);

	ip->hdr_opcode = IP_ATTRIBUTES;
	ip->hdr_mods = IP_OM_LOAD;
	/*
	 * Insert parameter ID into request parm parameter.
	 */
	cp = (u_char *)(ip + 1);
	while (((int)cp + 2) % sizeof (long) != 0)
		*cp++ = 0;		/* pad for alignment */
	*cp++ = rtp->rt_min_len + 1;	/* length (including ID byte) */
	*cp++ = rtp->rt_parm_id;	/* parameter ID */

	(*rtp->rt_func)(q, rtp->rt_parm_id, cp, rtp->rt_min_len, arg);
	cp += rtp->rt_min_len;

	/*
	 * Set parameter length and packet length.
	 */
	ip->hdr_pktlen = (cp - (u_char *)ip) - sizeof (ip->hdr_pktlen);
}

/*
 * Inspect vendor ID parameter (0x50), and set controller type.
 */
/* ARGSUSED */
static int
id_attr_vendor(ipiq_t *q, int parm_id, u_char *parm, int len, void *cp)
{
	register struct vendor_parm *vp;
	register id_ctlr_t *c = (id_ctlr_t *)cp;

	vp = (struct vendor_parm *) parm;

	if (strncmp(vp->manuf, "SUNMICRO", 8) == 0 &&
	    strncmp(vp->model, "PANTHER", 7) == 0) {

		c->c_ctype = DKC_SUN_IPI1;	/* Panther */

		/*
		 * Early versions of Panther don't support the slave
		 * reconfiguration parameter.
		 */
		if (vp->rev[3] == 0)
			c->c_caplim |= ID_NO_RECONF;
		/*
		 * At least Rev 6. versions have a problem
		 * running out of rqes, so try and limit
		 * the max commands below what it reports.
		 */
		if (vp->rev[3] <= 6) {
			c->c_caplim |= ID_LIMIT_CMDS;
		}

	} else {
		c->c_ctype = DKC_UNKNOWN;
		cmn_err(CE_WARN, "?idc%d: unknown ctlr vendor id: "
		    "manuf '%s' model '%s'", ddi_get_instance(c->c_dip),
		    vp->manuf, vp->model);
	}
}

/*
 * Handle multi port parameter (0x5e) for drive.
 */
static int
id_attr_port(ipiq_t *q, int parm_id, u_char *parm, int len, void *cp)
{
	register id_ctlr_t *c = (id_ctlr_t *)cp;
	register id_unit_t *un;
	register struct multiport_parm *mpp = (struct multiport_parm *) parm;
	int fac;

	fac = IPI_FAC(q->q_addr);
	if (fac < IPI_NFAC)
		un = c->c_un[fac];
	else
		un = (id_unit_t *) NULL;
	if (un) {
		cmn_err(CE_CONT, "?id_attr_port: id%d, ports =%x\n",
		    ddi_get_instance(un->un_dip), mpp->no_of_ports);
	}
}


/*
 * Handle addressee configuration parameter (0x65) for controller.
 */
/* ARGSUSED */
static int
id_ctlr_conf(ipiq_t *q, int parm_id, u_char *parm, int len, void *cp)
{
	u_int maxq;
	register id_ctlr_t *c = (id_ctlr_t *) cp;
	register struct addr_conf_parm *acp = (struct addr_conf_parm *) parm;

	maxq = (acp->ac_max_queue > 0) ? acp->ac_max_queue : (u_int) -1;

	/*
	 * If ID_LIMIT_CMDS is set, then limit commands lower than
	 * amount above. 16 seems a not unreasonable number.
	 */
	if (c->c_caplim & ID_LIMIT_CMDS) {
		if (id_dumb_limit > 16)
			id_dumb_limit--;
		if (maxq > id_dumb_limit) {
			maxq = id_dumb_limit;
		}
	}
	IDC_CONTROL(c, IPI_CTRL_LIMIT_SQ, (void *) maxq, 0);
}

/*
 * Handle slave reconfiguration (0x6e) (bit fields) parameter for controller.
 */
/* ARGSUSED */
static int
id_ctlr_reconf(ipiq_t *q, int parm_id, u_char *parm, int len, void *cp)
{
	register id_ctlr_t *c = (id_ctlr_t *)cp;
	register struct reconf_bs_parm *rp = (struct reconf_bs_parm *) parm;

	if (rp->sr_seek_alg)
		c->c_caplim |= ID_SEEK_ALG;
	c->c_reconf_bs_parm = *rp;
}

/*
 * Handle setting of slave reconfiguration (bit fields) parameter for ctlr.
 */
/* ARGSUSED */
static int
id_set_ctlr_reconf(ipiq_t *q, int parm_id, u_char *parm, int len, void *cp)
{
	register id_ctlr_t *c = (id_ctlr_t *)cp;
	register struct reconf_bs_parm *rp;

	rp = &c->c_reconf_bs_parm;	/* point to copy in ctlr struct */
	rp->sr_inh_resp_succ = 0;
	rp->sr_inh_ext_substat = 0;
	rp->sr_tnp_req = 1;
	rp->sr_inh_slv_msg = 0;		/* enable slave messages */
	rp->sr_dis_cl1x = 0;		/* enable class 1 transitions */
	*(struct reconf_bs_parm *)parm = *rp;	/* set parm */
	return (0);
}

/*
 * Handle facilities attached parameter (0x6e) for controller.
 */
/* ARGSUSED */
static int
id_ctlr_fat_attr(ipiq_t *q, int parm_id, u_char *parm, int len, void *cp)
{
	id_ctlr_t *c = (id_ctlr_t *)cp;
	struct fat_parm *fp = (struct fat_parm *)parm;
	int flags = 0;

	for (; len >= sizeof (*fp); fp++, len -= sizeof (*fp))
		flags |= 1 << fp->fa_addr;
	c->c_fac_flags = flags;
}

/*
 * Factility response parsing routines
 */

/*
 * Inspect Physical disk configuration parameter - set unit geometry.
 */
/* ARGSUSED */
static int
id_attr_physdk(ipiq_t *q, int parm_id, u_char *parm, int len, void *unp)
{
	register id_unit_t *un = (id_unit_t *)unp;
	register struct physdk_parm *pp = (struct physdk_parm *)parm;
	register struct dk_geom *g = &un->un_g;

	g->dkg_pcyl = pp->pp_last_cyl + 1;	/* number of physical cyls */
	g->dkg_ncyl = pp->pp_last_cyl;		/* allow one alternate */
	g->dkg_acyl = 1;			/* alternate cylinders */
	g->dkg_bcyl = 0;			/* beginning cylinder */
	g->dkg_nhead = pp->pp_nheads;		/* tracks per cylinder */

	/*
	 * rot/min = 60 sec/min * 1000000 usec/sec  /  usec/rot
	 * 	but try not to divide by zero.
	 */
	if (pp->pp_rotper > 0)
		g->dkg_rpm = 60 * 1000000 / pp->pp_rotper;
	else
		g->dkg_rpm = 3600;		/* reasonable default */
}


/*
 * Handle parameter:  Size of disk physical blocks.
 */
/* ARGSUSED */
static int
id_phys_bsize(ipiq_t *q, int parm_id, u_char *parm, int len, void *unp)
{
	register id_unit_t *un = (id_unit_t *)unp;
	register struct physbsize_parm *pp = (struct physbsize_parm *)parm;

	if ((un->un_phys_bsize = pp->pblksize) > 0)
		un->un_flags |= ID_FORMATTED;
}

/*
 * Handle parameter:  Size of disk logical blocks.
 */
/* ARGSUSED */
static int
id_log_bsize(ipiq_t *q, int parm_id, u_char *parm, int len, void *unp)
{
	register id_unit_t *un = (id_unit_t *)unp;
	register struct datbsize_parm *pp = (struct datbsize_parm *)parm;
	int i;

	un->un_log_bsize = pp->dblksize;
	for (i = DEV_BSHIFT; i < NBBY * NBPW; i++) {
		if (un->un_log_bsize == (1 << i)) {
			un->un_log_bshift = i;
			break;
		}
	}
	if (i == NBBY * NBPW) {
		cmn_err(CE_WARN, "id%d: invalid logical block size %d",
		    ddi_get_instance(un->un_dip), un->un_log_bsize);
	}
}

/*
 * Inspect Logical Geometry parameter - get logical blocks per track.
 * The geometry should be already set from the physical disk configuration
 * parameter, so doublecheck with this parameter.
 */
/* ARGSUSED */
static int
id_attr_nblks(ipiq_t *q, int parm_id, u_char *parm, int len, void *unp)
{
	register id_unit_t *un = (id_unit_t *)unp;
	register struct numdatblks_parm *pp = (struct numdatblks_parm *)parm;
	register struct dk_geom *g = &un->un_g;
	int i, inst;

	inst = ddi_get_instance(un->un_dip);

	g->dkg_nsect = pp->bpertrk;	/* number of blocks per track */
	un->un_first_block = pp->strtadr;
	if (pp->strtadr != 0) {
		cmn_err(CE_NOTE, "id%d: starting block address nonzero: %d",
		    inst, pp->strtadr);
	}

	if (pp->bpertrk == 0) {
		cmn_err(CE_WARN, "id%d: zero blocks per track", inst);
		un->un_flags &= ~ID_FORMATTED;
		return;
	}

	i = pp->bpercyl / pp->bpertrk;		/* tracks per cylinder */
	if (i != g->dkg_nhead) {
		cmn_err(CE_WARN, "id%d: inconsistent geometry: blk/cyl "
		    "%d blk/trk %d heads %d", inst, pp->bpercyl, pp->bpertrk,
		    g->dkg_nhead);
	}
	if (pp->bpercyl == 0) {
		cmn_err(CE_WARN, "id%d: zero blocks per cylinder", inst);
		un->un_flags &= ~ID_FORMATTED;
		return;
	}
	i = pp->bperpart / pp->bpercyl;		/* cylinder count */
	if (i > (int) g->dkg_pcyl) {
		cmn_err(CE_WARN, "id%d: inconsistent geometry: tot blks "
		    "%d blks/cyl %d pcyl %d", inst, pp->bperpart,
		    pp->bpercyl, g->dkg_pcyl);
		g->dkg_pcyl = MIN(i, (int) g->dkg_ncyl);
		g->dkg_ncyl = g->dkg_pcyl - g->dkg_acyl;
		cmn_err(CE_WARN, "id%d: setting pcyl %d ncyl %d acyl %d",
			inst, g->dkg_pcyl, g->dkg_ncyl, g->dkg_acyl);
	}
}

/*
 * Set of functions called by HADF ioctls.
 */

static int
id_reserve(dev_t dev, u_char hdr_mods)
{
	id_unit_t	*un;
	id_ctlr_t	*c;
	int		errno = 0;
	struct ipiq	*q;
	struct buf	*bp;

	c = id_ctlr[ID_CINST(dev)];
	un = c->c_un[ID_UINST(dev)];

	errno = IDU_ALLOC(un, 0, DDI_DMA_SLEEP, 0, &q);
	if (errno)
		return (errno);
	errno = id_getsbuf(un, 0, (caddr_t) 0, B_READ, 0);
	if (errno) {
		IDU_RELSE(un, q);
		return (errno);
	}
	bp = un->un_sbufp;

	q->q_cmd->hdr_opcode = IP_PORT_ADDR;
	q->q_cmd->hdr_mods = hdr_mods; /* IP_OM_RESRV or IP_OM_PRESRV */
	q->q_cmd->hdr_pktlen = IPI_HDRLEN;
	q->q_time = ID_REC_TIMEOUT;
	q->q_flag = IP_NO_RETRY | IP_SILENT;
	q->q_private[Q_BUF] = (u_long)bp;
	bp->b_forw = (struct buf *) q;

	idstrategy(bp);
	if (biowait(bp) != 0) {
		q->q_result = IP_ERROR;
		errno = EIO;
	} else if (q->q_result != IP_SUCCESS) {
		errno = EIO;
	}

	IDU_RELSE(un, q);
	sema_v(&un->un_sbs);
	return (errno);
}

static int
id_release(dev_t dev)
{
	id_unit_t	*un;
	id_ctlr_t	*c;
	int		errno = 0;
	struct ipiq	*q;
	struct buf	*bp;

	c = id_ctlr[ID_CINST(dev)];
	un = c->c_un[ID_UINST(dev)];

	errno = IDU_ALLOC(un, 0, DDI_DMA_SLEEP, 0, &q);
	if (errno)
		return (errno);
	errno = id_getsbuf(un, 0, (caddr_t) 0, B_READ, 0);
	if (errno) {
		IDU_RELSE(un, q);
		return (errno);
	}
	bp = un->un_sbufp;

	q->q_cmd->hdr_opcode = IP_PORT_ADDR;
	q->q_cmd->hdr_mods = IP_OM_RELSE;
	q->q_cmd->hdr_pktlen = IPI_HDRLEN;
	q->q_time = ID_REC_TIMEOUT;
	q->q_flag = IP_NO_RETRY | IP_SILENT;
	q->q_private[Q_BUF] = (u_long)bp;
	bp->b_forw = (struct buf *) q;

	idstrategy(bp);
	if (biowait(bp) != 0) {
		q->q_result = IP_ERROR;
		errno = EIO;
	} else if (q->q_result != IP_SUCCESS) {
		errno = EIO;
	}

	IDU_RELSE(un, q);
	sema_v(&un->un_sbs);
	return (errno);
}

/*
 * Return 0 if drive is responding, EACCES if busy (reserved by alternate
 * port). Otherwise return error.
 */
static int
id_ping_drive(dev_t dev)
{
	id_unit_t	*un;
	id_ctlr_t	*c;
	int		errno = 0;
	struct ipiq	*q;
	struct buf	*bp;
	int old_busy_state;
	int flags = 0;
	u_int condition_parm = 0;

	c = id_ctlr[ID_CINST(dev)];
	un = c->c_un[ID_UINST(dev)];

	errno = IDU_ALLOC(un, 0, DDI_DMA_SLEEP, 0, &q);
	if (errno)
		return (errno);
	errno = id_getsbuf(un, 0, (caddr_t) 0, B_READ, 0);
	if (errno) {
		IDU_RELSE(un, q);
		return (errno);
	}
	bp = un->un_sbufp;

	q->q_cmd->hdr_opcode = IP_REPORT_STAT;
	q->q_cmd->hdr_mods = IP_OM_CONDITION;
	q->q_cmd->hdr_pktlen = IPI_HDRLEN;
	q->q_time = ID_REC_TIMEOUT;
	q->q_flag = IP_NO_RETRY | IP_SILENT;
	q->q_private[Q_BUF] = (u_long)bp;
	bp->b_forw = (struct buf *) q;

	/*
	 * The IE_BUSY flag is set and restored, as it is uncertain when to
	 * clear this condition, once it is set. (one place is id_reserve).
	 * The Report Status will find P-Busy if the drive port is
	 * reserved-away, and id_error_parse will set IE_BUSY. Avoid getting
	 * stuck in this state.
	 */
	old_busy_state = un->un_flags & IE_BUSY;

	idstrategy(bp);
	if (biowait(bp) != 0) {
		q->q_result = IP_ERROR;
		errno = EIO;
	}

	mutex_enter(&c->c_lock);

	if (q->q_result == IP_SUCCESS) {
		flags = id_error_parse(q, id_condtab, IE_FAC);
	}
	if (q->q_resp) {
		bcopy((caddr_t) ((u_char *)q->q_resp +
		    sizeof (struct ipi3resp)), (caddr_t) &condition_parm, 4);
	}

	un->un_flags &= ~IE_BUSY; /* clear IE_BUSY set by report status */
	un->un_flags |= old_busy_state; /* restore old IE_BUSY state */

	mutex_exit(&c->c_lock);

	if (q->q_result != IP_SUCCESS) {
		q->q_result = IP_ERROR;
		errno = EIO;	/* command failed */

	} else if ((condition_parm & 0xffff0800) == 0x03510800) {
		/*
		 * Check for condition parm id (0x51) and length of 3 bytes.
		 * Also check for "Facility Switched to Another Port" bit.
		 */
		errno = EACCES; /* lost the reservation */

	} else if (!IE_STAT_READY(un->un_flags & ~(IE_RECOVER | IE_RE_INIT))) {
		/*
		 * Check flags. If the facility isn't ready, consider the
		 * status operation a failure.
		 * Ignore IE_RECOVER and IE_RE_INIT flags for this test.
		 */
		q->q_result = IP_ERROR;	/* drive not ready */
		errno = EIO;
	}

	IDU_RELSE(un, q);
	sema_v(&un->un_sbs);

	if (idha_debug && errno) {
		cmn_err(CE_CONT,
		"id%d: %s error, un_flags %x, flags %x, q_result %d, %x\n",
		ID_INST(dev), (errno == EACCES) ? "Reservation Lost" :
		(q->q_result != IP_SUCCESS) ? "Command" : "Ready",
		un->un_flags, flags, q->q_result, condition_parm);
	}
	return (errno);
}

static int
id_facility_reset(dev_t dev)
{
	id_unit_t	*un;
	id_ctlr_t	*c;
	int		errno = 0;
	struct ipiq	*q;
	struct ipi3header	*ip;
	char		*cp;
	struct buf	*bp;
	int		i;

	c = id_ctlr[ID_CINST(dev)];
	un = c->c_un[ID_UINST(dev)];

	errno = IDU_ALLOC(un, 0, DDI_DMA_SLEEP, 0, &q);
	if (errno)
		return (errno);
	errno = id_getsbuf(un, 0, (caddr_t) 0, B_READ, 0);
	if (errno) {
		IDU_RELSE(un, q);
		return (errno);
	}
	bp = un->un_sbufp;

	ip = q->q_cmd;
	q->q_cmd->hdr_opcode = IP_ABORT;
	q->q_cmd->hdr_mods = IP_OM_RESET | IP_CM_PRI;
	q->q_cmd->hdr_pktlen = IPI_HDRLEN;
	q->q_time = ID_REC_TIMEOUT;
	q->q_flag = IP_NO_RETRY | IP_SILENT;
	q->q_private[Q_BUF] = (u_long)bp;
	bp->b_forw = (struct buf *) q;

	cp = (char *)(ip + 1);

	/*
	 * Insert facility reset parmameter.
	 */
	q->q_cmd->hdr_pktlen = sizeof (struct ipi3header) + 3 +
		sizeof (struct fac_rst_parm) - sizeof (ip->hdr_pktlen);

	for (i = (int) (cp + 3); (i % sizeof (long)) != 0; i++)
		*cp++ = 0;
	*cp++ = sizeof (struct fac_rst_parm) + 1;	/* parm + id */
	*cp++ = FAC_RST_PARM;				/* parameter id */

	((struct fac_rst_parm *)cp)->rst_type = FAC_RST_POWER;

	idstrategy(bp);
	if (biowait(bp) != 0) {
		q->q_result = IP_ERROR;
		errno = EIO;
	} else if (q->q_result != IP_SUCCESS) {
		errno = EIO;
	}

	IDU_RELSE(un, q);
	sema_v(&un->un_sbs);
	return (errno);
}


static int
idha_failfast_request(dev_t dev, int interval)
{
	/* set minimum interval as one second */
	if (interval > 0 && interval < 1000)
		interval = 1000;
	interval *= 1000; /* milli seconds to micro seconds */

	return (idha_watch_submit(dev, interval, NULL, NULL));
	/*
	 * There is no call back function used now, if the facility loses
	 * reservation then the default action is to halt the system.
	 * Callback function may be used later, if HADF requires it.
	 */
}

struct	idha_watch_request {
	struct idha_watch_request	*idha_next;	/* next in list */
	dev_t				idha_dev;	/* facility */
	int				idha_interval;	/* interval for check */
	int				idha_timeout;	/* count down */
	int				idha_busy;	/* req in progress */
	int				(*idha_callback)();
	caddr_t				idha_callback_arg;
};

static struct	idha_watch {
	kthread_t			*idha_thread;
	kmutex_t			idha_mutex;	/* for this struct */
	kcondvar_t			idha_cv;
	struct idha_watch_request	*idha_head;	/* head of requests */
} idha_watch;

/*
 * idha_watch setup, called from _init()
 */
static void
idha_watch_init()
{
	mutex_init(&idha_watch.idha_mutex, "idha_watch_mutex", MUTEX_DRIVER,
		NULL);
	cv_init(&idha_watch.idha_cv, "idha_watch_cv", CV_DRIVER, NULL);
}

/*
 * idha_watch cleanup, called from _fini()
 */
static void
idha_watch_fini()
{
	ASSERT(idha_watch.idha_thread == 0);
	mutex_destroy(&idha_watch.idha_mutex);
	cv_destroy(&idha_watch.idha_cv);
}

static int
idha_watch_submit(dev_t dev, int interval, int (*callback)(), caddr_t arg)
{
	struct idha_watch_request	*p;

	mutex_enter(&idha_watch.idha_mutex);

	for (p = idha_watch.idha_head; p != NULL; p = p->idha_next) {
		if (p->idha_dev == dev)
			break;
	}

	/*
	 * Delete an existing watch request.
	 */
	if (interval == 0) {
		if (p == NULL) {
			cmn_err(CE_CONT, "idha_watch_submit: req not found\n");
		} else {
			p->idha_interval = 0;
			/* p will be freed by idha_watch_thread. */
		}
		mutex_exit(&idha_watch.idha_mutex);
		return (0);
	}

	/*
	 * Change the interval of an existing request.
	 */
	if (p) {
		p->idha_interval = interval;
		cv_signal(&idha_watch.idha_cv);
		mutex_exit(&idha_watch.idha_mutex);
		return (0);
	}
	mutex_exit(&idha_watch.idha_mutex);

	/*
	 * Rest of the code deals with adding a new watch request.
	 */
	p = (struct idha_watch_request *) kmem_zalloc(
		sizeof (struct idha_watch_request), KM_SLEEP);

	p->idha_dev = dev;
	p->idha_interval = interval;
	p->idha_timeout = 0;
	p->idha_busy = 0;
	p->idha_callback = callback;
	p->idha_callback_arg = arg;

	mutex_enter(&idha_watch.idha_mutex);

	if (idha_watch.idha_thread == 0) {
		idha_watch.idha_thread = thread_create((caddr_t) NULL, 0,
			idha_watch_thread, (caddr_t) 0, 0, &p0, TS_RUN,
			v.v_maxsyspri - 2);
	}
	p->idha_next = idha_watch.idha_head;
	idha_watch.idha_head = p;

	/* reset all timeouts */
	while (p) {
		p->idha_timeout = p->idha_interval;
		p = p->idha_next;
	}
	cv_signal(&idha_watch.idha_cv);
	mutex_exit(&idha_watch.idha_mutex);
	return (0);
}

static void idha_failfast_check(struct idha_watch_request *);
/*
 * idha_watch thread.
 */
static void
idha_watch_thread()
{
	struct idha_watch_request	*p;
	struct idha_watch_request	*q;
	int				last_delay = 0;
	int				next_delay = 0;
	u_long				now;

	mutex_enter(&idha_watch.idha_mutex);

again:
	for (;;) {
		if (idha_watch.idha_head == NULL) {
			/*
			 * No more work to do, reset and exit thread.
			 */
			idha_watch.idha_thread = 0;
			mutex_exit(&idha_watch.idha_mutex);
			return;
		}

		p = idha_watch.idha_head;
		next_delay = 0;
		while (p) {
			if (p->idha_interval == 0 && !p->idha_busy) {
				/*
				 * This facility need not be checked anymore.
				 * remove watch request from queue.
				 */
				q = p;
				if (p == idha_watch.idha_head) {
					p = p->idha_next;
					idha_watch.idha_head = p;
				} else {
					p = idha_watch.idha_head;
					while (p->idha_next != q)
						p = p->idha_next;
					p->idha_next = q->idha_next;
					p = q->idha_next;
				}
				kmem_free((caddr_t) q,
				    sizeof (struct idha_watch_request));
				continue;
			}

			next_delay = (next_delay == 0) ? p->idha_timeout :
				min(next_delay, p->idha_timeout);
			p->idha_timeout -= last_delay;

			if (p->idha_timeout <= 0 && !p->idha_busy) {
				p->idha_timeout = p->idha_interval;
				p->idha_busy = 1;

				/*
				 * OK to release and reacquire mutex.
				 * No one changes the req list, but this routine
				 * The submit routine only adds at head.
				 */
				mutex_exit(&idha_watch.idha_mutex);

				/*
				 * Check the facility for ready status and
				 * lost reservation
				 */
				idha_failfast_check(p);
				mutex_enter(&idha_watch.idha_mutex);
			}
			p = p->idha_next;
		}

		if (next_delay <= 0)
			next_delay = 5000000;	/* 5 seconds */

		(void) drv_getparm(LBOLT, &now);
		/*
		 * If we return from cv_timedwait because of signal, delay
		 * may not be accurate, but it does not matter.
		 */
		(void) cv_timedwait(&idha_watch.idha_cv, &idha_watch.idha_mutex,
			now + drv_usectohz(next_delay));
		last_delay = next_delay;
		next_delay = 0;
	}
	/* Not reached */
}


/*
 * This function is called at regular intervals by the idha_watch_thread.
 * This function function may be blocked when
 *	ipi q is not available
 *	un_sbufp is not available
 * 	in biowait
 * Hopefully the delay is not much unless the user is trying failfast
 * on a disk on which format ioctl is running !
 */

static void
idha_failfast_check(struct idha_watch_request *idha_requestp)
{
	id_unit_t	*un;
	id_ctlr_t	*c;
	dev_t		dev = idha_requestp->idha_dev;

	c = id_ctlr[ID_CINST(dev)];
	un = c->c_un[ID_UINST(dev)];

	switch (id_ping_drive(dev)) {
	case 0:
		if (idha_debug) {
			cmn_err(CE_CONT,
			"id%d: IDHA failfast check ok\n", ID_INST(dev));
		}
		break;
	case EACCES:
		if (idha_debug) {
			cmn_err(CE_CONT,
			"id%d: IDHA failfast check, lost reservation\n",
			ID_INST(dev));
		}
		if ((un->un_idha_status & IDHA_FAILFAST) &&
		    (un->un_idha_status & IDHA_RESERVE)) {
			un->un_idha_status |= IDHA_LOST_RESERVE;
			if (idha_failfast_enable) {
				cmn_err(CE_PANIC,
				"idha_watch: id%d: Reservation lost\n",
				ID_INST(dev));
				/*
				 * We could use idha_requestp->callback().
				 */
			} else {
				cmn_err(CE_CONT,
				"idha_watch: id%d: Reservation lost\n",
				ID_INST(dev));
			}
		}
		break;
	default:
		cmn_err(CE_CONT, "idha_watch: id%d: Error from reserved disk\n",
			ID_INST(dev));
		break;
	}
	/*
	 * Reset the busy flag indicating that the failfast check is done,
	 * so that after idha_interval, this function will be called again.
	 */
	idha_requestp->idha_busy = 0;
	/*
	 * no need to acquire mutex, thread ignores the requests that are
	 * busy. And after resetting busy this function is just returning.
	 */
}
