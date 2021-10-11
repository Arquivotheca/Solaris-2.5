/*
 * Copyright (c) 1992, 1994 by Sun Microsystems, Inc.
 * All Rights Reserved
 */

#pragma ident	"@(#)ie.c	1.37	95/01/13 SMI"

/*
 * SunOS MT STREAMS Intel 82586 Ethernet Device Driver
 *
 * Refer to document:
 *	"SunOS 5.0.1 IE Device Driver", 801-3393-01.
 *
 * Since the Intel 82586 is only used in sun4 and Sun-3/E board, this
 * driver takes some shortcut and is sometimes anti-DDI.  For example,
 * we do not call ddi_dma_sync().  Neither do we read back the ethernet
 * control register to flush the write buffer which is required in
 * some machines, eg, SS10 which has the write buffer in the Mbus to
 * Sbus interface.
 */

#define	DEBUG	1	/* for debug.h ASSERT() */

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
#include	<sys/kmem.h>
#include	<sys/conf.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>
#include	<sys/ksynch.h>
#include	<sys/strsun.h>
#include	<sys/stat.h>
#include	<sys/kstat.h>
#include	<sys/dlpi.h>
#include	<sys/ethernet.h>
#include	<sys/i82586.h>
#include	<sys/ie.h>
#include	<sys/iocache.h>

/*
 * Function prototypes.
 */
static	int ieidentify(dev_info_t *);
static	int ieprobe(dev_info_t *);
static	int ieattach(dev_info_t *, ddi_attach_cmd_t);
static	void iestatinit(ie_t *);
static	int ieinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static	int ieopen(queue_t *, dev_t *, int, int, cred_t *);
static	int ieclose(queue_t *);
static	int iewput(queue_t *, mblk_t *);
static	int iewsrv(queue_t *);
static	void ieioctl(queue_t *, mblk_t *);
static	u_int	ieintr();
static	int ieallocmem(ie_t *);
static	int iebufalloc(ie_t *);
static	void iedescriptorinit(ie_t *);
static	void iechipreset(ie_t *);
static	void ie_dl_ioc_hdr_info(queue_t *, mblk_t  *);
static	void ieattachreq(struct  iestr *, queue_t *, mblk_t *);
static	void iedetachreq(struct iestr *, queue_t *, mblk_t *);
static	void iebindreq(struct iestr *, queue_t *, mblk_t *);
static	void ieunbindreq(struct iestr *, queue_t *, mblk_t *);
static	void ieinforeq(struct iestr *, queue_t *, mblk_t *);
static	void ieunitdatareq(struct iestr *, queue_t *, mblk_t *);
static	void iepromisconreq(struct iestr *, queue_t *, mblk_t *);
static	void iepromiscoffreq(struct iestr *, queue_t *, mblk_t *);
static	void ieaddmultireq(struct iestr *, queue_t *, mblk_t *);
static	void iedelmultireq(struct iestr *, queue_t *, mblk_t *);
static	void iephsaddreq(struct iestr *, queue_t *, mblk_t *);
static	void iesetphsaddreq(struct iestr *, queue_t *, mblk_t *);
static	void iedodetach(struct iestr *);
static	int iedoaddr(ie_t *);
static	int iechipinit(ie_t *);
static	int iechkcca(ie_t *);
static	void ierecv(ie_t *);
static	void iewenable(ie_t *);
static	void iecuclean(ie_t *);
static	void iecustart(ie_t *);
static	void ieuninit(ie_t *);
static	void iexmitdone(ie_t *, ietcb_t *);
static	void ieproto(queue_t *, mblk_t *);
static	void iefreebuf(iebuf_t *);
static	void iesendup(ie_t *, mblk_t *, struct iestr *(*)());
static	int iesynccmd(ie_t *);
static	void ieread(ie_t *, volatile ierfd_t *);
static	void iedefaultconf(struct ieconf *);
static	void ierustart(ie_t *);
static	void iedog(ie_t *);
static	void ienetjammed(ie_t *);
static	void ierbufinit(struct ie *, ierbd_t *, iebuf_t *);
static	struct iestr *ieaccept(struct iestr *, ie_t *, int,
	struct ether_addr *);
static	struct iestr *iepaccept(struct iestr *, ie_t *, int,
	struct ether_addr *);
static	int iemcmatch(struct iestr *, struct ether_addr *);
static	mblk_t *ieaddudind(ie_t *, mblk_t *, struct ether_addr *,
	struct ether_addr *, int, ulong);
static	iebuf_t *iegetbuf(ie_t *, int);
static	int iestart(queue_t *, mblk_t *, ie_t *);
static	int iewaitintr(ie_t *);
static	ieoff_t	toieoff(ie_t *, caddr_t);
static	ieint_t	toieint(ieint_t);
static	caddr_t fromieoff(ie_t *, ieoff_t);
static	int ierequeue(ie_t *, ietcb_t *, ietbd_t *);
static	void ie_ioc_init(void);
static	void ierror(dev_info_t *dip, char *fmt, ...);

static	struct	module_info	ieminfo = {
	IEIDNUM,	/* mi_idnum */
	IENAME,		/* mi_idname */
	IEMINPSZ,	/* mi_minpsz */
	IEMAXPSZ,	/* mi_maxpsz */
	IEHIWAT,	/* mi_hiwat */
	IELOWAT		/* mi_lowat */
};

static	struct	qinit	ierinit = {
	NULL,		/* qi_putp */
	NULL,		/* qi_srvp */
	ieopen,		/* qi_qopen */
	ieclose,	/* qi_qclose */
	NULL,		/* qi_qadmin */
	&ieminfo,	/* qi_minfo */
	NULL		/* qi_mstat */
};

static	struct	qinit	iewinit = {
	iewput,		/* qi_putp */
	iewsrv,		/* qi_srvp */
	NULL,		/* qi_qopen */
	NULL,		/* qi_qclose */
	NULL,		/* qi_qadmin */
	&ieminfo,	/* qi_minfo */
	NULL		/* qi_mstat */
};

static struct	streamtab	ie_info = {
	&ierinit,	/* st_rdinit */
	&iewinit,	/* st_wrinit */
	NULL,		/* st_muxrinit */
	NULL		/* st_muxwrinit */
};

static	struct	cb_ops	cb_ie_ops = {
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
	&ie_info,	/* cb_stream */
	D_MP		/* cb_flag */
};

static	struct	dev_ops	ie_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	ieinfo,			/* devo_getinfo */
	ieidentify,		/* devo_identify */
	ieprobe,		/* devo_probe */
	ieattach,		/* devo_attach */
	nodev,			/* devo_detach */
	nodev,			/* devo_reset */
	&cb_ie_ops,		/* devo_cb_ops */
	(struct bus_ops *)NULL	/* devo_bus_ops */
};

/*
 * DMA limits for the intel ethernet chip.
 * (for OB implementations only).
 */
static ddi_dma_lim_t ie_dma_lim = {
	(u_long) 0xff000000,	/* dlim_addr_lo */
	(u_long) 0xffffffff,	/* dlim_addr_hi */
	(u_int) ((1<<24)-1),	/* dlim_cntr_max */
	(u_int) 0x12,		/* dlim_burstsizes */
	2,			/* dlim_minxfer */
	1024			/* dlim_speed */
};
static ddi_dma_lim_t ie_scp_lim = {
	(u_long) (0 - 0x2000),
	(u_long) -1,
	(u_int) 0x2000-1,
	(u_int) 0x2,
	2,
	0
};

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern struct mod_ops mod_driverops;

/*
 * Module linkage information for the kernel.
 */
static struct modldrv modldrv  = {
	&mod_driverops, /* Type of module.  This one is a driver */
	"Intel Ethernet Driver v1.37",
	&ie_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * XXX Autoconfiguration lock:  We want to initialize all the global
 * locks at _init().  However, we do not have the cookie required which
 * is returned in ddi_add_intr() which is usually called at the attach
 * time.
 */
static	kmutex_t	ieautolock;

/*
 * Linked list of active (inuse) driver Streams.
 */
static	struct	iestr	*iestrup = NULL;
static	krwlock_t	iestruplock;

/*
 * Single private "global" lock for the few rare conditions
 * we want single-threaded.
 */
static	kmutex_t	ielock;

int
_init(void)
{
	int	status;

	mutex_init(&ieautolock, "ie autoconfig lock", MUTEX_DRIVER, NULL);
	status = mod_install(&modlinkage);
	if (status != 0) {
		mutex_destroy(&ieautolock);
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
	mutex_destroy(&ielock);
	rw_destroy(&iestruplock);
	mutex_destroy(&ieautolock);
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
static	int	iedebug = 0;

/*
 * Patchable xmit drain value.
 * Allow up to this much time for all frames on the transmit unit
 * to complete transmission.  Figure a max of 30 descriptors
 * with 1200 us each.
 *
 * Notice that iedraintime happens to be smaller than IEDELAY.
 */
static	int	iedraintime = 36000;	/* # microseconds */

/*
 * Allocate and zero-out "number" structures
 * each of type "structure" in kernel memory.
 */
#define	GETSTRUCT(structure, number)	\
	((structure *) kmem_zalloc(\
		(u_int) (sizeof (structure) * (number)), KM_SLEEP))

/*
 * Number of tcb and tbd.
 */
#define	IE_NXMIT	30

/*
 * Number of rfd and rbd.
 */
#define	IE_NRECV	30

/*
 * Total number of transmit buffers and receive buffers.
 */
#define	IE_NBUF		78

#define	fromieint	toieint

/*
 * Delay values
 */
#define	IEDELAY		400000	/* delay period (in us) before giving up */
				/* Required by the Intel D-Step Errata */
#define	IEKLUDGE	20	/* delay period (in us) to make chip work */
#define	IETIMEOUT	300	/* seconds w/o receive to reset chip */
#define	IEWAITTIME	10
static	int	iedelaytimes = IEDELAY / IEWAITTIME;

/*
 * debugging aid.
 */
#define	IEMAXARRAY	256
int iearray[IEMAXARRAY];
int iei = 0;
#define	IEINS(i)	{ \
	if (iei == IEMAXARRAY) \
		iei = 0; \
	iearray[iei++] = (i); \
}

/*
 * Our DL_INFO_ACK template.
 */
static	dl_info_ack_t ie_infoack = {
	DL_INFO_ACK,				/* dl_primitive */
	ETHERMTU,				/* dl_max_sdu */
	0,					/* dl_min_sdu */
	IEADDRL,				/* dl_addr_length */
	DL_ETHER,				/* dl_mac_type */
	0,					/* dl_reserved */
	0,					/* dl_current_state */
	-2,					/* dl_sap_length */
	DL_CLDLS,				/* dl_service_mode */
	0,					/* dl_qos_length */
	0,					/* dl_qos_offset */
	0,					/* dl_range_length */
	0,					/* dl_range_offset */
	DL_STYLE2,				/* dl_provider_style */
	sizeof (dl_info_ack_t),			/* dl_addr_offset */
	DL_VERSION_2,				/* dl_version */
	ETHERADDRL,				/* dl_brdcst_addr_length */
	sizeof (dl_info_ack_t) + IEADDRL,	/* dl_brdcst_addr_offset */
	0					/* dl_growth */
};

/*
 * Read the latest IO status.  The sync is not required for sun4.
 */
#ifdef notdef
#define	IE_CPU_SYNC(iep, addr, size) {\
	(void) ddi_dma_sync((iep)->ie_dscpthandle, \
		(off_t)((u_long) (addr) - (u_long) (iep)->ie_cb_base), \
		(u_int) (size), DDI_DMA_SYNC_FORCPU) \
}
#endif
#define	IE_CPU_SYNC(iep, addr, size)

/*
 * Flush the CPU cache.  The sync is not required for sun4.
 */
#ifdef notdef
#define	IE_DEV_SYNC(iep, addr, size) {\
	(void) ddi_dma_sync((iep)->ie_dscpthandle, \
		(off_t)((u_long) (addr) - (u_long) (iep)->ie_cb_base), \
		(u_int) (size), DDI_DMA_SYNC_FORDEV) \
}
#endif
#define	IE_DEV_SYNC(iep, addr, size)

#define	IESAPMATCH(sap, type, flags) ((sap == type)? 1 : \
	((flags & ISALLSAP)? 1 : \
	((sap < ETHERMTU) && (sap > 0) && (type <= ETHERMTU))? 1 : 0))

/*
 * Pointers to the device register.
 */
#define	IE_TECSR	((short *)iep->ie_csr)
#define	IE_OBCSR	((char *)iep->ie_csr)

/*
 * The size of the RAM area in the 3E borad reserved for the descriptors.
 */
#define	IE_TE_DES_SIZE	3 * 1024

/*
 * XXX Sun4/4XX IO cache flush bytes for xmit/recv lines.  Not ddi-compliant.
 */
#define	IOC_FLUSH_IN	0
#define	IOC_FLUSH_OUT	1

/*
 * Get the next transmit command block.
 */
#define	NEXTTCB(iep, tcbp) \
	(((tcbp) + 1) == (iep)->ie_tcblim ? (iep)->ie_tcbring : ((tcbp) + 1))

/*
 * Sun-3/E uses PHYSICAL addresses to access the local memory.  The local
 * memory located at physical address 0x20000 to 0x3FFFF on the board.
 */
#define	IEPHYADDR(iep, a)	((u_int)a - (u_int)iep->ie_ram + 0x020000)

/*
 * Is this device slave only (actually, a Sun-3/E card) or a DVMA master
 * (actually, a Sun4 motherborad)?
 */
#define	ISSLAVE		(iep->ie_flags & IESLAVE) != 0
#define	ISMASTER	(iep->ie_flags & IESLAVE) == 0

/*
 * Convert big endian address to Intel 24-bit address.
 */
#define	PUT_IEADDR(iep, addr, where) { \
	register char *cp; \
	union { \
		u_int	n; \
		char	c[4]; \
	} a; \
	\
	if (ISSLAVE) \
		a.n = (int)IEPHYADDR(iep, addr); \
	else \
		a.n = (int)(addr); \
	cp = (char *)(where); \
	cp[0] = a.c[3]; \
	cp[1] = a.c[2]; \
	cp[2] = a.c[1]; \
}

/*
 * Activate the channel attention line.
 */
#define	IECA(iep)	{ \
	if (ISSLAVE) { \
		*IE_TECSR |= TIE_CA; \
		*IE_TECSR &= ~TIE_CA; \
	} else { \
		*IE_OBCSR |= OBIE_CA; \
		*IE_OBCSR &= ~OBIE_CA; \
	} \
}

/*
 * Ethernet broadcast address definition.
 */
static	struct	ether_addr	etherbroadcastaddr = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/*
 * Linked list of "ie" structures - one per device.
 */
ie_t *iedev = NULL;

/*
 * Confirm whether we drive this device.
 */
