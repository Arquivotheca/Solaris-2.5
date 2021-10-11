#ifndef lint
static char sccsid[] = "@(#)cgtwo.c 1.40 89/09/19 SMI";
#endif
#ident	"@(#)cgtwo.c	1.47	94/06/27 SMI"	/* SunOS-4.1.2 1.40 */

/*
 * Copyright 1986, 1987 by Sun Microsystems, Inc.
 */

/*
 * Sun-2/Sun-3 color board driver
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/map.h>
#include <sys/vmmac.h>
#include <sys/time.h>
#include <sys/fbio.h>

#include <sys/pixrect.h>
#include <sys/pr_impl_util.h>
#include <sys/memreg.h>
#include <sys/cg2reg.h>
#include <sys/cg2var.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/cmn_err.h>

#include <sys/open.h>
#include <sys/file.h>
#include <sys/ddi_impldefs.h>
#include <sys/bustypes.h>

/* total size of board */
#define	CG2_TOTAL_SIZE	(CG2_MAPPED_OFFSET + CG2_MAPPED_SIZE)

/* configuration options */

#ifndef NWIN
#define NWIN 1
#endif		/* NWIN */

#define NOHWINIT 1

#ifndef NOHWINIT
#define NOHWINIT 0
#endif		/* NOHWINIT */

#if NOHWINIT
int cg2_hwinit = 0;
#endif		/* NOHWINIT */

#define CG2DEBUG 7

#ifndef CG2DEBUG
#define CG2DEBUG 0
#endif		/* CG2DEBUG */

#if CG2DEBUG
int cg2_debug = 0; /* CG2DEBUG; */
#define DEBUGF(level, args)    _STMT(if (cg2_debug >= (level)) cmn_err args;)
#define DEBUG   1
#else		/* CG2DEBUG */
#define DEBUGF(level, args)
#endif		/* CG2DEBUG */

#include <sys/debug.h>          /* for ASSERT and STATIC */
#include <sys/debug/debug.h>    /* for CALL_DEBUG() */
 
static int cg2_open(dev_t *, int, int, cred_t *);
static int cg2_close(dev_t, int, int, cred_t *);
static int cg2_ioctl(dev_t, int, int, int, cred_t *, int *);
static int cg2_mmap(dev_t, off_t, int);

static u_int cg2_intr(caddr_t);

static int cg2_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int cg2_identify(dev_info_t *);
static int cg2_attach(dev_info_t *, ddi_attach_cmd_t);
static int cg2_detach(dev_info_t *, ddi_detach_cmd_t);
static int cg2_probe(dev_info_t *);
static int cg2_reset(dev_info_t *, ddi_reset_cmd_t);
 
struct cb_ops	cg2_cb_ops = {
	cg2_open,
	cg2_close,
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	cg2_ioctl,
	nodev,			/* devmap */
	cg2_mmap,
	nodev,
	nochpoll,		/* poll */
	ddi_prop_op,
	0,			/* streamtab  */
	D_NEW|D_MP		/* Driver comaptibility flag */
};      
 
struct dev_ops cgtwo_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	cg2_info,
	cg2_identify,
	cg2_probe,		/* probe */
	cg2_attach,
	cg2_detach,		/* detach */
	nodev,			/* reset  */
	&cg2_cb_ops,		/* driver operations */
	(struct bus_ops *)0	/* bus operations */
 
};

/* driver per-unit data */
struct cg2_softc {
	kmutex_t	    softc_lock;	    /* to protect updates to softc */
	int		    flags;	    /* bits defined in cg2var.h */
					    /* (struct cg2pr, flags member) */
	struct cg2fb	   *fb;		    /* virtual address */
	int		    w, h;	    /* resolution */
#if NWIN > 0
	Pixrect		    pr;		    /* kernel pixrect & private data */
	struct cg2pr	    prd;
	kmutex_t	    pixrect_mutex;  /* mutex for pixrect operations */
#endif /* NWIN > 0 */
	ddi_iblock_cookie_t iblkc;
	kcondvar_t	    vrtsleep;
	ddi_idevice_cookie_t idevc;
	dev_info_t	   *mydip;	    /* my devinfo ptr */
	kmutex_t	    busy_mutex;	    /* mutex for .. */
	int		    busy;	    /* true if this instance is in use*/
	int		    toutid;
};

static int ncg2;		/* counts the number of bwtwos */
static void *cg2_state;		/* opaque handle where all the state hangs */

