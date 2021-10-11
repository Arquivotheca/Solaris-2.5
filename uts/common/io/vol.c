/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)vol.c	1.52	95/10/16 SMI"

/*
 * VOL: the volume management driver
 */

#include	<sys/types.h>
#include	<sys/param.h>
#include	<sys/buf.h>
#include	<sys/conf.h>
#include	<sys/proc.h>
#include	<sys/user.h>
#include	<sys/cred.h>
#include	<sys/file.h>
#include	<sys/open.h>
#include	<sys/poll.h>
#include	<sys/errno.h>
#include	<sys/ioccom.h>
#include	<sys/cmn_err.h>
#include	<sys/kmem.h>
#include	<sys/uio.h>
#include	<sys/cpu.h>
#include	<sys/modctl.h>
#include	<sys/stat.h>
#include	<sys/dkio.h>
#include	<sys/cdio.h>
#include	<sys/fdio.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>
#include	<sys/fdio.h>
#include	<sys/vol.h>
#include	<sys/session.h>
#include	<sys/systm.h>
#include	<sys/debug.h>


/* names */
#define	VOLNODENAME	"vol"			/* driver name */
#define	VOLLONGNAME	"Volume Management Mux Driver"	/* long driver name */
#define	VOLNBUFPROP	"nbuf"			/* number of bufs property */
#define	VOLRWLOCK	"vol_rwlock"		/* rwlock name */
#define	VOLBSEMA	"vol_bsema"		/* bhead semaphore name */
#define	VOLBMUTEX	"vol_bmutex"		/* bhead mutex name */


static char	*vold_root = NULL;		/* location of vol root */
static u_int	vold_root_len = 0;
#define	VOL_ROOT_DEFAULT	"/vol"		/* default vold_root */
#define	VOL_ROOT_DEFAULT_LEN	(strlen(VOL_ROOT_DEFAULT)) /* it's length */


/*
 * debug stuff
 */

#ifndef	VOLDEBUG
#define	VOLDEBUG	0
#endif

int		voldebug = VOLDEBUG;

#define	DPRINTF		if (voldebug > 0) printf
#define	DPRINTF2	if (voldebug > 1) printf
#define	DPRINTF3	if (voldebug > 2) printf
#define	DPRINTF4	if (voldebug > 3) printf


/*
 * keep kvioc_queue and kvioc_event in sync.  It is important that
 * kve_next and kve_prev are in the same order and relative position
 * in the resepctive structures.
 */
struct kvioc_queue {
	struct kvioc_event *kve_next;
	struct kvioc_event *kve_prev;
};

struct kvioc_event {
	struct kvioc_event *kve_next;
	struct kvioc_event *kve_prev;
	struct vioc_event   kve_event;
};

/*
 * private device info -- controlling device "volctl"
 */
static struct volctl {
	dev_info_t	*ctl_dip;	/* dev info */
	struct buf	*ctl_bhead;	/* bufs to use for strategy */
	u_int		ctl_evcnt;	/* count of events on queue */
	u_int		ctl_stoppoll;	/* threads to stop polling */
	u_int		ctl_maxunit;	/* largest unit # we've seen */
	u_int		ctl_open;	/* control port open count */
	struct kvioc_queue ctl_events;	/* queue of events for vold */
	struct kvioc_event *ctl_evend;	/* pointer to end of queue */
	krwlock_t	ctl_rwlock;	/* readers/writer lock */
	ksema_t		ctl_bsema;	/* semaphore for vol_bhead */
	kmutex_t	ctl_bmutex;	/* mutex for vol_bhead */
	kmutex_t	ctl_muxmutex;	/* mutex for voltab */
	kmutex_t	ctl_evmutex;	/* mutex for events */
	kmutex_t	ctl_insert_mutex;	/* mutex for insert */
	kmutex_t	ctl_s_insert_mutex;	/* serializer for insert */
	kcondvar_t	ctl_insert_cv;		/* condvar for insert */
	int		ctl_insert_rval;	/* return value for insert */
	kmutex_t	ctl_inuse_mutex;	/* mutex for inuse */
	kmutex_t	ctl_s_inuse_mutex;	/* serializer for inuse */
	kcondvar_t	ctl_inuse_cv;	/* condvar for inuse */
	int		ctl_inuse_rval;	/* return value for inuse */
	int		ctl_daemon_pid;	/* pid of daemon at work */
	/* for the cleanup thread daemon */
	kcondvar_t	ctl_daemon_cv;		/* for daemon kicking */
	kmutex_t	ctl_daemon_mx;		/* for daemon kicking */
	int		ctl_daemon_die_flag;	/* daemon die flag */
	kmutex_t	ctl_daemon_death_mx;	/* for killing daemon */
	kcondvar_t	ctl_daemon_death_cv;	/* for signally death */
	struct buf	*ctl_daemon_bhead;	/* bufs to resubmit */
	kmutex_t	ctl_daemon_bh_mx;	/* for daemon_bhead */
	kcondvar_t	ctl_symname_cv;		/* for symname */
	kmutex_t	ctl_symname_mutex;	/* for symname */
	kmutex_t	ctl_s_symname_mutex;	/* for serializing */
	char		*ctl_symname;		/* the symname itself */
	size_t		ctl_symname_len;	/* length of symname field */
	kcondvar_t	ctl_symdev_cv;		/* for symdev */
	kmutex_t	ctl_symdev_mutex;	/* for symdev */
	kmutex_t	ctl_s_symdev_mutex;	/* for serializing */
	char		*ctl_symdev;		/* symdev path name */
	size_t		ctl_symdev_len;		/* length of symdev field */

} volctl;

static struct pollhead vol_pollhead;

/*
 * private device info, per active minor node.
 */
static struct vol_tab {
	dev_t		vol_dev;	/* stacked device */
	struct dev_ops	*vol_devops;	/* stacked dev_ops */
	u_int		vol_bocnt; 	/* open count, block  */
	u_int		vol_cocnt; 	/* open count, character  */
	u_int		vol_locnt;	/* open count, layered */
	u_long		vol_flags;	/* miscellaneous flags */
	int		vol_cancel;	/* cancel flag */
	int		vol_unit;	/* minor number of this struct */
	int		vol_mtype;	/* type of media (for checking) */
	u_longlong_t	vol_id;		/* id of the volume */
	char		*vol_path;	/* path of mapped device */
	size_t		vol_pathlen;	/* length (incl null) of above path */
	enum eject_state vol_eject_status; /* ejection status */
	krwlock_t	vol_rwlock;	/* readers/writer lock */
	kmutex_t	vol_ejmutex;	/* ejection mutex */
	kcondvar_t	vol_ejcv;	/* ejection condvar */
	kmutex_t	vol_inmutex;	/* insertion mutex */
	kcondvar_t	vol_incv;	/* insertion condvar */
	kmutex_t	vol_attr_mutex;	/* attribute mutex */
	kcondvar_t	vol_attr_cv;
	int		vol_attr_err;	/* return errno */
	struct ve_attr	*vol_attr_ptr;	/* return info */
};

static void  *voltab;		/* dynamic voltab */

/* vol_flags */
#define	ST_OPEN		0x0001		/* device is open */
#define	ST_EXCL		0x0002		/* device is open exclusively */
#define	ST_STACKOPEN	0x0004		/* stacked device is open */
#define	ST_ENXIO	0x0008		/* return enxio till close */
#define	ST_CHKMEDIA	0x0010		/* device should be checked b4 i/o */
#define	ST_IOPEND	0x0020		/* i/o waiting around */
#define	ST_RDONLY	0x0040		/* volume is read-only */

/* vol_mtype */
#define	MT_FLOPPY	0x0001		/* floppy that supports FDGETCHANGE */

/* flags to the vol_gettab function */
#define	VGT_NOWAIT	0x01
#define	VGT_WAITSIG	0x02
#define	VGT_NEW		0x04
#define	VGT_CLOSE	0x08
#define	VGT_NDELAY	0x10

/* local functions */
static void 		vol_enqueue(enum vie_event type, void *data);
static int		vol_done(struct buf *bp);
static void		vol_cleanup(void);
static void		vol_unmap(struct vol_tab *);
static void		vol_checkwrite(struct vol_tab *tp,
				struct uio *uiop, int unit);
static struct vol_tab 	*vol_gettab(int unit,
				u_int flags, int *error);
static int		vol_checkmedia(struct vol_tab *tp, int *found_media);
static int		vol_checkmedia_machdep(struct vol_tab *tp);
static int		vol_start_daemon(void);


/* defaults */
#define	DEFAULT_NBUF	20	/* default number of bufs to allocate */
#define	DEFAULT_MAXUNIT	100	/* default number of minor units to alloc */

/* devsw ops */
static int	volopen(dev_t *devp, int flag, int otyp, cred_t *credp);
static int	volclose(dev_t dev, int flag, int otyp, cred_t *credp);
static int	volstrategy(struct buf *bp);
static int	volread(dev_t dev, struct uio *uiop, cred_t *credp);
static int	volwrite(dev_t dev, struct uio *uiop, cred_t *credp);
static int	volprop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op,
			int flags, char *name, caddr_t valuep, int *lengthp);
static int	volioctl(dev_t dev, int cmd, int arg, int mode,
			cred_t *credp, int *rvalp);
static int	volpoll(dev_t dev, short events, int anyyet,
			short *reventsp, struct pollhead **phpp);

static struct cb_ops	vol_cb_ops = {
	volopen,		/* open */
	volclose,		/* close */
	volstrategy,		/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	volread,		/* read */
	volwrite,		/* write */
	volioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	volpoll,		/* poll */
	volprop_op,		/* prop_op */
	(struct streamtab *)0,	/* streamtab */
	D_NEW | D_MP,		/* flags */
};

static int	volidentify(dev_info_t *dip);
static int	volattach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int	voldetach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int	volinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg,
			void **result);

static struct dev_ops	vol_ops = {
	DEVO_REV,		/* rev */
	0,			/* refcnt */
	volinfo,		/* info */
	volidentify,		/* identify */
	nulldev,		/* probe */
	volattach,		/* attach */
	voldetach,		/* detach */
	nulldev,		/* reset */
	&vol_cb_ops,		/* cb_ops */
	(struct bus_ops *)0,	/* bus_ops */
};

extern struct mod_ops	mod_pseudodrvops;
extern struct mod_ops	mod_driverops;

static struct modldrv	vol_driver_info = {
	&mod_driverops,		/* modops */
	VOLLONGNAME,		/* name */
	&vol_ops,		/* dev_ops */
};

static struct modlinkage vol_linkage = {
	MODREV_1,			/* rev */
	{				/* linkage */
		&vol_driver_info,
		NULL,
		NULL,
		NULL,
	},
};

static kmutex_t	floppy_chk_mutex;



/*
 * Virtual driver loader entry points
 */

int
_init(void)
{
	DPRINTF("vol: _init\n");
	return (mod_install(&vol_linkage));
}


int
_fini(void)
{
	DPRINTF("vol: _fini\n");
	return (mod_remove(&vol_linkage));
}


int
_info(struct modinfo *modinfop)
{
	DPRINTF("vol: _info: modinfop %x\n", (int)modinfop);
	return (mod_info(&vol_linkage, modinfop));
}


/*
 * Driver administration entry points
 */

