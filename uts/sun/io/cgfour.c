/*
 * Copyright 1986 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)cgfour.c	1.10	94/11/17 SMI"

/*
 *
 * Color frame buffer with overlay plane (cg4) driver.
 *
 * This port of cg4 to SVR4 supports only sun4 (cg4 type B).
 */

/*
 * Configuration Switches:
 */
#define	DEVELOPMENT_ONLY	/* always off for integrated code   */
#define	CG4DEBUG		/* inserts pokable debugging code   */

#if NWIN < 1
#undef NWIN
#define NWIN 1			/* force insertion of pixrect code  */
#endif	NWIN < 1

#define NCGFOUR	1		/*  all you can put on the P4 anyway  */


/*
 * Include files:
 */
#include <sys/param.h>
#include <sys/time.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/map.h>
#include <sys/mmu.h>
#include <sys/visual_io.h>
#include <sys/fbio.h>
#include <sys/p4reg.h>
#include <sys/pixrect.h>
#include <sys/pr_impl_util.h>
#include <sys/pr_planegroups.h>
#include <sys/memreg.h>
#include <sys/cg4reg.h>
#include <sys/cg4var.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/cmn_err.h>
#include <sys/open.h>
#include <sys/file.h>
#include <sys/modctl.h>
#include <sys/machsystm.h>

#ifdef	CG4DEBUG
extern	void	prom_printf(char *fmt, ...);
int cg4_debug = 0;
#define DEBUGF(level, args) \
			_STMT(if (cg4_debug >= (level)) prom_printf args;)
#else	CG4DEBUG
#define DEBUGF(level, args)	/* nothing */
#endif 	CG4DEBUG


#define	P4_ID_TYPE_MASK		0x7f000000
#define	P4_REG_RESET		0x01	/* should be in p4reg.h     */


extern u_char enablereg;		/* system enable register   */

static struct vis_identifier cg4_ident = { "SUNWcg4" };

/* driver per-unit data */
struct cg4_softc {
	kmutex_t	softc_lock;	/* protect softc            */
	short		flags;		/* internal flags           */
#define	CG4_UPDATE_PENDING	1	/* colormap update in prog. */
#define	CG4_OVERLAY_CMAP	2	/* has overlay colormap     */
#define	CG4_OWNER_WANTS_COLOR	4	/* auto-init should display */
					/* in color                 */
	caddr_t		fb[CG4_NFBS];	/* pointers to fb sections  */
	int		w, h;		/* resolution               */
	int		size;		/* total size of fb         */
	int		bw2size;	/* total size of overlay pl */
	struct proc	*owner;		/* owner of the fb          */
	struct fbgattr	gattr;		/* current attributes       */
	u_long		*p4reg;		/* pointer to P4 register   */
	ddi_iblock_cookie_t iblkc;	/* interrupt identifier     */
	dev_info_t	*mydip;		/* my devinfo pointer       */
	struct cg4b_cmap *cmap;		/* ptr to hardware colormap */
	union {				/* shadow overlay color map */
		u_char	omap_char[3][2];
	} omap_image;
#define	omap_red	omap_image.omap_char[0]
#define	omap_green	omap_image.omap_char[1]
#define	omap_blue	omap_image.omap_char[2]
#define	omap_rgb	omap_image.omap_char[0]
	u_short		cmap_index;	/* colormap update index    */
	u_short		cmap_count;	/* colormap update count    */
	union {				/* shadow color map         */
		u_long	cmap_long[CG4_CMAP_ENTRIES*3/sizeof(u_long)];
		u_char	cmap_char[3][CG4_CMAP_ENTRIES];
	} cmap_image;
#define	cmap_red	cmap_image.cmap_char[0]
#define	cmap_green	cmap_image.cmap_char[1]
#define	cmap_blue	cmap_image.cmap_char[2]
#define	cmap_rgb	cmap_image.cmap_char[0]

#if NWIN > 0
	kmutex_t	pixrect_mutex;	/* protect pixrect data     */
	Pixrect		pr;
	struct cg4_data	cgd;
#endif NWIN > 0
};

/* probe size -- enough for the P4 register + colormap/status reg. */
#define	CG4_PROBESIZE	(NBPG + sizeof *cg4_softc[0].cmap)

/* default structure for FBIOGTYPE ioctl */
static struct fbtype cg4typedefault =  {
/*	type           h  w  depth cms size */
	FBTYPE_SUN2BW, 0, 0, 1,    2,   0
};

/* default structure for FBIOGATTR ioctl */
static struct fbgattr cg4attrdefault =  {
/*	real_type         owner */
	FBTYPE_SUN4COLOR, 0,
/* fbtype: type             h  w  depth cms  size */
	{ FBTYPE_SUN4COLOR, 0, 0, 8,    256,  0 }, 
/* fbsattr: flags           emu_type       dev_specific */
	{ FB_ATTR_AUTOINIT, FBTYPE_SUN2BW, { 0 } },
/*        emu_types */
	{ FBTYPE_SUN4COLOR, FBTYPE_SUN2BW, -1, -1}
};

/* frame buffer description table */
static struct cg4_fbdesc {
	short	depth;			/* depth, bits */
	short	group;			/* plane group */
	int	allplanes;		/* initial plane mask */
} cg4_fbdesc[CG4_NFBS + 1] = {
	{ 1, PIXPG_OVERLAY,		  0 },
	{ 1, PIXPG_OVERLAY_ENABLE,	  0 },
	{ 8, PIXPG_8BIT_COLOR,		255 }
};
#define	CG4_FBINDEX_OVERLAY	0
#define	CG4_FBINDEX_ENABLE	1
#define	CG4_FBINDEX_COLOR	2

/* initial active frame buffer */
#ifndef	CG4_INITFB
#define	CG4_INITFB	CG4_FBINDEX_COLOR
#endif

extern  int copyin(), copyout(), kcopy();

/* 
 * Macros:
 */
#define getsoftc(instance) \
	((struct cg4_softc *)ddi_get_soft_state(cg4_state,(instance)))

#define ITEMSIN(array)  (sizeof (array) / sizeof (array)[0])
#define	ENDOF(array)	((array) + ITEMSIN(array))
#define BZERO(d,c)      bzero((caddr_t) (d), (u_int) (c))
#define COPY(f,s,d,c)   (f((caddr_t) (s), (caddr_t) (d), (u_int) (c)))
#define BCOPY(s,d,c)    COPY(bcopy,(s),(d),(c))
#define COPYIN(s,d,c)   COPY(copyin,(s),(d),(c))
#define COPYOUT(s,d,c)  COPY(copyout,(s),(d),(c))

/* enable/disable interrupt */
#define	cg4_int_enable(softc)	_STMT( \
	if ((softc)->p4reg) \
		*(softc)->p4reg |= P4_REG_INTEN | P4_REG_INTCLR; \
	setintrenable(1);)

