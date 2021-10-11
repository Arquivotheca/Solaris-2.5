/*
 * Copyright (c) 1991,1994 by Sun Microsystems, Inc.
 * All Rights Reserved
 */

#pragma ident	"@(#)le.c 1.104     95/09/18 SMI"

/*
 *  SunOS 5.0 Multithreaded STREAMS DLPI LANCE (Am7990) Ethernet Driver
 *
 *  Refer to document:
 *	"SunOS 5.0 STREAMS LE Driver", 800-7663-01.
 */

#include	<sys/types.h>
#include	<sys/errno.h>
#include	<sys/debug.h>
#include	<sys/time.h>
#include	<sys/sysmacros.h>
#include	<sys/systm.h>
#include	<sys/user.h>
#include	<sys/stropts.h>
#include	<sys/stream.h>
#include	<sys/strlog.h>
#include	<sys/cmn_err.h>
#include	<sys/cpu.h>
#include	<sys/kmem.h>
#include	<sys/conf.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>
#include	<sys/ksynch.h>
#include	<sys/stat.h>
#include	<sys/kstat.h>
#include	<sys/vtrace.h>
#include	<sys/strsun.h>
#include	<sys/dlpi.h>
#include	<sys/ethernet.h>
#include	<sys/lance.h>
#include	<sys/varargs.h>
#include	<sys/le.h>
#ifdef NETDEBUGGER
#include	<netinet/in.h>
#include	<netinet/in_systm.h>
#include	<netinet/ip.h>
#include	<netinet/udp.h>
unsigned int dle_virt_addr;
unsigned int dle_dma_addr;
unsigned int dle_regs;
unsigned int dle_k_ib;

/* there is a better way to do this */
#define	DLE_MEM_SIZE 0x910
#define	DEBUG_PORT_NUM 1080
#endif /* NETDEBUGGER */

/*
 * Function prototypes.
 */
static	int leidentify(dev_info_t *);
static	int leprobe(dev_info_t *);
static	int leattach(dev_info_t *, ddi_attach_cmd_t);
static	int	ledetach(dev_info_t *, ddi_detach_cmd_t);
static	void lestatinit(struct le *);
static	int leinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static	void leallocthings(struct le *);
static	int leopen(queue_t *, dev_t *, int, int, cred_t *);
static	int leclose(queue_t *rq, int flag, int otyp, cred_t *credp);
static	int lewput(queue_t *, mblk_t *);
static	int lewsrv(queue_t *);
static	void leproto(queue_t *, mblk_t *);
static	void leioctl(queue_t *, mblk_t *);
static	void le_dl_ioc_hdr_info(queue_t *, mblk_t *);
static	void leareq(queue_t *, mblk_t *);
static	void ledreq(queue_t *, mblk_t *);
static	void ledodetach(struct lestr *);
static	void lebreq(queue_t *, mblk_t *);
static	void leubreq(queue_t *, mblk_t *);
static	void leireq(queue_t *, mblk_t *);
static	void leponreq(queue_t *, mblk_t *);
static	void lepoffreq(queue_t *, mblk_t *);
static	void leemreq(queue_t *, mblk_t *);
static	void ledmreq(queue_t *, mblk_t *);
static	void lepareq(queue_t *, mblk_t *);
static	void lespareq(queue_t *, mblk_t *);
static	void leudreq(queue_t *, mblk_t *);
static	int lestart(queue_t *, mblk_t *, struct le *);
static	u_int leintr(caddr_t arg);
static	void lereclaim(struct le *);
static	void lewenable(struct le *);
static	struct lestr *leaccept(struct lestr *, struct le *, int,
	struct ether_addr *);
static	struct lestr *lepaccept(struct lestr *, struct le *, int,
	struct	ether_addr *);
static	void	lesetipq(struct le *);
static	void leread(struct le *, volatile struct lmd *);
static	void lesendup(struct le *, mblk_t *, struct lestr * (*)());
static	mblk_t *leaddudind(struct le *, mblk_t *, struct ether_addr *,
	struct ether_addr *, int, ulong);
static	int lemcmatch(struct lestr *, struct ether_addr *);
static	int leinit(struct le *);
static	void leuninit(struct le *lep);
static	struct lebuf *legetbuf(struct le *, int);
static	void lefreebuf(struct lebuf *);
static	void lermdinit(struct le *, volatile struct lmd *, struct lebuf *);
static	void le_rcv_error(struct le *, struct lmd *);
static	int le_xmit_error(struct le *, struct lmd *);
static	int le_chip_error(struct le *);
static void lerror(dev_info_t *dip, char *fmt, ...);
static	void leopsadd(struct leops *);
static	struct leops *leopsfind(dev_info_t *);

extern mblk_t *desballoc(unsigned char *, int, int, frtn_t *);  /* XXX */

static	struct	module_info	leminfo = {
	LEIDNUM,	/* mi_idnum */
	LENAME,		/* mi_idname */
	LEMINPSZ,	/* mi_minpsz */
	LEMAXPSZ,	/* mi_maxpsz */
	LEHIWAT,	/* mi_hiwat */
	LELOWAT		/* mi_lowat */
};

static	struct	qinit	lerinit = {
	NULL,		/* qi_putp */
	NULL,		/* qi_srvp */
	leopen,		/* qi_qopen */
	leclose,	/* qi_qclose */
	NULL,		/* qi_qadmin */
	&leminfo,	/* qi_minfo */
	NULL		/* qi_mstat */
};

static	struct	qinit	lewinit = {
	lewput,		/* qi_putp */
	lewsrv,		/* qi_srvp */
	NULL,		/* qi_qopen */
	NULL,		/* qi_qclose */
	NULL,		/* qi_qadmin */
	&leminfo,	/* qi_minfo */
	NULL		/* qi_mstat */
};

static struct	streamtab	le_info = {
	&lerinit,	/* st_rdinit */
	&lewinit,	/* st_wrinit */
	NULL,		/* st_muxrinit */
	NULL		/* st_muxwrinit */
};

static	struct	cb_ops	cb_le_ops = {
	nulldev,	/* cb_open */
	nulldev,	/* cb_close */
	nodev,		/* cb_strategy */
	nodev,		/* cb_print */
	nodev,		/* cb_dump */
	nodev,		/* cb_read */
	nodev,		/* cb_write */
	nodev,		/* cb_ioctl */
	nodev,		/* cb_devmap */
	nodev,		/* cb_mmap */
	nodev,		/* cb_segmap */
	nochpoll,	/* cb_chpoll */
	ddi_prop_op,	/* cb_prop_op */
	&le_info,	/* cb_stream */
	D_MP		/* cb_flag */
};

static	struct	dev_ops	le_ops = {
	DEVO_REV,	/* devo_rev */
	0,		/* devo_refcnt */
	leinfo,		/* devo_getinfo */
	leidentify,	/* devo_identify */
	leprobe,	/* devo_probe */
	leattach,	/* devo_attach */
	ledetach,	/* devo_detach */
	nodev,		/* devo_reset */
	&cb_le_ops,	/* devo_cb_ops */
	(struct bus_ops *)NULL,	/* devo_bus_ops */
	ddi_power	/* devo_power */
};

/*
 * The lance chip's dma capabilities are for 24 bits of
 * address. For the sun4c, the top byte is forced to 0xff
 * by the DMA chip so the lance can address 0xff000000-0xffffffff.
 *
 * Note 1:
 *	The lance has a 16-bit data port, so the wordsize
 *	is 16 bits. The initialization block for the lance
 *	has to be aligned on a word boundary. The message
 *	descriptors must be aligned on a quadword boundary
 *	(8 byte). The actual data buffers can be aligned
 *	on a byte boundary.
 */
#define	LEDLIMADDRLO	(0xff000000)
#define	LEDLIMADDRHI	(0xffffffff)
static ddi_dma_lim_t le_dma_limits = {
	(u_long) LEDLIMADDRLO,	/* dlim_addr_lo */
	(u_long) LEDLIMADDRHI,	/* dlim_addr_hi */
	(u_int) ((1<<24)-1),	/* dlim_cntr_max */
	(u_int) 0x3f,		/* dlim_burstsizes (1, 2, 8) */
	0x1,			/* dlim_minxfer */
	1024			/* dlim_speed */
};


/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern	struct	mod_ops	mod_driverops;

/*
 * Module linkage information for the kernel.
 */
static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a driver */
	"Lance Ethernet Driver v1.104",
	&le_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

/*
 * XXX Autoconfiguration lock:  We want to initialize all the global
 * locks at _init().  However, we do not have the cookie required which
 * is returned in ddi_add_intr(), which in turn is usually called at attach
 * time.
 */
static  kmutex_t	leautolock;
static int created_global_mutexes = 0;

/*
 * Linked list of "le" structures - one per device.
 */
struct le *ledev = NULL;

/*
 * Linked list of active (inuse) driver Streams.
 */
static	struct	lestr	*lestrup = NULL;
static	krwlock_t	lestruplock;

/*
 * Linked list of device "opsvec" structures.
 */
static	struct	leops	*leops = NULL;

#define	LE_SYNC_INIT
#ifdef	LE_SYNC_INIT
int	le_sync_init = 1;
#endif /* LE_SYNC_INIT */

/*
 * Single private "global" lock for the few rare conditions
 * we want single-threaded.
 */
static	kmutex_t	lelock;

int
_init(void)
{
	int 	status;

	mutex_init(&leautolock, "le autoconfig lock", MUTEX_DRIVER, NULL);
	status = mod_install(&modlinkage);
	if (status != 0) {
		mutex_destroy(&leautolock);
		return (status);
	}
	return (0);
}

int
_fini(void)
{
	int	status;

	status = mod_remove(&modlinkage);
	if (status != 0)
		return (status);
	if (created_global_mutexes) {
		mutex_destroy(&lelock);
		rw_destroy(&lestruplock);
	}
	mutex_destroy(&leautolock);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Patchable debug flag.
 * Set this to nonzero to enable *all* error messages.
 */
static	int	ledebug = 0;

/*
 * Allocate and zero-out "number" structures
 * each of type "structure" in kernel memory.
 */
#define	GETSTRUCT(structure, number)   \
	((structure *) kmem_zalloc(\
		(u_int) (sizeof (structure) * (number)), KM_SLEEP))
#define	GETBUF(structure, size)   \
	((structure *) kmem_zalloc((u_int) (size), KM_SLEEP))

/*
 * Translate a kernel virtual address to i/o address.
 */
#define	LEBUFIOADDR(lep, a) \
	((lep)->le_bufiobase + ((u_long)(a) - (u_long)(lep)->le_bufbase))
#define	LEIOPBIOADDR(lep, a) \
	((lep)->le_iopbiobase + ((u_long)(a) - (lep)->le_iopbkbase))

/*
 * ddi_dma_sync() a buffer.
 */
#define	LESYNCBUF(lep, a, size, who) \
	if (((lep)->le_flags & LESLAVE) == 0) \
		(void) ddi_dma_sync((lep)->le_bufhandle, \
			((u_long)(a) - (u_long)(lep)->le_bufbase), \
			(size), \
			(who))

/*
 * XXX
 * Define LESYNCIOPB to nothing for now.
 * If/when we have PSO-mode kernels running which really need
 * to sync something during a ddi_dma_sync() of iopb-allocated memory,
 * then this can go back in, but for now we take it out
 * to save some microseconds.
 */
#define	LESYNCIOPB(lep, a, size, who)

#ifdef	notdef
/*
 * ddi_dma_sync() a TMD or RMD descriptor.
 */
#define	LESYNCIOPB(lep, a, size, who) \
	if (((lep)->le_flags & LESLAVE) == 0) \
		(void) ddi_dma_sync((lep)->le_iopbhandle, \
			((u_long)(a) - (lep)->le_iopbkbase), \
			(size), \
			(who))
#endif	notdef


#define	LESAPMATCH(sap, type, flags) ((sap == type)? 1 : \
	((flags & SLALLSAP)? 1 : \
	((sap <= ETHERMTU) && (sap > 0) && (type <= ETHERMTU))? 1 : 0))

#define	SAMEMMUPAGE(a, b) \
	(((u_int)(a) & MMU_PAGEMASK) == ((u_int)(b) & MMU_PAGEMASK))

/*
 * Ethernet broadcast address definition.
 */
static	struct ether_addr	etherbroadcastaddr = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/*
 * Resource amounts.
 *	For now, at least, these never change, and resources
 *	are allocated once and for all at leattach() time.
 */
int	le_ntmdp2 = 7;		/* power of 2 Transmit Ring Descriptors */
int	le_nrmdp2 = 5;		/* power of 2 Receive Ring Descriptors */
int	le_nbufs = 64;		/* # buffers allocated for xmit/recv pool */

/*
 * Our DL_INFO_ACK template.
 */
static	dl_info_ack_t leinfoack = {
	DL_INFO_ACK,			/* dl_primitive */
	ETHERMTU,			/* dl_max_sdu */
	0,				/* dl_min_sdu */
	LEADDRL,			/* dl_addr_length */
	DL_ETHER,			/* dl_mac_type */
	0,				/* dl_reserved */
	0,				/* dl_current_state */
	-2,				/* dl_sap_length */
	DL_CLDLS,			/* dl_service_mode */
	0,				/* dl_qos_length */
	0,				/* dl_qos_offset */
	0,				/* dl_range_length */
	0,				/* dl_range_offset */
	DL_STYLE2,			/* dl_provider_style */
	sizeof (dl_info_ack_t),		/* dl_addr_offset */
	DL_VERSION_2,			/* dl_version */
	ETHERADDRL,			/* dl_brdcst_addr_length */
	sizeof (dl_info_ack_t) + LEADDRL,	/* dl_brdcst_addr_offset */
	0				/* dl_growth */
};

/*
 * Identify device.
 */