#define getsoftc(unit) \
	((struct cg2_softc *)ddi_get_soft_state(cg2_state, (unit)))

static int probeit(struct cg2fb *fb, dev_info_t *devi);

/* default structure for FBIOGATTR/FBIOGTYPE ioctls */
static struct fbgattr fbgattr_default =  {
/*	real_type         owner */
	FBTYPE_SUN2COLOR, 0,
/* fbtype: type             h  w  depth cms  size */
	{ FBTYPE_SUN2COLOR, 0, 0, 8,    256, CG2_TOTAL_SIZE },
/* fbsattr: flags         emu_type */
	{ FB_ATTR_DEVSPECIFIC, -1, 
/* dev_specific:   FLAGS,      BUFFERS, PRFLAGS */
	{ FB_ATTR_CG2_FLAGS_PRFLAGS, 1, 0 } },
/* emu_types */
	{ -1, -1, -1, -1}
};

/* double buffering enable flag */
int cg2_dblbuf_enable = 1;

#if NWIN > 0

/* SunWindows specific stuff */

/* kernel pixrect ops vector */
struct pixrectops cg2_ops = {
	cg2_rop,
	cg2_putcolormap,
	cg2_putattributes,
#	ifdef _PR_IOCTL_KERNEL_DEFINED
	0
#	endif
};

#if !defined(FBIO_KCOPY) && !defined(FKIOCTL)
/*
 * This flag indicates that we should use kcopy
 * instead of copyin/copyout for ioctls.  It is
 * set by the sunview window system to indicate
 * the ioctl is issued from the kernel.
 */
extern int fbio_kcopy;
#endif

#endif /* NWIN > 0 */

extern  int copyin(), copyout(), kcopy();

#define BZERO(d,c)      bzero((caddr_t) (d), (u_int) (c))
#define COPY(f,s,d,c)   (f((caddr_t) (s), (caddr_t) (d), (u_int) (c)))
#define BCOPY(s,d,c)    COPY(bcopy,(s),(d),(c))
#define COPYIN(s,d,c)   COPY(copyin,(s),(d),(c))
#define COPYOUT(s,d,c)  COPY(copyout,(s),(d),(c))


#include <sys/modctl.h>

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* This is a device driver */
	"cgtwo driver",
	&cgtwo_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};



int
_init(void)
{
	register int e;

	DEBUGF(3, (CE_CONT, "cg2: compiled %s, %s\n", __TIME__, __DATE__));

	e = ddi_soft_state_init(&cg2_state, sizeof (struct cg2_softc), 1);

	if (e == 0 && (e = mod_install(&modlinkage)) != 0)
	    ddi_soft_state_fini(&cg2_state);

	DEBUGF(3, (CE_CONT, "cg2: _init done, return(%d)\n", e));
	return (e);
}