static int
ieidentify(dev_info_t *dip)
{
	if (strcmp(ddi_get_name(dip), "ie") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/*
 * Probe for device.
 */
static int
ieprobe(dip)
dev_info_t	*dip;
{
	caddr_t reg;
	int retval = DDI_PROBE_FAILURE;
	int type = 0;

	if (ddi_map_regs(dip, 0, (caddr_t *) &reg, 0, 0) != DDI_SUCCESS)
		return (DDI_PROBE_FAILURE);
	if (strcmp(ddi_get_name(ddi_get_parent(dip)), "obio") == 0) {

		/*
		 * Onboard interface probe
		 */
		if (ddi_pokec(dip, reg, 0) != DDI_SUCCESS ||
			ddi_peekc(dip, reg, (char *)0) != DDI_SUCCESS ||
			(*reg & OBIE_NORESET))
			goto out;
	} else {

		/*
		 * 3E interface probe
		 */
		register short *sp = (short *) reg;
		short value;

		if (ddi_pokes(dip, sp, 0xffff) != DDI_SUCCESS)
			goto out;
		if (ddi_peeks(dip, sp, &value) != DDI_SUCCESS)
			goto out;
		if (value != (short) 0xf000)
			goto out;
		type = IESLAVE;
	}
	ddi_set_driver_private(dip, (caddr_t) type);
	retval = DDI_PROBE_SUCCESS;
out:
	ddi_unmap_regs(dip, 0, &reg, 0, 0);
	return (retval);
}

/*
 * Interface exists; make available by filling in network interface
 * record.  System will initialize the interface when it is ready
 * to accept packets.
 */
static int
ieattach(dip, cmd)
dev_info_t	*dip;
ddi_attach_cmd_t	cmd;
{
	register ie_t *iep = (ie_t *) NULL;
	caddr_t csr, ram, iocache;
	ddi_iblock_cookie_t c = NULL;
	ddi_idevice_cookie_t intrc;
	int c_type;
	int type;
	int once = 1;

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	csr = ram = iocache = NULL;

	/*
	 * Allocate soft data structure
	 */
	iep = GETSTRUCT(struct ie, 1);

	/*
	 * Map in the device registers.
	 *
	 * Type 0 is the Ethernet Control and Status Register.
	 * Type 1 is the IORAM for Sun4/4XX or the RAM on the 3E board.
	 * Type 2 is the IOCache lines for transmit/receive on Sun4/4XX.
	 */
	if (ddi_map_regs(dip, 0, (caddr_t *) &csr, 0, 0)) {
		ierror(dip, "ddi_map_regs for type 0 failed");
		goto bad;
	}
	if (ddi_dev_nregs(dip, &type) == DDI_SUCCESS && type > 1) {
		if (ddi_map_regs(dip, 1, &ram, 0, 0)) {
			ierror(dip, "ddi_map_regs for type 1 failed");
			goto bad;
		}
		if (type == 3) {
			if (ddi_map_regs(dip, 2, &iocache, 0, 0)) {
				ierror(dip, "ddi_map_regs for type 2 failed");
				goto bad;
			}
		}
	}

	if (iedebug)
		cmn_err(CE_CONT, "csr %x ram %x iocache %x\n",
			(int)csr, (int)ram, (int)iocache);

	/*
	 * Add interrupt to system.
	 */
	if (ddi_add_intr(dip, 0, &c, &intrc, ieintr, (caddr_t) iep)) {
		ierror(dip, "ddi_add_intr failed");
		goto bad;
	}

	/*
	 * Initialize the per-device mutex and the buffer mutex.
	 */
	mutex_init(&iep->ie_devlock, "ie device lock", MUTEX_DRIVER, (void *)c);
	mutex_init(&iep->ie_buflock, "ie buffer lock", MUTEX_DRIVER, (void *)c);

	c_type = (int) ddi_get_driver_private(dip);
	iep->ie_flags |= c_type;

	/*
	 * Set the VME interrupt vector for 3E.
	 */
	if (c_type == IESLAVE)
		((struct tie_device *)csr)->tie_ivec =
			(u_char) intrc.idev_vector;

	/*
	 * At this point, we are *really* here.
	 */
	ddi_set_driver_private(dip, (caddr_t) iep);
	iep->ie_dip = dip;
	iep->ie_csr = csr;
	if (type > 1)
		iep->ie_ram = ram;
	else
		iep->ie_ram = NULL;
	if (type > 2)
		iep->ie_iocache = iocache;
	else
		iep->ie_iocache = NULL;

	iechipreset(iep);

	/*
	 * Get the local ethernet address.
	 */
	(void) localetheraddr((struct ether_addr *)NULL, &iep->ie_ouraddr);

	/*
	 * Create the filesystem device node.
	 */
	if (ddi_create_minor_node(dip, "ie", S_IFCHR,
		ddi_get_instance(dip), DDI_NT_NET, CLONE_DEV) == DDI_FAILURE) {
		ierror(dip, "ddi_create_minor_node failed");
		mutex_destroy(&iep->ie_devlock);
		mutex_destroy(&iep->ie_buflock);
		goto bad;
	}

	/*
	 * One time only driver initializations.
	 */
	mutex_enter(&ieautolock);
	if (once) {
		once = 0;
		rw_init(&iestruplock, "ie streams linked list lock",
			RW_DRIVER, (void *)c);
		mutex_init(&ielock, "ie global lock", MUTEX_DRIVER, (void *)c);
	}
	mutex_exit(&ieautolock);

	/*
	 * Link this per-device structure in with the rest.
	 */
	mutex_enter(&ielock);
	iep->ie_nextp = iedev;
	iedev = iep;
	mutex_exit(&ielock);

	iestatinit(iep);
	ddi_report_dev(dip);
	return (DDI_SUCCESS);

bad:
	if (iep)
		kmem_free((caddr_t) iep, sizeof (*iep));
	if (csr)
		ddi_unmap_regs(dip, 0, &csr, 0, 0);
	if (ram)
		ddi_unmap_regs(dip, 0, &ram, 0, 0);
	if (iocache)
		ddi_unmap_regs(dip, 0, &iocache, 0, 0);
	if (c)
		ddi_remove_intr(dip, 0, c);

	return (DDI_FAILURE);
}

static int
iestat_kstat_update(kstat_t *ksp, int rw)
{
	struct ie *iep;
	struct iestat *iesp;

	if (rw == KSTAT_WRITE)
		return (EACCES);
	iep = (struct ie *) ksp->ks_private;
	iesp = (struct iestat *) ksp->ks_data;
	iesp->ies_ipackets.value.ul	= iep->ie_ipackets;
	iesp->ies_ierrors.value.ul	= iep->ie_ierrors;
	iesp->ies_opackets.value.ul	= iep->ie_opackets;
	iesp->ies_oerrors.value.ul	= iep->ie_oerrors;
	iesp->ies_collisions.value.ul	= iep->ie_collisions;
	iesp->ies_defer.value.ul	= iep->ie_defer;
	iesp->ies_crc.value.ul		= iep->ie_crc;
	iesp->ies_oflo.value.ul		= iep->ie_oflo;
	iesp->ies_uflo.value.ul		= iep->ie_uflo;
	iesp->ies_missed.value.ul	= iep->ie_missed;
	iesp->ies_tlcol.value.ul	= iep->ie_tlcol;
	iesp->ies_trtry.value.ul	= iep->ie_trtry;
	iesp->ies_tnocar.value.ul	= iep->ie_tnocar;
	iesp->ies_inits.value.ul	= iep->ie_inits;
	iesp->ies_nocanput.value.ul	= iep->ie_nocanput;
	iesp->ies_allocbfail.value.ul	= iep->ie_allocbfail;
	iesp->ies_xmiturun.value.ul	= iep->ie_xmiturun;
	iesp->ies_recvorun.value.ul	= iep->ie_recvorun;
	iesp->ies_align.value.ul	= iep->ie_align;
	iesp->ies_notcbs.value.ul	= iep->ie_notcbs;
	iesp->ies_notbufs.value.ul	= iep->ie_notbufs;
	iesp->ies_norbufs.value.ul	= iep->ie_norbufs;
	return (0);
}

static void
iestatinit(iep)
struct ie	*iep;
{
	kstat_t	*ksp;
	struct	iestat	*iesp;

	if ((ksp = kstat_create("ie", ddi_get_instance(iep->ie_dip),
	    NULL, "net", KSTAT_TYPE_NAMED,
	    sizeof (struct iestat) / sizeof (kstat_named_t), 0)) == NULL) {
		ierror(iep->ie_dip, "kstat_create failed");
		return;
	}

	iesp = (struct iestat *) (ksp->ks_data);

	kstat_named_init(&iesp->ies_ipackets,		"ipackets",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_ierrors,		"ierrors",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_opackets,		"opackets",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_oerrors,		"oerrors",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_collisions,		"collisions",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_defer,		"defer",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_crc,		"crc",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_oflo,		"oflo",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_uflo,		"uflo",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_missed,		"missed",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_tlcol,		"late_collisions",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_trtry,		"retry_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_tnocar,		"nocarrier",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_inits,		"inits",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_nocanput,		"nocanput",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_allocbfail,		"allocbfail",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_xmiturun,		"xmitunderrun",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_recvorun,		"recvoverrun",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_align,		"align",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_notcbs,		"notcbs",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_notbufs,		"notbufs",
		KSTAT_DATA_ULONG);
	kstat_named_init(&iesp->ies_norbufs,		"norbufs",
		KSTAT_DATA_ULONG);

	ksp->ks_update = iestat_kstat_update;
	ksp->ks_private = (void *) iep;
	kstat_install(ksp);
}

/*
 * Translate "dev_t" to a pointer to the associated "dev_info_t".
 */
/* ARGSUSED1 */
static int
ieinfo(dip, infocmd, arg, result)
dev_info_t *dip;
ddi_info_cmd_t	infocmd;
void *arg;
void **result;
{
	dev_t		dev = (dev_t) arg;
	int		instance, rc;
	register	struct iestr *iestrp;

	instance = getminor(dev);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		rw_enter(&iestruplock, RW_READER);
		dip = NULL;
		for (iestrp = iestrup; iestrp; iestrp = iestrp->is_nextp)
			if (iestrp->is_minor == instance) {
				if (iestrp->is_iep)
					dip = iestrp->is_iep->ie_dip;
				break;
			}

		rw_exit(&iestruplock);

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
 * Cut the chip out of the loop and halt it by starting the reset cycle.
 */
static void
iechipreset(iep)
register ie_t *iep;
{
	if (ISMASTER)
		*IE_OBCSR = 0;				/* power on reset */
	else {
		*IE_TECSR |= TIE_RESET;
		drv_usecwait(IEKLUDGE);			/* required delay */
		*(char *)IE_TECSR = 0;			/* power on reset */
	}
}

/*
 * Initialize chip and driver.
 * Return 0 on success, nonzero on error.
 */
static int
ieinit(iep)
register ie_t *iep;
{
	struct  iestr *isp;
	int i;
	int error = 0;

	mutex_enter(&iep->ie_devlock);
	rw_enter(&iestruplock, RW_WRITER);

	/* Flush Sun4/4XX IO cache */
	if (iep->ie_iocache)
		iep->ie_iocache[IOC_FLUSH_IN] =
			iep->ie_iocache[IOC_FLUSH_OUT] = 0;

	iechipreset(iep);
	(void) untimeout(iep->ie_dogid);

	/*
	 * Set the IEPROMISC flag if any stream needs it.
	 */
	iep->ie_flags &= ~(IEPROMISC);
	for (isp = iestrup; isp; isp = isp->is_nextp)
		if ((isp->is_iep == iep) && (isp->is_flags & ISALLPHYS)) {
			iep->ie_flags |= IEPROMISC;
			break;
		}

	iep->ie_wantw = 0;
	iep->ie_flags &= ~(IERUNNING);	/* Keep IESLAVE */
	iep->ie_inits++;

	if (ieallocmem(iep) || iechipinit(iep)) {
		error = 1;
		goto done;
	}

	/*
	 * Free any pending xmit msgs.
	 */
	for (i = 0; i < IE_NXMIT; i++) {
		if (iep->ie_tmblk[i]) {
			freemsg(iep->ie_tmblk[i]);
			iep->ie_tmblk[i] = NULL;
		}
		if (iep->ie_tbp[i]) {
			iefreebuf(iep->ie_tbp[i]);
			iep->ie_tbp[i] = NULL;
		}
	}

	/*
	 * Free any pending recv bufs.
	 */
	for (i = 0; i < IE_NRECV; i++)
		if (iep->ie_rbp[i]) {
			iefreebuf(iep->ie_rbp[i]);
			iep->ie_rbp[i] = NULL;
		}

	iedescriptorinit(iep);

	/*
	 * Set the individual and multicast addresses.
	 */
	if (iedoaddr(iep)) {
		error = 1;
		goto done;
	}

	iep->ie_flags |= IERUNNING;
	ierustart(iep);
	iep->ie_dogid = timeout(iedog, (caddr_t)iep, 60 * hz);

	/*
	 * Switch from the local loopback mode to the real networking mode.
	 */
	if (ISSLAVE)
		*IE_TECSR |= TIE_NOLOOP | TIE_IE;
	else
		*IE_OBCSR |= OBIE_NOLOOP | OBIE_IE;
	iewenable(iep);

done:
	rw_exit(&iestruplock);
	mutex_exit(&iep->ie_devlock);

	return (error);
}

/*
 * Allocate memory for:
 * - SCP, ISCP and SCB
 * - transmit frame descriptors
 * - transmit buffer descriptors
 * - receive frame descriptors
 * - receive buffer descriptors
 * - transmit and receive buffers
 * - a command block for synchronous requests
 *
 * For 3E, everything will be allocated from the RAM on the 3E card.  The
 * base address of the RAM is mapped in ieattach().
 *
 * For Sun4/4XX, the descriptors will be allocated from the IORAM.  The base
 * address of the IORAM is also mapped in ieattach().  The buffers will
 * be allocated from the main memory.
 *
 * For Sun4/1XX and Sun4/2XX, everything will be allocated from the main
 * memory.
 *
 * Call with per-device locks acquired.  Return 0 on success, 1 otherwise.
 */
static int
ieallocmem(iep)
register ie_t *iep;
{
	caddr_t iopb;
	caddr_t	tmpp;
	register u_int size;

	/*
	 * Return if resources are already allocated.
	 */
	if (iep->ie_cbsyncp != NULL)
		return (0);

	if (ISMASTER) {
		ddi_dma_handle_t h;
		int flag = DDI_DMA_RDWR|DDI_DMA_CONSISTENT;

		if (ddi_iopb_alloc(iep->ie_dip, &ie_scp_lim, ptob(1), &iopb)) {
			ierror(iep->ie_dip, "ieallocmem: out of iopb space");
			return (1);
		}
		if (ddi_dma_addr_setup(iep->ie_dip, (struct as *) 0,
			iopb, ptob(1), flag, DDI_DMA_SLEEP, (caddr_t) 0,
			&ie_scp_lim, &h)) {
			ierror(iep->ie_dip, "ieallocmem: no dma map");
			return (1);
		}
		iep->ie_scp = (struct iescp *) (((u_int) iopb) +
			ptob(1) - sizeof (struct iescp));

		if (iep->ie_iocache != NULL) {
			ddi_dma_lim_t tlim;
			ddi_dma_handle_t desh;

			/*
			 * Okay, we're on a Sun4/4XX.
			 */
			/*
			 * On a Sun4/4XX, if A[23] && A[15] are one as
			 * emitted by chip, that the access will be
			 * to the IOC ram, not OBMEM.  Here we want
			 * to ensure that buffer accesses will not
			 * have A[23] set and descriptor accesses
			 * will have A[23] set.
			 */
			ie_dma_lim.dlim_addr_hi = (u_long) (0xff800000 - 1);
			tlim = ie_scp_lim;
			tlim.dlim_addr_hi = (u_long) (0 - 0x2000);
			tlim.dlim_addr_lo = (u_long) (0 - 0x4000);

			if (ddi_dma_addr_setup(iep->ie_dip, 0,
				iep->ie_ram, IE_IORAM_SIZE,
				DDI_DMA_RDWR|DDI_DMA_CONSISTENT,
				DDI_DMA_SLEEP, 0, &tlim, &desh)) {
				ierror(iep->ie_dip,
					"ieallocmem: no dma map of IOC ram");
				return (1);
			}
			iep->ie_dscpthandle = desh;
			(void) ddi_dma_kvaddrp(desh, 0, IE_IORAM_SIZE,
				&iep->ie_cb_base);
			iep->ie_cb_size = IE_IORAM_SIZE;
			ie_ioc_init();
		} else {

			/*
			 * We are on the motherboard of a 4/1XX or 4/2XX.
			 */
			iep->ie_dscpthandle = h;
			iep->ie_cb_base = iopb;
			iep->ie_cb_size = ptob(1) - sizeof (struct iescp);
		}
	} else {

		/*
		 * 3E board.
		 */
		iep->ie_cb_base = (caddr_t) ((u_long) iep->ie_ram +
			IE_TE_MEMSIZE - IE_TE_DES_SIZE);
		iep->ie_scp = (struct iescp *) ((u_long) iep->ie_ram +
			IE_TE_MEMSIZE - sizeof (struct iescp));
		iep->ie_cb_size = IE_TE_DES_SIZE - sizeof (struct iescp);
	}

	/*
	 * Memory map:
	 *
	 *	+-----------------------+ <- ie_tbdlim
	 *	|	xmit buf des	|
	 *	+-----------------------+ <- ie_tbdring = ie_tcblim
	 *	|	xmit cmd blk	|
	 *	+-----------------------+ <- ie_tcbring = ie_rbdlim
	 *	|	recv buf des	|
	 *	+-----------------------+ <- ie_rbdring = ie_rfdlim
	 *	|	recv frame des	|
	 *	+-----------------------+ <- ie_rfdring
	 *	|	SCB		|
	 *	+-----------------------+ <- ie_scbp
	 *	|	ISCP		|
	 *	+-----------------------+ <- ie_iscp
	 *	|	sync cmd blk	|
	 *	+-----------------------+ <- ie_cbsyncp
	 */
	tmpp = iep->ie_cb_base;
	iep->ie_cbsyncp = (struct iecb *) tmpp;
	size = max(sizeof (struct iecb), sizeof (struct iemcaddr));
	size = max((u_int)size, sizeof (struct ieiaddr));
	size = roundup(size, I82586ALIGN);
	tmpp += size;

	iep->ie_iscp = (struct ieiscp *) tmpp;
	tmpp += roundup(sizeof (struct ieiscp), I82586ALIGN);

	iep->ie_scbp = (struct iescb *) tmpp;
	tmpp += roundup(sizeof (struct iescb), I82586ALIGN);

	/*
	 * Set up descriptor pools.
	 */
	iep->ie_rfdring = (ierfd_t *) tmpp;
	size = IE_NRECV * sizeof (ierfd_t);
	tmpp += size;
	iep->ie_rfdlim = (ierfd_t *) tmpp;

	iep->ie_rbdring = (ierbd_t *) tmpp;
	size = IE_NRECV * sizeof (ierbd_t);
	tmpp += size;
	iep->ie_rbdlim = (ierbd_t *) tmpp;

	iep->ie_tcbring = (ietcb_t *) tmpp;
	size = IE_NXMIT * sizeof (ietcb_t);
	tmpp += size;
	iep->ie_tcblim = (ietcb_t *) tmpp;

	iep->ie_tbdring = (ietbd_t *) tmpp;
	size = IE_NXMIT * sizeof (ietbd_t);
	tmpp += size;
	iep->ie_tbdlim = (ietbd_t *) tmpp;

	if (iedebug) {
		ierror(iep->ie_dip, "iep %x scb_base %x cs_size %x", iep,
			(char *)iep->ie_cb_base, (char *)iep->ie_cb_size);
		ierror(iep->ie_dip, "scp %x iscp %x scb %x synccb %x",
			(char *)iep->ie_scp, (char *)iep->ie_iscp,
			(char *)iep->ie_scbp, (char *)iep->ie_cbsyncp);
		ierror(iep->ie_dip, "rfd %x rbd %x tcb %x tbd %x",
			(char *)iep->ie_rfdring, (char *)iep->ie_rbdring,
			(char *)iep->ie_tcbring, (char *)iep->ie_tbdring);
	}

	/*
	 * Now make sure that everything is within our control space size.
	 */
	size = (u_long) tmpp - (u_long) iep->ie_cb_base;
	if (size > iep->ie_cb_size) {
		panic("ieallocmem: ctl space overflow");
		/*NOTREACHED*/
	}

	bzero(iep->ie_cb_base, iep->ie_cb_size);
	if (iebufalloc(iep))
		return (1);
	return (0);
}

/*
 * Allocate the receive and transmit buffer pool and related tables of pointers.
 * Return 0 on success, 1 otherwise.
 */
static int
iebufalloc(iep)
register ie_t *iep;
{
	int i;
	iebuf_t *ibp;

	if (ISSLAVE) {

		/*
		 * All the buffers for the 3E board needs to be allocated
		 * from the RAM on the slave card since the 82586 can only
		 * access that area.
		 */
		iep->ie_bufbase = (struct  iebuf   *)iep->ie_ram;
		bzero((caddr_t)iep->ie_bufbase,
			IE_NBUF * sizeof (iebuf_t));
	} else {
		u_int amt = IE_NBUF * sizeof (iebuf_t);
		u_int realamt;
		ddi_dma_handle_t h;
		caddr_t addr;

		/*
		 * The buffers are allocated from the main memory for
		 * the 82586 on the motherboard.
		 */
		if (ddi_mem_alloc(iep->ie_dip, &ie_dma_lim, amt, 1,
			&addr, &realamt)) {
			ierror(iep->ie_dip, "iebufalloc: no memory");
			return (1);
		}
		iep->ie_bufbase = (iebuf_t *)addr;
		if (ddi_dma_addr_setup(iep->ie_dip, (struct as *) 0, addr,
			realamt, DDI_DMA_RDWR, DDI_DMA_SLEEP, (caddr_t) 0,
			&ie_dma_lim, &h)) {
			ierror(iep->ie_dip, "iebufalloc: no dma map");
			return (1);
		}
		iep->ie_bufhandle = h;
	}

	/*
	 * Allocate buffer pointer stack (filo).
	 */
	iep->ie_buftab = GETSTRUCT(iebuf_t *, IE_NBUF);

	/*
	 * Initialize buffer pointer stack.
	 */
	for (i = 0; i < IE_NBUF; i++) {
		ibp = &((iebuf_t *)iep->ie_bufbase)[i];
		ibp->ib_iep = iep;
		ibp->ib_frtn.free_func = iefreebuf;
		ibp->ib_frtn.free_arg = (char *) ibp;
		iefreebuf(ibp);
	}

	/*
	 * Allocate and zero out xmit and rcv holders.
	 */
	iep->ie_tmblk = GETSTRUCT(mblk_t *, IE_NXMIT);
	iep->ie_rbp = GETSTRUCT(iebuf_t *, IE_NRECV);
	iep->ie_tbp = GETSTRUCT(iebuf_t *, IE_NRECV);

	return (0);
}

/*
 * Clean up the IOC data and tags for Sun4/4XX.  Sun4/4XX will hang if we
 * do not do this for diskless clients.
 */
static void
ie_ioc_init()
{
	register u_long *p, tag;

	/* zero descriptor data */
	p = (u_long *)IOC_IEDESCR_ADDR;
	while (p < (u_long *) (IOC_IEDESCR_ADDR+0x1000))
		*p++ = 0;

	/*
	 * Initialize descriptor tags,
	 * Bit 0 is the modified bit, bit 1 is the valid bit.
	 * Bits 2-4 aren't used and the rest are virtual address bits.
	 */
	p = (u_long *)(IOC_IEDESCR_ADDR+0x1000);
	tag = (u_long)(IOC_IEDESCR_ADDR+3);
	while ((u_long)p < IOC_IEDESCR_ADDR+0x2000) {
		*p = tag;
		p++;
		if ((p != (u_long *)(IOC_IEDESCR_ADDR+0x1000)) &&
			(((u_long)p%32) == 0))
			tag += IOC_LINESIZE;
	}
}

/*
 * Basic 82586 initialization.  Return 0 on success, 1 otherwise.
 */
static int
iechipinit(iep)
register ie_t *iep;
{
	volatile struct ieiscp *iscp = iep->ie_iscp;
	volatile struct iescb *scbp = iep->ie_scbp;
	volatile struct iescp *scp = iep->ie_scp;
	int i;
	int gotintr;
	int error = 0;
	struct ieconf *ic;
	int failed = 0;

reset:
	bzero((caddr_t)scp, sizeof (struct iescp));
	PUT_IEADDR(iep, (caddr_t)iscp, &scp->iescp_iscp);
	bzero((caddr_t)iscp, sizeof (struct ieiscp));
	iscp->ieiscp_busy = 1;
	PUT_IEADDR(iep, (caddr_t)iep->ie_cb_base, &iscp->ieiscp_cbbase);
	iscp->ieiscp_scb = toieoff(iep, (caddr_t)scbp);
	bzero((caddr_t)scbp, sizeof (struct iescb));

#ifdef	notdef
	if (ISMASTER)
		IE_DEV_SYNC(iep, iep->ie_cb_base, iep->ie_cb_size);
#endif

	/*
	 * Hardware reset the chip.  We make the interval from
	 * reset to initial channel attention as small as reasonable
	 * to reduce the risk of scribbling chips getting us.
	 */
	if (ISMASTER) {
		*IE_OBCSR |= OBIE_NORESET;
		drv_usecwait(IEKLUDGE);			/* required delay */
	}
	IECA(iep);

	/*
	 * Ensure chip updates both iscp and scb and turns on the interrupt bit.
	 */
	if (ISSLAVE) {
		CDELAY(!iscp->ieiscp_busy, IEDELAY);
		CDELAY(scbp->iescb_status & IESCB_CNA, IEDELAY);
	} else {

		/*
		 * We have to sync for dma master.
		 */
		i = IEDELAY;
		while (i-- > 0) {
			IE_CPU_SYNC(iep, iep->ie_cb_base, iep->ie_cb_size);
			if (!iscp->ieiscp_busy)
				break;
			drv_usecwait(1);
		}
		i = IEDELAY;
		while (i-- > 0) {
			IE_CPU_SYNC(iep, iep->ie_cb_base, iep->ie_cb_size);
			if (scbp->iescb_status & IESCB_CNA)
				break;
			drv_usecwait(1);
		}
	}

	gotintr = iewaitintr(iep);
	if (!gotintr || iscp->ieiscp_busy || ! scbp->iescb_status & IESCB_CNA) {

		/*
		 * Give the chip another chance.
		 */
		failed++;
		if (failed < 3) {
			drv_usecwait(1000);
			iechipreset(iep);
			drv_usecwait(1000);
			goto reset;
		}

		ierror(iep->ie_dip, "chipinit:  %s%s%s",
			iscp->ieiscp_busy	? " iscp busy" : "",
			!scbp->iescb_status & IESCB_CNA	? " no cnr" : "",
			!gotintr		? " no intr" : "");
		goto exit;
	}

	if (scbp->iescb_status & IESCB_CUS != IECUS_IDLE) {
		ierror(iep->ie_dip, "chipinit: cus not idle after reset");
		iechipreset(iep);
		goto reset;
	}

	/*
	 * Issue a synchronous DIAGNOSE command.
	 */
	bzero((caddr_t)iep->ie_cbsyncp, sizeof (struct iecb));
	((struct iecb *)iep->ie_cbsyncp)->iecb_cmd = IE_DIAGNOSE;
	if (iesynccmd(iep)) {
		error = 1;
		goto exit;
	}
	if (!((struct iecb *)iep->ie_cbsyncp)->iecb_status & IECB_OK) {
		ierror(iep->ie_dip, "chipinit: 82586 failed diagnostics");
		goto reset;
	}

	/*
	 * Configure the chip.  Notice that sizeof (Conigure command block)
	 * is great than sizeof (Transmit command block).
	 */
	ic = (struct ieconf *)iep->ie_cbsyncp;
	iedefaultconf(ic);
	if (iep->ie_flags & IEPROMISC)
		ic->ieconf_data3 |= IECONF_PROMISC;
	if (iesynccmd(iep))
		error = 1;
	else
		error = 0;

exit:
	return (error);
}

/*
 * Wait for an interrupt and relay results.
 */
static int
iewaitintr(iep)
register ie_t *iep;
{
	int ok;

	if (ISMASTER) {
		CDELAY(*IE_OBCSR & OBIE_INTR, IEDELAY);
		ok = *IE_OBCSR & OBIE_INTR;
	} else {
		CDELAY(*IE_TECSR & TIE_INTR, IEDELAY);
		ok = *IE_TECSR & TIE_INTR;
	}
	return (ok);
}

/*
 * Set default configuration parameters.
 */
static void
iedefaultconf(ic)
register struct ieconf *ic;
{
	bzero((caddr_t)ic, sizeof (*ic));
	ic->ieconf_cb.iecb_cmd = IE_CONFIG | IECB_EL;
	ic->ieconf_bytes = 12;
	ic->ieconf_data0 = 2 << 4;	/* preamble length code */
	ic->ieconf_data0 |= IECONF_ACLOC;
	ic->ieconf_data0 |= 6;		/* address length */
	ic->ieconf_space = 96;
	ic->ieconf_data2 = 512 >> 8;	/* slot time */
	ic->ieconf_data2 |= 15 << 4;	/* # xmit retries */
	ic->ieconf_minfrm = 64;
	ic->ieconf_data3 = 3;		/* carrier filter bits */

	/* XXX Should this be 13 for Sun4/4XX? */
	ic->ieconf_fifolim = 12;
}

/*
 * Initialize tcbs, tbds, rfds, and rbds.
 */
static void
iedescriptorinit(iep)
register ie_t *iep;
{
	ietbd_t *tbd;
	ietcb_t *tcb;
	ierbd_t *rbd;
	ierfd_t *rfd;
	int i;
	iebuf_t *ibp;

	/*
	 * Set up the transmit descriptors so that all the tcbs form a ring
	 * and every tcb points to a tbd.
	 */
	for (tcb = iep->ie_tcbring, tbd = iep->ie_tbdring;
		tcb < iep->ie_tcblim; tcb++, tbd++) {
		tcb->ietcb_status = 0;
		tcb->ietcb_command = IETCB_EL | IE_TRANSMIT;
		tcb->ietcb_tbd = toieoff(iep, (caddr_t)tbd);
		if ((tcb + 1) < iep->ie_tcblim)
			tcb->ietcb_next = toieoff(iep, (caddr_t) (tcb + 1));
		else
			tcb->ietcb_next = toieoff(iep,
				(caddr_t)iep->ie_tcbring);
		tbd->ietbd_eofcnthi = IETBD_EOF;
	}
	iep->ie_tcbclaimed = iep->ie_tcbtl = iep->ie_tcbring;

	/*
	 * Boundary condition: We have to turn the bit on for tcb[0] because
	 * iecustart() looks for the first tcb which is not complete.
	 */
	iep->ie_tcbring->ietcb_status = IECB_DONE;

	/*
	 * Link the rbds together into a ring and initialize all the rbds.
	 */
	for (rbd = iep->ie_rbdring, i = 0; i < IE_NRECV;
		rbd++, i++) {
		ibp = iegetbuf(iep, 1);
		if (ibp == NULL)
			panic("iedescriptorinit: buffer not enough");
		ierbufinit(iep, rbd, ibp);
		rbd->ierbd_status = 0;
		iep->ie_rbp[i] = ibp;
		if ((rbd + 1) == iep->ie_rbdlim) {
			rbd->ierbd_elsize |= IERBD_EL;
			rbd->ierbd_next = toieoff(iep,
				(caddr_t)iep->ie_rbdring);
			iep->ie_rbdtl = rbd;
		} else
			rbd->ierbd_next = toieoff(iep, (caddr_t) (rbd + 1));
	}
	iep->ie_rbdhd = iep->ie_rbdring;

	/*
	 * Link the rfds together into a ring and initialize all the rfds.
	 */
	for (rfd = iep->ie_rfdring; rfd < iep->ie_rfdlim; rfd++) {
		rfd->ierfd_status = 0;
		rfd->ierfd_command = 0;
		rfd->ierfd_rbd = IENULLOFF;
		if ((rfd + 1) == iep->ie_rfdlim) {
			rfd->ierfd_command |= IERFD_EL;
			rfd->ierfd_next = toieoff(iep,
				(caddr_t)iep->ie_rfdring);
			iep->ie_rfdtl = rfd;
		} else
			rfd->ierfd_next = toieoff(iep, (caddr_t) (rfd + 1));
	}
	iep->ie_rfdhd = iep->ie_rfdring;
	iep->ie_rfdhd->ierfd_rbd = toieoff(iep, (caddr_t)iep->ie_rbdhd);

#ifdef	notdef
	/*
	 * Synchronize the whole command area (overkill)
	 */
	if (ISMASTER)
		IE_DEV_SYNC(iep, iep->ie_cb_base, iep->ie_cb_size);
#endif
}

/*
 * Initialize an rbuf.
 */
static void
ierbufinit(iep, rbdp, rbufp)
struct	ie	*iep;
ierbd_t *rbdp;
iebuf_t *rbufp;
{
	caddr_t buffer;

	buffer = (caddr_t) ((u_long) rbufp + IERBUFOFF);
	PUT_IEADDR(iep, buffer, &rbdp->ierbd_buf);
	rbdp->ierbd_elsize = (IEBUFSIZE - IERBUFOFF) >> 8;
	rbdp->ierbd_sizelo = (IEBUFSIZE - IERBUFOFF) & 0xff;
}

/*
 * Do the command contained in cb synchronously, so that we know
 * it's complete upon return.  Return 0 on success, 1 otherwise.
 */
static int
iesynccmd(iep)
register ie_t *iep;
{
	volatile struct iecb *cbp = (struct iecb *)iep->ie_cbsyncp;
	volatile struct iescb *scbp = iep->ie_scbp;
	volatile struct iecb *icp;
	u_short cmd = 0;

	/*
	 * Wait until all the transmissions are done and the deadman timer
	 * expires.  We use IEDELAY here since it is greater than iedraintime.
	 */
	if ((iep->ie_scbp->iescb_status & IESCB_CUS) == IECUS_ACTIVE) {
		icp = (struct iecb *)iep->ie_tcbtl;
		CDELAY(icp->iecb_status & IECB_DONE, IEDELAY);
		if (! icp->iecb_status & IECB_DONE) {
			cmn_err(CE_CONT,
				"iesynccmd: can't drain all the xmit frames\n");
			return (1);
		} else
			iecuclean(iep);
	}

	cbp->iecb_status = 0;

	/* XXX Do we need to enable the interrupt? */
	cbp->iecb_cmd |= IECB_EL;
	scbp->iescb_cbl = toieoff(iep, (caddr_t)cbp);
	scbp->iescb_cmd |= IECMD_CU_START;
	IECA(iep);

	/* Give it a chance to complete. */
	CDELAY(cbp->iecb_status & IECB_DONE, IEDELAY);
	if (!(cbp->iecb_status & IECB_DONE)) {
		cmn_err(CE_CONT, "iesynccmd: can't complete the command\n");
		return (1);
	}

	/* Acknowledge status information from the chip. */
	if (scbp->iescb_cmd && iechkcca(iep) == 0) {
		return (1);
	}
	if (scbp->iescb_status & IESCB_CX)
		cmd |= IECMD_ACK_CX;
	if (scbp->iescb_status & IESCB_CNA)
		cmd |= IECMD_ACK_CNA;
	scbp->iescb_cmd = cmd;
	IECA(iep);
	return (0);
}

/*
 * Start to transmit the frame.  Among other things, set up tcb, tbd and
 * call ie_custart().  Return 0 upon success, 1 upon failure.
 */
static int
iestart(wq, mp, iep)
queue_t	*wq;
mblk_t *mp;
register ie_t *iep;
{
	int tcbi;
	volatile ietbd_t *tbdp;
	int	len;
	mblk_t *nmp = NULL;
	ietcb_t *ntcbp;
	iebuf_t *ibp;
	caddr_t	addr;
	int	flags = iep->ie_flags;

	if (flags & IEPROMISC)
		if ((nmp = copymsg(mp)) == NULL)
			iep->ie_allocbfail++;

	len = msgsize(mp);
	if (len > ETHERMAX) {
		if (iedebug)
			ierror(iep->ie_dip, "msg too big:  %d", (char *)len);
		iep->ie_oerrors++;
		freemsg(mp);
		if (nmp)
			freemsg(nmp);
		return (1);
	}

	addr = (caddr_t) mp->b_rptr;

	/*
	 * Acquire per-device lock now and hold it until done
	 * to block out an otherwise possible ieinit().
	 */
	mutex_enter(&iep->ie_devlock);

	ntcbp = NEXTTCB(iep, iep->ie_tcbtl);

	if (ntcbp == iep->ie_tcbclaimed) {
		iecuclean(iep);
		ntcbp = NEXTTCB(iep, iep->ie_tcbtl);

		if (ntcbp == iep->ie_tcbclaimed) {

			/* out of tcbs */
			iep->ie_notcbs++;
			(void) putbq(wq, mp);
			iep->ie_wantw = 1;
			mutex_exit(&iep->ie_devlock);
			if (nmp)
				freemsg(nmp);
			if (iedebug)
				ierror(iep->ie_dip, "out of tcbs");
			return (1);
		}
	}

	/*
	 * If more than one mblk,
	 * or we are on a 3/E,
	 * or this is a Sun4/4XX,
	 * or data is not within "ok" range,
	 * or extending the data to the minimum frame size would
	 * extend past the end of the buffer,
	 * then copy it to a new buffer.
	 */
	if (mp->b_cont ||
		(flags & IESLAVE) ||
		iep->ie_iocache ||
		(mp->b_rptr < (u_char *) iep->ie_bufbase) ||
		(mp->b_rptr + ETHERMIN >= DB_LIM(mp))) {

		/*
		 * Allocate new buffer.
		 */
		if ((ibp = iegetbuf(iep, 1)) == NULL) {
			if (iedebug)
				ierror(iep->ie_dip, "no xmit buf");
			iep->ie_notbufs++;
			(void) putbq(wq, mp);
			iep->ie_wantw = 1;
			mutex_exit(&iep->ie_devlock);
			if (nmp)
				freemsg(nmp);
			return (1);
		}

		/*
		 * Copy msg into the buf and free the original msg.
		 */
		addr = (caddr_t) ibp->ib_buf;
		if (mp->b_cont)

			/* slow, unaligned copy */
			mcopymsg(mp, (u_char *)addr);
		else {

			/* fast aligned copy */
			addr = (caddr_t) ((int) addr +
				((u_int)mp->b_rptr & IEBURSTMASK));
			bcopy((caddr_t) mp->b_rptr, addr, len);
			freemsg(mp);
		}
		mp = NULL;
	}

	/*
	 * Pad out to ETHERMIN.
	 */
	if (len < ETHERMIN)
		len = ETHERMIN;

	/*
	 * Point the tbd at this buffer.
	 */
	tcbi = ntcbp - iep->ie_tcbring;
	tbdp = iep->ie_tbdring + tcbi;
	PUT_IEADDR(iep, addr, &tbdp->ietbd_buf);
	tbdp->ietbd_cntlo = len & 0xff;
	tbdp->ietbd_eofcnthi = IETBD_EOF | (len >> 8);

	/*
	 * Save pointer to this msg or xmit buf to free later.
	 */
	if (mp)
		iep->ie_tmblk[tcbi] = mp;
	else
		iep->ie_tbp[tcbi] = ibp;

	/*
	 * Sync the transmit buffer.  Not required for sun4.
	 */
#ifdef notdef
	if (iep->ie_bufhandle)
		ddi_dma_sync(iep->ie_bufhandle,
			(off_t)((u_long)addr - (u_long)iep->ie_bufbase),
			len, DDI_DMA_SYNC_FORDEV);
#endif

	/*
	 * If we clear the status word in iexmitdone() instead of here,
	 * a frame could be sent twice.
	 */
	ntcbp->ietcb_status = 0;
	ntcbp->ietcb_command |= IECB_EL | IECB_INTR;

	/*
	 * Only the last frame in the tcb chain should interrupt the
	 * CPU.  Otherwise, a race condition may show up and some
	 * frames may be transmitted twice.
	 */
	iep->ie_tcbtl->ietcb_command &= ~(IECB_EL | IECB_INTR);
	iep->ie_tcbtl = ntcbp;

	iecustart(iep);

	if (nmp != NULL)
		iesendup(iep, nmp, iepaccept);

	/*
	 * Now it's ok to release the lock.
	 */
	mutex_exit(&iep->ie_devlock);

	return (0);
}

/*
 * Append the transmit request and start the CU with the first CBL.
 */
static void
iecustart(iep)
ie_t *iep;
{
	volatile struct iescb *scbp = iep->ie_scbp;
	volatile ietcb_t *hdcp;

	if (scbp->iescb_cmd && iechkcca(iep) == 0)
		return;

	if (iep->ie_tcbtl->ietcb_status & IECB_DONE)
		return;

	/*
	 * There is a race condition here:
	 *
	 * The chip may have just finished xmiting the last frame
	 * here.  Right before the chip updates the SCB, the CPU links
	 * a new tcb, notices the chip is busy and return.  The chip
	 * then updates the SCB to shows it is idle - the last frame
	 * will not be xmited until the next time iecustart() is called.
	 * This race condition causes NFS to time out.
	 *
	 * If we always restart the chip when we have a frame to xmit,
	 * another race condition can cause a frame to be sent twice
	 * which is bad for the performance.
	 *
	 * The solution is to also call iecustart() at the xmit
	 * completion interrupt time.
	 */
	if ((scbp->iescb_status & IESCB_CUS) == IECUS_ACTIVE)
		return;

	/*
	 * XXX Assume the I/O cache is enabled, flush the outgoing
	 * ethernet line.  This insures that the buffers we are about
	 * to add to the queue are not already in the I/O cache.
	 */
	if (iep->ie_iocache)
		iep->ie_iocache[IOC_FLUSH_OUT] = 0;

	/*
	 * Find the first active command in the command list.
	 */
	hdcp = iep->ie_tcbclaimed;
	while (hdcp->ietcb_status & IETCB_DONE)
		if (hdcp == iep->ie_tcbtl)
			return;
		else
			hdcp = NEXTTCB(iep, hdcp);

	scbp->iescb_cbl = toieoff(iep, (caddr_t)hdcp);
	scbp->iescb_cmd = IECMD_CU_START;
	IECA(iep);
}

/*
 * Process completed CBs, reclaiming specified storage.
 */
static void
iecuclean(iep)
ie_t *iep;
{
	ietcb_t *tcbp;

	while ((tcbp = NEXTTCB(iep, iep->ie_tcbclaimed)) != NULL &&
		tcbp->ietcb_status & IETCB_DONE) {
		if ((tcbp->ietcb_command & IECB_CMD) == IE_TRANSMIT)
			iexmitdone(iep, tcbp);
		else
			ierror(iep->ie_dip, "unknown cmd %x done\n",
				(char *)(tcbp->ietcb_command & IECB_CMD));
		iep->ie_tcbclaimed = tcbp;

		/*
		 * The reclaim pointer should never go beyond the tail
		 * pointer.
		 */
		if (iep->ie_tcbclaimed == iep->ie_tcbtl)
			break;
	}
}

/*
 * Update error counters and free up resources after transmitting a packet.
 * Called by iecuclean().
 *
 * Under certain conditions we're willing to make a second try at sending
 * a packet when the first attempt fails.  The constraints are: the facility
 * is enabled in the first place; no packet has gone out since the failed
 * one (this prevents out of order transmissions); and we haven't already
 * retried the packet (this prevents the driver from eating up the entire
 * system when the net breaks).
 */
static void
iexmitdone(iep, tcbp)
ie_t *iep;
ietcb_t *tcbp;
{
	static	int	opackets;
	int    canretransmit = (tcbp->ietcb_command & IETCB_EL) &&
				(iep->ie_opackets != opackets);
	ietbd_t *tbdp;
	volatile ietbd_t *realtbdp;
	mblk_t *mp;
	static int nocar = 0;
	int tcbi;

#ifdef	notdef
	if (ISMASTER)
		IE_DEV_SYNC(iep, tcbp, sizeof (*tcbp));
#endif
	if (tcbp->ietcb_status & IETCB_OK) {
		iep->ie_collisions +=
			((int)(tcbp->ietcb_status & IETCB_NCOLL) >> 8);
		iep->ie_opackets++;
		if (tcbp->ietcb_status & IETCB_DEFER)
			iep->ie_defer++;
		if (tcbp->ietcb_status & IETCB_HEART)
			iep->ie_heart++;
	} else {
		iep->ie_oerrors++;
		if (tcbp->ietcb_status & IETCB_XCOLL) {
			iep->ie_trtry++;
			if (!canretransmit)
				ienetjammed(iep);
		}
		if (tcbp->ietcb_status & IETCB_NOCARR) {
			iep->ie_tnocar++;
			if (!canretransmit)
				if (iedebug || ++nocar == 5) {
					ierror(iep->ie_dip, "no carrier");
					nocar = 0;
				}
		} else
			nocar = 0;
		if (tcbp->ietcb_status & IETCB_NOCTS) {
			iep->ie_tnocts++;
			if (!canretransmit)
				ierror(iep->ie_dip, "no CTS");
		}
		if (tcbp->ietcb_status & IETCB_UNDERRUN) {
			iep->ie_xmiturun++;
			if (iedebug)
				ierror(iep->ie_dip, "xmit underrun");
		}
	}

	/*
	 * Check for tbd misaligned.
	 */
	tbdp = (ietbd_t *) fromieoff(iep, (ieoff_t)tcbp->ietcb_tbd);
	tcbi = tcbp - iep->ie_tcbring;
	realtbdp = iep->ie_tbdring + tcbi;
	if (tbdp != realtbdp) {
		ierror(iep->ie_dip, "tbdp %x, realtbdp %x, tbdring %x",
			(char *)tbdp, (char *)realtbdp,
			(char *)iep->ie_tbdring);
		panic("iexmitdone: tbds out of sync");
	}

	/*
	 * Requeue if necessary and possible.  If successful,
	 * ierequeue will set pendreque.
	 */
	if (!(tcbp->ietcb_status & IETCB_OK) && canretransmit) {
		opackets = iep->ie_opackets;

		if (ierequeue(iep, tcbp, tbdp))

			/*
			 * No need to free the message if ie_requeue()
			 * succeeds.
			 */
			return;
	}

	/*
	 * Free any held msg.
	 */
	if ((mp = iep->ie_tmblk[tcbi]) != NULL) {
		freemsg(mp);
		iep->ie_tmblk[tcbi] = NULL;
	} else if (iep->ie_tbp[tcbi]) {
		iefreebuf(iep->ie_tbp[tcbi]);
		iep->ie_tbp[tcbi] = NULL;
	}

	if (iep->ie_wantw)
		iewenable(iep);
}

/*
 * Patchable period to suppress the net jammed message
 * after we print it out once.
 */
static	int	iesuptime = 3600;

void
ienetjammed(iep)
ie_t *iep;
{
	if (iedebug)
		ierror(iep->ie_dip, "Ethernet jammed");
	else {
		if ((hrestime.tv_sec - iep->ie_jamtime) < iesuptime)
			return;
		else {
			ierror(iep->ie_dip, "Ethernet jammed");
			iep->ie_jamtime = hrestime.tv_sec;
		}
	}
}

/*
 * Put the frame at the tail of the tcb ring and try to transmit again.
 * Return 1 if the requeue succeeds, 0 otherwise.
 */
static int
ierequeue(iep, tcbp, tbdp)
ie_t *iep;
ietcb_t *tcbp;
ietbd_t *tbdp;
{
	int tcbi, ntcbi;
	ietcb_t *ntcbp;
	volatile ietbd_t *ntbdp;

	ntcbp = NEXTTCB(iep, iep->ie_tcbtl);
	if (ntcbp == iep->ie_tcbclaimed)
		/*
		 * No tcb left.  Simply give up.
		 */
		return (0);

	/*
	 * Copy the current tcb to the tail tcb.
	 */
	ntcbp->ietcb_command = tcbp->ietcb_command;
	ntcbi = ntcbp - iep->ie_tcbring;
	ntbdp = iep->ie_tbdring + ntcbi;
	*ntbdp = *tbdp;
	tcbi = tcbp - iep->ie_tcbring;
	if (iep->ie_tmblk[tcbi]) {
		iep->ie_tmblk[ntcbi] = iep->ie_tmblk[tcbi];
		iep->ie_tmblk[tcbi] = NULL;
	} else if (iep->ie_tbp[tcbi]) {
		iep->ie_tbp[ntcbi] = iep->ie_tbp[tcbi];
		iep->ie_tbp[tcbi] = 0;
	}

	/*
	 * If we clear the status word in iexmitdone() instead of here,
	 * a frame could be sent twice.
	 */
	ntcbp->ietcb_status = 0;
	ntcbp->ietcb_command |= IECB_EL | IECB_INTR;

	/*
	 * Only the last frame in the tcb chain should interrupt the
	 * CPU.  Otherwise, a race condition may show up and some
	 * frames may be transmitted twice.
	 */
	iep->ie_tcbtl->ietcb_command &= ~(IECB_EL | IECB_INTR);
	iep->ie_tcbtl = ntcbp;

	iecustart(iep);

	return (1);
}

/*
 * Tell the chip to start the receive unit.
 */
static void
ierustart(iep)
ie_t *iep;
{
	volatile struct iescb *scbp = iep->ie_scbp;
	int retry = 0;

	/*
	 * Get the latest status of the scb.
	 */
#ifdef	notdef
	if (ISMASTER)
		IE_CPU_SYNC(iep, scbp, sizeof (scbp));
#endif

	if ((scbp->iescb_status & IESCB_RUS) != IERUS_IDLE) {
		ierror(iep->ie_dip, "RU unexpectedly not idle");
		if (scbp->iescb_cmd)
			(void) iechkcca(iep);
		scbp->iescb_cmd |= IECMD_RU_ABORT;
#ifdef	notdef
		if (ISMASTER)
			IE_DEV_SYNC(iep, scbp, sizeof (scbp));
#endif
		IECA(iep);
	}
	if (scbp->iescb_cmd && iechkcca(iep) == 0)
		return;
	scbp->iescb_rfa = toieoff(iep, (caddr_t)iep->ie_rfdhd);

restart:
	scbp->iescb_cmd |= IECMD_RU_START;

#ifdef	notdef
	if (ISMASTER)
		IE_DEV_SYNC(iep, scbp, sizeof (scbp));
#endif

	IECA(iep);
	CDELAY((scbp->iescb_status & IESCB_RUS) == IERUS_READY, IEDELAY);

	if ((scbp->iescb_status & IESCB_RUS) != IERUS_READY) {

		/*
		 * Give the chip another chance if it fails during the first
		 * time.  This fixed some weird failures.
		 */
		if ((scbp->iescb_status & IESCB_RUS) == IERUS_IDLE &&
			retry == 0) {
			retry++;
			goto restart;
		} else {
			ierror(iep->ie_dip, "RU did not become ready");
		}
	}
}

/*
 * Tell the chip its Ethernet address and the set of multicast
 * addresses it should recognize.  Return 0 on success, 1 otherwise.
 */
static int
iedoaddr(iep)
ie_t *iep;
{
	struct iestr *isp;
	int i;
	int mc_exist = 0;
	caddr_t addr;
	struct iemcaddr *imc;
	u_short mcount = 0;

	bzero((caddr_t)iep->ie_cbsyncp, sizeof (struct ieiaddr));
	((struct iecb *)iep->ie_cbsyncp)->iecb_cmd = IE_IADDR;
	*(struct ether_addr *)((struct ieiaddr *)iep->ie_cbsyncp)->ieia_addr =
		iep->ie_ouraddr;
	if (iesynccmd(iep))
		return (1);

	/*
	 * Set the multicast addresses for the chip.
	 */
	for (isp = iestrup; isp; isp = isp->is_nextp) {
		if (isp->is_iep != iep)
			continue;
		if (isp->is_flags & ISALLMULTI) {

			/*
			 * Fill the multicast address vector with addresses
			 * which turn on all 64 hash filter bits.  It just
			 * so happens that starting with the address
			 * 01:00:00:00:00:00 and adding 2 to the first byte,
			 * up to 7f:00:00:00:00:00, hits all the hash bits.
			 */
			imc = (struct iemcaddr *)iep->ie_cbsyncp;
			bzero((caddr_t)imc, sizeof (struct iemcaddr));
			for (i = 0; i < IEMCADDRMAX; i++)
				imc->iemc_addr[i*IEETHERADDRL] = i * 2 + 1;
			mcount = IEMCADDRMAX * IEETHERADDRL;
			mc_exist++;
			break;
		} else if (isp->is_mccount > 0) {
			if (!mc_exist) {
				imc = (struct iemcaddr *)iep->ie_cbsyncp;
				addr = (caddr_t) imc->iemc_addr;
				bzero((caddr_t)iep->ie_cbsyncp,
					sizeof (struct iemcaddr));
				mc_exist++;
			}
			for (i = 0; i < isp->is_mccount; i++) {
				bcopy((caddr_t)&isp->is_mctab[i], addr,
					ETHERADDRL);
				addr += ETHERADDRL;
			}
			mcount += isp->is_mccount * IEETHERADDRL;
		}
	}
	if (mc_exist) {
		imc->iemc_cb.iecb_cmd = IE_MADDR;
		imc->iemc_count = toieint(mcount);
		if (iesynccmd(iep))
			return (1);
	}
	return (0);
}

/*ARGSUSED*/
static
ieopen(rq, devp, flag, sflag, credp)
queue_t	*rq;
dev_t	*devp;
int	flag;
int	sflag;
cred_t	*credp;
{
	struct iestr *isp;
	struct iestr **previsp;
	int minordev;
	int rc = 0;

	ASSERT(rq);
	ASSERT(sflag != MODOPEN);

	/*
	 * Serialize all driver open and closes.
	 */
	rw_enter(&iestruplock, RW_WRITER);

	/*
	 * Determine minor device number.
	 */
	previsp = &iestrup;
	if (sflag == CLONEOPEN) {
		minordev = 0;
		for (; (isp = *previsp) != NULL; previsp = &isp->is_nextp) {
			if (minordev < isp->is_minor)
				break;
			minordev++;
		}
		*devp = makedevice(getmajor(*devp), minordev);
	} else
		minordev = getminor(*devp);

	if (rq->q_ptr)
		goto done;

	isp = GETSTRUCT(struct iestr, 1);

	isp->is_minor = minordev;
	isp->is_rq = rq;
	isp->is_state = DL_UNATTACHED;
	isp->is_sap = 0;
	isp->is_flags = 0;
	isp->is_iep = NULL;
	isp->is_mccount = 0;
	isp->is_mctab = NULL;
	mutex_init(&isp->is_lock, "ie stream lock", MUTEX_DRIVER, (void *)0);

	/*
	 * Link new entry into the list of active entries.
	 */
	isp->is_nextp = *previsp;
	*previsp = isp;

	rq->q_ptr = WR(rq)->q_ptr = (char *) isp;

	/*
	 * Disable our write-side service procedure.
	 */
	noenable(WR(rq));

done:
	rw_exit(&iestruplock);
	qprocson(rq);
	return (rc);
}

static
ieclose(rq)
queue_t	*rq;
{
	struct iestr *isp;
	struct iestr **previsp;

	ASSERT(rq);
	ASSERT(rq->q_ptr);

	qprocsoff(rq);

	isp = (struct iestr *) rq->q_ptr;

	/*
	 * Implicit detach Stream from interface.
	 */
	if (isp->is_iep) {
		mutex_enter(&isp->is_lock);
		iedodetach(isp);
		mutex_exit(&isp->is_lock);
	}

	/* XXX should use rwlock */
	rw_enter(&iestruplock, RW_WRITER);

	/*
	 * Unlink the per-Stream entry from the active list and free it.
	 */
	for (previsp = &iestrup; (isp = *previsp) != NULL;
		previsp = &isp->is_nextp)
		if (isp == (struct iestr *)rq->q_ptr)
			break;
	ASSERT(isp);
	*previsp = isp->is_nextp;

	mutex_destroy(&isp->is_lock);
	kmem_free((char *) isp, sizeof (struct iestr));

	rq->q_ptr = WR(rq)->q_ptr = NULL;

	rw_exit(&iestruplock);
	return (0);
}

static
iewput(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	register	struct	iestr	*isp = (struct iestr *) wq->q_ptr;
	struct	ie	*iep;

	ASSERT(wq);
	ASSERT(mp);

	switch (DB_TYPE(mp)) {
		case M_DATA:		/* "fastpath" */
			iep = isp->is_iep;
			if (!(isp->is_flags & (ISFAST|ISRAW)) ||
				(isp->is_state != DL_IDLE) ||
				(iep == NULL)) {
				merror(wq, mp, EPROTO);
				break;
			}

			/*
			 * If any msgs already enqueued or the interface will
			 * loop back up the message (due to IEPROMISC). then
			 * enqueue the msg.  Otherwise just xmit it directly.
			 */
			if (wq->q_first) {
				(void) putq(wq, mp);
				iep->ie_wantw = 1;
				qenable(wq);
			} else if (iep->ie_flags & IEPROMISC) {
				(void) putq(wq, mp);
				qenable(wq);
			} else {
				(void) iestart(wq, mp, iep);

				/*
				 * XXX After the NFS bug is fixed, we need
				 * to un-comment out the following three
				 * lines.
				 */
#ifdef	notdef
				mutex_enter(&iep->ie_devlock);
				iecuclean(iep);
				mutex_exit(&iep->ie_devlock);
#endif
			}
			break;

		case M_PROTO:
		case M_PCPROTO:
			/*
			 * Break the association between the current thread
			 * and the thread that calls ieproto() to resolve the
			 * problem of ieintr() threads which loop back around
			 * to call ieproto and try to recursively acquire
			 * internal locks.
			 */
			(void) putq(wq, mp);
			qenable(wq);
			break;

		case M_IOCTL:
			ieioctl(wq, mp);
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
	return (0);
}

/*
 * Normally disabled.  Xmit out-of-resources conditions cause M_DATA
 * msgs to be enqueued.  When transmit resources are probably
 * once again available, this routine is explicitly enabled
 * and attempts to xmit all msgs on the wq.
 */
static
iewsrv(wq)
queue_t	*wq;
{
	mblk_t	*mp;
	struct iestr *isp;
	struct	ie *iep;

	isp = (struct iestr *) wq->q_ptr;
	iep = isp->is_iep;

	while (mp = getq(wq))
		switch (DB_TYPE(mp)) {
			case	M_DATA:
				if (iep) {
					if (iestart(wq, mp, iep))
						goto done;
					/*
					 * XXX After the NFS bug is fixed,
					 * we need to un-comment out the
					 * following three lines.
					 */
#ifdef	notif
					mutex_enter(&iep->ie_devlock);
					iecuclean(iep);
					mutex_exit(&iep->ie_devlock);
#endif
				} else
					freemsg(mp);
				break;
			case	M_PROTO:
			case	M_PCPROTO:
				ieproto(wq, mp);
				break;

			default:
				ASSERT(0);
				break;
		}

done:
	return (0);
}

static void
ieioctl(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	iocblk	*iocp = (struct iocblk *) mp->b_rptr;
	struct	iestr	*isp = (struct iestr *) wq->q_ptr;

	switch (iocp->ioc_cmd) {
	case DLIOCRAW:		/* raw M_DATA mode */
		isp->is_flags |= ISRAW;
		miocack(wq, mp, 0, 0);
		break;

	case DL_IOC_HDR_INFO:	/* M_DATA "fastpath" info request */
		ie_dl_ioc_hdr_info(wq, mp);
		break;

	default:
		miocnak(wq, mp, 0, EINVAL);
		break;
	}
}

static void
ieproto(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	register	union	DL_primitives	*dlp;
	struct	iestr	*isp;
	u_long	prim;


	isp = (struct iestr *) wq->q_ptr;
	dlp = (union DL_primitives *) mp->b_rptr;
	prim = dlp->dl_primitive;

	mutex_enter(&isp->is_lock);

	switch (prim) {
		case	DL_UNITDATA_REQ:
			ieunitdatareq(isp, wq, mp);
			break;

		case	DL_ATTACH_REQ:
			ieattachreq(isp, wq, mp);
			break;

		case	DL_DETACH_REQ:
			iedetachreq(isp, wq, mp);
			break;

		case	DL_BIND_REQ:
			iebindreq(isp, wq, mp);
			break;

		case	DL_UNBIND_REQ:
			ieunbindreq(isp, wq, mp);
			break;

		case	DL_INFO_REQ:
			ieinforeq(isp, wq, mp);
			break;

		case	DL_PROMISCON_REQ:
			iepromisconreq(isp, wq, mp);
			break;

		case	DL_PROMISCOFF_REQ:
			iepromiscoffreq(isp, wq, mp);
			break;

		case	DL_ENABMULTI_REQ:
			ieaddmultireq(isp, wq, mp);
			break;

		case	DL_DISABMULTI_REQ:
			iedelmultireq(isp, wq, mp);
			break;

		case	DL_PHYS_ADDR_REQ:
			iephsaddreq(isp, wq, mp);
			break;

		case	DL_SET_PHYS_ADDR_REQ:
			iesetphsaddreq(isp, wq, mp);
			break;

		default:
			dlerrorack(wq, mp, prim, DL_UNSUPPORTED, 0);
			break;
	}

	mutex_exit(&isp->is_lock);
}

/*
 * M_DATA "fastpath" info request.
 * Following the M_IOCTL mblk should come a DL_UNITDATA_REQ mblk.
 * We ack with an M_IOCACK pointing to the original DL_UNITDATA_REQ mblk
 * followed by an mblk containing the raw ethernet header corresponding
 * to the destination address.  Following this,
 * we may receive M_DATA msgs which start with this header and
 * may send up M_DATA msgs with b_rptr pointing to a (ulong) group address
 * indicator followed by the network-layer data (IP packet header).
 * This is all selectable on a per-Stream basis.
 */
static void
ie_dl_ioc_hdr_info(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	mblk_t	*nmp;
	struct	iestr	*isp;
	struct	iedladdr	*dladdrp;
	dl_unitdata_req_t	*dludp;
	struct	ether_header	*headerp;
	struct	ie	*iep;
	int	off, len;
	int	minsize;

	isp = (struct iestr *) wq->q_ptr;
	minsize = sizeof (dl_unitdata_req_t) + IEADDRL;

	/*
	 * Sanity check the request.
	 */
	if ((mp->b_cont == NULL) ||
		(MBLKL(mp->b_cont) < minsize) ||
		(*((u_long *) mp->b_cont->b_rptr) != DL_UNITDATA_REQ) ||
		((iep = isp->is_iep) == NULL)) {
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
	if (!MBLKIN(mp->b_cont, off, len) || (len != IEADDRL)) {
		miocnak(wq, mp, 0, EINVAL);
		return;
	}

	dladdrp = (struct iedladdr *) (mp->b_cont->b_rptr + off);

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
	ether_copy(&dladdrp->dl_phys, &headerp->ether_dhost);
	ether_copy(&iep->ie_ouraddr, &headerp->ether_shost);
	headerp->ether_type = dladdrp->dl_sap;

	/*
	 * Link new mblk in after the "request" mblks.
	 */
	linkb(mp, nmp);

	isp->is_flags |= ISFAST;
	miocack(wq, mp, msgsize(mp->b_cont), 0);
}

static void
ieattachreq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	register	union	DL_primitives	*dlp;
	register	struct	ie	*iep;
	int	ppa;

	dlp = (union DL_primitives *) mp->b_rptr;

	if (MBLKL(mp) < DL_ATTACH_REQ_SIZE) {
		dlerrorack(wq, mp, DL_ATTACH_REQ, DL_BADPRIM, 0);
		return;
	}

	if (isp->is_state != DL_UNATTACHED) {
		dlerrorack(wq, mp, DL_ATTACH_REQ, DL_OUTSTATE, 0);
		return;
	}

	ppa = dlp->attach_req.dl_ppa;

	/*
	 * Valid ppa (unit number)?
	 */
	mutex_enter(&ielock);
	for (iep = iedev; iep; iep = iep->ie_nextp)
		if (ddi_get_instance(iep->ie_dip) == ppa)
			break;
	mutex_exit(&ielock);

	if (iep == NULL) {
		dlerrorack(wq, mp, dlp->dl_primitive, DL_BADPPA, 0);
		return;
	}

	/*
	 * Has device been initialized?  Do so if necessary.
	 */
	if ((iep->ie_flags & IERUNNING) == 0) {
		if (ieinit(iep)) {
			if (iedebug)
				cmn_err(CE_CONT, "attach fails\n");
			dlerrorack(wq, mp, dlp->dl_primitive,
				DL_INITFAILED, 0);
			return;
		}
	}

	/*
	 * Point to attached device and update our state.
	 */
	isp->is_iep = iep;
	isp->is_state = DL_UNBOUND;

	/*
	 * Return DL_OK_ACK response.
	 */
	dlokack(wq, mp, DL_ATTACH_REQ);
}

static void
iedetachreq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	if (MBLKL(mp) < DL_DETACH_REQ_SIZE) {
		dlerrorack(wq, mp, DL_DETACH_REQ, DL_BADPRIM, 0);
		return;
	}

	if (isp->is_state != DL_UNBOUND) {
		dlerrorack(wq, mp, DL_DETACH_REQ, DL_OUTSTATE, 0);
		return;
	}

	iedodetach(isp);
	dlokack(wq, mp, DL_DETACH_REQ);
}

/*
 * Detach a Stream from an interface.
 */
static void
iedodetach(isp)
struct	iestr	*isp;
{
	struct	iestr	*tisp;
	struct	ie	*iep;
	int	reinit = 0;

	ASSERT(isp->is_iep);

	iep = isp->is_iep;
	isp->is_iep = NULL;

	/*
	 * Disable promiscuous mode if on.
	 */
	if (isp->is_flags & ISALLPHYS) {
		isp->is_flags &= ~ISALLPHYS;
		reinit = 1;
	}

	/*
	 * Disable ISALLMULTI if set.
	 */
	if (isp->is_flags & ISALLMULTI) {
		isp->is_flags &= ~ISALLMULTI;
		reinit = 1;
	}

	/*
	 * Disable any Multicast Addresses.
	 */
	isp->is_mccount = 0;
	if (isp->is_mctab) {
		kmem_free(isp->is_mctab, IEMCALLOC);
		isp->is_mctab = NULL;
		reinit = 1;
	}

	/*
	 * Detach from device structure.
	 * Uninit the device when no other streams are attached to it.
	 */
	for (tisp = iestrup; tisp; tisp = tisp->is_nextp)
		if (tisp->is_iep == iep)
			break;
	if (tisp == NULL)
		ieuninit(iep);
	else if (reinit)
		(void) ieinit(iep);

	isp->is_state = DL_UNATTACHED;
}

static void
iebindreq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	register	union	DL_primitives	*dlp;
	register	struct	ie	*iep;
	struct	iedladdr	ieaddr;
	u_long	sap;
	int	xidtest;

	if (MBLKL(mp) < DL_BIND_REQ_SIZE) {
		dlerrorack(wq, mp, DL_BIND_REQ, DL_BADPRIM, 0);
		return;
	}

	if (isp->is_state != DL_UNBOUND) {
		dlerrorack(wq, mp, DL_BIND_REQ, DL_OUTSTATE, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	iep = isp->is_iep;
	sap = dlp->bind_req.dl_sap;
	xidtest = dlp->bind_req.dl_xidtest_flg;

	ASSERT(iep);

	if (xidtest) {
		dlerrorack(wq, mp, DL_BIND_REQ, DL_NOAUTO, 0);
		return;
	}

	if (sap > ETHERTYPE_MAX) {
		if (iedebug)
			ierror(isp->is_iep->ie_dip, "bind fails");
		dlerrorack(wq, mp, dlp->dl_primitive, DL_BADSAP, 0);
		return;
	}

	/*
	 * Save SAP value for this Stream and change change.
	 */
	isp->is_sap = sap;
	isp->is_state = DL_IDLE;

	ieaddr.dl_sap = sap;
	ether_copy(&iep->ie_ouraddr, &ieaddr.dl_phys);
	dlbindack(wq, mp, sap, &ieaddr, IEADDRL, 0, 0);
}

static void
ieunbindreq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	if (MBLKL(mp) < DL_UNBIND_REQ_SIZE) {
		dlerrorack(wq, mp, DL_UNBIND_REQ, DL_BADPRIM, 0);
		return;
	}

	if (isp->is_state != DL_IDLE) {
		dlerrorack(wq, mp, DL_UNBIND_REQ, DL_OUTSTATE, 0);
		return;
	}

	isp->is_state = DL_UNBOUND;

	putnextctl1(RD(wq), M_FLUSH, FLUSHRW);
	dlokack(wq, mp, DL_UNBIND_REQ);
}

static void
ieinforeq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	register	dl_info_ack_t	*dlip;
	register	struct	iedladdr	*dlap;
	int	size;
	struct	ether_addr	*ep;

	if (MBLKL(mp) < DL_INFO_REQ_SIZE) {
		dlerrorack(wq, mp, DL_INFO_REQ, DL_BADPRIM, 0);
		return;
	}

	/*
	 * Exchange current msg for a DL_INFO_ACK.
	 */
	size = sizeof (dl_info_ack_t) + IEADDRL + ETHERADDRL;
	if ((mp = mexchange(wq, mp, size, M_PCPROTO, DL_INFO_ACK)) == NULL)
		return;

	/*
	 * Fill in the DL_INFO_ACK fields and reply.
	 */
	dlip = (dl_info_ack_t *) mp->b_rptr;
	*dlip = ie_infoack;
	dlip->dl_current_state = isp->is_state;
	dlap = (struct iedladdr *) (mp->b_rptr + dlip->dl_addr_offset);
	dlap->dl_sap = isp->is_sap;
	if (isp->is_iep) {
		ether_copy(&isp->is_iep->ie_ouraddr, &dlap->dl_phys);
	} else
		bzero((caddr_t) &dlap->dl_phys, ETHERADDRL);
	ep = (struct ether_addr *) (mp->b_rptr + dlip->dl_brdcst_addr_offset);
	ether_copy(&etherbroadcastaddr, ep);

	qreply(wq, mp);
}

static void
iepromisconreq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	if (MBLKL(mp) < DL_PROMISCON_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PROMISCON_REQ, DL_BADPRIM, 0);
		return;
	}

	switch (((dl_promiscon_req_t *) mp->b_rptr)->dl_level) {
		case DL_PROMISC_PHYS:
			isp->is_flags |= ISALLPHYS;
			break;

		case DL_PROMISC_SAP:
			isp->is_flags |= ISALLSAP;
			break;

		case DL_PROMISC_MULTI:
			isp->is_flags |= ISALLMULTI;
			break;

		default:
			dlerrorack(wq, mp, DL_PROMISCON_REQ,
				DL_NOTSUPPORTED, 0);
			return;
	}

	if (isp-> is_iep)
		(void) ieinit(isp->is_iep);

	dlokack(wq, mp, DL_PROMISCON_REQ);
}

