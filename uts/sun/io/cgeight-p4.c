#pragma ident	"@(#)cgeight-p4.c	1.13	94/11/17 SMI"
/*
	from cgfour.c 1.3 92/06/05 SMI
*/

/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

/*
	Twenty Four Bit Color frame buffer with overlay plane (cg8) driver.
*/

/*
 * Configuration Switches:
 */
#define	DEVELOPMENT_ONLY	/* always off for integrated code */
#define	CG8DEBUG		/* inserts pokable debugging code */

#if NWIN < 1
#undef NWIN
#define	NWIN 1			/* force insertion of pixrect code */
#endif

#define	NCGEIGHT	1	/* all you can put on the P4 anyway */


/*
 * Include files:
 */
#include <sys/param.h>
#include <sys/time.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/map.h>
#include <sys/fbio.h>
#include <sys/p4reg.h>
#include <sys/pixrect.h>
#include <sys/pr_impl_util.h>
#include <sys/pr_planegroups.h>
#include <sys/memreg.h>
#include <sys/cg8-p4reg.h>
#include <sys/cg8-p4var.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/cmn_err.h>
#include <sys/open.h>
#include <sys/file.h>
#include <sys/modctl.h>
#include <sys/machsystm.h>

#ifdef	CG8DEBUG
extern	void	prom_printf(char *fmt, ...);
int cg8_debug = 0;
#define	DEBUGF(level, args)  _STMT(\
			if (cg8_debug >= (level)) prom_printf args; \
)
#else	CG8DEBUG
#define	DEBUGF(level, args)	/* nothing */
#endif 	CG8DEBUG


#define	P4_ID_TYPE_MASK		0x7f000000
#define	P4_REG_RESET		0x01	/* should be in p4reg.h	*/


/* driver per-unit data */
struct cg8_softc {
	kmutex_t	softc_lock;	/* protect softc	*/
	short		flags;		/* internal flags	*/
	caddr_t		fb[CG8_NFBS];	/* pointers to fb sections	*/
	int		w, h;		/* resolution	*/
	int		size;		/* total size of fb	*/
	int		bw2size;	/* total size of overlay pl */
	struct proc	*owner;		/* owner of the fb	*/
	struct fbgattr	gattr;		/* current attributes	*/
	u_long	cg8_physical;
	short		*ovlptr;
	short		*enptr;
	u_long		*p4reg;		/* pointer to P4 register	*/
	ddi_iblock_cookie_t iblkc;	/* interrupt identifier	*/
	dev_info_t	*mydip;		/* my devinfo pointer	*/
	dev_t		mydev_t;	/* a hack	*/
	struct ramdac	*lut;		/* ptr to hardware colormap */
#define	CG8_CMAP_SIZE 256
#define	CG8_OMAP_SIZE 4
	union fbunit	cmap_rgb[CG8_CMAP_SIZE];
	int		cmap_begin;
	int		cmap_end;
	union fbunit	omap_rgb[CG8_OMAP_SIZE];
	int		omap_begin;
	int		omap_end;
	u_char		em_red[CG8_CMAP_SIZE]; /* indexed cmap emulation */
	u_char		em_green[CG8_CMAP_SIZE];
	u_char		em_blue[CG8_CMAP_SIZE];
#if NWIN > 0
	kmutex_t	pixrect_mutex;	/* protect pixrect data	*/
	Pixrect		pr;
	struct cg8_data	cg8d;
#endif NWIN > 0
};

/* probe size -- enough for the P4 register + colormap/status reg. */
#define	CG8_PROBESIZE	(NBPG + sizeof (*cg8_softc[0].cmap))

/* default structure for FBIOGTYPE ioctl */
static struct fbtype cg8typedefault = {
/*	type	h	w	depth cms size */
	FBTYPE_SUN2BW, 0, 0, 1, 0, 0
};

/* default structure for FBIOGATTR ioctl */
static struct fbgattr cg8attrdefault = {
/*	real_type	owner */
	FBTYPE_MEMCOLOR, 0,
/* fbtype: type			h	w	depth	cms	size */
	{FBTYPE_MEMCOLOR,	0,	0,	32,	256,	0},
/* fbsattr: flags		emu_type	dev_specific */
	{FB_ATTR_AUTOINIT,	FBTYPE_SUN2BW,	{0}},
/* emu_types */
	{FBTYPE_MEMCOLOR, FBTYPE_SUN2BW, -1, -1}
};

/* frame buffer description table */
static struct cg8_fbdesc {
	short	depth;		/* depth, bits */
	short	group;		/* plane group */
	int	allplanes;	/* initial plane mask */
}	cg8_fbdesc[CG8_NFBS + 1] = {
	{ 1, PIXPG_OVERLAY, 0 },
	{ 1, PIXPG_OVERLAY_ENABLE, 0 },
	{ 32, PIXPG_24BIT_COLOR, 0xffffff } };

#define	CG8_FBINDEX_OVERLAY	0
#define	CG8_FBINDEX_ENABLE	1
#define	CG8_FBINDEX_COLOR	2

/* initial active frame buffer */
#ifndef	CG8_INITFB
#define	CG8_INITFB	CG8_FBINDEX_OVERLAY
#endif

extern	int copyin(), copyout(), kcopy();

/*
	Macros:
*/
#define	getsoftc(instance) \
	((struct cg8_softc *)ddi_get_soft_state(cg8_state, (instance)))

#define	ITEMSIN(array)	(sizeof (array) / sizeof (array)[0])
#define	ENDOF(array)	((array) + ITEMSIN(array))
#define	BZERO(d, c)	bzero((caddr_t) (d), (u_int) (c))
#define	COPY(f, s, d, c)	(f((caddr_t) (s), (caddr_t) (d), (u_int) (c)))
#define	BCOPY(s, d, c)	COPY(bcopy, (s), (d), (c))
#define	COPYIN(s, d, c)	COPY(copyin, (s), (d), (c))
#define	COPYOUT(s, d, c)	COPY(copyout, (s), (d), (c))

/* enable/disable interrupt */
#define	ENABLE_INTR(s)	\
	(setintrenable(1),	\
	(*(s)->p4reg = (*(s)->p4reg & ~P4_REG_RESET) | 		\
	P4_REG_INTCLR | P4_REG_INTEN),				\
	(*(s)->p4reg = (*(s)->p4reg & ~(P4_REG_RESET|P4_REG_INTCLR))))

#define	cg8_int_enable(softc)	ENABLE_INTR((softc))

#define	DISABLE_INTR(s)	{	\
	*(s)->p4reg &= ~P4_REG_INTEN & ~P4_REG_RESET;	\
	setintrenable(0);	\
	}
#define	cg8_int_disable(softc)	DISABLE_INTR((softc))

/* check if color map update is pending */
#define	cg8_update_pending(softc)	\
		((softc)->cg8d.flags & CG8_UPDATE_PENDING)

static void	*cg8_state;	/* opaque basket of softc structs */
static int	instance_max;	/* highest instance number seen	*/