static int
volidentify(dev_info_t *dip)
{
	DPRINTF("vol: identify: dip %x\n", (int)dip);

	if (strcmp(ddi_get_name(dip), VOLNODENAME) == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}


static int
volattach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	struct vol_tab	*tp;
	int		unit;
	int		length;
	int		nbuf;
	int		i;
	int		err = DDI_SUCCESS;



	DPRINTF("vol: attach: %d: dip %x cmd %d\n",
	    ddi_get_instance(dip), (int)dip, (int)cmd);

	unit = ddi_get_instance(dip);

	/* check unit */
	if (unit != 0) {
		return (ENXIO);
	}

	/* check command */
	if (cmd != DDI_ATTACH) {
		cmn_err(CE_CONT, "vol: attach: %d: unknown cmd %d\n",
		    unit, cmd);
		return (DDI_FAILURE);
	}

	if (volctl.ctl_dip != (dev_info_t *)NULL) {
		cmn_err(CE_CONT,
		    "vol: attach: %d: already attached\n", unit);
		return (DDI_FAILURE);
	}

	/* clear device entry, initialize locks, and save dev info */
	bzero((caddr_t)&volctl, sizeof (volctl));
	volctl.ctl_dip = dip;

	rw_init(&volctl.ctl_rwlock, VOLRWLOCK, RW_DEFAULT, (void *)NULL);
	sema_init(&volctl.ctl_bsema, 0, VOLBSEMA, SEMA_DRIVER, (void *)NULL);
	mutex_init(&volctl.ctl_bmutex, VOLBMUTEX, MUTEX_DRIVER, (void *)NULL);

	/* get number of buffers, must use DDI_DEV_T_ANY */
	length = sizeof (nbuf);
	if ((err = ddi_prop_op(DDI_DEV_T_ANY, dip, PROP_LEN_AND_VAL_BUF,
	    0, VOLNBUFPROP, (caddr_t)&nbuf, &length))
	    != DDI_SUCCESS) {
		DPRINTF("vol: couldn't get nbuf prop, using default %d\n",
		    DEFAULT_NBUF);
		nbuf = DEFAULT_NBUF;
		err = 0;	/* no biggie */
	}

	DPRINTF2("vol: attach: %d: nbuf %d\n", ddi_get_instance(dip), nbuf);

	/* allocate buffers to stack with */
	volctl.ctl_bhead = (struct buf *)NULL;
	for (i = 0; (i < nbuf); ++i) {
		struct buf	*bp;

		if ((bp = getrbuf(KM_NOSLEEP)) == (struct buf *)NULL) {
			cmn_err(CE_CONT,
			    "vol: attach: %d: could not allocate buf\n", unit);
			err = ENOMEM;
			goto out;
		}
		bp->b_chain = volctl.ctl_bhead;
		volctl.ctl_bhead = bp;
		sema_v(&volctl.ctl_bsema);
	}

	/* create minor node for /dev/volctl */
	if ((err = ddi_create_minor_node(dip, VOLCTLNAME, S_IFCHR,
	    0, DDI_PSEUDO, 0)) != DDI_SUCCESS) {
		cmn_err(CE_CONT,
		    "vol: attach: %d: ddi_create_minor_node '%s' failed\n",
		    unit, VOLCTLNAME);
		goto out;
	}

	/*
	 * The ddi_soft_state code automatically grows the array
	 * when more is asked for.  DEFAULT_MAXUNIT is
	 * just a reasonable lower bound.
	 */
	ddi_soft_state_init(&voltab,
	    sizeof (struct vol_tab), DEFAULT_MAXUNIT);
	/*
	 * build our 'tp' for unit 0.  makes things look better below
	 */
	ddi_soft_state_zalloc(voltab, 0);
	if ((tp = (struct vol_tab *)ddi_get_soft_state(voltab, 0)) == NULL) {
		cmn_err(CE_CONT, "vol: attach, could not get soft state");
		err = DDI_FAILURE;
		goto out;
	}

	/* build the mapping */
	tp->vol_dev = NODEV;
	tp->vol_devops = NULL;
	rw_init(&tp->vol_rwlock, VOLRWLOCK, RW_DEFAULT, NULL);

	/* initialize my linked list */
	volctl.ctl_events.kve_next =
		(struct kvioc_event *)&volctl.ctl_events;
	volctl.ctl_evcnt = 0;
	volctl.ctl_evend = NULL;

	/* start the cleanup daemon */
	if (vol_start_daemon() != 0) {
		DPRINTF("vol: _init: daemon failed to start\n");
		err = ENXIO;
		/* clean up the rw-lock just created */
		rw_destroy(&tp->vol_rwlock);
		goto out;
	}

out:
	/* cleanup or return success */
	if (err != DDI_SUCCESS) {
		ddi_remove_minor_node(dip, (char *)NULL);
		while (volctl.ctl_bhead != (struct buf *)NULL) {
			struct buf	*bp;

			bp = volctl.ctl_bhead;
			volctl.ctl_bhead = bp->b_chain;
			freerbuf(bp);
		}
		mutex_destroy(&volctl.ctl_bmutex);
		sema_destroy(&volctl.ctl_bsema);
		rw_destroy(&volctl.ctl_rwlock);
		bzero((caddr_t)&volctl, sizeof (volctl));

		/* daemon isn't yet started, so no need to kill it */
		DPRINTF("vol: _init: removing daemon mx\n");
		cv_destroy(&volctl.ctl_daemon_cv);
		mutex_destroy(&volctl.ctl_daemon_mx);
		cv_destroy(&volctl.ctl_daemon_death_cv);
		mutex_destroy(&volctl.ctl_daemon_death_mx);
		mutex_destroy(&volctl.ctl_daemon_bh_mx);
	} else {
		ddi_report_dev(dip);
	}
	return (err);
}


static int
voldetach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	struct vol_tab	*tp;
	struct kvioc_event *kve;
	int		unit;
	int		err;
	int		i;
	int		stat;


	DPRINTF("vol: detach: %d: dip %x cmd %d\n", ddi_get_instance(dip),
	    (int)dip, (int)cmd);

	/* get and check unit */
	if ((unit = ddi_get_instance(dip)) != 0) {
		return (ENXIO);
	}

	switch (cmd) {
		/* cleanup and detach */
	case DDI_DETACH:
		/*
		 * if the daemon has us open, say no without looking
		 * any further.
		 */
		if (volctl.ctl_open > 0) {
			return (DDI_FAILURE);
		}

		/*
		 * Make sure there are no lingering mappings.
		 */
		for (i = 1; i < volctl.ctl_maxunit+1; i++) {
			tp = vol_gettab(i, VGT_NOWAIT, &err);
			if (tp == NULL) {
				continue;
			}
			if (tp->vol_flags & ST_OPEN) {
				DPRINTF("vol: detach: unit %d still open!\n",
				    tp->vol_unit);
				rw_exit(&tp->vol_rwlock);
				return (DDI_FAILURE);
			}
			rw_exit(&tp->vol_rwlock);
		}

		/*
		 * Free various data structures that have been allocated
		 * behind our back.
		 */
		ddi_remove_minor_node(dip, (char *)NULL);

		/*
		 * Clean up allocated stuff.  Also takes care
		 * of freeing any units living there.
		 */
		ddi_soft_state_fini(&voltab);

		/*
		 * Free up anything lurking on the event queue.
		 */
		mutex_enter(&volctl.ctl_evmutex);
		while (volctl.ctl_evcnt > 0) {
			kve = volctl.ctl_events.kve_next;
			volctl.ctl_evcnt--;
			remque(kve);
			kmem_free(kve, sizeof (struct kvioc_event));
		}
		volctl.ctl_evend = NULL;
		mutex_exit(&volctl.ctl_evmutex);

		/*
		 * Return our bufs to the world.
		 */
		while (volctl.ctl_bhead != (struct buf *)NULL) {
			struct buf	*bp;

			bp = volctl.ctl_bhead;
			volctl.ctl_bhead = bp->b_chain;
			freerbuf(bp);
		}

		/*
		 * Get rid of our various locks.
		 */
		mutex_destroy(&volctl.ctl_bmutex);
		sema_destroy(&volctl.ctl_bsema);
		rw_destroy(&volctl.ctl_rwlock);

		/* kill our thread */
		DPRINTF("vol: detach: killing thread\n");

		/* set flag telling daemon thread to die */
		DPRINTF("vol: detach: setting 'die' flag\n");
		volctl.ctl_daemon_die_flag = 1;

		/* poke daemon */
		DPRINTF("vol: detach: poking daemon\n");
		mutex_enter(&volctl.ctl_daemon_mx);
		cv_signal(&volctl.ctl_daemon_cv);
		mutex_exit(&volctl.ctl_daemon_mx);

		/* wait for daemon death */
		DPRINTF("vol: detach: waiting for daemon death\n");
		mutex_enter(&volctl.ctl_daemon_death_mx);
		cv_wait(&volctl.ctl_daemon_death_cv,
		    &volctl.ctl_daemon_death_mx);
		mutex_exit(&volctl.ctl_daemon_death_mx);

		/* cleanup */
		DPRINTF("vol: detach: cleaning up after daemon\n");
		cv_destroy(&volctl.ctl_daemon_cv);
		mutex_destroy(&volctl.ctl_daemon_mx);
		cv_destroy(&volctl.ctl_daemon_death_cv);
		mutex_destroy(&volctl.ctl_daemon_death_mx);
		mutex_destroy(&volctl.ctl_daemon_bh_mx);

		/*
		 * A nice fresh volctl, for the next attach.
		 */
		bzero((caddr_t)&volctl, sizeof (volctl));

		/* release volmgt root pathname */
		if (vold_root != NULL) {
			kmem_free(vold_root, vold_root_len+1);
			vold_root = NULL;
			vold_root_len = 0;
		}

		stat = DDI_SUCCESS;
		break;

	default:
		cmn_err(CE_CONT, "vol: detach: %d: unknown cmd %d\n",
		    unit, cmd);
		stat = DDI_FAILURE;
		break;
	}
	return (stat);
}


/* ARGSUSED */
static int
volinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	dev_t		dev;
	int		unit;
	struct vol_tab	*tp;
	int		err = DDI_SUCCESS;

	DPRINTF("vol: info: dip %x cmd %d arg %x (%d.%d) result %x\n",
	    (int)dip, (int)cmd, (int)arg, (int)getmajor((dev_t)arg),
	    (int)getminor((dev_t)arg), (int)result);

	dev = (dev_t)arg;
	unit = getminor(dev);
	if ((tp = vol_gettab(unit, VGT_NOWAIT, &err)) == NULL) {
		return (err);
	}

	/* process command */
	switch (cmd) {

	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *)volctl.ctl_dip;
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)unit;
		break;

	default:
		cmn_err(CE_CONT, "vol: info: %d: unknown cmd %d\n",
		    unit, cmd);
		err = DDI_FAILURE;
		break;
	}

	/* release lock, return success */
	rw_exit(&tp->vol_rwlock);
	return (err);
}


/*
 * Common entry points
 */