static void
iepromiscoffreq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	int	flag;

	if (MBLKL(mp) < DL_PROMISCOFF_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PROMISCOFF_REQ, DL_BADPRIM, 0);
		return;
	}

	switch (((dl_promiscoff_req_t *) mp->b_rptr)->dl_level) {
		case DL_PROMISC_PHYS:
			flag = ISALLPHYS;
			break;

		case DL_PROMISC_SAP:
			flag = ISALLSAP;
			break;

		case DL_PROMISC_MULTI:
			flag = ISALLMULTI;
			break;

		default:
			dlerrorack(wq, mp, DL_PROMISCOFF_REQ,
				DL_NOTSUPPORTED, 0);
			return;
	}

	if ((isp->is_flags & flag) == 0) {
		dlerrorack(wq, mp, DL_PROMISCOFF_REQ, DL_NOTENAB, 0);
		return;
	}

	isp->is_flags &= ~flag;
	if (isp->is_iep)
		(void) ieinit(isp->is_iep);

	dlokack(wq, mp, DL_PROMISCOFF_REQ);
}

static void
ieaddmultireq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	union	DL_primitives	*dlp;
	struct	ether_addr	*addrp;
	int	off;
	int	len;

	if (MBLKL(mp) < DL_ENABMULTI_REQ_SIZE) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_BADPRIM, 0);
		return;
	}

	if (isp->is_state == DL_UNATTACHED) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_OUTSTATE, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	off = dlp->enabmulti_req.dl_addr_offset;
	len = dlp->enabmulti_req.dl_addr_length;
	addrp = (struct ether_addr *) (mp->b_rptr + off);

	if (!MBLKIN(mp, off, len) ||
		(len != ETHERADDRL) ||
		!(addrp->ether_addr_octet[0] & 01)) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_BADADDR, 0);
		return;
	}

	if ((isp->is_mccount + 1) >= IEMAXMC) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_TOOMANY, 0);
		return;
	}

	/*
	 * Allocate table on first request.
	 */
	if (isp->is_mctab == NULL)
		isp->is_mctab = kmem_alloc(IEMCALLOC, KM_SLEEP);

	isp->is_mctab[isp->is_mccount++] = *addrp;

	(void) ieinit(isp->is_iep);
	dlokack(wq, mp, DL_ENABMULTI_REQ);
}

