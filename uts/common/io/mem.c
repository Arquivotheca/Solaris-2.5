/*
 * Copyright (c) 1988-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mem.c	1.33	95/10/24 SMI"

/* From 4.1.1 sun4/mem.c 1.16 */

/*
 * Memory special file
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
#include <sys/fs/snode.h>
#include <vm/seg.h>
#include <vm/page.h>
#include <sys/stat.h>

#include <vm/seg_kmem.h>
#include <vm/seg_vn.h>
#include <vm/seg_dev.h>
#include <vm/hat.h>

#include <sys/conf.h>
#include <sys/mem.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/memlist.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/debug.h>

/*
 * Turn a 64-bit byte address into a u_int page number (the
 * regular btop casts the arg to u_int first).
 */
#define	btop64(x)	((u_int)((x) >> PAGESHIFT))

static int mmopen(dev_t *, int, int, cred_t *);
static int mmread(dev_t, struct uio *, cred_t *);
static int mmwrite(dev_t, struct uio *, cred_t *);
static int mmmmap(dev_t, off_t, int);
static int mmsegmap(dev_t, off_t, struct as *, caddr_t *, off_t, u_int,
    u_int, u_int, cred_t *);
static int mmchpoll(dev_t dev, short events, int anyyet, short *reventsp,
	struct pollhead **phpp);

static int mm_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int mm_identify(dev_info_t *);
static int mm_attach(dev_info_t *, ddi_attach_cmd_t);

static int mmrw(dev_t, struct uio *, enum uio_rw, cred_t *);

static dev_info_t *mm_dip;	/* private copy of devinfo pointer */

static struct cb_ops mm_cb_ops = {
	mmopen,			/* open */
	nulldev,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	mmread,			/* read */
	mmwrite,		/* write */
	nodev,			/* ioctl */
	nodev,			/* devmap */
	mmmmap,			/* mmap */
	mmsegmap,		/* segmap */
	mmchpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_NEW | D_MP | D_64BIT	/* Driver compatibility flag */
};

static struct dev_ops mm_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	mm_info,		/* get_dev_info */
	mm_identify,		/* identify */
	nulldev,		/* probe */
	mm_attach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	&mm_cb_ops,		/* driver operations */
	(struct bus_ops *)0	/* bus operations */
};

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"memory special driver for 'mm'",
	&mm_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

static kmutex_t mm_lock;

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

static int
mm_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "mm") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/*ARGSUSED1*/
static int
mm_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	int i;
	struct mem_minor {
		char *name;
		int minor;
	} mm[] = {
		{ "mem",	M_MEM},
		{ "kmem",	M_KMEM},
		{ "null",	M_NULL},
		{ "zero",	M_ZERO},
	};

	mutex_init(&mm_lock, "/dev/mem lock", MUTEX_DEFAULT, NULL);

	for (i = 0; i < (sizeof (mm) / sizeof (mm[0])); i++) {
		if (ddi_create_minor_node(devi, mm[i].name, S_IFCHR,
		    mm[i].minor, NULL, NULL) == DDI_FAILURE) {
			ddi_remove_minor_node(devi, NULL);
			return (DDI_FAILURE);
		}
	}

	mm_dip = devi;
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
mm_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *)mm_dip;
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
mmopen(dev_t *devp, int flag, int typ, struct cred *cred)
{
	dev_t dev = *devp;

	switch (getminor(dev)) {

	case M_MEM:
	case M_KMEM:
	case M_NULL:
	case M_ZERO:
		/* standard devices */
		break;

	default:
		/* Unsupported or unknown type */
		return (EINVAL);
	}
	return (0);
}

/*ARGSUSED*/
static int
mmchpoll(dev_t dev, short events, int anyyet, short *reventsp,
	struct pollhead **phpp)
{
	switch (getminor(dev)) {
	case M_NULL:
	case M_ZERO:
	case M_MEM:
	case M_KMEM:
		*reventsp = 0;
		if (events & POLLIN)
			*reventsp |= POLLIN;
		if (events & POLLOUT)
			*reventsp |= POLLOUT;
		if (events & POLLPRI)
			*reventsp |= POLLPRI;
		return (0);
	default:
		/* no other devices currently support polling */
		return (ENXIO);
	}
}

static int
mmread(dev_t dev, struct uio *uio, cred_t *cred)
{
	return (mmrw(dev, uio, UIO_READ, cred));
}

static int
mmwrite(dev_t dev, struct uio *uio, cred_t *cred)
{
	return (mmrw(dev, uio, UIO_WRITE, cred));
}

/*
 * When reading the M_ZERO device, we simply copyout the zeroes
 * array in NZEROES sized chunks to the user's address.
 *
 * XXX - this is not very elegant and should be redone.
 */
#define	NZEROES		0x100
static char zeroes[NZEROES];