/* forward references */
static struct	modlinkage modlinkage;
static u_int	cgeight_intr();
static int	cgeight_ioctl();
static int	cg8_rop();
static int	cg8_putcolormap();
static int	cg8_putattributes();
static int	cg8_ioctl();

/* more of same */
static int p4probe(dev_info_t *devi, caddr_t addr, int *w, int *h);

#if NWIN > 0

/*
	SunWindows specific stuff
*/

/* kernel pixrect ops vector */
static struct pixrectops cg8_ops = {
	cg8_rop,
	cg8_putcolormap,
	cg8_putattributes,
#ifdef _PR_IOCTL_KERNEL_DEFINED
	cg8_ioctl
#endif
};

/*
 * replacement for pfind() which disappeared in 5.0
 */
extern struct proc	*prfind();
extern kmutex_t		pidlock;

static struct proc *
pfind(pid_t pid)
{
	struct proc *proc_p;

	mutex_enter(&pidlock);
	proc_p = prfind(pid);
	mutex_exit(&pidlock);

	return (proc_p);
}


/*
	From cg8_colormap.c  Always returns zero.
 */
static int
cg8_putattributes(
	Pixrect *pr,
	int	*planesp)
{
	struct cg8_data *cg8d;
	u_int	planes;
	u_int		group;
	int	dont_set_planes;

	DEBUGF(1, ("cg8_putattributes - entry\n"));

	dont_set_planes = *planesp & PIX_DONT_SET_PLANES;
	planes = *planesp & PIX_ALL_PLANES;	/* the plane mask */
	group = PIX_ATTRGROUP (*planesp);	/* extract the group part */

	cg8d = cg8_d (pr);

	/*
		User is trying to set the group to something else. We'll see if
		the group is supported.	If it is, do as he wishes.
	*/
	if (group != PIXPG_CURRENT || group == PIXPG_INVALID) {
		int	active = 0;
		int	found = 0;
		int	cmsize;

		/* kernel should not access the color frame buffer */
		if (group == PIXPG_TRANSPARENT_OVERLAY)
		{
			group = PIXPG_OVERLAY;
			cg8d->flags |= CG8_COLOR_OVERLAY;
		}
		else
			cg8d->flags &= ~CG8_COLOR_OVERLAY;

		for (; active < cg8d->num_fbs && !found; active++)
			found = (group == cg8d->fb[active].group);
			if (found) {
				cg8d->mprp = cg8d->fb[active].mprp;
				cg8d->planes = PIX_GROUP (group) |
					(cg8d->mprp.planes & PIX_ALL_PLANES);
				cg8d->active = active;
				pr->pr_depth = cg8d->fb[active].depth;
				switch (group)
				{
				default:
				case PIXPG_8BIT_COLOR:
				case PIXPG_24BIT_COLOR:
					cmsize = 256;
					if (cg8d->flags & CG8_PIP_PRESENT)
						cg8d->flags |= CG8_STOP_PIP;
					break;

				case PIXPG_OVERLAY:
					cmsize = 4;
					cg8d->flags &= ~CG8_STOP_PIP;
					break;

				case PIXPG_OVERLAY_ENABLE:
					cmsize = 0;
					cg8d->flags &= ~CG8_STOP_PIP;
					break;

				case PIXPG_VIDEO_ENABLE:
					cmsize = 0;
					if (cg8d->flags & CG8_PIP_PRESENT)
						cg8d->flags |= CG8_STOP_PIP;
					break;
				}
				(void) cg8_ioctl(pr, FBIOSCMSIZE,
					(caddr_t)&cmsize);
			}
	}

	/* group is PIXPG_CURRNT here */
	if (!dont_set_planes) {
		cg8d->planes =
			cg8d->mprp.planes =
			cg8d->fb[cg8d->active].mprp.planes =
			PIX_GROUP (group) | planes;
	}

	DEBUGF(1, ("cg8_putattributes - exit"));
	return (0);
}

static int
cg8_putcolormap(
	Pixrect		*pr,
	int		index,
	int		count,
	unsigned char	*red,
	unsigned char	*green,
	unsigned char	*blue)
{
	struct cg8_data		*cg8d;
	struct fbcmap		fbmap;
	int			cc;
	int			i;
	int			plane;
	struct cg8_softc	*softc;
	int			kernelModeBit = 0;
	caddr_t			fbmAddr;

	DEBUGF(1, ("cg8_putcolormap - entry\n"));
#if defined(FBIO_KCOPY) || defined(FKIOCTL)
#ifdef FBIO_KCOPY
	kernelModeBit = FBIO_KCOPY;
#else /* FKIOCTL */
	kernelModeBit = FKIOCTL;
#endif
#endif

	/*
	* Set "cg8d" to the cg8 private data structure.	If the plane
	* is not specified in the index, then use the currently active
	* plane.
	*/

	cg8d = cg8_d(pr);
	softc = getsoftc(cg8d->fd);
	fbmAddr = (caddr_t)&fbmap;
	DEBUGF(2, ("putc got softc address %x fbmap %x\n", softc, fbmAddr));
	if (PIX_ATTRGROUP(index))
	{
	plane = PIX_ATTRGROUP(index) & PIX_GROUP_MASK;
	index &= PR_FORCE_UPDATE | PIX_ALL_PLANES;
	}
	else
	plane = cg8d->fb[cg8d->active].group;

	/*
	* When PR_FORCE_UPDATE is set, pass everything straight through to
	* the ioctl.	If PR_FORCE_UPDATE is not set, take "emulated index"
	* actions.
	*/

	if (index & PR_FORCE_UPDATE || plane == PIXPG_24BIT_COLOR ||
		plane == PIXPG_8BIT_COLOR)
	{
		softc->cg8d.flags |= CG8_KERNEL_UPDATE;	/* bcopy vs. copyin */
		fbmap.index = index | PIX_GROUP(plane);
		fbmap.count = count;
		fbmap.red = red;
		fbmap.green = green;
		fbmap.blue = blue;
		cc = cgeight_ioctl((softc->mydev_t), FBIOPUTCMAP, (int)fbmAddr,
			kernelModeBit, 0, 0);
		return (cc);
	} else
	{
		/*
			emulated index
		*/
		if (plane == PIXPG_OVERLAY_ENABLE)
			return (0);

		if (plane == PIXPG_OVERLAY)
		{
			for (cc = i = 0; count && !cc; i++, index++, count--)
			{
				/* bcopy vs. copyin */
				softc->cg8d.flags |= CG8_KERNEL_UPDATE;
				/*
					Index 0 is mapped to 1.
					All others mapped to 3.
				*/
				fbmap.index =
					(index ? 3 : 1) | PIX_GROUP(plane);
				fbmap.count = 1;
				fbmap.red = red+i;
				fbmap.green = green+i;
				fbmap.blue = blue+i;
				cc = cgeight_ioctl(
					(softc->mydev_t), FBIOPUTCMAP,
					(int)fbmAddr, kernelModeBit, 0, 0);
			}
			return (cc);
		}
	}
	DEBUGF(1, ("cg8_putcolormap - exit\n"));
	return (PIX_ERR);
}

#endif	NWIN > 0

/*
 * Determine if a cgeight exists at the given address.
 */