static void
iedelmultireq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	union	DL_primitives	*dlp;
	struct	ether_addr	*addrp;
	int	off;
	int	len;
	int	i;

	if (MBLKL(mp) < DL_DISABMULTI_REQ_SIZE) {
		dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_BADPRIM, 0);
		return;
	}

	if (isp->is_state == DL_UNATTACHED) {
		dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_OUTSTATE, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	off = dlp->disabmulti_req.dl_addr_offset;
	len = dlp->disabmulti_req.dl_addr_length;
	addrp = (struct ether_addr *) (mp->b_rptr + off);

	if (!MBLKIN(mp, off, len) || (len != ETHERADDRL)) {
		dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_BADADDR, 0);
		return;
	}

	/*
	 * Find the address in the multicast table for this Stream
	 * and delete it by shifting all subsequent multicast address
	 * table entries back over it then reinitialize chip.
	 */
	for (i = 0; i < isp->is_mccount; i++)
		if (ether_cmp(addrp, &isp->is_mctab[i]) == 0) {
			bcopy((caddr_t) &isp->is_mctab[i+1],
				(caddr_t) &isp->is_mctab[i],
				((isp->is_mccount - i) *
				sizeof (struct ether_addr)));
			isp->is_mccount--;
			(void) ieinit(isp->is_iep);
			dlokack(wq, mp, DL_DISABMULTI_REQ);
			return;
		}
	dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_NOTENAB, 0);
}

