/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)sbusmem.c	1.15	93/07/13 SMI" /* SVr4 5.0 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/open.h>
#include <sys/stat.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

/* #define SBUSMEM_DEBUG */

#ifdef SBUSMEM_DEBUG
#include <sys/ddi_impldefs.h>

int sbusmem_debug_flag;
#define	sbusmem_debug	if (sbusmem_debug_flag) printf
#endif /* SBUSMEM_DEBUG */

static void *sbusmem_state_head;

struct sbusmem_unit {
	u_int size;
	u_long pagesize;
	dev_info_t *dip;
};

static int sbmem_open(dev_t *, int, int, cred_t *);
static int sbmem_close(dev_t, int, int, struct cred *);
static int sbmem_read(dev_t, struct uio *, cred_t *);
static int sbmem_write(dev_t, struct uio *, cred_t *);
static int sbmem_mmap(dev_t, off_t, int);

static struct cb_ops sbmem_cb_ops = {

	sbmem_open,		/* open */
	sbmem_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	sbmem_read,		/* read */
	sbmem_write,		/* write */
	nodev,			/* ioctl */
	nodev,			/* devmap */
	sbmem_mmap,		/* mmap */
	ddi_segmap,		/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_NEW | D_MP,		/* Driver compatibility flag */

};

static int sbmem_identify(dev_info_t *);
static int sbmem_attach(dev_info_t *, ddi_attach_cmd_t);
static int sbmem_detach(dev_info_t *, ddi_detach_cmd_t);
static int sbmem_info(dev_info_t *, ddi_info_cmd_t, void *, void **);

static struct dev_ops sbmem_ops = {

	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	sbmem_info,		/* get_dev_info */
	sbmem_identify,		/* identify */
	nulldev,		/* probe */
	sbmem_attach,		/* attach */
	sbmem_detach,		/* detach */
	nodev,			/* reset */
	&sbmem_cb_ops,		/* driver operations */
	(struct bus_ops *)0,	/* bus operations */

};

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a driver */
	"SBus memory driver", /* Name of module. */
	&sbmem_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

static int sbmem_rw(dev_t, struct uio *, enum uio_rw, cred_t *);

/*
 * This is the driver initialization routine.
 */
int
_init(void)
{
	int error;

	if ((error = ddi_soft_state_init(&sbusmem_state_head,
	    sizeof (struct sbusmem_unit), 1)) != 0) {
		return (error);
	}
	if ((error = mod_install(&modlinkage)) != 0) {
		ddi_soft_state_fini(&sbusmem_state_head);
	}
	return (error);
}

/*
 * Check to see if anyone's using the driver, returning EBUSY if they are,
 */
int
_fini(void)
{
	int error;

	if ((error = mod_remove(&modlinkage)) == 0) {
		ddi_soft_state_fini(&sbusmem_state_head);
	}
	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
sbmem_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "sbusmem") == 0) {
		return (DDI_IDENTIFIED);
	}
	return (DDI_NOT_IDENTIFIED);
}

static int
sbmem_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	register struct sbusmem_unit *un;
	register int error = DDI_FAILURE;
	int instance, ilen;
	u_int size;
	char *ident;

	switch (cmd) {
	case DDI_ATTACH:
		instance = ddi_get_instance(devi);

		size = ddi_getprop(DDI_DEV_T_NONE, devi,
		    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "size", -1);
		if (size == (u_int)-1) {
#ifdef SBUSMEM_DEBUG
			sbusmem_debug(
			    "sbmem_attach%d: No size property\n",
			    instance);
#endif /* SBUSMEM_DEBUG */
			break;
		}

		if (ddi_getlongprop(DDI_DEV_T_NONE, devi,
		    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "ident",
		    (caddr_t) &ident, &ilen) != DDI_PROP_SUCCESS) {
#ifdef SBUSMEM_DEBUG
			sbusmem_debug(
			    "sbmem_attach%d: No ident property\n", instance);
#endif /* SBUSMEM_DEBUG */
			break;
		}

		if (ddi_soft_state_zalloc(sbusmem_state_head,
		    instance) != DDI_SUCCESS)
			break;

		if ((un = ddi_get_soft_state(sbusmem_state_head,
		    instance)) == NULL) {
			ddi_soft_state_free(sbusmem_state_head, instance);
			break;
		}

		if (ddi_create_minor_node(devi, ident, S_IFCHR, instance,
		    NULL, NULL) == DDI_FAILURE) {
			kmem_free(ident, ilen);
			ddi_remove_minor_node(devi, NULL);
			ddi_soft_state_free(sbusmem_state_head, instance);
			break;
		}
		kmem_free(ident, ilen);
		un->dip = devi;
		un->size = size;
		un->pagesize = ddi_ptob(devi, 1);

#ifdef SBUSMEM_DEBUG
		sbusmem_debug("sbmem_attach%d: dip 0x%x size 0x%x\n",
		    instance, devi, size);
#endif /* SBUSMEM_DEBUG */

		ddi_report_dev(devi);
		error = DDI_SUCCESS;
		break;
	case DDI_RESUME:
		error = DDI_SUCCESS;
		break;
	default:
		break;
	}
	return (error);
}

