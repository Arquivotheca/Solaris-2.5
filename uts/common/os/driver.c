/*
 * Copyright (c) 1992-1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)driver.c	1.27	94/11/17 SMI"

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/buf.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <sys/map.h>
#include <sys/vnode.h>
#include <sys/fs/snode.h>
#include <sys/open.h>
#include <sys/debug.h>
#include <sys/tnf_probe.h>

/* Don't #include <sys/ddi.h> - it #undef's getmajor() */

#include <sys/sunddi.h>
#include <sys/esunddi.h>
#include <sys/autoconf.h>

#define	UNSAFE_ENTER()	mutex_enter(&unsafe_driver)
#define	UNSAFE_EXIT()	mutex_exit(&unsafe_driver)

/*
 * Configuration-related entry points for nexus and leaf drivers
 */

int
devi_identify(dev_info_t *devi)
{
	register struct dev_ops *ops;
	register struct cb_ops *cb;
	register int error;
	register int (*fn)(dev_info_t *);

	if ((ops = ddi_get_driver(devi)) == NULL ||
	    (fn = ops->devo_identify) == NULL)
		return (DDI_NOT_IDENTIFIED);

	if ((cb = ops->devo_cb_ops) != NULL && !(cb->cb_flag & D_MP)) {
		UNSAFE_ENTER();
		error = (*fn)(devi);
		UNSAFE_EXIT();
		return (error);
	} else
		return ((*fn)(devi));
}

int
devi_probe(dev_info_t *devi)
{
	register struct dev_ops *ops;
	register struct cb_ops *cb;
	register int error;
	register int (*fn)(dev_info_t *);

	ops = ddi_get_driver(devi);
	ASSERT(ops);

	/*
	 * probe(9E) in 2.0 implies that you can get
	 * away with not writing one of these .. so we
	 * pretend we're 'nulldev' if we don't find one (sigh).
	 */
	if ((fn = ops->devo_probe) == NULL)
		return (DDI_PROBE_DONTCARE);

	if ((cb = ops->devo_cb_ops) != NULL && !(cb->cb_flag & D_MP)) {
		UNSAFE_ENTER();
		error = (*fn)(devi);
		UNSAFE_EXIT();
		return (error);
	} else
		return ((*fn)(devi));
}

int
devi_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	register struct dev_ops *ops;
	register struct cb_ops *cb;
	register int error;
	register int (*fn)(dev_info_t *, ddi_attach_cmd_t);

	if ((ops = ddi_get_driver(devi)) == NULL ||
	    (fn = ops->devo_attach) == NULL)
		return (DDI_FAILURE);

	if ((cb = ops->devo_cb_ops) != NULL && !(cb->cb_flag & D_MP)) {
		UNSAFE_ENTER();
		error = (*fn)(devi, cmd);
		UNSAFE_EXIT();
		return (error);
	} else
		return ((*fn)(devi, cmd));
}

int
devi_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	register struct dev_ops *ops;
	register struct cb_ops *cb;
	register int error;
	register int (*fn)(dev_info_t *, ddi_detach_cmd_t);

	if ((ops = ddi_get_driver(devi)) == NULL ||
	    (fn = ops->devo_detach) == NULL)
		return (DDI_FAILURE);

	if ((cb = ops->devo_cb_ops) != NULL && !(cb->cb_flag & D_MP)) {
		UNSAFE_ENTER();
		error = (*fn)(devi, cmd);
		UNSAFE_EXIT();
		return (error);
	} else
		return ((*fn)(devi, cmd));
}

/*
 * This entry point not defined by Solaris 2.0 DDI/DKI, so
 * its inclusion here is somewhat moot.
 */
int
devi_reset(dev_info_t *devi, ddi_reset_cmd_t cmd)
{
	register struct dev_ops *ops;
	register struct cb_ops *cb;
	register int error;
	register int (*fn)(dev_info_t *, ddi_reset_cmd_t);

	if ((ops = ddi_get_driver(devi)) == NULL ||
	    (fn = ops->devo_reset) == NULL)
		return (DDI_FAILURE);

	/*
	 * XXX	Is "RESET_FORCE => no unsafe_mutex" really correct?
	 */
	if ((cb = ops->devo_cb_ops) != NULL && !(cb->cb_flag & D_MP) &&
	    cmd != DDI_RESET_FORCE) {
		UNSAFE_ENTER();
		error = (*fn)(devi, cmd);
		UNSAFE_EXIT();
		return (error);
	} else
		return ((*fn)(devi, cmd));
}

/*
 * Leaf driver entry points
 */