static void
iephsaddreq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	union	DL_primitives	*dlp;
	int	type;
	struct	ie	*iep;
	struct	ether_addr	addr;

	if (MBLKL(mp) < DL_PHYS_ADDR_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PHYS_ADDR_REQ, DL_BADPRIM, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	type = dlp->physaddr_req.dl_addr_type;
	iep = isp->is_iep;

	if (iep == NULL) {
		dlerrorack(wq, mp, DL_PHYS_ADDR_REQ, DL_OUTSTATE, 0);
		return;
	}

	switch (type) {
		case	DL_FACT_PHYS_ADDR:
			(void) localetheraddr((struct ether_addr *) NULL,
				&addr);
			break;

		case	DL_CURR_PHYS_ADDR:
			ether_copy(&iep->ie_ouraddr, &addr);
			break;

		default:
			dlerrorack(wq, mp, DL_PHYS_ADDR_REQ,
				DL_NOTSUPPORTED, 0);
			return;
	}

	dlphysaddrack(wq, mp, &addr, ETHERADDRL);
}

static void
iesetphsaddreq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	union	DL_primitives	*dlp;
	int	off;
	int	len;
	struct	ether_addr	*addrp;
	struct	ie	*iep;

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
	if ((iep = isp->is_iep) == NULL) {
		dlerrorack(wq, mp, DL_SET_PHYS_ADDR_REQ, DL_OUTSTATE, 0);
		return;
	}

	/*
	 * Set new interface local address and re-init device.
	 * This is destructive to any other streams attached
	 * to this device.
	 */
	ether_copy(addrp, &iep->ie_ouraddr);
	(void) ieinit(isp->is_iep);

	dlokack(wq, mp, DL_SET_PHYS_ADDR_REQ);
}