/*ARGSUSED3*/
static int
mmrw(dev_t dev, struct uio *uio, enum uio_rw rw, cred_t *cred)
{
	register int o;
	register u_int c, v;
	register struct iovec *iov;
	int error = 0;
	struct memlist *pmem;
	extern struct memlist *phys_install;

	while (uio->uio_resid > 0 && error == 0) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("mmrw");
			continue;
		}
		switch (getminor(dev)) {

		case M_MEM:
			v = btop64(uio->uio_loffset);
			for (pmem = phys_install;
			    pmem != (struct memlist *)NULL;
			    pmem = pmem->next) {
				if (v >= btop64(pmem->address) &&
				    v < btop64(pmem->address + pmem->size))
					break;
			}
			if (pmem == (struct memlist *)NULL)
				goto fault;
			mutex_enter(&mm_lock);

			segkmem_mapin(&kvseg, mm_map, MMU_PAGESIZE,
			    (u_int)(rw == UIO_READ ? PROT_READ :
			    PROT_READ | PROT_WRITE), v, HAT_NOCONSIST);

			o = uio->uio_loffset & MMU_PAGEOFFSET;
			c = MIN((u_int)(NBPG - o), (u_int)iov->iov_len);
			/*
			 * We restrict ourselves to only do the rest of the
			 * page because we have only one page of mapping
			 * behind mm_map.
			 */
			c = MIN(c, (u_int)(NBPG -
				((int)iov->iov_base & MMU_PAGEOFFSET)));
			error = uiomove((caddr_t)&mm_map[o], (long)c, rw, uio);
			segkmem_mapout(&kvseg, mm_map, MMU_PAGESIZE);

			mutex_exit(&mm_lock);
			break;

		case M_KMEM:
			c = iov->iov_len;
			if (ddi_peekpokeio((dev_info_t *)0, uio, rw,
			    (caddr_t)uio->uio_offset, (int)c, sizeof (long))
			    != DDI_SUCCESS)
				error = EFAULT;
			break;

		case M_ZERO:
			if (rw == UIO_READ) {
				c = MIN(iov->iov_len, sizeof (zeroes));
				error = uiomove(zeroes, (int)c, rw, uio);
				break;
			}
			/* else it's a write, fall through to NULL case */
			/*FALLTHROUGH*/

		case M_NULL:
			if (rw == UIO_READ)
				return (0);
			c = iov->iov_len;
			iov->iov_base += c;
			iov->iov_len -= c;
			uio->uio_offset += c;
			uio->uio_resid -= c;
			break;

		}
	}
	return (error);
fault:
	return (EFAULT);
}

/*ARGSUSED2*/
static int
mmmmap(dev_t dev, off_t off, int prot)
{
	int pf;
	register struct memlist *pmem;
	extern struct memlist *phys_install;

	switch (getminor(dev)) {

	case M_MEM:
		pf = btop(off);
		for (pmem = phys_install;
		    pmem != (struct memlist *)NULL; pmem = pmem->next) {
			if (pf >= btop64(pmem->address) &&
			    pf < btop64(pmem->address + pmem->size))
				return (impl_obmem_pfnum(pf));
		}
		break;

	case M_KMEM:
		if ((pf = hat_getkpfnum((caddr_t)off)) != -1)
			return (pf);
		break;

	case M_ZERO:
		/*
		 * We shouldn't be mmap'ing to /dev/zero here as
		 * mmsegmap() should have already converted
		 * a mapping request for this device to a mapping
		 * using seg_vn for anonymous memory.
		 */
		break;

	}
	return (-1);
}

/*
 * This function is called when a memory device is mmap'ed.
 * Set up the mapping to the correct device driver.
 */
static int
mmsegmap(dev_t dev, off_t off, struct as *as, caddr_t *addrp, off_t len,
    u_int prot, u_int maxprot, u_int flags, struct cred *cred)
{
	struct segvn_crargs vn_a;
	struct segdev_crargs dev_a;
	int error;
	minor_t minor;

	/*
	 * If we are mapping /dev/mem, then use spec_segmap()
	 * to set up the mapping which resolves to using mmmap().
	 */
	minor = getminor(dev);
	if (minor == M_MEM) {
		return (spec_segmap(dev, off, as, addrp, len, prot, maxprot,
		    flags, cred));
	}

	as_rangelock(as);
	if ((flags & MAP_FIXED) == 0) {
		/*
		 * No need to worry about vac alignment on /dev/zero
		 * since this is a "clone" object that doesn't yet exist.
		 */
		map_addr(addrp, len, (off_t)off, minor == M_KMEM);
		if (*addrp == NULL) {
			as_rangeunlock(as);
			return (ENOMEM);
		}
	} else {
		/*
		 * User specified address -
		 * Blow away any previous mappings.
		 */
		(void) as_unmap(as, *addrp, len);
	}

	switch (minor) {
	case M_ZERO:
		/*
		 * Use seg_vn segment driver for /dev/zero mapping.
		 * Passing in a NULL amp gives us the "cloning" effect.
		 */
		vn_a.vp = NULL;
		vn_a.offset = 0;
		vn_a.type = (flags & MAP_TYPE);
		vn_a.prot = prot;
		vn_a.maxprot = maxprot;
		vn_a.flags = flags & ~MAP_TYPE;
		vn_a.cred = cred;
		vn_a.amp = NULL;
		error = as_map(as, *addrp, len, segvn_create, (caddr_t)&vn_a);
		break;

	case M_KMEM:
		/*
		 * Use seg_dev segment driver for /dev/kmem mapping.
		 */
		dev_a.mapfunc = mmmmap;
		dev_a.dev = dev;
		dev_a.offset = off;
		dev_a.prot = (u_char)prot;
		dev_a.maxprot = (u_char)maxprot;
		dev_a.hat_flags = (flags & MAP_FIXED) ? 0 : HAT_KMEM;
		error = as_map(as, *addrp, len, segdev_create, (caddr_t)&dev_a);
		break;

	default:
		error = ENXIO;
	}

	as_rangeunlock(as);
	return (error);
}