/* ARGSUSED3 */
static int
volopen(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	int		unit;
	struct vol_tab	*tp;
	int		err = 0;
	u_int		gflags;


	DPRINTF("vol: open: devp %x (%d.%d) flag %x otyp %x credp %x\n",
	    (int)devp, (int)getmajor(*devp), (int)getminor(*devp),
	    flag, otyp, (int)credp);

	unit = getminor(*devp);


	gflags = VGT_NEW|VGT_WAITSIG;

	/* implement non-blocking open */
	if (flag & FNDELAY) {
		gflags |= VGT_NDELAY;
	}

	if ((unit == 0) && ((otyp == OTYP_BLK) || (otyp == OTYP_CHR))) {
		volctl.ctl_open++;
	}

	/* get our vol structure for this unit */
	if ((tp = vol_gettab(unit, gflags, &err)) == NULL) {
		DPRINTF("vol: open: gettab on unit %d, err %d\n", unit, err);
		if (err == EAGAIN) {
			err = EIO;		/* convert to usable errno */
		}
		return (err);
	}

	if (err == EAGAIN) {
		err = 0;
	}

	/* check for opening read-only with write flag set */
	if ((flag & FWRITE) && (tp->vol_flags & ST_RDONLY)) {
		err = EROFS;
		goto out;
	}

	/* implement exclusive use */
	if (((flag & FEXCL) && (tp->vol_flags & ST_OPEN)) ||
	    (tp->vol_flags & ST_EXCL)) {
		err = EBUSY;
		goto out;
	}
	rw_exit(&tp->vol_rwlock);
	rw_enter(&tp->vol_rwlock, RW_WRITER);
	if (flag & FEXCL) {
		tp->vol_flags |= ST_EXCL;
	}

	/* count and flag open */
	if (otyp == OTYP_BLK) {
		tp->vol_bocnt++;		/* user block device open */
	} else if (otyp == OTYP_CHR) {
		tp->vol_cocnt++;		/* user character device */
	} else {
		tp->vol_locnt++;	/* kernel open */
	}

	tp->vol_flags |= ST_OPEN;

	/* release lock, return err */
out:
	rw_exit(&tp->vol_rwlock);
	return (err);
}


/* ARGSUSED3 */
static int
volclose(dev_t dev, int flag, int otyp, cred_t *credp)
{
	int		unit;
	struct vol_tab	*tp;
	int		err = 0;

	DPRINTF("vol: close: dev %d.%d flag %x otyp %x credp %x\n",
	    (int)getmajor(dev), (int)getminor(dev), flag, otyp, (int)credp);

	unit = getminor(dev);

	if ((tp = vol_gettab(unit, VGT_NOWAIT|VGT_CLOSE, &err)) == NULL) {
		return (0);
	}

	rw_exit(&tp->vol_rwlock);
	rw_enter(&tp->vol_rwlock, RW_WRITER);
	if (otyp == OTYP_BLK) {
		tp->vol_bocnt = 0;	/* block opens */
	} else if (otyp == OTYP_CHR) {
		tp->vol_cocnt = 0;	/* character opens */
	} else {
		tp->vol_locnt--;	/* kernel opens */
	}

	if ((tp->vol_bocnt == 0) && (tp->vol_cocnt == 0) &&
	    (tp->vol_locnt == 0)) {
		tp->vol_flags &= ~(ST_OPEN|ST_EXCL);
	}


	/* close lower device, if necessary */
	if ((! (tp->vol_flags & ST_OPEN)) && (tp->vol_flags & ST_STACKOPEN)) {
		ASSERT(tp->vol_dev != NODEV);
		if ((err = dev_close(tp->vol_dev, FREAD,
		    OTYP_LYR, kcred)) != 0) {
			DPRINTF("vol: close: %d: dev_close err %d\n",
			    unit, err);
		}
		tp->vol_flags &= ~ST_STACKOPEN;
		rw_exit(&tp->vol_rwlock);
		rw_enter(&tp->vol_rwlock, RW_WRITER);
		tp->vol_dev = NODEV;
		tp->vol_devops = NULL;
	}

	/*
	 * If we've closed the device clean up after ourselves
	 * and tell the daemon that we're done
	 */
	if (((tp->vol_flags & ST_OPEN) == 0) && (unit != 0)) {
#ifdef	notdef
		vol_unmap(tp);
		rwunlock = 0;
#endif
		vol_enqueue(VIE_CLOSE, (void *)&unit);
	}
	/* "tp" is invalid after vol_unmap!!! */

	if (unit == 0) {
		rw_exit(&tp->vol_rwlock);
		volctl.ctl_open = 0;
		volctl.ctl_daemon_pid = 0;
		vol_cleanup();
		return (0);
	}
	/* release lock, return success */
	rw_exit(&tp->vol_rwlock);
	return (err);
}


static int
volstrategy(struct buf *bp)
{
	int		unit;
	struct vol_tab	*tp;
	struct buf	*mybp;
	int		err = 0;


	DPRINTF2("vol: strategy: bp %x dev %d.%d off %d len %d\n",
	    (int)bp, (int)getmajor(bp->b_edev), (int)getminor(bp->b_edev),
	    dbtob(bp->b_blkno), bp->b_bcount);

	unit = getminor(bp->b_edev);
	if (unit == 0) {
		bp->b_resid = bp->b_bcount;
		bioerror(bp, ENXIO);
		biodone(bp);
		return (0);
	}

	if ((tp = vol_gettab(unit, VGT_WAITSIG, &err)) == NULL) {
		bp->b_resid = bp->b_bcount;
		bioerror(bp, err);
		biodone(bp);
		DPRINTF("vol: strategy: gettab error %d\n", err);
		return (0);
	}

	if ((tp->vol_flags & ST_OPEN) == 0) {
		/* it's not even open */
		bp->b_resid = bp->b_bcount;
		bioerror(bp, ENXIO);
		rw_exit(&tp->vol_rwlock);
		biodone(bp);
		DPRINTF("vol: strategy: device not even open (ENXIO)\n");
		return (0);
	}

	/* allocate new buffer */
	sema_p(&volctl.ctl_bsema);
	mutex_enter(&volctl.ctl_bmutex);
	if (volctl.ctl_bhead == (struct buf *)NULL) {
		panic("vol: strategy: bhead == NULL");
	}
	mybp = volctl.ctl_bhead;
	volctl.ctl_bhead = mybp->b_chain;
	mutex_exit(&volctl.ctl_bmutex);

	/* setup buffer */
	ASSERT(tp->vol_dev != NODEV);
	*mybp = *bp;		/* XXX copy everything (structure copy) */
	mybp->b_flags |= B_KERNBUF;
	mybp->b_forw = mybp->b_back = mybp;
	mybp->av_forw = mybp->av_back = (struct buf *)NULL;
	mybp->b_dev = cmpdev(tp->vol_dev);
	mybp->b_iodone = vol_done;
	mybp->b_edev = tp->vol_dev;
	mybp->b_chain = bp;	/* squirrel away old buf for vol_done */
	sema_init(&mybp->b_io, 0, "bp io semaphore", SEMA_DRIVER,
	    (void *)NULL);
	sema_init(&mybp->b_sem, 0, "bp semaphore", SEMA_DRIVER,
	    (void *)NULL);

	if (tp->vol_flags & ST_CHKMEDIA) {
		err = vol_checkmedia(tp, NULL);
		if (err) {
			/* release lock, return err */
			bp->b_resid = bp->b_bcount;
			bioerror(bp, err);
			rw_exit(&tp->vol_rwlock);
			biodone(bp);
			DPRINTF("vol: strategy: precheck failed, error %d\n",
			    err);
			return (0);
		}
	}

	/* release lock, pass request on to stacked driver */
	rw_exit(&tp->vol_rwlock);

	/* pass request on to stacked driver */
	return (bdev_strategy(mybp));
}


static int
vol_done(struct buf *mybp)
{
	struct buf		*bp = mybp->b_chain;
	int			flags;
	int			err = 0;
#ifdef	notdef
	struct vol_tab		*tp;
	int			unit;
	int			badnews;
#endif

	DPRINTF2("vol: done: mybp %x bp %x dev %d.%d off %d len %d\n",
	    (int)mybp, (int)bp, (int)getmajor(bp->b_edev),
	    (int)getminor(bp->b_edev), dbtob(bp->b_blkno), bp->b_bcount);

#ifdef	notdef
	if (mybp->b_error) {

		unit = getminor(bp->b_edev);
		tp = vol_gettab(unit, VGT_NOWAIT, &err);

		if (tp == NULL || !(tp->vol_flags & ST_CHKMEDIA)) {
			DPRINTF("vol: done: tp = 0x%x, flags = 0x%x\n",
			    (u_int)tp, (u_int)tp->vol_flags);
			goto out;
		}

		if (err) {
			/* holy cow.  this really shouldn't happend */
			DPRINTF("vol: done: err = %d\n", err);
		}

		badnews = vol_checkmedia_machdep(tp);
		if (badnews) {
			/* there's no media in the drive */

			DPRINTF("vol: done: no media\n");

			/* unmap the device */
			rw_exit(&tp->vol_rwlock);
			rw_enter(&tp->vol_rwlock, RW_WRITER);
			tp->vol_devops = NULL;
			tp->vol_dev = NODEV;

			vol_enqueue(VIE_REMOVED, (void *)&unit);

			tp->vol_flags |= ST_IOPEND;

			/* hand the buffer to the thread */
			mutex_enter(&volctl.ctl_daemon_bh_mx);
			mybp->b_forw = volctl.ctl_daemon_bhead;
			volctl.ctl_daemon_bhead = mybp;
			mutex_exit(&volctl.ctl_daemon_bh_mx);
			rw_exit(&tp->vol_rwlock);
			return (0);
		}
		rw_exit(&tp->vol_rwlock);
	}
#else
	if (mybp->b_error) {
		DPRINTF("vol: error %d from device (should retry)\n",
		    mybp->b_error);
	}
#endif

#ifdef	notdef
out:
#endif
	/* copy status */
	flags = bp->b_flags;
	bp->b_flags = mybp->b_flags & ~B_KERNBUF;
	bp->b_flags |= flags & B_KERNBUF;
	bp->b_un = mybp->b_un;
	bp->b_oerror = mybp->b_oerror;
	bp->b_resid = mybp->b_resid;
	bp->b_error = mybp->b_error;

	/* free buffer */
	mutex_enter(&volctl.ctl_bmutex);
	mybp->b_chain = volctl.ctl_bhead;
	volctl.ctl_bhead = mybp;
	mutex_exit(&volctl.ctl_bmutex);

	/* release semaphore */
	sema_v(&volctl.ctl_bsema);

	/* continue on with biodone() */
	biodone(bp);
	return (err);
}


/* ARGSUSED */
static int
volread(dev_t dev, struct uio *uiop, cred_t *credp)
{
	struct vol_tab	*tp;
	int		unit;
	int		err = 0;
	int		err1;
	int		found_media;

	DPRINTF2("vol: read: dev %d.%d uiop %x credp %x\n",
	    (int)getmajor(dev), (int)getminor(dev), (int)uiop, (int)credp);

	unit = getminor(dev);
	if (unit == 0) {
		return (ENXIO);
	}

	if ((tp = vol_gettab(unit, VGT_WAITSIG, &err)) == NULL) {
		DPRINTF("vol: read: gettab on unit %d, err %d\n", unit, err);
		if (err == EAGAIN) {
			err = EIO;		/* convert to usable errno */
		}
		return (err);
	}

	if (! (tp->vol_flags & ST_OPEN)) {
		err = ENXIO;
		goto out;
	}

	if (tp->vol_flags & ST_CHKMEDIA) {
		err = vol_checkmedia(tp, NULL);
		if (err) {
			goto out;
		}
	}

	/*CONSTANTCONDITION*/
	while (1) {
		/* read data */
		ASSERT(tp->vol_dev != NODEV);
		err = cdev_read(tp->vol_dev, uiop, kcred);

		if (err && tp->vol_flags & ST_CHKMEDIA) {
			err1 = vol_checkmedia(tp, &found_media);

			/*
			 * if we got an error and media was actually
			 * in the drive, just return the error.
			 */
			if (found_media) {
				break;
			}

			/*
			 * probably a cancel on the i/o.
			 */
			if (err1) {
				err = err1;
				break;
			}
		} else {
			break;
		}
	}


	/* release lock, return success */
out:
	rw_exit(&tp->vol_rwlock);
	return (err);
}