static void
ieunitdatareq(isp, wq, mp)
struct	iestr	*isp;
queue_t	*wq;
mblk_t	*mp;
{
	register	struct	ie	*iep;
	register	dl_unitdata_req_t	*dludp;
	mblk_t	*nmp;
	struct	iedladdr	*dlap;
	struct	ether_header	*headerp;
	int	off, len;

	if (isp->is_state != DL_IDLE) {
		dlerrorack(wq, mp, DL_UNITDATA_REQ, DL_OUTSTATE, 0);
		return;
	}

	iep = isp->is_iep;

	dludp = (dl_unitdata_req_t *) mp->b_rptr;

	off = dludp->dl_dest_addr_offset;
	len = dludp->dl_dest_addr_length;

	/*
	 * Validate destination address format.
	 */
	if (!MBLKIN(mp, off, len) || (len != IEADDRL)) {
		dluderrorind(wq, mp, mp->b_rptr + off, len, DL_BADADDR, 0);
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

	dlap = (struct iedladdr *) (mp->b_rptr + off);

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
		ether_copy(&iep->ie_ouraddr, &headerp->ether_shost);
		headerp->ether_type = dlap->dl_sap;
		freeb(mp);
		mp = nmp;
	} else {
		DB_TYPE(mp) = M_DATA;
		headerp = (struct ether_header *) mp->b_rptr;
		mp->b_wptr = mp->b_rptr + sizeof (struct ether_header);
		ether_copy(&dlap->dl_phys, &headerp->ether_dhost);
		ether_copy(&iep->ie_ouraddr, &headerp->ether_shost);
		headerp->ether_type = dlap->dl_sap;
	}

	/*
	 * Transmit it.
	 */
	(void) iestart(wq, mp, iep);

	/*
	 * XXX After the NFS bug is fixed,
	 * we need to un-comment out the
	 * following three lines.
	 */
#ifdef	notdef
	mutex_enter(&iep->ie_devlock);
	iecuclean(iep);
	mutex_exit(&iep->ie_devlock);
#endif
}

