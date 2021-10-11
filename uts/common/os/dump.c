/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident	"@(#)dump.c	1.27	94/04/02 SMI" /* from SunOS 4.1 1.7 */

/*
 * Dump driver.
 * Allows reading and writing of crash dumps from the "dump file"
 * (the device to which "dumpvp" refers).
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/fs/snode.h>
#include <sys/cred.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/cmn_err.h>

/*
 * Define cb_ops and dev_ops
 */

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

int dumpread(dev_t dev, struct uio *uio_p, cred_t *cred_p);
int dumpwrite(dev_t dev, struct uio *uio_p, cred_t *cred_p);
int dumpopen(dev_t *dev_p, int flag, int otyp, cred_t *cred_p);
int dumpprop_op(dev_t dev, dev_info_t *di, ddi_prop_op_t prop,
    int m, char *name, caddr_t valuep, int *lengthp);

static long dump_size;
static int dump_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int dump_identify(dev_info_t *devi);
static int dump_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static int dump_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);

static dev_info_t *dump_dip;	/* private copy of the devinfo pointer */

struct cb_ops	dump_cb_ops = {

	dumpopen,		/* open */
	nulldev,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	dumpread,		/* read */
	dumpwrite,		/* write */
	nodev,			/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev, 			/* segmap */
	nochpoll,		/* poll */
	dumpprop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_NEW | D_MP		/* Driver compatibility flag */
};

struct dev_ops	dump_ops = {

	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt */
	dump_info,		/* info */
	dump_identify,		/* identify */
	nulldev,		/* probe */
	dump_attach,		/* attach */
	dump_detach,		/* detach */
	nodev,			/* reset */
	&dump_cb_ops,		/* driver operations */
	(struct bus_ops *)0	/* bus operations */

};

extern struct mod_ops mod_driverops;

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"Crash Dump reader driver'dump'",
	&dump_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};


int
_init(void)
{
	return (mod_install(&modlinkage));
}


int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
dump_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "dump") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

static int
dump_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_create_minor_node(devi, "dump", S_IFCHR,
	    0, NULL, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (-1);
	}
	dump_dip = devi;
	return (DDI_SUCCESS);
}

static int
dump_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ddi_remove_minor_node(devi, NULL);
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
dump_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register dev_t dev = (dev_t) arg;
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (dump_dip == NULL) {
			*result = (void *)NULL;
			error = DDI_FAILURE;
		} else {
			*result = (void *) dump_dip;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		if (getminor(dev) != 0) {
			*result = (void *)-1;
			error = DDI_FAILURE;
		} else {
			*result = (void *)0;
			error = DDI_SUCCESS;
		}
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

extern	struct vnode *specfind(dev_t, vtype_t);

/*
 * We used to have a hack in spec_getattr that would snarf the
 * dump file size from the snode, which we used to set here. Now that
 * we support the "size" property for VCHR's, we treat the size as
 * a ddi property, which we intercept with our own property routine. (see
 * below)  The static global "dump_size is used to cache our dump size.
 */
/*ARGSUSED*/
dumpopen(
	dev_t *dev_p,
	int flag,	/* flags from open call */
	int otyp,
	cred_t *cred_p)
{
	struct vnode		*vp;
	struct vattr		vattr;
	register int		error;

	/* Might be no dump device, (if we have no physical swap devices) */
	if (!dumpvp)
		return (ENODEV);
	/*
	 * Get the size of the dump file.
	 */
	if (dump_size != 0) {
		/*
		 * no need getting this if we already have it.
		 */
		return (0);
	} else {
		if ((vp = specfind(*dev_p, VCHR)) == NULL)
			panic("dumpopen: vnode for /dev/dump not found");

		vattr.va_mask = AT_SIZE;
		error = VOP_GETATTR(dumpvp, &vattr, 0, CRED());
		if (error == 0)
			dump_size = vattr.va_size;
		VN_RELE(vp); /* was held by specfind */
		return (error);
	}
}

/*ARGSUSED*/
int
dumpread(dev_t dev, struct uio *uio_p, cred_t *cred_p)
{
	int error;

	VOP_RWLOCK(dumpvp, 0);
	error = VOP_READ(dumpvp, uio_p, 0, CRED()); /* XXX - just cred_p? */
	VOP_RWUNLOCK(dumpvp, 0);
	return (error);
}

/*ARGSUSED*/
int
dumpwrite(dev_t dev, struct uio *uio_p, cred_t *cred_p)
{
	int error;

	VOP_RWLOCK(dumpvp, 1);
	error =  VOP_WRITE(dumpvp, uio_p, 0, CRED()); /* XXX - just cred_p? */
	VOP_RWUNLOCK(dumpvp, 1);
	return (error);
}

/*
 * Intercept the size property request. In the first implementation of
 * this, dumpopen() was a do nothing routine, and we called specfind() here
 * to get our dump size. Unfortunately, that results in a recursive
 * mutex_enter on stable_lock. So, now the size is set by dumpopen(), and
 * simply returned here. The problem with this is that dumpopen() must be
 * called before any spec_getattr will return the correct size. If one stat's
 * /dev/dump, without first opening it, they'll get a size of 0. This
 * isn't such a big deal, since the prior implementation had the same
 * limitation.
 */
int
dumpprop_op(dev_t dev, dev_info_t *di, ddi_prop_op_t prop,
    int m, char *name, caddr_t valuep, int *lengthp)
{
	register caddr_t	retbuf;
	register int		km_flags;

	if (strcmp(name, "size") == 0) {
		/*
		 * allocate space if necessary. Set buffer to size.
		 */
		switch (prop) {
		case PROP_LEN:
			*lengthp = sizeof (long);
			return (DDI_PROP_SUCCESS);
		case PROP_LEN_AND_VAL_ALLOC:
			if (m & DDI_PROP_CANSLEEP)
				km_flags = KM_SLEEP;
			else
				km_flags = KM_NOSLEEP;
			retbuf = (caddr_t)kmem_alloc(sizeof (long),
			    km_flags);
			if (retbuf == (caddr_t)0) {
				cmn_err(CE_CONT, "dump: no memory for \
size property.\n");
				return (DDI_PROP_NO_MEMORY);
			}
			/* LINTED [no alignment problem. treat as char. */
			*(caddr_t *)valuep = retbuf;
			*lengthp = sizeof (long);
			break;
		case PROP_LEN_AND_VAL_BUF:
			if (sizeof (long) > (*lengthp))
				return (DDI_PROP_BUF_TOO_SMALL);
			else
				*lengthp = sizeof (long);
			retbuf = valuep;
			break;
		}
		*((long *)retbuf) = dump_size;
		return (DDI_PROP_SUCCESS);
	} else
		return (ddi_prop_op(dev, di, prop, m, name,
		    valuep, lengthp));
}