int
_fini(void)
{
	register int unit, busy, e;
	register struct cg2_softc *softc;

	DEBUGF(3, (CE_CONT, "cg2: _fini\n"));

	if ((e = mod_remove(&modlinkage)) != 0)
	    return (e);

	ddi_soft_state_fini(&cg2_state);

	DEBUGF(3, (CE_CONT, "cg2: _fini done, return(%d)\n", e));

	return (DDI_SUCCESS);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


static int
cg2_identify(dev_info_t *devi)
{
	char	*name = ddi_get_name(devi);

	DEBUGF(3, (CE_CONT, "cg2_identify(%s)\n", name));

	return (strcmp(name, "cgtwo") == 0 ? DDI_IDENTIFIED : DDI_NOT_IDENTIFIED);
}

static int
cg2_probe(dev_info_t *devi)
{
	struct cg2fb *fb;
	register struct cg2_softc *softc;
	register int instance, error;


        DEBUGF(3, (CE_CONT, "cg2_probe (%s) ncg2=%d unit=%d",
	    ddi_get_name(devi), ncg2, ddi_get_instance(devi)));

	instance = ddi_get_instance(devi);

	if (instance > ncg2)
	    ncg2 = instance;

        if (ddi_soft_state_zalloc(cg2_state, instance) != 0) 
	{
	    return (DDI_PROBE_FAILURE);
	    /*NOTREACHED*/
        }

        softc = getsoftc(instance);
        softc->mydip = devi;

        ddi_set_driver_private(devi, (caddr_t)softc);

        if (ddi_dev_is_sid(devi) == DDI_SUCCESS)
            return (DDI_PROBE_DONTCARE);

	/*
	 * Config address is base address of board, so we actually
	 * have the plane mode memory (and a little bit of pixel mode)
	 * mapped at this point.  Re-map 2M higher to get the rasterop
	 * mode memory and control registers mapped.
	 */

        if( ddi_map_regs(devi, 0, (caddr_t *)&fb,
	    CG2_MAPPED_OFFSET, CG2_MAPPED_SIZE) !=0) 
	{
            DEBUGF(1, (CE_CONT, "  map_regs failed, returning failure\n"));

            return (DDI_PROBE_FAILURE) ;
        }

	if (probeit(fb, devi))
	{
	    DEBUGF(1, (CE_CONT, "  peek/poke failed, returning failure\n"));

	    ddi_unmap_regs(devi, 0, (caddr_t *)&fb,
		CG2_MAPPED_OFFSET, CG2_MAPPED_SIZE);

	    return (DDI_PROBE_FAILURE) ;
	}

	softc->fb = fb;
	softc->flags = 0;

	/* check for supported resolution */
	switch (fb->status.reg.resolution) 
	{
	    case CG2_SCR_1152X900:
		softc->w = 1152; 
		softc->h =  900;
		softc->flags = CG2D_STDRES;
		break;

	    case CG2_SCR_1024X1024:
		softc->w = 1024;
		softc->h = 1024;
		break;

	    default:

		cmn_err(CE_CONT, "  unsupported resolution (%d)\n",
		    fb->status.reg.resolution);

		ddi_unmap_regs(devi, 0, (caddr_t *)&fb,
		    CG2_MAPPED_OFFSET, CG2_MAPPED_SIZE);

		return (DDI_PROBE_FAILURE);
	}

        DEBUGF(2,(CE_CONT, "  returning success\n"));

	return (DDI_PROBE_SUCCESS);
}

static int
probeit(struct cg2fb *fb, dev_info_t *devi)
{
	char   c;
	union {
		struct cg2statusreg reg;
		short word;
	} status;

#define	allrop(fb, reg)	((short *) &(fb)->ropcontrol[CG2_ALLROP].ropregs.reg)
#define	pixel0(fb)	((char *) &fb->ropio.roppixel.pixel[0][0])

	/*
	 * Probe sequence:
	 *
	 * set board for pixel mode access
	 * enable all planes
	 * set rasterop function to CG_SRC
	 * disable end masks
	 * set fifo shift/direction to zero/left-to-right
	 * write 0xa5 to pixel at (0,0)
	 * check pixel value
	 * enable subset of planes (0xcc)
	 * set rasterop function to ~CG_DEST
	 * write to pixel at (0,0) again
	 * enable all planes again
	 * read pixel value; should be 0xa5 ^ 0xcc = 0x69
	 */

	if (ddi_peeks(devi, (short *) &fb->status.word,
	    (short *) &status.word) != DDI_SUCCESS) 
	{
	    DEBUGF(1, (CE_CONT, "peek failed\n"));
	    return 1;
	}

	DEBUGF(3, (CE_CONT, "peek returned:%x\n",status.word));

	status.reg.ropmode = SWWPIX;

	if (ddi_pokes(devi,(short *)&fb->status.word,status.word)!=DDI_SUCCESS||
	    ddi_pokes(devi, (short *) &fb->ppmask.reg, 255) != DDI_SUCCESS ||
	    ddi_pokes(devi, allrop(fb, mrc_op), CG_SRC) != DDI_SUCCESS ||
	    ddi_pokes(devi, allrop(fb, mrc_mask1), 0) != DDI_SUCCESS ||
	    ddi_pokes(devi, allrop(fb, mrc_mask2), 0) != DDI_SUCCESS ||
	    ddi_pokes(devi, allrop(fb, mrc_shift), 1 << 8) != DDI_SUCCESS ||
	    ddi_pokec(devi, pixel0(fb), 0xa5) != DDI_SUCCESS ||
	    ddi_pokec(devi, pixel0(fb), 0) != DDI_SUCCESS ||
	    ddi_peekc(devi, pixel0(fb), &c) != DDI_SUCCESS ||
	    ((c & 0xff) != 0xa5)  ||
	    ddi_pokes(devi,(short *) &fb->ppmask.reg,0xcc) != DDI_SUCCESS ||
	    ddi_pokes(devi, allrop(fb, mrc_op), ~CG_DEST) != DDI_SUCCESS ||
	    ddi_pokec(devi, pixel0(fb), 0) != DDI_SUCCESS ||
	    ddi_pokes(devi, (short *) &fb->ppmask.reg,255) != DDI_SUCCESS ||
	    ddi_peekc(devi, pixel0(fb), &c) != DDI_SUCCESS ||
	    ((c & 0xff) != (0xa5 ^ 0xcc))) 
	{
	    DEBUGF(1, (CE_CONT, "poke/poke failed\n"));
	    return 1;
	}

	DEBUGF(3, (CE_CONT, "peekc returned:%x\n",c));

	return 0;

#undef	allrop
#undef	pixel0
}

/*ARGSUSED*/
static int
cg2_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	register int instance = ddi_get_instance(devi);
	register struct cg2_softc *softc = getsoftc(instance);
	register struct cg2fb *fb = softc->fb;
	register int flags = softc->flags;
	char	name[16];
	int	page;
	long	sword;

	DEBUGF(3, (CE_CONT, "cg2_attach%d cmd 0x%x\n", instance, cmd));

	if (cmd != DDI_ATTACH)
	    return (DDI_FAILURE);

#define	dummy	flags

	/* 
	 * Determine whether this is a Sun-2 or Sun-3 color board
	 * by setting the wait bit in the double buffering register
	 * and seeing if it clears itself during retrace.
	 *
	 * On the Sun-2 color board this just writes a bit in the
	 * "wordpan" register.
	 */
	fb->misc.nozoom.dblbuf.word = 0;
	fb->misc.nozoom.dblbuf.reg.wait = 1;

	/* wait for leading edge, then trailing edge of retrace */
	while (fb->status.reg.retrace)
	    /* nothing */ ;
	while (!fb->status.reg.retrace)
	    /* nothing */ ;
	while (fb->status.reg.retrace)
	    /* nothing */ ;

	if (fb->misc.nozoom.dblbuf.reg.wait) 
	{
	    /* Sun-2 color board */
	    fb->misc.nozoom.dblbuf.reg.wait = 0;
	    flags |= CG2D_ZOOM;
	}
	else 
	{
	    /* Sun-3 color board (or better) */
	    flags |= CG2D_32BIT | CG2D_NOZOOM;

	    if (fb->status.reg.fastread)
		flags |= CG2D_FASTREAD;

	    if (fb->status.reg.id) 
		flags |= CG2D_ID | CG2D_ROPMODE;

	    /*
	     * Probe for double buffering feature.
	     * Write distinctive values to one pixel in both buffers,
	     * then two pixels in buffer B only.
	     * Read from buffer B and see what we get.
	     *
	     * Warning: assumes we were called right after cgtwoprobe.
	     */
	    cg2_setfunction(fb, CG2_ALLROP, CG_SRC);
	    fb->ropio.roppixel.pixel[0][0] = 0x5a;
	    fb->ropio.roppixel.pixel[0][0] = 0xa5;
	    fb->misc.nozoom.dblbuf.reg.nowrite_a = 1;
	    fb->ropio.roppixel.pixel[0][0] = 0xc3;
	    fb->ropio.roppixel.pixel[0][4] = dummy;
	    if (fb->ropio.roppixel.pixel[0][0] == 0x5a) 
	    {
		fb->misc.nozoom.dblbuf.reg.read_b = 1;

		if (fb->ropio.roppixel.pixel[0][0] == 0xa5 &&
		    fb->ropio.roppixel.pixel[0][4] == 0xc3 &&
		    cg2_dblbuf_enable)
		    flags |= CG2D_DBLBUF;
	    }

	    fb->misc.nozoom.dblbuf.word = 0;
	}

	softc->flags = flags;

	/*  re-map into correct VME space if necessary */

	page = hat_getkpfnum((caddr_t) fb);

	DEBUGF(3, (CE_CONT, "cg2_attach%d page 0x%x flags 0x%x\n",
	    instance, page, flags));

	if ((flags & CG2D_32BIT) && (ddi_peekl(devi, (long *) fb,
	    (long *) &sword) != DDI_SUCCESS)) 
	{
	    int nregs;

	    DEBUGF(3, (CE_CONT, "peekl read:%x\n", sword));

	    DEBUGF(3, (CE_CONT, "cg2_attach%d unmapping\n",instance));

	    ddi_unmap_regs(devi, 0, (caddr_t *)&softc->fb,
		CG2_MAPPED_OFFSET, CG2_MAPPED_SIZE);
	    
	    if (ddi_dev_nregs(devi, &nregs) == DDI_FAILURE)
	    {
		DEBUGF(1, (CE_CONT, "ddi_dev_nregs failed\n"));
		return (DDI_FAILURE) ;
	    }
	    else if (nregs > 1)
	    {
		DEBUGF(3, (CE_CONT, "cg2_attach%d remapping with reg set 1\n",
		    instance));

		if( ddi_map_regs(devi, 1, (caddr_t *)&softc->fb,
		    CG2_MAPPED_OFFSET, CG2_MAPPED_SIZE) !=0) 
		{
		    DEBUGF(1,(CE_CONT, " remap_regs failed\n"));
		    return (DDI_FAILURE) ;
		}
	    }
	}

	DEBUGF(3, (CE_CONT, "cg2_attach%d fb 0x%x kpfnum 0x%x\n",
	    instance, (u_int)softc->fb, hat_getkpfnum((caddr_t)softc->fb)));
 
	if (ddi_peekl(devi, (long *) fb, (long *) &sword) != DDI_SUCCESS) 
	{
	    DEBUGF(1, (CE_CONT, "peekl failed\n"));
		return (DDI_FAILURE) ;
        }

	DEBUGF(3, (CE_CONT, "peekl read:%x\n", sword));

	/*
	 * Register the interrupt handler, if any.
	 */
	ddi_add_intr(devi, (u_int)0, &softc->iblkc,
	    &softc->idevc, cg2_intr, (caddr_t)softc);

	/*  set interrupt vector */
	fb->intrptvec.reg = softc->idevc.idev_vector;

	/*
 	 * Create and initialize some mutexen.
	 */
	mutex_init(&softc->softc_lock,
	    "cg2 softc", MUTEX_DRIVER, softc->iblkc);
	mutex_init(&softc->pixrect_mutex,
	    "cg2 pixrect", MUTEX_DRIVER, softc->iblkc);
	mutex_init(&softc->busy_mutex,
	    "cg2 busy", MUTEX_DRIVER, softc->iblkc);
 
	cv_init(&softc->vrtsleep, "cg2_vert", CV_DRIVER, softc->iblkc);

        sprintf(name, "cgtwo%d", instance);

        if (ddi_create_minor_node(devi, name, S_IFCHR,
            instance, DDI_NT_DISPLAY, NULL) == DDI_FAILURE) 
	{
	    ddi_remove_minor_node(devi, NULL);
	    return (DDI_FAILURE) ;
        }

	/* print informative message */
	cmn_err(CE_CONT, "cgtwo%d: Sun-%c color board%s%s\n", 
	    instance, flags & CG2D_ZOOM ? '2' : '3',
	    flags & CG2D_DBLBUF ? ", double buffered" : "",
	    flags & CG2D_FASTREAD ? ", fast read" : "");

	ddi_report_dev(devi);

	cmn_err(CE_CONT, "!%s%d: resolution %d x %d\n",
	    ddi_get_name(devi), instance, softc->w, softc->h);
 
	return (DDI_SUCCESS);
	/*NOTREACHED*/
}

