/*
 * Copyright (c) 1988-1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)eeprom.c	1.13	94/12/12 SMI"

/* From 4.1.1 sun4/mem.c 1.16 */

/*
 * Memory special file for eeprom
 *
 * This driver assumes that there is only one eeprom.
 *
 * XXX	Not true for sun4d - let's hope we never have to port it there.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/vm.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/kmem.h>
#include <vm/seg.h>
#include <sys/stat.h>

#include <sys/cpu.h>
#include <sys/eeprom.h>
#include <vm/seg_kmem.h>
#include <vm/seg_vn.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/mem.h>
#include <sys/open.h>

/* for mostek48T02 */
#include <sys/clock.h>

/* for /dev/meter */
#include <sys/memerr.h>

static kmutex_t ee_lock;	/* locks access to /dev/eeprom */

static kcondvar_t *eepbcv;
static int eeprombusy;		/* device is settling */

static int eeopen(dev_t *, int, int, cred_t *);
static int eeclose(dev_t, int, int, cred_t *);
static int eeread(dev_t, struct uio *, cred_t *);
static int eewrite(dev_t, struct uio *, cred_t *);

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>
#include <sys/errno.h>
extern struct mod_ops mod_driverops;
static struct dev_ops ee_ops;

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a driver */
	"eeprom driver", /* Name of module. */
	&ee_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

static int ee_identify(dev_info_t *);
static int ee_attach(dev_info_t *, ddi_attach_cmd_t);
static int ee_detach(dev_info_t *, ddi_detach_cmd_t);

/*
 * This is the driver initialization routine.
 */

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
ee_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result);

static dev_info_t *ee_dip;	/* private copy of devinfo pointer */

static struct cb_ops ee_cb_ops = {

	eeopen,			/* open */
	eeclose,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	eeread,			/* read */
	eewrite,		/* write */
	nodev,			/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_NEW | D_MP		/* Driver compatibility flag */

};

static struct dev_ops ee_ops = {

	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ee_info,		/* get_dev_info */
	ee_identify,		/* identify */
	nulldev,		/* probe */
	ee_attach,		/* attach */
	ee_detach,		/* detach */
	nodev,			/* reset */
	&ee_cb_ops,		/* driver operations */
	(struct bus_ops *)0	/* bus operations */

};

static int
ee_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "eeprom") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

static int eerw(dev_t, struct uio *, enum uio_rw, cred_t *);
/*
 * ee_promio will point to one of the two following routines
 */
static int (*ee_promio)(struct uio *, enum uio_rw, caddr_t, int);
static int eeprom(struct uio *, enum uio_rw, caddr_t, int);
static int nvram(struct uio *, enum uio_rw, caddr_t, int);

static int
ee_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	char *modelp;
	int model_len;

	switch (cmd) {
	case DDI_ATTACH:

		ee_dip = devi;
		mutex_init(&ee_lock, "eeprom lock", MUTEX_DRIVER, DEFAULT_WT);

		/*
		 * If we are an OBP machine, the PROM will export a regspec
		 * that tells us what we need to know about the eeprom
		 * (really nvram).  Otherwise (sun4), we have a real eeprom,
		 * with write settling time.  However the .conf file will
		 * still contain a regspec telling us where it is.
		 *
		 * XXX	Yes, the thing's mapped at EEPROM too - but this
		 *	is simply a bug.
		 */

		if (ddi_dev_is_sid(devi) == DDI_SUCCESS) {
			if (ddi_getlongprop(DDI_DEV_T_ANY, devi,
			    DDI_PROP_DONTPASS, "model", (caddr_t)&modelp,
			    &model_len) != DDI_SUCCESS)
			return (DDI_FAILURE);

			/*
			 * These are the only ones we know how to
			 * handle right now
			 */
			if (strcmp(modelp, "mk48t02") != 0 &&
			    strcmp(modelp, "mk48t08") != 0) {
				kmem_free(modelp, model_len);
				return (DDI_FAILURE);
			}

			kmem_free(modelp, model_len);
			ee_promio = nvram;
		} else {
			/*
			 * sun4 only
			 */
			eepbcv = (kcondvar_t *)
			    kmem_alloc(sizeof (kcondvar_t), KM_SLEEP);
			cv_init(eepbcv, "eeprom write delay cv", CV_DRIVER,
			    (void *) 0);
			ee_promio = eeprom;
		}

		if (ddi_create_minor_node(devi, "eeprom", S_IFCHR,
		    M_EEPROM, NULL, NULL) == DDI_FAILURE) {
			goto broken;
			/*NOTREACHED*/
		}

		if (cputype == CPU_SUN4_470)	/* only on Sunray */
			if (ddi_create_minor_node(devi, "meter", S_IFCHR,
			    M_METER, NULL, NULL) == DDI_FAILURE) {
				goto broken;
				/*NOTREACHED*/
			}

		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

broken:
	ddi_remove_minor_node(devi, NULL);
	if (eepbcv) {
		cv_destroy(eepbcv);
		kmem_free(eepbcv, sizeof (*eepbcv));
		eepbcv = 0;
	}
	mutex_destroy(&ee_lock);
	return (DDI_FAILURE);
}