#define	cg4_int_disable(softc)	_STMT( \
	setintrenable(0); \
	if ((softc)->p4reg) { \
		int x; \
		x = spl4(); \
		*(softc)->p4reg &= ~P4_REG_INTEN; \
		(void) splx(x); \
	} )

/* check if color map update is pending */
#define	cgfour_update_pending(softc)	\
		((softc)->flags & CG4_UPDATE_PENDING)

/* 
 * Compute color map update parameters: starting index and count.
 * If count is already nonzero, adjust values as necessary.
 * Zero count argument indicates overlay color map update desired
 * (may cause bus error if hardware doesn't have overlay color map).
 */
#ifdef lint
#define	cgfour_update_cmap(softc, index, count)	\
			(softc)->cmap_index = (index) + (count);
#else lint
#define	cgfour_update_cmap(/* struct cg4_softc */ softc, \
			/* u_int */ index, /* u_int */ count) \
	if (count) \
		if ((softc)->cmap_count) { \
			if ((index) + (count) > (u_int) \
				( (softc)->cmap_count += \
					(softc)->cmap_index) ) \
				(softc)->cmap_count = (index) + \
							(count); \
			if ((index) < (softc)->cmap_index) \
				(softc)->cmap_index = (index); \
			(softc)->cmap_count -= (softc)->cmap_index; \
		} \
		else { \
			(softc)->cmap_index = (index); \
			(softc)->cmap_count = (count); \
		} \
	(softc)->flags |= CG4_UPDATE_PENDING
#endif lint

static void	*cg4_state;  	/* opaque basket of softc structs   */
static int	instance_max;	/* highest instance number seen     */

/* forward references */
static struct modlinkage modlinkage;
static void cgfour_set_enable_plane();
static void cgfour_reset_cmap();
static void cgfour_cmap_bcopy();
static void cgfour_reset_b();
static u_int cgfour_intr();
static int cgfour_ioctl();
static int p4probe(dev_info_t *, caddr_t, int *, int *);
static int cg4_ioctl();
static int cg4_rop();

#if NWIN > 0

/* SunWindows specific stuff */