static int
cg2_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
        register int instance = ddi_get_instance(devi);
        register struct cg2_softc *softc = getsoftc(instance);

        DEBUGF(1, (CE_CONT, "cg2_detach%d cmd 0x%x\n", instance, cmd));

        switch (cmd) 
	{

	    case DDI_DETACH:
                /*
                 * Restore the state of the device to what it was before
                 * we attached it.
                 */
                if (softc->busy)
		    return (DDI_FAILURE);

                ddi_remove_intr(devi, 0, &softc->iblkc);
                ddi_remove_minor_node(devi, NULL);

                if (softc->fb != NULL)
		    ddi_unmap_regs(devi, (u_int)0, (caddr_t *)&softc->fb,
		    CG2_MAPPED_OFFSET, CG2_MAPPED_SIZE);
 
                mutex_destroy(&softc->busy_mutex);
                mutex_destroy(&softc->pixrect_mutex);
                mutex_destroy(&softc->softc_lock);
		cv_destroy(&softc->vrtsleep);
 
                ddi_soft_state_free(cg2_state, instance);
                return (DDI_SUCCESS);
 
	    default:
                return (DDI_FAILURE);
        }
}


/* ARGSUSED */
static int
cg2_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register dev_t dev = (dev_t) arg;
	register int unit, error;
	register struct cg2_softc *softc;

	DEBUGF(2, (CE_CONT, "cg2_info: dev:%x\n", dev));

	if ((unit = getminor(dev)) > ncg2)
	    return (DDI_FAILURE);

	switch (infocmd) 
	{

	    case DDI_INFO_DEVT2DEVINFO:

		if ((softc = getsoftc(unit)) == NULL) 
		    error = DDI_FAILURE;
		else 
		{
		    *result = (void *) softc->mydip;
		    error = DDI_SUCCESS;
		}
		break;

	    case DDI_INFO_DEVT2INSTANCE:

		*result = (void *)unit;
		error = DDI_SUCCESS;
		break;

	    default:
		error = DDI_FAILURE;
	}

	return (error);
}