static int
leidentify(dev_info_t *dip)
{
	if (strcmp(ddi_get_name(dip), "le") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/*
 * Probe for device.
 */
static int
leprobe(dev_info_t *dip)
{
	struct lanceregs *regsp;
	int	i;

	if (ddi_dev_is_sid(dip) == DDI_FAILURE) {
		/* XXX - need better test */
		if (ddi_map_regs(dip, 0, (caddr_t *) &regsp, 0, 0)
			== DDI_SUCCESS) {
			i = ddi_pokes(dip, (short *)&regsp->lance_rdp, 0);
			ddi_unmap_regs(dip, 0, (caddr_t *)&regsp, 0, 0);
			if (i)
				return (DDI_PROBE_FAILURE);
		}
		else
			return (DDI_PROBE_FAILURE);
	}
	return (DDI_PROBE_SUCCESS);
}

static char tlt_disabled[] =
	"?NOTICE: le%d: 'tpe-link-test?' is false.  "
	"TPE/AUI autoselection disabled.\n";
static char tlt_setprop[] =
	"?NOTICE: le%d: Set 'tpe-link-test?' to true if using AUI connector\n";
/*
 * Interface exists: make available by filling in network interface
 * record.  System will initialize the interface when it is ready
 * to accept packets.
 */
static int
leattach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	register struct le *lep;
	struct lanceregs *regsp;
	ddi_iblock_cookie_t	c;
	struct	leops	*lop;
	ulong_t	timestamp;

	lep = (struct le *) NULL;
	regsp = (struct lanceregs *) NULL;

	if (cmd == DDI_ATTACH) {
		/*
		 * Allocate soft data structure
		 */
		lep = GETSTRUCT(struct le, 1);

		/*
		 * Map in the device registers.
		 */
		if (ddi_map_regs(dip, 0, (caddr_t *) &regsp, 0, 0)) {
			kmem_free(lep, sizeof (struct le));
			lerror(dip, "ddi_map_regs failed");
			return (DDI_FAILURE);
		}

		/*
		 * Stop the chip.
		 */
		regsp->lance_rap = LANCE_CSR0;
		regsp->lance_csr = LANCE_STOP;

		if (ddi_get_iblock_cookie(dip, 0, &c) != DDI_SUCCESS) {
			ddi_unmap_regs(dip, 0, (caddr_t *) &regsp, 0, 0);
			kmem_free(lep, sizeof (struct le));
			lerror(dip, "ddi_get_iblock_cookie failed");
			return (DDI_FAILURE);
		}

		/*
		 * Initialize mutex's for this device.
		 */
		mutex_init(&lep->le_xmitlock, "le xmit lock",
			MUTEX_DRIVER, (void *)c);
		mutex_init(&lep->le_intrlock, "le intr lock",
			MUTEX_DRIVER, (void *)c);
		mutex_init(&lep->le_buflock, "le bufs lock",
			MUTEX_DRIVER, (void *)c);

		/*
		 * One time only driver initializations.
		 */
		mutex_enter(&leautolock);
		if (created_global_mutexes == 0) {
			created_global_mutexes = 1;
			rw_init(&lestruplock, "le streams list lock",
				RW_DRIVER, (void *)c);
			mutex_init(&lelock, "lelock", MUTEX_DRIVER,
				(void *)c);
		}
		mutex_exit(&leautolock);

		ddi_set_driver_private(dip, (caddr_t) lep);
		lep->le_dip = dip;
		lep->le_regsp = regsp;
		lep->le_oopkts = -1;
		lep->le_autosel = 1;

		/*
		 * Add interrupt to system.
		 */
		if (ddi_add_intr(dip, 0, &c, 0, leintr, (caddr_t) lep)) {
			mutex_destroy(&lep->le_xmitlock);
			mutex_destroy(&lep->le_intrlock);
			mutex_destroy(&lep->le_buflock);
			ddi_unmap_regs(dip, 0, (caddr_t *) &regsp, 0, 0);
			kmem_free(lep, sizeof (struct le));
			lerror(dip, "ddi_add_intr failed");
			return (DDI_FAILURE);
		}

		/*
		 * Get the local ethernet address.
		 */
		localetheraddr((struct ether_addr *)NULL,
			&lep->le_ouraddr);

		/*
		 * Look for "learg" property and call leopsadd()
		 * with the info
		 * from our parent node if we find it.
		 */
		lop = (struct leops *) ddi_getprop(DDI_DEV_T_NONE,
			ddi_get_parent(dip), 0, "learg", 0);
		if (lop)
			leopsadd(lop);

		/*
		 * Create the filesystem device node.
		 */
		if (ddi_create_minor_node(dip, "le", S_IFCHR,
			ddi_get_instance(dip), DDI_NT_NET,
				CLONE_DEV) == DDI_FAILURE) {
			mutex_destroy(&lep->le_xmitlock);
			mutex_destroy(&lep->le_intrlock);
			mutex_destroy(&lep->le_buflock);
			ddi_remove_intr(dip, 0, c);
			ddi_unmap_regs(dip, 0, (caddr_t *) &regsp, 0, 0);
			kmem_free(lep, sizeof (struct le));
			lerror(dip, "ddi_create_minor_node failed");
			return (DDI_FAILURE);
		}


		/*
		 * Create power management properties.
		 */
		drv_getparm(TIME, &timestamp);
		if (ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
			"pm_timestamp", (caddr_t) &timestamp,
			sizeof (timestamp)) != DDI_SUCCESS)
			lerror(dip, "leattach:  ddi_prop_create error");
		timestamp = 1;
		if (ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
			"pm_norm_pwr", (caddr_t) &timestamp,
			sizeof (timestamp)) != DDI_SUCCESS)
			lerror(dip, "leattach:  ddi_prop_create error");

		/*
		 * Link this per-device structure in with the rest.
		 */
		mutex_enter(&lelock);
		lep->le_nextp = ledev;
		ledev = lep;
		mutex_exit(&lelock);

		if (ddi_get_instance(dip) == 0 &&
		    strcmp(ddi_get_name(ddi_get_parent(dip)), "ledma") == 0) {

			int cablelen;
			char *cable_select = NULL;
			int proplen;
			char *prop;

			/*
			 * Always honour cable-selection, if set.
			 */
			if (ddi_getlongprop(DDI_DEV_T_ANY,
			    ddi_get_parent(lep->le_dip), DDI_PROP_CANSLEEP,
			    "cable-selection", (caddr_t)&cable_select,
			    &cablelen) == DDI_PROP_SUCCESS) {
				/*
				 * It's set, so disable auto-selection
				 */
				lep->le_autosel = 0;
				if (strncmp(cable_select, "tpe", cablelen) == 0)
					lep->le_tpe = 1;
				else
					lep->le_tpe = 0;
				kmem_free(cable_select, cablelen);
			} else {
				lep->le_tpe = 0;
				lep->le_autosel = 1;
			}

			/*
			 * If auto-selection is disabled, check
			 * to see if tpe-link-test? property is
			 * set to false.  If it is set to false, driver will
			 * not be interrupted with Loss of Carrier
			 * Interrupt and will cause the auto selection
			 * algorithm to break.
			 *
			 * Warn the user of the cable-selection property
			 * not being set, and tell them how to get out
			 * of it.
			 */
			if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
			    DDI_PROP_CANSLEEP, "tpe-link-test?",
			    (caddr_t)&prop, &proplen) == DDI_PROP_SUCCESS) {
				if (strncmp("false", prop, proplen) == 0) {
					if (lep->le_autosel == 1 ||
					    lep->le_tpe == 0) {
						cmn_err(CE_CONT, tlt_disabled,
						    ddi_get_instance(dip));
						cmn_err(CE_CONT, tlt_setprop,
						    ddi_get_instance(dip));
					}
				}
				kmem_free(prop, proplen);
			}

			if (ledebug) {
				lerror(lep->le_dip, lep->le_tpe ?
					"uses twisted-pair" : "uses aui");
				lerror(lep->le_dip, lep->le_autosel ?
					"auto-select" : "no auto-select");
			}
		}

		lestatinit(lep);
		ddi_report_dev(dip);
		return (DDI_SUCCESS);
	} else if (cmd == DDI_RESUME) {
		if ((lep = (struct le *)ddi_get_driver_private(dip)) == NULL)
			return (DDI_FAILURE);
		lep->le_flags &= ~LESUSPENDED;
		leinit(lep);
		return (DDI_SUCCESS);
	} else
		return (DDI_FAILURE);
}

static int
ledetach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	register struct le *lep, *letmp, **prevlep;
	volatile struct lanceregs *regsp;

	if (cmd == DDI_DETACH) {
		lep = (struct le *)ddi_get_driver_private(dip);
		regsp = lep->le_regsp;

		/*
		 * Make driver quiescent
		 */
		regsp->lance_rap = LANCE_CSR0;
		regsp->lance_csr = LANCE_STOP;

		ddi_remove_minor_node(dip, NULL);
		/*
		 * Destroy all mutexes and data structures allocated during
		 * attach time.
		 */
		if (regsp)
			ddi_unmap_regs(dip, 0, (caddr_t *) &regsp, 0, 0);

		ddi_remove_intr(dip, 0, lep->le_cookie);

		/*
		 * Remove lep from the link list of device structures
		 */

		for (prevlep = &ledev; (letmp = *prevlep) != NULL;
			prevlep = &letmp->le_nextp)
			if (letmp == lep)
				break;

		if (letmp) {
			*prevlep = letmp->le_nextp;
			kstat_delete(letmp->le_ksp);
			mutex_destroy(&letmp->le_xmitlock);
			mutex_destroy(&letmp->le_intrlock);
			mutex_destroy(&letmp->le_buflock);
			if (!(letmp->le_flags & LESLAVE) &&
				letmp->le_iopbhandle) {
				ddi_dma_free(letmp->le_iopbhandle);
				ddi_dma_free(letmp->le_bufhandle);
				ddi_iopb_free((caddr_t)letmp->le_iopbkbase);
				kmem_free((void *)letmp->le_bufkbase,
					LEBURSTSIZE +
					(letmp->le_nbufs *
					sizeof (struct lebuf)));
				kmem_free(letmp->le_buftab,
				(letmp->le_nbufs * sizeof (struct lebuf *)));
			}
			kmem_free(letmp, sizeof (struct le));
		}
		return (DDI_SUCCESS);
	} else if ((cmd == DDI_SUSPEND) || (cmd == DDI_PM_SUSPEND)) {
		if ((lep = (struct le *) ddi_get_driver_private(dip))
			== NULL)
			return (DDI_FAILURE);
		lep->le_flags |= LESUSPENDED;
		leuninit(lep);
		return (DDI_SUCCESS);
	} else
		return (DDI_FAILURE);
}

static int
lestat_kstat_update(kstat_t *ksp, int rw)
{
	struct le *lep;
	struct lestat *lesp;

	lep = (struct le *) ksp->ks_private;
	lesp = (struct lestat *) ksp->ks_data;

	if (rw == KSTAT_WRITE) {
#ifdef	kstat
		lep->le_ipackets	= lesp->les_ipackets.value.ul;
		lep->le_ierrors		= lesp->les_ierrors.value.ul;
		lep->le_opackets	= lesp->les_opackets.value.ul;
		lep->le_oerrors		= lesp->les_oerrors.value.ul;
		lep->le_collisions	= lesp->les_collisions.value.ul;
		lep->le_defer		= lesp->les_defer.value.ul;
		lep->le_fram		= lesp->les_fram.value.ul;
		lep->le_crc		= lesp->les_crc.value.ul;
		lep->le_oflo		= lesp->les_oflo.value.ul;
		lep->le_uflo		= lesp->les_uflo.value.ul;
		lep->le_missed		= lesp->les_missed.value.ul;
		lep->le_tlcol		= lesp->les_tlcol.value.ul;
		lep->le_trtry		= lesp->les_trtry.value.ul;
		lep->le_tnocar		= lesp->les_tnocar.value.ul;
		lep->le_inits		= lesp->les_inits.value.ul;
		lep->le_nocanput	= lesp->les_nocanput.value.ul;
		lep->le_allocbfail	= lesp->les_allocbfail.value.ul;
#else
		return (EACCES);
#endif	kstat
	} else {
		lesp->les_ipackets.value.ul	= lep->le_ipackets;
		lesp->les_ierrors.value.ul	= lep->le_ierrors;
		lesp->les_opackets.value.ul	= lep->le_opackets;
		lesp->les_oerrors.value.ul	= lep->le_oerrors;
		lesp->les_collisions.value.ul	= lep->le_collisions;
		lesp->les_defer.value.ul	= lep->le_defer;
		lesp->les_fram.value.ul		= lep->le_fram;
		lesp->les_crc.value.ul		= lep->le_crc;
		lesp->les_oflo.value.ul		= lep->le_oflo;
		lesp->les_uflo.value.ul		= lep->le_uflo;
		lesp->les_missed.value.ul	= lep->le_missed;
		lesp->les_tlcol.value.ul	= lep->le_tlcol;
		lesp->les_trtry.value.ul	= lep->le_trtry;
		lesp->les_tnocar.value.ul	= lep->le_tnocar;
		lesp->les_inits.value.ul	= lep->le_inits;
		lesp->les_nocanput.value.ul	= lep->le_nocanput;
		lesp->les_allocbfail.value.ul	= lep->le_allocbfail;
	}
	return (0);
}

static void
lestatinit(struct le *lep)
{
	kstat_t	*ksp;
	struct lestat *lesp;

#ifdef	kstat
	if ((ksp = kstat_create("le", ddi_get_instance(lep->le_dip),
	    NULL, "net", KSTAT_TYPE_NAMED,
	    sizeof (struct lestat) / sizeof (kstat_named_t),
		KSTAT_FLAG_PERSISTENT)) == NULL) {
#else
	if ((ksp = kstat_create("le", ddi_get_instance(lep->le_dip),
	    NULL, "net", KSTAT_TYPE_NAMED,
	    sizeof (struct lestat) / sizeof (kstat_named_t), 0)) == NULL) {
#endif	kstat
		lerror(lep->le_dip, "kstat_create failed");
		return;
	}
	lep->le_ksp = ksp;
	lesp = (struct lestat *) (ksp->ks_data);
	kstat_named_init(&lesp->les_ipackets,		"ipackets",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_ierrors,		"ierrors",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_opackets,		"opackets",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_oerrors,		"oerrors",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_collisions,		"collisions",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_defer,		"defer",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_fram,		"framming",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_crc,		"crc",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_oflo,		"oflo",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_uflo,		"uflo",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_missed,		"missed",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_tlcol,		"late_collisions",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_trtry,		"retry_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_tnocar,		"nocarrier",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_inits,		"inits",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_nocanput,		"nocanput",
		KSTAT_DATA_ULONG);
	kstat_named_init(&lesp->les_allocbfail,		"allocbfail",
		KSTAT_DATA_ULONG);
	ksp->ks_update = lestat_kstat_update;
	ksp->ks_private = (void *) lep;
	kstat_install(ksp);
}

/*
 * Translate "dev_t" to a pointer to the associated "dev_info_t".
 */
/* ARGSUSED */
static int
leinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
	void **result)
{
	dev_t	dev = (dev_t) arg;
	int	instance, rc;
	struct	lestr	*slp;

	instance = getminor(dev);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		rw_enter(&lestruplock, RW_READER);
		dip = NULL;
		for (slp = lestrup; slp; slp = slp->sl_nextp)
			if (slp->sl_minor == instance)
				break;
		if (slp && slp->sl_lep)
			dip = slp->sl_lep->le_dip;
		rw_exit(&lestruplock);

		if (dip) {
			*result = (void *) dip;
			rc = DDI_SUCCESS;
		} else
			rc = DDI_FAILURE;
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)instance;
		rc = DDI_SUCCESS;
		break;

	default:
		rc = DDI_FAILURE;
		break;
	}
	return (rc);
}