static int
cgeight_probe(dev_info_t *devi)
{
	caddr_t			p4regAddr;
	int			w, h;
	int			p4_id;

	DEBUGF(1, ("cgeight: cgeight_probe: entering\n"));

	/*
		Map the P4 register so that we can (try to) get the resolution.
	 */
	if (ddi_map_regs(devi, CG8_REGNUM_P4_PROBE_ID,
		&p4regAddr, (off_t)0, sizeof (u_long)) != 0)
	{
		DEBUGF(1, ("cgeight_probe: ddi_map_regs (p4reg) FAILED\n"));
		return (DDI_PROBE_FAILURE);
	}

	/*
		Determine frame buffer resolution: use the P4 type code.
	 */
	p4_id = p4probe(devi, p4regAddr, &w, &h);
	DEBUGF(2, ("cg8 - p4_id is %x\n", p4_id));
	if (p4_id < 0)
	{
		DEBUGF(1, ("cgeight_probe: p4probe FAILED\n"));
		return (DDI_PROBE_FAILURE);
	}

	if (p4_id != P4_ID_COLOR24)
	{
		DEBUGF(1, ("cgeight_probe: (p4_id != P4_ID_COLOR24)\n"));
		return (DDI_PROBE_FAILURE);
	}

	/*
		Reset it, and assign the old value back.
		Attach may be called after the boot prom has probed it.
		To avoid losing the video halfway booting, we set it to
		the old value.  Then write zero to sync it.
	*/
	*p4regAddr = (*p4regAddr & ~P4_REG_RESET) | P4_REG_SYNC;
	*p4regAddr &= ~P4_REG_SYNC & ~P4_REG_RESET;

	ddi_unmap_regs(devi, CG8_REGNUM_P4_PROBE_ID, &p4regAddr,
		(off_t)0, sizeof (u_long));
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
cgeight_detach(
	dev_info_t		*devi,
	ddi_detach_cmd_t	detach_cmd)
{
	return (DDI_FAILURE);		/* not unloadable */
}

static int
cgeight_attach(
	dev_info_t		*devi,
	ddi_attach_cmd_t	attach_cmd)
{
	int			instance;
	int			retval;
	struct cg8_softc	*softc;
	char			name[16];
	int			nbytes_1bit_plane;
	int			nbytes_24bit_plane;

	instance = ddi_get_instance(devi);
	DEBUGF(2, ("cgeight%d: cgeight_attach: entering\n", instance));

	if (attach_cmd != DDI_ATTACH)
	{
		DEBUGF(1, ("cgeight: attach: (attach_cmd != DDI_ATTACH)\n"));
		return (DDI_FAILURE);
	}

	if (instance > instance_max)
	{
		instance_max = instance;
	}

	if (ddi_soft_state_zalloc(cg8_state, instance) != 0)
	{
		DEBUGF(1, ("cgeight_attach: ddi_soft_state_zalloc FAILED\n"));
		return (DDI_FAILURE);
	}

	softc = getsoftc(instance);
	softc->mydip = devi;
	ddi_set_driver_private(devi, (caddr_t)softc);
	DEBUGF(2, ("cg8 softc at %x\n", softc));

	/*
	 * Map the P4 register so that we can get the resolution.
	 */
	if (ddi_map_regs(devi, CG8_REGNUM_P4_PROBE_ID,
		(caddr_t *)&softc->p4reg,
		(off_t)0, sizeof (*softc->p4reg)) != 0)
	{
		DEBUGF(1, ("cgeight_attach: ddi_map_regs (p4reg) FAILED\n"));
		return (DDI_FAILURE);
	}

	/*
		Figure the size, rounded up to the nearest page
	*/
	nbytes_1bit_plane = ddi_ptob(devi,
		ddi_btopr(devi, mpr_linebytes(softc->w, 1) * softc->h));
	nbytes_24bit_plane = ddi_ptob(devi,
		ddi_btopr(devi, mpr_linebytes(softc->w, 32) * softc->h));

	softc->bw2size = nbytes_1bit_plane;
	softc->size = nbytes_1bit_plane + nbytes_1bit_plane
		+ nbytes_24bit_plane;

	/*
		Map the device registers:
	 */
	if (ddi_map_regs(devi, CG8_REGNUM_CMAP,
		(caddr_t *)&softc->lut,
		(off_t)0, sizeof (*softc->lut)) != 0)
	{
		DEBUGF(1, ("cgeight_attach: ddi_map_regs (lut) FAILED\n"));
		ddi_unmap_regs(devi, CG8_REGNUM_P4_PROBE_ID,
			(caddr_t *)&softc->p4reg,
			(off_t)0, sizeof (*softc->p4reg));
		return (DDI_FAILURE);
	}

	if (ddi_map_regs(devi, CG8_REGNUM_OVERLAY,
		(caddr_t *)&softc->fb[CG8_FBINDEX_OVERLAY],
		(off_t)0, nbytes_1bit_plane) != 0)
	{
		DEBUGF(1, ("cgeight_attach: ddi_map_regs (OVERLAY) FAILED\n"));
		ddi_unmap_regs(devi, CG8_REGNUM_P4_PROBE_ID,
			(caddr_t *)&softc->p4reg,
			(off_t)0, sizeof (*softc->p4reg));
		ddi_unmap_regs(devi, CG8_REGNUM_CMAP,
			(caddr_t *)&softc->lut,
			(off_t)0, sizeof (*softc->lut));
		return (DDI_FAILURE);
	}

	if (ddi_map_regs(devi, CG8_REGNUM_ENABLE,
		(caddr_t *)&softc->fb[CG8_FBINDEX_ENABLE],
		(off_t)0, nbytes_1bit_plane) != 0)
	{
		DEBUGF(1, ("cgeight_attach: ddi_map_regs (ENABLE) FAILED\n"));
		ddi_unmap_regs(devi, CG8_REGNUM_P4_PROBE_ID,
			(caddr_t *)&softc->p4reg,
			(off_t)0, sizeof (*softc->p4reg));
		ddi_unmap_regs(devi, CG8_REGNUM_CMAP,
			(caddr_t *)&softc->lut,
			(off_t)0, sizeof (*softc->lut));
		ddi_unmap_regs(devi, CG8_REGNUM_OVERLAY,
			(caddr_t *)&softc->fb[CG8_FBINDEX_OVERLAY],
			(off_t)0, nbytes_1bit_plane);
		return (DDI_FAILURE);
	}

	if (ddi_map_regs(devi, CG8_REGNUM_COLOR,
		(caddr_t *)&softc->fb[CG8_FBINDEX_COLOR],
		(off_t)0, nbytes_24bit_plane) != 0)
	{
		DEBUGF(1, ("cgeight_attach: ddi_map_regs (COLOR) FAILED\n"));
		ddi_unmap_regs(devi, CG8_REGNUM_P4_PROBE_ID,
			(caddr_t *)&softc->p4reg,
			(off_t)0, sizeof (*softc->p4reg));
		ddi_unmap_regs(devi, CG8_REGNUM_CMAP,
			(caddr_t *)&softc->lut,
			(off_t)0, sizeof (*softc->lut));
		ddi_unmap_regs(devi, CG8_REGNUM_OVERLAY,
			(caddr_t *)&softc->fb[CG8_FBINDEX_OVERLAY],
			(off_t)0, nbytes_1bit_plane);
		ddi_unmap_regs(devi, CG8_REGNUM_ENABLE,
			(caddr_t *)&softc->fb[CG8_FBINDEX_ENABLE],
			(off_t)0, nbytes_1bit_plane);
		return (DDI_FAILURE);
	}

	/*
		Initialize our softc
	*/
	softc->flags = 0;
	softc->owner = NULL;

	softc->gattr = cg8attrdefault;
	softc->gattr.fbtype.fb_height = softc->h;
	softc->gattr.fbtype.fb_width = softc->w;
	softc->gattr.fbtype.fb_size = softc->size;

	/*
	* initialize the Brooktree ramdac. The addr_reg selects which register
	* inside the ramdac will be affected.
	*/

	INIT_BT458(softc->lut);
	INIT_OCMAP(softc->omap_rgb);
	INIT_CMAP(softc->cmap_rgb, CG8_CMAP_SIZE);

	softc->omap_begin = 0;
	softc->omap_end = 4;
	softc->cmap_begin = 0;
	softc->cmap_end = CG8_CMAP_SIZE;
	softc->cg8d.flags |= CG8_OVERLAY_CMAP | CG8_UPDATE_PENDING |
		CG8_24BIT_CMAP;

	/*
	 * Register the interrupt handler.
	 */
	retval = ddi_add_intr(devi, (u_int)0, &softc->iblkc,
		(ddi_idevice_cookie_t *)0, cgeight_intr, (caddr_t)softc);
	if (retval != DDI_SUCCESS)
	{
		DEBUGF(1, ("cgeight: attach: ddi_add_intr FAILED\n"));
		return (DDI_FAILURE);
	}

	/*
	 * Create and initialize some mutexen.
	 */
	mutex_init(&softc->softc_lock,
		"cgeight softc", MUTEX_DRIVER, softc->iblkc);
	mutex_init(&softc->pixrect_mutex,
		"cgeight pixrect", MUTEX_DRIVER, softc->iblkc);

	/*
	 * Create the minor device entries:
	 */
	sprintf(name, "cgeight%d", instance);
	if (ddi_create_minor_node(devi, name, S_IFCHR, instance,
				DDI_NT_DISPLAY, 0) == DDI_FAILURE)
	{
		DEBUGF(1, ("cgeight: attach: ddi_create_minor_node FAILED\n"));
		ddi_remove_minor_node(devi, NULL);
		return (DDI_FAILURE);
	}

	ddi_report_dev(devi);

	cmn_err(CE_CONT, "!%s%d: resolution %d x %d\n",
		ddi_get_name(devi), instance, softc->w, softc->h);

	DEBUGF(2, ("cgeight%d: cgeight_attach: returning DDI_SUCCESS \n",
							instance));
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
cgeight_open(
	dev_t	*dev_p,
	int	flag,
	int	otyp,
	cred_t	*cr)
{
	int			instance;
	struct cg8_softc	*softc;
	int 			error = 0;

	instance = getminor(*dev_p);
	softc	 = getsoftc(instance);
	softc->mydev_t = *dev_p;

	DEBUGF(2,
		("cgeight%d: cgeight_open: softc %x softc->mydev_t %x\n",
		instance, softc, softc->mydev_t));

	if (otyp != OTYP_CHR)
	{
		error = EINVAL;
		goto error_out;
	}

	if (softc == NULL)
	{
		error = ENXIO;
		goto error_out;
	}

	DEBUGF(2, ("cgeight: cgeight_open: leaving (success)\n"));
	return (error);

error_out:
	DEBUGF(1, ("cgeight: cgeight_open: returning ERROR: %d\n", error));
	return (error);
}


/*ARGSUSED*/
static int
cgeight_close(
	dev_t		dev,
	int		flag,
	int		otyp,
	struct cred	*cred)
{
	int			instance;
	struct cg8_softc	*softc;
	int 			error = 0;

	instance = getminor(dev);
	softc	 = getsoftc(instance);

	DEBUGF(2, ("cgeight%d: cgeight_close: entering\n", instance));

	if (otyp != OTYP_CHR)
	{
		error = EINVAL;
		goto error_out;
	}

	if (softc == NULL)
	{
		error = ENXIO;
		goto error_out;
	}

	mutex_enter(&softc->softc_lock);

	softc->cg8d.flags &= (CG8_UPDATE_PENDING | CG8_OVERLAY_CMAP);
	softc->owner = NULL;

	softc->gattr = cg8attrdefault;
	softc->gattr.fbtype.fb_height = softc->h;
	softc->gattr.fbtype.fb_width =	softc->w;
	softc->gattr.fbtype.fb_size = softc->size;

	/* re-initialize overlay colormap (this is a hack!) */
	if (softc->flags & CG8_OVERLAY_CMAP)
	{
		if (cg8_update_pending(softc))
			cg8_int_disable(softc);
		INIT_OCMAP(softc->omap_rgb);
		cg8_int_enable(softc);
	}

	mutex_exit(&softc->softc_lock);
	DEBUGF(2, ("cgeight: cgeight_close: leaving (success)\n"));
	return (error);

error_out:
	DEBUGF(1, ("cgeight: cgeight_close: returning ERROR: %d\n",
							error));
	return (error);
}

/*ARGSUSED*/
static int
cgeight_mmap(
	dev_t	dev,
	off_t	off,
	int	prot)
{
	int			instance;
	struct cg8_softc	*softc;
	int			fbIndex;
	int			nbytes_1bit_plane;
	int			fbOff;

	instance = getminor(dev);
	softc	 = getsoftc(instance);

	DEBUGF(9, ("cgeight%d: cgeight_mmap: entering, offset: 0x%x\n",
					instance, (off_t)off));

	if ((u_int) off >= softc->size)
		return (-1);

	nbytes_1bit_plane = mpr_linebytes(softc->w, 1) * softc->h;
	fbIndex = off < nbytes_1bit_plane ? 0 :
		(off < 2*nbytes_1bit_plane ? 1 : 2);

	fbOff = off - fbIndex*nbytes_1bit_plane;

	DEBUGF(9, ("For offset %x (%x) mapped from fb %d\n",
		off, fbOff, fbIndex));

	return (hat_getkpfnum((caddr_t)softc->fb[fbIndex]) +
					ddi_btop(softc->mydip, fbOff));
}

/*ARGSUSED*/
static int
cgeight_ioctl(
	dev_t dev,
	int cmd,
	int arg,
	int mode,
	cred_t *cred_p,
	int *rval_p)
{
	int			instance;
	struct cg8_softc	*softc;
	int 			error = 0;
	int			(*copyin_func)();
	int			(*copyout_func)();
	static u_char		cmapbuf[CG8_CMAP_SIZE];

	instance = getminor(dev);
	softc	 = getsoftc(instance);

	DEBUGF(2, ("cgeight%d: cgeight_ioctl: dev %x cmd %x mode %x arg %x\n",
		instance, dev, cmd, mode, arg));

	/*
	 * Handle kernel-mode ioctl's issued by sunview:
	 */
#if NWIN > 0
#if defined(FBIO_KCOPY) || defined(FKIOCTL)
#ifdef FBIO_KCOPY
	copyin_func	= mode & FBIO_KCOPY ? kcopy : copyin;
	copyout_func = mode & FBIO_KCOPY ? kcopy : copyout;
#else /* FKIOCTL */
	copyin_func	= mode & FKIOCTL ? kcopy : copyin;
	copyout_func = mode & FKIOCTL ? kcopy : copyout;
#endif
#endif
#else
	copyin_func	= copyin;
	copyout_func = copyout;
#endif

	switch (cmd)
	{
	case FBIOPUTCMAP:
	case FBIOGETCMAP:
		{
			auto struct fbcmap	cmap;
			auto u_int		index;
			auto u_int		count;
			union fbunit		*map;
			int			*begin;
			int			*end;
			auto u_int		entries;
			u_int			intr_flag;
			u_char			tmp[3][CG8_CMAP_SIZE];
			int			i;

			/*
				Need to protect the whole mess
				here against the interrupt thread
				and any other ioctl thread
			*/
			mutex_enter(&softc->softc_lock);

			error = COPY(copyin_func, arg, &cmap,
				sizeof (struct fbcmap));
			if (error)
			{
				DEBUGF(1, ("cgeight_ioctl (FBIOGETCMAP): %s\n",
					"copyin_func failed"));
				mutex_exit(&softc->softc_lock);
				return (error);
			}

			index = (u_int) cmap.index;
			count = (u_int) cmap.count;

			switch (PIX_ATTRGROUP(index))
			{
			case 0:
			case PIXPG_24BIT_COLOR:
				begin	= &softc->cmap_begin;
				end	= &softc->cmap_end;
				map	= softc->cmap_rgb;
				intr_flag	= CG8_24BIT_CMAP;
				entries	= CG8_CMAP_SIZE;
				break;

			case PIXPG_OVERLAY:
				begin	= &softc->omap_begin;
				end	= &softc->omap_end;
				map	= softc->omap_rgb;
				intr_flag	= CG8_OVERLAY_CMAP;
				entries	= CG8_OMAP_SIZE;
				break;

			default:
				mutex_exit(&softc->softc_lock);
				return (EINVAL);
			}

			if ((index &= PIX_ALL_PLANES) >= entries ||
				index + count > entries)
			{
				mutex_exit(&softc->softc_lock);
				return (EINVAL);
			}

			if (count == 0)
			{
				mutex_exit(&softc->softc_lock);
				return (0);
			}

			/*
				we seem to need to deal with two cases here -
				if the call was not through cg8_putcolormap
				then the rgb arrays are in user address space.

				If it was, then they are in kernel address
				space.

				The whole mess could be looked into - there's
				at least one too many colormap copies here.
			*/
			if (cmd == FBIOPUTCMAP)
			{
				if (cg8_update_pending(softc))
					cg8_int_disable(softc);

				if (softc->cg8d.flags & CG8_KERNEL_UPDATE)
				{
					/*
						called from putcolormap
					*/
					kcopy((caddr_t)cmap.red,
						(caddr_t)tmp[0], count);
					kcopy((caddr_t)cmap.green,
						(caddr_t)tmp[1], count);
					kcopy((caddr_t)cmap.blue,
						(caddr_t)tmp[1], count);
					softc->cg8d.flags &= ~CG8_KERNEL_UPDATE;
				} else
				{
					/*
						called directly
					*/
					if (error =
						copyin((caddr_t)cmap.red,
						(caddr_t)tmp[0], count))
						goto copyin_error;

					if (error =
						copyin((caddr_t)cmap.green,
						(caddr_t)tmp[1], count))
						goto copyin_error;

					if (error =
						copyin((caddr_t)cmap.blue,
						(caddr_t)tmp[2], count))
						goto copyin_error;

copyin_error:
					if (error)
					{
						if (cg8_update_pending(softc))
							cg8_int_enable(softc);
						mutex_exit(&softc->softc_lock);
						return (EFAULT);
					}
				}
				if (!(cmap.index & PR_FORCE_UPDATE) &&
					intr_flag == CG8_24BIT_CMAP)
				{
					for (i = 0; i < count; i++, index++)
					{
						softc->em_red[index] =
							tmp[0][i];
						softc->em_green[index] =
							tmp[1][i];
						softc->em_blue[index] =
							tmp[2][i];
					}
					mutex_exit(&softc->softc_lock);
					return (0);
				}

				softc->cg8d.flags |= intr_flag;
				*begin = min(*begin, index);
				*end = max(*end, index + count);

				for (i = 0, map += index;
					count; i++, count--, map++)
					map->packed =
						tmp[0][i] |
						(tmp[1][i]<<8) |
							(tmp[2][i]<<16);

				DEBUGF(2, ("about to enable the interrupts\n"));
				softc->cg8d.flags |= CG8_UPDATE_PENDING;
				cg8_int_enable(softc);
			} else
			{
				/*
					FBIOGETCMAP
				*/
				if (error = copyout((caddr_t) cmapbuf,
					(caddr_t) cmap.red, count))
					goto copyout_error;
				if (error = copyout((caddr_t) cmapbuf,
					(caddr_t) cmap.green, count))
					goto copyout_error;
				if (error = copyout((caddr_t) cmapbuf,
					(caddr_t) cmap.blue, count))
					goto copyout_error;
copyout_error:
				if (error)
				{
					mutex_exit(&softc->softc_lock);
					return (EFAULT);
				}
			}
			mutex_exit(&softc->softc_lock);
			break;
		}

	case FBIOSATTR:
		{
			struct fbsattr sattr;

			DEBUGF(7, ("cgeight_ioctl: (FBIOSATTR) entering\n"));

#ifdef ONLY_OWNER_CAN_SATTR
			/* this can only happen for the owner */
			if (softc->owner != curproc)
			{
				DEBUGF(1, ("cgeight_ioctl (FBIOSATTR): %s\n",
					"ONLY_OWNER_CAN_SATTR"));
				return (ENOTTY);
			}
#endif

			error = COPY(copyin_func, arg, &sattr,
				sizeof (struct fbsattr));
			if (error)
			{
				DEBUGF(1, ("cgeight_ioctl (FBIOSATTR): %s\n",
					"copyin_func failed"));
				return (error);
			}

			softc->gattr.sattr.flags = sattr.flags;

			if (sattr.emu_type != -1)
				softc->gattr.sattr.emu_type = sattr.emu_type;

			if (sattr.flags & FB_ATTR_DEVSPECIFIC)
			{
				bcopy((char *)sattr.dev_specific,
					(char *)softc->gattr.sattr.dev_specific,
					sizeof (sattr.dev_specific));

				if (softc->gattr.sattr.dev_specific
					[FB_ATTR_CG8_SETOWNER_CMD] == 1)
				{
					register struct proc *newowner = 0;

					if (softc->gattr.sattr.dev_specific
					[FB_ATTR_CG8_SETOWNER_PID] > 0 &&
						(newowner = pfind(
						softc->gattr.sattr.dev_specific
						[FB_ATTR_CG8_SETOWNER_PID])))
					{
						softc->owner = newowner;
						softc->gattr.owner =
							newowner->p_pid;
					}
					softc->gattr.sattr.dev_specific
						[FB_ATTR_CG8_SETOWNER_CMD] = 0;
					softc->gattr.sattr.dev_specific
						[FB_ATTR_CG8_SETOWNER_PID] = 0;

					if (!newowner)
						return (ESRCH);
				}
			}
			break;
		}

	case FBIOGATTR:
		{
			auto struct fbgattr gattr;

			DEBUGF(7, ("cgeight_ioctl: (FBIOGATTR) entering\n"));

			/*
				Set owner if not owned or previous owner is dead
			*/
			if (softc->owner == NULL ||
				softc->owner->p_stat == NULL ||
				softc->owner->p_pid != softc->gattr.owner)
			{
				softc->owner = curproc;
				softc->gattr.owner = curproc->p_pid;
				softc->gattr.sattr.flags |= FB_ATTR_AUTOINIT;
			}

			gattr = softc->gattr;

			if (curproc == softc->owner)
				gattr.owner = 0;

			/*
				Copy the data back to the caller:
			*/
			error = COPY(copyout_func, &gattr, arg,
				sizeof (struct fbgattr));
			if (error)
				return (error);

			break;
		}

	case FBIOGTYPE:
		{
			auto struct fbtype fb;

			DEBUGF(7, ("cgeight_ioctl: (FBIOGTYPE) entering\n"));

			/*
				Set owner if not owned or previous owner is dead
			*/
			if (softc->owner == NULL ||
				softc->owner->p_stat == NULL ||
				softc->owner->p_pid != softc->gattr.owner)
			{
				softc->owner = curproc;
				softc->gattr.owner = curproc->p_pid;
				softc->gattr.sattr.flags |= FB_ATTR_AUTOINIT;
			}

			switch (softc->gattr.sattr.emu_type)
			{
			case FBTYPE_SUN2BW:
				fb = cg8typedefault;
				fb.fb_height	= softc->h;
				fb.fb_width	= softc->w;
				fb.fb_size	= softc->bw2size;
				break;

			default:
				fb = softc->gattr.fbtype;
				break;
			}

			/*
				Copy the data back to the caller:
			*/
			error = COPY(copyout_func, &fb, arg,
						sizeof (struct fbtype));
			if (error)
				return (error);

			break;
		}

#if NWIN > 0
	case FBIOGPIXRECT:
		{
			auto struct fbpixrect	fbpr;
			auto struct cg8fb	*fbp;
			auto struct cg8_fbdesc	*descp;
			auto int		i;

			DEBUGF(7, ("cgeight_ioctl: (FBIOGPIXRECT) entering\n"));

			if ((mode & FKIOCTL) != FKIOCTL)
			{
				DEBUGF(1, ("cgeight_ioctl: (FBIOGPIXRECT) %s\n",
					"called from user land???"));
				return (ENOTTY);
			}

			mutex_enter(&softc->pixrect_mutex);

			/* "Allocate" pixrect and private data */
			fbpr.fbpr_pixrect = &softc->pr;
			softc->pr.pr_data = (caddr_t) &softc->cg8d;

			/* initialize pixrect */
			softc->pr.pr_ops = &cg8_ops;
			softc->pr.pr_size.x = softc->w;
			softc->pr.pr_size.y = softc->h;

			/* initialize private data */
			softc->cg8d.flags = 0;
			softc->cg8d.planes = 0;
			softc->cg8d.fd = getminor(dev);

			for (fbp = softc->cg8d.fb, descp = cg8_fbdesc, i = 0;
				i < CG8_NFBS; fbp++, descp++, i++)
			{
				fbp->group = descp->group;
				fbp->depth = descp->depth;
				fbp->mprp.mpr.md_linebytes =
					mpr_linebytes(softc->w, descp->depth);
				fbp->mprp.mpr.md_image =
					(short *) softc->fb[i];
				fbp->mprp.mpr.md_offset.x = 0;
				fbp->mprp.mpr.md_offset.y = 0;
				fbp->mprp.mpr.md_primary = 0;
				fbp->mprp.mpr.md_flags =
					(descp->allplanes != 0)
						? MP_DISPLAY | MP_PLANEMASK
						: MP_DISPLAY;
				fbp->mprp.planes = descp->allplanes;
			}



			/*
				switch to each plane group,
				set up image pointers and
				initialize the images
			*/
			{
				int	initplanes;
				Pixrect	*tmppr = &softc->pr;

				initplanes =
					PIX_GROUP(
					cg8_fbdesc[CG8_FBINDEX_COLOR].group) |
					cg8_fbdesc[CG8_FBINDEX_COLOR].allplanes;

				(void) cg8_putattributes (tmppr, &initplanes);
				cg8_d(tmppr)-> fb[CG8_FBINDEX_COLOR].
					mprp.mpr.md_image =
					cg8_d(tmppr)->mprp.mpr.md_image =
					(short *)-1;
				cg8_d(tmppr)->mprp.mpr.md_linebytes = 0;

				initplanes =
					PIX_GROUP(
					cg8_fbdesc[CG8_FBINDEX_ENABLE].group) |
					cg8_fbdesc[CG8_FBINDEX_ENABLE].
					allplanes;
				(void) cg8_putattributes(tmppr, &initplanes);
				DEBUGF(1, ("ioctl GPIXRECT pr_depth = %d",
					tmppr->pr_depth));

				cg8_d(tmppr)->fb[CG8_FBINDEX_ENABLE].
					mprp.mpr.md_image =
					cg8_d(tmppr)->mprp.mpr.md_image =
					softc->enptr;
				pr_rop(&softc->pr, 0, 0,
					softc->pr.pr_size.x,
					softc->pr.pr_size.y,
					PIX_SRC | PIX_COLOR (1),
					(Pixrect *) 0, 0, 0);

				initplanes =
					PIX_GROUP(
					cg8_fbdesc[CG8_INITFB].group) |
					cg8_fbdesc[CG8_INITFB].allplanes;
				(void) cg8_putattributes(&softc->pr,
					&initplanes);
				DEBUGF(1, ("ioctl GPIXRECT pr_depth = %d\n",
					tmppr->pr_depth));

				cg8_d(tmppr)->
					fb[CG8_INITFB].mprp.mpr.md_image =
					cg8_d (tmppr)->mprp.mpr.md_image =
					softc->ovlptr;
				pr_rop (tmppr, 0, 0,
					softc->pr.pr_size.x,
					softc->pr.pr_size.y,
					PIX_SRC, (Pixrect *)0, 0, 0);

			}

			/* enable video */
			if (softc->p4reg)
			{
				*softc->p4reg = (*softc->p4reg & ~P4_REG_RESET)
					| P4_REG_VIDEO;
			} else
			{
				DEBUGF(1, ("cgeight_ioctl (FBIOGPIXRECT): %s\n",
					"ERROR: softc->p4reg was NULL"));
				mutex_exit(&softc->pixrect_mutex);
				return (EIO);
			}

			/*
				Copy the data back to the caller:
			*/
			error = COPY(kcopy, &fbpr, arg,
				sizeof (struct fbpixrect));
			if (error)
			{
				DEBUGF(1, ("cgeight_ioctl (FBIOGPIXRECT): %s\n",
					"kcopy failed"));

				mutex_exit(&softc->pixrect_mutex);
				return (error);
			}

			mutex_exit(&softc->pixrect_mutex);
			break;
		}
#endif NWIN > 0

	case FBIOSVIDEO:
		{
		auto int video_data;
		auto int video_on;

		DEBUGF(7, ("cgeight_ioctl: (FBIOSVIDEO) entering\n"));

		error = COPY(copyin_func, arg, &video_data,
						sizeof (int));
		if (error)
		{
			DEBUGF(1, ("cgeight_ioctl (FBIOSVIDEO): %s\n",
				"copyin_func failed"));

			return (error);
		}

		video_on = video_data & FBVIDEO_ON;

		mutex_enter(&softc->softc_lock);

		if (softc->p4reg)
		{
			if (video_on)
				*softc->p4reg = (*softc->p4reg & ~P4_REG_RESET)
					| P4_REG_VIDEO;
			else
				*softc->p4reg = (*softc->p4reg & ~P4_REG_RESET)
					& ~P4_REG_VIDEO;
		}
		else
		{
			DEBUGF(1, ("cgeight_ioctl (FBIOSVIDEO): %s\n",
				"ERROR: softc->p4reg was NULL"));
			mutex_exit(&softc->softc_lock);
			return (EIO);
		}

		mutex_exit(&softc->softc_lock);
		break;
		}

	case FBIOGVIDEO:
		{
			auto int video_data;

			DEBUGF(7, ("cgeight_ioctl: (FBIOGVIDEO) entering\n"));

			if (softc->p4reg)
			{
				video_data =
					(*softc->p4reg & P4_REG_VIDEO) ?
						FBVIDEO_ON : FBVIDEO_OFF;
			} else
			{
				DEBUGF(1, ("cgeight_ioctl (FBIOGVIDEO): %s\n",
					"ERROR: softc->p4reg was NULL"));
				return (EIO);
			}

			/*
				Copy the data back to the caller:
			*/
			error = COPY(copyout_func, &video_data, arg,
						sizeof (video_data));
			if (error)
			{
				DEBUGF(1, ("cgeight_ioctl (FBIOGVIDEO): %s\n",
						"copyout_func failed"));
				return (error);
			}

			break;
		}

	default:
		DEBUGF(1, ("cgeight: BAD IOCTL - cmd %d mode %d\n", cmd, mode));
		return (EINVAL);

	} /* switch(cmd) */

	DEBUGF(2, ("cgeight: cgeight_ioctl: returning success\n"));
	return (0);
}

/* ARGSUSED */
static int
cgeight_info(
	dev_info_t	*dip,
	ddi_info_cmd_t	infocmd,
	void		*arg,
	void		**result)
{
	dev_t			dev = (dev_t) arg;
	int			instance;
	struct cg8_softc	*softc;
	int			error = DDI_SUCCESS;

	DEBUGF(2, ("cgeight: cgeight_info: entering\n"));

	instance = getminor(dev);

	if (instance > instance_max)
		return (DDI_FAILURE);

	switch (infocmd)
	{

	case DDI_INFO_DEVT2DEVINFO:
		if ((softc = getsoftc(instance)) == NULL)
		{
			error = DDI_FAILURE;
			DEBUGF(1, (
				"cgeight: cgeight_info: %s \n",
				"DDI_INFO_DEVT2DEVINFO failed"));
		}
		else
		{
			*result = (void *) softc->mydip;
		}
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)instance;
		break;

	default:
		DEBUGF(1, ("cgeight: cgeight_info: %s 0x%x\n",
				"unknown infocmd:", infocmd));
		error = DDI_FAILURE;
	}
	return (error);
}

static u_int
cgeight_intr(caddr_t intr_arg)
{
	struct cg8_softc	*softc = (struct cg8_softc *) intr_arg;
	register int		i;
	register union fbunit	*lutptr;
	register union fbunit	*map;
	register int		shadowP4 = *softc->p4reg;

	DEBUGF(3, ("cgeight: cgeight_intr: entering - p4 %x at %x\n",
		shadowP4, softc->p4reg));

	cg8_int_disable(softc);

	/*
	 * Test to see if there is really work to do.
	 */
	if (!cg8_update_pending(softc))
	{
		DEBUGF(4, (
			"cgeight: cgeight_intr: spurious interrupt\n"));
		return (DDI_INTR_CLAIMED);
	}

	/*
		OK, things look good, load the color map(s):
		But don't touch the color maps while someone is updating
		them in the softc
	*/
	mutex_enter(&softc->softc_lock);

	softc->cg8d.flags &= ~CG8_UPDATE_PENDING;
	if (softc->cg8d.flags & CG8_OVERLAY_CMAP)
	{
		softc->cg8d.flags &= ~CG8_OVERLAY_CMAP;
		ASSIGN_LUT(softc->lut->addr_reg, softc->omap_begin);
		lutptr = &softc->lut->overlay;
		map = softc->omap_rgb+softc->omap_begin;
		for (i = softc->omap_begin; i < softc->omap_end; i++, map++)
		{
			lutptr->packed = map->packed;
			lutptr->packed = map->packed;
			lutptr->packed = map->packed;
		}
		softc->omap_begin = CG8_OMAP_SIZE;
		softc->omap_end = 0;
	}
	if (softc->cg8d.flags & CG8_24BIT_CMAP)
	{
		softc->cg8d.flags &= ~CG8_24BIT_CMAP;
		ASSIGN_LUT(softc->lut->addr_reg, softc->cmap_begin);
		lutptr = &softc->lut->lut_data;
		map = softc->cmap_rgb+softc->cmap_begin;
		for (i = softc->cmap_begin; i < softc->cmap_end; i++, map++)
		{
			lutptr->packed = map->packed;
			lutptr->packed = map->packed;
			lutptr->packed = map->packed;
		}
		softc->cmap_begin = CG8_CMAP_SIZE;
		softc->cmap_end = 0;
	}
	mutex_exit(&softc->softc_lock);

	return (DDI_INTR_CLAIMED);
}

static int
p4probe(
	dev_info_t *devi,
	caddr_t addr,
	int *w,
	int *h)
{
	static int mono_size[P4_ID_RESCODES][2] = {
		1600, 1280,
		1152,	900,
		1024, 1024,
		1280, 1024,
		1440, 1440,
		640, 480
	};

	static int color_size[P4_ID_RESCODES][2] = {
		1600, 1280,
		1152, 900,
		1024, 1024,
		1280, 1024,
		1440, 1440,
		1152, 900	/* 24-bit */
	};

	long *reg = (long *) addr;
	long id;
	int type;

	/*
		peek the P4 register, then try to modify the type code
	*/
	if (ddi_peekl(devi, reg, &id) != DDI_SUCCESS ||
		ddi_pokel(devi, reg,
			(long)((id &= ~P4_REG_RESET) ^ P4_ID_TYPE_MASK |
					P4_REG_VIDEO))
		!= DDI_SUCCESS)
	{
		return (-1);
	}

	/*
		if the "type code" changed, put the old value back and quit
	*/
	if ((*reg ^ id) & P4_ID_TYPE_MASK)
	{
		*reg = id;
		return (-1);
	}

	/*
		Except for cg8, the high nibble is the type
		and the low nibble is the resolution...
	*/
	switch (type = (id = (u_long) id >> 24) & P4_ID_MASK)
	{
	case P4_ID_FASTCOLOR:
		*w = color_size[5][0];	/* cgsix is 0x60 */
		*h = color_size[5][1];
		return (id);

	case P4_ID_COLOR8P1:
		if ((id &= ~P4_ID_MASK) < P4_ID_RESCODES)
		{
			*w = color_size[id][0];
			*h = color_size[id][1];
		}
		if (id < 5)
			return (type);
		return (type + id);

	case P4_ID_BW:
		if ((id &= ~P4_ID_MASK) < P4_ID_RESCODES)
		{
			*w = mono_size[id][0];
			*h = mono_size[id][1];
		}
		return (type);

	default:
		return (-1);
	}
}

int
_init(void)
{
	int retval;

	DEBUGF(2, ("cgeight: _init: entering\n"));
	retval = ddi_soft_state_init(&cg8_state,
				sizeof (struct cg8_softc), NCGEIGHT);
	if (retval != 0)
	{
		DEBUGF(1, (
			"cgeight: _init: ddi_soft_state_init FAILED\n"));
		return (retval);
	}

	retval = mod_install(&modlinkage);
	if (retval != 0)
	{
		DEBUGF(1, (
			"cgeight: _init: mod_install FAILED\n"));
		ddi_soft_state_fini(&cg8_state);
		return (retval);
	}

	DEBUGF(1, ("cgeight: _init: returning: success\n"));
	return (retval);
}


int
_fini(void)
{
	int			retval;

	DEBUGF(2, ("cgeight: _fini: entering\n"));

	if ((retval = mod_remove(&modlinkage)) != 0)
	{
		DEBUGF(1, (
			"cgeight: _fini: mod_remove FAILED\n"));
	return (retval);
	}

	ddi_soft_state_fini(&cg8_state);

	DEBUGF(1, ("cgeight: _fini: returning: success\n"));
	return (DDI_SUCCESS);
}


int
_info(struct modinfo *modinfop)
{
	DEBUGF(2, ("cgeight: _info: called\n"));

	return (mod_info(&modlinkage, modinfop));
}

static int
cgeight_identify(dev_info_t *devi)
{
	char	*name;

	name = ddi_get_name(devi);

	if (strcmp(name, "cgeight-p4") == 0)
	{
		DEBUGF(1, (
			"cgeight: cgeight_identify: CLAIMED (%s)\n", name));
		return (DDI_IDENTIFIED);
	}
	else
	{
		DEBUGF(1, (
			"cgeight: cgeight_identify: rejected (%s)\n", name));
		return (DDI_NOT_IDENTIFIED);
	}
}


struct cb_ops	cgeight_cb_ops = {
	cgeight_open,		/* open */
	cgeight_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	cgeight_ioctl,		/* ioctl */
	nodev,			/* devmap */
	cgeight_mmap,		/* mmap */
	ddi_segmap,		/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* prop_op */
	0,			/* streamtab	*/
	D_NEW			/* driver comaptibility flag */
};


struct dev_ops cgeight_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt */
	cgeight_info,		/* info */
	cgeight_identify,	/* identify */
	cgeight_probe,		/* probe */
	cgeight_attach,		/* attach */
	cgeight_detach,		/* detach */
	nodev,			/* reset */
	&cgeight_cb_ops,	/* driver operations */
	(struct bus_ops *)0	/* no bus operations */
};