/*ARGSUSED*/
static int
cg2_open(dev_t *dev_p, int flag, int otyp, cred_t *cr)
{
	register int instance = getminor(*dev_p);
	register struct cg2_softc *softc = getsoftc(instance);
	register int error = 0;

	DEBUGF(2, (CE_CONT, "cg2_open%d, flag:%x\n", instance, flag));

	error = (instance > ncg2) ? ENXIO :0;

	if (otyp != OTYP_CHR)
	    error = EINVAL;
	else if (softc == NULL)
	    error = ENXIO;

	if (!error)
	{
	    mutex_enter(&softc->busy_mutex);
	    softc->busy = 1;
	    mutex_exit(&softc->busy_mutex);
	}

	DEBUGF(2, (CE_CONT, "cg2_open%d, otyp:%x, error:%x\n", instance, 
	    otyp, error));

	return (error);
}

/*ARGSUSED*/
static int
cg2_close(dev_t dev, int flag, int otyp, struct cred *cred)
{
	register int instance = getminor(dev);
	register struct cg2_softc *softc = getsoftc(instance);
	register struct cg2fb *fb = softc->fb;
	register int error = 0;

	DEBUGF(2, (CE_CONT, "cg2_close%d, flag:%x\n", instance, flag));

	if (otyp != OTYP_CHR)
	    error = EINVAL;
	else if (softc == NULL)
	    error = ENXIO;
	else 
	{
	    mutex_enter(&softc->softc_lock);

	    /* fix up zoom and/or double buffering on close */

	    if (softc->flags & CG2D_ZOOM) 
	    {
		fb->misc.zoom.wordpan.reg = 0;	/* hi pixel adr = 0 */
		fb->misc.zoom.zoom.word = 0;	/* zoom=0, yoff=0 */
		fb->misc.zoom.pixpan.word = 0;	/* pix adr=0, xoff=0 */
		fb->misc.zoom.varzoom.reg = 255;/*unzoom at line 4*255*/
	    }

	    if (softc->flags & CG2D_NOZOOM) 
		fb->misc.nozoom.dblbuf.word = 0;

	    mutex_exit(&softc->softc_lock);

	    mutex_enter(&softc->busy_mutex);
	    softc->busy = 0;
	    mutex_exit(&softc->busy_mutex);

	    /*
	     * if we get descheduled here, and the driver is unloaded
	     * we die horribly right here ..
	     */
	}
 
	DEBUGF(2, (CE_CONT, "cg2_close%d, otyp:%x, error:%x\n", instance, 
	    otyp, error));

	return (error);
}

