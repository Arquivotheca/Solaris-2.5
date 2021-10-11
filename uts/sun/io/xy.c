/*
 * Copyright (c) 1987-1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)xy.c	1.27	94/08/08 SMI"

/*
 * Driver for Xylogics 450/451 SMD disk controllers
 */

/*
 * WARNING: This driver is still in the process of being debugged/enhanced.
 * Only Read and Writes have been tested, with 3 processes running
 * concurrently.
 * there is an internal trace to buffer xy_trace, this will be needed for
 * post-mortem debugging. Please, do not turn off this internal trace.
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>	/* Hmm.. */
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

#include <sys/xyvar.h>
#include <sys/xyerr.h>

/*
 * Local defines
 */
#define	KIOSP		KSTAT_IO_PTR(un->un_iostats)

/*
 * Function Prototypes
 */

static int xy_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
    void **result);
static int xyidentify(dev_info_t *);
static int xyprobe(dev_info_t *);
static int xyattach(dev_info_t *, ddi_attach_cmd_t);
static void initlabel(struct xyunit *);
static int islabel(struct xyunit *, struct dk_label *);
static int ck_cksum(struct dk_label *);
static void uselabel(struct xyunit *, struct dk_label *);
static int usegeom(struct xyunit *, int);

static int xyopen(dev_t *, int, int, cred_t *);
static int xyclose(dev_t dev, int flag, int otyp, cred_t *cred_p);
static int xyrw(dev_t, struct uio *, int);
static int xyread(dev_t, struct uio *, cred_t *);
static int xywrite(dev_t, struct uio *, cred_t *);
static int xystrategy(struct buf *);
static int xyioctl(dev_t, int, int, int, cred_t *, int *);
static int xy_ugtyp(struct xyunit *, dev_t, int, int);
static int xy_ustyp(struct xyunit *, dev_t, int, int);
static int xy_ucmd(struct xyunit *, dev_t, struct hdk_cmd *);
static int xydump(dev_t, caddr_t, daddr_t, int);

static void xygo(struct xycmdblock *);

static void xy_build_user_vtoc(struct xyunit *un, struct vtoc *vtoc);
static int xy_build_label_vtoc(struct xyunit *un, struct vtoc *vtoc);
static int xy_write_label(dev_t dev);

static void xymin(struct buf *);
static void xxprintf(char *fmt, ...);

/*
 * Debugging macros
 */

#define	PRINTFN if (0) printf
#define	TRACE_XY if (xy_debug == 5)
#define	TRACE_NN if (xy_debug == 20000)

/*
 * Declarations for wrap-around trace
 */

#define	PRINTF1 if (xy_debug)	  xxprintf
#define	PRINTF2 if (xy_debug > 1) xxprintf
#define	PRINTF3 if (xy_debug > 2) xxprintf
#define	PRINTF4 if (xy_debug > 3) xxprintf

/*
 *
 *		Adjustable parameters
 *		at compile time
 *
 * MAXLINESIZE = upper limit for one sprintf (increase if needed)
 * TRACE_SIZE  = Size in bytes of the whole trace (increase if needed)
 * xy_debug    = level of tracing (0 no tracing, 4 maximum tracing).
 *
 *
 *		Adjustable parameters
 *		at run time
 *
 * trace_mode  = 0 for no trace
 *		 1 internal trace (wrap around	 in trace buffer)
 *		 2 internal and external trace.
 */

#define		MAXLINESIZE	120
#define		TRACE_SIZE	2048
#define		RED_ZONE	20

#ifdef	DEBUG
#define	XYDEBUG
#endif

#if	defined(XYDEBUG) || defined(lint)
static	int	xy_debug = 0;
static	int	trace_mode = 1;
static	int	to_trace = 0;
static	int	itrace;
static	char	xy_trace[TRACE_SIZE + RED_ZONE];
kmutex_t	xy_trace_mutex;

#else
#define	xy_debug 0

static	void
xxprintf(char *fmt, ...)
{

}
#endif

/*
 * Flags value that turns off command chaining
 */
#define	XY_NOCHAINING	0x1

/*
 * Autoconfiguration data
 */

static void *xy_state;

static struct driver_minor_data {
	char    *name;
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

static struct cb_ops xy_cb_ops = {

	xyopen,			/* open */
	xyclose, 		/* close */
	xystrategy, 		/* strategy */
	nodev, 			/* print */
	xydump,			/* dump */
	xyread,			/* read */
	xywrite, 		/* write */
	xyioctl, 		/* ioctl */
	nodev, 			/* devmap */
	nodev, 			/* mmap */
	nodev, 			/* segmap */
	nochpoll, 		/* poll */
	ddi_prop_op, 		/* cb_prop_op */
	0, 			/* streamtab  */
	D_NEW | D_MP		/* Driver compatibility flag */

};

static struct dev_ops xy_ops = {
	DEVO_REV, 		/* devo_rev, */
	0, 			/* refcnt  */
	xy_info,		/* get_dev_info */
	xyidentify, 		/* identify */
	xyprobe, 		/* probe */
	xyattach, 		/* attach */
	nodev, 			/* detach */
	nodev, 			/* reset */
	&xy_cb_ops, 		/* driver operations */
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
	&mod_driverops, /* Type of module.  This one is a driver */
	"Xylogics 450/451 SMD Disk Controller",
	&xy_ops, 	/* driver ops */
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

#ifdef	XYDEBUG
	if (xy_debug)
		mutex_init(&xy_trace_mutex, "xy_trace",
		    MUTEX_DRIVER, (void *) 0);
#endif