extern struct mod_ops mod_driverops;
static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module. I'm a driver	*/
#ifdef	DEVELOPMENT_ONLY
	__TIME__,		/* module name & vers. for modinfo */
#else
	"color framebuffer cgeight-p4.c 1.13 'cgeight'",
				/* module name & vers. for modinfo */
#endif
	&cgeight_ops		/* address of ops struct for driver */
};

static struct modlinkage modlinkage = {
	MODREV_1,		/* rev. of loadable modules system	*/
	(void *)&modldrv,	/* address of the linkage structure */
	NULL			/* terminate list of linkage structs */
};

/* Macros to change a cg8 pixrect into a memory pixrect */

#define	PR_TO_MEM(src, mem)	\
	if (src && src->pr_ops != &mem_ops)	\
	{	\
	mem.pr_ops	= &mem_ops;	\
	mem.pr_size = src->pr_size;	\
	mem.pr_depth	= src->pr_depth;	\
	mem.pr_data = (char *) &cg8_d(src)->mprp;	\
	src	= &mem;	\
	}

/*
	cg8_rop
 */
static int cg8_rop(
	Pixrect*	dpr,
	int		dx,
	int		dy,
	int		w,
	int		h,
	int		op,
	Pixrect*	spr,
	int		sx,
	int		sy)
{
	Pixrect		dmempr; /* mem pixrect for dst. in. */
	Pixrect		smempr; /* mem pixrect for src in. */
	int		pr_ropRet;

	DEBUGF(4, ("cg8_rop entry\n"));

	PR_TO_MEM(dpr, dmempr);
	PR_TO_MEM(spr, smempr);
	pr_ropRet = pr_rop(dpr, dx, dy, w, h, op, spr, sx, sy);

	DEBUGF(4, ("cg8_rop exit\n"));
	return (pr_ropRet);
}

/*
	Here is the old sun view stuff ...
*/

#define	ERROR ENOTTY

static int
cg8_ioctl(
	Pixrect*	pr,
	int		cmd,
	caddr_t		data)
{
	static int	cmsize;

	DEBUGF(2, ("CG8_IOCTL ENTRY: cmd %x\n", cmd));

	switch (cmd) {

	case FBIOGPLNGRP:
		*(int *) data = PIX_ATTRGROUP(cg8_d(pr)->planes);
		break;

	case FBIOAVAILPLNGRP:
		{
			static int	cg8groups =
			MAKEPLNGRP(PIXPG_OVERLAY) |
			MAKEPLNGRP(PIXPG_OVERLAY_ENABLE) |
			MAKEPLNGRP(PIXPG_8BIT_COLOR);

			*(int *) data = cg8groups;
			break;
		}

	case FBIOGCMSIZE:
		if (cmsize < 0)
			return (-1);

		*(int *) data = cmsize;
		break;

	case FBIOSCMSIZE:
		cmsize = *(int *) data;
		break;

	default:
		return (ERROR);
	}
	DEBUGF(2, ("CG8_IOCTL EXIT: cmd %x\n", cmd));
	return (0);
}