/*ARGSUSED*/
static int
cg2_mmap(dev_t dev, off_t off, int prot)
{
	register struct cg2_softc *softc = getsoftc(getminor(dev));

	DEBUGF(off ? 7 : 1,
	    (CE_CONT, "cg2_mmap%d: 0x%x\n", getminor(dev), (u_int)off));

	if ((u_int) off >= CG2_TOTAL_SIZE)
	    return -1;

	return (hat_getkpfnum((caddr_t)softc->fb) + ddi_btop(softc->mydip,off) -
	    ddi_btop(softc->mydip, CG2_MAPPED_OFFSET));
}

/*ARGSUSED*/
cg2_ioctl(dev_t dev,int cmd, int arg, int mode, cred_t *cred_p, int *rval_p)
{
	register int instance = getminor(dev);
	register struct cg2_softc *softc = getsoftc(instance);
	register int error = 0;
	int	 (*copyinfun)(), (*copyoutfun)();

	DEBUGF(2, (CE_CONT, "cg2_ioctl%d: 0x%x\n", instance, cmd));

	/* handle kernel-mode ioctl's issued by sunview */

#if NWIN > 0
#if defined(FBIO_KCOPY) || defined(FKIOCTL)
#ifdef FBIO_KCOPY
	copyinfun = mode & FBIO_KCOPY ? kcopy : copyin;
	copyoutfun = mode & FBIO_KCOPY ? kcopy : copyout;
#else /* FKIOCTL */
	copyinfun = mode & FKIOCTL ? kcopy : copyin;
	copyoutfun = mode & FKIOCTL ? kcopy : copyout;
#endif
#else
	copyinfun = fbio_kcopy ? kcopy : copyin;
	copyoutfun = fbio_kcopy ? kcopy : copyout;
	fbio_kcopy = 0;
#endif
#else
	copyinfun = copyin;
	copyoutfun = copyout;
#endif

	switch (cmd) 
	{
	    case FBIOGTYPE: 
	    {
		auto struct fbtype fb = fbgattr_default.fbtype;

		DEBUGF(2, (CE_CONT, "FBIOGTYPE\n"));

		fb.fb_height = softc->h;
		fb.fb_width  = softc->w;

		if (error = COPY(copyoutfun, &fb, arg, sizeof(struct fbtype)))
		    return(error);
	    }
	    break;

	    case FBIOGATTR: 
	    {
		auto struct fbgattr gattr = fbgattr_default;

		DEBUGF(2, (CE_CONT, "FBIOGATTR\n"));

		gattr.fbtype.fb_height = softc->h;
		gattr.fbtype.fb_width = softc->w;

		if (softc->flags & CG2D_NOZOOM)
		    gattr.sattr.dev_specific[FB_ATTR_CG2_FLAGS] |=
			FB_ATTR_CG2_FLAGS_SUN3;

		if (softc->flags & CG2D_DBLBUF)
		    gattr.sattr.dev_specific[FB_ATTR_CG2_BUFFERS] = 2;

		gattr.sattr.dev_specific[FB_ATTR_CG2_PRFLAGS] = softc->flags;

		if (error = COPY(copyoutfun,&gattr,arg,sizeof(struct fbgattr)))
		    return(error);
	    }
	    break;

	    case FBIOSATTR: 
	    { 
		DEBUGF(2, (CE_CONT, "FBIOGATTR\n"));
	    }
	    break;

#if NWIN > 0

	    case FBIOGPIXRECT: 
	    {
		auto struct fbpixrect fbpix;

		DEBUGF(2, (CE_CONT, "FBIOGPIXRECT\n"));

		if ((mode & FKIOCTL) != FKIOCTL)	/* for kernel only ! */
		{
		    DEBUGF(2, (CE_CONT, "FBIOGPIXRECT issued by user\n"));
		    return (ENOTTY);
		}

		mutex_enter(&softc->pixrect_mutex);

		fbpix.fbpr_pixrect = &softc->pr;

		/* initialize pixrect */
		softc->pr.pr_ops = &cg2_ops;
		softc->pr.pr_size.x = softc->w;
		softc->pr.pr_size.y = softc->h;
		softc->pr.pr_depth = CG2_DEPTH;
		softc->pr.pr_data = (caddr_t) &softc->prd;

		/* initialize private data */
		bzero((char *) &softc->prd, sizeof softc->prd);
		softc->prd.cgpr_va = softc->fb;
		softc->prd.cgpr_fd = 0;
		softc->prd.cgpr_planes = 255;
		softc->prd.ioctl_fd = getminor(dev);
		softc->prd.flags = softc->flags;
		softc->prd.linebytes = softc->w;

		/* enable video */
		softc->fb->status.reg.video_enab = 1;

		mutex_exit(&softc->pixrect_mutex);

		if (error = COPY(kcopy, &fbpix, arg, sizeof(struct fbpixrect))) 
		    return(error);

	    }
	    break;

#endif		/* NWIN > 0 */

	    /* get info for GP */
	    case FBIOGINFO: 
	    {
		auto struct fbinfo fbinfo;

		DEBUGF(2, (CE_CONT, "FBIOGINFO\n"));

		fbinfo.fb_physaddr = 
		    (hat_getkpfnum((caddr_t) softc->fb) << PGSHIFT) -
		    CG2_MAPPED_OFFSET & 0xffffff;

		fbinfo.fb_hwwidth = softc->w;
		fbinfo.fb_hwheight = softc->h;
		fbinfo.fb_ropaddr = (u_char *) softc->fb;

		if (error = COPY(copyoutfun,&fbinfo,arg,sizeof(struct fbinfo)))
		    return(error);
	
	    }
	    break;

	    /* set video flags */
	    case FBIOSVIDEO: 
	    {
                auto int video;
                  
                DEBUGF(2, (CE_CONT, "FBIOSVIDEO\n"));

                if (error = COPY(copyinfun, arg, &video, sizeof (int)))
		    return(error);

		mutex_enter(&softc->softc_lock);

		softc->fb->status.reg.video_enab = 
		    video & FBVIDEO_ON ? 1 : 0;

		mutex_exit(&softc->softc_lock);

	    }
	    break;

	    /* get video flags */
	    case FBIOGVIDEO: 
	    {
                auto int video;
                  
                DEBUGF(2, (CE_CONT, "FBIOGVIDEO\n"));

		video = softc->fb->status.reg.video_enab
		    ? FBVIDEO_ON : FBVIDEO_OFF;

		if (error = COPY(copyoutfun, &video, arg, sizeof (int)))
		    return(error);
	    }
	    break;

	    case FBIOVERTICAL: 
                DEBUGF(2, (CE_CONT, "FBIOVERTICAL\n"));

		cgtwo_wait(getminor(dev));
		break;

	    default:
                DEBUGF(2, (CE_CONT, "not supported ioctl:%x\n",cmd));

		return ENOTTY;
	}

	return 0;
}