/* ARGSUSED */
static int
volwrite(dev_t dev, struct uio *uiop, cred_t *credp)
{
	struct vol_tab	*tp;
	int		unit;
	int		err = 0;
	int		err1;
	int		found_media;

	DPRINTF2("vol: write: dev %d.%d uiop %x credp %x\n",
	    (int)getmajor(dev), (int)getminor(dev), (int)uiop, (int)credp);

	unit = getminor(dev);
	if (unit == 0) {
		return (ENXIO);
	}

	if ((tp = vol_gettab(unit, VGT_WAITSIG, &err)) == NULL) {
		DPRINTF("vol: write: gettab on unit %d, err %d\n", unit, err);
		if (err == EAGAIN) {
			err = EIO;		/* convert to usable errno */
		}
		return (err);
	}

	if (! (tp->vol_flags & ST_OPEN)) {
		err = ENXIO;
		goto out;
	}

	vol_checkwrite(tp, uiop, unit);

	if (tp->vol_flags & ST_CHKMEDIA) {
		err = vol_checkmedia(tp, NULL);
		if (err) {
			goto out;
		}
	}

	/*CONSTANTCONDITION*/
	while (1) {
		/* write data */
		ASSERT(tp->vol_dev != NODEV);
		err = cdev_write(tp->vol_dev, uiop, kcred);

		if (err && tp->vol_flags & ST_CHKMEDIA) {
			err1 = vol_checkmedia(tp, &found_media);

			/*
			 * if we got an error and media was actually
			 * in the drive, just return the error.
			 */
			if (found_media) {
				break;
			}

			/*
			 * probably a cancel on the i/o.
			 */
			if (err1) {
				err = err1;
				break;
			}
		} else {
			break;
		}
	}

	/* release lock, return err */
out:
	rw_exit(&tp->vol_rwlock);
	return (err);
}


/*
 * Check the write to see if we are writing over the label
 * on this unit.  If we are, let the daemon know.
 */
static void
vol_checkwrite(struct vol_tab *tp, struct uio *uiop, int unit)
{
	/*
	 * XXX: this is VERY incomplete.
	 * This only works with a full label write of the Sun label.
	 */
	if (uiop->uio_loffset == 0) {
		vol_enqueue(VIE_NEWLABEL, (void *)&unit);
		/*
		 * We now need to invalidate the blocks that
		 * are cached for both the device we point at.
		 * Odds are good that the label was written
		 * through the raw device, and we don't want to
		 * read stale stuff.
		 */
		binval(tp->vol_dev);		/* XXX: not DDI compliant */
	}
}


static int
volprop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op,
	int flags, char *name, caddr_t valuep, int *lengthp)
{
	int		unit;
	struct vol_tab	*tp;
	dev_info_t	*stackdip;
	int		err = 0;


	DPRINTF2("vol: prop_op: dev %d.%d dip %x prop_op %d flags %x\n",
	    (int)getmajor(dev), (int)getminor(dev), (int)dip, (int)prop_op,
	    flags);
	DPRINTF2("     name '%s' valuep %x lengthp %x\n",
	    name, (int)valuep, (int)lengthp);

	/* send our props on to ddi_prop_op */
	if ((strcmp(name, "name") == 0) ||
	    (strcmp(name, "parent") == 0) ||
	    (strcmp(name, VOLNBUFPROP) == 0)) {
		err = ddi_prop_op(dev, dip, prop_op, flags,
		    name, valuep, lengthp);
		return (err);
	}

	unit = getminor(dev);
	if (unit == 0) {
		return (0);
	}

	if ((tp = vol_gettab(unit, VGT_NOWAIT, &err)) == NULL) {
		DPRINTF("vol: prop_op: gettab on unit %d, err %d\n", unit,
		    err);
		return (err);
	}

	/* get stacked dev info */
	ASSERT(tp->vol_devops != NULL);
	if ((err = (*(tp->vol_devops->devo_getinfo))((dev_info_t *)NULL,
	    DDI_INFO_DEVT2DEVINFO, (void *)tp->vol_dev, (void *)&stackdip))
	    != DDI_SUCCESS) {
		cmn_err(CE_CONT,
		    "vol: prop_op: %d: could not get child dev info err %d\n",
		    unit, err);
		goto out;
	}

	/* pass request on to stacked driver */
	err = cdev_prop_op(tp->vol_dev, stackdip, prop_op, flags,
	    name, valuep, lengthp);

	if (err) {
		DPRINTF("vol: cdev_prop_op: err = %d\n", err);
	}

	/* release lock, return err */
out:
	rw_exit(&tp->vol_rwlock);
	return (err);
}


