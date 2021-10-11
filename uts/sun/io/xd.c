/*
 * Copyright (c) 1987-1993 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)xd.c	1.26	94/08/08 SMI"

/*
 * Driver for Xylogics 7053 SMD disk controllers
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/stat.h>

#include <sys/errno.h>
#include <sys/open.h>
#include <sys/varargs.h>

#include <sys/hdio.h>
#include <sys/dkio.h>
#include <sys/dkbad.h>
#include <sys/vtoc.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/kstat.h>

#include <sys/xdvar.h>
#include <sys/xderr.h>

/*
 * Local defines
 */
#define	KIOSP		KSTAT_IO_PTR(un->un_iostats)

/*
 * Function Prototypes
 */

static int xdinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
    void **result);
static int xdidentify(dev_info_t *);
static int xdprobe(dev_info_t *);
static int xdattach(dev_info_t *, ddi_attach_cmd_t);
static void initlabel(struct xdunit *);
static int islabel(struct xdunit *, struct dk_label *);
static int ck_cksum(struct dk_label *);
static void uselabel(struct xdunit *, struct dk_label *);
static int usegeom(struct xdunit *, int);

static int xdopen(dev_t *, int, int, cred_t *);
static int xdclose(dev_t dev, int flag, int otyp, cred_t *cred_p);
static int xdrw(dev_t, struct uio *, int);
static int xdread(dev_t, struct uio *, cred_t *);
static int xdwrite(dev_t, struct uio *, cred_t *);
static int xdstrategy(struct buf *);
static int xdioctl(dev_t, int, int, int, cred_t *, int *);
static int xd_ugtyp(struct xdunit *, dev_t, int, int);
static int xd_ucmd(struct xdunit *, dev_t, struct hdk_cmd *);
static int xddump(dev_t, caddr_t, daddr_t, int);

static void xdgo(struct xdcmdblock *);

static void xd_build_user_vtoc(struct xdunit *un, struct vtoc *vtoc);
static int xd_build_label_vtoc(struct xdunit *un, struct vtoc *vtoc);
static int xd_write_label(dev_t dev);

static void xdmin(struct buf *);

#ifdef	DEBUG
#define	XDDEBUG
#endif

#if	defined(XDDEBUG) || defined(lint)
static int xddebug = 0;
#else
#define	xddebug	0
#endif

/*
 * Defines for setting ctlr, drive and format parameters.
 */
#define	XD_DRPARAM7053	0x00
#define	XD_FORMPAR2	((XD_FIELD1 << 8) | XD_FIELD2)
#define	XD_FORMPAR3	((((XD_FIELD3 << 8) | XD_FIELD4) * un->un_g.dkg_nhead \
			+ (XD_FIELD5 >> 8)) * un->un_g.dkg_nsect + \
			(XD_FIELD5 & 0xff))
#define	XD_FORMPAR4	((XD_FIELD6 << 24) | (XD_FIELD7 << 16) | XD_FIELD5ALT)

/*
 * Defines for specific format parameters fields.
 */
#define	XD_FIELD1	0x01		/* sector pulse to read gate */
#define	XD_FIELD2	0x0a		/* read gate to header sync */
#define	XD_FIELD3	0x1b		/* sector pulse to header sync */
#define	XD_FIELD4	0x14		/* header ecc to data sync */
#define	XD_FIELD5	0x200		/* sector size */
#define	XD_FIELD6	0x0a		/* header ecc to read gate */
#define	XD_FIELD7	0x03		/* data ecc to write gate */
#define	XD_FIELD5ALT	0x200		/* alternate sector size */


/*
 * Autoconfiguration data
 */

static void *xd_state;

static struct driver_minor_data {
	char	*name;
	int	minor;
	int	type;
} xd_minor_data[] = {
	{"a", 0, S_IFBLK},
	{"b", 1, S_IFBLK},
	{"c", 2, S_IFBLK},
	{"d", 3, S_IFBLK},
	{"e", 4, S_IFBLK},
	{"f", 5, S_IFBLK},
	{"g", 6, S_IFBLK},
	{"h", 7, S_IFBLK},
	{"a,raw", 0, S_IFCHR},
	{"b,raw", 1, S_IFCHR},
	{"c,raw", 2, S_IFCHR},
	{"d,raw", 3, S_IFCHR},
	{"e,raw", 4, S_IFCHR},
	{"f,raw", 5, S_IFCHR},
	{"g,raw", 6, S_IFCHR},
	{"h,raw", 7, S_IFCHR},
	{0}
};

/*
 * Device driver ops vector
 */

static struct cb_ops xd_cb_ops = {

	xdopen,			/* open */
	xdclose,		/* close */
	xdstrategy,		/* strategy */
	nodev,			/* print */
	xddump,			/* dump */
	xdread,			/* read */
	xdwrite,		/* write */
	xdioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev, 			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_NEW | D_MP		/* Driver compatibility flag */

};

static struct dev_ops xd_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	xdinfo,			/* info */
	xdidentify,		/* identify */
	xdprobe,		/* probe */
	xdattach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	&xd_cb_ops,		/* driver operations */
	(struct bus_ops *)0	/* bus operations */
};

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern struct mod_ops mod_driverops;

/*
 * Module linkage information for the kernel.
 */
static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a driver */
	"Xylogics 7053 SMD Disk Controller",
	&xd_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * This is the module initialization routine.
 */
int
_init(void)
{
	register int e;

	if ((e = ddi_soft_state_init(&xd_state,
	    sizeof (struct xdunit), 1)) != 0) {
		return (e);
	}

	if ((e = mod_install(&modlinkage)) != 0) {
		ddi_soft_state_fini(&xd_state);
	}
	return (e);
}