/* kernel pixrect ops vector */
struct pixrectops cg4_ops = {
	cg4_rop,
	cg4_putcolormap,
	cg4_putattributes,
#ifdef _PR_IOCTL_KERNEL_DEFINED
        cg4_ioctl
#endif
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
      

/*
 * replacement for pfind() which disappeared in 5.0
 */
extern struct proc *prfind();
extern kmutex_t pidlock;
struct proc *
pfind(pid_t pid)
{
	struct proc *proc_p;

	mutex_enter(&pidlock);
	proc_p = prfind(pid);
	mutex_exit(&pidlock);

	return proc_p;
}


/*
 * From cg4_colormap.c
 * Always returns zero.
 */
int
cg4_putattributes(
	Pixrect *pr,
	int	*planesp)
{
	struct cg4_data *cgd;
	int		planes;
	int		group;

	if (planesp == 0)
		return 0;

	planes = *planesp;
        /*
         * for those call putattributes with ~0.  This can be better
         * handled if the check for PIX_DONT_SET_PLANE is inverted.   
         */
        if (planes == ~0)
                planes &= ~PIX_DONT_SET_PLANES; 
	cgd = cg4_d(pr);

	/*
	 * If user is trying to set the group, look for the frame
	 * buffer which implements the desired group and make it
	 * the active frame buffer.
	 */
	group = PIX_ATTRGROUP(planes);
	if (group == PIX_GROUP_MASK)
		planes &= PIX_ALL_PLANES;
	if (group != PIXPG_CURRENT || group == PIXPG_INVALID)
	{
		int active;
		struct cg4fb *fbp;
		int	cmsize;

		for (active = 0, fbp = cgd->fb; 
			active < CG4_NFBS; active++, fbp++)
			if (group == fbp->group) {
				cgd->planes = PIX_GROUP(group);
				cgd->active = active;

				if (!(planes & PIX_DONT_SET_PLANES))
					fbp->mprp.planes = 
					    planes & PIX_ALL_PLANES;

				cgd->mprp = fbp->mprp;
				cgd->planes |= fbp->mprp.planes;
				pr->pr_depth = fbp->depth;
				cmsize = (pr->pr_depth==8) ? 256 : 2;
				(void) cg4_ioctl(pr, FBIOSCMSIZE,
					(caddr_t)&cmsize);
				return 0;
			}
	}

	/* set planes for current group */
	if (!(planes & PIX_DONT_SET_PLANES)) 
	{
		planes &= PIX_ALL_PLANES;
		cgd->planes = 
			(cgd->planes & ~PIX_ALL_PLANES) | planes;
		cgd->mprp.planes = planes;
		cgd->fb[cgd->active].mprp.planes = planes;
	}

	return 0;
}

int
cg4_putcolormap(
	Pixrect		*pr,
	int		index,
	int		count,
	unsigned char	*red,
	unsigned char	*green,
	unsigned char	*blue)
{
	register struct cg4_softc *softc = getsoftc(cg4_d(pr)->fd);
	register u_int rindex = (u_int) index;
	register u_int rcount = (u_int) count;
	register u_char *map;
	register u_int entries;

	switch (softc->cgd.active) {
	case CG4_FBINDEX_COLOR:
		map = softc->cmap_rgb;
		entries = CG4_CMAP_ENTRIES;
		break;

	case CG4_FBINDEX_OVERLAY:
		if (softc->flags & CG4_OVERLAY_CMAP) {
			map = softc->omap_rgb;
			entries = 2;
			break;
		}
		/* fall through */

	default:
		if (mem_putcolormap(&softc->pr, index, count, 
			red, green, blue))
			return PIX_ERR;

		softc->cgd.fb[softc->cgd.active].mprp.mpr.md_flags =
			softc->cgd.mprp.mpr.md_flags;

		return 0;
	}

	/* check arguments */
	if (rindex >= entries || rindex + rcount > entries) 
		return PIX_ERR;

	if (rcount == 0)
		return 0;

	/* lock out updates of the hardware colormap XXX race?*/
	if (cgfour_update_pending(softc))
		cg4_int_disable(softc);

	map += rindex * 3;
	cgfour_cmap_bcopy(red,   map++, rcount);
	cgfour_cmap_bcopy(green, map++, rcount);
	cgfour_cmap_bcopy(blue,  map,   rcount);

	/* overlay colormap update */
	if (entries <= 2)
		rcount = 0;

	cgfour_update_cmap(softc, rindex, rcount);

	/* enable interrupt so we can load the hardware colormap */
	cg4_int_enable(softc);

	return 0;
}

#endif	NWIN > 0

/*
 * Determine if a cgfour exists at the given address.
 */
static int
cgfour_probe(dev_info_t *devi)
{
	struct cg4_softc	*softc;
	int			instance;
	int			p4_id;
	int			npages_1bit_plane;
	int			npages_8bit_plane;

	DEBUGF(9, ( "cgfour: cgfour_probe: entering\n"));

	instance = ddi_get_instance(devi);
	DEBUGF(9, ( "cgfour_probe: instance: %d\n", instance));
	if (instance > instance_max)
	{
		instance_max = instance;
	}

	if (ddi_soft_state_zalloc(cg4_state, instance) != 0)
	{
		DEBUGF(1, ( "cgfour_probe: ddi_soft_state_zalloc FAILED\n"));
		return DDI_PROBE_FAILURE;
	}

	softc = getsoftc(instance);
	softc->mydip = devi;
	ddi_set_driver_private(devi, (caddr_t)softc);
	DEBUGF( 2, ( "cg4 softc at %x\n", softc ));

	/*
	 * Map the P4 register so that we can get the resolution.
	 */
	if (ddi_map_regs(devi, CG4B_REGNUM_P4_PROBE_ID,
		(caddr_t *)&softc->p4reg,
		(off_t)0, sizeof(*softc->p4reg)) !=0)
	{
		DEBUGF(1, ( "cgfour_probe: ddi_map_regs (p4reg) FAILED\n"));
		goto probe_failure;
	}

	/* 
	 * Determine frame buffer resolution: use the P4 type code.
	 */
	p4_id = p4probe(devi, (caddr_t)softc->p4reg, &softc->w, &softc->h);
	if (p4_id < 0) 
	{
		DEBUGF(1, ( "cgfour_probe: p4probe FAILED\n"));
		goto probe_failure;
	}

	if (p4_id != P4_ID_COLOR8P1)
	{
		DEBUGF(1, ( "cgfour_probe: (p4_id != P4_ID_COLOR8P1)\n"));
		goto probe_failure;
	}

	npages_1bit_plane =
		btoc(mpr_linebytes(softc->w, 1) * softc->h);
	npages_8bit_plane =
		btoc(mpr_linebytes(softc->w, 8) * softc->h);

	softc->bw2size	= ctob(npages_1bit_plane);
	softc->size	= ctob(npages_1bit_plane + 
			      npages_1bit_plane + npages_8bit_plane);

	/*
	 * Map the device registers:
	 */
	if (ddi_map_regs(devi, CG4B_REGNUM_CMAP, 
		(caddr_t *)&softc->cmap, 
		(off_t)0, sizeof(*softc->cmap)) !=0)
	{
		DEBUGF(1, ( "cgfour_probe: ddi_map_regs (cmap) FAILED\n"));
		goto probe_failure;
	}

	if (ddi_map_regs(devi, CG4B_REGNUM_OVERLAY, 
		(caddr_t *)&softc->fb[CG4_FBINDEX_OVERLAY],
		(off_t)0, ctob(npages_1bit_plane) ) !=0)
	{
		DEBUGF(1, ( "cgfour_probe: ddi_map_regs (OVERLAY) FAILED\n"));
		goto probe_failure;
	}

	if (ddi_map_regs(devi, CG4B_REGNUM_ENABLE, 
		(caddr_t *)&softc->fb[CG4_FBINDEX_ENABLE],
		(off_t)0, ctob(npages_1bit_plane) ) !=0)
	{
		DEBUGF(1, ( "cgfour_probe: ddi_map_regs (ENABLE) FAILED\n"));
		goto probe_failure;
	}

	if (ddi_map_regs(devi, CG4B_REGNUM_COLOR, 
		(caddr_t *)&softc->fb[CG4_FBINDEX_COLOR],
		(off_t)0, ctob(npages_8bit_plane) ) !=0)
	{
		DEBUGF(1, ( "cgfour_probe: ddi_map_regs (COLOR) FAILED\n"));
		goto probe_failure;
	}

	DEBUGF(9, ( "cgfour_probe: returning DDI_SUCCESS\n"));
	return DDI_SUCCESS;

probe_failure:

	/*
	 * Unmap everything that we mapped:
	 */
	if (softc->p4reg)
	{
		ddi_unmap_regs(devi, CG4B_REGNUM_P4_PROBE_ID,
			(caddr_t *)&softc->p4reg,
			(off_t)0, sizeof(*softc->p4reg));
	}

	if (softc->cmap)
	{
		ddi_unmap_regs(devi, CG4B_REGNUM_CMAP,
			(caddr_t *)&softc->cmap,
			(off_t)0, sizeof(*softc->cmap));
	}

	if (softc->fb[CG4_FBINDEX_OVERLAY])
	{
		ddi_unmap_regs(devi, CG4B_REGNUM_OVERLAY, 
			(caddr_t *)&softc->fb[CG4_FBINDEX_OVERLAY],
			(off_t)0, ctob(npages_1bit_plane) );
	}

	if (softc->fb[CG4_FBINDEX_ENABLE])
	{
		ddi_unmap_regs(devi, CG4B_REGNUM_ENABLE, 
			(caddr_t *)&softc->fb[CG4_FBINDEX_ENABLE],
			(off_t)0, ctob(npages_1bit_plane) );
	}

	if (softc->fb[CG4_FBINDEX_COLOR])
	{
		ddi_unmap_regs(devi, CG4B_REGNUM_COLOR, 
			(caddr_t *)&softc->fb[CG4_FBINDEX_COLOR],
			(off_t)0, ctob(npages_8bit_plane) );
	}

	DEBUGF(1, ( "cgfour_probe: returning DDI_PROBE_FAILURE\n"));
	return DDI_PROBE_FAILURE;
}

/*ARGSUSED*/
static int
cgfour_detach(dev_info_t *devi, ddi_detach_cmd_t detach_cmd)
{
	return DDI_FAILURE;		/* not unloadable */
}

static int
cgfour_attach(dev_info_t *devi, ddi_attach_cmd_t attach_cmd)
{
	int			instance;
	int			retval;
	struct cg4_softc	*softc;
	char			name[16];

	instance = ddi_get_instance(devi);
	softc	 = getsoftc(instance);

	DEBUGF(9, ( "cgfour%d: cgfour_attach: entering\n", instance));

	if (attach_cmd != DDI_ATTACH)
	{
		DEBUGF(1, ( "cgfour: attach: (attach_cmd != DDI_ATTACH)\n"));
		return (DDI_FAILURE);
	}

	softc->flags = 0;
	softc->owner = NULL;

 	softc->gattr = cg4attrdefault;
	softc->gattr.fbtype.fb_height = softc->h;
	softc->gattr.fbtype.fb_width =  softc->w;
	softc->gattr.fbtype.fb_size = softc->size;

	/* 
	 * Initialize hardware colormap and software colormap images.
	 * It might make sense to read the hardware colormap here. 
	 */
	cgfour_reset_b(softc);
	softc->flags = CG4_OVERLAY_CMAP;
	cgfour_reset_cmap(softc, softc->omap_rgb, 2);
	cgfour_reset_cmap(softc, softc->cmap_rgb, CG4_CMAP_ENTRIES);
	cgfour_update_cmap(softc, 0, CG4_CMAP_ENTRIES);

	/*
	 * Register the interrupt handler.
	 */
	retval = ddi_add_intr(devi, (u_int)0, &softc->iblkc,
			(ddi_idevice_cookie_t *)0, cgfour_intr,
						(caddr_t)softc);
	if (retval != DDI_SUCCESS)
	{
		DEBUGF(1, ( "cgfour: attach: ddi_add_intr FAILED\n"));
		return (DDI_FAILURE);
	}

	/*
	 * Create and initialize some mutexen.
	 */
        mutex_init(&softc->softc_lock,
		"cgfour softc", MUTEX_DRIVER, softc->iblkc);
	mutex_init(&softc->pixrect_mutex, 
		"cgfour pixrect", MUTEX_DRIVER, softc->iblkc);
								     
	/*
	 * Create the minor device entries:
	 */
	sprintf(name, "cgfour%d", instance);
	if (ddi_create_minor_node(devi, name, S_IFCHR, instance, 
				DDI_NT_DISPLAY, 0) == DDI_FAILURE)
	{
		DEBUGF(1, ( "cgfour: attach: ddi_create_minor_node FAILED\n"));
		ddi_remove_minor_node(devi, NULL);
		return (DDI_FAILURE);
	}

	ddi_report_dev(devi);
	
	cmn_err( CE_CONT, "!%s%d: resolution %d x %d\n",
		ddi_get_name(devi), instance, softc->w, softc->h);
	
	DEBUGF(9, ( "cgfour%d: cgfour_attach: returning DDI_SUCCESS \n",
							instance));
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
cgfour_open(
	dev_t	*dev_p,
	int	flag,
	int	otyp,
	cred_t	*cr)
{
	int			instance;
	struct cg4_softc	*softc;
	int 			error = 0;

	instance = getminor(*dev_p);
	softc	 = getsoftc(instance);

	DEBUGF(9, ( "cgfour%d: cgfour_open: entering\n", instance));

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

	DEBUGF(9, ( "cgfour: cgfour_open: leaving (success)\n"));
	return error;

error_out:
	DEBUGF(1, ( "cgfour: cgfour_open: returning ERROR: %d\n", error));
	return error;
}
 

/*ARGSUSED*/
static int
cgfour_close(
	dev_t		dev,
	int		flag,
	int		otyp,
	struct cred	*cred)
{
	int			instance;
	struct cg4_softc	*softc;
	int 			error = 0;

	instance = getminor(dev);
	softc	 = getsoftc(instance);

	DEBUGF(9, ( "cgfour%d: cgfour_close: entering\n", instance));

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

	softc->flags &= (CG4_UPDATE_PENDING | CG4_OVERLAY_CMAP);
	softc->owner = NULL;

 	softc->gattr = cg4attrdefault;
	softc->gattr.fbtype.fb_height = softc->h;
	softc->gattr.fbtype.fb_width =  softc->w;
	softc->gattr.fbtype.fb_size = softc->size;

	/* re-initialize overlay colormap (this is a hack!) */
	if (softc->flags & CG4_OVERLAY_CMAP)
	{
		if (cgfour_update_pending(softc))
			cg4_int_disable(softc);
		cgfour_reset_cmap(softc, softc->omap_rgb, 2);
		cgfour_update_cmap(softc, 0, 0);
		cg4_int_enable(softc);
	}

	mutex_exit(&softc->softc_lock);
	DEBUGF(9, ( "cgfour: cgfour_close: leaving (success)\n"));
	return error;

error_out:
	DEBUGF(1, ( "cgfour: cgfour_close: returning ERROR: %d\n",
							error));
	return error;
}

/*ARGSUSED*/
static int
cgfour_mmap(
	dev_t	dev,
	off_t	off,
	int	prot)
{
	int			instance;
	struct cg4_softc	*softc;
	int			fbIndex;
	int			nbytes_1bit_plane;
	int			fbOff;

	instance = getminor(dev);
	softc	 = getsoftc(instance);

	DEBUGF(9, ( "cgfour%d: cgfour_mmap: entering, offset: 0x%x\n",
					instance, (off_t)off));

	/* 
	 * Initialize overlay enable plane if necessary.
	 *
	 * If the owner wants color (as inferred from its use of the
	 * FBIOGATTR ioctl), set to display color planes.
	 * (This is a hack.)
	 *
	 * Note this will work for a non-owner process as long as
	 * the autoinit bit is set.
	 */
	if (softc->gattr.sattr.flags & FB_ATTR_AUTOINIT) 
	{
		cgfour_set_enable_plane(softc->fb[CG4_FBINDEX_ENABLE],
			softc->w, softc->h,
			(softc->flags&CG4_OWNER_WANTS_COLOR) ? 0 : 1);

		softc->gattr.sattr.flags &= ~FB_ATTR_AUTOINIT;
	}

	if ((u_int) off >= softc->size)
		return -1;

	nbytes_1bit_plane = mpr_linebytes(softc->w, 1) * softc->h;
	fbIndex = off < nbytes_1bit_plane ? 0 : 
		( off < 2*nbytes_1bit_plane ? 1 : 2 );

	fbOff = off - fbIndex*nbytes_1bit_plane;

	DEBUGF( 9, ( "For offset %x (%x) mapped from fb %d\n", off, fbOff, fbIndex ) );

	return (hat_getkpfnum((caddr_t)softc->fb[fbIndex]) + 
					ddi_btop(softc->mydip, fbOff) );
}

/*ARGSUSED*/
static int
cgfour_ioctl(
	dev_t dev,
	int cmd,
	int arg,
	int mode,
	cred_t *cred_p,
	int *rval_p)
{
	int			instance;
	struct cg4_softc	*softc;
	int 			error = 0;
	int			(*copyin_func)();
	int			(*copyout_func)();
	static u_char		cmapbuf[CG4_CMAP_ENTRIES];

	instance = getminor(dev);
	softc	 = getsoftc(instance);

	DEBUGF( 2, ( "cgfour%d: cgfour_ioctl: entering cmd %d mode %d\n", 
		instance, cmd, mode ) );

	/*
	 * Handle kernel-mode ioctl's issued by sunview:
	 */
#if NWIN > 0
#if defined(FBIO_KCOPY) || defined(FKIOCTL)
#ifdef FBIO_KCOPY
	copyin_func  = mode & FBIO_KCOPY ? kcopy : copyin;
	copyout_func = mode & FBIO_KCOPY ? kcopy : copyout;
#else /* FKIOCTL */
	copyin_func  = mode & FKIOCTL ? kcopy : copyin;
	copyout_func = mode & FKIOCTL ? kcopy : copyout;
#endif
#else
	copyin_func  = fbio_kcopy ? kcopy : copyin;
	copyout_func = fbio_kcopy ? kcopy : copyout;
	fbio_kcopy = 0;
#endif
#else
	copyin_func  = copyin;
	copyout_func = copyout;
#endif

	switch (cmd)
	{

	case VIS_GETIDENTIFIER:
		if (COPY(copyout_func, &cg4_ident, arg,
		    sizeof (struct vis_identifier)) == -1)
			return (EFAULT);
		break;

	case FBIOPUTCMAP: 
	case FBIOGETCMAP:
	    {
		auto struct fbcmap	cmap;
		auto u_int		index;
		auto u_int		count;
		auto u_char		*map;
		auto u_int		entries;

		error = COPY(copyin_func, arg, &cmap,
					sizeof(struct fbcmap));
		if (error)
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOGETCMAP): %s\n",
				"copyin_func failed"));

			return error;
		}