static int
dev_open_unsafe(dev_t *devp, int flag, int type, struct cred *cred,
	struct cb_ops *cb)
{
	int	err;
	label_t	saveq = ttolwp(curthread)->lwp_qsav;

	if (setjmp(&ttolwp(curthread)->lwp_qsav))
		err = EINTR;
	else {
		UNSAFE_ENTER();
		err = (*cb->cb_open)(devp, flag, type, cred);
		UNSAFE_EXIT();
	}
	ttolwp(curthread)->lwp_qsav = saveq;
	return (err);
}

int
dev_open(dev_t *devp, int flag, int type, struct cred *cred)
{
	register struct cb_ops	*cb;

	cb = devopsp[getmajor(*devp)]->devo_cb_ops;
	if (!(cb->cb_flag & D_MP))
		return (dev_open_unsafe(devp, flag, type, cred, cb));
	return ((*cb->cb_open)(devp, flag, type, cred));
}

static int
dev_close_unsafe(dev_t dev, int flag, int type, struct cred *cred,
	struct cb_ops *cb)
{
	int	err;
	label_t	saveq = ttolwp(curthread)->lwp_qsav;

	if (setjmp(&ttolwp(curthread)->lwp_qsav))
		err = EINTR;
	else {
		UNSAFE_ENTER();
		err = (*cb->cb_close)(dev, flag, type, cred);
		UNSAFE_EXIT();
	}
	ttolwp(curthread)->lwp_qsav = saveq;
	return (err);
}

int
dev_close(dev_t dev, int flag, int type, struct cred *cred)
{
	register struct cb_ops	*cb;
	register int error;

	/*
	 * The target driver is held (referenced) until we are
	 * done. (See spec_close).
	 */
	cb = (devopsp[getmajor(dev)])->devo_cb_ops;
	if (!(cb->cb_flag & D_MP))
		error = dev_close_unsafe(dev, flag, type, cred, cb);
	else
		error = (*cb->cb_close)(dev, flag, type, cred);
	return (error);
}

/*
 * We only attempt to find the devinfo node if the driver is *already*
 * in memory and has attached.  If it isn't in memory, we don't load it,
 * - that's left to open(2).  If it is in memory, but not attached,
 * we don't attach it, we just fail.
 *
 * If it is attached, we return the dev_info node and increment the
 * devops so that it won't disappear while we're looking at it..
 */
dev_info_t *
dev_get_dev_info(dev_t dev, int otyp)
{
	register struct dev_ops	*ops;
	register struct cb_ops *cb;
	dev_info_t	*dip;
	register int error;
	major_t major = getmajor(dev);
	register struct devnames *dnp;

#ifdef	lint
	otyp = otyp;
#endif	/* lint */

	if (major >= devcnt)
		return (NULL);

	dnp = &(devnamesp[major]);
	LOCK_DEV_OPS(&(dnp->dn_lock));
	ops = devopsp[major];
	if (ops == NULL || ops->devo_getinfo == NULL) {
		UNLOCK_DEV_OPS(&(dnp->dn_lock));
		return (NULL);
	}

	if ((!CB_DRV_INSTALLED(ops)) || (ops->devo_getinfo == NULL) ||
	    ((dnp->dn_flags & DN_DEVS_ATTACHED) == 0))  {
		UNLOCK_DEV_OPS(&(dnp->dn_lock));
		return (NULL);
	}

	INCR_DEV_OPS_REF(ops);
	UNLOCK_DEV_OPS(&(dnp->dn_lock));

	if ((cb = ops->devo_cb_ops) != NULL && !(cb->cb_flag & D_MP)) {
		UNSAFE_ENTER();
		error = (*ops->devo_getinfo)(NULL, DDI_INFO_DEVT2DEVINFO,
		    (void *)dev, (void **)&dip);
		UNSAFE_EXIT();
	} else
		error = (*ops->devo_getinfo)(NULL, DDI_INFO_DEVT2DEVINFO,
		    (void *)dev, (void **)&dip);

	if (error != DDI_SUCCESS || dip == NULL) {
		LOCK_DEV_OPS(&(dnp->dn_lock));
		DECR_DEV_OPS_REF(ops);
		UNLOCK_DEV_OPS(&(dnp->dn_lock));
		return (NULL);
	}

	return (dip);	/* with one hold outstanding .. */
}

/*
 * The following function does not load the driver if it's not loaded.
 * Returns DDI_FAILURE or the instance number of the given dev_t as
 * interpreted by the device driver.
 */