int
_fini(void)
{
	register int e;

	if ((e = mod_remove(&modlinkage)) != 0) {
		return (e);
	}
	ddi_soft_state_fini(&xd_state);
	return (e);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Autoconfiguration Routines
 */

static int
xdidentify(dev_info_t *dev)
{
	char *name = ddi_get_name(dev);

	/*
	 * This module now drives "xd" devices only
	 */

	if (strcmp(name, "xd") == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

static int
xdprobe(dev_info_t *dev)
{
	register  err;
	register struct xdctlr *c;
	register struct xdunit *un;
	register struct xdcmdblock *xdcbi;
	register int instance = ddi_get_instance(dev);
	register int ctlr_instance;
	int s_len, slave;

	c = (struct xdctlr *) ddi_get_driver_private(ddi_get_parent(dev));
	ctlr_instance = ddi_get_instance(c->c_dip);
	if (xddebug) {
		cmn_err(CE_CONT, "xdprobe instance %d par %d\n", instance,
		    ctlr_instance);
	}
	s_len = sizeof (slave);
	if (ddi_prop_op(DDI_DEV_T_NONE, dev, PROP_LEN_AND_VAL_BUF,
	    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "slave", (caddr_t)&slave,
	    &s_len) != DDI_SUCCESS || slave > XDUNPERC)
		return (DDI_PROBE_FAILURE);

	if (ddi_soft_state_zalloc(xd_state, instance) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "xd%d cannot alloc softstate",
		    instance);
		return (DDI_PROBE_FAILURE);
	}
	un = ddi_get_soft_state(xd_state, instance);
	ASSERT(un != NULL);
	c->c_units[slave] = un;
	un->un_dip = dev;
	un->un_c = c;
	un->un_slave = (u_char) slave;
	un->un_instance = instance;
	un->un_go = xdgo;
	ddi_set_driver_private(dev, (caddr_t) un);

	if ((xdcbi = XDGETCBI(c, 1, XY_SYNCH)) == (struct xdcmdblock *) 0) {
		cmn_err(CE_WARN, "xdc%d: cannot get cbi to probe unit",
		    ctlr_instance);
		ddi_soft_state_free(xd_state, instance);
		c->c_units[slave] = 0;
		return (DDI_PROBE_FAILURE);
	}

	mutex_enter(&c->c_mutex);
	err = XDCMD(xdcbi, XD_RESTORE, NOLPART, 0,
	    slave, 0, 0, XY_SYNCH, (xddebug) ? 0 : XY_NOMSG);
	mutex_exit(&c->c_mutex);

	XDPUTCBI(xdcbi);

	/*
	 * The rule is "probe is stateless".
	 */
	ddi_set_driver_private(dev, (caddr_t) 0);
	ddi_soft_state_free(xd_state, instance);
	c->c_units[slave] = 0;

	if (err == 0) {
		return (DDI_PROBE_SUCCESS);
	}
	return (DDI_PROBE_FAILURE);
}

static int
xdattach(dev_info_t *dev, ddi_attach_cmd_t cmd)
{
	register struct xdctlr *c;
	register struct xdunit *un;
	auto struct dk_label *l;
	ddi_dma_handle_t labhan;
	register struct xdcmdblock *xdcbi;
	int err, found, i;
	int s_len, slave;
	struct driver_minor_data *dmdp;
	register instance = ddi_get_instance(dev);

	if (xddebug) {
		cmn_err(CE_CONT, "xdattach: xd%d, %d\n", instance, cmd);
	}

	switch (cmd) {

	case DDI_ATTACH:
		c = (struct xdctlr *)ddi_get_driver_private(
		    ddi_get_parent(dev));
		s_len = sizeof (slave);
		if (ddi_prop_op(DDI_DEV_T_NONE, dev, PROP_LEN_AND_VAL_BUF,
		    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "slave",
		    (caddr_t)&slave, &s_len) != DDI_SUCCESS || slave > XDUNPERC)
			return (DDI_FAILURE);

		if (ddi_soft_state_zalloc(xd_state, instance) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "xd%d cannot alloc softstate",
			    instance);
			return (DDI_FAILURE);
		}
		un = ddi_get_soft_state(xd_state, instance);
		ASSERT(un != NULL);
		c->c_units[slave] = un;
		un->un_dip = dev;
		un->un_c = c;
		un->un_slave = (u_char) slave;
		un->un_instance = instance;
		un->un_go = xdgo;
		ddi_set_driver_private(dev, (caddr_t) un);

		/*
		 * We need to set up all the structures for the device.
		 */
		un->un_flags |= XY_UN_ATTACHED;
		un->un_sbufp = getrbuf(1);
		sema_init(&un->un_semoclose, 1, "xd_o", SEMA_DRIVER,
		    (void *)c->c_ibc);
		mutex_init(&un->un_sbmutex, "xd_s", MUTEX_DRIVER,
		    (void *)c->c_ibc);

		/*
		 * Initialize the label structures.  This is necessary so weird
		 * entries in the bad sector map don't bite us while reading the
		 * label. Also, this MUST come before the write parameters
		 * command so the geometry is not random.
		 */
		initlabel(un);

		/*
		 * Execute a set the drive parameters command via usegeom().
		 * This is necessary to define EC32 (error correction) of the
		 * drive parameters now that the IRAM can't remember after the
		 * power has been off.  Don't worry about the bogus drive
		 * geometry info
		 * as it gets filled in once the label is read below.
		 */

		if (usegeom(un, XY_SYNCH) != DDI_SUCCESS) {
			/* set driver parameters must have failed in usegeom */
			mutex_destroy(&un->un_sbmutex);
			sema_destroy(&un->un_semoclose);
			ddi_set_driver_private(dev, (caddr_t) 0);
			ddi_soft_state_free(xd_state, instance);
			c->c_units[slave] = 0;
			return (DDI_FAILURE);
		}

		/*
		 * Execute a set format parameters command.  This is necessary
		 * to define the sector size and interleave factors.  The other
		 * fields describe the format of each sector (gap sizes).
		 */

		xdcbi = XDGETCBI(c, 1, XY_SYNCH);

		mutex_enter(&c->c_mutex);
		err = XDCMD(xdcbi, XD_WPAR | XD_FORMAT, NOLPART,
		    (ddi_dma_handle_t) XD_FORMPAR4, un->un_slave,
		    (daddr_t) XD_FORMPAR3, XD_FORMPAR2, XY_SYNCH, 0);
		mutex_exit(&c->c_mutex);

		XDPUTCBI(xdcbi);

		if (err) {
			cmn_err(CE_WARN,
			    "xd%d: initialization failed (format parameters)",
			    instance);
			mutex_destroy(&un->un_sbmutex);
			sema_destroy(&un->un_semoclose);
			ddi_set_driver_private(dev, (caddr_t) 0);
			ddi_soft_state_free(xd_state, instance);
			c->c_units[slave] = 0;
			return (DDI_FAILURE);
		}

		/*
		 * Allocate a temporary buffer in DVMA space for reading the
		 * label.
		 */
		if (ddi_iopb_alloc(c->c_dip, c->c_lim, SECSIZE,
		    (caddr_t *) &l)) {
			cmn_err(CE_WARN,
			    "xd%d: cannot allocate iopb space for label",
			    instance);
			mutex_destroy(&un->un_sbmutex);
			sema_destroy(&un->un_semoclose);
			ddi_set_driver_private(dev, (caddr_t) 0);
			ddi_soft_state_free(xd_state, instance);
			c->c_units[slave] = 0;
			return (DDI_FAILURE);
		}

		if (ddi_dma_addr_setup(c->c_dip, (struct as *) 0, (caddr_t) l,
		    SECSIZE, DDI_DMA_READ, DDI_DMA_SLEEP, (caddr_t) 0,
		    c->c_lim, &labhan)) {
			cmn_err(CE_WARN, "xd%d: cannot map iopb label space",
			    instance);
			ddi_iopb_free((caddr_t) l);
			mutex_destroy(&un->un_sbmutex);
			sema_destroy(&un->un_semoclose);
			ddi_set_driver_private(dev, (caddr_t) 0);
			ddi_soft_state_free(xd_state, instance);
			c->c_units[slave] = 0;
			return (DDI_FAILURE);
		}

		/*
		 * Unit is now officially present.  It can now be accessed by
		 * the system even if the rest of this routine fails.
		 */
		un->un_flags |= XY_UN_PRESENT;

		/*
		 * Attempt to read the label.  We use the silent flag so
		 * no one will know if we fail.
		 */
		xdcbi = XDGETCBI(c, 1, XY_SYNCH);

		mutex_enter(&c->c_mutex);
		err = XDCMD(xdcbi, XD_READ, NOLPART, labhan, un->un_slave,
		    (daddr_t) 0, 1, XY_SYNCH, (xddebug)? 0 : XY_NOMSG);
		mutex_exit(&c->c_mutex);

		(void) ddi_dma_sync(labhan, 0, SECSIZE, DDI_DMA_SYNC_FORCPU);
		XDPUTCBI(xdcbi);

		/*
		 * If we found a label, attempt to digest it.
		 */
		found = 0;
		if (err == 0 && islabel(un, l) == DDI_SUCCESS) {
			uselabel(un, l);
			if (usegeom(un, XY_SYNCH) == DDI_SUCCESS)
				found = 1;
		}
		/*
		 * If we found the label, attempt to read the bad sector map.
		 */
		if (found) {
			daddr_t bn = (((un->un_g.dkg_ncyl + un->un_g.dkg_acyl) *
			    un->un_g.dkg_nhead) - 1) * un->un_g.dkg_nsect;
			xdcbi = XDGETCBI(c, 1, XY_SYNCH);

			mutex_enter(&c->c_mutex);
			err = XDCMD(xdcbi, XD_READ, NOLPART, labhan,
			    un->un_slave, bn, 1, XY_SYNCH, 0);
			mutex_exit(&c->c_mutex);
			(void) ddi_dma_sync(labhan, 0, SECSIZE,
			    DDI_DMA_SYNC_FORCPU);
			XDPUTCBI(xdcbi);

			if (err) {
				/*
				 * If we failed, print a message and invalidate
				 * the map in case it got destroyed in the read.
				 */
				cmn_err(CE_WARN,
				    "xd%d: unable to read bad sector info",
				    instance);
				mutex_enter(&un->un_c->c_mutex);
				for (i = 0; i < NDKBAD; i++)
					un->un_bad.bt_bad[i].bt_cyl
					    = (short) 0xFFFF;
				mutex_exit(&un->un_c->c_mutex);
			} else {
				un->un_bad = *(struct dkbad *)l;
			}
		} else {
			/*
			 * If we couldn't read the label, print a message and
			 * invalidate the label structures in case they got
			 * destroyed in the reads.
			 */
			cmn_err(CE_NOTE, "xd%d: cannot read label", instance);
			initlabel(un);
		}
		ddi_dma_free(labhan);
		ddi_iopb_free((caddr_t) l);
		for (dmdp = xd_minor_data; dmdp->name != NULL; dmdp++) {
			(void) ddi_prop_create(makedevice(DDI_MAJOR_T_UNKNOWN,
			    (instance << 3) | dmdp->minor),
			    dev, DDI_PROP_CANSLEEP, "nblocks",
			    (caddr_t)&un->un_map[dmdp->minor].dkl_nblk,
			    sizeof (un->un_map[dmdp->minor].dkl_nblk));
			if (ddi_create_minor_node(dev, dmdp->name, dmdp->type,
			    (instance << 3) | dmdp->minor,
			    DDI_NT_BLOCK, NULL) == DDI_FAILURE) {
				ddi_remove_minor_node(dev, NULL);
				mutex_destroy(&un->un_sbmutex);
				sema_destroy(&un->un_semoclose);
				ddi_set_driver_private(dev, (caddr_t) 0);
				ddi_soft_state_free(xd_state, instance);
				c->c_units[slave] = 0;
				return (DDI_FAILURE);
			}
		}

		/*
		 * Add a zero-length attribute to tell the world we support
		 * kernel ioctls (for layered drivers)
		 */
		(void) ddi_prop_create(DDI_DEV_T_NONE, dev, DDI_PROP_CANSLEEP,
		    DDI_KERNEL_IOCTL, NULL, 0);

		ddi_report_dev(dev);
		return (DDI_SUCCESS);

	default:
		/*
		 * Other values of 'cmd' are reserved and may be
		 * extended in future releases.  So we check to see
		 * that we're only responding for the case we've
		 * implemented here.
		 */
		return (DDI_FAILURE);
	}
}

/*
 * This routine initializes the unit label structures.  The logical partitions
 * are set to zero so normal opens will fail.  The geometry is set to
 * nonzero small numbers as a paranoid defense against zero divides.
 * Bad sector map is filled with non-entries.
 */
static void
initlabel(register struct xdunit *un)
{
	register int i;

	bzero((caddr_t)&un->un_map[0], sizeof (struct dk_map) * XYNLPART);
	bzero((caddr_t)&un->un_g, sizeof (struct dk_geom));
	un->un_g.dkg_ncyl = un->un_g.dkg_nhead = 8;
	un->un_g.dkg_nsect = 8;
	for (i = 0; i < NDKBAD; i++)
		un->un_bad.bt_bad[i].bt_cyl = (short) 0xFFFF;
}

/*
 * This routine verifies that the block read is indeed a disk label.  It
 * is used by doattach().  It is always called at disk interrupt priority.
 */
static int
islabel(struct xdunit *un, register struct dk_label *l)
{

	if (l->dkl_magic != DKL_MAGIC)
		return (DDI_FAILURE);
	if (!ck_cksum(l)) {
		cmn_err(CE_WARN, "xd%d: corrupt label",
		    ddi_get_instance(un->un_dip));
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

/*
 * This routine checks the checksum of the disk label.  It is used by
 * islabel().  It is always called at disk interrupt priority.
 */
static int
ck_cksum(register struct dk_label *l)
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
 * the unit structure.  It is used by doattach().  It is always called
 * at disk interrupt priority.
 */
static void
uselabel(register struct xdunit *un, register struct dk_label *l)
{
	int i;

	/*
	 * Print out the disk description.
	 * (not an error)
	 */
	cmn_err(CE_CONT, "?xd%d:\t<%s>\n", ddi_get_instance(un->un_dip),
	    l->dkl_asciilabel);

	/*
	 * Fill in the geometry information.
	 */
	un->un_g.dkg_ncyl = l->dkl_ncyl;
	un->un_g.dkg_acyl = l->dkl_acyl;
	un->un_g.dkg_bcyl = 0;
	un->un_g.dkg_nhead = l->dkl_nhead;
	un->un_g.dkg_nsect = l->dkl_nsect;
	un->un_g.dkg_intrlv = l->dkl_intrlv;
	un->un_g.dkg_rpm = l->dkl_rpm;
	un->un_g.dkg_pcyl = l->dkl_pcyl;
	un->un_g.dkg_write_reinstruct = l->dkl_write_reinstruct;
	un->un_g.dkg_read_reinstruct = l->dkl_read_reinstruct;

	/*
	 * Some labels might not have pcyl in them, so we make a guess at it.
	 */
	if (un->un_g.dkg_pcyl == 0)
		un->un_g.dkg_pcyl = un->un_g.dkg_ncyl + un->un_g.dkg_acyl;
	/*
	 * Fill in the logical partition map (structure assignments).
	 */
	for (i = 0; i < XYNLPART; i++)
		un->un_map[i] = l->dkl_map[i];

	/*
	 * Copy the vtoc info
	 */
	un->un_vtoc = l->dkl_vtoc;
	bcopy((caddr_t)l->dkl_asciilabel, (caddr_t)un->un_asciilabel,
	    LEN_DKL_ASCII);
}

/*
 * This routine is used to initialize the drive.  The 7053 requires
 * that each drive be set up once by sending a set drive parameter
 * command to the controller.  It is used by doattach() and xdioctl().
 */
static int
usegeom(register struct xdunit *un, int mode)
{
	daddr_t lastb;
	int err;
	register struct dk_geom *g = &un->un_g;
	register struct xdcmdblock *xdcbi;

	/*
	 * Just to be safe, we make sure we are initializing the drive
	 * to the larger of the two sizes, logical or physical.
	 */
	if (g->dkg_pcyl < (unsigned short) (g->dkg_ncyl + g->dkg_acyl))
		lastb = (g->dkg_ncyl + g->dkg_acyl) * g->dkg_nhead *
		    g->dkg_nsect - 1;
	else
		lastb = g->dkg_pcyl * g->dkg_nhead * g->dkg_nsect - 1;

	xdcbi = XDGETCBI(un->un_c, 1, mode);

	mutex_enter(&un->un_c->c_mutex);
	err = XDCMD(xdcbi, XD_WPAR | XD_DRIVE, NOLPART,
	    (ddi_dma_handle_t) XD_DRPARAM7053, un->un_slave,
	    lastb, (int)((g->dkg_nsect - 1) << 8), mode, 0);
	mutex_exit(&un->un_c->c_mutex);

	XDPUTCBI(xdcbi);

	if (err) {
		cmn_err(CE_WARN,
		    "xd%d: driver parameter initialization failed",
		    ddi_get_instance(un->un_dip));
		return (DDI_FAILURE);
	} else {
		return (DDI_SUCCESS);
	}
}

/*
 * UNIX entry points
 */
/*
 * This routine opens the device.  It is designed so that a disk can
 * be spun up after the system is running and an open will automatically
 * attach it as if it had been there all along.
 */
/*
 * NOTE: there is a synchronization issue that is not resolved here. If
 * a disk is spun up and two processes attempt to open it at the same time,
 * they may both attempt to attach the disk. This is extremely unlikely, and
 * non-fatal in most cases, but should be fixed.
 */
/* ARGSUSED3 */
static int
xdopen(dev_t *dev_p, int flag, int otyp, cred_t *cred_p)
{
	int part;
	register struct xdunit *un;
	register dev_t dev = *dev_p;

	un = ddi_get_soft_state(xd_state, INSTANCE(dev));

	/*
	 * If the disk is not present, we need to look for it.
	 * XXX: NOT DONE YET
	 */

	if (!un || (un->un_flags & XY_UN_PRESENT) == 0) {
		return (ENXIO);
	}
	sema_p(&un->un_semoclose);
	part = LPART(dev);

	/*
	 * By the time we get here the disk is marked present if it exists
	 * at all.  We simply check to be sure the partition being opened
	 * is nonzero in size.  If a raw partition is opened with the
	 * nodelay flag, we let it succeed even if the size is zero.  This
	 * allows ioctls to later set the geometry and partitions.
	 */

	if (un->un_map[part].dkl_nblk <= 0) {
		if ((flag & (FNDELAY|FNONBLOCK)) == 0 || otyp != OTYP_CHR) {
			sema_v(&un->un_semoclose);
			return (ENXIO);
		}
	}

	if (otyp >= OTYPCNT) {
		sema_v(&un->un_semoclose);
		return (EINVAL);
	} else if (otyp == OTYP_LYR) {
		un->un_ocmap.lyropen[part]++;
	} else {
		un->un_ocmap.regopen[otyp] |= 1<<part;
	}

	if (un->un_iostats == NULL) {
		if ((un->un_iostats = kstat_create("xd",
		    ddi_get_instance(un->un_dip), NULL, "disk",
		    KSTAT_TYPE_IO, 1, KSTAT_FLAG_PERSISTENT)) != NULL) {
			un->un_iostats->ks_lock = &un->un_c->c_mutex;
			kstat_install(un->un_iostats);
		}
	}
	sema_v(&un->un_semoclose);
	return (0);
}

/* ARGSUSED3 */
static int
xdclose(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	int part;
	register struct xdunit *un;

	un = ddi_get_soft_state(xd_state, INSTANCE(dev));
	if (!un)
		return (ENXIO);
	part = LPART(dev);
	sema_p(&un->un_semoclose);
	if (otyp == OTYP_LYR) {
		un->un_ocmap.lyropen[part] -= 1;
	} else {
		un->un_ocmap.regopen[otyp] &= ~(1<<part);
	}

	/*
	 * The only reason to check for 'totally' closed
	 * right now is to see whether it is time to
	 * remove iostat information.
	 */
	if (un->un_iostats) {
		register u_char *cp = &un->un_ocmap.chkd[0];
		while (cp < &un->un_ocmap.chkd[OCSIZE]) {
			if (*cp != (u_char) 0) {
				break;
			}
			cp++;
		}
		if (cp == &un->un_ocmap.chkd[OCSIZE]) {
			kstat_delete(un->un_iostats);
			un->un_iostats = 0;
		}
	}
	sema_v(&un->un_semoclose);
	return (0);
}

/*
 * Given the device number return the devinfo pointer.
 */
/* ARGSUSED */
static int
xdinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register dev_t dev;
	register int error;
	register struct xdunit *un;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		dev = (dev_t) arg;
		un = ddi_get_soft_state(xd_state, INSTANCE(dev));
		if (un  == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) un->un_dip;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		dev = (dev_t) arg;
		*result = (void *)INSTANCE(dev);
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

static int
xdrw(dev_t dev, struct uio *uio, int flag)
{
	struct xdunit *un = ddi_get_soft_state(xd_state, INSTANCE(dev));

	if (!un || (un->un_flags & XY_UN_PRESENT) == 0)
		return (ENXIO);
	return (physio(xdstrategy, (struct buf *) 0, dev, flag, xdmin, uio));
}

/* ARGSUSED2 */
static int
xdread(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	return (xdrw(dev, uio, B_READ));
}

/* ARGSUSED2 */
static int
xdwrite(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	return (xdrw(dev, uio, B_WRITE));
}

/*
 * This routine is the high level interface to the disk.  It performs
 * reads and writes on the disk using the buf as the method of communication.
 * It is called from the device switch for block operations, via physio()
 * for raw and control operations.
 */
static int
xdstrategy(register struct buf *bp)
{
	register struct xdunit *un;
	register struct xdctlr *c;

	un = ddi_get_soft_state(xd_state, INSTANCE(bp->b_edev));
	if ((un == (struct xdunit *) 0) ||
	    (bp != un->un_sbufp && (un->un_flags & XY_UN_PRESENT) == 0)) {
		bp->b_resid = bp->b_bcount;
		bp->b_error = ENXIO;
		bp->b_flags |= B_ERROR;
		biodone(bp);
		return (0);
	}

	if (bp != un->un_sbufp) {
		register struct dk_map *lp = &un->un_map[LPART(bp->b_edev)];
		register daddr_t bn = dkblock(bp);

		if (xddebug) {
			cmn_err(CE_CONT, "xd%d%c: bp %x blkno %d cnt %d",
			    ddi_get_instance(un->un_dip),
			    LPART(bp->b_edev) + 'a', bp, bn, bp->b_bcount);
			cmn_err(CE_CONT, " flags %x", bp->b_flags);
			if ((bp->b_flags & (B_PAGEIO|B_REMAPPED)) == B_PAGEIO)
				cmn_err(CE_CONT, " pages %x\n",
				    (int) bp->b_pages);
			else
				cmn_err(CE_CONT, " addr %x\n",
				    (int) bp->b_un.b_addr);
		}

		if (bn > lp->dkl_nblk || lp->dkl_nblk == 0) {
			bp->b_resid = bp->b_bcount;
			bp->b_error = ENXIO;
			bp->b_flags |= B_ERROR;
			biodone(bp);
			return (0);
		} else if (bn == lp->dkl_nblk) {
			bp->b_resid = bp->b_bcount;
			biodone(bp);
			return (0);
		}
	}

	/*
	 * We're playing with queues, so we lock out interrupts.
	 */

	bp->av_forw = NULL;
	c = un->un_c;
	mutex_enter(&c->c_mutex);

	if (un->un_iostats) {
		kstat_waitq_enter(KIOSP);
	}
	/*
	 * queue the buf - fifo, the controller sorts the disk operations now.
	 */
	if (c->c_waitqf == NULL) {
		c->c_waitqf = bp;
	} else {
		c->c_waitql->av_forw = bp;
	}
	c->c_waitql = bp;
	mutex_exit(&c->c_mutex);

	/*
	 * run the buf queue
	 */
	(void) XDSTART(c);

	/*
	 * return ignored value
	 */
	return (0);
}

/*
 * This routine implements the ioctl calls for the 7053.
 */
#define	COPYOUT(a, b, c, f)	\
	ddi_copyout((caddr_t) (a), (caddr_t) (b), sizeof (c), f)
#define	COPYIN(a, b, c, f)	\
	ddi_copyin((caddr_t) (a), (caddr_t) (b), sizeof (c), f)

/* ARGSUSED3 */
static int
xdioctl(dev_t dev, int cmd, int arg, int flag, cred_t *cred_p, int *rval_p)
{
	register struct xdunit *un;
	auto long data[512 / (sizeof (long))];
	struct dk_cinfo *info;
	struct hdk_diag *diag;
	struct hdk_badmap dkbm;
	struct vtoc vtoc;
	u_short write_reinstruct;
	u_short read_reinstruct;
	int i;

	un = ddi_get_soft_state(xd_state, INSTANCE(dev));
	if (!un) {
		return (ENXIO);
	}

	bzero((caddr_t) data, sizeof (data));

	if (xddebug) {
		cmn_err(CE_CONT, "xd%d: xdioctl cmd %x arg %x\n",
		    ddi_get_instance(un->un_dip), cmd, arg);
	}

	switch (cmd) {
	case DKIOCINFO:
		info = (struct dk_cinfo *)data;
		/*
		 * Controller Information
		 */
		info->dki_ctype = DKC_XD7053;
		info->dki_cnum = ddi_get_instance(un->un_c->c_dip);
		(void) strcpy(info->dki_cname,
		    ddi_get_name(ddi_get_parent(un->un_c->c_dip)));
		/*
		 * Unit Information
		 */
		info->dki_unit = ddi_get_instance(un->un_dip);
		info->dki_slave = un->un_slave;
		(void) strcpy(info->dki_dname, ddi_get_name(un->un_dip));
		info->dki_flags = DKI_BAD144 | DKI_FMTTRK;
		info->dki_partition = LPART(dev);
		info->dki_maxtransfer = XD_MAXBUFSIZE / DEV_BSIZE;

		/*
		 * We can't get from here to there yet
		 */
		info->dki_addr = 0;
		info->dki_space = 0;
		info->dki_prio = 0;
		info->dki_vec = 0;

		if (COPYOUT(data, arg, struct dk_cinfo, flag))
			return (EFAULT);
		break;

	case DKIOCGGEOM:
		/*
		 * If reinstruct times are zero, calculate them
		 * from rotational delay.
		 * XXX	Shouldn't we use some magic invalid value
		 *	rather than zero?
		 */
		write_reinstruct = un->un_g.dkg_write_reinstruct;
		read_reinstruct = un->un_g.dkg_read_reinstruct;
		if (write_reinstruct == 0) {
			un->un_g.dkg_write_reinstruct =
			    XD_WRITE_REINSTRUCT(un->un_g.dkg_nsect,
			    un->un_g.dkg_rpm);
		}
		if (read_reinstruct == 0) {
			un->un_g.dkg_read_reinstruct =
			    XD_READ_REINSTRUCT(un->un_g.dkg_nsect,
			    un->un_g.dkg_rpm);
		}

		/*
		 * Return the geometry of the specified unit.
		 */
		if (COPYOUT(&un->un_g, arg, struct dk_geom, flag)) {
			un->un_g.dkg_write_reinstruct = write_reinstruct;
			un->un_g.dkg_read_reinstruct = read_reinstruct;
			return (EFAULT);
		}

		/*
		 * Restore original reinstruct values
		 */
		un->un_g.dkg_write_reinstruct = write_reinstruct;
		un->un_g.dkg_read_reinstruct = read_reinstruct;
		break;

	case DKIOCSGEOM:
		/*
		 * Set the geometry of the specified unit.
		 */
		if (COPYIN(arg, data, struct dk_geom, flag))
			return (EFAULT);
		mutex_enter(&un->un_c->c_mutex);
		un->un_g = *(struct dk_geom *)data;
		mutex_exit(&un->un_c->c_mutex);
		if (usegeom(un, XY_ASYNCHWAIT))
			return (EINVAL);
		break;


	case DKIOCGAPART:
		/*
		 * Return the map for all logical partitions.
		 */
		i = NDKMAP * sizeof (struct dk_map);
		if (ddi_copyout((caddr_t) un->un_map, (caddr_t) arg, i, flag))
			return (EFAULT);
		break;

	case DKIOCSAPART:
		/*
		 * Set the map for all logical partitions.
		 */
		i = NDKMAP * sizeof (struct dk_map);
		if (ddi_copyin((caddr_t) arg, (caddr_t) data, i, flag))
			return (EFAULT);
		mutex_enter(&un->un_c->c_mutex);
		bcopy((caddr_t) data, (caddr_t) un->un_map, i);
		mutex_exit(&un->un_c->c_mutex);
		break;

	case DKIOCGVTOC:
		if (un->un_g.dkg_pcyl == 0)	/* Zero until geom set */
			return (EINVAL);
		mutex_enter(&un->un_c->c_mutex);
		xd_build_user_vtoc(un, &vtoc);
		mutex_exit(&un->un_c->c_mutex);

		if (COPYOUT(&vtoc, arg, struct vtoc, flag))
			return (EFAULT);
		else
			return (0);

	case DKIOCSVTOC:
		if (un->un_g.dkg_ncyl == 0)
			return (EINVAL);
		if (COPYIN(arg, &vtoc, struct vtoc, flag))
			return (EFAULT);

		mutex_enter(&un->un_c->c_mutex);
		if ((i = xd_build_label_vtoc(un, &vtoc)) == 0)
			i = xd_write_label(dev);
		mutex_exit(&un->un_c->c_mutex);
		return (i);

	case HDKIOCGDIAG:
		/*
		 * Get error status from last command.
		 */

		diag = (struct hdk_diag *) data;
		diag->hdkd_errsect = un->un_errsect;
		diag->hdkd_errno = un->un_errno;
		diag->hdkd_errcmd = un->un_errcmd;
		diag->hdkd_severe = un->un_errsevere;
		if (COPYOUT(data, arg, struct hdk_diag, flag))
			return (EFAULT);
		break;

	case HDKIOCGBAD:
		/*
		 * Get the bad sector map.
		 */
		if (COPYIN(arg, &dkbm, struct hdk_badmap, flag))
			return (EFAULT);
		else if (COPYOUT(&un->un_bad, dkbm.hdkb_bufaddr,
		    struct dkbad, flag))
			return (EFAULT);
		break;

	case HDKIOCSBAD:
		/*
		 * Set the bad sector map.
		 */
		if (COPYIN(arg, &dkbm, struct hdk_badmap, flag))
			return (EFAULT);
		if (COPYIN(dkbm.hdkb_bufaddr, data, struct dkbad, flag))
			return (EFAULT);
		mutex_enter(&un->un_c->c_mutex);
		bcopy((caddr_t) data, (caddr_t) &un->un_bad,
		    sizeof (struct dkbad));
		mutex_exit(&un->un_c->c_mutex);
		break;

	case HDKIOCGTYPE:
		/*
		 * Get the specific drive type
		 */
		return (xd_ugtyp(un, dev, arg, flag));

	case HDKIOCSCMD:
		if (COPYIN(arg, data, struct hdk_cmd, flag))
			return (EFAULT);
		/*
		 * Run a generic command.
		 */
		return (xd_ucmd(un, dev, (struct hdk_cmd *) data));

	default:
		/*
		 * This ain't no party, this ain't no disco.
		 */
		return (ENOTTY);
	}
	return (0);
}


static int
xd_ugtyp(struct xdunit *un, dev_t dev, int arg, int flag)
{
	register struct xdcmdblock *xdcbi;
	int err;
	struct hdk_type local, *typ = &local;

	bzero((caddr_t) typ, sizeof (struct hdk_type));

	typ->hdkt_drtype = 0;
	xdcbi = XDGETCBI(un->un_c, 0, XY_ASYNCHWAIT);
	mutex_enter(&un->un_c->c_mutex);
	err = XDCMD(xdcbi, XD_RPAR | XD_CTLR, getminor(dev),
	    (caddr_t)0, un->un_slave, (daddr_t)0, 0, XY_ASYNCHWAIT, 0);
	mutex_exit(&un->un_c->c_mutex);
	if (err) {
		XDPUTCBI(xdcbi);
		if (xddebug) {
			cmn_err(CE_CONT, "xd%d: xd_ugtyp- ctlr RPAR fails\n",
			    ddi_get_instance(un->un_dip));
		}
		return (EIO);
	}
	typ->hdkt_promrev = xdcbi->iopb->xd_promrev;

	mutex_enter(&un->un_c->c_mutex);
	err = XDCMD(xdcbi, XD_RPAR | XD_DRIVE, getminor(dev),
	    (caddr_t)0, un->un_slave, (daddr_t)0, 0, XY_ASYNCHWAIT, 0);
	mutex_exit(&un->un_c->c_mutex);
	if (err) {
		XDPUTCBI(xdcbi);
		if (xddebug) {
			cmn_err(CE_CONT, "xd%d: xd_ugtyp- drive RPAR fails\n",
			    ddi_get_instance(un->un_dip));
		}
		return (EIO);
	}
	typ->hdkt_hsect = xdcbi->iopb->xd_hsect;
	typ->hdkt_drstat = xdcbi->iopb->xd_dstat;
	XDPUTCBI(xdcbi);
	if (COPYOUT(typ, arg, struct hdk_type, flag))
		return (EFAULT);
	else
		return (0);
}

static int
xd_ucmd(struct xdunit *un, dev_t dev, struct hdk_cmd *com)
{
	struct hdk_cmd *newcom;
	register struct xdcmdblock *xdcbi;
	register struct buf *bp;
	int cmddir, err, exec, hsect, flags;
	auto struct iovec aiov;
	auto struct uio auio;
	register struct uio *uio = &auio;


	if (xddebug) {
		cmn_err(CE_CONT,
		    "xd%d: xd_ucmd- cmd %x blk %d sec %d buflen %d flags %x\n",
		    ddi_get_instance(un->un_dip), com->hdkc_cmd,
		    (int) com->hdkc_blkno, com->hdkc_secnt,
		    com->hdkc_buflen, com->hdkc_flags);
	}

	flags = 0;
	if (com->hdkc_flags & HDK_SILENT)
		flags |= XY_NOMSG;
	if (com->hdkc_flags & HDK_DIAGNOSE)
		flags |= XY_DIAG;
	if (com->hdkc_flags & HDK_ISOLATE)
		flags |= XY_NOCHN;

	if (xddebug)
		flags &= ~XY_NOMSG;

	newcom = (struct hdk_cmd *) 0;
	bp = (struct buf *) 0;
	hsect = err = exec = 0;

	/*
	 * Theser are the only commands we accept that do *not* imply
	 * user data DMA. We'll handle them inline here (since it is easy).
	 */

	if (com->hdkc_cmd == XD_RESTORE ||
	    com->hdkc_cmd == (XD_WEXT | XD_FORMVER)) {
		if (com->hdkc_buflen != 0) {
			err = EINVAL;
			goto errout;
		}
		if (com->hdkc_cmd != XD_RESTORE &&
		    (!ONTRACK(com->hdkc_blkno))) {
			err = EINVAL;
			goto errout;
		}
		xdcbi = XDGETCBI(un->un_c, 0, XY_ASYNCHWAIT);
		mutex_enter(&un->un_c->c_mutex);
		err = XDCMD(xdcbi, com->hdkc_cmd, getminor(dev), 0,
		    un->un_slave, (daddr_t) com->hdkc_blkno, com->hdkc_secnt,
		    XY_ASYNCHWAIT, flags);
		mutex_exit(&un->un_c->c_mutex);
		exec = 1;
		XDPUTCBI(xdcbi);
		if (err) {
			err = EIO;
		}
		goto errout;
	}

	/*
	 * Check the parameters for the rest of the commands.
	 */
	cmddir = XY_OUT;
	switch (com->hdkc_cmd) {
	case XD_READ:
		cmddir = XY_IN;
		/* FALLTHROUGH */
	case XD_WRITE:
		if (com->hdkc_buflen != (com->hdkc_secnt * SECSIZE)) {
			err = EINVAL;
			goto errout;
		}
		break;
	case XD_REXT | XD_DEFECT:	/* read (extended) defect map */
	case XD_REXT | XD_EXTDEF:
		cmddir = XY_IN;
		/* FALLTHROUGH */
	case XD_WEXT | XD_DEFECT:	/* write (extended) defect map */
	case XD_WEXT | XD_EXTDEF:
		if ((!ONTRACK(com->hdkc_blkno)) ||
		    (com->hdkc_buflen != XY_MANDEFSIZE)) {
			err = EINVAL;
			goto errout;
		}
		break;
	case XD_REXT | XD_THEAD:	/* read track headers */
		cmddir = XY_IN;
		/* FALLTHROUGH */
	case XD_WEXT | XD_THEAD:	/* write track headers */
		xdcbi = XDGETCBI(un->un_c, 0, XY_ASYNCHWAIT);

		mutex_enter(&un->un_c->c_mutex);
		err = XDCMD(xdcbi, XD_RPAR | XD_DRIVE, getminor(dev),
		    (caddr_t) 0, un->un_slave, (daddr_t) 0, 0,
		    XY_ASYNCHWAIT, (xddebug) ? 0 : XY_NOMSG);
		mutex_exit(&un->un_c->c_mutex);

		hsect = xdcbi->iopb->xd_hsect;
		XDPUTCBI(xdcbi);

		if (err) {
			err = EIO;
			goto errout;
		}
		if ((!ONTRACK(com->hdkc_blkno)) ||
		    (com->hdkc_buflen != hsect * XD_HDRSIZE)) {
			err = EINVAL;
			goto errout;
		}
		break;
	default:
		err = EINVAL;
		goto errout;
	}

	/*
	 * Don't allow more than max at once. Also,
	 * at this point, we have to be doing DMA.
	 */

	if (com->hdkc_buflen == 0 || com->hdkc_buflen > XD_MAXBUFSIZE)
		return (EINVAL);

	/*
	 * Get a copy of the dk_cmd structure
	 */

	newcom = (struct hdk_cmd *) kmem_alloc(sizeof (*newcom), KM_SLEEP);
	bcopy((caddr_t) com, (caddr_t) newcom, sizeof (struct hdk_cmd));

	/*
	 * Get the unit's special I/O buf.
	 */
	mutex_enter(&un->un_sbmutex);
	bp = un->un_sbufp;
	bp->b_forw = (struct buf *) newcom;

	bzero((caddr_t) &aiov, sizeof (struct iovec));
	bzero((caddr_t) uio, sizeof (*uio));
	aiov.iov_base = com->hdkc_bufaddr;
	aiov.iov_len = com->hdkc_buflen;
	uio->uio_iov = &aiov;
	uio->uio_iovcnt = 1;
	/*
	 * XXX: Should this be based on secnt, not buflen?
	 */
	uio->uio_resid = com->hdkc_buflen;
	uio->uio_segflg = UIO_USERSPACE;
	uio->uio_offset = (off_t) dbtob(com->hdkc_blkno);
	uio->uio_fmode = (cmddir == XY_IN)? FREAD: FWRITE;

	err = physio(xdstrategy, bp, dev,
	    (cmddir == XY_IN)? B_READ : B_WRITE, xdmin, uio);
	mutex_exit(&un->un_sbmutex);

	kmem_free((caddr_t) newcom, sizeof (struct hdk_cmd));
	exec = 1;
errout:
	if (exec == 0 && err && (com->hdkc_flags & HDK_DIAGNOSE)) {
		un->un_errsect = 0;
		un->un_errno = XDE_UNKN;
		un->un_errcmd = com->hdkc_cmd;
		un->un_errsevere = HDK_FATAL;
	}
	return (err);
}

/*
 * This routine dumps memory to the disk.
 */
/* ARGSUSED */
static int
xddump(dev_t dev, caddr_t addr, daddr_t blkno, int nblk)
{
	register struct xdunit *un = ddi_get_soft_state(xd_state,
	    INSTANCE(dev));
	register struct dk_map *lp;
	register struct xdcmdblock *xdcbi;
	ddi_dma_handle_t labhan;
	int unit;
	int err;

	if (!un) {
		return (ENXIO);
	}

	lp = &un->un_map[LPART(dev)];
	unit = un->un_slave;

	/*
	 * Check to make sure the operation makes sense.
	 */
	if (!(un->un_flags & XY_UN_PRESENT))
		return (ENXIO);
	if (blkno >= lp->dkl_nblk || (blkno + nblk) > lp->dkl_nblk)
		return (EINVAL);
	/*
	 * Offset into the correct partition.
	 */
	blkno += (lp->dkl_cylno + un->un_g.dkg_bcyl) * un->un_g.dkg_nhead *
		un->un_g.dkg_nsect;

	if ((xdcbi = XDGETCBI(un->un_c, 0, XY_SYNCH)) == NULL) {
		cmn_err(CE_WARN, "xd%d: can't get free xdcbi", INSTANCE(dev));
		return (EIO);
	}

	if (ddi_dma_addr_setup(un->un_c->c_dip, (struct as *) 0, addr,
				(nblk*SECSIZE), DDI_DMA_WRITE, DDI_DMA_SLEEP,
				(caddr_t) 0, un->un_c->c_lim, &labhan)) {
		cmn_err(CE_WARN, "xd%d: cannot map dump space", INSTANCE(dev));
		XDPUTCBI(xdcbi);
		return (DDI_FAILURE);
	}

	mutex_enter(&un->un_c->c_mutex);
	/*
	 * Synchronously execute the dump and return the status.
	 */
	err = XDCMD(xdcbi, XD_WRITE, getminor(dev), labhan,
		    unit, blkno, (int)nblk, XY_SYNCH, 0);
	mutex_exit(&un->un_c->c_mutex);

	XDPUTCBI(xdcbi);
	ddi_dma_free(labhan);

	if (err) {
		cmn_err(CE_WARN, "xd%d: dump failed", INSTANCE(dev));
		return (EIO);
	}
	return (0);
}

/*
 * This routine translates a buf oriented command down
 * to a level where it can actually be executed.
 */

static void
xdgo(register struct xdcmdblock *xdcbi)
{
	register struct xdunit *un = xdcbi->un;
	register struct buf *bp;
	int secnt, flags;
	u_short cmd;
	daddr_t blkno;

	flags = 0;

	if ((bp = xdcbi->breq) == un->un_sbufp) {
		struct hdk_cmd *com = (struct hdk_cmd *) bp->b_forw;

		if (com->hdkc_flags & HDK_SILENT)
			flags |= XY_NOMSG;
		if (com->hdkc_flags & HDK_DIAGNOSE)
			flags |= XY_DIAG;
		if (com->hdkc_flags & HDK_ISOLATE)
			flags |= XY_NOCHN;
		if (xddebug)
			flags &= ~XY_NOMSG;
		cmd = com->hdkc_cmd;
		blkno = com->hdkc_blkno;
		secnt = com->hdkc_secnt;

	} else {
		/*
		 * Calculate how many sectors we really want to operate
		 * on and set resid to reflect it.
		 */
		register struct dk_map *lp = &un->un_map[LPART(bp->b_edev)];
		secnt = howmany(bp->b_bcount, SECSIZE);
		secnt = min(secnt, lp->dkl_nblk - dkblock(bp));
		bp->b_resid = bp->b_bcount - secnt * SECSIZE;

		/*
		 * Calculate all the parameters needed to execute the command.
		 */
		if (bp->b_flags & B_READ)
			cmd = XD_READ;
		else
			cmd = XD_WRITE;
		blkno = dkblock(bp);
		blkno += lp->dkl_cylno * un->un_g.dkg_nhead *
		    un->un_g.dkg_nsect;
	}

	/*
	 * Execute the command.
	 */
	(void) XDCMD(xdcbi, cmd, getminor(bp->b_edev), xdcbi->handle,
	    un->un_slave, blkno, secnt, XY_ASYNCH, flags);
}

static void
xdmin(register struct buf *bp)
{
	if (bp->b_bcount > XD_MAXBUFSIZE)
		bp->b_bcount = XD_MAXBUFSIZE;
}

static void
xd_build_user_vtoc(struct xdunit *un, struct vtoc *vtoc)
{
	int i;
	long nblks;
	struct dk_map2 *lpart;
	struct dk_map	*lmap;
	struct partition *vpart;

	/*
	 * Return vtoc structure fields in the provided VTOC area, addressed
	 * by *vtoc.
	 */

	bzero((caddr_t) vtoc, sizeof (struct vtoc));

	bcopy((caddr_t) un->un_vtoc.v_bootinfo, (caddr_t) vtoc->v_bootinfo,
	    sizeof (vtoc->v_bootinfo));

	vtoc->v_sanity		= VTOC_SANE;
	vtoc->v_version		= un->un_vtoc.v_version;

	bcopy((caddr_t) un->un_vtoc.v_volume, (caddr_t) vtoc->v_volume,
	    LEN_DKL_VVOL);

	vtoc->v_sectorsz = SECSIZE;
	vtoc->v_nparts = un->un_vtoc.v_nparts;

	bcopy((caddr_t) un->un_vtoc.v_reserved, (caddr_t) vtoc->v_reserved,
	    sizeof (vtoc->v_reserved));
	/*
	 * Convert partitioning information.
	 *
	 * Note the conversion from starting cylinder number
	 * to starting sector number.
	 */
	lmap = un->un_map;
	lpart = un->un_vtoc.v_part;
	vpart = vtoc->v_part;

	nblks = un->un_g.dkg_nsect * un->un_g.dkg_nhead;

	for (i = 0; i < V_NUMPAR; i++) {
		vpart->p_tag	= lpart->p_tag;
		vpart->p_flag	= lpart->p_flag;
		vpart->p_start	= lmap->dkl_cylno * nblks;
		vpart->p_size	= lmap->dkl_nblk;

		lmap++;
		lpart++;
		vpart++;
	}

	bcopy((caddr_t) un->un_vtoc.v_timestamp, (caddr_t) vtoc->timestamp,
	    sizeof (vtoc->timestamp));

	bcopy((caddr_t) un->un_asciilabel, (caddr_t) vtoc->v_asciilabel,
	    LEN_DKL_ASCII);
}

static int
xd_build_label_vtoc(struct xdunit *un, struct vtoc *vtoc)
{
	struct dk_map		*lmap;
	struct dk_map2		*lpart;
	struct partition	*vpart;
	long			nblks;
	long			ncyl;
	int			i;

	/*
	 * Sanity-check the vtoc
	 */
	if (vtoc->v_sanity != VTOC_SANE || vtoc->v_sectorsz != SECSIZE ||
			vtoc->v_nparts != V_NUMPAR) {
		return (EINVAL);
	}

	nblks = un->un_g.dkg_nsect * un->un_g.dkg_nhead;

	vpart = vtoc->v_part;
	for (i = 0; i < V_NUMPAR; i++) {
		if ((vpart->p_start % nblks) != 0)
			return (EINVAL);
		ncyl = vpart->p_start / nblks;
		ncyl += vpart->p_size / nblks;
		if ((vpart->p_size % nblks) != 0)
			ncyl++;
		if (ncyl > (long)un->un_g.dkg_ncyl)
			return (EINVAL);
		vpart++;
	}


	/*
	 * Put appropriate vtoc structure fields into the disk label
	 *
	 */
	bcopy((caddr_t) vtoc->v_bootinfo, (caddr_t) un->un_vtoc.v_bootinfo,
	    sizeof (vtoc->v_bootinfo));

	un->un_vtoc.v_sanity = vtoc->v_sanity;
	un->un_vtoc.v_version = vtoc->v_version;

	bcopy((caddr_t) vtoc->v_volume, (caddr_t) un->un_vtoc.v_volume,
	    LEN_DKL_VVOL);

	un->un_vtoc.v_nparts = vtoc->v_nparts;

	bcopy((caddr_t) vtoc->v_reserved, (caddr_t) un->un_vtoc.v_reserved,
	    sizeof (vtoc->v_reserved));

	/*
	 * Note the conversion from starting sector number
	 * to starting cylinder number.
	 * Return error if division results in a remainder.
	 */
	lmap = un->un_map;
	lpart = un->un_vtoc.v_part;
	vpart = vtoc->v_part;


	for (i = 0; i < (int)vtoc->v_nparts; i++) {
		lpart->p_tag  = vtoc->v_part[i].p_tag;
		lpart->p_flag = vtoc->v_part[i].p_flag;
		lmap->dkl_cylno = vpart->p_start / nblks;
		lmap->dkl_nblk = vpart->p_size;

		lmap++;
		lpart++;
		vpart++;
	}

	bcopy((caddr_t) vtoc->timestamp, (caddr_t) un->un_vtoc.v_timestamp,
	    sizeof (vtoc->timestamp));

	bcopy((caddr_t) vtoc->v_asciilabel, (caddr_t) un->un_asciilabel,
	    LEN_DKL_ASCII);

	return (0);
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
xd_write_label(dev_t dev)
{
	struct xdunit *un = ddi_get_soft_state(xd_state, INSTANCE(dev));
	struct xdctlr *c = un->un_c;
	struct xdcmdblock *xdcbi;
	ddi_dma_handle_t labhan;
	struct dk_label *dkl;
	int i, err;
	int sec, blk, head, cyl;
	short sum, *sp;

	/*
	 * Allocate a temporary buffer in DVMA space for writing the label.
	 */
	if (ddi_iopb_alloc(c->c_dip, c->c_lim, SECSIZE, (caddr_t *)&dkl)) {
		cmn_err(CE_WARN, "xd%d: cannot allocate iopb space for label",
		    INSTANCE(dev));
		return (DDI_FAILURE);
	}
	if (ddi_dma_addr_setup(c->c_dip, (struct as *)0, (caddr_t)dkl,
	    SECSIZE, DDI_DMA_WRITE, DDI_DMA_SLEEP, (caddr_t)0,
	    c->c_lim, &labhan)) {
		cmn_err(CE_WARN, "xd%d: cannot map iopb label space",
		    INSTANCE(dev));
		ddi_iopb_free((caddr_t) dkl);
		return (DDI_FAILURE);
	}

	bzero((caddr_t) dkl, sizeof (struct dk_label));
	bcopy((caddr_t) un->un_asciilabel, (caddr_t) dkl->dkl_asciilabel,
		LEN_DKL_ASCII);
	bcopy((caddr_t) &un->un_vtoc, (caddr_t) &(dkl->dkl_vtoc),
		sizeof (struct dk_vtoc));

	dkl->dkl_rpm	= un->un_g.dkg_rpm;
	dkl->dkl_pcyl	= un->un_g.dkg_pcyl;
	dkl->dkl_apc	= un->un_g.dkg_apc;
	dkl->dkl_obs1	= un->un_g.dkg_obs1;
	dkl->dkl_obs2	= un->un_g.dkg_obs2;
	dkl->dkl_intrlv	= un->un_g.dkg_intrlv;
	dkl->dkl_ncyl	= un->un_g.dkg_ncyl;
	dkl->dkl_acyl	= un->un_g.dkg_acyl;
	dkl->dkl_nhead	= un->un_g.dkg_nhead;
	dkl->dkl_nsect	= un->un_g.dkg_nsect;
	dkl->dkl_obs3	= un->un_g.dkg_obs3;

	bcopy((caddr_t) un->un_map, (caddr_t) dkl->dkl_map,
		NDKMAP * sizeof (struct dk_map));

	dkl->dkl_magic			= DKL_MAGIC;
	dkl->dkl_write_reinstruct	= un->un_g.dkg_write_reinstruct;
	dkl->dkl_read_reinstruct	= un->un_g.dkg_read_reinstruct;

	/*
	 * Construct checksum for the new disk label
	 */
	sum = 0;
	sp = (short *) dkl;
	i = sizeof (struct dk_label)/sizeof (short);
	while (i--) {
		sum ^= *sp++;
	}
	dkl->dkl_cksum = sum;


	/*
	 * Write the label
	 */
	mutex_exit(&un->un_c->c_mutex);
	xdcbi = XDGETCBI(un->un_c, 0, XY_ASYNCHWAIT);
	mutex_enter(&un->un_c->c_mutex);
	(void) ddi_dma_sync(labhan, 0, SECSIZE, DDI_DMA_SYNC_FORDEV);
	err = XDCMD(xdcbi, XD_WRITE, NOLPART, labhan, un->un_slave,
	    (daddr_t) 0, 1, XY_ASYNCHWAIT, (xddebug)? 0 : XY_NOMSG);

	if (err) {
		cmn_err(CE_WARN, "xd%d: cannot write label", INSTANCE(dev));
		mutex_exit(&un->un_c->c_mutex);
		XDPUTCBI(xdcbi);
		mutex_enter(&un->un_c->c_mutex);
		ddi_dma_free(labhan);
		ddi_iopb_free((caddr_t) dkl);
		return (DDI_FAILURE);
	}

	/*
	 * Calculate where the backup labels go.  They are always on
	 * the last alternate cylinder, but some older drives put them
	 * on head 2 instead of the last head.  They are always on the
	 * first 5 odd sectors of the appropriate track.
	 *
	 * We have no choice at this point, but to believe that the
	 * disk label is valid.  Use the geometry of the disk
	 * as described in the label.
	 */
	cyl = dkl->dkl_ncyl + dkl->dkl_acyl - 1;
	head = dkl->dkl_nhead-1;

	/*
	 * Write and verify the backup labels.
	 */
	for (sec = 1; sec < 5 * 2 + 1; sec += 2) {
		blk = chs2bn(dkl, cyl, head, sec);
		err = XDCMD(xdcbi, XD_WRITE, NOLPART, labhan, un->un_slave,
		    (daddr_t) blk, 1, XY_ASYNCHWAIT, (xddebug)? 0 : XY_NOMSG);

		if (err)
			cmn_err(CE_WARN,
				"xd%d: cannot write backup label at %d",
				INSTANCE(dev), blk);
	}
	mutex_exit(&un->un_c->c_mutex);
	XDPUTCBI(xdcbi);
	mutex_enter(&un->un_c->c_mutex);
	ddi_dma_free(labhan);
	ddi_iopb_free((caddr_t) dkl);

	return (DDI_SUCCESS);

}