/*
 * This oneshot timeout is needed to ensure that requested vertical
 * retrace interrupts are serviced.  It works around a problem exhibited
 * by pixrects on sparc platforms:  pixrects code performs exclusive-or
 * operations on the ropmode bits in the status register.  On the sparc,
 * the exclusive-or requires three instructions, ie it is nonatomic, and
 * can/does get interrupted in the middle of the 3-instruction sequence.
 */
static void
cgtwotimeout(unit)
int unit;
{
	register struct cg2_softc *softc = getsoftc(unit);

	DEBUGF(2, (CE_CONT, "cg2timeout%d: softc:%x\n", unit, softc));

	mutex_enter(&softc->softc_lock);

        softc->fb->status.reg.inten = 1;
        softc->toutid = timeout(cgtwotimeout, (caddr_t) (unit & 255), hz);

	DEBUGF(2, (CE_CONT, "cg2timeout%d: toutid:%x\n", unit, softc->toutid));

	mutex_exit(&softc->softc_lock);
}

/* wait for vertical retrace interrupt */
cgtwo_wait(unit )
	int unit ;
{
	register struct cg2_softc *softc = getsoftc(unit);

	DEBUGF(2, (CE_CONT, "cg2_wait%d: softc:%x\n", unit, softc));

	mutex_enter(&softc->softc_lock);

	softc->fb->status.reg.inten = 1;

        /* see comments on cgtwotimeout() */
        softc->toutid = timeout(cgtwotimeout, (caddr_t) (unit & 255), hz);

	DEBUGF(2,(CE_CONT,"cg2_wait%d: going to sleep on softc:%x, toutid:%x\n",
		unit, softc, softc->toutid));

	cv_wait(&softc->vrtsleep, &softc->softc_lock);

	mutex_exit(&softc->softc_lock);

	DEBUGF(2, (CE_CONT, "cg2_wait%d: woken up softc:%x\n", unit, softc));
}

/* vertical retrace interrupt service routine */
static u_int
cg2_intr(caddr_t intr_arg)
{
	register struct cg2_softc *softc = (struct cg2_softc *) intr_arg;

	DEBUGF(2, (CE_CONT, "cg2_intr: softc:%x\n", softc));

	mutex_enter(&softc->softc_lock);

	(void) untimeout(softc->toutid);

	softc->fb->status.reg.inten = 0;

	DEBUGF(2, (CE_CONT, "cg2_intr: going to wakeup softc:%x, toutid:%x\n",
		softc, softc->toutid));

	cv_signal(&softc->vrtsleep);

	mutex_exit(&softc->softc_lock);

	return (DDI_INTR_CLAIMED);
}