/*
 * Allocate memory for:
 *   - LANCE initialization block
 *   - LANCE transmit descriptor ring
 *   - LANCE receive descriptor ring
 *   - pool of xmit/recv buffers
 *
 * For SLAVE devices, allocate out of device local memory.
 * For DVMA devices, allocate out of main memory.
 *
 * The init block and descriptors are allocated contiguously
 * and the buffers are allocated contiguously.  Each
 * of the iopb and buf areas are described by a DDI DMA handle,
 * (null for slave devices), the base kernel virtual address
 * for driver usage, and the base io virtual address
 * for the chip usage.
 */
static void
leallocthings(register struct le *lep)
{
	struct	lebuf	*lbp;
	u_long a;
	int	size;
	int	i;
	ddi_dma_cookie_t	c;

	/*
	 * Return if resources are already allocated.
	 */
	if (lep->le_ibp)
		return;

	lep->le_nrmdp2 = le_nrmdp2;
	lep->le_nrmds = 1 << lep->le_nrmdp2;
	lep->le_ntmdp2 = le_ntmdp2;
	lep->le_ntmds = 1 << lep->le_ntmdp2;
	lep->le_nbufs = le_nbufs;

	/*
	 * Allocate Init block, RMDs, TMDs, and Buffers.
	 */
	if (lep->le_flags & LESLAVE) {	/* Slave (non-DMA) interface */

		/*
		 * Allocate data structures from device local memory
		 * starting at 'membase' which is assumed to be
		 * LANCEALIGNed.
		 */
		ASSERT((lep->le_membase & 0x3) == 0);
		a = lep->le_membase;

		/* Allocate the chip initialization block */
		lep->le_ibp = (struct lance_init_block *) a;
		a += sizeof (struct lance_init_block);
		a = LEROUNDUP(a, LANCEALIGN);

		/* Allocate the message descriptor rings */
		lep->le_rmdp = (struct lmd *) a;
		a += lep->le_nrmds * sizeof (struct lmd);
		a = LEROUNDUP(a, LANCEALIGN);
		lep->le_tmdp = (struct lmd *) a;
		a += lep->le_ntmds * sizeof (struct lmd);
		a = LEROUNDUP(a, LEBURSTSIZE);

		/* Allocate the buffer pool burst-aligned */
		lep->le_nbufs = (lep->le_memsize -
			OFFSET(lep->le_membase, a)) /
			sizeof (struct lebuf);
		lep->le_bufbase = (caddr_t) a;

		/* Initialize private io address handles */
		lep->le_iopbhandle = NULL;
		lep->le_iopbkbase = (u_long) lep->le_membase;
		lep->le_iopbiobase = 0;
		lep->le_bufhandle = NULL;
		lep->le_bufkbase = (u_long) lep->le_bufbase;
		lep->le_bufiobase = (u_long)lep->le_bufkbase
			- lep->le_membase;
	} else {	/* Master (DMA) interface */

		/*
		 * Allocate all data structures from main memory.
		 */

		/*
		 * Allocate the chip init block and descriptors
		 * all at one time remembering to allocate extra for
		 * alignments.
		 */
		size = sizeof (struct lance_init_block) +
			(lep->le_nrmds * sizeof (struct lmd)) +
			(lep->le_ntmds * sizeof (struct lmd)) +
			(4 * LANCEALIGN);	/* fudge */
		if (ddi_iopb_alloc(lep->le_dip, &le_dma_limits,
			(u_int) size,
			(caddr_t *) &lep->le_iopbkbase)) {
			panic("leallocthings:  out of iopb space");
			/*NOTREACHED*/
		}
		a = lep->le_iopbkbase;
		a = LEROUNDUP(a, LANCEALIGN);
		lep->le_ibp = (struct lance_init_block *) a;
		a += sizeof (struct lance_init_block);
		a = LEROUNDUP(a, LANCEALIGN);
		lep->le_rmdp = (struct lmd *) a;
		a += lep->le_nrmds * sizeof (struct lmd);
		a = LEROUNDUP(a, LANCEALIGN);
		lep->le_tmdp = (struct lmd *) a;

#ifdef NETDEBUGGER
		size = (DLE_MEM_SIZE > size) ? DLE_MEM_SIZE : size;
#endif /* NETDEBUGGER */

		/*
		 * IO map this and get an "iopb" dma handle.
		 */
		if (ddi_dma_addr_setup(lep->le_dip, (struct as *) 0,
			(caddr_t) lep->le_iopbkbase, size,
			DDI_DMA_RDWR|DDI_DMA_CONSISTENT,
			DDI_DMA_DONTWAIT, 0, &le_dma_limits,
			&lep->le_iopbhandle))
			panic("le:  ddi_dma_addr_setup iopb failed");

		/*
		 * Initialize iopb io virtual address.
		 */
		if (ddi_dma_htoc(lep->le_iopbhandle, 0, &c))
			panic("le:  ddi_dma_htoc iopb failed");
		lep->le_iopbiobase = c.dmac_address;

		/*
		 * Allocate the buffers burst-aligned.
		 */
		size = (lep->le_nbufs * sizeof (struct lebuf))
			+ LEBURSTSIZE;
		lep->le_bufbase = (caddr_t) GETBUF(struct lebuf, size);
		lep->le_bufkbase = (u_long) lep->le_bufbase;
		lep->le_bufbase = (caddr_t)
			LEROUNDUP((u_int)lep->le_bufbase, LEBURSTSIZE);

		/*
		 * IO map the buffers and get a "buffer" dma handle.
		 */
		if (ddi_dma_addr_setup(lep->le_dip, (struct as *) 0,
			(caddr_t) lep->le_bufbase,
			(lep->le_nbufs * sizeof (struct lebuf)),
			DDI_DMA_RDWR, DDI_DMA_DONTWAIT, 0, &le_dma_limits,
			&lep->le_bufhandle))
			panic("le: ddi_dma_addr_setup of bufs failed");

		/*
		 * Initialize buf io virtual address.
		 */
		if (ddi_dma_htoc(lep->le_bufhandle, 0, &c))
			panic("le:  ddi_dma_htoc buf failed");
		lep->le_bufiobase = c.dmac_address;

	}

#ifdef NETDEBUGGER
	dle_virt_addr = (u_long) lep->le_iopbkbase; /* DLE virt addr for iopb */
	dle_dma_addr = lep->le_iopbiobase;	/* dle IO addr for chip */
	dle_regs = lep->le_regsp;	/* addr of regs */
	dle_k_ib = lep->le_iopbiobase;	/* addr of iobp */
#endif /* NETDEBUGGER */

	/*
	 * Keep handy limit values for RMD, TMD, and Buffers.
	 */
	lep->le_rmdlimp = &((lep->le_rmdp)[lep->le_nrmds]);
	lep->le_tmdlimp = &((lep->le_tmdp)[lep->le_ntmds]);

	/*
	 * Allocate buffer pointer stack (fifo).
	 */
	size = lep->le_nbufs * sizeof (struct lebuf *);
	lep->le_buftab = kmem_alloc(size, KM_SLEEP);

	/*
	 * Zero out the buffers.
	 */
	bzero(lep->le_bufbase, lep->le_nbufs * sizeof (struct lebuf));

	/*
	 * Zero out xmit and rcv holders.
	 */
	bzero((caddr_t) lep->le_tbufp, sizeof (lep->le_tbufp));
	bzero((caddr_t) lep->le_tmblkp, sizeof (lep->le_tmblkp));
	bzero((caddr_t) lep->le_rbufp, sizeof (lep->le_rbufp));

	/*
	 * Initialize buffer pointer stack.
	 */
	lep->le_bufi = 0;
	for (i = 0; i < lep->le_nbufs; i++) {
		lbp = &((struct lebuf *)lep->le_bufbase)[i];
		lbp->lb_lep = lep;
		lbp->lb_frtn.free_func = lefreebuf;
		lbp->lb_frtn.free_arg = (char *) lbp;
		lefreebuf(lbp);
	}
	lep->le_buflim = lep->le_bufbase +
		(lep->le_nbufs * sizeof (struct lebuf));
}

/*ARGSUSED*/
static
leopen(queue_t *rq, dev_t *devp, int flag, int sflag, cred_t *credp)
{
	register	struct	lestr	*slp;
	register	struct	lestr	**prevslp;
	int	minordev;
	int	rc = 0;

	ASSERT(rq);
	ASSERT(sflag != MODOPEN);
	TRACE_1(TR_FAC_LE, TR_LE_OPEN, "leopen:  rq %X", rq);

	/*
	 * Serialize all driver open and closes.
	 */
	rw_enter(&lestruplock, RW_WRITER);

	/*
	 * Determine minor device number.
	 */
	prevslp = &lestrup;
	if (sflag == CLONEOPEN) {
		minordev = 0;
		for (; (slp = *prevslp) != NULL; prevslp = &slp->sl_nextp) {
			if (minordev < slp->sl_minor)
				break;
			minordev++;
		}
		*devp = makedevice(getmajor(*devp), minordev);
	} else
		minordev = getminor(*devp);

	if (rq->q_ptr)
		goto done;

	slp = GETSTRUCT(struct lestr, 1);
	slp->sl_minor = minordev;
	slp->sl_rq = rq;
	slp->sl_state = DL_UNATTACHED;
	slp->sl_sap = 0;
	slp->sl_flags = 0;
	slp->sl_lep = NULL;
	slp->sl_mccount = 0;
	slp->sl_mctab = NULL;
	mutex_init(&slp->sl_lock, "le stream lock", MUTEX_DRIVER, (void *)0);

	/*
	 * Link new entry into the list of active entries.
	 */
	slp->sl_nextp = *prevslp;
	*prevslp = slp;

	rq->q_ptr = WR(rq)->q_ptr = (char *) slp;

	/*
	 * Disable automatic enabling of our write service procedure.
	 * We control this explicitly.
	 */
	noenable(WR(rq));

done:
	rw_exit(&lestruplock);
	qprocson(rq);
	return (rc);
}

/*ARGSUSED1*/
static int
leclose(queue_t *rq, int flag, int otyp, cred_t *credp)
{
	register	struct	lestr	*slp;
	register	struct	lestr	**prevslp;

	TRACE_1(TR_FAC_LE, TR_LE_CLOSE, "leclose:  rq %X", rq);
	ASSERT(rq);
	ASSERT(rq->q_ptr);

	qprocsoff(rq);

	slp = (struct lestr *) rq->q_ptr;

	/*
	 * Implicit detach Stream from interface.
	 */
	if (slp->sl_lep)
		ledodetach(slp);

	rw_enter(&lestruplock, RW_WRITER);

	/*
	 * Unlink the per-Stream entry from the active list and free it.
	 */
	for (prevslp = &lestrup; (slp = *prevslp) != NULL;
		prevslp = &slp->sl_nextp)
		if (slp == (struct lestr *) rq->q_ptr)
			break;
	ASSERT(slp);
	*prevslp = slp->sl_nextp;

	mutex_destroy(&slp->sl_lock);
	kmem_free(slp, sizeof (struct lestr));

	rq->q_ptr = WR(rq)->q_ptr = NULL;

	rw_exit(&lestruplock);
	return (0);
}

static int
lewput(queue_t *wq, mblk_t *mp)
{
	register	struct	lestr	*slp = (struct lestr *) wq->q_ptr;
	struct	le	*lep;

	TRACE_2(TR_FAC_LE, TR_LE_WPUT_START,
		"lewput start:  wq %X db_type %o", wq, DB_TYPE(mp));

	switch (DB_TYPE(mp)) {
		case M_DATA:		/* "fastpath" */
			lep = slp->sl_lep;
			if ((slp->sl_flags & (SLFAST|SLRAW) == 0) ||
				(slp->sl_state != DL_IDLE) ||
				(lep == NULL)) {
				merror(wq, mp, EPROTO);
				break;
			}

			/*
			 * If any msgs already enqueued or the interface will
			 * loop back up the message (due to LEPROMISC), then
			 * enqueue the msg.  Otherwise just xmit it directly.
			 */
			if (wq->q_first) {
				(void) putq(wq, mp);
				lep->le_wantw = 1;
				qenable(wq);
			} else if (lep->le_flags & LEPROMISC) {
				(void) putq(wq, mp);
				qenable(wq);
			} else
				(void) lestart(wq, mp, lep);

			break;

		case M_PROTO:
		case M_PCPROTO:
			/*
			 * Break the association between the current thread and
			 * the thread that calls leproto() to resolve the
			 * problem of leintr() threads which loop back around
			 * to call leproto and try to recursively acquire
			 * internal locks.
			 */
			(void) putq(wq, mp);
			qenable(wq);
			break;

		case M_IOCTL:
			leioctl(wq, mp);
			break;

		case M_FLUSH:
			if (*mp->b_rptr & FLUSHW) {
				flushq(wq, FLUSHALL);
				*mp->b_rptr &= ~FLUSHW;
			}
			if (*mp->b_rptr & FLUSHR)
				qreply(wq, mp);
			else
				freemsg(mp);
			break;

		default:
			freemsg(mp);
			break;
	}
	TRACE_1(TR_FAC_LE, TR_LE_WPUT_END, "lewput end:  wq %X", wq);
	return (0);
}

/*
 * Enqueue M_PROTO/M_PCPROTO (always) and M_DATA (sometimes) on the wq.
 *
 * Processing of some of the M_PROTO/M_PCPROTO msgs involves acquiring
 * internal locks that are held across upstream putnext calls.
 * Specifically there's the problem of leintr() holding le_intrlock
 * and lestruplock when it calls putnext() and that thread looping
 * back around to call lewput and, eventually, leinit() to create a
 * recursive lock panic.  There are two obvious ways of solving this
 * problem: (1) have leintr() do putq instead of putnext which provides
 * the loopback "cutout" right at the rq, or (2) allow leintr() to putnext
 * and put the loopback "cutout" around leproto().  We choose the latter
 * for performance reasons.
 *
 * M_DATA messages are enqueued on the wq *only* when the xmit side
 * is out of tbufs or tmds.  Once the xmit resource is available again,
 * wsrv() is enabled and tries to xmit all the messages on the wq.
 */