		index = (u_int) cmap.index;
		count = (u_int) cmap.count;

		switch (PIX_ATTRGROUP(index)) 
		{
		case 0:
		case PIXPG_8BIT_COLOR:
			map = softc->cmap_rgb;
			entries = CG4_CMAP_ENTRIES;
			break;

		case PIXPG_OVERLAY:
			if (softc->flags & CG4_OVERLAY_CMAP) 
			{
				map = softc->omap_rgb;
				entries = 2;
				break;
			}
			/* fall through */

		default:
			return EINVAL;
		}

		if ((index &= PIX_ALL_PLANES) >= entries || 
			index + count > entries)
			return EINVAL;

		if (count == 0)
			return 0;

		if (cmd == FBIOPUTCMAP) 
		{ 
			if (cgfour_update_pending(softc))
				cg4_int_disable(softc);

			map += index * 3;

			if (error = copyin((caddr_t) cmap.red, 
				(caddr_t) cmapbuf, count))
			{
				DEBUGF(1, ( "cgfour_ioctl (FBIOPUTCMAP): %s\n",
				   "copyin (red) failed"));

				goto copyin_error;
			}

			cgfour_cmap_bcopy(cmapbuf, map++, count);

			if (error = copyin((caddr_t) cmap.green, 
				(caddr_t) cmapbuf, count))
			{
				DEBUGF(1, ( "cgfour_ioctl (FBIOPUTCMAP): %s\n",
				   "copyin (green) failed"));

				goto copyin_error;
			}

			cgfour_cmap_bcopy(cmapbuf, map++, count);

			if (error = copyin((caddr_t) cmap.blue, 
				(caddr_t) cmapbuf, count))
			{
				DEBUGF(1, ( "cgfour_ioctl (FBIOPUTCMAP): %s\n",
				   "copyin (blue) failed"));

				goto copyin_error;
			}

			cgfour_cmap_bcopy(cmapbuf, map, count);

copyin_error:
			if (error)
			{
				if (cgfour_update_pending(softc)) 
					cg4_int_enable(softc);
				return EFAULT;
			}

			/* overlay colormap update */
			if (entries <= 2)
				count = 0;

			cgfour_update_cmap(softc, index, count);
			cg4_int_enable(softc);
		}
		else 
		{	/* FBIOGETCMAP */
			map += index * 3;

			cgfour_cmap_bcopy(cmapbuf, map++, -count);
			if (copyout((caddr_t) cmapbuf,
				(caddr_t) cmap.red, count))
			{
				DEBUGF(1, ( "cgfour_ioctl (FBIOGETCMAP): %s\n",
				   "copyout (red) failed"));

				goto copyout_error;
			}

			cgfour_cmap_bcopy(cmapbuf, map++, -count);
			if (copyout((caddr_t) cmapbuf,
				(caddr_t) cmap.green, count))
			{
				DEBUGF(1, ( "cgfour_ioctl (FBIOGETCMAP): %s\n",
				   "copyout (green) failed"));

				goto copyout_error;
			}

			cgfour_cmap_bcopy(cmapbuf, map, -count);
			if (copyout((caddr_t) cmapbuf,
				(caddr_t) cmap.blue, count))
			{
				DEBUGF(1, ( "cgfour_ioctl (FBIOGETCMAP): %s\n",
				   "copyout (blue) failed"));

				goto copyout_error;
			}

copyout_error:
			if (error)
			{
				return EFAULT;
			}
		}
		break;
	    }

	case FBIOSATTR:
	    {
		struct fbsattr sattr;

		DEBUGF(7, ( "cgfour_ioctl: (FBIOSATTR) entering\n"));

#ifdef ONLY_OWNER_CAN_SATTR
		/* this can only happen for the owner */
		if (softc->owner != curproc)
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOSATTR): %s\n",
				"ONLY_OWNER_CAN_SATTR"));
			return ENOTTY;
		}