/*
 * Un-initialize (abort) the Intel chip
 */
static void
ieuninit(ie_t *iep)
{
	volatile struct iescb *scbp;

	if (iep->ie_cbsyncp == NULL) {
		iechipreset(iep);
		return;
	}

	/*
	 * Allow up to 'iedraintime' for pending xmit's to complete.
	 */
	CDELAY((iep->ie_scbp->iescb_cmd & IESCB_CUS) == IECUS_IDLE,
		iedraintime);

	mutex_enter(&iep->ie_devlock);

	/*
	 * Abort current xmit and recv operations.
	 */
	scbp = iep->ie_scbp;
	scbp->iescb_cmd = IECMD_RU_ABORT | IECMD_CU_ABORT;
	IECA(iep);
	if (scbp->iescb_cmd)
		(void) iechkcca(iep);
	iechipreset(iep);

	iep->ie_flags &= ~IERUNNING;

	mutex_exit(&iep->ie_devlock);
}

/*
 * Service Ethernet interrupts.
 *
 * When the transmit is done, we do not need to free the transmit
 * buffer immediately.  This will shorten the interrupt processing.
 * We can reclaim the buffer next time when we need to transmit a
 * frame by calling iecuclean() in iecustart().
 *
 * XXX There is a hardware bug in the Sun-3/E VME board which will
 * cause the interrupt handler to be called when the ie is not
 * interrupting the system.  See bug 1009297.
 */
static u_int
ieintr(iep)
register	ie_t *iep;
{
	int	found = 0;
	volatile struct iescb *scbp = iep->ie_scbp;
	int cmd;

	mutex_enter(&iep->ie_devlock);
	(void) iechkcca(iep);
	if (ISMASTER) {
		if (*IE_OBCSR & OBIE_BUSERR) {
			ierror(iep->ie_dip, "ieintr: bus error");
			iechipreset(iep);
			mutex_exit(&iep->ie_devlock);
			return (1);
		}
		if (!(*IE_OBCSR & OBIE_IE)) {
			mutex_exit(&iep->ie_devlock);
			return (0);
		}
		if (*IE_OBCSR & OBIE_INTR)
			found = 1;
	} else {
		if (!(*IE_TECSR & TIE_IE)) {
			mutex_exit(&iep->ie_devlock);
			return (0);
		}
		if (*IE_TECSR & TIE_INTR)
			found = 1;
	}

	/*
	 * Since the 82586 can take away an interrupt
	 * request after presenting it to the processsor
	 * (to facilitate an update of a new interrupt
	 * condition in the scb), we also have to check
	 * the scb to see if it indicates an interrupt
	 * condition from the chip.
	 */
	if (found == 0) {
		volatile struct iescb *scbp = iep->ie_scbp;

#ifdef	notdef
		/*
		 * Synchronize CPU's view of SCB.
		 */
		if (ISMASTER)
			IE_CPU_SYNC(iep, iep->ie_scbp, sizeof (*iep->ie_scbp));
#endif
		if (scbp->iescb_status & IESCB_CX ||
			scbp->iescb_status & IESCB_FR ||
			scbp->iescb_status & IESCB_CNA ||
			scbp->iescb_status & IESCB_RNR)
			found = 1;
	}
	if (found == 0) {
		mutex_exit(&iep->ie_devlock);
		return (0);
	}

#ifdef	notdef
	/*
	 * Synchronize CPU's view of SCB.
	 */
	if (ISMASTER)
		IE_CPU_SYNC(iep, scbp, sizeof (*scbp));
#endif
	if (scbp->iescb_cmd && iechkcca(iep) == 0) {
		mutex_exit(&iep->ie_devlock);
		return (1);
	}

handle:
	/* Clear the interrupt */
	scbp->iescb_cmd = cmd = scbp->iescb_status &
		(IECMD_ACK_CX|IECMD_ACK_FR|IECMD_ACK_CNA|IECMD_ACK_RNR);

#ifdef	notdef
	if (ISMASTER)
		IE_DEV_SYNC(iep, scbp, sizeof (*scbp));
#endif
	IECA(iep);

	if (cmd & (IECMD_ACK_RNR | IECMD_ACK_FR))
		ierecv(iep);

	if (cmd & (IECMD_ACK_CNA | IECMD_ACK_CX)) {

		/*
		 * Ideally, we do not need to start the chip here.
		 * However, there is a race condition in the chip
		 * which can cause the last frame of the NFS response
		 * to remain in the xmit queue and eventually cause
		 * the NFS to time out.  Therefore, we need to restart
		 * the chip here to hide the race condition.
		 */
		iecustart(iep);

		/*
		 * XXX We need to free the transmit buffer here.  Otherwise,
		 * NFS will hang because it is waiting for the xmit to
		 * complete.  Once the bug in NFS is fixed, we can remove
		 * these lines.
		 */
		iecuclean(iep);
	}

	/*
	 * It takes time between the channel attention is asserted and
	 * the interrupt is cleared.  If we simply return here, the
	 * interrupt in the csr may still remain on.  Then, the kernel
	 * will call us again and immediately the interrupt may go off.
	 * ieintr() will return 0 and we get warning message
	 * complaining the interrupt is not serviced.
	 */
	if ((ISMASTER && *IE_OBCSR & OBIE_INTR) ||
		(ISSLAVE && *IE_TECSR & TIE_INTR)) {

		/*
		 * Wait until the chip clears the interrupt bit.
		 */
		(void) iechkcca(iep);

		/*
		 * If the interrupt bit is still on, there must be
		 * a new interrupt.
		 */
		if ((ISMASTER && *IE_OBCSR & OBIE_INTR) ||
			(ISSLAVE && *IE_TECSR & TIE_INTR))
			goto handle;
	}

	mutex_exit(&iep->ie_devlock);
	return (1);
}

/*
 * Start xmit on any msgs previously enqueued on any write queues.
 */
static void
iewenable(iep)
ie_t *iep;
{
	struct iestr *isp;
	queue_t *wq;

	/*
	 * Order of wantw accesses is important.
	 */
	do {
		iep->ie_wantw = 0;
		for (isp = iestrup; isp; isp = isp->is_nextp)
			if (isp->is_iep == iep &&
				(wq = WR(isp->is_rq))->q_first)
				qenable(wq);
	} while (iep->ie_wantw);
}

/*
 * Process completed input packets and recycle resources.
 */
static void
ierecv(iep)
ie_t *iep;
{
	volatile ierfd_t *rfdp = iep->ie_rfdhd;
	ierbd_t *rbdp;
	volatile struct iescb *scbp = iep->ie_scbp;
	int eof;
	ieint_t e;

	if (rfdp == NULL) {
		if (iedebug)
			ierror(iep->ie_dip, "ierecv: no rfds\n");
		return;
	}

top:
	/*
	 * Process each received frame.
	 */
	while (rfdp->ierfd_status & IERFD_DONE) {

		ieread(iep, rfdp);

		/*
		 * Reclaim resources associated with the frame.
		 */
		if (rfdp->ierfd_rbd != IENULLOFF) {
			rbdp = iep->ie_rbdhd;
			while (rbdp && rbdp->ierbd_status & IERBD_VALID) {

				/*
				 * Advance the rbd list head and tail one
				 * notch around the ring.
				 */
				rbdp->ierbd_elsize |= IERBD_EL;
				eof = rbdp->ierbd_status & IERBD_EOF;
				rbdp->ierbd_cntlo = rbdp->ierbd_status = 0;

				/*
				 * Put rbd/buffer back onto receive list.
				 */
				iep->ie_rbdtl->ierbd_elsize &= ~IERBD_EL;
				iep->ie_rbdtl = rbdp;
				if (++rbdp >= iep->ie_rbdlim)
					rbdp = iep->ie_rbdring;
				iep->ie_rbdhd = rbdp;
				if (eof)
					break;
			}
		}

		/*
		 * Advance the rfd list head and tail
		 * one notch around the ring.
		 */
		rfdp->ierfd_rbd = IENULLOFF;
		rfdp->ierfd_command |= IERFD_EL;
		rfdp->ierfd_status = 0;

		/*
		 * Put frame descriptor back onto receive list.
		 */
		iep->ie_rfdtl->ierfd_command &= ~IERFD_EL;
		iep->ie_rfdtl = rfdp;
		if (++rfdp >= iep->ie_rfdlim)
			rfdp = iep->ie_rfdring;
		iep->ie_rfdhd = rfdp;
	}

	/*
	 * Update cumulative statistics.  (These relate to everything
	 * that's come in since we last updated, not to a single packet.)
	 */
	if ((e = scbp->iescb_crcerrs) != 0) {	/* count of CRC errors */
		scbp->iescb_crcerrs = 0;
		e = fromieint(e);
		iep->ie_crc += e;
		if (iedebug)
			ierror(iep->ie_dip, "recv CRC error");
	}
	if ((e = scbp->iescb_alnerrs) != 0) {	/* count of alignment errors */
		scbp->iescb_alnerrs = 0;
		e = fromieint(e);
		iep->ie_align += e;
		if (iedebug)
			ierror(iep->ie_dip, "recv alignment error");
	}
	if ((e = scbp->iescb_rscerrs) != 0) {	/* count of discarded packets */
		scbp->iescb_rscerrs = 0;
		e = fromieint(e);
		iep->ie_discard += e;
		iep->ie_ierrors += e;
		if (iedebug)
			ierror(iep->ie_dip, "resource error");
	}
	if ((e = scbp->iescb_ovrnerrs) != 0) {	/* count of overrun packets */
		scbp->iescb_ovrnerrs = 0;
		e = fromieint(e);
		iep->ie_recvorun += e;
		iep->ie_ierrors += e;
		if (iedebug)
			ierror(iep->ie_dip, "recv overrun");
	}

#ifdef	notdef
	/*
	 * If the receive unit is still chugging along, we're done.
	 * Otherwise, it must have caught up with us and entered
	 * the No Resources state, and we must restart it.
	 */
	if ((scbp->iescb_status & IESCB_RUS) == IERUS_READY)
		return;
	iep->ie_runotready++;

	if (iep->ie_rfdhd->ierfd_status & IERFD_DONE) {
		if (iedebug)
			ierror(iep->ie_dip, "another frame received");
		goto top;
	}

	/*
	 * Reset our data structures and restart.
	 */
	iep->ie_rfdhd->ierfd_rbd = toieoff(iep, (caddr_t)iep->ie_rbdhd);
	if (scbp->iescb_cmd && iechkcca(iep) == 0)
		return;
	scbp->iescb_rfa = toieoff(iep, (caddr_t)iep->ie_rfdhd);
	scbp->iescb_cmd = IECMD_RU_START;
	IECA(iep);
#endif
}

/*
 * Move info from driver toward protocol interface.
 */