/* ARGSUSED */
static int
volioctl(dev_t dev, int cmd, int arg, int mode, cred_t *credp, int *rvalp)
{
	int		unit;
	struct vol_tab	*tp;
	int		err = 0;
	int		pid;
	size_t		len;



	DPRINTF("volioctl: dev=%d.%d cmd=%x arg=%x mode=%x\n",
	    (int)getmajor(dev), (int)getminor(dev), cmd, arg, mode);

	unit = getminor(dev);
	if (unit == 0) {
		/* commands from vold */
		switch (cmd) {

		/*
		 * The daemon will call this if it's using the driver.
		 */
		case VOLIOCDAEMON:
			if (drv_priv(credp) != 0) {
				err = EPERM;
				break;
			}
			if (volctl.ctl_daemon_pid != 0) {
				/* already a daemon running! */
				err = EBUSY;
				break;
			}
			volctl.ctl_daemon_pid = arg;
			break;

		/*
		 * Establish a mapping between a unit (minor number)
		 * and a lower level driver (dev_t).
		 */
		case VOLIOCMAP: {
			struct vioc_map vim;

			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}

			err = ddi_copyin((caddr_t)arg, (caddr_t)&vim,
			    sizeof (struct vioc_map), mode);
			if (err) {
				DPRINTF("vol: map:copyin broke %d\n", err);
				break;
			}
			tp = vol_gettab(vim.vim_unit,
			    VGT_NOWAIT|VGT_NEW, &err);
			if (tp == NULL) {
				DPRINTF("vol: map:null on vol %d, err %d\n",
				    (int)vim.vim_unit, err);
				break;
			}
			if (err) {
				err = 0;
			}
			rw_exit(&tp->vol_rwlock);
			DPRINTF2("vol: 0x%x writelock1\n",
			    (int)&tp->vol_rwlock);
			mutex_enter(&volctl.ctl_muxmutex);
			rw_enter(&tp->vol_rwlock, RW_WRITER);
			tp->vol_id = vim.vim_id;

			if (vim.vim_flags & VIM_FLOPPY) {
				tp->vol_flags |= ST_CHKMEDIA;
				tp->vol_mtype = MT_FLOPPY;
			} else {
				/* clear data (in case of previous use) */
				tp->vol_flags &= ~ST_CHKMEDIA;
				tp->vol_mtype = 0;
			}

			/* is this a read-only volume? */
			if (vim.vim_flags & VIM_RDONLY) {
				tp->vol_flags |= ST_RDONLY;
			}

			/*
			 * if vim.vim_dev == NODEV, it means that
			 * the daemon is unblocking a "nodelay"
			 * request.
			 */
			if (vim.vim_dev != NODEV) {
				/* build the mapping */
				tp->vol_dev = vim.vim_dev;
				tp->vol_devops =
					ddi_hold_installed_driver(
					    getmajor(tp->vol_dev));
				if (tp->vol_devops == NULL) {
					rw_exit(&tp->vol_rwlock);
					mutex_exit(&volctl.ctl_muxmutex);
					err = ENODEV;
					DPRINTF("vol: map:hold_inst broke\n");
					break;
				}

				/* clear any pending cancel */
				tp->vol_cancel = FALSE;
				if (vim.vim_pathlen &&
				    (vim.vim_pathlen < MAXNAMELEN*2)) {
					tp->vol_path = (char *)kmem_alloc(
					    vim.vim_pathlen, KM_SLEEP);
					ddi_copyin(vim.vim_path, tp->vol_path,
					    vim.vim_pathlen, mode);
					tp->vol_pathlen = vim.vim_pathlen;
				}

				/*
				 * If there's a some pending data
				 * in the retry thread for this
				 * device, unload it
				 */
				if (tp->vol_flags & ST_IOPEND) {
					/* poke the cleanup thread */
					mutex_enter(&volctl.ctl_daemon_mx);
					cv_signal(&volctl.ctl_daemon_cv);
					mutex_exit(&volctl.ctl_daemon_mx);
				}
			} else {
				tp->vol_dev = NODEV;
				tp->vol_devops = NULL;
			}
			cv_broadcast(&tp->vol_incv);
			rw_exit(&tp->vol_rwlock);
			mutex_exit(&volctl.ctl_muxmutex);
			break;
		}

		/*
		 * Break a mapping established with the above
		 * map ioctl.
		 */
		case VOLIOCUNMAP: {
			minor_t	unit;

			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}

			err = ddi_copyin((caddr_t)arg, (caddr_t)&unit,
			    sizeof (minor_t), mode);
			if (err) {
				DPRINTF("vol: unmap:copyin broke %d\n", err);
				break;
			}
			tp = vol_gettab(unit, VGT_NOWAIT, &err);
			if (tp == NULL) {
				return (err);
			}

			rw_exit(&tp->vol_rwlock);
			rw_enter(&tp->vol_rwlock, RW_WRITER);
			tp->vol_dev = NODEV;
			tp->vol_devops = NULL;
			tp->vol_flags &= ~ST_RDONLY;
			if (tp->vol_path) {
				kmem_free(tp->vol_path,	tp->vol_pathlen);
			}
			tp->vol_path = 0;
			rw_exit(&tp->vol_rwlock);
			break;
		}

		/*
		 * Get an event.  This is used by calling this
		 * ioctl until it returns EWOULDBLOCK.  poll(2)
		 * is the mechanism for waiting around for an
		 * event to happen.
		 */
		case VOLIOCEVENT: {
			struct kvioc_event *kve = NULL;

			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}
			mutex_enter(&volctl.ctl_evmutex);
			if (volctl.ctl_evcnt) {
				kve = volctl.ctl_events.kve_next;
				volctl.ctl_evcnt--;
				if (volctl.ctl_evcnt == 0) {
					volctl.ctl_evend = NULL;
				}
				remque(kve);
			}
			mutex_exit(&volctl.ctl_evmutex);
			if (kve) {
				err = ddi_copyout((caddr_t)&kve->kve_event,
				    (caddr_t)arg, sizeof (struct vioc_event),
				    mode);
				if (err) {
					/* add it back on err */
					mutex_enter(&volctl.ctl_evmutex);
					insque(kve, &volctl.ctl_events);
					volctl.ctl_evcnt++;
					mutex_exit(&volctl.ctl_evmutex);
					DPRINTF("vol: event: copyout %d\n",
					    err);
					break;
				}

				kmem_free(kve, sizeof (struct kvioc_event));
			} else {
				err = EWOULDBLOCK;
			}
			break;
		}


		/*
		 * Deliver status (eject or don't eject) to a pending
		 * eject ioctl.  That ioctl will then send down the
		 * eject to the device (or not).
		 */
		case VOLIOCEJECT: {
			struct vioc_eject viej;

			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				DPRINTF("VOLIOCEJECT: pid error\n");
				err = EPERM;
				break;
			}

			err = ddi_copyin((caddr_t)arg, (caddr_t)&viej,
			    sizeof (struct vioc_eject), mode);
			if (err) {
				DPRINTF("VOLIOCEJECT: copyin error\n");
				break;
			}
			tp = vol_gettab(viej.viej_unit, VGT_NOWAIT, &err);
			if (tp == NULL) {
				DPRINTF("VOLIOCEJECT: gettab error\n");
				break;
			}
			rw_exit(&tp->vol_rwlock);

			mutex_enter(&tp->vol_ejmutex);
			tp->vol_eject_status = viej.viej_state;
			cv_broadcast(&tp->vol_ejcv);
			mutex_exit(&tp->vol_ejmutex);
			break;
		}

		/* Daemon response to setattr from user */
		case VOLIOCDSATTR: {
			struct vioc_dattr	vda;

			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}

			err = ddi_copyin((caddr_t)arg, (caddr_t)&vda,
			    sizeof (struct vioc_dattr), mode);
			if (err) {
				break;
			}
			tp = vol_gettab(vda.vda_unit, VGT_NOWAIT, &err);
			if (tp == NULL) {
				break;
			}

			mutex_enter(&tp->vol_attr_mutex);
			tp->vol_attr_err = vda.vda_errno;
			cv_signal(&tp->vol_attr_cv);
			mutex_exit(&tp->vol_attr_mutex);
			rw_exit(&tp->vol_rwlock);
			break;
		}
		/* Daemon response to getattr from user */
		case VOLIOCDGATTR: {
			struct vioc_dattr	vda;

			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}

			err = ddi_copyin((caddr_t)arg, (caddr_t)&vda,
			    sizeof (struct vioc_dattr), mode);
			if (err) {
				break;
			}
			tp = vol_gettab(vda.vda_unit, VGT_NOWAIT, &err);
			if (tp == NULL) {
				break;
			}

			mutex_enter(&tp->vol_attr_mutex);
			tp->vol_attr_err = vda.vda_errno;
			if (vda.vda_errno == 0) {
				(void) strncpy(tp->vol_attr_ptr->viea_value,
				    vda.vda_value, MAX_ATTR_LEN);
			}
			cv_signal(&tp->vol_attr_cv);
			mutex_exit(&tp->vol_attr_mutex);
			rw_exit(&tp->vol_rwlock);
			break;

		}

		/* Daemon response to insert from user */
		case VOLIOCDCHECK:
			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}

			mutex_enter(&volctl.ctl_insert_mutex);
			volctl.ctl_insert_rval = arg;
			cv_signal(&volctl.ctl_insert_cv);
			mutex_exit(&volctl.ctl_insert_mutex);
			break;

		/* Daemon response to inuse from user */
		case VOLIOCDINUSE:
			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}

			mutex_enter(&volctl.ctl_inuse_mutex);
			volctl.ctl_inuse_rval = arg;
			cv_signal(&volctl.ctl_inuse_cv);
			mutex_exit(&volctl.ctl_inuse_mutex);
			break;

		/* Daemon response to inuse from user */
		case VOLIOCFLAGS: {
			struct vioc_flags vfl;

			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}

			if ((err = ddi_copyin((caddr_t)arg, (caddr_t)&vfl,
			    sizeof (struct vioc_flags), mode)) != 0) {
				break;
			}
			if ((tp = vol_gettab(vfl.vfl_unit, VGT_NOWAIT,
			    &err)) == NULL) {
				break;
			}
			/*
			 * vol_gettab() returns the vol_tab struct pointed
			 * to by tp locked in reader mode -- but, to
			 * change something in that struct, we need to
			 * lock it in writer mode
			 */
			rw_exit(&tp->vol_rwlock);
			DPRINTF2("vol: 0x%x writelock1\n",
			    (int)&tp->vol_rwlock);
			rw_enter(&tp->vol_rwlock, RW_WRITER);

			/* set or clear the ST_ENXIO flag */
			if (vfl.vfl_flags & VFL_ENXIO) {
				tp->vol_flags |= ST_ENXIO;
#ifdef	DEBUG_ENXIO
				(void) printf(
	"volioctl: VOLIOCFLAGS(ST_ENXIO), unit %d (flags=0x%x, tp=0x%x)\n",
				    vfl.vfl_unit, tp->vol_flags, (char *)tp);
#endif
			} else {
				tp->vol_flags &= ~ST_ENXIO;
#ifdef	DEBUG_ENXIO
				(void) printf(
	"volioctl: VOLIOCFLAGS(0), unit %d (flags=0x%x, tp=0x%x)\n",
				    vfl.vfl_unit, tp->vol_flags, (char *)tp);
#endif
			}
			DPRINTF(
			    "vol: volioctl: %s ST_ENXIO flag for unit %d\n",
			    (vfl.vfl_flags & VFL_ENXIO) ? "set" : "cleared",
			    vfl.vfl_unit);

			rw_exit(&tp->vol_rwlock);
			break;
		}

		/* daemon response to symname request from user */
		case VOLIOCDSYMNAME: {
			char	*symname_tmp;

			/* ensure it's the daemon talking to use */
			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}

			/* make room for the string */
			symname_tmp = (char *)kmem_alloc(VOL_SYMNAME_LEN+1,
			    KM_SLEEP);

			/* get the string */
			err = copyinstr((caddr_t)arg,
			    (caddr_t)symname_tmp, VOL_SYMNAME_LEN, &len);

			/* lock data struct */
			mutex_enter(&volctl.ctl_symname_mutex);

			/* if copy worked then make a min-sized copy */
			if (err == 0) {
				volctl.ctl_symname = (char *)kmem_alloc(len+1,
				    KM_SLEEP);
				volctl.ctl_symname_len = len;
				(void) strcpy(volctl.ctl_symname, symname_tmp);
			}

			/* free max room allocated earlier */
			kmem_free(symname_tmp, VOL_SYMNAME_LEN+1);

			/* signal waiter that we have the info */
			cv_signal(&volctl.ctl_symname_cv);
			mutex_exit(&volctl.ctl_symname_mutex);

			break;
		}

		/* daemon response to symname request from user */
		case VOLIOCDSYMDEV: {
			char		*symdev_tmp;

			/* ensure it's the daemon talking to use */
			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}

			/* make enough room */
			symdev_tmp = kmem_alloc(VOL_SYMDEV_LEN+1, KM_SLEEP);

			/* get the string */
			err = copyinstr((caddr_t)arg,
			    (caddr_t)symdev_tmp, VOL_SYMDEV_LEN, &len);

			/* lock data struct */
			mutex_enter(&volctl.ctl_symdev_mutex);

			/* copy string into place */
			if (err == 0) {
				volctl.ctl_symdev = (char *)kmem_alloc(len+1,
				    KM_SLEEP);
				volctl.ctl_symdev_len = len;
				(void) strcpy(volctl.ctl_symdev, symdev_tmp);
			}

			/* free temp space */
			kmem_free(symdev_tmp, VOL_SYMDEV_LEN+1);

			/* signal waiter that we have the info */
			cv_signal(&volctl.ctl_symdev_cv);
			mutex_exit(&volctl.ctl_symdev_mutex);

			break;
		}

		/*
		 * Begin: ioctls that joe random program can issue to
		 * the volctl device.
		 */

		/*
		 * Tell volume daemon to check for new media and wait
		 * for it to tell us if anything was there.
		 */
		case VOLIOCCHECK:
			/*
			 * If there's no deamon, we already know the answer.
			 */
			if (volctl.ctl_daemon_pid == 0) {
				err = ENXIO;
				break;
			}
			mutex_enter(&volctl.ctl_s_insert_mutex);
			mutex_enter(&volctl.ctl_insert_mutex);
			vol_enqueue(VIE_CHECK, (void *)&arg);
			volctl.ctl_insert_rval = -1;

			while (volctl.ctl_insert_rval == -1) {
				if (cv_wait_sig(&volctl.ctl_insert_cv,
				    &volctl.ctl_insert_mutex) == 0) {
					break;
				}
			}

			if (volctl.ctl_daemon_pid == 0) {
				/* the daemon has died */
				err = ENXIO;
			} else if (volctl.ctl_insert_rval == -1) {
				/* we've been interrupted */
				err = EINTR;
			} else {
				err = volctl.ctl_insert_rval;
			}

			mutex_exit(&volctl.ctl_insert_mutex);
			mutex_exit(&volctl.ctl_s_insert_mutex);
			break;

		/*
		 * ask the volume daemon if it is running (dev_t ==
		 * this dev_t), or if it's controlling a particular
		 * device (any dev_t).
		 */
		case VOLIOCINUSE:
			/*
			 * If there's no deamon, we already know the answer.
			 */
			if (volctl.ctl_daemon_pid == 0) {
				err = ENXIO;
				break;
			}

			mutex_enter(&volctl.ctl_s_inuse_mutex);
			mutex_enter(&volctl.ctl_inuse_mutex);
			vol_enqueue(VIE_INUSE, (void *)&arg);
			volctl.ctl_inuse_rval = -1;

			while (volctl.ctl_inuse_rval == -1) {
				if (cv_wait_sig(&volctl.ctl_inuse_cv,
				    &volctl.ctl_inuse_mutex) == 0) {
					break;	/* interuppted */
				}
			}

			if (volctl.ctl_daemon_pid == 0) {
				/* the daemon has died */
				err = ENXIO;
			} else if (volctl.ctl_inuse_rval == -1) {
				/* we've been interrupted */
				err = EINTR;
			} else {
				err = volctl.ctl_inuse_rval;
			}

			mutex_exit(&volctl.ctl_inuse_mutex);
			mutex_exit(&volctl.ctl_s_inuse_mutex);
			break;

		/* Cancel initiated from the daemon */
		case VOLIOCCANCEL: {
			minor_t	unit;

			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}
			err = ddi_copyin((caddr_t)arg, (caddr_t)&unit,
			    sizeof (minor_t), mode);
			if (err) {
				DPRINTF("vol: cancel:copyin broke %d\n", err);
				break;
			}
			tp = vol_gettab(unit, VGT_NOWAIT, &err);
			if (tp == NULL) {
				return (err);
			}

			mutex_enter(&volctl.ctl_muxmutex);
			DPRINTF("vol: doing cancel on %d\n", unit);
			tp->vol_cancel = TRUE;
			cv_broadcast(&tp->vol_incv);
			mutex_exit(&volctl.ctl_muxmutex);
			rw_exit(&tp->vol_rwlock);
			break;
		}

		/* set the volmgt root dir (defaults to "/vol") */
		case VOLIOCDROOT: {
			char		*vold_root_tmp;

			/* only daemon can do this */
			drv_getparm(PPID, (u_long *)&pid);
			if (pid != volctl.ctl_daemon_pid) {
				err = EPERM;
				break;
			}

			/* can't set if already set */
			if (vold_root != NULL) {
				/* error */
				err = EAGAIN;
				break;
			}

			/*
			 * XXX: this is ugly, but, in order to save space ...
			 * we allocate the maximum room, copy in the string,
			 * find it's real length, then allocate just that
			 * much room (freeing the max string buf)
			 */
			vold_root_tmp = (char *)kmem_alloc(MAXPATHLEN+1,
			    KM_SLEEP);

			/* copy in the vol root pathname */
			if ((err = copyinstr((caddr_t)arg,
			    (caddr_t)vold_root_tmp, MAXPATHLEN, &len)) == 0) {

				/* allocate how much room we really need */
				vold_root = (char *)kmem_alloc(len+1,
				    KM_SLEEP);

				/* copy string & release big string area */
				(void) strcpy(vold_root, vold_root_tmp);
			}

			/* free max room allocated earlier */
			kmem_free(vold_root_tmp, MAXPATHLEN+1);
			break;
		}

		/* return where the vol root is */
		case VOLIOCROOT:
			/* if no root set then punt */
			if (vold_root == NULL) {
				/* allocate a default vol root */
				vold_root = kmem_alloc(VOL_ROOT_DEFAULT_LEN+1,
				    KM_SLEEP);
				vold_root_len = VOL_ROOT_DEFAULT_LEN;
				(void) strcpy(vold_root, VOL_ROOT_DEFAULT);
			}

			err = copyoutstr((caddr_t)vold_root, (caddr_t)arg,
			    MAXPATHLEN, &len);
			break;

		/* find a symname given a dev */
		case VOLIOCSYMNAME: {
			struct vioc_symname	sn;

			/* if there's no deamon then we can't check */
			if (volctl.ctl_daemon_pid == 0) {
				err = ENXIO;
				break;
			}

			/* get the struct */
			ddi_copyin((caddr_t)arg, (caddr_t)&sn,
			    sizeof (struct vioc_symname), mode);

			/* lock out others */
			mutex_enter(&volctl.ctl_s_symname_mutex);
			mutex_enter(&volctl.ctl_symname_mutex);

			/* tell the daemon that we want a symname */
			vol_enqueue(VIE_SYMNAME, (void *)&sn.sn_dev);

			/* wait for daemon to reply */
			if (cv_wait_sig(&volctl.ctl_symname_cv,
			    &volctl.ctl_symname_mutex) == 0) {
				err = EINTR;
			}

			/* return result (if not interrupted) */
			if (err == 0) {

				/* is there enough room for the result ? */
				if (volctl.ctl_symname_len > sn.sn_pathlen) {
					DPRINTF(
				"volctl: no room for symname result\n");
					err = EINVAL;
				} else {
					err = copyoutstr(
					    (caddr_t)volctl.ctl_symname,
					    (caddr_t)sn.sn_symname,
					    min(VOL_SYMNAME_LEN,
						sn.sn_pathlen),
					    &len);
				}
			}

			/* free room */
			if (volctl.ctl_symname != NULL) {
				kmem_free(volctl.ctl_symname,
				    volctl.ctl_symname_len+1);
				volctl.ctl_symname = NULL;
				volctl.ctl_symname_len = 0;
			}

			/* release lock */
			mutex_exit(&volctl.ctl_symname_mutex);
			mutex_exit(&volctl.ctl_s_symname_mutex);

			break;
		}

		/* find a dev path given a symname */
		case VOLIOCSYMDEV: {
			struct ve_symdev	vesd;
			struct vioc_symdev	sd;


			/* if there's no deamon then we can't check */
			if (volctl.ctl_daemon_pid == 0) {
				err = ENXIO;
				break;
			}

			/* get the struct */
			ddi_copyin((caddr_t)arg, (caddr_t)&sd,
			    sizeof (struct vioc_symdev), mode);

			/* get the symname */
			err = copyinstr((caddr_t)sd.sd_symname,
			    vesd.vied_symname, VOL_SYMNAME_LEN, &len);

			/* lock out others */
			mutex_enter(&volctl.ctl_s_symdev_mutex);
			mutex_enter(&volctl.ctl_symdev_mutex);

			/* tell the daemon that we want a symdev */
			vol_enqueue(VIE_SYMDEV, (void *)&vesd);

			/* wait for daemon to reply */
			if (cv_wait_sig(&volctl.ctl_symdev_cv,
			    &volctl.ctl_symdev_mutex) == 0) {
				err = EINTR;
			}

			/* return result (if not interrupted) */
			if (err == 0) {

				/* is there enough room for the result ? */
				if (volctl.ctl_symdev_len > sd.sd_pathlen) {
					DPRINTF(
				"volctl: no room for symdev result\n");
					err = EINVAL;
				} else {
					err = copyoutstr(
					    (caddr_t)volctl.ctl_symdev,
					    (caddr_t)sd.sd_symdevname,
					    min(VOL_SYMDEV_LEN, sd.sd_pathlen),
					    &len);
				}
			}

			/* free room */
			if (volctl.ctl_symdev != NULL) {
				kmem_free(volctl.ctl_symdev,
				    volctl.ctl_symdev_len+1);
				volctl.ctl_symdev = NULL;
				volctl.ctl_symdev_len = 0;
			}

			/* release lock */
			mutex_exit(&volctl.ctl_symdev_mutex);
			mutex_exit(&volctl.ctl_s_symdev_mutex);

			break;
		}

		default:
			err = ENOTTY;
			break;
		}

		if ((err != 0) && (err != EWOULDBLOCK)) {
			DPRINTF("vol: ioctl: err=%d (cmd=%x)\n", err, cmd);
		}

		return (err);
		/*NOTREACHED*/
	}

	/*
	 * This set of ioctls are available to be executed without
	 * having the unit available.
	 */
	if ((tp = vol_gettab(unit, VGT_NDELAY|VGT_NOWAIT, &err)) == NULL) {
		return (err);
	}

	switch (cmd) {

	case VOLIOCINFO: {
		/*
		 * Gather information about the unit.  This is specific to
		 * volume management.
		 */
		struct vioc_info	info;
		err = 0;

		err = ddi_copyin((caddr_t)arg, (caddr_t)&info,
		    sizeof (struct vioc_info), mode);
		if (err) {
			rw_exit(&tp->vol_rwlock);
			return (err);
		}
		info.vii_inuse = tp->vol_bocnt+tp->vol_cocnt+tp->vol_locnt;
		info.vii_id = tp->vol_id;
		err = ddi_copyout((caddr_t)&info, (caddr_t)arg,
		    sizeof (struct vioc_info), mode);
		if ((err == 0) && (info.vii_devpath != NULL) &&
		    (info.vii_pathlen != 0) && (tp->vol_path != NULL)) {
			err = ddi_copyout((caddr_t)tp->vol_path,
			    (caddr_t)info.vii_devpath,
			    min(info.vii_pathlen, tp->vol_pathlen), mode);
		}
		rw_exit(&tp->vol_rwlock);
		return (err);
	}

	/*
	 * Cancel i/o pending (i.e. waiting in vol_gettab) on
	 * a device.  Cancel will persist until the last close.
	 */
	case VOLIOCCANCEL:
		err = 0;	/* don't want to return ENODEV */
		mutex_enter(&volctl.ctl_muxmutex);
		DPRINTF("vol: doing cancel on %d\n", unit);
		tp->vol_cancel = TRUE;
		cv_broadcast(&tp->vol_incv);
		mutex_exit(&volctl.ctl_muxmutex);
		rw_exit(&tp->vol_rwlock);

		vol_enqueue(VIE_CANCEL, (void *)&unit);
		return (err);

	case VOLIOCSATTR: {
		struct ve_attr 		vea;
		struct vioc_sattr	sa;
		size_t			len;


		if (volctl.ctl_daemon_pid == 0) {
			err = ENXIO;
			goto sattr_dun;
		}

		if ((err = ddi_copyin((caddr_t)arg, (caddr_t)&sa,
		    sizeof (struct vioc_sattr), mode)) != 0) {
			goto sattr_dun;
		}

		if ((err = copyinstr((caddr_t)sa.sa_attr,
		    (caddr_t)vea.viea_attr, MAX_ATTR_LEN, &len)) != 0) {
			goto sattr_dun;
		}

		if ((err = copyinstr((caddr_t)sa.sa_value,
		    (caddr_t)vea.viea_value, MAX_ATTR_LEN, &len)) != 0) {
			goto sattr_dun;
		}

		vea.viea_unit = unit;
		vol_enqueue(VIE_SETATTR, &vea);

		mutex_enter(&tp->vol_attr_mutex);

		tp->vol_attr_err = -1;
		while (tp->vol_attr_err == -1) {
			if (cv_wait_sig(&tp->vol_attr_cv,
			    &tp->vol_attr_mutex) == 0) {
				break;
			}
		}

		if (volctl.ctl_daemon_pid == 0) {
			/* the daemon has died */
			err = ENXIO;
		} else if (tp->vol_attr_err == -1) {
			/* we've been interrupted */
			err = EINTR;
		} else {
			err = tp->vol_attr_err;
		}

		mutex_exit(&tp->vol_attr_mutex);
sattr_dun:
		rw_exit(&tp->vol_rwlock);
		return (err);
	}

	case VOLIOCGATTR: {
		struct ve_attr		vea;
		struct vioc_gattr	ga;
		size_t			len;


		if (volctl.ctl_daemon_pid == 0) {
			err = ENXIO;
			goto gattr_dun;
		}

		if ((err = ddi_copyin((caddr_t)arg, (caddr_t)&ga,
		    sizeof (struct vioc_gattr), mode)) != 0) {
			goto gattr_dun;
		}

		if ((err = copyinstr((caddr_t)ga.ga_attr,
		    (caddr_t)vea.viea_attr, MAX_ATTR_LEN, &len)) != 0) {
			goto gattr_dun;
		}

		vea.viea_unit = unit;
		vol_enqueue(VIE_GETATTR, &vea);

		mutex_enter(&tp->vol_attr_mutex);

		tp->vol_attr_err = -1;
		tp->vol_attr_ptr = &vea;
		while (tp->vol_attr_err == -1) {
			if (cv_wait_sig(&tp->vol_attr_cv,
			    &tp->vol_attr_mutex) == 0) {
				break;
			}
		}

		if (volctl.ctl_daemon_pid == 0) {
			/* the daemon has died */
			err = ENXIO;
		} else if (tp->vol_attr_err == -1) {
			/* we've been interrupted */
			err = EINTR;
		} else {
			err = tp->vol_attr_err;
		}

		mutex_exit(&tp->vol_attr_mutex);

		if (err == 0) {
			err = copyoutstr((caddr_t)vea.viea_value, ga.ga_value,
			    min(ga.ga_val_len, MAX_ATTR_LEN), &len);
		}
gattr_dun:
		rw_exit(&tp->vol_rwlock);
		return (err);
	}

	case CDROMEJECT:
	case FDEJECT:
	case DKIOCEJECT:
		if (tp->vol_devops == NULL) {
			rw_exit(&tp->vol_rwlock);
			return (EAGAIN);
		}
		rw_exit(&tp->vol_rwlock);
		break;

	default:
		rw_exit(&tp->vol_rwlock);
		break;

	}

	/*
	 * This is the part that passes ioctls on to the lower
	 * level devices.  Some of these may have to be trapped
	 * and remapped.
	 */
	if ((tp = vol_gettab(unit, VGT_WAITSIG, &err)) == NULL) {
		DPRINTF("vol: ioctl (to pass on): gettab on unit %d, err %d\n",
		    unit, err);
		return (err);
	}

	/*
	 * this is almost certainly the ENXIO case for that special
	 * flag we set.
	 */
	if (err) {
		goto out;
	}

	if (!(tp->vol_flags & ST_OPEN)) {
		err = ENXIO;
		goto out;
	}

	switch (cmd) {
	/*
	 * Here's where we watch for the eject ioctls.  Here, we enqueue
	 * a message for the daemon and wait around to hear the results.
	 */
	case CDROMEJECT:
	case FDEJECT:
	case DKIOCEJECT: {
		dev_t		savedev = tp->vol_dev;
		struct ve_eject	vej;


		rw_exit(&tp->vol_rwlock);

		if (volctl.ctl_daemon_pid == 0) {
			return (ENXIO);
		}

		vej.viej_unit = unit;
		vej.viej_force = 0;
		vol_enqueue(VIE_EJECT, (void *)&vej);

		mutex_enter(&tp->vol_ejmutex);

		tp->vol_eject_status = VEJ_NONE;

		while (tp->vol_eject_status == VEJ_NONE) {
			if (cv_wait_sig(&tp->vol_ejcv,
			    &tp->vol_ejmutex) == 0) {
				break;
			}
		}

		if (volctl.ctl_daemon_pid == 0) {
			/* the daemon has died */
			err = ENXIO;
		} else if (tp->vol_eject_status == VEJ_YES) {
			err = 0;
		} else if (tp->vol_eject_status == VEJ_NO) {
			err = EBUSY;
		} else {
			/* we've been interrupted */
			err = EINTR;
		}

		mutex_exit(&tp->vol_ejmutex);

		if (err == 0) {
			ASSERT(savedev != NODEV);
			err = cdev_ioctl(savedev,  cmd, arg,
			    FREAD, kcred, rvalp);
			/*
			 * clean out the block device.
			 */
			binval(savedev);	/* XXX: not DDI compliant */
		}

		/* we don't own the rw lock now */
		return (err);
	}

	/*
	 * The following ioctls cause volume management to
	 * reread the label after last close.  The assumption is
	 * that these are only used during "format" operations
	 * and labels and stuff get written with these.
	 */
	case DKIOCSVTOC:	/* set vtoc */
	case DKIOCSGEOM:	/* set geometry */
	case DKIOCSAPART:	/* set partitions */
	case FDRAW:		/* "raw" command to floppy */
		vol_enqueue(VIE_NEWLABEL, (void *)&unit);
		/* FALL THROUGH */
	default:
		/*
		 * Pass the ioctl on down.
		 */
		if (tp->vol_dev == NODEV) {
			err = EIO;

			DPRINTF("vol: tp->vol_dev = NODEV\n");
			DPRINTF(
	    "vol: ioctl: dev %d.%d cmd %x arg %x mode %x credp %x rvalp %x\n",
			    (int)getmajor(dev), (int)getminor(dev),
			    cmd, arg, mode, (int)credp, (int)rvalp);
			break;
		}
		err = cdev_ioctl(tp->vol_dev, cmd, arg,
		    mode & ~FWRITE, kcred, rvalp);
		break;
	}
	/* release lock, return err */