#endif ONLY_OWNER_CAN_SATTR

		error = COPY(copyin_func, arg, &sattr,
					sizeof (struct fbsattr));
		if (error)
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOSATTR): %s\n",
				"copyin_func failed"));

			return error;
		}

		softc->gattr.sattr.flags = sattr.flags;

		if (sattr.emu_type != -1)
			softc->gattr.sattr.emu_type = sattr.emu_type;

		if (sattr.flags & FB_ATTR_DEVSPECIFIC) 
		{
			bcopy((char *) sattr.dev_specific,
			    (char *) softc->gattr.sattr.dev_specific,
				sizeof sattr.dev_specific);

			if (softc->gattr.sattr.dev_specific
				[FB_ATTR_CG4_SETOWNER_CMD] == 1) 
			{
				register struct proc *newowner = 0;

				if (softc->gattr.sattr.dev_specific
				    [FB_ATTR_CG4_SETOWNER_PID] > 0 &&
				    (newowner = pfind(
				    softc->gattr.sattr.dev_specific
					[FB_ATTR_CG4_SETOWNER_PID])))
				{
					softc->owner = newowner;
					softc->gattr.owner =
						newowner->p_pid;
				}

				softc->gattr.sattr.dev_specific
				    [FB_ATTR_CG4_SETOWNER_CMD] = 0;
				softc->gattr.sattr.dev_specific
				    [FB_ATTR_CG4_SETOWNER_PID] = 0;

				if (!newowner)
				{
				    DEBUGF(1, ( "cgfour_ioctl (FBIOSATTR): %s\n",
					"FAILED: (!newowner)"));
				    return ESRCH;
				}
			}
		}
		break;
	    }

	case FBIOGATTR:
	    {
		auto struct fbgattr gattr;

		DEBUGF(7, ( "cgfour_ioctl: (FBIOGATTR) entering\n"));

		/* 
		 * Set owner if not owned or previous owner is dead 
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
		{
			gattr.owner = 0;
			softc->flags |= CG4_OWNER_WANTS_COLOR;
		}

		/*
		 * Copy the data back to the caller:
		 */
		error = COPY(copyout_func, &gattr, arg,
					sizeof(struct fbgattr));
		if (error)
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOGATTR) failed: err: %d\n",
							error));
			return(error);
		}

		break;
	    }

	case FBIOGTYPE:
	    {
		auto struct fbtype fb;

		DEBUGF(7, ( "cgfour_ioctl: (FBIOGTYPE) entering\n"));

		/* 
		 * Set owner if not owned or previous owner is dead 
		 */
		if (softc->owner == NULL || 
			softc->owner->p_stat == NULL || 
			softc->owner->p_pid != softc->gattr.owner)
		{
			softc->owner = curproc;
			softc->gattr.owner = curproc->p_pid;
			softc->gattr.sattr.flags |= FB_ATTR_AUTOINIT;
		}

		switch(softc->gattr.sattr.emu_type)
		{
		case FBTYPE_SUN2BW:
			fb = cg4typedefault;
			fb.fb_height	= softc->h;
			fb.fb_width	= softc->w;
			fb.fb_size	= softc->bw2size;
			break;

		case FBTYPE_SUN4COLOR:
		default:
			fb = softc->gattr.fbtype;
			break;
		}
		
		/*
		 * Copy the data back to the caller:
		 */
		error = COPY(copyout_func, &fb, arg,
					sizeof(struct fbtype));
		if (error)
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOGTYPE): %s\n",
					"copyout_func failed"));
			return(error);
		}

		break;
	    }