static
lewsrv(queue_t *wq)
{
	mblk_t	*mp;
	struct	lestr	*slp;
	struct	le	*lep;

	TRACE_1(TR_FAC_LE, TR_LE_WSRV_START, "lewsrv start:  wq %X", wq);

	slp = (struct lestr *) wq->q_ptr;
	lep = slp->sl_lep;

	while (mp = getq(wq))
		switch (DB_TYPE(mp)) {
			case	M_DATA:
				if (lep) {
					if (lestart(wq, mp, lep))
						goto done;
				} else
					freemsg(mp);
				break;

			case	M_PROTO:
			case	M_PCPROTO:
				leproto(wq, mp);
				break;

			default:
				ASSERT(0);
				break;
		}

done:
	TRACE_1(TR_FAC_LE, TR_LE_WSRV_END, "lewsrv end:  wq %X", wq);
	return (0);
}

static void
leproto(queue_t *wq, mblk_t *mp)
{
	union	DL_primitives	*dlp;
	struct	lestr	*slp;
	u_long	prim;

	slp = (struct lestr *) wq->q_ptr;
	dlp = (union DL_primitives *) mp->b_rptr;
	prim = dlp->dl_primitive;

	TRACE_2(TR_FAC_LE, TR_LE_PROTO_START,
		"leproto start:  wq %X dlprim %X", wq, prim);

	mutex_enter(&slp->sl_lock);

	switch (prim) {
		case	DL_UNITDATA_REQ:
			leudreq(wq, mp);
			break;

		case	DL_ATTACH_REQ:
			leareq(wq, mp);
			break;

		case	DL_DETACH_REQ:
			ledreq(wq, mp);
			break;

		case	DL_BIND_REQ:
			lebreq(wq, mp);
			break;

		case	DL_UNBIND_REQ:
			leubreq(wq, mp);
			break;

		case	DL_INFO_REQ:
			leireq(wq, mp);
			break;

		case	DL_PROMISCON_REQ:
			leponreq(wq, mp);
			break;

		case	DL_PROMISCOFF_REQ:
			lepoffreq(wq, mp);
			break;

		case	DL_ENABMULTI_REQ:
			leemreq(wq, mp);
			break;

		case	DL_DISABMULTI_REQ:
			ledmreq(wq, mp);
			break;

		case	DL_PHYS_ADDR_REQ:
			lepareq(wq, mp);
			break;

		case	DL_SET_PHYS_ADDR_REQ:
			lespareq(wq, mp);
			break;

		default:
			dlerrorack(wq, mp, prim, DL_UNSUPPORTED, 0);
			break;
	}

	TRACE_2(TR_FAC_LE, TR_LE_PROTO_END,
		"leproto end:  wq %X dlprim %X", wq, prim);

	mutex_exit(&slp->sl_lock);
}

static void
leioctl(queue_t *wq, mblk_t *mp)
{
	struct	iocblk	*iocp = (struct iocblk *) mp->b_rptr;
	struct	lestr	*slp = (struct lestr *) wq->q_ptr;

	switch (iocp->ioc_cmd) {
	case DLIOCRAW:		/* raw M_DATA mode */
		slp->sl_flags |= SLRAW;
		miocack(wq, mp, 0, 0);
		break;

	case DL_IOC_HDR_INFO:	/* M_DATA "fastpath" info request */
		le_dl_ioc_hdr_info(wq, mp);
		break;

	default:
		miocnak(wq, mp, 0, EINVAL);
		break;
	}
}

/*
 * M_DATA "fastpath" info request.
 * Following the M_IOCTL mblk should come a DL_UNITDATA_REQ mblk.
 * We ack with an M_IOCACK pointing to the original DL_UNITDATA_REQ mblk
 * followed by an mblk containing the raw ethernet header corresponding
 * to the destination address.  Subsequently, we may receive M_DATA
 * msgs which start with this header and may send up
 * up M_DATA msgs with b_rptr pointing to a (ulong) group address
 * indicator followed by the network-layer data (IP packet header).
 * This is all selectable on a per-Stream basis.
 */
static void
le_dl_ioc_hdr_info(queue_t *wq, mblk_t *mp)
{
	mblk_t	*nmp;
	struct	lestr	*slp;
	struct	ledladdr	*dlap;
	dl_unitdata_req_t	*dludp;
	struct	ether_header	*headerp;
	struct	le	*lep;
	int	off, len;
	int	minsize;

	slp = (struct lestr *) wq->q_ptr;
	minsize = sizeof (dl_unitdata_req_t) + LEADDRL;

	/*
	 * Sanity check the request.
	 */
	if ((mp->b_cont == NULL) ||
		(MBLKL(mp->b_cont) < minsize) ||
		(*((u_long *) mp->b_cont->b_rptr) != DL_UNITDATA_REQ) ||
		((lep = slp->sl_lep) == NULL)) {
		miocnak(wq, mp, 0, EINVAL);
		return;
	}

	/*
	 * Sanity check the DL_UNITDATA_REQ destination address
	 * offset and length values.
	 */
	dludp = (dl_unitdata_req_t *) mp->b_cont->b_rptr;
	off = dludp->dl_dest_addr_offset;
	len = dludp->dl_dest_addr_length;
	if (!MBLKIN(mp->b_cont, off, len) || (len != LEADDRL)) {
		miocnak(wq, mp, 0, EINVAL);
		return;
	}

	dlap = (struct ledladdr *) (mp->b_cont->b_rptr + off);

	/*
	 * Allocate a new mblk to hold the ether header.
	 */
	if ((nmp = allocb(sizeof (struct ether_header), BPRI_MED)) == NULL) {
		miocnak(wq, mp, 0, ENOMEM);
		return;
	}
	nmp->b_wptr += sizeof (struct ether_header);

	/*
	 * Fill in the ether header.
	 */
	headerp = (struct ether_header *) nmp->b_rptr;
	ether_copy(&dlap->dl_phys, &headerp->ether_dhost);
	ether_copy(&lep->le_ouraddr, &headerp->ether_shost);
	headerp->ether_type = dlap->dl_sap;

	/*
	 * Link new mblk in after the "request" mblks.
	 */
	linkb(mp, nmp);

	slp->sl_flags |= SLFAST;

	/*
	 * XXX Don't bother calling lesetipq() here.
	 */

	miocack(wq, mp, msgsize(mp->b_cont), 0);
}

static void
leareq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;
	union	DL_primitives	*dlp;
	struct	le	*lep;
	int	ppa;
	ulong_t	timestamp;

	slp = (struct lestr *) wq->q_ptr;
	dlp = (union DL_primitives *) mp->b_rptr;

	if (MBLKL(mp) < DL_ATTACH_REQ_SIZE) {
		dlerrorack(wq, mp, DL_ATTACH_REQ, DL_BADPRIM, 0);
		return;
	}

	if (slp->sl_state != DL_UNATTACHED) {
		dlerrorack(wq, mp, DL_ATTACH_REQ, DL_OUTSTATE, 0);
		return;
	}

	ppa = dlp->attach_req.dl_ppa;

	/*
	 * Valid ppa?
	 */
	mutex_enter(&lelock);
	for (lep = ledev; lep; lep = lep->le_nextp)
		if (ppa == ddi_get_instance(lep->le_dip))
			break;
	mutex_exit(&lelock);

	if (lep == NULL) {
		dlerrorack(wq, mp, dlp->dl_primitive, DL_BADPPA, 0);
		return;
	}

	/*
	 * Has device been initialized?  Do so if necessary.
	 * Also update power management property.
	 */
	if ((lep->le_flags & LERUNNING) == 0) {
		if (leinit(lep)) {
			dlerrorack(wq, mp, dlp->dl_primitive,
				DL_INITFAILED, 0);
			return;
		}

		timestamp = 0;	/* Busy */
		if (ddi_prop_modify(DDI_DEV_T_NONE, lep->le_dip, 0,
			"pm_timestamp", (caddr_t)&timestamp,
			sizeof (timestamp)) != DDI_SUCCESS)
			lerror(lep->le_dip,
				"leareq:  ddi_prop_modify error");
	}

	/*
	 * Set link to device and update our state.
	 */
	slp->sl_lep = lep;
	slp->sl_state = DL_UNBOUND;

	dlokack(wq, mp, DL_ATTACH_REQ);
}

static void
ledreq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;

	slp = (struct lestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_DETACH_REQ_SIZE) {
		dlerrorack(wq, mp, DL_DETACH_REQ, DL_BADPRIM, 0);
		return;
	}

	if (slp->sl_state != DL_UNBOUND) {
		dlerrorack(wq, mp, DL_DETACH_REQ, DL_OUTSTATE, 0);
		return;
	}

	ledodetach(slp);
	dlokack(wq, mp, DL_DETACH_REQ);
}

/*
 * Detach a Stream from an interface.
 */
static void
ledodetach(struct lestr *slp)
{
	struct	lestr	*tslp;
	struct	le	*lep;
	int	reinit = 0;
	ulong_t	timestamp;

	ASSERT(slp->sl_lep);

	lep = slp->sl_lep;
	slp->sl_lep = NULL;

	/*
	 * Disable promiscuous mode if on.
	 */
	if (slp->sl_flags & SLALLPHYS) {
		slp->sl_flags &= ~SLALLPHYS;
		reinit = 1;
	}

	/*
	 * Disable ALLMULTI mode if on.
	 */
	if (slp->sl_flags & SLALLMULTI) {
		slp->sl_flags &= ~SLALLMULTI;
		reinit = 1;
	}

	/*
	 * Disable any Multicast Addresses.
	 */
	slp->sl_mccount = 0;
	if (slp->sl_mctab) {
		kmem_free(slp->sl_mctab, LEMCALLOC);
		slp->sl_mctab = NULL;
		reinit = 1;
	}

	/*
	 * Detach from device structure.
	 * Uninit the device and update power management property
	 * when no other streams are attached to it.
	 */
	for (tslp = lestrup; tslp; tslp = tslp->sl_nextp)
		if (tslp->sl_lep == lep)
			break;
	if (tslp == NULL) {
		leuninit(lep);

		drv_getparm(TIME, &timestamp);
		if (ddi_prop_modify(DDI_DEV_T_NONE, lep->le_dip, 0,
			"pm_timestamp", (caddr_t)&timestamp,
			sizeof (timestamp)) != DDI_SUCCESS)
			lerror(lep->le_dip,
				"ledodetach:  ddi_prop_modify error");
	} else if (reinit)
		(void) leinit(lep);

	slp->sl_state = DL_UNATTACHED;

	lesetipq(lep);
}

static void
lebreq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;
	union	DL_primitives	*dlp;
	struct	le	*lep;
	struct	ledladdr	leaddr;
	u_long	sap;
	int	xidtest;

	slp = (struct lestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_BIND_REQ_SIZE) {
		dlerrorack(wq, mp, DL_BIND_REQ, DL_BADPRIM, 0);
		return;
	}

	if (slp->sl_state != DL_UNBOUND) {
		dlerrorack(wq, mp, DL_BIND_REQ, DL_OUTSTATE, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	lep = slp->sl_lep;
	sap = dlp->bind_req.dl_sap;
	xidtest = dlp->bind_req.dl_xidtest_flg;

	ASSERT(lep);

	if (xidtest) {
		dlerrorack(wq, mp, DL_BIND_REQ, DL_NOAUTO, 0);
		return;
	}

	if (sap > ETHERTYPE_MAX) {
		dlerrorack(wq, mp, dlp->dl_primitive, DL_BADSAP, 0);
		return;
	}

	/*
	 * Save SAP value for this Stream and change state.
	 */
	slp->sl_sap = sap;
	slp->sl_state = DL_IDLE;

	leaddr.dl_sap = sap;
	ether_copy(&lep->le_ouraddr, &leaddr.dl_phys);
	dlbindack(wq, mp, sap, &leaddr, LEADDRL, 0, 0);

	lesetipq(lep);
}

static void
leubreq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;

	slp = (struct lestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_UNBIND_REQ_SIZE) {
		dlerrorack(wq, mp, DL_UNBIND_REQ, DL_BADPRIM, 0);
		return;
	}

	if (slp->sl_state != DL_IDLE) {
		dlerrorack(wq, mp, DL_UNBIND_REQ, DL_OUTSTATE, 0);
		return;
	}

	slp->sl_state = DL_UNBOUND;
	slp->sl_sap = 0;

	putnextctl1(RD(wq), M_FLUSH, FLUSHRW);
	dlokack(wq, mp, DL_UNBIND_REQ);

	lesetipq(slp->sl_lep);
}

static void
leireq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;
	dl_info_ack_t	*dlip;
	struct	ledladdr	*dlap;
	struct	ether_addr	*ep;
	int	size;

	slp = (struct lestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_INFO_REQ_SIZE) {
		dlerrorack(wq, mp, DL_INFO_REQ, DL_BADPRIM, 0);
		return;
	}

	/*
	 * Exchange current msg for a DL_INFO_ACK.
	 */
	size = sizeof (dl_info_ack_t) + LEADDRL + ETHERADDRL;
	if ((mp = mexchange(wq, mp, size, M_PCPROTO, DL_INFO_ACK)) == NULL)
		return;

	/*
	 * Fill in the DL_INFO_ACK fields and reply.
	 */
	dlip = (dl_info_ack_t *) mp->b_rptr;
	*dlip = leinfoack;
	dlip->dl_current_state = slp->sl_state;
	dlap = (struct ledladdr *) (mp->b_rptr + dlip->dl_addr_offset);
	dlap->dl_sap = slp->sl_sap;
	if (slp->sl_lep) {
		ether_copy(&slp->sl_lep->le_ouraddr, &dlap->dl_phys);
	} else {
		bzero((caddr_t) &dlap->dl_phys, ETHERADDRL);
	}
	ep = (struct ether_addr *) (mp->b_rptr + dlip->dl_brdcst_addr_offset);
	ether_copy(&etherbroadcastaddr, ep);

	qreply(wq, mp);
}

static void
leponreq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;

	slp = (struct lestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_PROMISCON_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PROMISCON_REQ, DL_BADPRIM, 0);
		return;
	}

	switch (((dl_promiscon_req_t *) mp->b_rptr)->dl_level) {
		case DL_PROMISC_PHYS:
			slp->sl_flags |= SLALLPHYS;
			break;

		case DL_PROMISC_SAP:
			slp->sl_flags |= SLALLSAP;
			break;

		case DL_PROMISC_MULTI:
			slp->sl_flags |= SLALLMULTI;
			break;

		default:
			dlerrorack(wq, mp, DL_PROMISCON_REQ,
				DL_NOTSUPPORTED, 0);
			return;
	}

	if (slp->sl_lep)
		(void) leinit(slp->sl_lep);

	if (slp->sl_lep)
		lesetipq(slp->sl_lep);

	dlokack(wq, mp, DL_PROMISCON_REQ);
}