out:
	rw_exit(&tp->vol_rwlock);
	return (err);
}


volpoll(dev_t dev, short events, int anyyet, short *reventsp,
	struct pollhead **phpp)
{
	int		unit;
	struct vol_tab 	*tp;
	int		err = 0;

	DPRINTF4(
	    "vol: poll: dev %d.%d events 0x%x anyyet 0x%x reventsp 0x%x\n",
	    (int)getmajor(dev), (int)getminor(dev), (int)events, anyyet,
	    (int)*reventsp);

	unit = getminor(dev);
	if (unit == 0) {
		if (events & POLLRDNORM) {
			DPRINTF4("vol: poll: got a POLLIN\n");
			mutex_enter(&volctl.ctl_evmutex);
			if (volctl.ctl_evcnt) {
				DPRINTF3("vol: poll: we have data\n");
				*reventsp |= POLLIN;
				mutex_exit(&volctl.ctl_evmutex);
				return (0);
			}
			mutex_exit(&volctl.ctl_evmutex);
		}
		if (!anyyet) {
			*phpp = &vol_pollhead;
			*reventsp = 0;
		}
		return (0);
	}

	if ((tp = vol_gettab(unit, VGT_WAITSIG, &err)) == NULL) {
		return (err);
	}

	if (! (tp->vol_flags & ST_OPEN)) {
		err = ENXIO;
		goto out;
	}
	ASSERT(tp->vol_dev != NODEV);
	err = cdev_poll(tp->vol_dev, events, anyyet, reventsp, phpp);

out:
	rw_exit(&tp->vol_rwlock);
	return (err);
}