static int
ee_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	/*
	 * If we have a pending timeout, refuse the detach
	 */

	if (eeprombusy)
		return (DDI_FAILURE);

	if (eepbcv) {
		cv_destroy(eepbcv);
		kmem_free(eepbcv, sizeof (*eepbcv));
		eepbcv = 0;
	}

	ddi_remove_minor_node(devi, NULL);

	mutex_destroy(&ee_lock);

	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
ee_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *) ee_dip;
		error = DDI_SUCCESS;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

/*ARGSUSED1*/
static int
eeopen(dev_t *devp, int flag, int typ, cred_t *cred)
{
	if (typ != OTYP_CHR)
		return (EINVAL);

	switch (getminor(*devp)) {

	case M_EEPROM:
		break;

	case M_METER:
		if (cputype == CPU_SUN4_470)	/* only works for Sunray */
			break;
		/* otherwise fall through to unsupported/unknown */

	default:
		/* Unsupported or unknown type */
		return (EINVAL);
	}

	return (0);
}

/*ARGSUSED*/
static int
eeclose(dev_t dev, int flag, int otyp, cred_t *cred)
{
	if (otyp != OTYP_CHR)
		return (EINVAL);

	return (0);
}

static int
eeread(dev_t dev, struct uio *uio, cred_t *cred)
{
	return (eerw(dev, uio, UIO_READ, cred));
}

static int
eewrite(dev_t dev, struct uio *uio, cred_t *cred)
{
	return (eerw(dev, uio, UIO_WRITE, cred));
}

/*ARGSUSED*/
static int
eerw(dev_t dev, struct uio *uio, enum uio_rw rw, cred_t *cred)
{
	register int i;
	register struct iovec *iov;
	int error = 0;
	register u_int *p;
	u_int meters[5];

	while (uio->uio_resid > 0 && error == 0) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				cmn_err(CE_PANIC, "eerw");
			continue;
		}

		switch (getminor(dev)) {

		case M_EEPROM:
			error = (*ee_promio)(uio, rw, (caddr_t)uio->uio_offset,
			    iov->iov_len);
			if (error == -1)
				return (0);		/* EOF */
			break;

		case M_METER:
			/*
			 * fill the destination with the meter contents
			 * large sizes are ignored as this device has
			 * only four words, smaller ones will limit it.
			 * writes are not allowed.
			 */
			if (rw == UIO_WRITE)
				return (EPERM);

			p = (u_int *)(MEMERR_ADDR + 0x1c);
			for (i = 0; i < 5; i++) {
				meters[i] = *p++;
			}
			error = uiomove((caddr_t)meters,
				(iov->iov_len > 20) ? 20 : iov->iov_len,
				UIO_READ, uio);
			break;
		}
	}
	return (error);
fault:
	return (EFAULT);
}


/*
 * Sun4 only
 * If eeprombusy is true, then the eeprom has just
 * been written to and cannot be read or written
 * until the required 10 MS has passed. It is
 * assumed that the only way the EEPROM is written
 * is thru the eeprom routine.
 */
/*ARGSUSED*/
static void
eepromclear(caddr_t arg)
{
	mutex_enter(&ee_lock);
	if (eeprombusy) {
		eeprombusy = 0;
		cv_broadcast(eepbcv);
	}
	mutex_exit(&ee_lock);
}

/*
 * Sun4 version of eeprom io routine
 */