int
dev_to_instance(dev_t dev)
{
	register struct dev_ops	*ops;
	register struct cb_ops *cb;
	int instance;
	int error;
	major_t major = getmajor(dev);
	register struct devnames *dnp = &(devnamesp[major]);

	if (major >= devcnt)
		return (DDI_FAILURE);

	LOCK_DEV_OPS(&(dnp->dn_lock));
	ops = devopsp[major];
	if (ops == NULL || ops->devo_getinfo == NULL) {
		UNLOCK_DEV_OPS(&(dnp->dn_lock));
		return (DDI_FAILURE);
	}

	if ((!CB_DRV_INSTALLED(ops)) || (ops->devo_getinfo == NULL) ||
	    ((dnp->dn_flags & DN_DEVS_ATTACHED) == 0))  {
		UNLOCK_DEV_OPS(&(dnp->dn_lock));
		return (DDI_FAILURE);
	}

	INCR_DEV_OPS_REF(ops);
	UNLOCK_DEV_OPS(&(dnp->dn_lock));

	if ((cb = ops->devo_cb_ops) != NULL && !(cb->cb_flag & D_MP)) {
		UNSAFE_ENTER();
		error = (*ops->devo_getinfo)(NULL, DDI_INFO_DEVT2INSTANCE,
		    (void *)dev, (void **)&instance);
		UNSAFE_EXIT();
	} else
		error = (*ops->devo_getinfo)(NULL, DDI_INFO_DEVT2INSTANCE,
		    (void *)dev, (void **)&instance);

	LOCK_DEV_OPS(&(dnp->dn_lock));
	DECR_DEV_OPS_REF(ops);
	UNLOCK_DEV_OPS(&(dnp->dn_lock));

	if (error != DDI_SUCCESS)
		return (DDI_FAILURE);
	return (instance);
}

/*
 * bdev_strategy should really be a 'void' function, since
 * the driver's strategy doesn't return anything meaningful
 * (it returns errors and such through the buf(9S) structure).
 * However, this breaks some unbundled products that look at
 * the return value (which they shouldn't).
 */
int
bdev_strategy(struct buf *bp)
{
	register struct cb_ops	*cb;

	/* Kernel probe */
	TNF_PROBE_5(strategy, "io blockio", /* CSTYLED */,
		tnf_device,	device,		bp->b_edev,
		tnf_diskaddr,	block,		bp->b_lblkno,
		tnf_size,	size,		bp->b_bcount,
		tnf_opaque,	buf,		bp,
		tnf_bioflags,	flags,		bp->b_flags);

	cb = devopsp[getmajor(bp->b_edev)]->devo_cb_ops;
	if (!(cb->cb_flag & D_MP)) {
		UNSAFE_ENTER();
		(void) (*cb->cb_strategy)(bp);
		UNSAFE_EXIT();
	} else {
		(void) (*cb->cb_strategy)(bp);
	}
	return (0);
}

int
bdev_print(dev_t dev, caddr_t str)
{
	int err;
	register struct cb_ops	*cb;

	cb = devopsp[getmajor(dev)]->devo_cb_ops;
	if (!(cb->cb_flag & D_MP)) {
		label_t saveq = ttolwp(curthread)->lwp_qsav;

		if (setjmp(&ttolwp(curthread)->lwp_qsav))
			err = EINTR;
		else {
			UNSAFE_ENTER();
			err = (*cb->cb_print)(dev, str);
			UNSAFE_EXIT();
		}
		ttolwp(curthread)->lwp_qsav = saveq;
	} else {
		err = (*cb->cb_print)(dev, str);
	}
	return (err);
}

int
bdev_size(dev_t dev)
{
	/* Unsafe mutex is handled by cdev_prop_op() below */
	return (e_ddi_getprop(dev, VBLK, "nblocks",
	    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS, -1));
}

int
bdev_dump(dev_t dev, caddr_t addr, daddr_t blkno, int blkcnt)
{
	register int err;
	register struct cb_ops	*cb;

	cb = devopsp[getmajor(dev)]->devo_cb_ops;
	if (!(cb->cb_flag & D_MP)) {
		UNSAFE_ENTER();
		err = (*cb->cb_dump)(dev, addr, blkno, blkcnt);
		UNSAFE_EXIT();
	} else
		err = (*cb->cb_dump)(dev, addr, blkno, blkcnt);

	return (err);
}

static int
cdev_read_unsafe(dev_t dev, struct uio *uiop, struct cred *cred,
	struct cb_ops *cb)
{
	int err;
	label_t saveq = ttolwp(curthread)->lwp_qsav;