static void
lepoffreq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;
	int	flag;

	slp = (struct lestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_PROMISCOFF_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PROMISCOFF_REQ, DL_BADPRIM, 0);
		return;
	}

	switch (((dl_promiscoff_req_t *) mp->b_rptr)->dl_level) {
		case DL_PROMISC_PHYS:
			flag = SLALLPHYS;
			break;

		case DL_PROMISC_SAP:
			flag = SLALLSAP;
			break;

		case DL_PROMISC_MULTI:
			flag = SLALLMULTI;
			break;

		default:
			dlerrorack(wq, mp, DL_PROMISCOFF_REQ,
				DL_NOTSUPPORTED, 0);
			return;
	}

	if ((slp->sl_flags & flag) == 0) {
		dlerrorack(wq, mp, DL_PROMISCOFF_REQ, DL_NOTENAB, 0);
		return;
	}

	slp->sl_flags &= ~flag;
	if (slp->sl_lep)
		(void) leinit(slp->sl_lep);

	if (slp->sl_lep)
		lesetipq(slp->sl_lep);

	dlokack(wq, mp, DL_PROMISCOFF_REQ);
}

static void
leemreq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;
	union	DL_primitives	*dlp;
	struct	ether_addr	*addrp;
	int	off;
	int	len, i;

	slp = (struct lestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_ENABMULTI_REQ_SIZE) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_BADPRIM, 0);
		return;
	}

	if (slp->sl_state == DL_UNATTACHED) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_OUTSTATE, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	len = dlp->enabmulti_req.dl_addr_length;
	off = dlp->enabmulti_req.dl_addr_offset;
	addrp = (struct ether_addr *) (mp->b_rptr + off);

	if ((len != ETHERADDRL) ||
		!MBLKIN(mp, off, len) ||
		((addrp->ether_addr_octet[0] & 01) == 0)) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_BADADDR, 0);
		return;
	}

	if ((slp->sl_mccount + 1) >= LEMAXMC) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_TOOMANY, 0);
		return;
	}

	/*
	 * Allocate table on first request.
	 */
	if (slp->sl_mctab == NULL)
		slp->sl_mctab = kmem_alloc(LEMCALLOC, KM_SLEEP);

	/*
	 * Check to see if the address is already in the table.
	 */
	for (i = 0; i < slp->sl_mccount; i++) {
		if (ether_cmp(&slp->sl_mctab[i], addrp) == 0) {
			dlokack(wq, mp, DL_ENABMULTI_REQ);
			return;
		}
	}

	slp->sl_mctab[slp->sl_mccount++] = *addrp;

	(void) leinit(slp->sl_lep);
	dlokack(wq, mp, DL_ENABMULTI_REQ);
}

static void
ledmreq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;
	union	DL_primitives	*dlp;
	struct	ether_addr	*addrp;
	int	off;
	int	len;
	int	i;

	slp = (struct lestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_DISABMULTI_REQ_SIZE) {
		dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_BADPRIM, 0);
		return;
	}

	if (slp->sl_state == DL_UNATTACHED) {
		dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_OUTSTATE, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	len = dlp->disabmulti_req.dl_addr_length;
	off = dlp->disabmulti_req.dl_addr_offset;
	addrp = (struct ether_addr *) (mp->b_rptr + off);

	if ((len != ETHERADDRL) || !MBLKIN(mp, off, len)) {
		dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_BADADDR, 0);
		return;
	}

	/*
	 * Find the address in the multicast table for this Stream
	 * and delete it by shifting all subsequent multicast
	 * table entries over one.
	 */
	for (i = 0; i < slp->sl_mccount; i++)
		if (ether_cmp(addrp, &slp->sl_mctab[i]) == 0) {
			bcopy((caddr_t) &slp->sl_mctab[i+1],
				(caddr_t) &slp->sl_mctab[i],
				((slp->sl_mccount - i) *
				sizeof (struct ether_addr)));
			slp->sl_mccount--;
			(void) leinit(slp->sl_lep);
			dlokack(wq, mp, DL_DISABMULTI_REQ);
			return;
		}
	dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_NOTENAB, 0);
}

static void
lepareq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;
	union	DL_primitives	*dlp;
	int	type;
	struct	le	*lep;
	struct	ether_addr	addr;

	slp = (struct lestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_PHYS_ADDR_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PHYS_ADDR_REQ, DL_BADPRIM, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	type = dlp->physaddr_req.dl_addr_type;
	lep = slp->sl_lep;

	if (lep == NULL) {
		dlerrorack(wq, mp, DL_PHYS_ADDR_REQ, DL_OUTSTATE, 0);
		return;
	}

	switch (type) {
		case	DL_FACT_PHYS_ADDR:
			localetheraddr((struct ether_addr *) NULL, &addr);
			break;

		case	DL_CURR_PHYS_ADDR:
			ether_copy(&lep->le_ouraddr, &addr);
			break;

		default:
			dlerrorack(wq, mp, DL_PHYS_ADDR_REQ,
				DL_NOTSUPPORTED, 0);
			return;
	}

	dlphysaddrack(wq, mp, &addr, ETHERADDRL);
}