static int
eeprom(struct uio *uio, enum uio_rw rw, caddr_t addr, int len)
{
	int o, err;
	char c, oo;
	caddr_t kaddr;

	if ((int)addr > EEPROM_SIZE)
		return (EFAULT);

	if (ddi_map_regs(ee_dip,
	    0, &kaddr, (off_t)0, (off_t)0) != DDI_SUCCESS) {
		return (ENXIO);
	}

	mutex_enter(&ee_lock);
	while (len > 0) {
		if ((int)addr == EEPROM_SIZE) {
			err = -1;			/* EOF */
			goto out;
			/*NOTREACHED*/
		}

		if (cputype != CPU_SUN4_330) {
			while (eeprombusy)
				cv_wait(eepbcv, &ee_lock);
		}

		if (rw == UIO_WRITE) {
			if ((o = uwritec(uio)) == -1) {
				err = EFAULT;
				goto out;
				/*NOTREACHED*/
			}
			if (ddi_peekc(ee_dip, (u_int)kaddr + addr, &oo)
			    != DDI_SUCCESS) {
				err = EFAULT;
				goto out;
				/*NOTREACHED*/
			}
			/*
			 * Check to make sure that the data is actually
			 * changing before committing to doing the write.
			 * This avoids the unneeded eeprom lock out
			 * and reduces the number of times the eeprom
			 * is actually written to.
			 */
			if (o != oo) {
				if (ddi_pokec(ee_dip, (u_int)kaddr + addr,
				    (char)o) != DDI_SUCCESS) {
					err = EFAULT;
					goto out;
					/*NOTREACHED*/
				}
				if (cputype != CPU_SUN4_330) {
					/*
					 * Block out access to the eeprom for
					 * at least two clock ticks.
					 * (longer than > 10 MS).
					 */
					eeprombusy = 1;
					(void) timeout(eepromclear,
					    (caddr_t)0,
					    (long)drv_usectohz(20000));
				}
			}
		} else {
			if (ddi_peekc(ee_dip, (u_int)kaddr + addr, &c)
			    != DDI_SUCCESS) {
				err = EFAULT;
				goto out;
				/*NOTREACHED*/
			}
			if (ureadc(c, uio)) {
				err = EFAULT;
				goto out;
				/*NOTREACHED*/
			}
		}
		addr += sizeof (char);
		len -= sizeof (char);
	}
	err = 0;
out:
	mutex_exit(&ee_lock);
	ddi_unmap_regs(ee_dip, 0, &kaddr, (off_t)0, (off_t)0);
	return (err);
}

/*
 * Read/write the eeprom. On Sun4c & Sun4m machines, the eeprom is
 * really static memory, so there are no delays or write limits. We simply
 * do byte writes like it is memory.
 *
 * However, we do keep the eeprom unmapped when not actively
 * using it, since it does contain the idprom (cpuid and ethernet address)
 * and we don't want them to get accidentally scrogged.
 *
 * For that reason, mmap'ing the EEPROM is not allowed.
 * Since we don't allow touching of the clock part of this, we no longer
 * splclock() here.
 */
static int
nvram(struct uio *uio, enum uio_rw rw, caddr_t addr, int len)
{
	int o, err;
	caddr_t kaddr;		/* kernel virtual address after mapr_regs */
	u_int size;		/* nvram size in bytes */
	u_int offset = (u_int) addr;

	if (ddi_dev_regsize(ee_dip, 0, (off_t *)&size) == DDI_FAILURE)
		return (ENXIO);

	/*
	 * Subtract off the size of the IDPROM and the clock.
	 */
	size -= sizeof (struct mostek48T02) + IDPROMSIZE;

	if (offset > size)
		return (EFAULT);

	if (ddi_map_regs(ee_dip, 0, &kaddr, (off_t)0, (off_t)0))
		return (ENXIO);

	mutex_enter(&ee_lock);
	while (len > 0) {
		if (offset == size) {
			err = -1;			/* EOF */
			goto out;
			/*NOTREACHED*/
		}

		if (rw == UIO_WRITE) {
			if ((o = uwritec(uio)) == -1) {
				err = EFAULT;
				goto out;
				/*NOTREACHED*/
			}
			*(kaddr+offset) = o;

		} else {
			if (ureadc(*(kaddr+offset), uio)) {
				err = EFAULT;
				goto out;
				/*NOTREACHED*/
			}
		}
		offset += sizeof (char);
		len -= sizeof (char);
	}
	err = 0;
out:
	mutex_exit(&ee_lock);
	ddi_unmap_regs(ee_dip, 0, &kaddr, (off_t)0, (off_t)0);
	return (err);
}