	if (setjmp(&ttolwp(curthread)->lwp_qsav))
		err = EINTR;
	else {
		UNSAFE_ENTER();
		err = (*cb->cb_read)(dev, uiop, cred);
		UNSAFE_EXIT();
	}
	ttolwp(curthread)->lwp_qsav = saveq;
	return (err);
}

int
cdev_read(dev_t dev, struct uio *uiop, struct cred *cred)
{
	register struct cb_ops	*cb;

	cb = devopsp[getmajor(dev)]->devo_cb_ops;
	if (!(cb->cb_flag & D_MP))
		return (cdev_read_unsafe(dev, uiop, cred, cb));
	return ((*cb->cb_read)(dev, uiop, cred));
}

static int
cdev_write_unsafe(dev_t dev, struct uio *uiop, struct cred *cred,
	struct cb_ops *cb)
{
	int err;
	label_t saveq = ttolwp(curthread)->lwp_qsav;

	if (setjmp(&ttolwp(curthread)->lwp_qsav))
		err = EINTR;
	else {
		UNSAFE_ENTER();
		err = (*cb->cb_write)(dev, uiop, cred);
		UNSAFE_EXIT();
	}
	ttolwp(curthread)->lwp_qsav = saveq;
	return (err);
}

int
cdev_write(dev_t dev, struct uio *uiop, struct cred *cred)
{
	register struct cb_ops	*cb;

	cb = devopsp[getmajor(dev)]->devo_cb_ops;
	if (!(cb->cb_flag & D_MP))
		return (cdev_write_unsafe(dev, uiop, cred, cb));
	return ((*cb->cb_write)(dev, uiop, cred));
}

/*
 * We check to see if we've already got the mutex for the sake
 * of unsafe drivers that do ioctl's on other drivers .. like SunView.
 */
static int
cdev_ioctl_unsafe(dev_t dev, int cmd, int arg, int mode, struct cred *cred,
    int *rvalp, struct cb_ops *cb)
{
	register int	err;
	label_t		saveq = ttolwp(curthread)->lwp_qsav;
	register int	already_held = UNSAFE_DRIVER_LOCK_HELD();

	if (setjmp(&ttolwp(curthread)->lwp_qsav))
		err = EINTR;
	else {
		if (!already_held)
			UNSAFE_ENTER();
		err = (*cb->cb_ioctl)(dev, cmd, arg, mode, cred, rvalp);
		if (!already_held)
			UNSAFE_EXIT();
	}
	ttolwp(curthread)->lwp_qsav = saveq;
	return (err);
}

int
cdev_ioctl(dev_t dev, int cmd, int arg, int mode, struct cred *cred,
    int *rvalp)
{
	register struct cb_ops	*cb;

	cb = devopsp[getmajor(dev)]->devo_cb_ops;
	if (!(cb->cb_flag & D_MP))
		return (cdev_ioctl_unsafe(dev, cmd, arg, mode,
		    cred, rvalp, cb));
	return ((*cb->cb_ioctl)(dev, cmd, arg, mode, cred, rvalp));
}

static int
cdev_devmap_unsafe(dev_t dev, dev_info_t *dip, ddi_devmap_data_t *dvdp,
    ddi_devmap_cmd_t cmd, off_t offset, unsigned int len, unsigned int prot,
    cred_t *credp, struct cb_ops *cb)
{
	int err;
	label_t saveq = ttolwp(curthread)->lwp_qsav;

	if (setjmp(&ttolwp(curthread)->lwp_qsav))
		err = EINTR;
	else {
		UNSAFE_ENTER();
		err = (*cb->cb_devmap)(dev, dip, dvdp, cmd, offset,
		    len, prot, credp);
		UNSAFE_EXIT();
	}
	ttolwp(curthread)->lwp_qsav = saveq;
	return (err);
}

int
cdev_devmap(dev_t dev, dev_info_t *dip, ddi_devmap_data_t *dvdp,
    ddi_devmap_cmd_t cmd, off_t offset, unsigned int len, unsigned int prot,
    cred_t *credp)
{
	register struct cb_ops	*cb;

	cb = devopsp[getmajor(dev)]->devo_cb_ops;
	if (!(cb->cb_flag & D_MP))
		return (cdev_devmap_unsafe(dev, dip, dvdp, cmd, offset,
		    len, prot, credp, cb));
	return ((*cb->cb_devmap)(dev, dip, dvdp, cmd, offset, len, prot,
	    credp));
}

static int
cdev_mmap_unsafe(int (*mapfunc)(dev_t, off_t, int),
    dev_t dev, off_t off, int prot)
{
	register int	pfn;
	register int	already_held = UNSAFE_DRIVER_LOCK_HELD();

	if (!already_held)
		UNSAFE_ENTER();
	pfn = (*mapfunc)(dev, off, prot);
	if (!already_held)
		UNSAFE_EXIT();
	return (pfn);
}