static void
vol_enqueue(enum vie_event type, void *data)
{
	struct kvioc_event 	*kvie;
	cred_t			*c;
	proc_t			*p;
	uid_t			uid;
	gid_t			gid;
	dev_t			ctty;


	kvie = kmem_alloc(sizeof (struct kvioc_event), KM_SLEEP);
	kvie->kve_event.vie_type = type;

	/* build our user friendly slop.  Probably not DDI compliant */
	if (drv_getparm(UCRED, (u_long *)&c) != 0) {
		DPRINTF("vol: vol_enqueue: couldn't get ucred\n");
		uid = -1;
		gid = -1;
	} else {
		uid = c->cr_uid;
		gid = c->cr_gid;
	}

	if (drv_getparm(UPROCP, (u_long *)&p) != 0) {
		DPRINTF("vol: vol_enqueue: couldn't get uprocp\n");
		ctty = NODEV;
	} else {
		ctty = cttydev(p);
	}

	DPRINTF2("vol: vol_enqueue: uid=%d, gid=%d, ctty=0x%x\n", (int)uid,
	    (int)gid, (int)ctty);

	switch (type) {
	case VIE_MISSING:
		kvie->kve_event.vie_missing = *(struct ve_missing *)data;
		kvie->kve_event.vie_missing.viem_user = uid;
		kvie->kve_event.vie_missing.viem_tty = ctty;
		break;
	case VIE_INSERT:
		kvie->kve_event.vie_insert.viei_dev = *(dev_t *)data;
		break;
	case VIE_CHECK:
		kvie->kve_event.vie_check.viec_dev = *(dev_t *)data;
		break;
	case VIE_INUSE:
		kvie->kve_event.vie_inuse.vieu_dev = *(dev_t *)data;
		break;
	case VIE_EJECT:
		kvie->kve_event.vie_eject = *(struct ve_eject *)data;
		kvie->kve_event.vie_eject.viej_user = uid;
		kvie->kve_event.vie_eject.viej_tty = ctty;
		break;
	case VIE_DEVERR:
		kvie->kve_event.vie_error = *(struct ve_error *)data;
		break;
	case VIE_CLOSE:
		kvie->kve_event.vie_close.viecl_unit = *(minor_t *)data;
		break;
	case VIE_REMOVED:
		kvie->kve_event.vie_rm.virm_unit = *(minor_t *)data;
		break;
	case VIE_CANCEL:
		kvie->kve_event.vie_cancel.viec_unit = *(minor_t *)data;
		break;
	case VIE_NEWLABEL:
		kvie->kve_event.vie_newlabel.vien_unit = *(minor_t *)data;
		break;
	case VIE_GETATTR:
	case VIE_SETATTR:
		kvie->kve_event.vie_attr = *(struct ve_attr *)data;
		kvie->kve_event.vie_attr.viea_uid = uid;
		kvie->kve_event.vie_attr.viea_gid = gid;
		break;
	case VIE_SYMNAME:
		kvie->kve_event.vie_symname.vies_dev = *(dev_t *)data;
		break;
	case VIE_SYMDEV:
		kvie->kve_event.vie_symdev = *(struct ve_symdev *)data;
		break;
	default:
		cmn_err(CE_WARN, "vol_enqueue: bad type %d\n", type);
		kmem_free(kvie, sizeof (struct kvioc_event));
		return;
	}

	mutex_enter(&volctl.ctl_evmutex);
	if (volctl.ctl_evend) {
		insque(kvie, volctl.ctl_evend);
	} else {
		insque(kvie, &volctl.ctl_events);
	}
	volctl.ctl_evend = kvie;
	volctl.ctl_evcnt++;
	mutex_exit(&volctl.ctl_evmutex);
	pollwakeup(&vol_pollhead, POLLRDNORM);
}


/*
 * vol_gettab() returns a vol_tab_t * for the unit.  If the unit
 * isn't mapped, we tell vold about it and wait around until
 * a mapping occurs
 *
 * upon successful completion, the vol_tab for which the pointer is
 * returned has the read/write lock (vol_rwlock) held as a reader
 */
static struct vol_tab *
vol_gettab(int unit, u_int flags, int *err)
{
	struct vol_tab		*tp;
	int			rv;
	struct ve_missing	ve;


	*err = 0;

	if (volctl.ctl_open == 0) {
		*err = ENXIO;
		return (NULL);
	}

	ASSERT(unit >= 0);
	if (unit < 0) {
		*err = ENOTTY;
		DPRINTF("vol: vol_gettab: negative unit number!\n");
		return (NULL);
	}

	mutex_enter(&volctl.ctl_muxmutex);
	tp = (struct vol_tab *)ddi_get_soft_state(voltab, unit);
	if (tp == NULL) {
		/* this unit not yet created */
		if (flags & VGT_NEW) {
			/* the "create" flag is set, so create it */
			*err = ddi_soft_state_zalloc(voltab, unit);
			if (*err) {
				DPRINTF("vol: zalloc broke vol %d\n", unit);
				*err = ENODEV;
				goto out;
			}
			tp = (struct vol_tab *)
				ddi_get_soft_state(voltab, unit);
			if (tp == NULL) {
				DPRINTF("vol: soft state was null!\n");
				*err = ENOTTY;
				goto out;
			}

			tp->vol_unit = unit;
		} else {
			/* didn't build a new one and we don't have one */
			DPRINTF("vol: vol_gettab: ENODEV\n");
			*err = ENODEV;
			goto out;
		}
		rw_init(&tp->vol_rwlock, VOLRWLOCK, RW_DEFAULT, NULL);
	}

	/* now we know the unit exists */

	/* keep track of the largest unit number we've seen */
	if (unit > volctl.ctl_maxunit) {
		volctl.ctl_maxunit = unit;
	}

	/* check for ops already gotten, or for /dev/volctl */
	if ((tp->vol_devops != NULL) || (unit == 0)) {
		/* got it! */
		goto out;
	}

	/* no vol_devops yet (and unit not /dev/volctl) */

	if (flags & VGT_NOWAIT) {
		/* no ops, and they don't want to wait */
		DPRINTF("vol: vol_gettab: no mapping for %d, no waiting\n",
		    unit);
		*err = ENODEV;
		goto out;
	}

	/* no vol_devops yet, but caller is willing to wait */

#ifdef	DEBUG_ENXIO
	if (tp->vol_flags & ST_ENXIO) {
		(void) printf(
		    "vol_gettab: no mapping for %d: doing ENXIO\n", unit);
	} else {
		(void) printf(
"vol_gettab: no mapping for %d: it's MISSING (flags=0x%x, tp=0x%x)\n",
		    unit, tp->vol_flags, (char *)tp);
	}
#endif
	if (tp->vol_flags & ST_ENXIO) {
		/*
		 * It's been unmapped, but the requested behavior is to
		 * return ENXIO rather than waiting around.  The enxio
		 * behavior is cleared on close.
		 */
		DPRINTF("vol: vol_gettab: no mapping for %d, doing ENXIO\n",
		    unit);
		*err = ENXIO;
		goto out;
	}

	/*
	 * there isn't a mapping -- enqueue a missing message to the
	 * daemon and wait around until it appears
	 */
	ve.viem_unit = unit;
	ve.viem_ndelay = (flags & VGT_NDELAY) ? TRUE : FALSE;
	DPRINTF(
	    "vol: vol_gettab: enqueueing missing event, unit %d (ndelay=%d)\n",
	    unit, ve.viem_ndelay);
	vol_enqueue(VIE_MISSING, (void *)&ve);

	/*
	 * hang around until a unit appears or we're cancelled.
	 */
	while (tp->vol_devops == NULL) {
		if (tp->vol_cancel) {
			break;			/* a volcancel has been done */
		}

		/* wait right here */
		if (flags & VGT_WAITSIG) {
			rv = cv_wait_sig(&tp->vol_incv, &volctl.ctl_muxmutex);
			if (rv == 0) {
				DPRINTF("vol: vol_gettab: eintr\n");
				tp->vol_cancel = TRUE;
				vol_enqueue(VIE_CANCEL, (void *)&unit);
			}
		} else {
			/* can't be interrupted by a signal */
			cv_wait(&tp->vol_incv, &volctl.ctl_muxmutex);
		}
		DPRINTF2("vol: vol_gettab: insert cv wakeup\n");

		if ((flags & VGT_NDELAY) && (tp->vol_dev == NODEV)) {
			break;
		}
	}
out:
	/*
	 * If the device is "cancelled", don't return the tp unless
	 * the caller really wants it (nowait and ndelay).
	 */
	if ((tp != NULL) && tp->vol_cancel && !(flags & VGT_NOWAIT) &&
	    !(flags & VGT_NDELAY)) {
		DPRINTF("vol: vol_gettab: cancel (flags 0x%x)\n", flags);
		*err = EIO;
		tp = NULL;
	}

	if (tp != NULL) {
		rw_enter(&tp->vol_rwlock, RW_READER);
	}

retn:
	if (*err != 0) {
		DPRINTF("vol: vol_gettab: err=%d unit=%d, tp=0x%x\n",
		    *err, unit, (char *)tp);
	}
	mutex_exit(&volctl.ctl_muxmutex);
	return (tp);
}