static int
sbmem_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	int instance;

	switch (cmd) {
	case DDI_DETACH:

		instance = ddi_get_instance(devi);
		ddi_remove_minor_node(devi, NULL);
		ddi_soft_state_free(sbusmem_state_head, instance);
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

/*
 * This code used to live in mem.c
 */
/*ARGSUSED1*/
static int
sbmem_open(dev_t *devp, int flag, int typ, cred_t *cred)
{
	int instance;

	if (typ != OTYP_CHR)
		return (EINVAL);

	instance = getminor(*devp);
	if (ddi_get_soft_state(sbusmem_state_head, instance) == NULL) {
		return (ENXIO);
	}
	return (0);
}

/*ARGSUSED*/
static int
sbmem_close(dev_t dev, int flag, int otyp, struct cred *cred)
{
	if (otyp != OTYP_CHR)
		return (EINVAL);

	return (0);
}

static int
sbmem_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register int instance, error = DDI_FAILURE;
	register struct sbusmem_unit *un;

#if defined(lint) || defined(__lint)
	dip = dip;
#endif /* lint || __lint */

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		instance = getminor((dev_t) arg);
		if ((un = ddi_get_soft_state(sbusmem_state_head,
		    instance)) != NULL) {
			*result = (void *) un->dip;
			error = DDI_SUCCESS;
#ifdef SBUSMEM_DEBUG
		sbusmem_debug(
		    "sbmem_info%d: returning dip 0x%x\n", instance, un->dip);
#endif /* SBUSMEM_DEBUG */

		}
		break;

	case DDI_INFO_DEVT2INSTANCE:
		instance = getminor((dev_t) arg);
		*result = (void *) instance;
		error = DDI_SUCCESS;
		break;

	default:
		break;
	}
	return (error);
}

static int
sbmem_read(dev_t dev, struct uio *uio, cred_t *cred)
{
	return (sbmem_rw(dev, uio, UIO_READ, cred));
}

static int
sbmem_write(dev_t dev, struct uio *uio, cred_t *cred)
{
	return (sbmem_rw(dev, uio, UIO_WRITE, cred));
}

/*ARGSUSED3*/
static int
sbmem_rw(dev_t dev, struct uio *uio, enum uio_rw rw, cred_t *cred)
{
	register u_int c;
	register struct iovec *iov;
	register struct sbusmem_unit *un;
	u_long pagesize, msize;
	int instance, error = 0;
	dev_info_t *dip;
	caddr_t reg;

	instance = getminor(dev);
	if ((un = ddi_get_soft_state(sbusmem_state_head, instance)) == NULL) {
		return (ENXIO);
	}
	dip = un->dip;
	pagesize = un->pagesize;

	while (uio->uio_resid > 0 && error == 0) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				cmn_err(CE_PANIC, "sbmem_rw");
			continue;
		}

		if (uio->uio_offset > un->size) {
			return (EFAULT);
		}

		if (uio->uio_offset == un->size) {
			return (0);		/* EOF */
		}

#ifdef SBUSMEM_DEBUG
		{
			struct regspec *rp = ddi_rnumber_to_regspec(dip, 0);

			if (rp == NULL) {
				sbusmem_debug(
				    "sbmem_rw%d: No reg property\n",
				    instance);
			} else
				sbusmem_debug(
				    "sbmem_rw%d: Mapping 0x%x bytes at off"
				    " 0x%x slot 0x%x\n", instance,
				    rp->regspec_size, uio->uio_offset,
				    rp->regspec_bustype);
		}
#endif /* SBUSMEM_DEBUG */

		/*
		 * Mapin for SBus, no cache operation
		 * is involved.
		 */

		msize = pagesize - ((int) uio->uio_offset & (pagesize - 1));
		if (ddi_map_regs(dip, 0, &reg, uio->uio_offset,
		    (off_t)msize) != DDI_SUCCESS) {
			return (EFAULT);
		}
		c = min((u_int) msize, (u_int) iov->iov_len);
		if (ddi_peekpokeio(dip, uio, rw, reg, (int)c,
		    sizeof (long)) != DDI_SUCCESS)
			error = EFAULT;

		ddi_unmap_regs(dip, 0, &reg, uio->uio_offset, (off_t)msize);
	}
	return (error);
}

/*ARGSUSED2*/
static int
sbmem_mmap(dev_t dev, off_t off, int prot)
{
	register struct sbusmem_unit *un;
	dev_info_t *dip;
	u_long msize;
	int instance;
	u_int pfn;
	caddr_t reg;

	instance = getminor(dev);
	if ((un = ddi_get_soft_state(sbusmem_state_head, instance)) == NULL) {
		return (-1);
	}
	dip = un->dip;

	if (off >= un->size) {
		cmn_err(CE_WARN,
		    "sbmem_mmap%d: offset 0x%x out of slot space 0x%x",
			instance, (int)off, un->size);
		return (-1);
	}

#ifdef SBUSMEM_DEBUG
	{
		struct regspec *rp = ddi_rnumber_to_regspec(dip, 0);

		if (rp == NULL) {
			sbusmem_debug(
			    "sbmem_mmap%d: No reg property\n", instance);
		} else
			sbusmem_debug(
			    "sbmem_mmap%d: Mapping 0x%x bytes at off 0x%x ",
			    " slot 0x%x\n", instance, rp->regspec_size, off,
			    rp->regspec_bustype);
	}
#endif /* SBUSMEM_DEBUG */

	msize = un->pagesize - ((int) off & (un->pagesize - 1));
	if (ddi_map_regs(dip, 0, &reg, off, (off_t) msize)) {
		return (-1);
	}

	pfn = hat_getkpfnum(reg);
	ddi_unmap_regs(dip, 0, &reg, off, (off_t) msize);

#ifdef  SBUSMEM_DEBUG
	sbusmem_debug("sbmem_mmap%d: returning pfn 0x%x\n", instance, pfn);
#endif /* SBUSMEM_DEBUG */

	return (pfn);
}