static void
ieread(iep, rfdp)
ie_t *iep;
volatile ierfd_t *rfdp;
{
	int length, i;
	register ierbd_t *rbdp;
	caddr_t buffer;
	iebuf_t *iebufp, *nbuf;
	mblk_t *mp;

	iep->ie_ipackets++;

	if (!(rfdp->ierfd_status & IERFD_OK)) {
		iep->ie_ierrors++;
		ierror(iep->ie_dip, "receive error\n");
		return;
	}

	if (rfdp->ierfd_rbd == IENULLOFF) {
		length = 0;
		goto runt;
	} else {
		rbdp = (ierbd_t *)fromieoff(iep, (ieoff_t)rfdp->ierfd_rbd);
		if (!(rbdp->ierbd_status & IERBD_EOF)) {
			if (iedebug)
				ierror(iep->ie_dip, "giant packet");
			iep->ie_ierrors++;
			return;
		}
		length = ((rbdp->ierbd_status & IERBD_CNTHI) << 8) +
			rbdp->ierbd_cntlo;
	}

	i = rbdp - iep->ie_rbdring;
	iebufp = iep->ie_rbp[i];
	buffer = (caddr_t) iebufp + IERBUFOFF;

	/*
	 * Sync the receive buffer.  Not required for sun4.
	 */
#ifdef notdef
	if (ISMASTER) {
		off_t off = (off_t) ((u_long) iebufp -
				(u_long) iep->ie_bufbase);

		ddi_dma_sync(iep->ie_bufhandle, off, sizeof (iebuf_t),
			DDI_DMA_SYNC_FORCPU);
	}
#endif

	/*
	 * If the I/O cache is enabled, flush the incoming ethernet
	 * line. This insures that the buffer we are about to look
	 * at is not still in the I/O cache.
	 */
	if (iep->ie_iocache)
		iep->ie_iocache[IOC_FLUSH_IN] = 0;

	/*
	 * XXX call ether_check_trailer() here.
	 */

runt:
	if (length <= sizeof (struct ether_header)) {
		if (iedebug)
			ierror(iep->ie_dip, "runt packet\n");
		iep->ie_ierrors++;
		return;
	}

	/*
	 * Receive buffer loan-out:
	 *	We're willing to loan the buffer containing this
	 *	packet to the higher protocol layers provided that
	 *	we have a spare receive buffer to use as a replacement
	 *	for it.
	 * NOTE:
	 *	On sparc we don't loan out IE_TE buffers because if
	 *	anyone references the buffer as an int it could cause
	 *	a VME size error trap. This may be faster anyway since
	 *	the packet is brought into the cache.
	 */
	if (ISMASTER && (nbuf = iegetbuf(iep, 0)) != NULL) {

			/*
			 * Allocate and point at the receive buffer.
			 */
			if ((mp = esballoc((unsigned char *)buffer, length,
				BPRI_LO, &iebufp->ib_frtn)) == NULL) {
				iep->ie_allocbfail++;
				iep->ie_ierrors++;
				if (iedebug)
					ierror(iep->ie_dip, "esballoc failed");
				iefreebuf(nbuf);
				return;
			}
			mp->b_wptr += length;

			ierbufinit(iep, rbdp, nbuf);
			iep->ie_rbp[i] = nbuf;
			iep->ie_loaned++;
	} else {
		if ((mp = allocb(length + (3 * IEBURSTSIZE), BPRI_LO))
			== NULL) {
			iep->ie_ierrors++;
			iep->ie_allocbfail++;
			if (iedebug)
				ierror(iep->ie_dip, "allocb fail");
			return;
		}
		mp->b_rptr = (u_char *) IEROUNDUP((u_int)mp->b_rptr,
			IEBURSTSIZE) + ((u_int)buffer & IEBURSTMASK);
		mp->b_wptr = mp->b_rptr + length;
		bcopy(buffer, (caddr_t) mp->b_rptr, length);
	}

	iesendup(iep, mp, ieaccept);
}

/*
 * Send packet upstream.
 * Assume mp->b_rptr points to ether_header.
 */
static void
iesendup(iep, mp, acceptfunc)
ie_t *iep;
mblk_t *mp;
struct	iestr	*(*acceptfunc)();
{
	int	type;
	struct	ether_addr	*dhostp, *shostp;
	struct	iestr	*isp, *nisp;
	mblk_t	*nmp;
	ulong	isgroupaddr;

	dhostp = &((struct ether_header *) mp->b_rptr)->ether_dhost;
	shostp = &((struct ether_header *) mp->b_rptr)->ether_shost;
	type = ((struct ether_header *) mp->b_rptr)->ether_type;

	isgroupaddr = dhostp->ether_addr_octet[0] & 01;

	/*
	 * While holding a reader lock on the linked list of streams
	 * structures, attempt to match the address criteria for each stream
	 * and pass up the raw M_DATA ("fastpath") or a DL_UNITDATA_IND.
	 */
	rw_enter(&iestruplock, RW_READER);

	if ((isp = (*acceptfunc)(iestrup, iep, type, dhostp)) == NULL) {
		rw_exit(&iestruplock);
		freemsg(mp);
		return;
	}

	/*
	 * Loop on matching open streams until (*acceptfunc)() returns NULL.
	 */
	for (; nisp = (*acceptfunc)(isp->is_nextp, iep, type, dhostp);
		isp = nisp)
		if (canput(isp->is_rq->q_next))
			if (nmp = dupmsg(mp)) {
				if (isp->is_flags & ISFAST && !isgroupaddr) {
					nmp->b_rptr +=
						sizeof (struct ether_header);
					mutex_exit(&iep->ie_devlock);
					putnext(isp->is_rq, nmp);
					mutex_enter(&iep->ie_devlock);
				} else if (isp->is_flags & ISRAW) {
					mutex_exit(&iep->ie_devlock);
					putnext(isp->is_rq, nmp);
					mutex_enter(&iep->ie_devlock);
				} else if ((nmp = ieaddudind(iep, nmp, shostp,
					dhostp, type, isgroupaddr))) {
					mutex_exit(&iep->ie_devlock);
					putnext(isp->is_rq, nmp);
					mutex_enter(&iep->ie_devlock);
				}
			} else {
				if (iedebug)
					ierror(iep->ie_dip,
						"iesendup: dupmsg failed");
				iep->ie_allocbfail++;
			}
		else
			iep->ie_nocanput++;

	/*
	 * Do the last one.
	 */
	if (canput(isp->is_rq->q_next)) {
		if ((isp->is_flags & ISFAST) && !isgroupaddr) {
			mp->b_rptr += sizeof (struct ether_header);
			mutex_exit(&iep->ie_devlock);
			putnext(isp->is_rq, mp);
			mutex_enter(&iep->ie_devlock);
		} else if (isp->is_flags & ISRAW) {
			mutex_exit(&iep->ie_devlock);
			putnext(isp->is_rq, mp);
			mutex_enter(&iep->ie_devlock);
		} else if ((mp = ieaddudind(iep, mp, shostp, dhostp, type,
			isgroupaddr))) {
			mutex_exit(&iep->ie_devlock);
			putnext(isp->is_rq, mp);
			mutex_enter(&iep->ie_devlock);
		}
	} else {
		freemsg(mp);
		iep->ie_nocanput++;
	}

	rw_exit(&iestruplock);
}

/*
 * Test upstream destination sap and address match.
 */
static struct iestr *
ieaccept(isp, iep, type, addrp)
register struct iestr *isp;
register ie_t *iep;
int type;
struct ether_addr *addrp;
{
	int	sap;
	int	flags;

	for (; isp; isp = isp->is_nextp) {
		sap = isp->is_sap;
		flags = isp->is_flags;

		if ((isp->is_iep == iep) && IESAPMATCH(sap, type, flags))
			if ((ether_cmp(addrp, &iep->ie_ouraddr) == 0) ||
				(ether_cmp(addrp, &etherbroadcastaddr) == 0) ||
				(flags & ISALLPHYS) ||
				iemcmatch(isp, addrp))
				return (isp);
	}

	return (NULL);
}

/*
 * Test upstream destination sap and address match for SLALLPHYS only.
 */
/* ARGSUSED3 */
static struct iestr *
iepaccept(isp, iep, type, addrp)
register	struct	iestr	*isp;
register	struct	ie	*iep;
int	type;
struct	ether_addr	*addrp;
{
	int	sap;
	int	flags;

	for (; isp; isp = isp->is_nextp) {
		sap = isp->is_sap;
		flags = isp->is_flags;

		if ((isp->is_iep == iep) &&
			IESAPMATCH(sap, type, flags) &&
			(flags & ISALLPHYS))
			return (isp);
	}

	return (NULL);
}

/*
 * Return TRUE if the given multicast address is one
 * of those that this particular Stream is interested in.
 */
static
iemcmatch(isp, addrp)
register struct iestr *isp;
register struct ether_addr *addrp;
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
	if (isp->is_flags & ISALLMULTI)
		return (1);

	/*
	 * Return FALSE if no multicast addresses enabled for this Stream.
	 */
	if (isp->is_mccount == 0)
		return (0);

	/*
	 * Otherwise, find it in the table.
	 */
	mccount = isp->is_mccount;
	mctab = isp->is_mctab;

	for (i = 0; i < mccount; i++)
		if (!ether_cmp(addrp, &mctab[i]))
			return (1);

	return (0);
}

/*
 * Prefix msg with a DL_UNITDATA_IND mblk and return the new msg.
 */
static mblk_t *
ieaddudind(iep, mp, shostp, dhostp, type, isgroupaddr)
struct	ie *iep;
mblk_t	*mp;
struct	ether_addr	*shostp, *dhostp;
int	type;
ulong	isgroupaddr;
{
	dl_unitdata_ind_t	*dludindp;
	struct  iedladdr  *dladdrp;
	mblk_t  *nmp;
	int	size;

	mp->b_rptr += sizeof (struct ether_header);

	/*
	 * Allocate an M_PROTO mblk for the DL_UNITDATA_IND.
	 */
	size = sizeof (dl_unitdata_ind_t) + IEADDRL + IEADDRL;
	if ((nmp = allocb(size, BPRI_LO)) == NULL) {
		iep->ie_allocbfail++;
		iep->ie_ierrors++;
		if (iedebug)
			ierror(iep->ie_dip, "ieaddudind: allocb failed");
		freemsg(mp);
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
	dludindp->dl_dest_addr_length = IEADDRL;
	dludindp->dl_dest_addr_offset = sizeof (dl_unitdata_ind_t);
	dludindp->dl_src_addr_length = IEADDRL;
	dludindp->dl_src_addr_offset = sizeof (dl_unitdata_ind_t) + IEADDRL;
	dludindp->dl_group_address = isgroupaddr;

	dladdrp = (struct iedladdr *)(nmp->b_rptr + sizeof (dl_unitdata_ind_t));
	ether_copy(dhostp, &dladdrp->dl_phys);
	dladdrp->dl_sap = (u_short)type;

	dladdrp = (struct iedladdr *) (nmp->b_rptr
		+ sizeof (dl_unitdata_ind_t) + IEADDRL);
	ether_copy(shostp, &dladdrp->dl_phys);
	dladdrp->dl_sap = (u_short) type;

	/*
	 * Link the M_PROTO and M_DATA together.
	 */
	nmp->b_cont = mp;
	return (nmp);
}

static int iechkcca_recurse = 0;
/*
 * Delay until the 82586 has accepted the current
 * command word.  Watch for buggy hardware while at it.
 * Return 1 on success, 0 on failure.
 */
static int
iechkcca(iep)
ie_t *iep;
{
	volatile struct iescb *scbp = iep->ie_scbp;
	int i;

	if (scbp == NULL)
		return (0);

	for (i = 0; i < iedelaytimes; i++) {

#ifdef	notdef
		if (ISMASTER)
			IE_CPU_SYNC(iep, scbp, sizeof (*scbp));
#endif
		if (scbp->iescb_cmd == 0)
			break;
		drv_usecwait(IEWAITTIME);
	}

	if (i == iedelaytimes) {
		if (iedebug)
			ierror(iep->ie_dip, "cmd not accepted");
		if (iechkcca_recurse)
			panic("iechkcca");
		mutex_exit(&iep->ie_devlock);
		iechkcca_recurse++;
		(void) ieinit(iep);
		iechkcca_recurse--;
		mutex_enter(&iep->ie_devlock);
		return (0);
	} else
		return (1);
}

/*
 * Watchdog (deadman) timer routine, invoked every 1 minute.
 *
 * XXX The driver issued a no-op command in SunOS 4.X code for some buggy
 * hardware.  Since the driver has less hardware to support in this release,
 * no no-op command is issued.  It may be added in the future if needed.
 */
static void
iedog(iep)
ie_t *iep;
{
	volatile struct iescb	*scbp = iep->ie_scbp;
	int status;

	if ((iep->ie_flags & IERUNNING) == 0)
		return;

	/* poll for bus error on obie */
	if ((iep->ie_flags & IESLAVE) == 0)
		if (*IE_OBCSR & OBIE_BUSERR) {
			if (iedebug)
				ierror(iep->ie_dip, "obie buserr reset\n");
			goto reset;
		}

	status = scbp->iescb_status & IESCB_CUS;
	if (status != IECUS_IDLE && status != IECUS_ACTIVE) {
		if (iedebug)
			ierror(iep->ie_dip, "iedog: CU status %x\n",
				(char *)status);
		goto reset;
	}

	status = scbp->iescb_status & IESCB_RUS;
	if (status != IERUS_IDLE && status != IERUS_READY) {
		if (iedebug)
			ierror(iep->ie_dip, "iedog: RU status %x\n",
				(char *)status);
		goto reset;
	}

	iep->ie_dogid = timeout(iedog, (caddr_t)iep, 60 * hz);
	return;

reset:
	iep->ie_dogreset++;
	(void) ieinit(iep);
}

static ieoff_t
toieoff(iep, addr)
ie_t *iep;
caddr_t addr;
{
	union {
		ieoff_t s;
		char	c[2];
	} a, b;

	a.s = (ieoff_t)(addr - iep->ie_cb_base);
	b.c[0] = a.c[1];
	b.c[1] = a.c[0];
	return (b.s);
}

static caddr_t
fromieoff(register ie_t *iep, ieoff_t off)
{
	union {
		ieoff_t s;
		char	c[2];
	} a, b;

	a.s = off;
	b.c[0] = a.c[1];
	b.c[1] = a.c[0];
	return (iep->ie_cb_base + b.s);
}

static ieint_t
toieint(ieint_t n)
{
	union {
		ieint_t	s;
		char	c[2];
	} a, b;

	a.s = n;
	b.c[0] = a.c[1];
	b.c[1] = a.c[0];
	return (b.s);
}

#define	IEBUFSRESERVED	8
static iebuf_t *
iegetbuf(iep, pri)
register ie_t *iep;
int pri;
{
	register int i;
	iebuf_t *ibp;

	mutex_enter(&iep->ie_buflock);
	i = iep->ie_bufi;
	if ((i == 0) || ((pri == 0) && (i < IEBUFSRESERVED))) {
		mutex_exit(&iep->ie_buflock);
		return (NULL);
	}

	ibp = iep->ie_buftab[--i];
	iep->ie_bufi = i;
	mutex_exit(&iep->ie_buflock);

	return (ibp);
}

static void
iefreebuf(ibp)
register iebuf_t *ibp;
{
	register ie_t *iep = ibp->ib_iep;

	mutex_enter(&iep->ie_buflock);
	if (iep->ie_bufi > IE_NBUF)
		panic("iefreebuf: fake buffer\n");

	iep->ie_buftab[iep->ie_bufi++] = ibp;

	if (iep->ie_wantw)
		iewenable(iep);
	mutex_exit(&iep->ie_buflock);
}

/*VARARGS*/
static void
ierror(dev_info_t *dip, char *fmt, ...)
{
	static	long	last;
	static	char	*lastfmt;
	char		msg_buffer[255];
	va_list	ap;

	mutex_enter(&ielock);

	/*
	 * Don't print same error message too often.
	 */
	if ((last == (hrestime.tv_sec & ~1)) && (lastfmt == fmt)) {
		mutex_exit(&ielock);
		return;
	}
	last = hrestime.tv_sec & ~1;
	lastfmt = fmt;

	va_start(ap, fmt);
	vsprintf(msg_buffer, fmt, ap);
	cmn_err(CE_CONT, "%s%d: %s", ddi_get_name(dip),
					ddi_get_instance(dip),
					msg_buffer);
	va_end(ap);

	mutex_exit(&ielock);
}