static void
lespareq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;
	union	DL_primitives	*dlp;
	int	off;
	int	len;
	struct	ether_addr	*addrp;
	struct	le	*lep;

	slp = (struct lestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_SET_PHYS_ADDR_REQ_SIZE) {
		dlerrorack(wq, mp, DL_SET_PHYS_ADDR_REQ, DL_BADPRIM, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	len = dlp->set_physaddr_req.dl_addr_length;
	off = dlp->set_physaddr_req.dl_addr_offset;

	if (!MBLKIN(mp, off, len)) {
		dlerrorack(wq, mp, DL_SET_PHYS_ADDR_REQ, DL_BADPRIM, 0);
		return;
	}

	addrp = (struct ether_addr *) (mp->b_rptr + off);

	/*
	 * Error if length of address isn't right or the address
	 * specified is a multicast or broadcast address.
	 */
	if ((len != ETHERADDRL) ||
		((addrp->ether_addr_octet[0] & 01) == 1) ||
		(ether_cmp(addrp, &etherbroadcastaddr) == 0)) {
		dlerrorack(wq, mp, DL_SET_PHYS_ADDR_REQ, DL_BADADDR, 0);
		return;
	}

	/*
	 * Error if this stream is not attached to a device.
	 */
	if ((lep = slp->sl_lep) == NULL) {
		dlerrorack(wq, mp, DL_SET_PHYS_ADDR_REQ, DL_OUTSTATE, 0);
		return;
	}

	/*
	 * Set new interface local address and re-init device.
	 * This is destructive to any other streams attached
	 * to this device.
	 */
	ether_copy(addrp, &lep->le_ouraddr);
	(void) leinit(slp->sl_lep);

	dlokack(wq, mp, DL_SET_PHYS_ADDR_REQ);
}

static void
leudreq(queue_t *wq, mblk_t *mp)
{
	struct	lestr	*slp;
	register	struct	le	*lep;
	register	dl_unitdata_req_t	*dludp;
	mblk_t	*nmp;
	struct	ledladdr	*dlap;
	struct	ether_header	*headerp;
	ulong	off, len;
	ulong	sap;

	slp = (struct lestr *) wq->q_ptr;
	lep = slp->sl_lep;

	if (slp->sl_state != DL_IDLE) {
		dlerrorack(wq, mp, DL_UNITDATA_REQ, DL_OUTSTATE, 0);
		return;
	}

	dludp = (dl_unitdata_req_t *) mp->b_rptr;

	off = dludp->dl_dest_addr_offset;
	len = dludp->dl_dest_addr_length;

	/*
	 * Validate destination address format.
	 */
	if (!MBLKIN(mp, off, len) || (len != LEADDRL)) {
		dlerrorack(wq, mp, DL_UNITDATA_REQ, DL_BADADDR, 0);
		return;
	}

	/*
	 * Error if no M_DATA follows.
	 */
	nmp = mp->b_cont;
	if (nmp == NULL) {
		dluderrorind(wq, mp, mp->b_rptr + off, len, DL_BADDATA, 0);
		return;
	}

	dlap = (struct ledladdr *) (mp->b_rptr + off);

	/*
	 * Create ethernet header by either prepending it onto the
	 * next mblk if possible, or reusing the M_PROTO block if not.
	 */
	if ((DB_REF(nmp) == 1) &&
		(MBLKHEAD(nmp) >= sizeof (struct ether_header)) &&
		(((ulong) nmp->b_rptr & 0x1) == 0)) {
		nmp->b_rptr -= sizeof (struct ether_header);
		headerp = (struct ether_header *) nmp->b_rptr;
		ether_copy(&dlap->dl_phys, &headerp->ether_dhost);
		ether_copy(&lep->le_ouraddr, &headerp->ether_shost);
		sap = dlap->dl_sap;
		freeb(mp);
		mp = nmp;
	} else {
		DB_TYPE(mp) = M_DATA;
		headerp = (struct ether_header *) mp->b_rptr;
		mp->b_wptr = mp->b_rptr + sizeof (struct ether_header);
		ether_copy(&dlap->dl_phys, &headerp->ether_dhost);
		ether_copy(&lep->le_ouraddr, &headerp->ether_shost);
		sap = dlap->dl_sap;
	}

	/*
	 * sap value in the range [0-1500] are treated equivalent
	 * and represent a desire to use 802.3 mode. Therefore compute
	 * the length and put it in the type field in header
	 */
	if ((sap <= ETHERMTU) || (slp->sl_sap == 0))
		headerp->ether_type =
			(msgsize(mp) - sizeof (struct ether_header));
	else
		headerp->ether_type = sap;

	(void) lestart(wq, mp, lep);

	/*
	 * Delay for 90 usecs on first time to enable the interrupt
	 * to switch from AUI to TPE on an LCAR xmit error. On faster
	 * machines, leemreq() can call leinit() and reset the chip
	 * before the interface can be switched.
	 */
	if (!lep->le_oerrors && !lep->le_opackets)
		drv_usecwait(90);
}

/*
 * Start transmission.
 * Return zero on success,
 * otherwise put msg on wq, set 'want' flag and return nonzero.
 */
static
lestart(queue_t *wq, register mblk_t *mp, register struct le *lep)
{
	volatile	struct	lmd	*tmdp, *ntmdp;
	struct	lmd	t;
	struct	lebuf	*lbp;
	mblk_t	*nmp = NULL;
	int	len;
	u_long	a;
	u_int	sbound, soff;
	int	i;
	int	flags;

	TRACE_1(TR_FAC_LE, TR_LE_START_START, "lestart start:  wq %X", wq);

	flags = lep->le_flags;

	if ((flags & (LERUNNING|LEPROMISC)) != LERUNNING) {
		if (!(flags & LERUNNING)) {
			(void) putbq(wq, mp);
			return (1);
		}
		if (flags & LEPROMISC) {
			if ((nmp = copymsg(mp)) == NULL)
				lep->le_allocbfail++;
		}
	}

	len = msgsize(mp);
	if (len > ETHERMAX) {
		lerror(lep->le_dip, "msg too big:  %d", len);
		lep->le_oerrors++;
		freemsg(mp);
		if (nmp)
			freemsg(nmp);
		TRACE_1(TR_FAC_LE, TR_LE_START_END,
			"lestart end:  wq %X", wq);
		return (0);
	}

	a = (u_long) mp->b_rptr;

	/*
	 * Only copy msg to local tbuf under certain conditions.
	 */
	if (mp->b_cont ||
		(flags & LESLAVE) ||
		!(flags & LESUN4C) ||
		(a < LETOP16MEG) ||
		((len < ETHERMIN) &&
		!SAMEMMUPAGE(a, (a + ETHERMIN)))) {

		/*
		 * Get a local tbuf.
		 */
		if ((lbp = legetbuf(lep, 1)) == NULL) {
			lep->le_notbufs++;
			(void) putbq(wq, mp);
			lep->le_wantw = 1;
			if (nmp)
				freemsg(nmp);
			TRACE_1(TR_FAC_LE, TR_LE_START_END,
				"lestart end:  wq %X", wq);
			return (1);
		}

		/*
		 * Copy msg into tbuf and free the original msg.
		 */
		a = (u_long) lbp->lb_buf;
		if (mp->b_cont == NULL) {
			sbound = (u_int) mp->b_rptr & ~LEBURSTMASK;
			soff = (u_int) mp->b_rptr - sbound;
			bcopy((caddr_t) sbound, (caddr_t) a,
			    LEROUNDUP(len + soff, LEBURSTSIZE));
			a += soff;
			freemsg(mp);
		} else if (mp->b_cont->b_cont == NULL) {
			sbound = (u_int) mp->b_cont->b_rptr & ~LEBURSTMASK;
			soff = (u_int) mp->b_cont->b_rptr - sbound;
			a = LEROUNDUP(a + MBLKL(mp), LEBURSTSIZE);
			bcopy((caddr_t) sbound, (caddr_t) a,
			    LEROUNDUP(MBLKL(mp->b_cont) + soff, LEBURSTSIZE));
			a = a + soff - MBLKL(mp);
			bcopy((caddr_t) mp->b_rptr, (caddr_t) a, MBLKL(mp));
			freemsg(mp);
		} else
			(void) mcopymsg(mp, (u_char *) a);
		mp = NULL;
	}

	/*
	 * Pad out to ETHERMIN.
	 */
	if (len < ETHERMIN)
		len = ETHERMIN;

	/*
	 * Translate buffer kernel virtual address
	 * into IO address.
	 */
	if ((flags & LESLAVE) || !(flags & LESUN4C))
		a = LEBUFIOADDR(lep, a);

	/*
	 * Craft a new TMD for this buffer.
	 */
	t.lmd_ladr = (u_short) a;
	t.lmd_hadr = (int) a >> 16;
	t.lmd_bcnt = -len;
	t.lmd_flags3 = 0;
	t.lmd_flags = LMD_STP | LMD_ENP | LMD_OWN;

	/*
	 * Acquire xmit lock now and hold it until done.
	 */
	mutex_enter(&lep->le_xmitlock);

	/*
	 * Allocate a tmd and increment pointer to next tmd.
	 * Never allow "next" pointer to point to an in-use tmd.
	 */
	tmdp = lep->le_tnextp;
	ntmdp = NEXTTMD(lep, tmdp);
	if (ntmdp == lep->le_tcurp) {	/* out of tmds */
		lep->le_notmds++;
		mutex_exit(&lep->le_xmitlock);
		if (nmp)
			freemsg(nmp);
		if (mp) {
			(void) putbq(wq, mp);
			lep->le_wantw = 1;
			TRACE_1(TR_FAC_LE, TR_LE_START_END,
				"lestart end:  wq %X", wq);
			return (1);
		} else {
			lefreebuf(lbp);
			TRACE_1(TR_FAC_LE, TR_LE_START_END,
				"lestart end:  wq %X", wq);
			return (0);
		}
	}

	/*
	 * Save msg or tbuf to free later.
	 */
	i = tmdp - lep->le_tmdp;
	if (mp)
		lep->le_tmblkp[i] = mp;
	else
		lep->le_tbufp[i] = lbp;

	/*
	 * Sync the buffer so the device sees it.
	 */
	if ((flags & LESUN4C) == 0)
		LESYNCBUF(lep, a, len, DDI_DMA_SYNC_FORDEV);

	/*
	 * Write out the new TMD.
	 */
	*((longlong_t *)tmdp) = *((longlong_t *)&t);

	/*
	 * Update our TMD ring pointer.
	 */
	lep->le_tnextp = ntmdp;

	/*
	 * Sync the TMD.
	 */
	LESYNCIOPB(lep, tmdp, sizeof (struct lmd), DDI_DMA_SYNC_FORDEV);

	/*
	 * Bang the chip.
	 */
	lep->le_regsp->lance_csr = LANCE_TDMD | LANCE_INEA;

	/*
	 * Now it's ok to release the lock.
	 */
	mutex_exit(&lep->le_xmitlock);

	/*
	 * Loopback a broadcast packet.
	 */
	if ((flags & LEPROMISC) && nmp)
		lesendup(lep, nmp, lepaccept);

	TRACE_1(TR_FAC_LE, TR_LE_START_END, "lestart end:  wq %X", wq);
	return (0);
}

static u_int
leintr(caddr_t arg)
{
	volatile	struct	lmd	*rmdp;
	volatile	u_short	csr0;
	int	serviced = 0;
	ulong_t le_inits;
	struct le *lep = (struct le *)arg;

	mutex_enter(&lep->le_intrlock);

	le_inits = lep->le_inits;

	csr0 = lep->le_regsp->lance_csr;

	TRACE_2(TR_FAC_LE, TR_LE_INTR_START,
		"leintr start:  lep %X csr %X", lep, csr0);

	if (lep->le_intr_flag) {
		serviced = 1;
		lep->le_intr_flag = 0;
	}
	if ((csr0 & LANCE_INTR) == 0) {
		mutex_exit(&lep->le_intrlock);
		if (lep->le_intr && (*lep->le_intr)(lep->le_arg)) {
			(void) leinit(lep);
			serviced = 1;
		}
		TRACE_2(TR_FAC_LE, TR_LE_INTR_END,
			"leintr end:  lep %X serviced %d",
			lep, serviced);
		return (serviced);
	}

	/*
	 * Clear RINT/TINT .
	 */
	lep->le_regsp->lance_csr =
		(csr0 & (LANCE_RINT | LANCE_TINT)) | LANCE_INEA;
	serviced = 1;

#ifdef	notdef
	if (!(lep->le_flags & LERUNNING)) {
		mutex_exit(&lep->le_intrlock);
		leuninit(lep);
		mutex_enter(&lep->le_intrlock);
		goto done;
	}
#endif	notdef

	/*
	 * Check for receive activity.
	 * One packet per RMD.
	 */
	if (csr0 & LANCE_RINT) {
		rmdp = lep->le_rnextp;

		/*
		 * Sync RMD before looking at it.
		 */
		LESYNCIOPB(lep, rmdp, sizeof (struct lmd),
			DDI_DMA_SYNC_FORCPU);

		/*
		 * Loop through each RMD.
		 */
		while ((rmdp->lmd_flags & LMD_OWN) == 0) {
			leread(lep, rmdp);

			/*
			 * if the chip has been reinitialized
			 * then we break out and handle any
			 * new packets in the next interrupt
			 */
			if (le_inits < lep->le_inits)
				break;

			/*
			 * Give the descriptor and associated
			 * buffer back to the chip.
			 */
			rmdp->lmd_mcnt = 0;
			rmdp->lmd_flags = LMD_OWN;

			/*
			 * Sync the RMD after writing it.
			 */
			LESYNCIOPB(lep, rmdp, sizeof (struct lmd),
				DDI_DMA_SYNC_FORDEV);

			/*
			 * Increment to next RMD.
			 */
			lep->le_rnextp = rmdp = NEXTRMD(lep, rmdp);

			/*
			 * Sync the next RMD before looking at it.
			 */
			LESYNCIOPB(lep, rmdp, sizeof (struct lmd),
				DDI_DMA_SYNC_FORCPU);
		}
	}

	/*
	 * Check for transmit activity.
	 * One packet per TMD.
	 */
	if (csr0 & LANCE_TINT)
		lereclaim(lep);

	/*
	 * Check for errors not specifically related
	 * to transmission or reception.
	 */
	if ((csr0 & (LANCE_BABL|LANCE_MERR|LANCE_MISS|LANCE_TXON|LANCE_RXON))
		!= (LANCE_TXON|LANCE_RXON)) {
		(void) le_chip_error(lep);
	}

done:

	/*
	 * Read back register to flush write buffers.
	 */
	csr0 = lep->le_regsp->lance_csr;

	mutex_exit(&lep->le_intrlock);
	TRACE_2(TR_FAC_LE, TR_LE_INTR_END,
		"leintr end:  lep %X serviced %d", lep, serviced);
	return (serviced);
}

/*
 * Transmit completion reclaiming.
 */
static void
lereclaim(struct le *lep)
{
	volatile	struct	lmd	*tmdp;
	int	flags;
	int	i;

	tmdp = lep->le_tcurp;

	/*
	 * Sync TMD before looking at it.
	 */
	LESYNCIOPB(lep, tmdp, sizeof (struct lmd), DDI_DMA_SYNC_FORCPU);

	/*
	 * Loop through each TMD.
	 */
	while ((((flags = tmdp->lmd_flags) &
		(LMD_OWN | TMD_INUSE)) == 0) &&
		(tmdp != lep->le_tnextp)) {

		/*
		 * Keep defer/retry statistics.
		 */
		if (flags & TMD_DEF)
			lep->le_defer++;
		if (flags & TMD_ONE)
			lep->le_collisions++;
		else if (flags & TMD_MORE)
			lep->le_collisions += 2;

		/*
		 * Check for transmit errors and keep output stats.
		 */
		if (tmdp->lmd_flags3 & TMD_ANYERROR) {
			lep->le_oerrors++;
			if (le_xmit_error(lep, (struct lmd *) tmdp))
				return;
		} else
			lep->le_opackets++;

		/*
		 * Free msg or buffer.
		 */
		i = tmdp - lep->le_tmdp;
		if (lep->le_tmblkp[i]) {
			freemsg(lep->le_tmblkp[i]);
			lep->le_tmblkp[i] = NULL;
		} else if (lep->le_tbufp[i]) {
			lefreebuf(lep->le_tbufp[i]);
			lep->le_tbufp[i] = NULL;
		}

		tmdp = NEXTTMD(lep, tmdp);

		/*
		 * Sync TMD before looking at it.
		 */
		LESYNCIOPB(lep, tmdp, sizeof (struct lmd),
			DDI_DMA_SYNC_FORCPU);
	}

	lep->le_tcurp = tmdp;

	/*
	 * Check for any msgs that were queued
	 * due to out-of-tmd condition.
	 */
	if (lep->le_wantw)
		lewenable(lep);
}

/*
 * Start xmit on any msgs previously enqueued on any write queues.
 */
static void
lewenable(struct le *lep)
{
	struct	lestr	*slp;
	queue_t	*wq;

	/*
	 * Order of wantw accesses is important.
	 */
	do {
		lep->le_wantw = 0;
		for (slp = lestrup; slp; slp = slp->sl_nextp)
			if ((slp->sl_lep == lep) &&
				((wq = WR(slp->sl_rq))->q_first))
				qenable(wq);
	} while (lep->le_wantw);
}

/*
 * Test upstream destination sap and address match.
 */
static struct lestr *
leaccept(register struct lestr *slp, register struct le *lep,
    int type, struct ether_addr *addrp)
{
	int	sap;
	int	flags;

	for (; slp; slp = slp->sl_nextp) {
		sap = slp->sl_sap;
		flags = slp->sl_flags;

		if ((slp->sl_lep == lep) && LESAPMATCH(sap, type, flags))
			if ((ether_cmp(addrp, &lep->le_ouraddr) == 0) ||
				(ether_cmp(addrp, &etherbroadcastaddr) == 0) ||
				(flags & SLALLPHYS) ||
				lemcmatch(slp, addrp))
				return (slp);
	}

	return (NULL);
}

/*
 * Test upstream destination sap and address match for SLALLPHYS only.
 */
/* ARGSUSED3 */
static struct lestr *
lepaccept(register struct lestr *slp, register struct le *lep,
    int type, struct ether_addr *addrp)
{
	int	sap;
	int	flags;

	for (; slp; slp = slp->sl_nextp) {
		sap = slp->sl_sap;
		flags = slp->sl_flags;

		if ((slp->sl_lep == lep) &&
			LESAPMATCH(sap, type, flags) &&
			(flags & SLALLPHYS))
			return (slp);
	}

	return (NULL);
}

/*
 * Set or clear the device ipq pointer.
 * XXX Assumes IP is SLFAST.
 */
static void
lesetipq(struct le *lep)
{
	struct	lestr	*slp;
	int	ok = 1;
	queue_t	*ipq = NULL;

	rw_enter(&lestruplock, RW_READER);

	for (slp = lestrup; slp; slp = slp->sl_nextp)
		if (slp->sl_lep == lep) {
			if (slp->sl_flags & (SLALLPHYS|SLALLSAP))
				ok = 0;
			if (slp->sl_sap == ETHERTYPE_IP)
				if (ipq == NULL)
					ipq = slp->sl_rq;
				else
					ok = 0;
		}

	rw_exit(&lestruplock);

	if (ok)
		lep->le_ipq = ipq;
	else
		lep->le_ipq = NULL;
}

static void
leread(struct le *lep, volatile struct lmd *rmdp)
{
	register	int	i;
	register	mblk_t	*mp;
	register	struct	lebuf	*lbp, *nlbp;
	struct	ether_header	*ehp;
	queue_t	*ipq;
	u_long	bufp;
	int	len;
	u_int	sbound, soff;
	u_long	a;
#ifdef NETDEBUGGER
	struct ip *iphp;		/* ip header pointer */
	struct udphdr *udphp;		/* udp header pointer */
#endif /* NETDEBUGGER */

	TRACE_0(TR_FAC_LE, TR_LE_READ_START, "leread start");

	lep->le_ipackets++;

	/*
	 * Check for packet errors.
	 */
	if ((rmdp->lmd_flags & ~RMD_OFLO) != (LMD_STP | LMD_ENP)) {
		le_rcv_error(lep, (struct lmd *) rmdp);
		lep->le_ierrors++;
		TRACE_0(TR_FAC_LE, TR_LE_READ_END, "leread end");
		return;
	}

	i = rmdp - lep->le_rmdp;
	lbp = lep->le_rbufp[i];

	bufp = (u_long) lbp->lb_buf + LEHEADROOM;
	len = rmdp->lmd_mcnt - ETHERFCSL;

	if (len < ETHERMIN) {
		TRACE_0(TR_FAC_LE, TR_LE_READ_END, "leread end");
		return;
	}

	/*
	 * Sync the received buffer before looking at it.
	 */

	LESYNCBUF(lep, bufp, len, DDI_DMA_SYNC_FORKERNEL);

	/*
	 * If a (MASTER or sun4c) and buffers are available,
	 * then "loan up".  Otherwise allocb a new mblk and copy.
	 */
	if ((!(lep->le_flags & LESLAVE) || (lep->le_flags & LESUN4C)) &&
		(nlbp = legetbuf(lep, 0))) {

		if ((mp = desballoc((unsigned char *) bufp, len, BPRI_LO,
			&lbp->lb_frtn)) == NULL) {
			lep->le_allocbfail++;
			lep->le_ierrors++;
			if (ledebug)
				lerror(lep->le_dip, "desballoc failed");
			lefreebuf(nlbp);
			TRACE_0(TR_FAC_LE, TR_LE_READ_END, "leread end");
			return;
		}
		mp->b_wptr += len;

		/* leave the new rbuf with the current rmd. */
		a = (u_long) nlbp->lb_buf + LEHEADROOM;
		a = LEBUFIOADDR(lep, a);
		rmdp->lmd_ladr = (u_short) a;
		rmdp->lmd_hadr = (long) a >> 16;
		lep->le_rbufp[i] = nlbp;

	} else {

		lep->le_norbufs++;

		/* allocate and aligned-copy */
		if ((mp = allocb(len + (3 * LEBURSTSIZE), BPRI_LO))
			== NULL) {
			lep->le_ierrors++;
			lep->le_allocbfail++;
			if (ledebug)
				lerror(lep->le_dip, "allocb fail");
			TRACE_0(TR_FAC_LE, TR_LE_READ_END, "leread end");
			return;
		}

		mp->b_rptr = (u_char*)
			LEROUNDUP((u_int)mp->b_rptr, LEBURSTSIZE);
		sbound = bufp & ~LEBURSTMASK;
		soff = bufp - sbound;
		bcopy((caddr_t) sbound, (caddr_t) mp->b_rptr,
			LEROUNDUP(len + soff, LEBURSTSIZE));
		mp->b_rptr += soff;
		mp->b_wptr = mp->b_rptr + len;
	}

	ehp = (struct ether_header *) bufp;
	ipq = lep->le_ipq;

#ifdef NETDEBUGGER
	iphp = (struct ip *)  (((char *)ehp) + sizeof (struct ether_header));
	udphp = (struct udphdr *)  (((char *)iphp) + sizeof (struct ip));

	if ((ehp->ether_type == ETHERTYPE_IP) &&
	    (iphp->ip_p == IPPROTO_UDP) &&
	    (udphp->uh_dport == DEBUG_PORT_NUM)) {
		debug_enter("Network request to enter debugger");
	} else {
#endif /* NETDEBUGGER */

	/*
	 * IP shortcut
	 */
	if ((ehp->ether_type == ETHERTYPE_IP) &&
		((ehp->ether_dhost.ether_addr_octet[0] & 01) == 0) &&
		(ipq) &&
		canput(ipq->q_next)) {
		mp->b_rptr += sizeof (struct ether_header);
		putnext(ipq, mp);
	} else {
		/* Strip the PADs for 802.3 */
		if (ehp->ether_type + sizeof (struct ether_header) < ETHERMIN)
			mp->b_wptr = mp->b_rptr
					+ sizeof (struct ether_header)
					+ ehp->ether_type;
		lesendup(lep, mp, leaccept);
	}
#ifdef NETDEBUGGER
	}
#endif /* NETDEBUGGER */

	TRACE_0(TR_FAC_LE, TR_LE_READ_END, "leread end");
}

/*
 * Send packet upstream.
 * Assume mp->b_rptr points to ether_header.
 */
static void
lesendup(struct le *lep, mblk_t *mp, struct lestr *(*acceptfunc)())
{
	int	type;
	struct	ether_addr	*dhostp, *shostp;
	struct	lestr	*slp, *nslp;
	mblk_t	*nmp;
	ulong	isgroupaddr;

	TRACE_0(TR_FAC_LE, TR_LE_SENDUP_START, "lesendup start");

	dhostp = &((struct ether_header *) mp->b_rptr)->ether_dhost;
	shostp = &((struct ether_header *) mp->b_rptr)->ether_shost;
	type = ((struct ether_header *) mp->b_rptr)->ether_type;

	isgroupaddr = dhostp->ether_addr_octet[0] & 01;

	/*
	 * While holding a reader lock on the linked list of streams structures,
	 * attempt to match the address criteria for each stream
	 * and pass up the raw M_DATA ("fastpath") or a DL_UNITDATA_IND.
	 */

	rw_enter(&lestruplock, RW_READER);

	if ((slp = (*acceptfunc)(lestrup, lep, type, dhostp)) == NULL) {
		rw_exit(&lestruplock);

		/*
		 * On MACIO-based sun4m machines, network collisions in
		 * conjunction with back-to-back Inter-packet gap transmissions
		 * that violate the Ethernet/IEEE 802.3 specification may cause
		 * the NCR92C990 Lance internal fifo pointers to lose sync.
		 * This can result in one or more bytes of data prepended to
		 * the ethernet header from the previous packet that is written
		 * to memory.
		 * The upper layer protocol will disregard those packets and
		 * ethernet interface will have appeared to be hung.
		 * Check here for this condition since the leaccept routine
		 * did not find a match.
		 */
		if (lep->le_init && ((*acceptfunc) == &leaccept)) {
			register unsigned char *dp;
			int eaddr_ok = 1;

			/*
			 * Verify this condition by checking to see
			 * if the broadcast ethernet address starts in
			 * byte positions 1 - 4 of the header
			 */
			dp = (unsigned char *)&dhostp->ether_addr_octet[0];

			if (!(bcmp((char *)&dp[1],
					(char *)&etherbroadcastaddr, 6)) ||
			    !(bcmp((char *)&dp[2],
					(char *)&etherbroadcastaddr, 6)) ||
			    !(bcmp((char *)&dp[3],
					(char *)&etherbroadcastaddr, 6)) ||
			    !(bcmp((char *)&dp[4],
					(char *)&etherbroadcastaddr, 6)))
				eaddr_ok = 0;

			/*
			 * If the dest ethernet address is misaligned then
			 * reset the MACIO Lance chip
			 */
			if (!eaddr_ok) {
				if (ledebug)
					lerror(lep->le_dip,
				"Invalid dest address: %x:%x:%x:%x:%x:%x\n",
						dp[0], dp[1], dp[2],
						dp[3], dp[4], dp[5]);
				mutex_exit(&lep->le_intrlock);
				(void) leinit(lep);
				mutex_enter(&lep->le_intrlock);
			}
		}
		freemsg(mp);
		TRACE_0(TR_FAC_LE, TR_LE_SENDUP_END, "lesendup end");
		return;
	}

	/*
	 * Loop on matching open streams until (*acceptfunc)() returns NULL.
	 */
	for (; nslp = (*acceptfunc)(slp->sl_nextp, lep, type, dhostp);
		slp = nslp)
		if (canput(slp->sl_rq->q_next))
			if (nmp = dupmsg(mp)) {
				if ((slp->sl_flags & SLFAST) && !isgroupaddr) {
					nmp->b_rptr +=
						sizeof (struct ether_header);
					putnext(slp->sl_rq, nmp);
				} else if (slp->sl_flags & SLRAW)
					putnext(slp->sl_rq, nmp);
				else if ((nmp = leaddudind(lep, nmp, shostp,
						dhostp, type, isgroupaddr)))
						putnext(slp->sl_rq, nmp);
			}
			else
				lep->le_allocbfail++;
		else
			lep->le_nocanput++;


	/*
	 * Do the last one.
	 */
	if (canput(slp->sl_rq->q_next)) {
		if ((slp->sl_flags & SLFAST) && !isgroupaddr) {
			mp->b_rptr += sizeof (struct ether_header);
			putnext(slp->sl_rq, mp);
		} else if (slp->sl_flags & SLRAW)
			putnext(slp->sl_rq, mp);
		else if ((mp = leaddudind(lep, mp, shostp, dhostp,
			type, isgroupaddr)))
			putnext(slp->sl_rq, mp);
	} else {
		freemsg(mp);
		lep->le_nocanput++;
	}

	rw_exit(&lestruplock);
	TRACE_0(TR_FAC_LE, TR_LE_SENDUP_END, "lesendup end");
}

/*
 * Prefix msg with a DL_UNITDATA_IND mblk and return the new msg.
 */
static mblk_t *
leaddudind(struct le *lep, mblk_t *mp, struct ether_addr *shostp,
    struct ether_addr *dhostp, int type, ulong isgroupaddr)
{
	dl_unitdata_ind_t	*dludindp;
	struct	ledladdr	*dlap;
	mblk_t	*nmp;
	int	size;

	TRACE_0(TR_FAC_LE, TR_LE_ADDUDIND_START, "leaddudind start");

	mp->b_rptr += sizeof (struct ether_header);

	/*
	 * Allocate an M_PROTO mblk for the DL_UNITDATA_IND.
	 */
	size = sizeof (dl_unitdata_ind_t) + LEADDRL + LEADDRL;
	nmp = allocb(LEROUNDUP(LEHEADROOM + size, sizeof (double)), BPRI_LO);
	if (nmp == NULL) {
		lep->le_allocbfail++;
		lep->le_ierrors++;
		if (ledebug)
			lerror(lep->le_dip, "allocb failed");
		freemsg(mp);
		TRACE_0(TR_FAC_LE, TR_LE_ADDUDIND_END, "leaddudind end");
		return (NULL);
	}
	DB_TYPE(nmp) = M_PROTO;
	nmp->b_wptr = nmp->b_datap->db_lim;
	nmp->b_rptr = nmp->b_wptr - size;

	/*
	 * Construct a DL_UNITDATA_IND primitive.
	 */
	dludindp = (dl_unitdata_ind_t *) nmp->b_rptr;
	dludindp->dl_primitive = DL_UNITDATA_IND;
	dludindp->dl_dest_addr_length = LEADDRL;
	dludindp->dl_dest_addr_offset = sizeof (dl_unitdata_ind_t);
	dludindp->dl_src_addr_length = LEADDRL;
	dludindp->dl_src_addr_offset = sizeof (dl_unitdata_ind_t) + LEADDRL;
	dludindp->dl_group_address = isgroupaddr;

	dlap = (struct ledladdr *) (nmp->b_rptr + sizeof (dl_unitdata_ind_t));
	ether_copy(dhostp, &dlap->dl_phys);
	dlap->dl_sap = (u_short) type;

	dlap = (struct ledladdr *) (nmp->b_rptr + sizeof (dl_unitdata_ind_t)
		+ LEADDRL);
	ether_copy(shostp, &dlap->dl_phys);
	dlap->dl_sap = (u_short) type;

	/*
	 * Link the M_PROTO and M_DATA together.
	 */
	nmp->b_cont = mp;
	TRACE_0(TR_FAC_LE, TR_LE_ADDUDIND_END, "leaddudind end");
	return (nmp);
}

/*
 * Return TRUE if the given multicast address is one
 * of those that this particular Stream is interested in.
 */
static
lemcmatch(register struct lestr *slp, register struct ether_addr *addrp)
{
	register	struct	ether_addr	*mctab;
	register	int	mccount;
	register	int	i;

	/*
	 * Return FALSE if not a multicast address.
	 */
	if (!(addrp->ether_addr_octet[0] & 01))
		return (0);

	/*
	 * Check if all multicasts have been enabled for this Stream
	 */
	if (slp->sl_flags & SLALLMULTI)
		return (1);

	/*
	 * Return FALSE if no multicast addresses enabled for this Stream.
	 */
	if (slp->sl_mccount == 0)
		return (0);

	/*
	 * Otherwise, find it in the table.
	 */

	mccount = slp->sl_mccount;
	mctab = slp->sl_mctab;

	for (i = 0; i < mccount; i++)
		if (!ether_cmp(addrp, &mctab[i]))
			return (1);

	return (0);
}

/*
 * Initialize chip and driver.
 * Return 0 on success, nonzero on error.
 */
static int
leinit(register struct le *lep)
{
	volatile	struct	lance_init_block	*ibp;
	volatile	struct	lanceregs	*regsp;
	struct	lebuf	*lbp;
	struct	lestr	*slp;
	struct	leops	*lop;
	u_long	a;
	int	i;
#ifdef LE_SYNC_INIT
	int	leflags  = lep->le_flags;
#endif /* LE_SYNC_INIT */

	TRACE_1(TR_FAC_LE, TR_LE_INIT_START,
		"leinit start:  lep %X", lep);

	if (lep->le_flags & LESUSPENDED)
		ddi_dev_is_needed(lep->le_dip, 0, 1);

	mutex_enter(&lep->le_intrlock);
	rw_enter(&lestruplock, RW_WRITER);
	mutex_enter(&lep->le_xmitlock);

	lep->le_flags = 0;
	lep->le_wantw = 0;
	lep->le_inits++;
	lep->le_intr_flag = 1;		/* fix for race condition on fusion */

	/*
	 * Stop the chip.
	 */
	regsp = lep->le_regsp;

	/*
	 * An ugly anti-DDI hack for performance.
	 */
	if (((cputype & CPU_ARCH) == SUN4C_ARCH) ||
		((cputype & CPU_ARCH) == SUN4E_ARCH))
		lep->le_flags |= LESUN4C;

	/*
	 * Set device-specific information here
	 * before calling leallocthings().
	 */
	lop = (struct leops *) leopsfind(lep->le_dip);
	if (lop) {
		if (lop->lo_flags & LOSLAVE)
			lep->le_flags |= LESLAVE;
		lep->le_membase = lop->lo_base;
		lep->le_memsize = lop->lo_size;
		lep->le_init = lop->lo_init;
		lep->le_intr = lop->lo_intr;
		lep->le_arg = lop->lo_arg;
	}

	/*
	 * Reject this device if it's a Bus Master in a slave-only slot.
	 */
	if ((ddi_slaveonly(lep->le_dip) == DDI_SUCCESS) &&
		(!(lep->le_flags & LESLAVE))) {
		lerror(lep->le_dip,
			"this card won't work in a slave-only slot");
		goto done;
	}

#ifdef LE_SYNC_INIT
	if ((leflags & LERUNNING) && lep->le_init && le_sync_init) {
		volatile struct	lmd	*tmdp;
		/*
		 * if running and any pending writes
		 * wait for any potential dma to finish
		 */
		tmdp = lep->le_tcurp;

		while (tmdp != lep->le_tnextp) {
			LESYNCIOPB(lep, tmdp, sizeof (struct lmd),
				DDI_DMA_SYNC_FORCPU);
			while (tmdp->lmd_flags & (LMD_OWN | TMD_INUSE))
				drv_usecwait(100);
			tmdp = NEXTTMD(lep, tmdp);
		}
	}
#endif /* LE_SYNC_INIT */

	/*
	 * Allocate data structures.
	 */
	leallocthings(lep);

	/*
	 * MACIO B.1 requires E_CSR reset before INIT.
	 */
	if (lep->le_init)
		(*lep->le_init)(lep->le_arg, lep->le_tpe, &lep->le_bufhandle);

	/* Access LANCE registers after DMA2 is initialized. ESC #9879  */

	regsp->lance_rap = LANCE_CSR0;
	regsp->lance_csr = LANCE_STOP;

	/*
	 * Free any pending buffers or mgs.
	 */
	for (i = 0; i < 128; i++) {
		if (lep->le_tbufp[i]) {
			lefreebuf(lep->le_tbufp[i]);
			lep->le_tbufp[i] = NULL;
		}
		if (lep->le_tmblkp[i]) {
			freemsg(lep->le_tmblkp[i]);
			lep->le_tmblkp[i] = NULL;
		}
		if (lep->le_rbufp[i]) {
			lefreebuf(lep->le_rbufp[i]);
			lep->le_rbufp[i] = NULL;
		}
	}


	/*
	 * Reset RMD and TMD 'walking' pointers.
	 */
	lep->le_rnextp = lep->le_rmdp;
	lep->le_tcurp = lep->le_tmdp;
	lep->le_tnextp = lep->le_tmdp;

	/*
	 * Construct the LANCE initialization block.
	 */

	ibp = lep->le_ibp;
	bzero((caddr_t) ibp, sizeof (struct lance_init_block));

	/*
	 * Mode word 0 should be all zeros except
	 * possibly for the promiscuous mode bit.
	 */
	ibp->ib_prom = 0;
	for (slp = lestrup; slp; slp = slp->sl_nextp)
		if ((slp->sl_lep == lep) && (slp->sl_flags & SLALLPHYS)) {
			ibp->ib_prom = 1;
			lep->le_flags |= LEPROMISC;
			break;
		}

	/*
	 * Set our local individual ethernet address.
	 */
	ibp->ib_padr[0] = lep->le_ouraddr.ether_addr_octet[1];
	ibp->ib_padr[1] = lep->le_ouraddr.ether_addr_octet[0];
	ibp->ib_padr[2] = lep->le_ouraddr.ether_addr_octet[3];
	ibp->ib_padr[3] = lep->le_ouraddr.ether_addr_octet[2];
	ibp->ib_padr[4] = lep->le_ouraddr.ether_addr_octet[5];
	ibp->ib_padr[5] = lep->le_ouraddr.ether_addr_octet[4];

	/*
	 * Set up multicast address filter by passing all multicast
	 * addresses through a crc generator, and then using the
	 * high order 6 bits as a index into the 64 bit logical
	 * address filter. The high order two bits select the word,
	 * while the rest of the bits select the bit within the word.
	 */
	for (slp = lestrup; slp; slp = slp->sl_nextp)
		if (slp->sl_lep == lep) {
			if ((slp->sl_mccount == 0) &&
				!(slp->sl_flags & SLALLMULTI))
				continue;

			if (slp->sl_flags & SLALLMULTI) {
				for (i = 0; i < 4; i++) {
					ibp->ib_ladrf[i] = 0xffff;
				}
				break;	/* All bits are already on */
			}

			for (i = 0; i < slp->sl_mccount; i++) {
				register u_char *cp;
				register u_long crc;
				register u_long c;
				register int len;
				int	j;

				cp = (unsigned char *) &slp->sl_mctab[i];
				c = *cp;
				crc = (u_long) 0xffffffff;
				len = 6;
				while (len-- > 0) {
					c = *cp;
					for (j = 0; j < 8; j++) {
						if ((c & 0x01) ^ (crc & 0x01)) {
							crc >>= 1;
							/* polynomial */
							crc = crc ^ 0xedb88320;
						} else
							crc >>= 1;
						c >>= 1;
					}
					cp++;
				}
				/* Just want the 6 most significant bits. */
				crc = crc >> 26;

				/*
				 * Turn on the corresponding bit
				 * in the address filter.
				 */
				ibp->ib_ladrf[crc >> 4] |= 1 << (crc & 0xf);
			}
		}
	a = LEIOPBIOADDR(lep, lep->le_rmdp);
	ibp->ib_rdrp.lr_laddr = a;
	ibp->ib_rdrp.lr_haddr = a >> 16;
	ibp->ib_rdrp.lr_len   = lep->le_nrmdp2;

	a = LEIOPBIOADDR(lep, lep->le_tmdp);
	ibp->ib_tdrp.lr_laddr = a;
	ibp->ib_tdrp.lr_haddr = a >> 16;
	ibp->ib_tdrp.lr_len   = lep->le_ntmdp2;

	/*
	 * Clear all descriptors.
	 */
	bzero((caddr_t) lep->le_rmdp,
			(u_int) (lep->le_nrmds * sizeof (struct lmd)));
	bzero((caddr_t) lep->le_tmdp,
			(u_int) (lep->le_ntmds * sizeof (struct lmd)));

	/*
	 * Hang out receive buffers.
	 */
	for (i = 0; i < lep->le_nrmds; i++) {
		if ((lbp = legetbuf(lep, 1)) == NULL) {
			lerror(lep->le_dip, "leinit failed:  out of buffers");
			goto done;
		}
		lermdinit(lep, &lep->le_rmdp[i], lbp);
		lep->le_rbufp[i] = lbp;	/* save for later use */
	}

	/*
	 * Set CSR1, CSR2, and CSR3.
	 */
	a = LEIOPBIOADDR(lep, lep->le_ibp);
	regsp->lance_rap = LANCE_CSR1;	/* select the low address register */
	regsp->lance_rdp = a & 0xffff;
	regsp->lance_rap = LANCE_CSR2;	/* select the high address register */
	regsp->lance_rdp = (a >> 16) & 0xff;
	regsp->lance_rap = LANCE_CSR3;	/* Bus Master control register */
	regsp->lance_rdp = ddi_getprop(DDI_DEV_T_ANY, lep->le_dip, 0,
		"busmaster-regval", LANCE_BSWP | LANCE_ACON | LANCE_BCON);

	/*
	 * Sync the init block and descriptors.
	 */
	LESYNCIOPB(lep, lep->le_iopbkbase,
		(sizeof (struct lance_init_block)
		+ (lep->le_nrmds * sizeof (struct lmd))
		+ (lep->le_ntmds * sizeof (struct lmd))
		+ (4 * LANCEALIGN)),
		DDI_DMA_SYNC_FORDEV);

	/*
	 * Chip init.
	 */
	regsp->lance_rap = LANCE_CSR0;
	regsp->lance_csr = LANCE_INIT;

	/*
	 * Allow 10 ms for the chip to complete initialization.
	 */
	CDELAY((regsp->lance_csr & LANCE_IDON), 10000);
	if (!(regsp->lance_csr & LANCE_IDON)) {
		lerror(lep->le_dip, "LANCE chip didn't initialize!");
		goto done;
	}
	regsp->lance_csr = LANCE_IDON;		/* Clear this bit */

	/*
	 * Chip start.
	 */
	regsp->lance_csr = LANCE_STRT | LANCE_INEA;

	lep->le_flags |= LERUNNING;

	lewenable(lep);

done:
	mutex_exit(&lep->le_xmitlock);
	rw_exit(&lestruplock);
	mutex_exit(&lep->le_intrlock);

	TRACE_1(TR_FAC_LE, TR_LE_INIT_END,
		"leinit end:  lep %X", lep);

	return (!(lep->le_flags & LERUNNING));
}

/*
 * Un-initialize (STOP) LANCE
 */
static void
leuninit(struct le *lep)
{
	/*
	 * Allow up to 'ledraintime' for pending xmit's to complete.
	 */
	CDELAY((lep->le_tcurp == lep->le_tnextp), LEDRAINTIME);

	mutex_enter(&lep->le_intrlock);
	mutex_enter(&lep->le_xmitlock);
	mutex_enter(&lep->le_buflock);

	lep->le_flags &= ~LERUNNING;

	/*
	 * Stop the chip.
	 */
	lep->le_regsp->lance_rap = LANCE_CSR0;
	lep->le_regsp->lance_csr = LANCE_STOP;

	mutex_exit(&lep->le_buflock);
	mutex_exit(&lep->le_xmitlock);
	mutex_exit(&lep->le_intrlock);
}

#define	LEBUFSRESERVED	4

static struct lebuf *
legetbuf(register struct le *lep, int pri)
{
	struct	lebuf	*lbp;
	int	i;

	TRACE_1(TR_FAC_LE, TR_LE_GETBUF_START,
		"legetbuf start:  lep %X", lep);

	mutex_enter(&lep->le_buflock);

	i = lep->le_bufi;

	if ((i == 0) || ((pri == 0) && (i < LEBUFSRESERVED))) {
		mutex_exit(&lep->le_buflock);
		TRACE_1(TR_FAC_LE, TR_LE_GETBUF_END,
			"legetbuf end:  lep %X", lep);
		return (NULL);
	}

	lbp = lep->le_buftab[--i];
	lep->le_bufi = i;

	mutex_exit(&lep->le_buflock);

	TRACE_1(TR_FAC_LE, TR_LE_GETBUF_END,
		"legetbuf end:  lep %X", lep);
	return (lbp);
}

static void
lefreebuf(register struct lebuf *lbp)
{
	register	struct	le	*lep = lbp->lb_lep;

	TRACE_1(TR_FAC_LE, TR_LE_FREEBUF_START,
		"lefreebuf start:  lep %X", lep);

	mutex_enter(&lep->le_buflock);

	lep->le_buftab[lep->le_bufi++] = lbp;

	mutex_exit(&lep->le_buflock);

	if (lep->le_wantw)
		lewenable(lep);
	TRACE_1(TR_FAC_LE, TR_LE_FREEBUF_END,
		"lefreebuf end:  lep %X", lep);
}

/*
 * Initialize RMD.
 */
static void
lermdinit(struct le *lep, volatile struct lmd *rmdp, struct lebuf *lbp)
{
	u_long	a;

	a = (u_long) lbp->lb_buf + LEHEADROOM;
	a = LEBUFIOADDR(lep, a);

	rmdp->lmd_ladr = (u_short) a;
	rmdp->lmd_hadr = (long) a >> 16;
	rmdp->lmd_bcnt = (u_short) -(LEBUFSIZE - LEHEADROOM);
	rmdp->lmd_mcnt = 0;
	rmdp->lmd_flags = LMD_OWN;
}

/*
 * Report Receive errors.
 */
static void
le_rcv_error(struct le *lep, struct lmd *rmdp)
{
	u_int flags = rmdp->lmd_flags;
	static	u_short gp_count = 0;

	if ((flags & RMD_FRAM) && !(flags & RMD_OFLO)) {
		if (ledebug)
			lerror(lep->le_dip, "Receive: framming error");
		lep->le_fram++;
	}
	if ((flags & RMD_CRC) && !(flags & RMD_OFLO)) {
		if (ledebug)
			lerror(lep->le_dip, "Receive: crc error");
		lep->le_crc++;
	}
	if ((flags & RMD_OFLO) && !(flags & LMD_ENP)) {
		if (ledebug)
			lerror(lep->le_dip, "Receive: overflow error");
		lep->le_oflo++;
	}
	if (flags & RMD_BUFF)
		lerror(lep->le_dip, "Receive: BUFF set in rmd");
	/*
	 * If an OFLO error occurred, the chip may not set STP or ENP,
	 * so we ignore a missing ENP bit in these cases.
	 */
	if (!(flags & LMD_STP) && !(flags & RMD_OFLO)) {
		if (ledebug)
			lerror(lep->le_dip, "Receive: STP in rmd cleared");
		/*
		 * if using a macio ethernet chip - reset it
		 * as it may have gone into a hung state
		 */
		if ((flags & LMD_ENP) && (lep->le_init)) {
			/*
			 * only reset if the packet size was
			 * greater than 4096 bytes
			 */
			if (gp_count > rmdp->lmd_mcnt) {
				if (ledebug)
					lerror(lep->le_dip,
						"Receive: reset ethernet");
				mutex_exit(&lep->le_intrlock);
				(void) leinit(lep);
				mutex_enter(&lep->le_intrlock);
			}
			gp_count = 0;
		} else
			gp_count += LEBUFSIZE;
	} else if (!(flags & LMD_ENP) && !(flags & RMD_OFLO)) {
		if (ledebug)
			lerror(lep->le_dip,
				"Receive: giant packet");
		gp_count += LEBUFSIZE;
	}
}

static char *lenocar1 =
	"No carrier - transceiver cable problem?";
static char *lenocar2 =
	"No carrier - cable disconnected or hub link test disabled?";

/*
 * Report on transmission errors paying very close attention
 * to the Rev B (October 1986) Am7990 programming manual.
 * Return 1 if leinit() is called, 0 otherwise.
 */
static int
le_xmit_error(struct le *lep, struct lmd *tmdp)
{
	u_int flags = tmdp->lmd_flags3;

	/*
	 * BUFF is not valid if either RTRY or LCOL is set.
	 * We assume here that BUFFs are always caused by UFLO's
	 * and not driver bugs.
	 */
	if ((flags & (TMD_BUFF | TMD_RTRY | TMD_LCOL)) == TMD_BUFF) {
		if (ledebug)
			lerror(lep->le_dip, "Transmit: BUFF set in tmd");
	}
	if (flags & TMD_UFLO) {
		if (ledebug)
			lerror(lep->le_dip, "Transmit underflow");
		lep->le_uflo++;
	}
	if (flags & TMD_LCOL) {
		if (ledebug)
			lerror(lep->le_dip,
				"Transmit late collision - net problem?");
		lep->le_tlcol++;
	}

	/*
	 * Early MACIO chips set both LCAR and RTRY on RTRY.
	 */
	if ((flags & (TMD_LCAR|TMD_RTRY)) == TMD_LCAR) {
		if (lep->le_init) {
			if (lep->le_oopkts == lep->le_opackets) {
				lerror(lep->le_dip, "%s", lenocar2);
				lep->le_oopkts = -1;
				lep->le_tnocar++;
			} else
				lep->le_oopkts = lep->le_opackets;
			if (lep->le_autosel)
				lep->le_tpe = 1 - lep->le_tpe;
			mutex_exit(&lep->le_intrlock);
			(void) leinit(lep);
			mutex_enter(&lep->le_intrlock);
			return (1);
		} else
			lerror(lep->le_dip, "%s", lenocar1);
		lep->le_tnocar++;
	}
	if (flags & TMD_RTRY) {
		if (ledebug)
			lerror(lep->le_dip,
		"Transmit retried more than 16 times - net  jammed");
		lep->le_collisions += 16;
		lep->le_trtry++;
	}
	return (0);
}

/*
 * Handle errors reported in LANCE CSR0.
 */
static
le_chip_error(struct le *lep)
{
	register u_short	csr0 = lep->le_regsp->lance_csr;
	int restart = 0;

	if (csr0 & LANCE_MISS) {
		if (ledebug)
			lerror(lep->le_dip, "missed packet");
		lep->le_ierrors++;
		lep->le_missed++;
		lep->le_regsp->lance_csr = LANCE_MISS | LANCE_INEA;
	}

	if (csr0 & LANCE_BABL) {
	    lerror(lep->le_dip,
		"Babble error - packet longer than 1518 bytes");
		lep->le_oerrors++;
	    lep->le_regsp->lance_csr = LANCE_BABL | LANCE_INEA;
	}
	/*
	 * If a memory error has occurred, both the transmitter
	 * and the receiver will have shut down.
	 * Display the Reception Stopped message if it
	 * wasn't caused by MERR (should never happen) OR ledebug.
	 * Display the Transmission Stopped message if it
	 * wasn't caused by MERR (was caused by UFLO) AND ledebug
	 * since we will have already displayed a UFLO msg.
	 */
	if (csr0 & LANCE_MERR) {
		if (ledebug)
			lerror(lep->le_dip, "Memory error!");
		lep->le_ierrors++;
		lep->le_regsp->lance_csr = LANCE_MERR | LANCE_INEA;
		restart++;
	}
	if (!(csr0 & LANCE_RXON)) {
	    if (!(csr0 & LANCE_MERR) || ledebug)
		lerror(lep->le_dip, "Reception stopped");
	    restart++;
	}
	if (!(csr0 & LANCE_TXON)) {
		if (!(csr0 & LANCE_MERR) && ledebug)
			lerror(lep->le_dip, "Transmission stopped");
		restart++;
	}

	if (restart) {
		mutex_exit(&lep->le_intrlock);
		(void) leinit(lep);
		mutex_enter(&lep->le_intrlock);
	}
	return (restart);
}

/*VARARGS*/
static void
lerror(dev_info_t *dip, char *fmt, ...)
{
	static	long	last;
	static	char	*lastfmt;
	char		msg_buffer[255];
	va_list	ap;

	mutex_enter(&lelock);

	/*
	 * Don't print same error message too often.
	 */
	if ((last == (hrestime.tv_sec & ~1)) && (lastfmt == fmt)) {
		mutex_exit(&lelock);
		return;
	}
	last = hrestime.tv_sec & ~1;
	lastfmt = fmt;


	va_start(ap, fmt);
	vsprintf(msg_buffer, fmt, ap);
	cmn_err(CE_CONT, "%s%d: %s\n", ddi_get_name(dip),
					ddi_get_instance(dip),
					msg_buffer);
	va_end(ap);

	mutex_exit(&lelock);
}

/*
 * Create and add an leops structure to the linked list.
 */
static void
leopsadd(struct leops *lop)
{
	mutex_enter(&lelock);
	lop->lo_next = leops;
	leops = lop;
	mutex_exit(&lelock);
}

struct leops *
leopsfind(dev_info_t *dip)
{
	register	struct	leops	*lop;

	for (lop = leops; lop; lop = lop->lo_next)
		if (lop->lo_dip == dip)
			return (lop);
	return (NULL);
}