#if NWIN > 0
	case FBIOGPIXRECT: 
	    {
		auto struct fbpixrect	fbpr;
		auto struct cg4fb	*fbp;
		auto struct cg4_fbdesc	*descp;
		auto int		i;
		auto int		initplanes;

		DEBUGF(7, ( "cgfour_ioctl: (FBIOGPIXRECT) entering\n"));

		if ((mode & FKIOCTL) != FKIOCTL)
		{
			DEBUGF(1, ( "cgfour_ioctl: (FBIOGPIXRECT) %s\n",
				"called from user land???"));
			return ENOTTY;
		}

		mutex_enter(&softc->pixrect_mutex);

		/* "Allocate" pixrect and private data */
		fbpr.fbpr_pixrect = &softc->pr;
		softc->pr.pr_data = (caddr_t) &softc->cgd;

		/* initialize pixrect */
		softc->pr.pr_ops = &cg4_ops;
		softc->pr.pr_size.x = softc->w;
		softc->pr.pr_size.y = softc->h;

		/* initialize private data */
		softc->cgd.flags = 0;
		softc->cgd.planes = 0;
		softc->cgd.fd = getminor(dev);

		for (fbp = softc->cgd.fb, descp = cg4_fbdesc, i = 0; 
			i < CG4_NFBS; fbp++, descp++, i++)
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

		/* set up pixrect initial state */
		initplanes = PIX_GROUP(cg4_fbdesc[CG4_INITFB].group) |
				cg4_fbdesc[CG4_INITFB].allplanes;

		(void) cg4_putattributes(&softc->pr, &initplanes);

		/* enable video */
		if (softc->p4reg)
		{
			*softc->p4reg |= P4_REG_VIDEO;
		}
		else
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOGPIXRECT): %s\n",
				"ERROR: softc->p4reg was NULL"));
			mutex_exit(&softc->pixrect_mutex);
			return EIO;
		}

		/*
		 * Copy the data back to the caller:
		 */
		error = COPY(kcopy, &fbpr, arg, 
					sizeof (struct fbpixrect));
		if (error)
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOGPIXRECT): %s\n",
				"kcopy failed"));

			mutex_exit(&softc->pixrect_mutex);
			return error;
		}

		mutex_exit(&softc->pixrect_mutex);
		break;
	    }
#endif NWIN > 0

	case FBIOSVIDEO:
	    {
		auto int video_data;
		auto int video_on;

		DEBUGF(7, ( "cgfour_ioctl: (FBIOSVIDEO) entering\n"));

		error = COPY(copyin_func, arg, &video_data,
						sizeof (int));
		if (error)
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOSVIDEO): %s\n",
				"copyin_func failed"));

			return error;
		}

		video_on = video_data & FBVIDEO_ON;

		mutex_enter(&softc->softc_lock);

		if (softc->p4reg)
		{
			*softc->p4reg = *softc->p4reg & 
				~(P4_REG_INTCLR | P4_REG_VIDEO) |
				(video_on ? P4_REG_VIDEO : 0);
		}
		else
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOSVIDEO): %s\n",
				"ERROR: softc->p4reg was NULL"));
			mutex_exit(&softc->softc_lock);
			return EIO;
		}

		mutex_exit(&softc->softc_lock);
		break;
	    }

	case FBIOGVIDEO:
	    {
		auto int video_data;

		DEBUGF(7, ( "cgfour_ioctl: (FBIOGVIDEO) entering\n"));

		if (softc->p4reg)
		{
			video_data = 
				(*softc->p4reg & P4_REG_VIDEO) ? 
					FBVIDEO_ON : FBVIDEO_OFF;
		}
		else
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOGVIDEO): %s\n",
				"ERROR: softc->p4reg was NULL"));
			return EIO;
		}

		/*
		 * Copy the data back to the caller:
		 */
		error = COPY(copyout_func, &video_data, arg,
					sizeof(video_data));
		if (error)
		{
			DEBUGF(1, ( "cgfour_ioctl (FBIOGVIDEO): %s\n",
					"copyout_func failed"));
			return error;
		}

		break;
	    }

	default:
		DEBUGF( 1, ("cgfour: BAD IOCTL - cmd %d mode %d\n", cmd, mode ));
		return EINVAL;

	} /* switch(cmd) */

	DEBUGF(9, ( "cgfour: cgfour_ioctl: returning success\n"));
	return 0;
}