int
cdev_mmap(int (*mapfunc)(dev_t, off_t, int), dev_t dev, off_t off, int prot)
{
	register struct cb_ops	*cb;

	cb = devopsp[getmajor(dev)]->devo_cb_ops;
	if (!(cb->cb_flag & D_MP))
		return (cdev_mmap_unsafe(mapfunc, dev, off, prot));
	return ((*mapfunc)(dev, off, prot));
}

int
cdev_segmap(dev_t dev, off_t off, struct as *as, caddr_t *addrp, off_t len,
	    u_int prot, u_int maxprot, u_int flags, cred_t *credp)
{
	register int	unsafe;
	int err;

	register struct cb_ops	*cb;

	cb = devopsp[getmajor(dev)]->devo_cb_ops;
	unsafe = !(cb->cb_flag & D_MP);
	if (unsafe)
		UNSAFE_ENTER();
	err = (*cb->cb_segmap)(dev, off, as, addrp,
	    len, prot, maxprot, flags, credp);
	if (unsafe)
		UNSAFE_EXIT();
	return (err);
}

static int
cdev_poll_unsafe(dev_t dev, short events, int anyyet, short *reventsp,
    struct pollhead **pollhdrp, struct cb_ops *cb)
{
	int err;
	label_t saveq = ttolwp(curthread)->lwp_qsav;

	if (setjmp(&ttolwp(curthread)->lwp_qsav))
		err = EINTR;
	else {
		UNSAFE_ENTER();
		err = (*cb->cb_chpoll)(dev, events, anyyet, reventsp, pollhdrp);
		UNSAFE_EXIT();
	}
	ttolwp(curthread)->lwp_qsav = saveq;
	return (err);
}

int
cdev_poll(dev_t dev, short events, int anyyet, short *reventsp,
	    struct pollhead **pollhdrp)
{
	register struct cb_ops	*cb;

	cb = devopsp[getmajor(dev)]->devo_cb_ops;
	if (!(cb->cb_flag & D_MP))
		return (cdev_poll_unsafe(dev, events, anyyet, reventsp,
			pollhdrp, cb));
	return ((*cb->cb_chpoll)(dev, events, anyyet, reventsp, pollhdrp));
}

/*
 * A 'size' property can be provided by a VCHR device.
 *
 * Since it's defined as zero for STREAMS devices, so we avoid the
 * overhead of looking it up.  Note also that we don't force an
 * unused driver into memory simply to ask about it's size.  We also
 * don't bother to ask it its size unless it's already been attached
 * (the attach routine is the earliest place the property'll be created)
 *
 * XXX	In an ideal world, we'd call this at VOP_GETATTR() time.
 */
int
cdev_size(dev_t dev)
{
	register major_t maj;
	register struct devnames *dnp;

	if ((maj = getmajor(dev)) >= devcnt)
		return (0);

	dnp = &(devnamesp[maj]);
	if ((dnp->dn_flags & DN_DEVS_ATTACHED) && devopsp[maj]) {
		LOCK_DEV_OPS(&dnp->dn_lock);
		if (devopsp[maj] && devopsp[maj]->devo_cb_ops &&
		    !STREAMSTAB(maj)) {
			UNLOCK_DEV_OPS(&dnp->dn_lock);
			/* Unsafe mutex is handled by cdev_prop_op() below */
			return (e_ddi_getprop(dev, VCHR, "size",
			    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS, 0));
		} else
			UNLOCK_DEV_OPS(&dnp->dn_lock);
	}
	return (0);
}

/*
 * XXX	This routine is poorly named, because block devices can and do
 *	have properties (see bdev_size() above).
 *
 * XXX	fix the comment in devops.h that claims that cb_prop_op
 *	is character-only.
 */
int
cdev_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op, int mod_flags,
    char *name, caddr_t valuep, int *lengthp)
{
	register int		err;
	register struct cb_ops	*cb;

	if ((cb = devopsp[getmajor(dev)]->devo_cb_ops) == NULL)
		return (DDI_PROP_NOT_FOUND);

	if (!(cb->cb_flag & D_MP)) {
		UNSAFE_ENTER();
		err = (*cb->cb_prop_op)(dev, dip, prop_op, mod_flags,
		    name, valuep, lengthp);
		UNSAFE_EXIT();
	} else
		err = (*cb->cb_prop_op)(dev, dip, prop_op, mod_flags,
		    name, valuep, lengthp);
	return (err);
}