	if ((e = ddi_soft_state_init(&xy_state,
	    sizeof (struct xyunit), 1)) != 0) {
		return (e);
	}

	if ((e = mod_install(&modlinkage)) != 0) {
		ddi_soft_state_fini(&xy_state);
	}
	return (e);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	register int e;

	if ((e = mod_remove(&modlinkage)) != 0) {
		return (e);
	}
	ddi_soft_state_fini(&xy_state);
	return (e);
}


/*
 * Autoconfiguration Routines
 */

static int
xyidentify(dev_info_t *dev)
{
	char *name = ddi_get_name(dev);

	/*
	 * This module now only drives "xy" devices
	 */

	if (strcmp(name, "xy") == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

static int
xyprobe(dev_info_t *dev)
{
	register err;
	register struct xyctlr *c;
	register struct xyunit *un;
	register struct xycmdblock *xycbi;
	register int instance = ddi_get_instance(dev);
	register int ctlr_instance;
	int s_len, slave;

	PRINTF3("\n...xy_unit_probe  ");

	c = (struct xyctlr *) ddi_get_driver_private(ddi_get_parent(dev));
	ctlr_instance = ddi_get_instance(c->c_dip);
	if (xy_debug) {
		cmn_err(CE_CONT, "xyprobe instance %d par %d\n", instance,
		    ctlr_instance);
	}
	s_len = sizeof (slave);
	if (ddi_prop_op(DDI_DEV_T_NONE, dev, PROP_LEN_AND_VAL_BUF,
	    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "slave", (caddr_t)&slave,
	    &s_len) != DDI_SUCCESS || slave > XYUNPERC)
		return (DDI_PROBE_FAILURE);

	if (ddi_soft_state_zalloc(xy_state, instance) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "xy%d cannot alloc softstate",
		    instance);
		return (DDI_PROBE_FAILURE);
	}
	un = ddi_get_soft_state(xy_state, instance);
	ASSERT(un != NULL);
	c->c_units[slave] = un;
	un->un_dip = dev;
	un->un_c = c;
	un->un_slave = (u_char) slave;
	un->un_instance = instance;
	un->un_go = xygo;
	ddi_set_driver_private(dev, (caddr_t) un);

	if ((xycbi = XYGETCBI(c, 1, XY_SYNCH)) == (struct xycmdblock *) 0) {
		cmn_err(CE_WARN, "xyc%d: cannot get cbi to probe unit",
		    ctlr_instance);
		ddi_soft_state_free(xy_state, instance);
		c->c_units[slave] = 0;
		ddi_set_driver_private(dev, (caddr_t) 0);
		return (DDI_PROBE_FAILURE);
	}

	mutex_enter(&c->c_mutex);
	err = XYCMD(xycbi, XY_RESTORE, NOLPART, 0,
	    slave, 0, 0, XY_SYNCH, (xy_debug) ? 0 : XY_NOMSG);
	mutex_exit(&c->c_mutex);

	XYPUTCBI(xycbi);
	/*
	 * "Probe shall be stateless" (probe(9e)).
	 */
	ddi_set_driver_private(dev, (caddr_t) 0);
	ddi_soft_state_free(xy_state, instance);
	c->c_units[slave] = 0;
	if (err == 0) {
		if (xy_debug)
			cmn_err(CE_CONT, "struct c=0x%x  u=0x%x\n",
				(int) c, (int) un);
		return (DDI_PROBE_SUCCESS);
	}
	return (DDI_PROBE_FAILURE);
}

static int
xyattach(dev_info_t *dev, ddi_attach_cmd_t cmd)
{
	register struct xyctlr *c;
	register struct xyunit *un;
	auto struct dk_label *l;
	ddi_dma_handle_t labhan;
	register struct xycmdblock *xycbi;
	int err, found, i;
	int s_len, slave;
	struct driver_minor_data *dmdp;
	register int instance = ddi_get_instance(dev);

	PRINTF3("\n...xy_attach_unit  ");
	if (xy_debug) {
		cmn_err(CE_CONT, "xyattach: xy%d\n", instance);
	}

	switch (cmd) {

	case DDI_ATTACH:

		c = (struct xyctlr *)ddi_get_driver_private(
		    ddi_get_parent(dev));
		s_len = sizeof (slave);
		if (ddi_prop_op(DDI_DEV_T_NONE, dev, PROP_LEN_AND_VAL_BUF,
		    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "slave",
		    (caddr_t)&slave, &s_len) != DDI_SUCCESS || slave > XYUNPERC)
			return (DDI_FAILURE);

		if (ddi_soft_state_zalloc(xy_state, instance) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "xy%d cannot alloc softstate",
			    instance);
			return (DDI_FAILURE);
		}
		un = ddi_get_soft_state(xy_state, instance);
		ASSERT(un != NULL);
		c->c_units[slave] = un;
		un->un_dip = dev;
		un->un_c = c;
		un->un_slave = (u_char) slave;
		un->un_instance = instance;
		un->un_go = xygo;
		ddi_set_driver_private(dev, (caddr_t) un);

		un->un_flags |= XY_UN_ATTACHED;
		un->un_sbufp = getrbuf(1);
		sema_init(&un->un_semoclose, 1, "xy_o", SEMA_DRIVER,
		    (void *)c->c_ibc);
		mutex_init(&un->un_sbmutex, "xy_s", MUTEX_DRIVER,
		    (void *) c->c_ibc);

		/*
		 * Initialize the label structures.  This is necessary so weird
		 * entries in the bad sector map don't bite us while reading the
		 * label. Also, this MUST come before the write parameters
		 * command so the geometry is not random.
		 */
		initlabel(un);

		/*
		 * Allocate a temporary buffer in DVMA space for reading the
		 * label.
		 */
		if (ddi_iopb_alloc(c->c_dip, c->c_lim, SECSIZE,
		    (caddr_t *) &l)) {
			cmn_err(CE_WARN,
			    "xy%d: cannot allocate iopb space for label",
			    instance);
			mutex_destroy(&un->un_sbmutex);
			sema_destroy(&un->un_semoclose);
			ddi_set_driver_private(dev, (caddr_t) 0);
			ddi_soft_state_free(xy_state, instance);
			c->c_units[slave] = 0;
			return (DDI_FAILURE);
		}

		if (ddi_dma_addr_setup(c->c_dip, (struct as *) 0, (caddr_t) l,
		    SECSIZE, DDI_DMA_READ, DDI_DMA_SLEEP, (caddr_t) 0,
		    c->c_lim, &labhan)) {
			cmn_err(CE_WARN, "xy%d: cannot map iopb label space",
			    instance);
			mutex_destroy(&un->un_sbmutex);
			sema_destroy(&un->un_semoclose);
			ddi_iopb_free((caddr_t) l);
			ddi_set_driver_private(dev, (caddr_t) 0);
			ddi_soft_state_free(xy_state, instance);
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

		for (un->un_drtype = 0; un->un_drtype < NXYDRIVE;
		    un->un_drtype++) {
			l->dkl_magic = 0;	/* reset from last try */
			xycbi = XYGETCBI(c, 1, XY_SYNCH);

			mutex_enter(&c->c_mutex);
			err = XYCMD(xycbi, XY_READ, NOLPART, labhan,
			    un->un_slave, (daddr_t) 0, 1, XY_SYNCH,
			    (xy_debug)? 0 : XY_NOMSG);
			mutex_exit(&c->c_mutex);

			(void) ddi_dma_sync(labhan, 0, SECSIZE,
			    DDI_DMA_SYNC_FORCPU);
			XYPUTCBI(xycbi);

			/*
			 * If we found a label, attempt to digest it.
			 */
			found = 0;
			if (err == 0 && islabel(un, l) == DDI_SUCCESS) {
				uselabel(un, l);
				if (usegeom(un, XY_SYNCH) == DDI_SUCCESS) {
					found = 1;
					PRINTF3("\t\tF O U N D	L A B E L\n");
					break;
				}
			}
		}

		/*
		 * If we found the label, attempt to read the bad sector map.
		 */

		if (found) {
			daddr_t bn = (((un->un_g.dkg_ncyl + un->un_g.dkg_acyl) *
			    un->un_g.dkg_nhead) - 1) * un->un_g.dkg_nsect;
			xycbi = XYGETCBI(c, 1, XY_SYNCH);

			mutex_enter(&c->c_mutex);
			err = XYCMD(xycbi, XY_READ, NOLPART, labhan,
			    un->un_slave, bn, 1, XY_SYNCH, 0);
			mutex_exit(&c->c_mutex);
			(void) ddi_dma_sync(labhan, 0, SECSIZE,
			    DDI_DMA_SYNC_FORCPU);
			XYPUTCBI(xycbi);

			if (err) {
				/*
				 * If we failed, print a message and invalidate
				 * the map in case it got destroyed in the read.
				 */
				cmn_err(CE_WARN,
				    "xy%d: unable to read bad sector info",
				    instance);
				mutex_enter(&c->c_mutex);
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
			cmn_err(CE_NOTE, "xy%d: cannot read label", instance);
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
				ddi_soft_state_free(xy_state, instance);
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
		cv_init(&c->c_cw, "xyintr", CV_DRIVER, (void *)c->c_ibc);
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

/*
 * This routine initializes the unit label structures.	The logical partitions
 * are set to zero so normal opens will fail.  The geometry is set to
 * nonzero small numbers as a paranoid defense against zero divides.
 * Bad sector map is filled with non-entries.
 */
static void
initlabel(register struct xyunit *un)
{
	register int i;

	PRINTF3("\n...initlabel	 ");
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
islabel(struct xyunit *un, register struct dk_label *l)
{

	PRINTF3("\n...islabel  ");
	if (l->dkl_magic != DKL_MAGIC) {
		cmn_err(CE_WARN, "xy%d: dkl_magic = %x",
		    ddi_get_instance(un->un_dip), l->dkl_magic);
		return (DDI_FAILURE);
	}
	if (!ck_cksum(l)) {
		cmn_err(CE_WARN, "xy%d: corrupt label",
		    ddi_get_instance(un->un_dip));
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

/*
 * This routine checks the checksum of the disk label.	It is used by
 * islabel().  It is always called at disk interrupt priority.
 */
static int
ck_cksum(register struct dk_label *l)
{
	register short *sp, sum = 0;
	register short count = sizeof (struct dk_label)/sizeof (short);

	PRINTF3("\n...ck_cksum	");
	sp = (short *)l;
	while (count--)
		sum ^= *sp++;
	return (sum ? 0 : 1);
}

/*
 * This routine puts the label information into the various parts of
 * the unit structure.	It is used by doattach().  It is always called
 * at disk interrupt priority.
 */
static void
uselabel(register struct xyunit *un, register struct dk_label *l)
{
	int i;

	PRINTF3("\n...uselabel	");

	/*
	 * Print out the disk description.
	 * (not an error)
	 */
	cmn_err(CE_CONT, "?xy%d:\t<%s>\n", ddi_get_instance(un->un_dip),
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
 * This routine is used to initialize the drive. The 451 requires
 * that each drive be set up once by sending a set drive parameter
 * command to the controller.  It is used by doattach() and xyioctl().
 */
static int
usegeom(register struct xyunit *un, int mode)
{
	daddr_t lastb;
	int err, unit;
	register struct dk_geom *g = &un->un_g;
	register struct xycmdblock *xycbi;
	register struct dk_geom *ng;
	register struct xyunit *nun;
	register struct xyctlr *c = un->un_c;

	PRINTF3("\n...usegeom  ");


	/*
	 * Search for other disks of the same type on this
	 * controller.	If found, they must have the same
	 * geometry or we are stuck.
	 */
	for (unit = 0; unit < XYUNPERC; unit++) {
		if ((nun = c->c_units[unit]) == NULL || un == nun)
			continue;
		if (!(nun->un_flags & XY_UN_PRESENT))
			continue;
		if (nun->un_drtype != un->un_drtype)
			continue;
		ng = &nun->un_g;
		if ((g->dkg_ncyl + g->dkg_acyl != ng->dkg_ncyl +
		    ng->dkg_acyl) || g->dkg_nhead != ng->dkg_nhead ||
		    g->dkg_nsect != ng->dkg_nsect) {
			cmn_err(CE_WARN,
			"xy%d and xy%d same type (%d) but different geometries",
			    ddi_get_instance(un->un_dip),
			    ddi_get_instance(nun->un_dip),
			    un->un_drtype);
			return (DDI_FAILURE);
		}
		return (DDI_SUCCESS);
	}


	/*
	 * Just to be safe, we make sure we are initializing the drive
	 * to the larger of the two sizes, logical or physical.
	 */
	if (g->dkg_pcyl < (unsigned short) (g->dkg_ncyl + g->dkg_acyl))
		lastb = (g->dkg_ncyl + g->dkg_acyl) * g->dkg_nhead *
		    g->dkg_nsect - 1;
	else
		lastb = g->dkg_pcyl * g->dkg_nhead * g->dkg_nsect - 1;

	xycbi = XYGETCBI(un->un_c, 1, mode);

	mutex_enter(&c->c_mutex);
	err = XYCMD(xycbi, XY_INIT, NOLPART,
	    (ddi_dma_handle_t) 0, un->un_slave,
	    lastb, 0, mode, 0);
	mutex_exit(&c->c_mutex);

	XYPUTCBI(xycbi);

	if (err) {
		cmn_err(CE_WARN,
		    "xy%d: driver parameter initialization failed",
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
xyopen(dev_t *dev_p, int flag, int otyp, cred_t *cred_p)
{
	int part;
	register struct xyunit *un;
	register dev_t dev = *dev_p;

	un = ddi_get_soft_state(xy_state, INSTANCE(dev));
	PRINTF3("\n...xyopen  ");

	/*
	 * If the disk is not present, we need to look for it.
	 * XXX mj?: NOT DONE YET
	 */

	if (!un || (un->un_flags & XY_UN_PRESENT) == 0) {
		return (ENXIO);
	}
	sema_p(&un->un_semoclose);
	part = LPART(dev);

	/*
	 * By the time we get here the disk is marked present if it exists
	 * at all.  We simply check to be sure the partition being opened
	 * is nonzero in size.	If a raw partition is opened with the
	 * nodelay flag, we let it succeed even if the size is zero.  This
	 * allows ioctls to later set the geometry and partitions.
	 */

	if (un->un_map[part].dkl_nblk <= 0) {
		if ((flag & (FNDELAY|FNONBLOCK)) == 0 || !(otyp & OTYP_CHR)) {
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
		if ((un->un_iostats = kstat_create("xy",
		    ddi_get_instance(un->un_dip), NULL, "disk",
		    KSTAT_TYPE_IO, 1, KSTAT_FLAG_PERSISTENT)) != NULL) {
			un->un_iostats->ks_lock = &un->un_c->c_mutex;
			kstat_install(un->un_iostats);
		}
	}
	sema_v(&un->un_semoclose);
	return (0);
}

static int
xyclose(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	int part;
	register struct xyunit *un;

#ifdef	lint
	flag = flag;
	cred_p = cred_p;
#endif
	PRINTF3("\n...xyclose  ");
	un = ddi_get_soft_state(xy_state, INSTANCE(dev));
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

static int
xy_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register dev_t dev;
	register int error;
	struct xyunit *un;

#ifdef	lint
	dip = dip;
#endif
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		dev = (dev_t) arg;
		un = ddi_get_soft_state(xy_state, INSTANCE(dev));
		if (un == NULL) {
			*result = (void *)NULL;
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
xyrw(dev_t dev, struct uio *uio, int flag)
{
	struct xyunit *un = ddi_get_soft_state(xy_state, INSTANCE(dev));
	PRINTF3("\n...xyrw  ");

	if (!un || (un->un_flags & XY_UN_PRESENT) == 0)
		return (ENXIO);
		PRINTF3("\t\t>>> rw: uio offset	 %x\n", uio->uio_offset);
		PRINTF3("\t\t>>> rw: uio resid	 %x\n", uio->uio_resid);
	return (physio(xystrategy, (struct buf *) 0, dev, flag, xymin, uio));
}

/* ARGSUSED2 */
static int
xyread(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	return (xyrw(dev, uio, B_READ));
}

/* ARGSUSED2 */
static int
xywrite(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	return (xyrw(dev, uio, B_WRITE));
}

/*
 * This routine is the high level interface to the disk.  It performs
 * reads and writes on the disk using the buf as the method of communication.
 * It is called from the device switch for block operations, via physio()
 * for raw and control operations.
 */
static int
xystrategy(register struct buf *bp)
{
	register struct xyunit *un;
	register struct xyctlr *c;

	PRINTF3("\n...xystrategy  ");
	un = ddi_get_soft_state(xy_state, INSTANCE(bp->b_edev));
	if ((un == (struct xyunit *) 0) ||
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

		if (xy_debug > 3) {
			cmn_err(CE_CONT, "xy%d%c: bp %x blkno %d cnt %d",
			    ddi_get_instance(un->un_dip),
			    (int) LPART(bp->b_edev) + 'a', (int) bp,
			    (int) bn, bp->b_bcount);
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
	 * queue the buf - fifo, the controller sorts disk operations now.
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
	(void) XYSTART(c);

	/*
	 * return ignored value
	 */
	return (0);
}

/*
 * This routine implements the ioctl calls for the 451.
 */
#define	COPYOUT(a, b, c, f)	\
	ddi_copyout((caddr_t) (a), (caddr_t) (b), sizeof (c), f)
#define	COPYIN(a, b, c, f)	\
	ddi_copyin((caddr_t) (a), (caddr_t) (b), sizeof (c), f)

/* ARGSUSED3 */
static int
xyioctl(dev_t dev, int cmd, int arg, int flag, cred_t *cred_p, int *rval_p)
{
	register struct xyunit *un;
	auto long data[512 / (sizeof (long))];
	struct dk_map *lp;
	struct dk_cinfo *info;
	struct hdk_diag *diag;
	struct hdk_badmap dkbm;
	struct vtoc vtoc;
	u_short write_reinstruct;
	u_short read_reinstruct;
	int i;

	PRINTF3("\n...xyioctl  ");
	un = ddi_get_soft_state(xy_state, INSTANCE(dev));
	if (!un) {
		return (ENXIO);
	}

	lp = &un->un_map[LPART(dev)];
	bzero((caddr_t) data, sizeof (data));

	PRINTF3("xy%d: xyioctl cmd %x arg %x\n",
	    ddi_get_instance(un->un_dip), cmd, arg);

	switch (cmd) {
	case DKIOCINFO:
		info = (struct dk_cinfo *)data;
		/*
		 * Controller Information
		 */
		info->dki_ctype = DKC_XY450;
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
		info->dki_maxtransfer = XY_MAXBUFSIZE / DEV_BSIZE;

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
			    XY_WRITE_REINSTRUCT(un->un_g.dkg_nsect,
			    un->un_g.dkg_rpm);
		}
		if (read_reinstruct == 0) {
			un->un_g.dkg_read_reinstruct =
			    XY_READ_REINSTRUCT(un->un_g.dkg_nsect,
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
		if (usegeom(un, XY_ASYNCHWAIT) != DDI_SUCCESS) {
			return (EINVAL);
		}
		break;

/* XXX both should go away when sundiag is fixed ! */

#define	DKIOCGPART (DIOC | 4)
#define	DKIOCSPART (DIOC | 5)

	case DKIOCGPART:
		/*
		 * Return the map for the specified logical partition.
		 */
		if (COPYOUT(lp, arg, struct dk_map, flag))
			return (EFAULT);
		break;

	case DKIOCSPART:
		/*
		 * Set the map for the specified logical partition.
		 */
		if (COPYIN(arg, data, struct dk_map, flag))
			return (EFAULT);
		mutex_enter(&un->un_c->c_mutex);
		*lp = *(struct dk_map *)data;
		mutex_exit(&un->un_c->c_mutex);
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
		if (un->un_g.dkg_pcyl == 0) 	/* Zero until geom set */
			return (EINVAL);
		mutex_enter(&un->un_c->c_mutex);
		xy_build_user_vtoc(un, &vtoc);
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
		if ((i = xy_build_label_vtoc(un, &vtoc)) == 0)
			i = xy_write_label(dev);
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
		return (xy_ugtyp(un, dev, arg, flag));

	case HDKIOCSTYPE:
		/*
		 * Set the specific drive type
		 */
		return (xy_ustyp(un, dev, arg, flag));

	case HDKIOCSCMD:
		if (COPYIN(arg, data, struct hdk_cmd, flag))
			return (EFAULT);
		/*
		 * Run a generic command.
		 */
		return (xy_ucmd(un, dev, (struct hdk_cmd *) data));

	default:
		/*
		 * This ain't no party, this ain't no disco.
		 */
		return (ENOTTY);
	}
	return (0);
}


static int
xy_ugtyp(struct xyunit *un, dev_t dev, int arg, int flag)
{
	register struct xycmdblock *xycbi;
	int err;
	struct hdk_type local, *typ = &local;

	PRINTF3("\n...xy_ugtyp	");
	bzero((caddr_t) typ, sizeof (struct hdk_type));

	xycbi = XYGETCBI(un->un_c, 0, XY_ASYNCHWAIT);
	mutex_enter(&un->un_c->c_mutex);
	err = XYCMD(xycbi, XY_STATUS, getminor(dev),
	    (caddr_t)0, un->un_slave, (daddr_t)0, 0, XY_ASYNCHWAIT, 0);
	mutex_exit(&un->un_c->c_mutex);
	if (err) {
		XYPUTCBI(xycbi);
		if (xy_debug) {
			cmn_err(CE_CONT, "xy%d: xy_ugtyp- ctlr RPAR fails\n",
			    ddi_get_instance(un->un_dip));
		}
		return (EIO);
	}

	typ->hdkt_hsect = xycbi->iopb->xy_bufrel & 0xff;
	typ->hdkt_drstat = xycbi->iopb->xy_status;
	typ->hdkt_promrev = xycbi->iopb->xy_nsect >> 8;
	typ->hdkt_drtype = un->un_drtype;

	PRINTF1("xy hsect:    %d\n", typ->hdkt_hsect);
	PRINTF1("xy drstat    %d\n", typ->hdkt_drstat);
	PRINTF1("xy promrev:  %d\n", typ->hdkt_promrev);
	PRINTF1("xy drtype    %d\n", typ->hdkt_drtype);

	XYPUTCBI(xycbi);
	if (COPYOUT(typ, arg, struct hdk_type, flag))
		return (EFAULT);
	else
		return (0);
}



/*ARGSUSED*/
static int
xy_ustyp(struct xyunit *un, dev_t dev, int arg, int flag)
{
	struct hdk_type local;
	struct hdk_type *typ = &local;

	if (COPYIN(arg, typ, struct hdk_type, flag)) {
		return (EFAULT);
	}

	mutex_enter(&un->un_c->c_mutex);
	un->un_drtype = typ->hdkt_drtype;
	PRINTF1("set type     %d\n", typ->hdkt_drtype);
	mutex_exit(&un->un_c->c_mutex);
	if (usegeom(un, XY_ASYNCHWAIT) != DDI_SUCCESS) {
		return (EINVAL);
	}
	return (0);
}



static int
xy_ucmd(struct xyunit *un, dev_t dev, struct hdk_cmd *com)
{
	struct hdk_cmd *newcom;
	register struct xycmdblock *xycbi;
	register struct buf *bp;
	int cmddir, err, exec, hsect, flags;
	auto struct iovec aiov;
	auto struct uio auio;
	register struct uio *uio = &auio;

	PRINTF3("\n...xy_ucmd  ");

	if (xy_debug) {
		cmn_err(CE_CONT,
		    "xy%d: xy_ucmd- cmd %x blk %d sec %d buflen %d flags %x\n",
		    ddi_get_instance(un->un_dip), com->hdkc_cmd,
		    (int) com->hdkc_blkno, com->hdkc_secnt,
		    com->hdkc_buflen, (int) com->hdkc_flags);
	}

	flags = 0;
	if (com->hdkc_flags & HDK_SILENT)
		flags |= XY_NOMSG;
	if (com->hdkc_flags & HDK_DIAGNOSE)
		flags |= XY_DIAG;
	if (com->hdkc_flags & HDK_ISOLATE)
		flags |= XY_NOCHN;

	if (xy_debug)
		flags &= ~XY_NOMSG;

	newcom = (struct hdk_cmd *) 0;
	bp = (struct buf *) 0;
	hsect = err = exec = 0;

	/*
	 * Theser are the only commands we accept that do *not* imply
	 * user data DMA. We'll handle them inline here (since it is easy).
	 */

	if (com->hdkc_cmd == XY_RESTORE ||
	    com->hdkc_cmd == (XY_FORMAT)) {
		if (com->hdkc_buflen != 0) {
			err = EINVAL;
			goto errout;
		}
		if (com->hdkc_cmd != XY_RESTORE &&
		    (!ONTRACK(com->hdkc_blkno))) {
			err = EINVAL;
			goto errout;
		}
		xycbi = XYGETCBI(un->un_c, 0, XY_ASYNCHWAIT);
		mutex_enter(&un->un_c->c_mutex);
		err = XYCMD(xycbi, com->hdkc_cmd, getminor(dev), 0,
		    un->un_slave, (daddr_t) com->hdkc_blkno, com->hdkc_secnt,
		    XY_ASYNCHWAIT, flags);
		mutex_exit(&un->un_c->c_mutex);
		exec = 1;
		XYPUTCBI(xycbi);
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
	case XY_READ:
		cmddir = XY_IN;
		/* FALLTHROUGH */
	case XY_WRITE:
		if (com->hdkc_buflen != (com->hdkc_secnt * SECSIZE)) {
			err = EINVAL;
			goto errout;
		}
		break;
	case XY_READALL|XY_DEFLST:	/* read (extended) defect map */
		cmddir = XY_IN;
		/* FALLTHROUGH */
		if ((!ONTRACK(com->hdkc_blkno)) ||
		    (com->hdkc_buflen != XY_MANDEFSIZE)) {
			err = EINVAL;
			goto errout;
		}
		break;
	case XY_READHDR:	/* read track headers */
		cmddir = XY_IN;
		/* FALLTHROUGH */
	case XY_WRITEHDR:	/* write track headers */
		xycbi = XYGETCBI(un->un_c, 0, XY_ASYNCHWAIT);

		mutex_enter(&un->un_c->c_mutex);
		err = XYCMD(xycbi, XY_STATUS, getminor(dev),
		    (caddr_t) 0, un->un_slave, (daddr_t) 0, 0,
		    XY_ASYNCHWAIT, (xy_debug) ? 0 : XY_NOMSG);
		hsect = xycbi->iopb->xy_bufrel & 0xff;
		mutex_exit(&un->un_c->c_mutex);

		XYPUTCBI(xycbi);

		if (err) {
			err = EIO;
			goto errout;
		}
		if ((!ONTRACK(com->hdkc_blkno)) ||
		    (com->hdkc_buflen != hsect * XY_HDRSIZE)) {
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

	if (com->hdkc_buflen == 0 || com->hdkc_buflen > XY_MAXBUFSIZE)
		return (EINVAL);

	/*
	 * Get a copy of the hdk_cmd structure
	 */

	newcom = (struct hdk_cmd *) kmem_zalloc(sizeof (*newcom), KM_SLEEP);
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

	err = physio(xystrategy, bp, dev,
	    (cmddir == XY_IN)? B_READ : B_WRITE, xymin, uio);
	mutex_exit(&un->un_sbmutex);
	kmem_free((caddr_t) newcom, sizeof (struct hdk_cmd));
	exec = 1;

errout:
	if (exec == 0 && err && (com->hdkc_flags & HDK_DIAGNOSE)) {
		un->un_errsect = 0;
		un->un_errno = XYE_UNKN;
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
xydump(dev_t dev, caddr_t addr, daddr_t blkno, int nblk)
{
	register struct xyunit *un = ddi_get_soft_state(xy_state,
	    INSTANCE(dev));
	register struct dk_map *lp;
	register struct xycmdblock *xycbi;
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

	if ((xycbi = XYGETCBI(un->un_c, 0, XY_SYNCH)) == NULL) {
		cmn_err(CE_WARN, "xy%d: can't get free xycbi",
			(int) INSTANCE(dev));
		return (EIO);
	}

	if (ddi_dma_addr_setup(un->un_c->c_dip, (struct as *) 0, addr,
				(nblk*SECSIZE), DDI_DMA_WRITE, DDI_DMA_SLEEP,
				(caddr_t) 0, un->un_c->c_lim, &labhan)) {
		cmn_err(CE_WARN, "xy%d: cannot map dump space",
			(int) INSTANCE(dev));
		XYPUTCBI(xycbi);
		return (DDI_FAILURE);
	}

	mutex_enter(&un->un_c->c_mutex);

	/*
	 * Synchronously execute the dump and return the status.
	 */
	err = XYCMD(xycbi, XY_WRITE, getminor(dev), labhan,
		    unit, blkno, (int)nblk, XY_SYNCH, 0);
	mutex_exit(&un->un_c->c_mutex);

	XYPUTCBI(xycbi);
	ddi_dma_free(labhan);

	if (err) {
		cmn_err(CE_WARN, "xy%d: dump failed", (int) INSTANCE(dev));
		return (EIO);
	}
	return (0);
}

/*
 * This routine translates a buf oriented command down
 * to a level where it can actually be executed.
 */

static void
xygo(register struct xycmdblock *xycbi)
{
	register struct xyunit *un = xycbi->un;
	register struct buf *bp;
	int secnt, flags;
	u_short cmd;
	daddr_t blkno;

	PRINTF3("\n...xygo");
	flags = 0;

	if ((bp = xycbi->breq) == un->un_sbufp) {
		struct hdk_cmd *com = (struct hdk_cmd *) bp->b_forw;

		if (com->hdkc_flags & HDK_SILENT)
			flags |= XY_NOMSG;
		if (com->hdkc_flags & HDK_DIAGNOSE)
			flags |= XY_DIAG;
		if (com->hdkc_flags & HDK_ISOLATE)
			flags |= XY_NOCHN;
		if (xy_debug)
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
		secnt = MIN(secnt, lp->dkl_nblk - dkblock(bp));
		bp->b_resid = bp->b_bcount - secnt * SECSIZE;

		/*
		 * Calculate all the parameters needed to execute the command.
		 */
		if (bp->b_flags & B_READ)
			cmd = XY_READ;
		else
			cmd = XY_WRITE;
		blkno = dkblock(bp);
		blkno += lp->dkl_cylno * un->un_g.dkg_nhead *
		    un->un_g.dkg_nsect;
	}

	/*
	 * Execute the command.
	 */
	(void) XYCMD(xycbi, cmd, getminor(bp->b_edev), xycbi->handle,
	    un->un_slave, blkno, secnt, XY_ASYNCH, flags);
}

static void
xymin(register struct buf *bp)
{
	if (bp->b_bcount > XY_MAXBUFSIZE)
		bp->b_bcount = XY_MAXBUFSIZE;
}

static void
xy_build_user_vtoc(struct xyunit *un, struct vtoc *vtoc)
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
xy_build_label_vtoc(struct xyunit *un, struct vtoc *vtoc)
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
xy_write_label(dev_t dev)
{
	struct xyunit *un = ddi_get_soft_state(xy_state, INSTANCE(dev));
	struct xyctlr *c = un->un_c;
	struct xycmdblock *xycbi;
	ddi_dma_handle_t labhan;
	struct dk_label *dkl;
	int i, err;
	int sec, blk, head, cyl;
	short sum, *sp;

	/*
	 * Allocate a temporary buffer in DVMA space for writing the label.
	 */
	if (ddi_iopb_alloc(c->c_dip, c->c_lim, SECSIZE, (caddr_t *)&dkl)) {
		cmn_err(CE_WARN, "xy%d: cannot allocate iopb space for label",
		    (int) INSTANCE(dev));
		return (DDI_FAILURE);
	}
	if (ddi_dma_addr_setup(c->c_dip, (struct as *)0, (caddr_t)dkl,
	    SECSIZE, DDI_DMA_WRITE, DDI_DMA_SLEEP, (caddr_t)0,
	    c->c_lim, &labhan)) {
		cmn_err(CE_WARN, "xy%d: cannot map iopb label space",
		    (int) INSTANCE(dev));
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
	xycbi = XYGETCBI(un->un_c, 0, XY_ASYNCHWAIT);
	mutex_enter(&un->un_c->c_mutex);
	(void) ddi_dma_sync(labhan, 0, SECSIZE, DDI_DMA_SYNC_FORDEV);
	err = XYCMD(xycbi, XY_WRITE, NOLPART, labhan, un->un_slave,
	    (daddr_t) 0, 1, XY_ASYNCHWAIT, (xy_debug)? 0 : XY_NOMSG);

	if (err) {
		cmn_err(CE_WARN, "xy%d: cannot write label",
			(int) INSTANCE(dev));
		mutex_exit(&un->un_c->c_mutex);
		XYPUTCBI(xycbi);
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
		err = XYCMD(xycbi, XY_WRITE, NOLPART, labhan, un->un_slave,
		    (daddr_t) blk, 1, XY_ASYNCHWAIT, (xy_debug)? 0 : XY_NOMSG);

		if (err) {
			cmn_err(CE_WARN,
				"xy%d: cannot write backup label at %d",
				(int) INSTANCE(dev), blk);
		}
	}
	mutex_exit(&un->un_c->c_mutex);
	XYPUTCBI(xycbi);
	mutex_enter(&un->un_c->c_mutex);
	ddi_dma_free(labhan);
	ddi_iopb_free((caddr_t) dkl);

	return (DDI_SUCCESS);
}

#if	defined(XYDEBUG) || defined(lint)
static	void
xxprintf(char *fmt, ...)
{
	va_list adx;
	int j;


	mutex_enter(&xy_trace_mutex);
	va_start(adx, fmt);

	if (trace_mode > 0) {
		(void) vsprintf(&xy_trace[to_trace], fmt, adx);
		j = 0;
		while (xy_trace[to_trace]) {
			j++;
			to_trace++;
		}
		if (j > MAXLINESIZE || xy_trace[TRACE_SIZE])
			cmn_err(CE_WARN,
			"sprintf output > MAXLINESIZE");
		if (xy_trace[TRACE_SIZE + RED_ZONE -1])
			cmn_err(CE_PANIC,
			"sprintf output > MAXLINESIZE + RED_ZONE");
		xy_trace[to_trace] = '\n';

		if (to_trace < TRACE_SIZE - MAXLINESIZE) {
			for (j = 1; j < 79; j++) {
				if (j < 41)
					xy_trace[to_trace + j] = 'E';
				if (j < 2 || j == 39 || j == 40)
					xy_trace[to_trace + j] = '\n';
				if (j > 40 && xy_trace[to_trace + j]) {
					xy_trace[to_trace + j] = 'B';
					if (j == 78)
					    xy_trace[to_trace + j] = '\n';
				}
			}

		} else {
			for (j = 1; j < MAXLINESIZE; j++) {
				if (to_trace + j >= TRACE_SIZE)
					break;
				xy_trace[to_trace + j] = 'W';
				if (j < 2 || (j % 60) == 0)
					xy_trace[to_trace + j] = '\n';
			}

			/*
			 * Wrap around to beginning of buffer.
			 */

			to_trace = 0;
		}
		itrace = to_trace;
	}

	if (trace_mode ==  2) {
		vcmn_err(CE_CONT, fmt, adx);
	}
	va_end(adx);
	mutex_exit(&xy_trace_mutex);
#ifdef	lint
	itrace = itrace;
#endif
}

#endif