/* ARGSUSED */
static int
cgfour_info(
	dev_info_t	*dip,
	ddi_info_cmd_t	infocmd,
	void		*arg,
	void		**result)
{
	dev_t			dev = (dev_t) arg;
	int			instance;
	struct cg4_softc	*softc;
	int			error = DDI_SUCCESS;

	DEBUGF(9, ( "cgfour: cgfour_info: entering\n"));

	instance = getminor(dev);

	if (instance > instance_max)
		return DDI_FAILURE;

	switch (infocmd) 
	{

	case DDI_INFO_DEVT2DEVINFO:
		if ((softc = getsoftc(instance)) == NULL)
		{
			error = DDI_FAILURE;
			DEBUGF(1, ( 
				"cgfour: cgfour_info: %s \n",
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
		DEBUGF(1, ( "cgfour: cgfour_info: %s 0x%x\n",
				"unknown infocmd:", infocmd));
		error = DDI_FAILURE;
	}
	return (error);
}

/*
 * This code seems to presume that it is not called unless there
 * is a pending colormap post request, and therefore the vertical
 * retrace interrupt has been enabled.
 */
static u_int
cgfour_intr(caddr_t intr_arg) 
{
	struct cg4_softc *softc = (struct cg4_softc *) intr_arg;
	struct cg4b_cmap *cmap	= softc->cmap;

	DEBUGF(9, ( 
		"cgfour: cgfour_intr: entering\n"));
	
	/*
	 * Test to see if there is really work to do.
	 */
	if (! cgfour_update_pending(softc))
	{
		cg4_int_disable(softc);
		DEBUGF(4, (
		    "cgfour: cgfour_intr: spurious interrupt\n"));
		return DDI_INTR_CLAIMED;
	}

	/*
	 * If we got here we were expecting an interrupt.
	 * Make sure that interrupts are enabled to catch
	 * potential race condition.
	 */
	if (! (*softc->p4reg & P4_REG_INT))
	{
		DEBUGF(1, (
		    "cgfour: cgfour_intr: intr not enabled?\n"));
		return DDI_INTR_CLAIMED;
	}

	/* 
	 * OK, things look good, load the overlay color map:
	 */
	{
		register u_char *in	= softc->omap_rgb;
		register u_char *out	= &cmap->omap;

		/* background color */
		cmap->addr = 1;
		*out = *in++;
		*out = *in++;
		*out = *in++;

		/* foreground color */
		cmap->addr = 3;
		*out = *in++;
		*out = *in++;
		*out = *in;
	}

	/* 
	 * Load main color map 
	 */
	{
		register short count = softc->cmap_count;
		register u_long *in = &softc->cmap_image.cmap_long[0];
		register u_long tmp;

		{
			register short index = softc->cmap_index;

			/* convert count to multiple of 12 bytes */
			count = ((count + (index & 3) + 3) >> 2) * 3;

			/* round index to 4 entry boundary */
			index &= ~3;

			cmap->addr = (u_char)index;
			PTR_INCR(u_long *, in, index * 3);
		}

		{
			register u_char *out = &cmap->cmap;

			/*
			 * Copy 4 bytes (4/3 RGB entries)
			 * per loop iteration.
			 */
			PR_LOOPV(count, 
				tmp = *in++;
				*out = (u_char)(tmp >> 24);
				*out = (u_char)(tmp >> 16);
				*out = (u_char)(tmp >> 8);
				*out = (u_char)tmp; );
		}
	}

	softc->cmap_count = 0;
	softc->flags &= ~CG4_UPDATE_PENDING;
	cg4_int_disable(softc);
	return DDI_INTR_CLAIMED;
}

/* enable display of overlay plane or color planes */
static void
cgfour_set_enable_plane(
	caddr_t	image,
	int	w,
	int	h,
	int	overlay)
{
	u_long *lp = (u_long *)image;
	u_long on;
	u_long count;

	/* 1s enable the overlay plane, 0s enable the color planes */
	on = 0;
	if (overlay)
		on = ~on;

	/* compute number of 32-bit longs to be set */
	count = pr_product(mpr_linebytes(w, 1), h) >> 2;

	/* make sure count is a multiple of 8 */
	while ((count & 7) != 0) 
	{
		*lp++ = on;
		count--;
	}

	PR_LOOP(count >> 3,
		*lp++ = on;
		*lp++ = on;
		*lp++ = on;
		*lp++ = on;
		*lp++ = on;
		*lp++ = on;
		*lp++ = on;
		*lp++ = on);
}

/*
 * Initialize a colormap: background = white, all others = black
 */
/*ARGSUSED*/
static void
cgfour_reset_cmap(
	struct cg4_softc	*softc,
	u_char			*cmap,
	u_int			entries)
{
	bzero((char *) cmap, entries * 3);
	*cmap++ = 255;
	*cmap++ = 255;
	*cmap   = 255;
}

/*
 * Copy colormap entries between red, green, or blue array and
 * interspersed rgb array.
 *
 * count > 0 : copy count bytes from buf to rgb
 * count < 0 : copy -count bytes from rgb to buf
 */
static void
cgfour_cmap_bcopy(
	u_char	*bufp,
	u_char	*rgb,
	u_int	count)
{
	register short rcount = (short)count;

	if (--rcount >= 0)
	{
		PR_LOOPVP(rcount, *rgb = *bufp++; rgb += 3);
	}
	else 
	{
		rcount = -rcount - 2;
		PR_LOOPVP(rcount, *bufp++ = *rgb; rgb += 3);
	}
}

/* 
 * Initialize type B (Brooktree) color map chip.
 * This should be done by the boot PROM, but who knows?
 */
static void
cgfour_reset_b(struct cg4_softc *softc)
{
	register struct cg4b_cmap *cmap = softc->cmap;
	register u_char *p;

	/* control register initial values (addr, data) */
	static u_char ctrltab[4 * 2] = {
		/* read mask = all 1s */
		4, 0xff,

		/* blink mask = all 0s*/
		5, 0,

		/* 
		 * command register
		 *  CR7   0	4:1 multiplexing
		 *  CR6   1	use color palette RAM
		 *  CR5:4 11	blink divisor = 65536
		 *  CR3:2 00	OL blink disabled
		 *  CR1:0 11	OL display enabled
		 */
		6, 0x73,

		/* test register = 0 */
		7, 0
	};

	/* overlay color map initial values */
	static u_char otab[12] = {
		/* OL color 0 (not used) = yellow */
		255, 255,   0,

		/* OL color 1 (overlay plane background) = white */
		255, 255, 255,

		/* OL color 2 (not used) = cyan */
		  0, 255, 255,

		/* OL color 3 (overlay plane foreground) = black */
		  0,   0,   0
	};

	/* load control registers */
	p = ctrltab;
	PR_LOOPP(ITEMSIN(ctrltab) / 2 - 1, 
		cmap->addr = *p++;
		cmap->ctrl = *p++);

	/* load overlay color map */
	cmap->addr = 0;
	p = otab;
	PR_LOOPP(ITEMSIN(otab) - 1, cmap->omap = *p++);
}


static int
p4probe(dev_info_t *devi, caddr_t addr, int *w, int *h)
{
	static int mono_size[P4_ID_RESCODES][2] = {
		1600, 1280,
		1152,  900,
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
	 * peek the P4 register, then try to modify the type code
	 */
	if (ddi_peekl(devi, reg, &id) != DDI_SUCCESS || 
		ddi_pokel(devi, reg, 
		  (long)((id &= ~P4_REG_RESET) ^ P4_ID_TYPE_MASK))
		!= DDI_SUCCESS)
	{
		return (-1);
	}

	/*
	 * if the "type code" changed, put the old value back and quit
	 */
	if ((*reg ^ id) & P4_ID_TYPE_MASK)
	{
		*reg = id;
		return (-1);
	}

	/*
	 * Except for cg8, the high nibble is the type
	 * and the low nibble is the resolution...
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

	DEBUGF(9, ( "cgfour: _init: entering\n"));
	retval = ddi_soft_state_init(&cg4_state, 
				sizeof(struct cg4_softc), NCGFOUR);
	if (retval != 0)
	{
		DEBUGF(1, ( 
		    "cgfour: _init: ddi_soft_state_init FAILED\n"));
		return (retval);
	}

	retval = mod_install(&modlinkage);
	if (retval != 0)
	{
		DEBUGF(1, ( 
			"cgfour: _init: mod_install FAILED\n"));
		ddi_soft_state_fini(&cg4_state);
		return (retval);
	}

	DEBUGF(1, ( "cgfour: _init: returning: success\n"));
	return (retval);
}


int
_fini(void)
{
	int			retval;                 
 
	DEBUGF(9, ( "cgfour: _fini: entering\n"));

        if ((retval = mod_remove(&modlinkage)) != 0)
	{
		DEBUGF(1, ( 
			"cgfour: _fini: mod_remove FAILED\n"));
                return (retval);
	}
 
        ddi_soft_state_fini(&cg4_state);
 
	DEBUGF(1, ( "cgfour: _fini: returning: success\n"));
        return (DDI_SUCCESS);
}        
 

int
_info(struct modinfo *modinfop)
{
	DEBUGF(9, ( "cgfour: _info: called\n"));

	return (mod_info(&modlinkage, modinfop));
}

static int
cgfour_identify(dev_info_t *devi)
{
	char    *name;
	
	name = ddi_get_name(devi);
	 
	if (strcmp(name, "cgfour") == 0)
	{
		DEBUGF(1, ( 
		    "cgfour: cgfour_identify: CLAIMED (%s)\n", name));
		return DDI_IDENTIFIED;
	}
	else
	{
		DEBUGF(1, ( 
		   "cgfour: cgfour_identify: rejected (%s)\n", name));
		return DDI_NOT_IDENTIFIED;
	}
}


struct cb_ops	cgfour_cb_ops = {
        cgfour_open,		/* open */
        cgfour_close,		/* close */
        nodev,                  /* strategy */
        nodev,                  /* print */
        nodev,                  /* dump */
        nodev,                  /* read */
        nodev,                  /* write */
        cgfour_ioctl,		/* ioctl */
        nodev,                  /* devmap */
        cgfour_mmap,		/* mmap */
        nodev,			/* segmap */
        nochpoll,               /* poll */
        ddi_prop_op,		/* prop_op */
        0,                      /* streamtab  */
        D_NEW                   /* driver comaptibility flag */
};      
 

struct dev_ops cgfour_ops = {
	DEVO_REV,		/* devo_rev, */
	0,                      /* refcnt */
	cgfour_info,		/* info */
	cgfour_identify,	/* identify */
	cgfour_probe,		/* probe */
	cgfour_attach,		/* attach */
	cgfour_detach,		/* detach */
	nodev,			/* reset */
	&cgfour_cb_ops,		/* driver operations */
	(struct bus_ops *)0     /* no bus operations */
};

extern struct mod_ops mod_driverops;
static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module. I'm a driver     */
#ifdef  DEVELOPMENT_ONLY
	__TIME__,               /* module name & vers. for modinfo  */
#else   DEVELOPMENT_ONLY
	"color framebuffer cgfour.c 1.10 'cgfour'",
				/* module name & vers. for modinfo  */
#endif  DEVELOPMENT_ONLY
	&cgfour_ops      	/* address of ops struct for driver */
};
			 
static struct modlinkage modlinkage = {
	MODREV_1,               /* rev. of loadable modules system  */
	(void *)&modldrv,       /* address of the linkage structure */
	NULL			/* terminate list of linkage structs*/
};
 
/* Macros to change a cg4 pixrect into a memory pixrect */

#define PR_TO_MEM(src, mem)                     \
    if (src && src->pr_ops != &mem_ops)                 \
    {                                   \
	mem.pr_ops  = &mem_ops;                 \
	mem.pr_size = src->pr_size;                 \
	mem.pr_depth    = src->pr_depth;                \
	mem.pr_data = (char *) &cg4_d(src)->mprp;           \
	src     = &mem;                     \
    }

/*"cg4_rop"
 */
/*ARGSUSED*/
static int cg4_rop(dpr, dx, dy, w, h, op, spr, sx, sy)
Pixrect     *dpr, *spr;
int     dx, dy, w, h, op, sx, sy;
{
    Pixrect         dmempr; /* Scratch area to build mem pixrect for dst. in. */
    Pixrect         smempr; /* Scratch area to build mem pixrect for src in. */
    int		    pr_ropRet;


    if (dpr->pr_depth>8 || (spr && spr->pr_depth>8))
    {
	cmn_err( CE_CONT, "kernel: cg8_rop error: attempt at 32 bit rop\n" );
	return 0;
    }

    PR_TO_MEM(dpr, dmempr);
    PR_TO_MEM(spr, smempr);
    pr_ropRet = pr_rop(dpr, dx, dy, w, h, op, spr, sx, sy);
    return pr_ropRet;
}

/*	
	Here is the old sun view stuff ...
*/

#define ERROR ENOTTY

static int
cg4_ioctl (pr, cmd, data)
    Pixrect        *pr;
    int             cmd;
    caddr_t         data;
{
    static int      cmsize;

	DEBUGF( 2, ( "CG4_IOCTL ENTRY: cmd %x\n", cmd ) );
	switch (cmd) {
	
		case FBIOGPLNGRP:
		*(int *) data = PIX_ATTRGROUP(cg4_d(pr)->planes);
		break;

		case FBIOAVAILPLNGRP:
		{
			static int      cg4groups =
			MAKEPLNGRP(PIXPG_OVERLAY) |
			MAKEPLNGRP(PIXPG_OVERLAY_ENABLE) |
			MAKEPLNGRP(PIXPG_8BIT_COLOR);

			*(int *) data = cg4groups;
			break;
		}

		case FBIOGCMSIZE:
		if (cmsize < 0)
			return -1;

		*(int *) data = cmsize;
		break;

		case FBIOSCMSIZE:
		cmsize = *(int *) data;
		break;

		default:
		return ERROR;
	}
	DEBUGF( 2, ( "CG4_IOCTL EXIT: cmd %x\n", cmd ) );
	return 0;
}