/*
 * Unmap *tp.  Removes it from the ddi_soft_state list.
 */
static void
vol_unmap(struct vol_tab *tp)
{

	/* wait until everyone is done with it */
	rw_exit(&tp->vol_rwlock);	/* release the reader lock */
	rw_enter(&tp->vol_rwlock, RW_WRITER);
	if (tp->vol_path) {
		kmem_free(tp->vol_path, tp->vol_pathlen);
	}
	DPRINTF2("vol: 0x%x writelock\n", (int)&tp->vol_rwlock);
	rw_exit(&tp->vol_rwlock);

	mutex_enter(&volctl.ctl_muxmutex);	/* lock with vol_gettab */

	/* get rid of the thing */
	ddi_soft_state_free(voltab, tp->vol_unit);

	mutex_exit(&volctl.ctl_muxmutex);	/* let vol_gettab back in */
}


/*
 * This is called when the volume daemon closes its connection.
 * It cleans out our mux.
 */
static void
vol_cleanup(void)
{
	int		i;
	int		err;
	struct vol_tab	*tp;



	volctl.ctl_stoppoll = 1;	/* to be DEPRECIATED */

	for (i = 0; i < volctl.ctl_maxunit+1; i++) {
		tp = vol_gettab(i, VGT_NOWAIT, &err);
		if (tp == NULL) {
			continue;
		}

		cv_broadcast(&tp->vol_attr_cv);

		tp->vol_eject_status == VEJ_NO;
		cv_broadcast(&tp->vol_ejcv);

		cv_broadcast(&tp->vol_incv);

		vol_unmap(tp);
	}

	DPRINTF("vol: vol_cleanup: cleared from 0 to %d\n",
	    volctl.ctl_maxunit);

	volctl.ctl_maxunit = 0;

	/*
	 * handle threads waiting for replies from the daemon
	 *
	 * NOTE: there's a window here, since we signal the (possible)
	 * waiting thread.  When it wakes up it checks to see if the
	 * daemon pid is 0, and if so it assumes that we woke them up, and
	 * there's no more daemon.  But what if we do signal them, but
	 * another deamon starts up quickly, so the daemon_pid gets fill
	 * back in -- they'd think that somebody else woke them up!
	 */
	cv_broadcast(&volctl.ctl_inuse_cv);
	volctl.ctl_insert_rval == 0;
	cv_broadcast(&volctl.ctl_insert_cv);
	cv_broadcast(&volctl.ctl_symname_cv);
	cv_broadcast(&volctl.ctl_symdev_cv);

	/*
	 * release memory only needed while the daemon is runnning
	 */
	if (vold_root != NULL) {
		kmem_free(vold_root, vold_root_len+1);
		vold_root = NULL;
		vold_root_len = 0;
	}
}


void
vol_daemon(void)
{
	struct buf	*bp, *bp_next, *bp_prev;
	struct vol_tab	*tp;
	int		unit;
	int		err;


	DPRINTF("vol: daemon running\n");

	/*CONSTANTCONDITION*/
	while (1) {

		/* wait until we're poked */
		mutex_enter(&volctl.ctl_daemon_mx);
		cv_wait(&volctl.ctl_daemon_cv, &volctl.ctl_daemon_mx);
		mutex_exit(&volctl.ctl_daemon_mx);

		DPRINTF("vol: daemon has been woken\n");

		/* check for someone trying to kill us */
		if (volctl.ctl_daemon_die_flag) {
			break;
		}

		/*
		 * for each buffer on the chain,
		 *	get the tp and see if there's a mapping for it
		 *	if so, call volstrategy with it & clear ST_IOPEND
		 */

		bp_prev = NULL;
		bp_next = NULL;
		mutex_enter(&volctl.ctl_daemon_bh_mx);
		for (bp = volctl.ctl_daemon_bhead; bp; bp = bp_next) {
			bp_next = bp->b_forw;
			unit = getminor(bp->b_chain->b_edev);
			tp = vol_gettab(unit, VGT_NOWAIT, &err);
			/*
			 * if vol_gettab croaks, or there's no mapping,
			 * or there's no i/o pending ... keep looking
			 */
			if ((tp == NULL) || (tp->vol_devops == NULL)) {
				if (tp != NULL) {
					rw_exit(&tp->vol_rwlock);
				}
				bp_prev = bp;
				continue;
			}
			tp->vol_flags &= ~ST_IOPEND;
			rw_exit(&tp->vol_rwlock);

			/* remove the bp from the queue. */
			if (bp_prev) {
				bp_prev->b_forw = bp->b_forw;
			} else {
				volctl.ctl_daemon_bhead = bp->b_forw;
			}
			bp->b_forw = bp;
			(void) volstrategy(bp->b_chain);

			/* free buffer */
			mutex_enter(&volctl.ctl_bmutex);
			bp->b_chain = volctl.ctl_bhead;
			volctl.ctl_bhead = bp;
			mutex_exit(&volctl.ctl_bmutex);

			/* release semaphore */
			sema_v(&volctl.ctl_bsema);

		}
		mutex_exit(&volctl.ctl_daemon_bh_mx);


	}

	/* someone wants us dead! Let's oblige them */
	DPRINTF("vol: daemon: commiting suicide\n");

	/* signal our finish */
	mutex_enter(&volctl.ctl_daemon_death_mx);
	cv_signal(&volctl.ctl_daemon_death_cv);
	mutex_exit(&volctl.ctl_daemon_death_mx);

	/* commit suicide */
	thread_exit();
}


/*
 * start the vol daemon, which takes care of resubmitting read or write
 *	requests that have failed (e.g the user has popped out the floppy
 *	in the middle of read from/writing to it).
 */
static int
vol_start_daemon(void)
{
	DPRINTF("vol: start_daemon\n");

	/* creat the synchronization stuff needed for the thread */
	cv_init(&volctl.ctl_daemon_cv, "vol daemon cv", CV_DRIVER, NULL);
	mutex_init(&volctl.ctl_daemon_mx, "vol daemon mx",
	    MUTEX_DRIVER, NULL);
	cv_init(&volctl.ctl_daemon_death_cv, "vol daemon death cv",
	    CV_DRIVER, NULL);
	mutex_init(&volctl.ctl_daemon_death_mx, "vol daemon death mx",
	    MUTEX_DRIVER, NULL);
	mutex_init(&volctl.ctl_daemon_bh_mx, "vol daemon buf mx",
	    MUTEX_DRIVER, NULL);

	/* initialize the cleanup daemon buf list head (to empty) */
	volctl.ctl_daemon_bhead = NULL;

	/* initialize the "die" flag to "don't die" */
	volctl.ctl_daemon_die_flag = 0;

	/*
	 * Create the thread.  For the time being we use the default
	 * stack allocation mechanism.  As a system thread, we "belong to"
	 * process 0.
	 */
	if (thread_create(NULL, 0, vol_daemon,
	    0, 0, &p0, TS_RUN, 60) == NULL) {
		DPRINTF("vol: start_daemon: failed\n");
		return (1);	/* error starting thread */
	}

	/* thread started ok */
	return (0);
}


/*
 * Check the floppy drive to see if there's a floppy still in the
 * drive.  If there isn't this function will block until the floppy
 * is either back in the drive or the i/o is cancelled.  If found_media
 * is supplied the status will be returned through it.
 */
static int
vol_checkmedia(struct vol_tab *tp, int *found_media)
{
	int 		err = 0;
	int		badnews = 0;
	struct vol_tab	*tp0;

	DPRINTF2("vol: checkmedia\n");

	/* do the grotty stuff to get the answer */
	badnews = vol_checkmedia_machdep(tp);

	/* check to see if there's no media in the drive */
	if (badnews) {
		/* there's no media in the drive */

		if (found_media) {
			*found_media = FALSE;	/* return result */
		}

		/* unmap the device */
		rw_exit(&tp->vol_rwlock);
		rw_enter(&tp->vol_rwlock, RW_WRITER);
		tp->vol_devops = NULL;
		tp->vol_dev = NODEV;
		rw_exit(&tp->vol_rwlock);

		vol_enqueue(VIE_REMOVED, (void *)&tp->vol_unit);

		/* get the mapping for this device, waiting if needed */
		DPRINTF("vol: checkmedia: calling gettab\n");
		tp0 = vol_gettab(tp->vol_unit, VGT_WAITSIG, &err);

		/*
		 * if vol_gettab returns NULL, it means that we
		 * didn't get an rw_enter from vol_gettab, so
		 * we just have to do it here to make sure the
		 * enters and exits match up.
		 */
		if (tp0 == NULL) {
			rw_enter(&tp->vol_rwlock, RW_READER);
		}

		DPRINTF("vol: checkmedia: gettab has returned\n");

	} else {

		/* there is media in the drive */

		if (found_media) {
			*found_media = TRUE;	/* return results */
		}

	}

	/* all done */
	return (err);
}


/*
 * return the bad news: media there (0) or not (1).
 */
static int
vol_checkmedia_machdep(struct vol_tab *tp)
{
	int	err;
	int	fl_rval = 0;	/* bitmap word: all bits clear initially */


	switch (tp->vol_mtype) {
	case MT_FLOPPY:
		/* check for a floppy disk in the drive */

		/* ensure we have a dev to do the ioctl on */
		ASSERT(tp->vol_dev != NODEV);
		if (tp->vol_dev == NODEV) {
			/* it's been unmapped (so probably not there) */
			DPRINTF("vol: checkmedia: volume unmapped\n");
			return (1);
		}

		/*
		 * XXX this mutex make sure that we're only doing one of
		 * XXX these ioctl's at a time.  this avoids a deadlock
		 * XXX in the floppy driver.
		 */
		mutex_enter(&floppy_chk_mutex);
		err = cdev_ioctl(tp->vol_dev, FDGETCHANGE,
		    (int)&fl_rval, FKIOCTL, kcred, NULL);
		mutex_exit(&floppy_chk_mutex);

		if (err != 0) {
			DPRINTF("vol: checkmedia: FDGETCHANGE failed %d\n",
			    err);
			/* if we got an error, assume the worst */
			return (1);
		}

		/* is media present ?? */
		if (fl_rval & FDGC_CURRENT) {
			DPRINTF(
			    "vol: checkmedia: no media! (fl_rval = 0x%x)\n",
			    fl_rval);
			return (1);	/* no media in the drive */
		}

		return (0);		/* media in the drive */

	default:
		DPRINTF("vol: checkmedia: bad mtype %d\n", tp->vol_mtype);
		return (1);
	}
	/*NOTREACHED*/
}
