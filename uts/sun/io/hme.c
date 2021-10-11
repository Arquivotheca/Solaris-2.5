/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)hme.c 1.36	95/10/16 SMI"

/*
 * SunOS MT STREAMS FEPS Fast-Ethernet Device Driver
 */

#include	<sys/types.h>
#include	<sys/debug.h>
#include	<sys/stropts.h>
#include	<sys/stream.h>
#include	<sys/cmn_err.h>
#include	<sys/vtrace.h>
#include	<sys/kmem.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>
#include	<sys/strsun.h>
#include	<sys/stat.h>
#include	<sys/cpu.h>
#include	<sys/kstat.h>
#include	<inet/common.h>
#include	<sys/dlpi.h>
#include	<sys/ethernet.h>
#include	<sys/hme_phy.h>
#include	<sys/hme_mac.h>
#include	<sys/hme.h>

typedef int	(*fptri_t)();
typedef void	(*fptrv_t)();


#define	FEPS_ERX_BUG
#define	HME_ERX_DEBUG

#define	FEPS_URUN_BUG
#define	HME_CODEVIOL_BUG

/*
#define	HME_LANCE_MODE_DEBUG
#define	HME_BOOT_DEBUG
#define	HME_CONF_DEBUG
#define	HME_FASTDMA_DEBUG
#define	HME_IPG_DEBUG
#define	HME_ND_DEBUG
#define	HME_PHY_DEBUG
#define	HME_LATECOLL_DEBUG
#define	HME_CFG_DEBUG
#define	HME_MIFPOLL_DEBUG
#define	HME_AUTOINFO_DEBUG
#define	HME_LINKCHECK_DEBUG
#define	HME_AR_DEBUG
#define	HME_AUTOINFO_DEBUG
#define	HME_FDX_DEBUG
#define	HME_AUTONEG_DEBUG
#define	HME_LINKCHECK_DEBUG
#define	HMEDEBUG
#define	HME_EXT_XCVR_DEBUG
#define	HME_SPURIOUS_INTERRUPT
#define	HME_FRM_DEBUG
#define	HME_SYNC_DEBUG
#define	HME_READ_DEBUG
#define	HME_TX_SLOW
#define	HME_FORCE_DEBUG
*/

/*
 * Patchable debug flag.
 * Set this to nonzero to enable error messages.
 */
static	int	hmedebug = 0;

/*
 * The followng variables are used for checking fixes in FEPS 2.0
 */
static	int	hme_erx_fix = 1;	/* Use the fix for erx bug */
static	int	hme_erx_debug = 0;
static	int	hme_urun_fix = 0;	/* Bug fixed in FEPS 2.0 */

/*
 * The followng variables are used for configuring various features
 */
static	int	hme_64bit_enable = 1;	/* Use 64-bit sbus transfers */
static	int	hme_reject_own = 1;	/* Reject packets with own SA */
static	int	hme_autoneg_enable = 1;	/* Enable auto-negotiation */

static	int	hme_ngu_enable = 1;	/* to enable Never Give Up mode */
static	int	hme_mifpoll_enable = 1; /* to enable mif poll */


/*
 * The followng variables are used for configuring link-operation.
 * Later these parameters may be changed per interface using "ndd" command
 * These parameters may also be specified as properties using the .conf
 * file mechanism for each interface.
 */

static	int	hme_lance_mode = 1; /* to enable lance mode */
static	int	hme_ipg0 = 16;
static	int	hme_ipg1 = 8;
static	int	hme_ipg2 = 4;
static	int	hme_use_int_xcvr = 0;
static	int	hme_pace_size = 0;	/* Do not use pacing */

/*
 * The following parameters may be configured by the user. If they are not
 * configured by the user, the values will be based on the capabilities of
 * the transceiver.
 * The value "HME_NOTUSR" is ORed with the parameter value to indicate values
 * which are NOT configured by the user.
 */

#define	HME_NOTUSR	0x0f000000

static	int	hme_adv_autoneg_cap = HME_NOTUSR | 0;
static	int	hme_adv_100fdx_cap = HME_NOTUSR | 0;
static	int	hme_adv_100hdx_cap = HME_NOTUSR | 0;
static	int	hme_adv_10fdx_cap = HME_NOTUSR | 0;
static	int	hme_adv_10hdx_cap = HME_NOTUSR | 0;

/*
 * PHY_IDR1 and PHY_IDR2 values to identify National Semiconductor's DP83840
 * Rev C chip which needs some work-arounds.
 */
#define	HME_NSIDR1	0x2000
#define	HME_NSIDR2	0x5c00
#define	HME_DP83840_RevC	((hmep->hme_idr1 == HME_NSIDR1) && \
				(hmep->hme_idr2 == HME_NSIDR2))
/*
 * Function prototypes.
 */
static	int hmeidentify(dev_info_t *);
static	int hmeattach(dev_info_t *, ddi_attach_cmd_t);
static	int hmedetach(dev_info_t *, ddi_detach_cmd_t);
static	int hmestop(struct hme *);
#ifndef	MPSAS
static	void hmestatinit(struct hme *);
#endif	MPSAS
static	int hmeinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static	void hmeallocthings(struct hme *);
static	void hmefreebufs(struct hme *);
static	int hmeopen(queue_t *, dev_t *, int, int, cred_t *);
static	int hmeclose(queue_t *);
static	int hmewput(queue_t *, mblk_t *);
static	int hmewsrv(queue_t *);
static	void hmeproto(queue_t *, mblk_t *);
static	void hmeioctl(queue_t *, mblk_t *);
static	void hme_dl_ioc_hdr_info(queue_t *, mblk_t *);
static	void hmeareq(queue_t *, mblk_t *);
static	void hmedreq(queue_t *, mblk_t *);
static	void hmedodetach(struct hmestr *);
static	void hmebreq(queue_t *, mblk_t *);
static	void hmeubreq(queue_t *, mblk_t *);
static	void hmeireq(queue_t *, mblk_t *);
static	void hmeponreq(queue_t *, mblk_t *);
static	void hmepoffreq(queue_t *, mblk_t *);
static	void hmeemreq(queue_t *, mblk_t *);
static	void hmedmreq(queue_t *, mblk_t *);
static	void hmepareq(queue_t *, mblk_t *);
static	void hmespareq(queue_t *, mblk_t *);
static	void hmeudreq(queue_t *, mblk_t *);
static	int hmestart(queue_t *, mblk_t *, struct hme *);
static	u_int hmeintr();
static	void hmewenable(struct hme *);
static	void hmereclaim(struct hme *);
static	int hmeinit(struct hme *);
static	void hmeuninit(struct hme *hmep);
static	void hmermdinit(volatile struct hme_rmd *, u_long);
static  void hmerror(dev_info_t *dip, char *fmt, ...);
static  void hme_display_msg(struct hme *, dev_info_t *dip, char *fmt, ...);
static	u_int hmetxreset(struct hme *);
static	u_int hmerxreset(struct hme *);
static	mblk_t *hmeaddudind(struct hme *, mblk_t *, struct ether_addr *,
	struct ether_addr *, int, ulong);
static	struct hmestr *hmeaccept(struct hmestr *, struct hme *, int,
	struct ether_addr *);
static	struct hmestr *hmepaccept(struct hmestr *, struct hme *, int,
	struct	ether_addr *);
static	void hmesetipq(struct hme *);
static	int hmemcmatch(struct hmestr *, struct ether_addr *);
static	void hmesendup(struct hme *, mblk_t *, struct hmestr *(*)());
static 	void hmeread(struct hme *, volatile struct hme_rmd *);
static	void hmesavecntrs(struct hme *);
static void hme_fatal_err(struct hme *, u_int);
static void hme_nonfatal_err(struct hme *, u_int);
static void	 hme_check_link(struct hme *);
static void	 hme_try_speed(struct hme *);
static void	 hme_force_speed(struct hme *);
static int 	hmeburstsizes(dev_info_t *dip);
static int	hme_try_auto_negotiation(struct hme *);
static void	hme_get_autoinfo(struct hme *);
static void	hme_start_mifpoll(struct hme *);
static void	hme_stop_mifpoll(struct hme *);
static	void	hme_param_cleanup(struct hme *);
static	int	hme_param_get(queue_t * q, mblk_t * mp, caddr_t cp);
static	int	hme_param_register(struct hme *, hmeparam_t *, int);
static	int	hme_param_set(queue_t *, mblk_t *, char *, caddr_t);
static	void	hme_setup_link_default(struct hme *);
static void	hme_setup_link_status(struct hme *);
static void	hme_setup_link_control(struct hme *);

static	int	mi_mpprintf(MBLKP mp, char * fmt, ...);
static	long	mi_strtol(char * str, char ** ptr, int base);
static	int	mi_mpprintf_putc(char * cookie, int ch);

static	void	hme_nd_free(caddr_t * nd_pparam);
#ifdef notdef
static	int	hme_nd_get_long(queue_t * q, MBLKP mp, caddr_t data);
static	int	hme_nd_set_long(queue_t * q, MBLKP mp, char * value,
								caddr_t data);
#endif
static	int	hme_nd_getset(queue_t * q, caddr_t nd_param, MBLKP mp);
static	boolean_t	hme_nd_load(caddr_t * nd_pparam, char * name,
				pfi_t get_pfi, pfi_t set_pfi, caddr_t data);
staticf	int	nd_get_default(queue_t * q, MBLKP mp, caddr_t data);
staticf	int nd_get_names(queue_t * q, MBLKP mp, caddr_t nd_param);
staticf	int nd_set_default(queue_t * q, MBLKP mp, char * value, caddr_t data);

#define	ND_BASE		('N' << 8)	/* base */
#define	ND_GET		(ND_BASE + 0)	/* Get a value */
#define	ND_SET		(ND_BASE + 1)	/* Set a value */


static void 	hme_bb_force_idle(struct hme *);

static	struct	module_info	hmeminfo = {
	HMEIDNUM,	/* mi_idnum */
	HMENAME,	/* mi_idname */
	HMEMINPSZ,	/* mi_minpsz */
	HMEMAXPSZ,	/* mi_maxpsz */
	HMEHIWAT,	/* mi_hiwat */
	HMELOWAT		/* mi_lowat */
};

static	struct	qinit	hmerinit = {
	NULL,		/* qi_putp */
	NULL,		/* qi_srvp */
	hmeopen,	/* qi_qopen */
	hmeclose,	/* qi_qclose */
	NULL,		/* qi_qadmin */
	&hmeminfo,	/* qi_minfo */
	NULL		/* qi_mstat */
};

static	struct	qinit	hmewinit = {
	hmewput,		/* qi_putp */
	hmewsrv,		/* qi_srvp */
	NULL,		/* qi_qopen */
	NULL,		/* qi_qclose */
	NULL,		/* qi_qadmin */
	&hmeminfo,	/* qi_minfo */
	NULL		/* qi_mstat */
};

static struct	streamtab	hme_info = {
	&hmerinit,	/* st_rdinit */
	&hmewinit,	/* st_wrinit */
	NULL,		/* st_muxrinit */
	NULL		/* st_muxwrinit */
};

static	struct	cb_ops	cb_hme_ops = {
	nodev,		/* cb_open */
	nodev,		/* cb_close */
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
	&hme_info,	/* cb_stream */
	D_MP		/* cb_flag */
};

static	struct	dev_ops	hme_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	hmeinfo,		/* devo_getinfo */
	hmeidentify,		/* devo_identify */
	nulldev,		/* devo_probe */
	hmeattach,		/* devo_attach */
	hmedetach,		/* devo_detach */
	nodev,			/* devo_reset */
	&cb_hme_ops,		/* devo_cb_ops */
	(struct bus_ops *)NULL,	/* devo_bus_ops */
	ddi_power		/* devo_power */
};

/*
 * Claim the device is ultra-capable of burst in the begining.  Use
 * the value returned by ddi_dma_burstsizes() to actually set the HME
 * global configuration register later.
 *
 * FEPS supports burst sizes of 16, 32 and 64 bytes. Also, it supports
 * 32-bit and 64-bit Sbus transfers. Hence the dlim_burstsizes field contains
 * the the burstsizes in both the lo and hi words.
 */
#define	HMELIMADDRLO	(0x00000000)
#define	HMELIMADDRHI	(0xffffffff)

static ddi_dma_lim_t hme_dma_limits = {
	(u_long) HMELIMADDRLO,	/* dlim_addr_lo */
	(u_long) HMELIMADDRHI,	/* dlim_addr_hi */
	(u_int) HMELIMADDRHI,	/* dlim_cntr_max */
	(u_int) 0x00700070,	/* dlim_burstsizes for 32 and 64 bit xfers */
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
	"FEPS Ethernet Driver  v 1.36",
	&hme_ops,	/* driver ops */
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
static	kmutex_t	hmeautolock;

/*
 * Linked list of active (inuse) driver Streams.
 */
static	struct	hmestr	*hmestrup = NULL;
static	krwlock_t	hmestruplock;

/*
 * Single private "global" lock for the few rare conditions
 * we want single-threaded.
 */
static	kmutex_t	hmelock;

static	int	hme_device = -1;

/*
 * Internal PHY Id:
 */

#define	HME_BB1	0x15	/* Babybac1, Rev 1.5 */
#define	HME_BB2 0x20	/* Babybac2, Rev 0 */


#ifdef MPSAS
int hme_mpsas_period = 1000;
#endif

/* <<<<<<<<<<<<<<<<<<<<<<  Configuration Parameters >>>>>>>>>>>>>>>>>>>>> */

#define	BMAC_DEFAULT_JAMSIZE	(0x04)		/* jamsize equals 4 */
#define	BMAC_LONG_JAMSIZE	(0x10)		/* jamsize equals 0x10 */
static	int 	jamsize = BMAC_DEFAULT_JAMSIZE;


/* <<<<<<<<<<<<<<<<<<<<<<<<  Bit Bang Operations >>>>>>>>>>>>>>>>>>>>>>>> */

static int hme_internal_phy_id = HME_BB2;	/* Internal PHY is Babybac2  */


static void
send_bit(hmep, x)
struct hme	*hmep;
u_int		x;
{
	volatile struct hme_mif	*mifp = hmep->hme_mifregp;

	mifp->mif_bbdata = x;
	mifp->mif_bbclk = HME_BBCLK_LOW;
	mifp->mif_bbclk = HME_BBCLK_HIGH;
}

/*
 * To read the MII register bits from the Babybac1 transceiver
 */
static u_int
get_bit(hmep)
struct hme	*hmep;
{
	volatile struct hme_mif	*mifp = hmep->hme_mifregp;
	u_int	x;

	mifp->mif_bbclk = HME_BBCLK_LOW;
	mifp->mif_bbclk = HME_BBCLK_HIGH;
	if (hmep->hme_transceiver == HME_INTERNAL_TRANSCEIVER)
		x = (mifp->mif_cfg & HME_MIF_CFGM0) ? 1 : 0;
	else
		x = (mifp->mif_cfg & HME_MIF_CFGM1) ? 1 : 0;
	return (x);
}


/*
 * To read the MII register bits according to the IEEE Standard
 */
static u_int
get_bit_std(hmep)
struct hme	*hmep;
{
	volatile struct hme_mif	*mifp = hmep->hme_mifregp;
	u_int	x;

	mifp->mif_bbclk = HME_BBCLK_LOW;
	drv_usecwait(1);	/* wait for  >330 ns for stable data */
	if (hmep->hme_transceiver == HME_INTERNAL_TRANSCEIVER)
		x = (mifp->mif_cfg & HME_MIF_CFGM0) ? 1 : 0;
	else
		x = (mifp->mif_cfg & HME_MIF_CFGM1) ? 1 : 0;
	mifp->mif_bbclk = HME_BBCLK_HIGH;
	return (x);
}

#define	SEND_BIT(x)		send_bit(hmep, x)
#define	GET_BIT(x)		x = get_bit(hmep)
#define	GET_BIT_STD(x)		x = get_bit_std(hmep)


static void
hme_bb_mii_write(hmep, regad, data)
struct hme	*hmep;
u_char		regad;
u_short		data;
{
	u_char	phyad;
	int		i;
	volatile struct hme_mif	*mifp = hmep->hme_mifregp;

	mifp->mif_bbopenb = 1;	/* Enable the MII driver */
	phyad = hmep->hme_phyad;
	(void) hme_bb_force_idle(hmep);
	SEND_BIT(0); SEND_BIT(1);	/* <ST> */
	SEND_BIT(0); SEND_BIT(1);	/* <OP> */
	for (i = 4; i >= 0; i--) {		/* <AAAAA> */
		SEND_BIT((phyad >> i) & 1);
	}
	for (i = 4; i >= 0; i--) {		/* <RRRRR> */
		SEND_BIT((regad >> i) & 1);
	}
	SEND_BIT(1); SEND_BIT(0);	/* <TA> */
	for (i = 0xf; i >= 0; i--) {	/* <DDDDDDDDDDDDDDDD> */
		SEND_BIT((data >> i) & 1);
	}
	mifp->mif_bbopenb = 0;	/* Disable the MII driver */
}

/* Return 0 if OK, 1 if error (Transceiver does not talk management) */
static int
hme_bb_mii_read(hmep, regad, datap)
struct hme	*hmep;
u_char		regad;
u_short		*datap;
{
	u_char	phyad;
	int		i;
	u_int	x;
	u_int	y;
	volatile struct hme_mif	*mifp = hmep->hme_mifregp;

	*datap = 0;

	mifp->mif_bbopenb = 1;	/* Enable the MII driver */
	phyad = hmep->hme_phyad;
	(void) hme_bb_force_idle(hmep);
	SEND_BIT(0); SEND_BIT(1);	/* <ST> */
	SEND_BIT(1); SEND_BIT(0);	/* <OP> */
	for (i = 4; i >= 0; i--) {		/* <AAAAA> */
		SEND_BIT((phyad >> i) & 1);
	}
	for (i = 4; i >= 0; i--) {		/* <RRRRR> */
		SEND_BIT((regad >> i) & 1);
	}

	mifp->mif_bbopenb = 0;	/* Disable the MII driver */

	if ((hme_internal_phy_id == HME_BB2) ||
			(hmep->hme_transceiver == HME_EXTERNAL_TRANSCEIVER)) {
		GET_BIT_STD(x);
		GET_BIT_STD(y);		/* <TA> */
		for (i = 0xf; i >= 0; i--) {	/* <DDDDDDDDDDDDDDDD> */
			GET_BIT_STD(x);
			*datap += (x << i);
		}
		/* Kludge to get the Transceiver out of hung mode */
		GET_BIT_STD(x);
		GET_BIT_STD(x);
		GET_BIT_STD(x);
	} else {
		GET_BIT(x);
		GET_BIT(y);		/* <TA> */
		for (i = 0xf; i >= 0; i--) {	/* <DDDDDDDDDDDDDDDD> */
			GET_BIT(x);
			*datap += (x << i);
		}
		/* Kludge to get the Transceiver out of hung mode */
		GET_BIT(x);
		GET_BIT(x);
		GET_BIT(x);
	}

	return (y);
}


static void
hme_bb_force_idle(hmep)
struct hme	*hmep;
{
	int		i;

	for (i = 0; i < 33; i++) {
		SEND_BIT(1);
	}
}

/* <<<<<<<<<<<<<<<<<<<<End of Bit Bang Operations >>>>>>>>>>>>>>>>>>>>>>>> */


/* <<<<<<<<<<<<< Frame Register used for MII operations >>>>>>>>>>>>>>>>>>>> */

#ifdef HME_FRM_DEBUG
int hme_frame_flag = 0;
#endif

/* Return 0 if OK, 1 if error (Transceiver does not talk management) */
static int
hme_mii_read(hmep, regad, datap)
struct hme	*hmep;
u_char		regad;
u_short		*datap;
{
	volatile u_int	*framerp = &hmep->hme_mifregp->mif_frame;
	u_int 		frame;
	u_char		phyad;

	if (hmep->hme_transceiver == HME_NO_TRANSCEIVER)
		return (1);	/* No transceiver present */

	if (!hmep->hme_frame_enable)
		return (hme_bb_mii_read(hmep, regad, datap));

	phyad = hmep->hme_phyad;
#ifdef HME_FRM_DEBUG
	if (!hme_frame_flag) {
		hmerror(hmep->hme_dip, "Frame Register used for MII");
		hme_frame_flag = 1;
	}
		hmerror(hmep->hme_dip,
		"Frame Reg :mii_read: phyad = %X reg = %X ", phyad, regad);
#endif

	*framerp = HME_MIF_FRREAD | (phyad << HME_MIF_FRPHYAD_SHIFT) |
					(regad << HME_MIF_FRREGAD_SHIFT);
/*
	HMEDELAY((*framerp & HME_MIF_FRTA0), HMEMAXRSTDELAY);
*/
	HMEDELAY((*framerp & HME_MIF_FRTA0), 300);
	frame = *framerp;
	if ((frame & HME_MIF_FRTA0) == 0) {
		hmerror(hmep->hme_dip, "MIF Read failure: data = %X", frame);
		return (1);
	} else {
		*datap = (u_short)(frame & HME_MIF_FRDATA);
#ifdef HME_FRM_DEBUG
		hmerror(hmep->hme_dip,
			"Frame Reg :mii_read: succesful:data = %X ", *datap);
#endif
		return (0);
	}

}

static void
hme_mii_write(hmep, regad, data)
struct hme	*hmep;
u_char		regad;
u_short		data;
{
	volatile u_int	*framerp = &hmep->hme_mifregp->mif_frame;
	u_int frame;
	u_char		phyad;

	if (!hmep->hme_frame_enable) {
		hme_bb_mii_write(hmep, regad, data);
		return;
	}

	phyad = hmep->hme_phyad;
#ifdef HME_FRM_DEBUG
		hmerror(hmep->hme_dip, "FRame Reg :mii_write: phyad = %X \
reg = %X data = %X", phyad, regad, data);
#endif
	*framerp = HME_MIF_FRWRITE | (phyad << HME_MIF_FRPHYAD_SHIFT) |
					(regad << HME_MIF_FRREGAD_SHIFT) | data;
/*
	HMEDELAY((*framerp & HME_MIF_FRTA0), HMEMAXRSTDELAY);
*/
	HMEDELAY((*framerp & HME_MIF_FRTA0), 300);
	frame = *framerp;
	if ((frame & HME_MIF_FRTA0) == 0) {
		hmerror(hmep->hme_dip, "MIF Write failure");
		return;
	} else {
#ifdef HME_FRM_DEBUG
		hmerror(hmep->hme_dip, "Frame Reg :mii_write: succesful");
#endif
		return;
	}
}


#ifdef notdef

static void
hme_delay(usec)
int		usec;
{
	hrtime_t		hrtime_start;
	hrtime_t		hrtime_end;
	timestruc_t		timestruc_start;
	timestruc_t		timestruc_end;
	long			nsec;

	hrtime_start = gethrtime();
	hrt2ts(hrtime_start, &timestruc_start);
	do {
		hrtime_end = gethrtime();
		hrt2ts(hrtime_end, &timestruc_end);
		nsec = timestruc_end.tv_nsec - timestruc_start.tv_nsec;
		if (nsec < 0) {
			nsec = timestruc_end.tv_nsec +
					(0x7fffffff - timestruc_start.tv_nsec);
		}
	} while (nsec < usec*1000);
}
#endif notdef

/*
 * hme_stop_timer function is used by a function before doing link-related
 * processing. It locks the "hme_linklock" to protect the link-related data
 * structures. This lock will be subsequently released in hme_start_timer().
 */
static void
hme_stop_timer(hmep)
struct hme	*hmep;
{
	mutex_enter(&hmep->hme_linklock);
	if (hmep->hme_timerid) {
		untimeout(hmep->hme_timerid);
		hmep->hme_timerid = 0;
	}
}

static void
hme_start_timer(hmep, func, msec)
struct hme	*hmep;
fptrv_t		func;
int			msec;
{
	if (hmep->hme_timerid)
		untimeout(hmep->hme_timerid);
	hmep->hme_timerid = timeout(func, (caddr_t)hmep,
						drv_usectohz(1000*msec));
	mutex_exit(&hmep->hme_linklock);
}

/*
 * hme_select_speed is required only when auto-negotiation is not supported.
 * It should be used only for the Internal Transceiver and not the External
 * transceiver because we wouldn't know how to generate Link Down state on
 * the wire.
 * Currently it is required to support Electron 1.1 Build machines. When all
 * these machines are upgraded to 1.2 or better, remove this function.
 *
 * Returns 1 if the link is up, 0 otherwise.
 */

static int
hme_select_speed(hmep, speed)
struct hme	*hmep;
int			speed;
{
	u_short		stat;
	u_int 		fdx;

#ifdef MPSAS
	return (0);
#endif
	if (hmep->hme_linkup_cnt)  /* not first time */
		goto read_status;

	if (hmep->hme_fdx)
		fdx = PHY_BMCR_FDX;
	else
		fdx = 0;

	switch (speed) {
	case HME_SPEED_100:

		switch (hmep->hme_transceiver) {
		case HME_INTERNAL_TRANSCEIVER:
			hme_mii_write(hmep, HME_PHY_BMCR, fdx | PHY_BMCR_100M);
			break;
		case HME_EXTERNAL_TRANSCEIVER:
			if (hmep->hme_delay == 0) {
				hme_mii_write(hmep, HME_PHY_BMCR,
							fdx | PHY_BMCR_100M);
			}
			break;
		default:
			break;
		}
		break;
	case HME_SPEED_10:
		if (hmep->hme_delay == 0) {
			hme_mii_write(hmep, HME_PHY_BMCR, fdx);
		}
		break;
	default:
		return (0);
	}

	if (!hmep->hme_linkup_cnt) {  /* first time; select speed */
		(void) hme_mii_read(hmep, HME_PHY_BMSR, &stat);
		hmep->hme_linkup_cnt++;
		return (0);
	}

read_status:
	hmep->hme_linkup_cnt++;
	(void) hme_mii_read(hmep, HME_PHY_BMSR, &stat);
	if (stat & PHY_BMSR_LNKSTS)
		return (1);
	else
		return (0);
}


static int
hme_reset_transceiver(hmep)
struct hme	*hmep;
{
	volatile struct hme_mif	*mifp = hmep->hme_mifregp;
	u_int 		cfg;
	u_short		stat;
	u_short		anar;
	u_short		control;
	u_short		csc;
	int n;

#ifdef MPSAS
	return (0);
#endif


	cfg = mifp->mif_cfg;
	if (hmep->hme_transceiver == HME_EXTERNAL_TRANSCEIVER) {
		/* Isolate the Internal Transceiver */
		mifp->mif_cfg = (u_int) (cfg & ~HME_MIF_CFGPS);
		hmep->hme_phyad = HME_INTERNAL_PHYAD;
		hmep->hme_transceiver = HME_INTERNAL_TRANSCEIVER;
		hme_mii_write(hmep, HME_PHY_BMCR, (PHY_BMCR_ISOLATE |
				PHY_BMCR_PWRDN | PHY_BMCR_LPBK));
		if (hme_mii_read(hmep, HME_PHY_BMCR, &control) == 1) {
			return (1);	/* Transceiver does not talk MII */
		}

		/* select the External transceiver */
		mifp->mif_cfg = (u_int) (cfg | HME_MIF_CFGPS);
		hmep->hme_transceiver = HME_EXTERNAL_TRANSCEIVER;
		hmep->hme_phyad = HME_EXTERNAL_PHYAD;
	} else if (cfg & HME_MIF_CFGM1) {
	/* Isolate the External transceiver, if present */
		mifp->mif_cfg = (u_int) (cfg | HME_MIF_CFGPS);
		hmep->hme_phyad = HME_EXTERNAL_PHYAD;
		hmep->hme_transceiver = HME_EXTERNAL_TRANSCEIVER;
		hme_mii_write(hmep, HME_PHY_BMCR, (PHY_BMCR_ISOLATE |
				PHY_BMCR_PWRDN | PHY_BMCR_LPBK));
		if (hme_mii_read(hmep, HME_PHY_BMCR, &control) == 1) {
			return (1);	/* Transceiver does not talk MII */
		}

		/* select the Internal transceiver */
		mifp->mif_cfg = (u_int) (cfg & ~HME_MIF_CFGPS);
		hmep->hme_transceiver = HME_INTERNAL_TRANSCEIVER;
		hmep->hme_phyad = HME_INTERNAL_PHYAD;

	}

	hme_mii_write(hmep, HME_PHY_BMCR, PHY_BMCR_RESET);

	/* Check for transceiver reset completion */
	n = HME_PHYRST_MAXDELAY / HMEWAITPERIOD;
	while (--n > 0) {
		if (hme_mii_read(hmep, HME_PHY_BMCR, &control) == 1) {
			return (1);	/* Transceiver does not talk MII */
		}
		if ((control & PHY_BMCR_RESET) == 0)
			break;
		drv_usecwait(HMEWAITPERIOD);
	}
	if (n == 0)
		return (1);	/* transceiver reset failure */

	/*
	 * Get the PHY id registers. We need this to implement work-arounds
	 * for bugs in transceivers which use the National DP83840 PHY chip.
	 * National should fix this in the next release.
	 */

	(void) hme_mii_read(hmep, HME_PHY_BMSR, &stat);
	(void) hme_mii_read(hmep, HME_PHY_IDR1, &hmep->hme_idr1);
	(void) hme_mii_read(hmep, HME_PHY_IDR2, &hmep->hme_idr2);
	(void) hme_mii_read(hmep, HME_PHY_ANAR, &anar);

#ifdef HME_PHY_DEBUG
	if (hmedebug) {
		hmerror(hmep->hme_dip,
	"reset_trans: control = %x status = %x idr1 = %x idr2 = %x anar = %x",
		control, stat, hmep->hme_idr1, hmep->hme_idr2, anar);
	}
#endif

	hmep->hme_bmcr = control;
	hmep->hme_anar = anar;
	hmep->hme_bmsr = stat;

	/*
	 * The strapping of AN0 and AN1 pins on DP83840 cannot select
	 * 10FDX, 100FDX and Auto-negotiation. So select it here for the
	 * Internal Transceiver.
	 */
	if (hmep->hme_transceiver == HME_INTERNAL_TRANSCEIVER) {
		anar = (PHY_ANAR_TX | PHY_ANAR_10 | PHY_SELECTOR);
	}
	/*
	 * Modify control and bmsr based on anar for Rev-C of DP83840.
	 */
	if (HME_DP83840_RevC) {
		n = 0;
		if (anar & PHY_ANAR_TXFDX) {
			stat |= PHY_BMSR_100FDX;
			n++;
		}
		else
			stat &= ~PHY_BMSR_100FDX;

		if (anar & PHY_ANAR_TX) {
			stat |= PHY_BMSR_100HDX;
			n++;
		}
		else
			stat &= ~PHY_BMSR_100HDX;

		if (anar & PHY_ANAR_10FDX) {
			stat |= PHY_BMSR_10FDX;
			n++;
		}
		else
			stat &= ~PHY_BMSR_10FDX;

		if (anar & PHY_ANAR_10) {
			stat |= PHY_BMSR_10HDX;
			n++;
		}
		else
			stat &= ~PHY_BMSR_10HDX;

		if (n == 1) { 	/* only one mode. disable auto-negotiation */
			stat &= ~PHY_BMSR_ACFG;
			control &= ~PHY_BMCR_ANE;
		}
		if (n) {
			hmep->hme_bmsr = stat;
			hmep->hme_bmcr = control;
#ifdef HME_PHY_DEBUG
	if (hmedebug)
		hmerror(hmep->hme_dip,
		"DP83840 Rev-C found: Modified bmsr = %x control = %X n = %x",
							stat, control, n);
#endif
		}
	}
	hme_setup_link_default(hmep);
	hme_setup_link_status(hmep);


	/* Place the Transceiver in normal operation mode */
	hme_mii_write(hmep, HME_PHY_BMCR, (control & ~PHY_BMCR_ISOLATE));

	/* check if the transceiver is not in Isolate mode */
	n = HME_PHYRST_MAXDELAY / HMEWAITPERIOD;
	while (--n > 0) {
		if (hme_mii_read(hmep, HME_PHY_BMCR, &control) == 1) {
			return (1);	/* Transceiver does not talk MII */
		}
		if ((control & PHY_BMCR_ISOLATE) == 0)
			goto setconn;
		drv_usecwait(HMEWAITPERIOD);
	}
	return (1);	/* transceiver reset failure */

	/*
	 * Work-around for the late-collision problem with 100m cables.
	 * National should fix this in the next release !
	 */
setconn:
	(void) hme_mii_read(hmep, HME_PHY_CSC, &csc);
#ifdef HME_LATECOLL_DEBUG
		hmerror(hmep->hme_dip,
		"hme_reset_trans: CSC read = %x written = %x",
					csc, csc | PHY_CSCR_FCONN);
#endif
	hme_mii_write(hmep, HME_PHY_CSC, (csc | PHY_CSCR_FCONN));


	return (0);
}


static void
hme_check_transceiver(hmep)
struct hme	*hmep;
{
	volatile struct hme_mif	*mifp;
	u_int 			cfgsav;
	u_int 			cfg;
	u_int 			stat;

	/*
	 * If the MIF Polling is ON, and Internal transceiver is in use, just
	 * check for the presence of the External Transceiver.
	 * Otherwise:
	 * First check to see what transceivers are out there.
	 * If an external transceiver is present
	 * then use it, regardless of whether there is a Internal transceiver.
	 * If Internal transceiver is present and no external transceiver
	 * then use the Internal transceiver.
	 * If there is no external transceiver and no Internal transceiver,
	 * then something is wrong so print an error message.
	 */

#ifdef MPSAS
	hmep->hme_transceiver = HME_INTERNAL_TRANSCEIVER;
	return;
#endif MPSAS

	mifp = hmep->hme_mifregp;
	cfgsav = mifp->mif_cfg;
	if (hmep->hme_polling_on) {
#ifdef HME_MIFPOLL_DEBUG
	if (hmedebug)
		hmerror(hmep->hme_dip,
				"check_trans: polling_on: cfg = %X", cfgsav);
#endif

		if (hmep->hme_transceiver == HME_INTERNAL_TRANSCEIVER) {
			if ((cfgsav & HME_MIF_CFGM1) && !hme_param_use_intphy) {
				hme_stop_mifpoll(hmep);
				hmep->hme_phyad = HME_EXTERNAL_PHYAD;
				hmep->hme_transceiver =
						HME_EXTERNAL_TRANSCEIVER;
				mifp->mif_cfg = (u_int) ((cfgsav &
					~HME_MIF_CFGPE) | HME_MIF_CFGPS);
			}
		} else if (hmep->hme_transceiver == HME_EXTERNAL_TRANSCEIVER) {
			stat = (mifp->mif_bsts >> 16);
			if ((stat == 0x00) || (hme_param_use_intphy)) {
#ifdef HME_MIFPOLL_DEBUG
				if (hmedebug)
					hmerror(hmep->hme_dip,
					"External Transceiver disconnected");
#endif
				hme_stop_mifpoll(hmep);
				hmep->hme_phyad = HME_INTERNAL_PHYAD;
				hmep->hme_transceiver =
						HME_INTERNAL_TRANSCEIVER;
				mifp->mif_cfg = (u_int) (mifp->mif_cfg &
								~HME_MIF_CFGPS);
			}
		}
		return;
	}

#ifdef HME_MIFPOLL_DEBUG
	if (hmedebug)
		hmerror(hmep->hme_dip,
			"check_trans: polling_off: cfg = %X", cfgsav);
#endif

	cfg = mifp->mif_cfg;
	if ((cfg & HME_MIF_CFGM1) && !hme_param_use_intphy) {
		mifp->mif_cfg = (u_int) (cfgsav | HME_MIF_CFGPS);
		hmep->hme_phyad = HME_EXTERNAL_PHYAD;
		hmep->hme_transceiver = HME_EXTERNAL_TRANSCEIVER;
	} else if (cfg & HME_MIF_CFGM0) {  /* Internal Transceiver OK */
		mifp->mif_cfg = (u_int) (cfgsav & ~HME_MIF_CFGPS);
		hmep->hme_phyad = HME_INTERNAL_PHYAD;
		hmep->hme_transceiver = HME_INTERNAL_TRANSCEIVER;
	} else {
		hmep->hme_transceiver = HME_NO_TRANSCEIVER;
		hmerror(hmep->hme_dip, "No transceiver found!");
	}
}

#ifdef notdef
/*
 * Determine the speed selected by the PHY chip after auto-negotiation
 */
static void
hme_get_speed(hmep)
struct hme	*hmep;
{
	u_short ar;

	(void) hme_mii_read(hmep, HME_PHY_AR, &ar);
#ifdef HME_AR_DEBUG
	hmerror(hmep->hme_dip, "ar = %X", ar);
#endif
	if (ar & PHY_AR_SPEED10)
		hmep->hme_tryspeed = HME_SPEED_10;
	else
		hmep->hme_tryspeed = HME_SPEED_100;

}
#endif

static void
hme_setup_link_default(hmep)
struct hme	*hmep;
{
	u_short		bmsr;

	bmsr = hmep->hme_bmsr;
	if (hme_param_autoneg & HME_NOTUSR)
		hme_param_autoneg = HME_NOTUSR |
					((bmsr & PHY_BMSR_ACFG) ? 1 : 0);
	if (hme_param_anar_100fdx & HME_NOTUSR)
		hme_param_anar_100fdx = HME_NOTUSR; /* no fdx by default */
	if (hme_param_anar_100hdx & HME_NOTUSR)
		hme_param_anar_100hdx = HME_NOTUSR |
					((bmsr & PHY_BMSR_100HDX) ? 1 : 0);
	if (hme_param_anar_100fdx & HME_NOTUSR)
		hme_param_anar_10fdx = HME_NOTUSR; /* no fdx by default */
	if (hme_param_anar_100fdx & HME_NOTUSR)
		hme_param_anar_10hdx = HME_NOTUSR |
					((bmsr & PHY_BMSR_10HDX) ? 1 : 0);
}

static void
hme_setup_link_status(hmep)
struct hme	*hmep;
{
	u_short tmp;

	if (hmep->hme_transceiver == HME_EXTERNAL_TRANSCEIVER)
		hme_param_transceiver = 1;
	else
		hme_param_transceiver = 0;

	tmp = hmep->hme_bmsr;
	if (tmp & PHY_BMSR_ACFG)
		hme_param_bmsr_ancap = 1;
	else
		hme_param_bmsr_ancap = 0;
	if (tmp & PHY_BMSR_100FDX)
		hme_param_bmsr_100fdx = 1;
	else
		hme_param_bmsr_100fdx = 0;
	if (tmp & PHY_BMSR_100HDX)
		hme_param_bmsr_100hdx = 1;
	else
		hme_param_bmsr_100hdx = 0;
	if (tmp & PHY_BMSR_10FDX)
		hme_param_bmsr_10fdx = 1;
	else
		hme_param_bmsr_10fdx = 0;
	if (tmp & PHY_BMSR_10HDX)
		hme_param_bmsr_10hdx = 1;
	else
		hme_param_bmsr_10hdx = 0;

	if (!hmep->hme_linkup) {
		hme_param_linkup = 0;
		return;
	}

	hme_param_linkup = 1;
	if (hmep->hme_fdx == HME_FULL_DUPLEX)
		hme_param_mode = 1;
	else
		hme_param_mode = 0;

	if (hmep->hme_mode == HME_FORCE_SPEED) {
		if (hmep->hme_forcespeed == HME_SPEED_100)
			hme_param_speed = 1;
		else
			hme_param_speed = 0;
		return;
	}
	if (hmep->hme_tryspeed == HME_SPEED_100)
		hme_param_speed = 1;
	else
		hme_param_speed = 0;


	if (!(hmep->hme_aner & PHY_ANER_LPNW)) {
		hme_param_aner_lpancap = 0;
		hme_param_anlpar_100fdx = 0;
		hme_param_anlpar_100hdx = 0;
		hme_param_anlpar_10fdx = 0;
		hme_param_anlpar_10hdx = 0;
		return;
	}
	hme_param_aner_lpancap = 1;
	tmp = hmep->hme_anlpar;
	if (tmp & PHY_ANLPAR_TXFDX)
		hme_param_anlpar_100fdx = 1;
	else
		hme_param_anlpar_100fdx = 0;
	if (tmp & PHY_ANLPAR_TX)
		hme_param_anlpar_100hdx = 1;
	else
		hme_param_anlpar_100hdx = 0;
	if (tmp & PHY_ANLPAR_10FDX)
		hme_param_anlpar_10fdx = 1;
	else
		hme_param_anlpar_10fdx = 0;
	if (tmp & PHY_ANLPAR_10)
		hme_param_anlpar_10hdx = 1;
	else
		hme_param_anlpar_10hdx = 0;
}

static void
hme_setup_link_control(hmep)
struct hme	*hmep;
{
	u_int anar = PHY_SELECTOR;
	int	autoneg = ~HME_NOTUSR & hme_param_autoneg;
	int	anar_100fdx = ~HME_NOTUSR & hme_param_anar_100fdx;
	int	anar_100hdx = ~HME_NOTUSR & hme_param_anar_100hdx;
	int	anar_10fdx = ~HME_NOTUSR & hme_param_anar_10fdx;
	int	anar_10hdx = ~HME_NOTUSR & hme_param_anar_10hdx;

	if (autoneg) {
		hmep->hme_mode = HME_AUTO_SPEED;
		hmep->hme_tryspeed = HME_SPEED_100;
		if (anar_100fdx)
			anar |= PHY_ANAR_TXFDX;
		if (anar_100hdx)
			anar |= PHY_ANAR_TX;
		if (anar_10fdx)
			anar |= PHY_ANAR_10FDX;
		if (anar_10hdx)
			anar |= PHY_ANAR_10;
		hmep->hme_anar = anar;
	} else {
		hmep->hme_mode = HME_FORCE_SPEED;
		if (anar_100fdx) {
			hmep->hme_forcespeed = HME_SPEED_100;
			hmep->hme_fdx = HME_FULL_DUPLEX;
		} else if (anar_100hdx) {
			hmep->hme_forcespeed = HME_SPEED_100;
			hmep->hme_fdx = HME_HALF_DUPLEX;
#ifdef HME_FORCE_DEBUG
	hmerror(hmep->hme_dip, "hme_link_control: force 100 hdx");
#endif
		} else if (anar_10fdx) {
			hmep->hme_forcespeed = HME_SPEED_10;
			hmep->hme_fdx = HME_FULL_DUPLEX;
		} else {
			hmep->hme_forcespeed = HME_SPEED_10;
			hmep->hme_fdx = HME_HALF_DUPLEX;
#ifdef HME_FORCE_DEBUG
	hmerror(hmep->hme_dip, "hme_link_control: force 10 hdx");
#endif
		}
	}
}

/*
 * 	hme_check_link ()
 * Called as a result of HME_LINKCHECK_TIMER timeout, to poll for Transceiver
 * change or when a transceiver change has been detected by the hme_try_speed
 * function.
 * This function will also be called from the interrupt handler when polled mode
 * is used. Before calling this function the interrupt lock should be freed
 * so that the hmeinit() may be called.
 * Note that the hmeinit() function calls hme_select_speed() to set the link
 * speed and check for link status.
 */

static void
hme_check_link(hmep)
struct hme	*hmep;
{
	u_short		stat;
	u_int 		temp;
	volatile struct hme_mif		*mifp;

#ifdef MPSAS
	return;
#endif
	hme_stop_timer(hmep);	/* acquire hme_linklock */

#ifdef HME_LINKCHECK_DEBUG
	hmerror(hmep->hme_dip, "link_check entered:");
#endif
/*
 * check if the transceiver is the same.
 * init to be done if the external transceiver is connected/disconeected
 */
	temp = hmep->hme_transceiver; /* save the transceiver type */
	hme_check_transceiver(hmep);
	if ((temp != hmep->hme_transceiver) || (hmep->hme_linkup == 0)) {
		if (temp != hmep->hme_transceiver) {
			if (hmep->hme_transceiver == HME_EXTERNAL_TRANSCEIVER)
				hmerror(hmep->hme_dip,
					"External Transceiver Selected");
			else
				hmerror(hmep->hme_dip,
					"Internal Transceiver Selected");
		}
		hmep->hme_linkcheck = 0;
		hme_start_timer(hmep, hme_check_link, HME_LINKCHECK_TIMER);
		(void) hmeinit(hmep); /* To reset the transceiver and */
					/* to init the interface */
		return;
	}


	if (hmep->hme_mifpoll_enable) {
		mifp = hmep->hme_mifregp;
		stat = (mifp->mif_bsts >> 16);
#ifdef HME_MIFPOLL_DEBUG
		if (hmedebug)
		hmerror(hmep->hme_dip, "int_flag = %X old_stat = %X stat = %X",
			hmep->hme_mifpoll_flag, hmep->hme_mifpoll_data, stat);
#endif
		if (!hmep->hme_mifpoll_flag) {
			if (stat & PHY_BMSR_LNKSTS) {
				hme_start_timer(hmep, hme_check_link,
							HME_LINKCHECK_TIMER);
				return;
			}
#ifdef HME_MIFPOLL_DEBUG
			hmerror(hmep->hme_dip,
				"hme_check_link:DOWN polled data = %X\n", stat);
#endif
			hme_stop_mifpoll(hmep);
#ifdef HME_MIFPOLL_DEBUG
			temp = (mifp->mif_bsts >> 16);
			hmerror(hmep->hme_dip,
				"hme_check_link:after poll-stop: stat = %X",
									temp);
#endif
		} else {
			hmep->hme_mifpoll_flag = 0;
		}
	} else {
		if (hme_mii_read(hmep, HME_PHY_BMSR, &stat) == 1) {
		/* Transceiver does not talk mii */
			hme_start_timer(hmep, hme_check_link,
							HME_LINKCHECK_TIMER);
			return;
		} if (stat & PHY_BMSR_LNKSTS) {
			hme_start_timer(hmep, hme_check_link,
							HME_LINKCHECK_TIMER);
			return;
		}
	}

#ifdef HME_LINKCHECK_DEBUG
		hmerror(hmep->hme_dip, "mifpoll_flag = %x first stat = %X",
					hmep->hme_mifpoll_flag, stat);
#endif

	(void) hme_mii_read(hmep, HME_PHY_BMSR, &stat);
#ifdef HME_LINKCHECK_DEBUG
	hmerror(hmep->hme_dip, "second stat = %X", stat);
#endif
	/*
	 * The PHY may have automatically renegotiated link speed and mode.
	 * Get the new link speed and mode.
	 */
	if ((stat & PHY_BMSR_LNKSTS) && hme_autoneg_enable) {
		if (hmep->hme_mode == HME_AUTO_SPEED) {
			(void) hme_get_autoinfo(hmep);
			hme_start_mifpoll(hmep);
			if (hmep->hme_fdx != hmep->hme_macfdx) {
				hme_start_timer(hmep, hme_check_link,
						HME_LINKCHECK_TIMER);
				(void) hmeinit(hmep);
				return;
			}
		}
		hme_start_mifpoll(hmep);
		hme_start_timer(hmep, hme_check_link, HME_LINKCHECK_TIMER);
		return;
	}
	hmep->hme_linkup_msg = 1; /* Enable display of messages */
	hmerror(hmep->hme_dip, "Link Down");

	(void) hme_reset_transceiver(hmep);
	hmep->hme_linkcheck = 0;
	hmep->hme_linkup = 0;
	hme_setup_link_status(hmep);
	hmep->hme_autoneg = HME_HWAN_TRY;
	hmep->hme_force_linkdown = HME_FORCE_LINKDOWN;
	hmep->hme_linkup_cnt = 0;
	hmep->hme_delay = 0;
	hme_setup_link_control(hmep);
	hme_start_timer(hmep, hme_check_link, HME_LINKCHECK_TIMER);
	switch (hmep->hme_mode) {
	case HME_AUTO_SPEED:
		hmep->hme_linkup_10 = 0;
		hmep->hme_tryspeed = HME_SPEED_100;
		hmep->hme_ntries = hmep->hme_nlasttries =
						HME_NTRIES_LOW;
		hme_try_speed(hmep);
		break;
	case HME_FORCE_SPEED:
		hme_force_speed(hmep);
		break;
	default:
		break;
	}
}

static void
hme_display_transceiver(hmep)
struct hme	*hmep;
{
	switch (hmep->hme_transceiver) {
	case HME_INTERNAL_TRANSCEIVER:
		hme_display_msg(hmep, hmep->hme_dip,
						"Using Internal Transceiver");
		break;
	case HME_EXTERNAL_TRANSCEIVER:
		hme_display_msg(hmep, hmep->hme_dip,
						"Using External Transceiver");
		break;
	default:
		hmerror(hmep->hme_dip, "No transceiver found!");
		break;
	}
}

static void
hme_force_speed(hmep)
struct hme	*hmep;
{
	int		linkup;
	u_int		temp;
	u_short		csc;

#ifdef HME_FORCE_DEBUG
	hmerror(hmep->hme_dip, "hme_force_speed entered");
#endif
	hme_stop_timer(hmep);
	if (hmep->hme_fdx != hmep->hme_macfdx) {
		hme_start_timer(hmep, hme_check_link, HME_TICKS*5);
		return;
	}
	temp = hmep->hme_transceiver; /* save the transceiver type */
	hme_check_transceiver(hmep);
	if (temp != hmep->hme_transceiver) {
		if (hmep->hme_transceiver == HME_EXTERNAL_TRANSCEIVER)
			hmerror(hmep->hme_dip, "External Transceiver Selected");
		else
			hmerror(hmep->hme_dip, "Internal Transceiver Selected");
		hme_start_timer(hmep, hme_check_link, HME_TICKS * 10);
		return;
	}

	/*
	 * To interoperate with auto-negotiable capable systems
	 * the link should be brought down for 1 second.
	 * How to do this using only standard registers ?
	 */
	if (HME_DP83840_RevC) {
		if (hmep->hme_force_linkdown == HME_FORCE_LINKDOWN) {
			hmep->hme_force_linkdown = HME_LINKDOWN_STARTED;
			hme_mii_write(hmep, HME_PHY_BMCR, PHY_BMCR_100M);
			(void) hme_mii_read(hmep, HME_PHY_CSC, &csc);
			hme_mii_write(hmep, HME_PHY_CSC,
						(csc | PHY_CSCR_TXOFF));
			hme_start_timer(hmep, hme_force_speed, 10 * HME_TICKS);
			return;
		} else if (hmep->hme_force_linkdown == HME_LINKDOWN_STARTED) {
			(void) hme_mii_read(hmep, HME_PHY_CSC, &csc);
			hme_mii_write(hmep, HME_PHY_CSC,
						(csc & ~PHY_CSCR_TXOFF));
			hmep->hme_force_linkdown = HME_LINKDOWN_DONE;
		}
	} else {
		if (hmep->hme_force_linkdown == HME_FORCE_LINKDOWN) {
			hmep->hme_force_linkdown = HME_LINKDOWN_STARTED;
			hme_mii_write(hmep, HME_PHY_BMCR, PHY_BMCR_LPBK);
			hme_start_timer(hmep, hme_force_speed, 10 * HME_TICKS);
			return;
		} else if (hmep->hme_force_linkdown == HME_LINKDOWN_STARTED) {
			hmep->hme_force_linkdown = HME_LINKDOWN_DONE;
		}
	}


	linkup = hme_select_speed(hmep, hmep->hme_forcespeed);
	if (hmep->hme_linkup_cnt == 1) {
		hme_start_timer(hmep, hme_force_speed, SECOND(4));
		return;
	}
	if (linkup) {
		hmep->hme_linkup = 1;
		hmep->hme_linkcheck = 1;
		hme_display_transceiver(hmep);
		switch (hmep->hme_forcespeed) {
		case HME_SPEED_100:
			hme_display_msg(hmep, hmep->hme_dip,
							"100 Mbps Link Up");
			break;
		case HME_SPEED_10:
			hme_display_msg(hmep, hmep->hme_dip, "10 Mbps Link Up");
			break;
		default:
			break;
		}
		hme_setup_link_status(hmep);
		hme_start_mifpoll(hmep);
		hme_start_timer(hmep, hme_check_link, HME_LINKCHECK_TIMER);
	} else {
		hmerror(hmep->hme_dip, "Link Down - cable problem?");
		hmep->hme_linkup_msg = 1; /* Enable display of messages */
		hme_start_timer(hmep, hme_force_speed, HME_TICKS);
	}
}

static void
hme_get_autoinfo(hmep)
struct hme	*hmep;
{
	u_short		anar;
	u_short		aner;
	u_short		anlpar;
	u_short		tmp;
	u_short		ar;

	(void) hme_mii_read(hmep, HME_PHY_ANER, &aner);
	(void) hme_mii_read(hmep, HME_PHY_ANLPAR, &anlpar);
	(void) hme_mii_read(hmep, HME_PHY_ANAR, &anar);
#ifdef HME_AUTOINFO_DEBUG
	hmerror(hmep->hme_dip,
	"autoinfo: aner = %X anar = %X anlpar = %X", aner, anar, anlpar);
#endif
	hmep->hme_anlpar = anlpar;
	hmep->hme_aner = aner;

	if (aner & PHY_ANER_LPNW) {
#ifdef HME_AUTOINFO_DEBUG
			hmerror(hmep->hme_dip,
			" hme_try_autoneg: Link Partner AN able");
#endif
		tmp = anar & anlpar;
		if (tmp & PHY_ANAR_TXFDX) {
			hmep->hme_tryspeed = HME_SPEED_100;
			hmep->hme_fdx = HME_FULL_DUPLEX;
		} else if (tmp & PHY_ANAR_TX) {
			hmep->hme_tryspeed = HME_SPEED_100;
			hmep->hme_fdx = HME_HALF_DUPLEX;
		} else if (tmp & PHY_ANLPAR_10FDX) {
			hmep->hme_tryspeed = HME_SPEED_10;
			hmep->hme_fdx = HME_FULL_DUPLEX;
		} else if (tmp & PHY_ANLPAR_10) {
			hmep->hme_tryspeed = HME_SPEED_10;
			hmep->hme_fdx = HME_HALF_DUPLEX;
		} else {

#ifdef HME_AUTOINFO_DEBUG
			hmerror(hmep->hme_dip,
			" hme_try_autoneg: anar not set with speed selection");
#endif
			hmep->hme_fdx = HME_HALF_DUPLEX;
			(void) hme_mii_read(hmep, HME_PHY_AR, &ar);

#ifdef HME_AUTOINFO_DEBUG
			hmerror(hmep->hme_dip, "ar = %X", ar);
#endif
			if (ar & PHY_AR_SPEED10)
				hmep->hme_tryspeed = HME_SPEED_10;
			else
				hmep->hme_tryspeed = HME_SPEED_100;
		}
#ifdef HME_AUTOINFO_DEBUG
			hmerror(hmep->hme_dip,
			" hme_try_autoneg: fdx = %d", hmep->hme_fdx);
#endif
	} else {
#ifdef HME_AUTOINFO_DEBUG
			hmerror(hmep->hme_dip,
			" hme_try_autoneg: parallel detection done");
#endif
		hmep->hme_fdx = HME_HALF_DUPLEX;
		if (anlpar & PHY_ANLPAR_TX)
			hmep->hme_tryspeed = HME_SPEED_100;
		else if (anlpar & PHY_ANLPAR_10)
			hmep->hme_tryspeed = HME_SPEED_10;
		else {
#ifdef HME_AUTOINFO_DEBUG
			hmerror(hmep->hme_dip,
" hme_try_autoneg: parallel detection: anar not set with speed selection");
#endif

			(void) hme_mii_read(hmep, HME_PHY_AR, &ar);

#ifdef HME_AUTOINFO_DEBUG
			hmerror(hmep->hme_dip, "ar = %X", ar);
#endif
			if (ar & PHY_AR_SPEED10)
				hmep->hme_tryspeed = HME_SPEED_10;
			else
				hmep->hme_tryspeed = HME_SPEED_100;
		}
	}

	hmep->hme_linkup = 1;
	hmep->hme_linkcheck = 1;
	hme_display_transceiver(hmep);
	switch (hmep->hme_tryspeed) {
	case HME_SPEED_100:
		hme_display_msg(hmep, hmep->hme_dip, "100 Mbps Link Up ");
		break;
	case HME_SPEED_10:
		hmep->hme_linkup_10 = 0;
		hme_display_msg(hmep, hmep->hme_dip, "10 Mbps Link Up ");
		break;
	}

}

/*
 * Return 1 if the link is up or auto-negotiation being tried, 0 otherwise.
 */

static int
hme_try_auto_negotiation(hmep)
struct hme	*hmep;
{
	u_short		stat;
	u_short		aner;
#ifdef HME_AUTONEG_DEBUG
	u_short		anar;
	u_short		anlpar;
	u_short		control;
#endif

	if (hmep->hme_autoneg == HME_HWAN_TRY) {
					/* auto negotiation not initiated */
		(void) hme_mii_read(hmep, HME_PHY_BMSR, &stat);
		if (hme_mii_read(hmep, HME_PHY_BMSR, &stat) == 1) {
			/* Transceiver does not talk mii */
			goto hme_anfail;
		}
		if ((stat & PHY_BMSR_ACFG) == 0) { /* auto neg. not supported */
#ifdef HME_AUTONEG_DEBUG
			hmerror(hmep->hme_dip, " PHY status reg = %X", stat);
			hmerror(hmep->hme_dip,
				" Auto-negotiation not supported");
#endif
			return (hmep->hme_autoneg = HME_HWAN_FAILED);
		}

/*
 * Read ANER to clear status from previous operations.
 */
		if (hme_mii_read(hmep, HME_PHY_ANER, &aner) == 1) {
			/* Transceiver does not talk mii */
			goto hme_anfail;
		}

		hme_mii_write(hmep, HME_PHY_ANAR, hmep->hme_anar);
		hme_mii_write(hmep, HME_PHY_BMCR, PHY_BMCR_ANE | PHY_BMCR_RAN);
		/* auto-negotiation initiated */
		hmep->hme_delay = 0;
		hme_start_timer(hmep, hme_try_speed, HME_TICKS);
		return (hmep->hme_autoneg = HME_HWAN_INPROGRESS);
					/* auto-negotiation in progress */
	}

	/* Auto-negotiation has been in progress. Wait for atleast 1000 ms */
	if (hmep->hme_delay < 10) {
		hmep->hme_delay++;
		hme_start_timer(hmep, hme_try_speed, HME_TICKS);
		return (hmep->hme_autoneg = HME_HWAN_INPROGRESS);
	}

	(void) hme_mii_read(hmep, HME_PHY_BMSR, &stat);
	if (hme_mii_read(hmep, HME_PHY_BMSR, &stat) == 1) {
		/* Transceiver does not talk mii */
		goto hme_anfail;
	}

	if ((stat & PHY_BMSR_ANC) == 0) {
		/* wait for a maximum of 5 seconds */
		if (hmep->hme_delay < 50) {
			hmep->hme_delay++;
			hme_start_timer(hmep, hme_try_speed, HME_TICKS);
			return (hmep->hme_autoneg = HME_HWAN_INPROGRESS);
		}
#ifdef HME_AUTONEG_DEBUG
		hmerror(hmep->hme_dip,
				"Auto-negotiation not completed in 5 seconds");
		hmerror(hmep->hme_dip, " PHY status reg = %X", stat);
		hme_mii_read(hmep, HME_PHY_BMCR, &control);
		hmerror(hmep->hme_dip, " PHY control reg = %x", control);
		hme_mii_read(hmep, HME_PHY_ANAR, &anar);
		hmerror(hmep->hme_dip, " PHY anar reg = %x", anar);
		hme_mii_read(hmep, HME_PHY_ANER, &aner);
		hmerror(hmep->hme_dip, " PHY aner reg = %x", aner);
		hme_mii_read(hmep, HME_PHY_ANLPAR, &anlpar);
		hmerror(hmep->hme_dip, " PHY anlpar reg = %x", anlpar);
#endif
		hmerror(hmep->hme_dip, "Link Down - cable problem?");
		hmep->hme_linkup_msg = 1; /* Enable display of messages */
		goto hme_anfail;
	}
#ifdef HME_AUTONEG_DEBUG
	hmerror(hmep->hme_dip,
	"Auto-negotiation completed within %d 100ms time", hmep->hme_delay);
#endif
	(void) hme_mii_read(hmep, HME_PHY_ANER, &aner);
	if (aner & PHY_ANER_MLF) {
		hmerror(hmep->hme_dip,
		" hme_try_autoneg: parallel detection fault");
		goto hme_anfail;
	}

	if (!(stat & PHY_BMSR_LNKSTS)) {
		/* wait for a maximum of 10 seconds */
		if (hmep->hme_delay < 100) {
			hmep->hme_delay++;
			hme_start_timer(hmep, hme_try_speed, HME_TICKS);
			return (hmep->hme_autoneg = HME_HWAN_INPROGRESS);
		}
#ifdef HME_AUTONEG_DEBUG
		hmerror(hmep->hme_dip,
			"Link not Up in 10 seconds: stat = %X", stat);
#endif
		goto hme_anfail;
	} else {
		hmep->hme_bmsr |= (PHY_BMSR_LNKSTS);
		hme_get_autoinfo(hmep);
		hmep->hme_force_linkdown = HME_LINKDOWN_DONE;
		hme_setup_link_status(hmep);
		hme_start_mifpoll(hmep);
		hme_start_timer(hmep, hme_check_link, HME_LINKCHECK_TIMER);
		if (hmep->hme_fdx != hmep->hme_macfdx)
			(void) hmeinit(hmep);
		return (hmep->hme_autoneg = HME_HWAN_SUCCESFUL);
	}

hme_anfail:
#ifdef HME_AUTONEG_DEBUG
	hmerror(hmep->hme_dip, "Retry Auto-negotiation.");
#endif
	hme_start_timer(hmep, hme_try_speed, HME_TICKS);
	return (hmep->hme_autoneg = HME_HWAN_TRY);
}

/*
 * This function is used to perform automatic speed detection.
 * The Internal Transceiver which is based on the National PHY chip
 * 83840 supports auto-negotiation functionality.
 * Some External transceivers may not support auto-negotiation.
 * In that case, the software performs the speed detection.
 * The software tries to bring down the link for about 2 seconds to
 * force the Link Partner to notice speed change.
 * The software speed detection favors the 100 Mbps speed.
 * It does this by setting the 100 Mbps for longer duration ( 5 seconds )
 * than the 10 Mbps ( 2 seconds ). Also, even after the link is up
 * in 10 Mbps once, the 100 Mbps is also tried. Only if the link
 * is not up in 100 Mbps, the 10 Mbps speed is tried again.
 */

static void
hme_try_speed(hmep)
struct hme	*hmep;
{
	int		linkup;
	u_int		temp;
	u_short		csc;

#ifdef MPSAS
	hmep->hme_linkup = 1;
	return;
#endif
	hme_stop_timer(hmep);
	temp = hmep->hme_transceiver; /* save the transceiver type */
	hme_check_transceiver(hmep);
	if (temp != hmep->hme_transceiver) {
		if (hmep->hme_transceiver == HME_EXTERNAL_TRANSCEIVER)
			hmerror(hmep->hme_dip, "External Transceiver Selected");
		else
			hmerror(hmep->hme_dip, "Internal Transceiver Selected");
		hme_start_timer(hmep, hme_check_link, 10 * HME_TICKS);
		return;
	}

	if (hme_autoneg_enable && (hmep->hme_autoneg != HME_HWAN_FAILED)) {
		if (hme_try_auto_negotiation(hmep) != HME_HWAN_FAILED)
			return;	/* auto negotiation succesful or being tried  */
	}

/*
 * TBD: The following code should not be done for  External Transceivers.
 * It is currently required to support Electron 1.1 build machines which
 * do not allow auto-negotiation to be performed.
 */

	if (hmep->hme_force_linkdown == HME_FORCE_LINKDOWN) {
		hmep->hme_force_linkdown = HME_LINKDOWN_STARTED;
		(void) hme_mii_read(hmep, HME_PHY_CSC, &csc);
#ifdef HME_CSC_DEBUG
		hmerror(hmep->hme_dip, "csc = %X", csc);
#endif
		hme_mii_write(hmep, HME_PHY_CSC, (csc | PHY_CSCR_TXOFF));
		hme_mii_write(hmep, HME_PHY_BMCR, PHY_BMCR_100M);
		hme_start_timer(hmep, hme_try_speed, 20 * HME_TICKS);
		return;
	} else if (hmep->hme_force_linkdown == HME_LINKDOWN_STARTED) {
		(void) hme_mii_read(hmep, HME_PHY_CSC, &csc);
		hme_mii_write(hmep, HME_PHY_CSC, (csc & ~PHY_CSCR_TXOFF));
		hmep->hme_force_linkdown = HME_LINKDOWN_DONE;
	}

	linkup = hme_select_speed(hmep, hmep->hme_tryspeed);
	if (hmep->hme_linkup_cnt == 1) {
		hme_start_timer(hmep, hme_try_speed, SECOND(1));
		return;
	}
	if (linkup) {
		switch (hmep->hme_tryspeed) {
		case HME_SPEED_100:
			if (hmep->hme_linkup_cnt == 4) {
				hmep->hme_ntries = hmep->hme_nlasttries =
								HME_NTRIES_LOW;
				hmep->hme_linkup = 1;
				hmep->hme_linkcheck = 1;
				hme_display_transceiver(hmep);
				hme_display_msg(hmep, hmep->hme_dip,
							"100 Mbps Link Up");
				hme_setup_link_status(hmep);
				hme_start_mifpoll(hmep);
				hme_start_timer(hmep, hme_check_link,
							HME_LINKCHECK_TIMER);
				if (hmep->hme_fdx != hmep->hme_macfdx)
					(void) hmeinit(hmep);
			} else
				hme_start_timer(hmep, hme_try_speed, HME_TICKS);
			break;
		case HME_SPEED_10:
			if (hmep->hme_linkup_cnt == 4) {
				if (hmep->hme_linkup_10) {
					hmep->hme_linkup_10 = 0;
					hmep->hme_ntries = HME_NTRIES_LOW;
					hmep->hme_nlasttries = HME_NTRIES_LOW;
					hmep->hme_linkup = 1;
					hmep->hme_linkcheck = 1;
					hme_display_transceiver(hmep);
					hme_display_msg(hmep, hmep->hme_dip,
							"10 Mbps Link Up");
					hme_setup_link_status(hmep);
					hme_start_mifpoll(hmep);
					hme_start_timer(hmep, hme_check_link,
							HME_LINKCHECK_TIMER);
					if (hmep->hme_fdx != hmep->hme_macfdx)
						(void) hmeinit(hmep);
				} else {
					hmep->hme_linkup_10 = 1;
					hmep->hme_tryspeed = HME_SPEED_100;
					hmep->hme_force_linkdown =
							HME_FORCE_LINKDOWN;
					hmep->hme_linkup_cnt = 0;
					hmep->hme_ntries = HME_NTRIES_LOW;
					hmep->hme_nlasttries = HME_NTRIES_LOW;
					hme_start_timer(hmep,
						hme_try_speed, HME_TICKS);
				}

			} else
				hme_start_timer(hmep, hme_try_speed, HME_TICKS);
			break;
		default:
			break;
		}
		return;
	}

	hmep->hme_ntries--;
	hmep->hme_linkup_cnt = 0;
	if (hmep->hme_ntries == 0) {
		hmep->hme_force_linkdown = HME_FORCE_LINKDOWN;
		switch (hmep->hme_tryspeed) {
		case HME_SPEED_100:
			hmep->hme_tryspeed = HME_SPEED_10;
			hmep->hme_ntries = HME_NTRIES_LOW_10;
			break;
		case HME_SPEED_10:
			hmep->hme_ntries = HME_NTRIES_LOW;
			hmep->hme_tryspeed = HME_SPEED_100;
			break;
		default:
			break;
		}
	}
	hme_start_timer(hmep, hme_try_speed, HME_TICKS);
}

/* <<<<<<<<<<<<<<<<<<<<<<<<<<<  LOADABLE ENTRIES  >>>>>>>>>>>>>>>>>>>>>>> */

int
_init(void)
{
	int	status;

	mutex_init(&hmeautolock,
			"hme autoconfig lock", MUTEX_DRIVER, NULL);

	status = mod_install(&modlinkage);
	if (status != 0)
		mutex_destroy(&hmeautolock);
	return (status);
}

int
_fini(void)
{
	int	status;

	status = mod_remove(&modlinkage);
	if (status != 0)
		return (status);

	mutex_destroy(&hmelock);
	rw_destroy(&hmestruplock);
	mutex_destroy(&hmeautolock);
	return (0);
}


int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}



#define	HMERINDEX(i)		(i % HMERPENDING)

#define	DONT_FLUSH		-1

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
#define	HMEBUFIOADDR(hmep, a) \
	((hmep)->hme_bufiobase + ((u_long)(a) - (hmep)->hme_bufkbase))
#define	HMEIOPBIOADDR(hmep, a) \
	((hmep)->hme_iopbiobase + ((u_long)(a) - (hmep)->hme_iopbkbase))

/*
 * XXX
 * Define HMESYNCIOPB to nothing for now.
 * If/when we have PSO-mode kernels running which really need
 * to sync something during a ddi_dma_sync() of iopb-allocated memory,
 * then this can go back in, but for now we take it out
 * to save some microseconds.
 */
#define	HMESYNCIOPB(hmep, a, size, who)

#ifdef notdef
/*
 * ddi_dma_sync() a TMD or RMD descriptor.
 */
#define	HMESYNCIOPB(hmep, a, size, who) \
	(void) ddi_dma_sync((hmep)->hme_iopbhandle, \
		((u_long)(a) - (hmep)->hme_iopbkbase), \
		(size), \
		(who))
#endif

#define	HMESAPMATCH(sap, type, flags) ((sap == type)? 1 : \
	((flags & HMESALLSAP)? 1 : \
	((sap <= ETHERMTU) && (sap > 0) && (type <= ETHERMTU))? 1 : 0))

/*
 * Ethernet broadcast address definition.
 */
static	struct ether_addr	etherbroadcastaddr = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/*
 * Linked list of hme structures - one per card.
 */
static struct hme *hmeup = NULL;

/*
 * force the fallback to ddi_dma routines
 */
#ifdef MPSAS
static int hme_force_dma = 1;
#else
static int hme_force_dma = 0;
#endif

/*
 * Our DL_INFO_ACK template.
 */
static	dl_info_ack_t hmeinfoack = {
	DL_INFO_ACK,				/* dl_primitive */
	ETHERMTU,				/* dl_max_sdu */
	0,					/* dl_min_sdu */
	HMEADDRL,				/* dl_addr_length */
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
	sizeof (dl_info_ack_t) + HMEADDRL,	/* dl_brdcst_addr_offset */
	0					/* dl_growth */
};

/*
 * Identify device.
 */
static int
hmeidentify(dev_info_t	*dip)
{
#ifdef HMEDEBUG
	if (hmedebug) {
		hmerror(dip, "hmeidentify:  Entered");
	}
#endif
	if (strcmp(ddi_get_name(dip), "hme") == 0)
		return (DDI_IDENTIFIED);
	else if (strcmp(ddi_get_name(dip), "SUNW,hme") == 0)
		return (DDI_IDENTIFIED);
	return (DDI_NOT_IDENTIFIED);
}

/*
 * Interface exists: make available by filling in network interface
 * record.  System will initialize the interface when it is ready
 * to accept packets.
 */
static int
hmeattach(dip, cmd)
dev_info_t	*dip;
ddi_attach_cmd_t	cmd;
{
	register struct hme *hmep;
	static	once = 1;
	int regno;
	ulong_t timestamp;
	int	hm_rev = 0;
	int	prop_len = sizeof (int);
	int i;
	int	hme_ipg1_conf, hme_ipg2_conf;
	int	hme_use_int_xcvr_conf, hme_pace_count_conf;
	int	hme_autoneg_conf;
	int	hme_anar_100fdx_conf, hme_anar_100hdx_conf;
	int	hme_anar_10fdx_conf, hme_anar_10hdx_conf;
	int	hme_ipg0_conf, hme_lance_mode_conf;

#ifdef HMEDEBUG
	if (hmedebug)
		hmerror(dip, "hmeattach:  Entered");
#endif

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		if ((hmep = (struct hme *) ddi_get_driver_private(dip)) == NULL)
			return (DDI_FAILURE);

		hmep->hme_flags &= ~HMESUSPENDED;
		(void) hmeinit(hmep);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	hmep = (struct hme *)NULL;

	/*
	 * Allocate soft device data structure
	 */
	if ((hmep = GETSTRUCT(struct hme, 1)) == NULL) {
		hmerror(dip, "hmeattach:  kmem_alloc hme failed");
		return (DDI_FAILURE);
	}

	/*
	 * Map in the device registers.
	 *
	 * Reg # 0 is the Global register set
	 * Reg # 1 is the ETX register set
	 * Reg # 2 is the ERX register set
	 * Reg # 3 is the BigMAC register set.
	 * Reg # 4 is the MIF register set
	 */
	if (ddi_dev_nregs(dip, &regno) != (DDI_SUCCESS) || (regno != 5)) {
		hmerror(dip, "ddi_dev_nregs failed, returned %d",
							(char *)regno);
		goto bad;
	}
	if (ddi_map_regs(dip, 0, (caddr_t *)&(hmep->hme_globregp), 0, 0)) {
		hmerror(dip, "ddi_map_regs for hme global reg failed");
		goto bad;
	}
	if (ddi_map_regs(dip, 1, (caddr_t *)&(hmep->hme_etxregp), 0, 0)) {
		hmerror(dip, "ddi_map_regs for hme etx reg failed");
		goto bad;
	}
	if (ddi_map_regs(dip, 2, (caddr_t *)&(hmep->hme_erxregp), 0, 0)) {
		hmerror(dip, "ddi_map_regs for hme erx reg failed");
		goto bad;
	}
	if (ddi_map_regs(dip, 3, (caddr_t *)&(hmep->hme_bmacregp), 0, 0)) {
		hmerror(dip, "ddi_map_regs for hme bmac reg failed");
		goto bad;
	}

	if (ddi_map_regs(dip, 4, (caddr_t *)&(hmep->hme_mifregp), 0, 0)) {
		hmerror(dip, "ddi_map_regs for hme transceiver reg failed");
		goto bad;
	}

	/*
	 * Based on the hm-rev, set some capabilities
	 * Set up default capabilities for HM 2.0
	 */

	hmep->hme_mifpoll_enable = 0;
	hmep->hme_frame_enable = 0;
	hmep->hme_lance_mode_enable = 0;
	hmep->hme_rxcv_enable = 0;

	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "hm-rev",
				(caddr_t)&hm_rev, &prop_len)
				== DDI_PROP_SUCCESS) {
		hmep->hme_asicrev = hm_rev;
		if ((hm_rev == HME_2P1_REVID) ||
				(hm_rev == HME_2P1_REVID_OBP)) {
			hme_display_msg(hmep, dip,
					"FEPS 2.1 Found (Rev Id = %X)", hm_rev);
			hmep->hme_mifpoll_enable = 1;
			hmep->hme_frame_enable = 1;
		} else if (hm_rev == HME_2P0_REVID) {
			hme_display_msg(hmep, dip,
					"FEPS 2.0 Found (Rev Id = %X)", hm_rev);
		} else {
			hme_display_msg(hmep, dip,
					"FEPS (Rev Id = %X) Found", hm_rev);
			hmep->hme_mifpoll_enable = 1;
			hmep->hme_frame_enable = 1;
			hmep->hme_lance_mode_enable = 1;
			hmep->hme_rxcv_enable = 1;
		}
	} else {
		hme_display_msg(hmep, dip,
				"ddi_getlongprop_buf() for hm-rev failed");
		hme_display_msg(hmep, dip, "Treat this as HM 2.0");
	}
	if (!hme_mifpoll_enable)
		hmep->hme_mifpoll_enable = 0;

	hmep->hme_dip = dip;

	/*
	 * Add interrupt to system
	 */
	if (ddi_add_intr(dip, 0, &hmep->hme_cookie, 0, hmeintr,
							(caddr_t)hmep)) {
		hmerror(dip, "ddi_add_intr failed");
		goto bad;
	}

	/*
	 * At this point, we are *really* here.
	 */
	ddi_set_driver_private(dip, (caddr_t)hmep);

	/*
	 * Initialize mutex's for this device.
	 */
	mutex_init(&hmep->hme_xmitlock, "hme xmit lock", MUTEX_DRIVER,
		(void *)hmep->hme_cookie);
	mutex_init(&hmep->hme_intrlock, "hme intr lock", MUTEX_DRIVER,
		(void *)hmep->hme_cookie);
	mutex_init(&hmep->hme_linklock, "hme link lock", MUTEX_DRIVER,
		(void *)hmep->hme_cookie);

	/*
	 * Get the local ethernet address.
	 */
	localetheraddr((struct ether_addr *)NULL, &hmep->hme_ouraddr);

	/*
	 * Create the filesystem device node.
	 */
	if (ddi_create_minor_node(dip, "hme", S_IFCHR,
		ddi_get_instance(dip), DDI_NT_NET, CLONE_DEV) == DDI_FAILURE) {
		hmerror(dip, "ddi_create_minor_node failed");
		mutex_destroy(&hmep->hme_xmitlock);
		mutex_destroy(&hmep->hme_intrlock);
		mutex_destroy(&hmep->hme_linklock);
		goto bad;
	}

	/*
	 * Create power management properties.
	 */
	drv_getparm(TIME, &timestamp);

	if (ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
		"pm_timestamp", (caddr_t) &timestamp,
		sizeof (timestamp)) != DDI_SUCCESS)
		hmerror(dip, "hmeattach:  ddi_prop_create error");

	timestamp = 1;

	if (ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
		"pm_norm_pwr", (caddr_t) &timestamp,
		sizeof (timestamp)) != DDI_SUCCESS)
		hmerror(dip, "hmeattach:  ddi_prop_create error");

	mutex_enter(&hmeautolock);
	if (once) {
		once = 0;
		rw_init(&hmestruplock, "hme streams linked list lock",
			RW_DRIVER, (void *)hmep->hme_cookie);
		mutex_init(&hmelock, "hme global lock", MUTEX_DRIVER,
			(void *)hmep->hme_cookie);
	}
	mutex_exit(&hmeautolock);


	for (i = 0; i < A_CNT(hme_param_arr); i++)
		hmep->hme_param_arr[i] = hme_param_arr[i];
	if (!hmep->hme_g_nd && !hme_param_register(hmep, hmep->hme_param_arr,
		A_CNT(hme_param_arr))) {
		hmerror(dip, "hmeattach:  hme_param_register error");
		goto bad;
	}
	hme_param_device = ddi_get_instance(hmep->hme_dip);

	/*
	 * Set up the start-up values for user-configurable parameters
	 * Get the values from the global variables first.
	 */
	hme_param_ipg1 = hme_ipg1;
	hme_param_ipg2 = hme_ipg2;
	hme_param_use_intphy = hme_use_int_xcvr;
	hme_param_pace_count = hme_pace_size;
	hme_param_autoneg = hme_adv_autoneg_cap;
	hme_param_anar_100fdx = hme_adv_100fdx_cap;
	hme_param_anar_100hdx = hme_adv_100hdx_cap;
	hme_param_anar_10fdx = hme_adv_10fdx_cap;
	hme_param_anar_10hdx = hme_adv_10hdx_cap;
	hme_param_ipg0 = hme_ipg0;
	hme_param_lance_mode = hme_lance_mode;


	/*
	 * Get the parameter values configured in .conf file.
	 */
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "ipg1",
				(caddr_t)&hme_ipg1_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
#ifdef HME_CONF_DEBUG
		hmerror(dip,
			"hmeattach: hme_ipg1 property = %X", hme_ipg1_conf);
#endif
		hme_param_ipg1 = hme_ipg1_conf;
	}
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "ipg2",
				(caddr_t)&hme_ipg2_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
		hme_param_ipg2 = hme_ipg2_conf;
	}
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "use_int_xcvr",
				(caddr_t)&hme_use_int_xcvr_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
		hme_param_use_intphy = hme_use_int_xcvr_conf;
	}
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "pace_size",
				(caddr_t)&hme_pace_count_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
		hme_param_pace_count = hme_pace_count_conf;
	}
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "adv_autoneg_cap",
				(caddr_t)&hme_autoneg_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
		hme_param_autoneg = hme_autoneg_conf;
	}
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "adv_100fdx_cap",
				(caddr_t)&hme_anar_100fdx_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
		hme_param_anar_100fdx = hme_anar_100fdx_conf;
	}
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "adv_100hdx_cap",
				(caddr_t)&hme_anar_100hdx_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
		hme_param_anar_100hdx = hme_anar_100hdx_conf;
	}
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "adv_10fdx_cap",
				(caddr_t)&hme_anar_10fdx_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
		hme_param_anar_10fdx = hme_anar_10fdx_conf;
	}
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "adv_10hdx_cap",
				(caddr_t)&hme_anar_10hdx_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
		hme_param_anar_10hdx = hme_anar_10hdx_conf;
	}
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "ipg0",
				(caddr_t)&hme_ipg0_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
		hme_param_ipg0 = hme_ipg0_conf;
	}
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "lance_mode",
				(caddr_t)&hme_lance_mode_conf, &prop_len)
				== DDI_PROP_SUCCESS) {
		hme_param_lance_mode = hme_lance_mode_conf;
	}

	/* lock hme structure while manipulating link list of hme structs */
	mutex_enter(&hmelock);
	hmep->hme_nextp = hmeup;
	hmeup = hmep;
	mutex_exit(&hmelock);

#ifndef	MPSAS
	hmestatinit(hmep);
#endif	MPSAS
	ddi_report_dev(dip);
	return (DDI_SUCCESS);

bad:
	if (hmep->hme_cookie)
		ddi_remove_intr(dip, 0, hmep->hme_cookie);
	if (hmep->hme_globregp)
		ddi_unmap_regs(dip, 0, (caddr_t *)&(hmep->hme_globregp), 0, 0);
	if (hmep->hme_etxregp)
		ddi_unmap_regs(dip, 1, (caddr_t *)&(hmep->hme_etxregp), 0, 0);
	if (hmep->hme_erxregp)
		ddi_unmap_regs(dip, 2, (caddr_t *)&(hmep->hme_erxregp), 0, 0);
	if (hmep->hme_bmacregp)
		ddi_unmap_regs(dip, 3, (caddr_t *)&(hmep->hme_bmacregp), 0, 0);
	if (hmep->hme_mifregp)
		ddi_unmap_regs(dip, 4, (caddr_t *)&(hmep->hme_mifregp), 0, 0);
	if (hmep)
		kmem_free((caddr_t)hmep, sizeof (*hmep));

	if (hmedebug)
		hmerror(dip, "hmeattach:  Unsuccesful Exiting ");
	return (DDI_FAILURE);
}

static int
hmedetach(dip, cmd)
dev_info_t	*dip;
ddi_detach_cmd_t	cmd;
{
	register struct hme 		*hmep, *hmetmp, **prevhmep;

	switch (cmd) {
	case DDI_DETACH:
		break;

	case DDI_SUSPEND:
	case DDI_PM_SUSPEND:
		if ((hmep = (struct hme *)ddi_get_driver_private(dip)) == NULL)
			return (DDI_FAILURE);

		hmep->hme_flags |= HMESUSPENDED;
		hmeuninit(hmep);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	hmep = (struct hme *)ddi_get_driver_private(dip);

	if (!hmep)			/* No resources allocated */
		return (DDI_SUCCESS);

	/*
	 * Make driver quiescent
	 */
	if (hmetxreset(hmep)) {
		hmerror(hmep->hme_dip, "txmac did not reset");
	}
	if (hmerxreset(hmep))
		hmerror(hmep->hme_dip, "rxmac did not reset");
/* sampath : stop the chip */
	(void) hmestop(hmep);

	ddi_remove_minor_node(dip, NULL);

	/*
	 * Remove instance of the intr
	 */
	ddi_remove_intr(dip, 0, hmep->hme_cookie);

	/*
	 * Destroy all mutexes and data structures allocated during
	 * attach time.
	 */
	if (hmep->hme_globregp)
		(void) ddi_unmap_regs(dip, 0,
				(caddr_t *)&(hmep->hme_globregp), 0, 0);
	if (hmep->hme_etxregp)
		(void) ddi_unmap_regs(dip, 1,
				(caddr_t *)&(hmep->hme_etxregp), 0, 0);
	if (hmep->hme_erxregp)
		(void) ddi_unmap_regs(dip, 2,
				(caddr_t *)&(hmep->hme_erxregp), 0, 0);
	if (hmep->hme_bmacregp)
		(void) ddi_unmap_regs(dip, 3,
				(caddr_t *)&(hmep->hme_bmacregp), 0, 0);
	if (hmep->hme_mifregp)
		(void) ddi_unmap_regs(dip, 4,
				(caddr_t *)&(hmep->hme_mifregp), 0, 0);

	/*
	 * Remove hmep from the link list of device structures
	 */

	for (prevhmep = &hmeup; (hmetmp = *prevhmep) != NULL;
		prevhmep = &hmetmp->hme_nextp)
		if (hmetmp == hmep) {
#ifndef	MPSAS
			kstat_delete(hmetmp->hme_ksp);
#endif	MPSAS
			*prevhmep = hmetmp->hme_nextp;
			hme_stop_timer(hmetmp);
			mutex_exit(&hmep->hme_linklock);
			mutex_destroy(&hmetmp->hme_xmitlock);
			mutex_destroy(&hmetmp->hme_intrlock);
			mutex_destroy(&hmetmp->hme_linklock);
			if (hmetmp->hme_iopbhandle)
				ddi_dma_free(hmetmp->hme_iopbhandle);
			if (hmetmp->hme_iopbkbase)
				ddi_iopb_free((caddr_t)hmetmp->hme_iopbkbase);

			hmefreebufs(hmetmp);

			if (hmetmp->hme_dvmarh) {
				(void) dvma_release(hmetmp->hme_dvmarh);
				(void) dvma_release(hmetmp->hme_dvmaxh);
				hmetmp->hme_dvmarh = hmetmp->hme_dvmaxh = NULL;
			}
			if (hmetmp->hme_dmarh) {
				kmem_free((caddr_t)hmetmp->hme_dmaxh,
				    (HME_TMDMAX + HMERPENDING) *
				    (sizeof (ddi_dma_handle_t)));
				hmetmp->hme_dmarh = hmetmp->hme_dmaxh = NULL;
			}

			hme_param_cleanup(hmetmp);
			kmem_free((caddr_t)hmetmp, sizeof (struct hme));
			break;
		}
	return (DDI_SUCCESS);
}

/*
 * Return 0 upon success, 1 on failure.
 */
static int
hmestop(hmep)
struct	hme	*hmep;
{
	volatile u_int *gresetrp = &(hmep->hme_globregp->reset);

	*gresetrp = HMEG_RESET_GLOBAL;
#ifdef MPSAS
	*gresetrp = HMEG_RESET_GLOBAL;
	*gresetrp = HMEG_RESET_GLOBAL;
#endif MPSAS
	HMEDELAY((*gresetrp == 0), HMEMAXRSTDELAY);
	if (*gresetrp) {
		hmerror(hmep->hme_dip, "cannot stop hme");
		return (1);
	} else
		return (0);
}

#ifndef	MPSAS
static int
hmestat_kstat_update(kstat_t *ksp, int rw)
{
	struct hme *hmep;
	struct hmekstat *hkp;

	hmep = (struct hme *) ksp->ks_private;
	hkp = (struct hmekstat *) ksp->ks_data;

	/*
	 * Update all the stats by reading all the counter registers.
	 * Counter register stats are not updated till they overflow
	 * and interrupt.
	 */
	hmesavecntrs(hmep);

	if (rw == KSTAT_WRITE) {
#ifdef	kstat
		hmep->hme_ipackets	= hkp->hk_ipackets.value.ul;
		hmep->hme_ierrors		= hkp->hk_ierrors.value.ul;
		hmep->hme_opackets	= hkp->hk_opackets.value.ul;
		hmep->hme_oerrors		= hkp->hk_oerrors.value.ul;
		hmep->hme_coll		= hkp->hk_collisions.value.ul;
		hmep->hme_fram		= hkp->hk_fram.value.ul;
		hmep->hme_crc		= hkp->hk_crc.value.ul;
		hmep->hme_sqerr		= hkp->hk_sqerr.value.ul;
		hmep->hme_cvc		= hkp->hk_cvc.value.ul;
		hmep->hme_lenerr	= hkp->hk_lenerr.value.ul;
		hmep->hme_buff		= hkp->hk_buff.value.ul;
		hmep->hme_oflo		= hkp->hk_oflo.value.ul;
		hmep->hme_uflo		= hkp->hk_uflo.value.ul;
		hmep->hme_missed		= hkp->hk_missed.value.ul;
		hmep->hme_tlcol		= hkp->hk_tlcol.value.ul;
		hmep->hme_trtry		= hkp->hk_trtry.value.ul;
		hmep->hme_fstcol		= hkp->hk_fstcol.value.ul;
		hmep->hme_tnocar		= hkp->hk_tnocar.value.ul;
		hmep->hme_inits		= hkp->hk_inits.value.ul;
		hmep->hme_nocanput	= hkp->hk_nocanput.value.ul;
		hmep->hme_allocbfail	= hkp->hk_allocbfail.value.ul;
		hmep->hme_runt		= hkp->hk_runt.value.ul;
		hmep->hme_jab		= hkp->hk_jab.value.ul;
		hmep->hme_babl		= hkp->hk_babl.value.ul;
		hmep->hme_tmder 		= hkp->hk_tmder.value.ul;
		hmep->hme_txlaterr		= hkp->hk_txlaterr.value.ul;
		hmep->hme_rxlaterr		= hkp->hk_rxlaterr.value.ul;
		hmep->hme_slvparerr		= hkp->hk_slvparerr.value.ul;
		hmep->hme_txparerr		= hkp->hk_txparerr.value.ul;
		hmep->hme_rxparerr		= hkp->hk_rxparerr.value.ul;
		hmep->hme_slverrack		= hkp->hk_slverrack.value.ul;
		hmep->hme_txerrack		= hkp->hk_txerrack.value.ul;
		hmep->hme_rxerrack		= hkp->hk_rxerrack.value.ul;
		hmep->hme_txtagerr		= hkp->hk_txtagerr.value.ul;
		hmep->hme_rxtagerr		= hkp->hk_rxtagerr.value.ul;
		hmep->hme_eoperr		= hkp->hk_eoperr.value.ul;
		hmep->hme_notmds		= hkp->hk_notmds.value.ul;
		hmep->hme_notbufs		= hkp->hk_notbufs.value.ul;
		hmep->hme_norbufs		= hkp->hk_norbufs.value.ul;
		hmep->hme_clsn		= hkp->hk_clsn.value.ul;
#else
		return (EACCES);
#endif	kstat
	} else {
		hkp->hk_ipackets.value.ul	= hmep->hme_ipackets;
		hkp->hk_ierrors.value.ul	= hmep->hme_ierrors;
		hkp->hk_opackets.value.ul	= hmep->hme_opackets;
		hkp->hk_oerrors.value.ul	= hmep->hme_oerrors;
		hkp->hk_coll.value.ul		= hmep->hme_coll;
		hkp->hk_fram.value.ul		= hmep->hme_fram;
		hkp->hk_crc.value.ul		= hmep->hme_crc;
		hkp->hk_sqerr.value.ul		= hmep->hme_sqerr;
		hkp->hk_cvc.value.ul		= hmep->hme_cvc;
		hkp->hk_lenerr.value.ul		= hmep->hme_lenerr;
		hkp->hk_buff.value.ul		= hmep->hme_buff;
		hkp->hk_oflo.value.ul		= hmep->hme_oflo;
		hkp->hk_uflo.value.ul		= hmep->hme_uflo;
		hkp->hk_missed.value.ul		= hmep->hme_missed;
		hkp->hk_tlcol.value.ul		= hmep->hme_tlcol;
		hkp->hk_trtry.value.ul		= hmep->hme_trtry;
		hkp->hk_fstcol.value.ul		= hmep->hme_fstcol;
		hkp->hk_tnocar.value.ul		= hmep->hme_tnocar;
		hkp->hk_inits.value.ul		= hmep->hme_inits;
		hkp->hk_nocanput.value.ul	= hmep->hme_nocanput;
		hkp->hk_allocbfail.value.ul	= hmep->hme_allocbfail;
		hkp->hk_runt.value.ul		= hmep->hme_runt;
		hkp->hk_jab.value.ul		= hmep->hme_jab;
		hkp->hk_babl.value.ul		= hmep->hme_babl;
		hkp->hk_tmder.value.ul		= hmep->hme_tmder;
		hkp->hk_txlaterr.value.ul	= hmep->hme_txlaterr;
		hkp->hk_rxlaterr.value.ul	= hmep->hme_rxlaterr;
		hkp->hk_slvparerr.value.ul	= hmep->hme_slvparerr;
		hkp->hk_txparerr.value.ul	= hmep->hme_txparerr;
		hkp->hk_rxparerr.value.ul	= hmep->hme_rxparerr;
		hkp->hk_slverrack.value.ul	= hmep->hme_slverrack;
		hkp->hk_txerrack.value.ul	= hmep->hme_txerrack;
		hkp->hk_rxerrack.value.ul	= hmep->hme_rxerrack;
		hkp->hk_txtagerr.value.ul	= hmep->hme_txtagerr;
		hkp->hk_rxtagerr.value.ul	= hmep->hme_rxtagerr;
		hkp->hk_eoperr.value.ul		= hmep->hme_eoperr;
		hkp->hk_notmds.value.ul		= hmep->hme_notmds;
		hkp->hk_notbufs.value.ul	= hmep->hme_notbufs;
		hkp->hk_norbufs.value.ul	= hmep->hme_norbufs;
		hkp->hk_clsn.value.ul		= hmep->hme_clsn;
	}
	return (0);
}
static void
hmestatinit(hmep)
struct	hme	*hmep;
{
	struct	kstat	*ksp;
	struct	hmekstat	*hkp;

#ifdef	kstat
	if ((ksp = kstat_create("hme", ddi_get_instance(hmep->hme_dip),
		NULL, "net", KSTAT_TYPE_NAMED,
		sizeof (struct hmekstat) / sizeof (kstat_named_t),
		KSTAT_FLAG_PERSISTENT)) == NULL) {
#else
	if ((ksp = kstat_create("hme", ddi_get_instance(hmep->hme_dip),
	    NULL, "net", KSTAT_TYPE_NAMED,
	    sizeof (struct hmekstat) / sizeof (kstat_named_t), 0)) == NULL) {
#endif	kstat
		hmerror(hmep->hme_dip, "kstat_create failed");
		return;
	}

	hmep->hme_ksp = ksp;
	hkp = (struct hmekstat *) (ksp->ks_data);
	kstat_named_init(&hkp->hk_ipackets,		"ipackets",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_ierrors,		"ierrors",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_opackets,		"opackets",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_oerrors,		"oerrors",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_coll,			"collisions",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_fram,			"framing",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_crc,			"crc",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_sqerr,		"sqe",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_cvc,			"code_violations",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_lenerr,		"len_errors",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_buff,			"buff",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_oflo,			"oflo",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_uflo,			"uflo",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_missed,		"missed",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_tlcol,		"tx_late_collisions",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_trtry,		"retry_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_fstcol,		"first_collisions",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_tnocar,		"nocarrier",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_inits,		"inits",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_nocanput,		"nocanput",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_allocbfail,		"allocbfail",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_runt,			"runt",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_jab,			"jabber",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_babl,			"babble",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_tmder,		"tmd_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_txlaterr,		"tx_late_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_rxlaterr,		"rx_late_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_slvparerr,		"slv_parity_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_txparerr,		"tx_parity_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_rxparerr,		"rx_parity_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_slverrack,		"slv_error_ack",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_txerrack,		"tx_error_ack",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_rxerrack,		"rx_error_ack",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_txtagerr,		"tx_tag_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_rxtagerr,		"rx_tag_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_eoperr,		"eop_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_notmds,		"no_tmds",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_notbufs,		"no_tbufs",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_norbufs,		"no_rbufs",
		KSTAT_DATA_ULONG);
	kstat_named_init(&hkp->hk_clsn,			"rx_late_collisions",
		KSTAT_DATA_ULONG);
	ksp->ks_update = hmestat_kstat_update;
	ksp->ks_private = (void *) hmep;
	kstat_install(ksp);
}
#endif	MPSAS

/*
 * Translate "dev_t" to a pointer to the associated "dev_info_t".
 */
/* ARGSUSED */
static int
hmeinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	dev_t	dev = (dev_t) arg;
	int	instance, rc;
	struct	hmestr	*sbp;

	instance = getminor(dev);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		rw_enter(&hmestruplock, RW_READER);
		dip = NULL;
		for (sbp = hmestrup; sbp; sbp = sbp->sb_nextp)
			if (sbp->sb_minor == instance)
				break;
		if (sbp && sbp->sb_hmep)
			dip = sbp->sb_hmep->hme_dip;
		rw_exit(&hmestruplock);

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
 * Assorted DLPI V2 routines.
 */
/*ARGSUSED*/
static
hmeopen(rq, devp, flag, sflag, credp)
queue_t	*rq;
dev_t	*devp;
int	flag;
int	sflag;
cred_t	*credp;
{
	register	struct	hmestr	*sbp;
	register	struct	hmestr	**prevsbp;
	int	minordev;
	int	rc = 0;

	ASSERT(sflag != MODOPEN);
	TRACE_1(TR_FAC_BE, TR_BE_OPEN, "hmeopen:  rq %X", rq);

	/*
	 * Serialize all driver open and closes.
	 */
	rw_enter(&hmestruplock, RW_WRITER);

	/*
	 * Determine minor device number.
	 */
	prevsbp = &hmestrup;
	if (sflag == CLONEOPEN) {
		minordev = 0;
		for (; (sbp = *prevsbp) != NULL; prevsbp = &sbp->sb_nextp) {
			if (minordev < sbp->sb_minor)
				break;
			minordev++;
		}
		*devp = makedevice(getmajor(*devp), minordev);
	} else
		minordev = getminor(*devp);

	if (rq->q_ptr)
		goto done;

	if ((sbp = GETSTRUCT(struct hmestr, 1)) == NULL) {
		rc = ENOMEM;
		goto done;
	}

#ifdef HME_BOOT_DEBUG
	cmn_err(CE_CONT, "hmeopen: sbp = %X\n", sbp);
#endif

	sbp->sb_minor = minordev;
	sbp->sb_rq = rq;
	sbp->sb_state = DL_UNATTACHED;
	sbp->sb_sap = 0;
	sbp->sb_flags = 0;
	sbp->sb_hmep = NULL;
	sbp->sb_mccount = 0;
	sbp->sb_mctab = NULL;
	mutex_init(&sbp->sb_lock, "hme stream lock", MUTEX_DRIVER, (void *)0);

	/*
	 * Link new entry into the list of active entries.
	 */
	sbp->sb_nextp = *prevsbp;
	*prevsbp = sbp;

	rq->q_ptr = WR(rq)->q_ptr = (char *) sbp;

	/*
	 * Disable automatic enabling of our write service procedure.
	 * We control this explicitly.
	 */
	noenable(WR(rq));

done:
	rw_exit(&hmestruplock);
	qprocson(rq);
	return (rc);
}

static
hmeclose(rq)
queue_t	*rq;
{
	register	struct	hmestr	*sbp;
	register	struct	hmestr	**prevsbp;

	TRACE_1(TR_FAC_BE, TR_BE_CLOSE, "hmeclose:  rq %X", rq);
	ASSERT(rq->q_ptr);

	qprocsoff(rq);

	sbp = (struct hmestr *) rq->q_ptr;

	/*
	 * Implicit detach Stream from interface.
	 */
	if (sbp->sb_hmep)
		hmedodetach(sbp);

	rw_enter(&hmestruplock, RW_WRITER);

	/*
	 * Unlink the per-Stream entry from the active list and free it.
	 */
	for (prevsbp = &hmestrup; (sbp = *prevsbp) != NULL;
		prevsbp = &sbp->sb_nextp)
		if (sbp == (struct hmestr *) rq->q_ptr)
			break;
	ASSERT(sbp);
	*prevsbp = sbp->sb_nextp;

	mutex_destroy(&sbp->sb_lock);
	kmem_free((char *) sbp, sizeof (struct hmestr));

	rq->q_ptr = WR(rq)->q_ptr = NULL;

	rw_exit(&hmestruplock);
	return (0);
}

static
hmewput(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	register	struct	hmestr	*sbp = (struct hmestr *) wq->q_ptr;
	struct	hme	*hmep;

	TRACE_2(TR_FAC_BE, TR_BE_WPUT_START,
		"hmewput start:  wq %X db_type %o", wq, DB_TYPE(mp));

	switch (DB_TYPE(mp)) {
		case M_DATA:		/* "fastpath" */
			hmep = sbp->sb_hmep;
			if ((sbp->sb_flags & (HMESFAST|HMESRAW) == 0) ||
				(sbp->sb_state != DL_IDLE) ||
				(hmep == NULL)) {
				merror(wq, mp, EPROTO);
				break;
			}

			/*
			 * If any msgs already enqueued or the interface will
			 * loop back up the message (due to HMEPROMISC), then
			 * enqueue the msg.  Otherwise just xmit it directly.
			 */
			if (wq->q_first) {
				(void) putq(wq, mp);
				hmep->hme_wantw = 1;
				qenable(wq);
			} else if (hmep->hme_flags & HMEPROMISC) {
				(void) putq(wq, mp);
				qenable(wq);
			} else
				(void) hmestart(wq, mp, hmep);

			break;

		case M_PROTO:
		case M_PCPROTO:
			/*
			 * Break the association between the current thread and
			 * the thread that calls hmeproto() to resolve the
			 * problem of hmeintr() threads which loop back around
			 * to call hmeproto and try to recursively acquire
			 * internal locks.
			 */
			(void) putq(wq, mp);
			qenable(wq);
			break;

		case M_IOCTL:
			hmeioctl(wq, mp);
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
	TRACE_1(TR_FAC_BE, TR_BE_WPUT_END, "hmewput end:  wq %X", wq);
	return (0);
}

/*
 * Enqueue M_PROTO/M_PCPROTO (always) and M_DATA (sometimes) on the wq.
 *
 * Processing of some of the M_PROTO/M_PCPROTO msgs involves acquiring
 * internal locks that are held across upstream putnext calls.
 * Specifically there's the problem of hmeintr() holding hme_intrlock
 * and hmestruplock when it calls putnext() and that thread looping
 * back around to call hmewput and, eventually, hmeinit() to create a
 * recursive lock panic.  There are two obvious ways of solving this
 * problem: (1) have hmeintr() do putq instead of putnext which provides
 * the loopback "cutout" right at the rq, or (2) allow hmeintr() to putnext
 * and put the loopback "cutout" around hmeproto().  We choose the latter
 * for performance reasons.
 *
 * M_DATA messages are enqueued on the wq *only* when the xmit side
 * is out of tbufs or tmds.  Once the xmit resource is available again,
 * wsrv() is enabled and tries to xmit all the messages on the wq.
 */
static
hmewsrv(wq)
queue_t	*wq;
{
	mblk_t	*mp;
	struct	hmestr	*sbp;
	struct	hme	*hmep;

	TRACE_1(TR_FAC_BE, TR_BE_WSRV_START, "hmewsrv start:  wq %X", wq);

	sbp = (struct hmestr *) wq->q_ptr;
	hmep = sbp->sb_hmep;

	while (mp = getq(wq))
		switch (DB_TYPE(mp)) {
			case	M_DATA:
				if (hmep) {
					if (hmestart(wq, mp, hmep))
						goto done;
				} else
					freemsg(mp);
				break;

			case	M_PROTO:
			case	M_PCPROTO:
				hmeproto(wq, mp);
				break;

			default:
				ASSERT(0);
				break;
		}

done:
	TRACE_1(TR_FAC_BE, TR_BE_WSRV_END, "hmewsrv end:  wq %X", wq);
	return (0);
}

static void
hmeproto(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	union	DL_primitives	*dlp;
	struct	hmestr	*sbp;
	u_long	prim;

	sbp = (struct hmestr *) wq->q_ptr;
	dlp = (union DL_primitives *) mp->b_rptr;
	prim = dlp->dl_primitive;

	TRACE_2(TR_FAC_BE, TR_BE_PROTO_START,
		"hmeproto start:  wq %X dlprim %X", wq, prim);

	mutex_enter(&sbp->sb_lock);

	switch (prim) {
		case	DL_UNITDATA_REQ:
			hmeudreq(wq, mp);
			break;

		case	DL_ATTACH_REQ:
			hmeareq(wq, mp);
			break;

		case	DL_DETACH_REQ:
			hmedreq(wq, mp);
			break;

		case	DL_BIND_REQ:
			hmebreq(wq, mp);
			break;

		case	DL_UNBIND_REQ:
			hmeubreq(wq, mp);
			break;

		case	DL_INFO_REQ:
			hmeireq(wq, mp);
			break;

		case	DL_PROMISCON_REQ:
			hmeponreq(wq, mp);
			break;

		case	DL_PROMISCOFF_REQ:
			hmepoffreq(wq, mp);
			break;

		case	DL_ENABMULTI_REQ:
			hmeemreq(wq, mp);
			break;

		case	DL_DISABMULTI_REQ:
			hmedmreq(wq, mp);
			break;

		case	DL_PHYS_ADDR_REQ:
			hmepareq(wq, mp);
			break;

		case	DL_SET_PHYS_ADDR_REQ:
			hmespareq(wq, mp);
			break;

		default:
			dlerrorack(wq, mp, prim, DL_UNSUPPORTED, 0);
			break;
	}

	TRACE_2(TR_FAC_BE, TR_BE_PROTO_END,
		"hmeproto end:  wq %X dlprim %X", wq, prim);

	mutex_exit(&sbp->sb_lock);
}

static struct hme *
hme_set_ppa(sbp)
struct	hmestr	*sbp;
{
	struct	hme	*hmep;

	if (sbp->sb_hmep)	/* ppa has been selected */
		return (sbp->sb_hmep);

	mutex_enter(&hmelock);	/* select the first one found */
	if (hme_device == -1)
		hmep = hmeup;
	else {
		for (hmep = hmeup; hmep; hmep = hmep->hme_nextp)
			if (hme_device == ddi_get_instance(hmep->hme_dip))
				break;
	}
	mutex_exit(&hmelock);

	sbp->sb_hmep = hmep;
	return (hmep);
}

static void
hmeioctl(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	iocblk	*iocp = (struct iocblk *) mp->b_rptr;
	struct	hmestr	*sbp = (struct hmestr *) wq->q_ptr;
	struct	hme		*hmep = sbp->sb_hmep;
	struct	hme		*hmep1;
	hme_ioc_cmd_t	*ioccmdp;
	int old_ipg1, old_ipg2, old_use_int_xcvr, old_autoneg;
	int	old_device, new_device;
	int	old_100fdx, old_100hdx, old_10fdx, old_10hdx;
	int	old_ipg0, old_lance_mode;

	switch (iocp->ioc_cmd) {
	case DLIOCRAW:		/* raw M_DATA mode */
		sbp->sb_flags |= HMESRAW;
		miocack(wq, mp, 0, 0);
		break;

/* XXX Remove this line in mars */
#define	DL_IOC_HDR_INFO	(DLIOC|10)	/* XXX reserved */

	case DL_IOC_HDR_INFO:	/* M_DATA "fastpath" info request */
		hme_dl_ioc_hdr_info(wq, mp);
		break;

	case HME_ND_GET:
		hmep = hme_set_ppa(sbp);
		if (hmep == NULL) {	/* no device present */
			miocnak(wq, mp, 0, EINVAL);
			return;
		}
#ifdef HME_ND_DEBUG
		hmerror(hmep->hme_dip, "hmeioctl:ND_GET");
#endif
		mutex_enter(&hmelock);
		old_autoneg = hme_param_autoneg;
		old_100fdx = hme_param_anar_100fdx;
		old_100hdx = hme_param_anar_100hdx;
		old_10fdx = hme_param_anar_10fdx;
		old_10hdx = hme_param_anar_10hdx;

		hme_param_autoneg = old_autoneg & ~HME_NOTUSR;
		hme_param_anar_100fdx = old_100fdx & ~HME_NOTUSR;
		hme_param_anar_100hdx = old_100hdx & ~HME_NOTUSR;
		hme_param_anar_10fdx = old_10fdx & ~HME_NOTUSR;
		hme_param_anar_10hdx = old_10hdx & ~HME_NOTUSR;

		if (!hme_nd_getset(wq, hmep->hme_g_nd, mp)) {
			hme_param_autoneg = old_autoneg;
			hme_param_anar_100fdx = old_100fdx;
			hme_param_anar_100hdx = old_100hdx;
			hme_param_anar_10fdx = old_10fdx;
			hme_param_anar_10hdx = old_10hdx;
			mutex_exit(&hmelock);
#ifdef HME_ND_DEBUG
			hmerror(hmep->hme_dip,
				"hmeioctl:false ret from hme_nd_getset");
#endif
			miocnak(wq, mp, 0, EINVAL);
			return;
		}
		hme_param_autoneg = old_autoneg;
		hme_param_anar_100fdx = old_100fdx;
		hme_param_anar_100hdx = old_100hdx;
		hme_param_anar_10fdx = old_10fdx;
		hme_param_anar_10hdx = old_10hdx;

		mutex_exit(&hmelock);
#ifdef HME_ND_DEBUG
			hmerror(hmep->hme_dip,
				"hmeioctl:true ret from hme_nd_getset");
#endif
		qreply(wq, mp);
		break;

	case HME_ND_SET:
		hmep = hme_set_ppa(sbp);
		if (hmep == NULL) {	/* no device present */
			miocnak(wq, mp, 0, EINVAL);
			return;
		}
#ifdef HME_ND_DEBUG
		if (hmedebug)
			hmerror(hmep->hme_dip, "hmeioctl:ND_SET");
#endif
		old_device = hme_param_device;
		old_ipg0 = hme_param_ipg0;
		old_lance_mode = hme_param_lance_mode;
		old_ipg1 = hme_param_ipg1;
		old_ipg2 = hme_param_ipg2;
		old_use_int_xcvr = hme_param_use_intphy;
		old_autoneg = hme_param_autoneg;
		hme_param_autoneg = 0xff;

		mutex_enter(&hmelock);
		if (!hme_nd_getset(wq, hmep->hme_g_nd, mp)) {
			hme_param_autoneg = old_autoneg;
			mutex_exit(&hmelock);
			miocnak(wq, mp, 0, EINVAL);
			return;
		}
		mutex_exit(&hmelock);

		if (old_device != hme_param_device) {
			new_device = hme_param_device;
			hme_param_device = old_device;
			hme_param_autoneg = old_autoneg;
			mutex_enter(&hmelock);
			for (hmep1 = hmeup; hmep1; hmep1 = hmep1->hme_nextp)
				if (new_device ==
					ddi_get_instance(hmep1->hme_dip))
					break;
			mutex_exit(&hmelock);

			if (hmep1 == NULL) {
				miocnak(wq, mp, 0, EINVAL);
				return;
			}
			hme_device = new_device;
			sbp->sb_hmep = hmep1;
			qreply(wq, mp);
			return;
		}

		qreply(wq, mp);

		if (hme_param_autoneg != 0xff) {
			hmep->hme_linkcheck = 0;
			(void) hmeinit(hmep);
		} else {
			hme_param_autoneg = old_autoneg;
			if (old_use_int_xcvr != hme_param_use_intphy) {
				hmep->hme_linkcheck = 0;
				(void) hmeinit(hmep);
			} else if ((old_ipg1 != hme_param_ipg1) ||
					(old_ipg2 != hme_param_ipg2) ||
					(old_ipg0 != hme_param_ipg0) ||
				(old_lance_mode != hme_param_lance_mode)) {
				(void) hmeinit(hmep);
			}
		}
		break;

	case HME_IOC:
		ioccmdp = (hme_ioc_cmd_t *) mp->b_cont->b_rptr;
		switch (ioccmdp->hdr.cmd) {

			case HME_IOC_GET_SPEED:
				ioccmdp->mode = hmep->hme_mode;
				switch (hmep->hme_mode) {
				case HME_AUTO_SPEED:
					ioccmdp->speed = hmep->hme_tryspeed;
					break;
				case HME_FORCE_SPEED:
					ioccmdp->speed = hmep->hme_forcespeed;
					break;
				default:
					break;
				}
				miocack(wq, mp, msgsize(mp->b_cont), 0);
				break;
			case HME_IOC_SET_SPEED:
				hmep->hme_mode = ioccmdp->mode;
				hmep->hme_linkup = 0;
				hmep->hme_delay = 0;
				hmep->hme_linkup_cnt = 0;
				hmep->hme_force_linkdown = HME_FORCE_LINKDOWN;
				hme_display_msg(hmep, hmep->hme_dip,
								"Link Down");
				/* Enable display of linkup message */
				switch (hmep->hme_mode) {
				case HME_AUTO_SPEED:
#ifdef HME_FORCE_DEBUG
	hmerror(hmep->hme_dip, "ioctl: AUTO_SPEED");

#endif
					hmep->hme_linkup_10 = 0;
					hmep->hme_tryspeed = HME_SPEED_100;
					hmep->hme_ntries = HME_NTRIES_LOW;
					hmep->hme_nlasttries = HME_NTRIES_LOW;
					hme_try_speed(hmep);
					break;
				case HME_FORCE_SPEED:
#ifdef HME_FORCE_DEBUG
	hmerror(hmep->hme_dip, "ioctl: FORCE_SPEED");
#endif
					hmep->hme_forcespeed = ioccmdp->speed;
					hme_force_speed(hmep);
					break;
				default:
					miocnak(wq, mp, 0, EINVAL);
					return;
				}
				miocack(wq, mp, 0, 0);
				break;
			default:
				miocnak(wq, mp, 0, EINVAL);
				break;
		}
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
hme_dl_ioc_hdr_info(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	mblk_t	*nmp;
	struct	hmestr	*sbp;
	struct	hmedladdr	*dlap;
	dl_unitdata_req_t	*dludp;
	struct	ether_header	*headerp;
	struct	hme	*hmep;
	int	off, len;
	int	minsize;

	sbp = (struct hmestr *) wq->q_ptr;
	minsize = sizeof (dl_unitdata_req_t) + HMEADDRL;

	/*
	 * Sanity check the request.
	 */
	if ((mp->b_cont == NULL) ||
		(MBLKL(mp->b_cont) < minsize) ||
		(*((u_long *) mp->b_cont->b_rptr) != DL_UNITDATA_REQ) ||
		((hmep = sbp->sb_hmep) == NULL)) {
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
	if (!MBLKIN(mp->b_cont, off, len) || (len != HMEADDRL)) {
		miocnak(wq, mp, 0, EINVAL);
		return;
	}

	dlap = (struct hmedladdr *) (mp->b_cont->b_rptr + off);

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
	ether_copy(&hmep->hme_ouraddr, &headerp->ether_shost);
	headerp->ether_type = dlap->dl_sap;

	/*
	 * Link new mblk in after the "request" mblks.
	 */
	linkb(mp, nmp);

	sbp->sb_flags |= HMESFAST;
	miocack(wq, mp, msgsize(mp->b_cont), 0);
}

static void
hmeareq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;
	union	DL_primitives	*dlp;
	struct	hme	*hmep;
	int	ppa;
	ulong_t	timestamp;

	sbp = (struct hmestr *) wq->q_ptr;
	dlp = (union DL_primitives *) mp->b_rptr;

	if (MBLKL(mp) < DL_ATTACH_REQ_SIZE) {
		dlerrorack(wq, mp, DL_ATTACH_REQ, DL_BADPRIM, 0);
		return;
	}

	if (sbp->sb_state != DL_UNATTACHED) {
		dlerrorack(wq, mp, DL_ATTACH_REQ, DL_OUTSTATE, 0);
		return;
	}

	ppa = dlp->attach_req.dl_ppa;

	/*
	 * Valid ppa?
	 */
	mutex_enter(&hmelock);
	for (hmep = hmeup; hmep; hmep = hmep->hme_nextp)
		if (ppa == ddi_get_instance(hmep->hme_dip))
			break;
	mutex_exit(&hmelock);

	if (hmep == NULL) {
		dlerrorack(wq, mp, dlp->dl_primitive, DL_BADPPA, 0);
		return;
	}

	/* Has device been initialized?  Do so if necessary. */
	if ((hmep->hme_flags & HMERUNNING) == 0) {
		if (hmeinit(hmep)) {
			dlerrorack(wq, mp, dlp->dl_primitive, DL_INITFAILED, 0);
			return;
		}

		timestamp = 0;  /* Busy */

		if (ddi_prop_modify(DDI_DEV_T_NONE, hmep->hme_dip, 0,
			"pm_timestamp", (caddr_t)&timestamp,
			sizeof (timestamp)) != DDI_SUCCESS)
			hmerror(hmep->hme_dip,
				"hmeareq:  ddi_prop_modify error");
	}

	/* Set link to device and update our state. */
	sbp->sb_hmep = hmep;
	sbp->sb_state = DL_UNBOUND;

	dlokack(wq, mp, DL_ATTACH_REQ);
}

static void
hmedreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;

	sbp = (struct hmestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_DETACH_REQ_SIZE) {
		dlerrorack(wq, mp, DL_DETACH_REQ, DL_BADPRIM, 0);
		return;
	}

	if (sbp->sb_state != DL_UNBOUND) {
		dlerrorack(wq, mp, DL_DETACH_REQ, DL_OUTSTATE, 0);
		return;
	}

	hmedodetach(sbp);
	dlokack(wq, mp, DL_DETACH_REQ);
}

/*
 * Detach a Stream from an interface.
 */
static void
hmedodetach(sbp)
struct	hmestr	*sbp;
{
	struct	hmestr	*tsbp;
	struct	hme	*hmep;
	int	reinit = 0;
	ulong_t	timestamp;

	ASSERT(sbp->sb_hmep);

	hmep = sbp->sb_hmep;
	sbp->sb_hmep = NULL;

	/* Disable promiscuous mode if on. */
	if (sbp->sb_flags & HMESALLPHYS) {
		sbp->sb_flags &= ~HMESALLPHYS;
		reinit = 1;
	}

	/* Disable ALLMULTI mode if on. */
	if (sbp->sb_flags & HMESALLMULTI) {
		sbp->sb_flags &= ~HMESALLMULTI;
		reinit = 1;
	}

	/* Disable any Multicast Addresses. */
	sbp->sb_mccount = 0;
	if (sbp->sb_mctab) {
		kmem_free(sbp->sb_mctab, HMEMCALLOC);
		sbp->sb_mctab = NULL;
		reinit = 1;
	}

	sbp->sb_state = DL_UNATTACHED;

	/*
	 * Detach from device structure.
	 * Uninit the device and update power management property
	 * when no other streams are attached to it.
	 */
	for (tsbp = hmestrup; tsbp; tsbp = tsbp->sb_nextp)
		if (tsbp->sb_hmep == hmep)
			break;
#ifdef	MPSAS
	if (tsbp == NULL) {
		hmep->hme_bmacregp->rxcfg &= ~BMAC_RXCFG_ENAB;
		hmep->hme_flags &= ~HMERUNNING;

		/*
		 * The frame may still be in HME local memory waiting to be
		 * transmitted.
		 */
		drv_usecwait(HMEDRAINTIME);
	}
#else
	if (tsbp == NULL)
		hmeuninit(hmep);

	drv_getparm(TIME, &timestamp);
	if (ddi_prop_modify(DDI_DEV_T_NONE, hmep->hme_dip, 0,
		"pm_timestamp", (caddr_t)&timestamp,
		sizeof (timestamp)) != DDI_SUCCESS)
		hmerror(hmep->hme_dip, "hmedodetach:  ddi_prop_modify error");
#endif	MPSAS
	else if (reinit)
		(void) hmeinit(hmep);

	hmesetipq(hmep);

}

static void
hmebreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;
	union	DL_primitives	*dlp;
	struct	hme	*hmep;
	struct	hmedladdr	hmeaddr;
	u_long	sap;
	int	xidtest;

	sbp = (struct hmestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_BIND_REQ_SIZE) {
		dlerrorack(wq, mp, DL_BIND_REQ, DL_BADPRIM, 0);
		return;
	}

	if (sbp->sb_state != DL_UNBOUND) {
		dlerrorack(wq, mp, DL_BIND_REQ, DL_OUTSTATE, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	hmep = sbp->sb_hmep;
	sap = dlp->bind_req.dl_sap;
	xidtest = dlp->bind_req.dl_xidtest_flg;

	ASSERT(hmep);

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
	sbp->sb_sap = sap;
	sbp->sb_state = DL_IDLE;

	hmeaddr.dl_sap = sap;
	ether_copy(&hmep->hme_ouraddr, &hmeaddr.dl_phys);
	dlbindack(wq, mp, sap, &hmeaddr, HMEADDRL, 0, 0);

	hmesetipq(hmep);

}

static void
hmeubreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;

	sbp = (struct hmestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_UNBIND_REQ_SIZE) {
		dlerrorack(wq, mp, DL_UNBIND_REQ, DL_BADPRIM, 0);
		return;
	}

	if (sbp->sb_state != DL_IDLE) {
		dlerrorack(wq, mp, DL_UNBIND_REQ, DL_OUTSTATE, 0);
		return;
	}

	sbp->sb_state = DL_UNBOUND;
	sbp->sb_sap = 0;

	dlokack(wq, mp, DL_UNBIND_REQ);

	hmesetipq(sbp->sb_hmep);
}

static void
hmeireq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;
	dl_info_ack_t	*dlip;
	struct	hmedladdr	*dlap;
	struct	ether_addr	*ep;
	int	size;

	sbp = (struct hmestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_INFO_REQ_SIZE) {
		dlerrorack(wq, mp, DL_INFO_REQ, DL_BADPRIM, 0);
		return;
	}

	/* Exchange current msg for a DL_INFO_ACK. */
	size = sizeof (dl_info_ack_t) + HMEADDRL + ETHERADDRL;
	if ((mp = mexchange(wq, mp, size, M_PCPROTO, DL_INFO_ACK)) == NULL)
		return;

	/* Fill in the DL_INFO_ACK fields and reply. */
	dlip = (dl_info_ack_t *) mp->b_rptr;
	*dlip = hmeinfoack;
	dlip->dl_current_state = sbp->sb_state;
	dlap = (struct hmedladdr *) (mp->b_rptr + dlip->dl_addr_offset);
	dlap->dl_sap = sbp->sb_sap;
	if (sbp->sb_hmep) {
		ether_copy(&sbp->sb_hmep->hme_ouraddr, &dlap->dl_phys);
	} else {
		bzero((caddr_t)&dlap->dl_phys, ETHERADDRL);
	}
	ep = (struct ether_addr *) (mp->b_rptr + dlip->dl_brdcst_addr_offset);
	ether_copy(&etherbroadcastaddr, ep);

	qreply(wq, mp);
}

static void
hmeponreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;

	sbp = (struct hmestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_PROMISCON_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PROMISCON_REQ, DL_BADPRIM, 0);
		return;
	}

	switch (((dl_promiscon_req_t *) mp->b_rptr)->dl_level) {
		case DL_PROMISC_PHYS:
			sbp->sb_flags |= HMESALLPHYS;
			break;

		case DL_PROMISC_SAP:
			sbp->sb_flags |= HMESALLSAP;
			break;

		case DL_PROMISC_MULTI:
			sbp->sb_flags |= HMESALLMULTI;
			break;

		default:
			dlerrorack(wq, mp, DL_PROMISCON_REQ,
						DL_NOTSUPPORTED, 0);
			return;
	}

	if (sbp->sb_hmep)
		(void) hmeinit(sbp->sb_hmep);

	if (sbp->sb_hmep)
		hmesetipq(sbp->sb_hmep);

	dlokack(wq, mp, DL_PROMISCON_REQ);
}

static void
hmepoffreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;
	int	flag;

	sbp = (struct hmestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_PROMISCOFF_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PROMISCOFF_REQ, DL_BADPRIM, 0);
		return;
	}

	switch (((dl_promiscoff_req_t *) mp->b_rptr)->dl_level) {
		case DL_PROMISC_PHYS:
			flag = HMESALLPHYS;
			break;

		case DL_PROMISC_SAP:
			flag = HMESALLSAP;
			break;

		case DL_PROMISC_MULTI:
			flag = HMESALLMULTI;
			break;

		default:
			dlerrorack(wq, mp, DL_PROMISCOFF_REQ,
						DL_NOTSUPPORTED, 0);
			return;
	}

	if ((sbp->sb_flags & flag) == 0) {
		dlerrorack(wq, mp, DL_PROMISCOFF_REQ, DL_NOTENAB, 0);
		return;
	}

	sbp->sb_flags &= ~flag;
	if (sbp->sb_hmep)
		(void) hmeinit(sbp->sb_hmep);

	if (sbp->sb_hmep)
		hmesetipq(sbp->sb_hmep);

	dlokack(wq, mp, DL_PROMISCOFF_REQ);
}

static void
hmeemreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;
	union	DL_primitives	*dlp;
	struct	ether_addr	*addrp;
	int	off;
	int	len;
	int	i;

	sbp = (struct hmestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_ENABMULTI_REQ_SIZE) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_BADPRIM, 0);
		return;
	}

	if (sbp->sb_state == DL_UNATTACHED) {
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

	if ((sbp->sb_mccount + 1) >= HMEMAXMC) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_TOOMANY, 0);
		return;
	}

	/*
	 * Allocate table on first request.
	 */
	if (sbp->sb_mctab == NULL)
		if ((sbp->sb_mctab = (struct ether_addr *)
			kmem_alloc(HMEMCALLOC, KM_SLEEP)) == NULL) {
			dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_SYSERR, ENOMEM);
			return;
		}

	/*
	 * Check to see if the address is already in the table.
	 */
	for (i = 0; i < sbp->sb_mccount; i++) {
		if (ether_cmp(&sbp->sb_mctab[i], addrp) == 0) {
			dlokack(wq, mp, DL_ENABMULTI_REQ);
			return;
		}
	}

	sbp->sb_mctab[sbp->sb_mccount++] = *addrp;

	(void) hmeinit(sbp->sb_hmep);
	dlokack(wq, mp, DL_ENABMULTI_REQ);
}

static void
hmedmreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;
	union	DL_primitives	*dlp;
	struct	ether_addr	*addrp;
	int	off;
	int	len;
	int	i;

	sbp = (struct hmestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_DISABMULTI_REQ_SIZE) {
		dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_BADPRIM, 0);
		return;
	}

	if (sbp->sb_state == DL_UNATTACHED) {
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
	for (i = 0; i < sbp->sb_mccount; i++)
		if (ether_cmp(addrp, &sbp->sb_mctab[i]) == 0) {
			bcopy((caddr_t)&sbp->sb_mctab[i+1],
				(caddr_t)&sbp->sb_mctab[i],
				((sbp->sb_mccount - i) *
				sizeof (struct ether_addr)));
			sbp->sb_mccount--;
			(void) hmeinit(sbp->sb_hmep);
			dlokack(wq, mp, DL_DISABMULTI_REQ);
			return;
		}
	dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_NOTENAB, 0);
}

static void
hmepareq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;
	union	DL_primitives	*dlp;
	int	type;
	struct	hme	*hmep;
	struct	ether_addr	addr;

	sbp = (struct hmestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_PHYS_ADDR_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PHYS_ADDR_REQ, DL_BADPRIM, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	type = dlp->physaddr_req.dl_addr_type;
	hmep = sbp->sb_hmep;

	if (hmep == NULL) {
		dlerrorack(wq, mp, DL_PHYS_ADDR_REQ, DL_OUTSTATE, 0);
		return;
	}

	switch (type) {
		case	DL_FACT_PHYS_ADDR:
			localetheraddr((struct ether_addr *) NULL, &addr);
			break;

		case	DL_CURR_PHYS_ADDR:
			ether_copy(&hmep->hme_ouraddr, &addr);
			break;

		default:
			dlerrorack(wq, mp, DL_PHYS_ADDR_REQ,
						DL_NOTSUPPORTED, 0);
			return;
	}

	dlphysaddrack(wq, mp, &addr, ETHERADDRL);
}

static void
hmespareq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;
	union	DL_primitives	*dlp;
	int	off;
	int	len;
	struct	ether_addr	*addrp;
	struct	hme	*hmep;

	sbp = (struct hmestr *) wq->q_ptr;

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
	if ((hmep = sbp->sb_hmep) == NULL) {
		dlerrorack(wq, mp, DL_SET_PHYS_ADDR_REQ, DL_OUTSTATE, 0);
		return;
	}

	/*
	 * Set new interface local address and re-init device.
	 * This is destructive to any other streams attached
	 * to this device.
	 */
	ether_copy(addrp, &hmep->hme_ouraddr);
	(void) hmeinit(sbp->sb_hmep);

	dlokack(wq, mp, DL_SET_PHYS_ADDR_REQ);
}

static void
hmeudreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	hmestr	*sbp;
	register	struct	hme	*hmep;
	register	dl_unitdata_req_t	*dludp;
	mblk_t	*nmp;
	struct	hmedladdr	*dlap;
	struct	ether_header	*headerp;
	ulong	off, len;
	ulong	sap;

	sbp = (struct hmestr *) wq->q_ptr;
	hmep = sbp->sb_hmep;

	if (sbp->sb_state != DL_IDLE) {
		dlerrorack(wq, mp, DL_UNITDATA_REQ, DL_OUTSTATE, 0);
		return;
	}

	dludp = (dl_unitdata_req_t *) mp->b_rptr;

	off = dludp->dl_dest_addr_offset;
	len = dludp->dl_dest_addr_length;

	/*
	 * Validate destination address format.
	 */
	if (!MBLKIN(mp, off, len) || (len != HMEADDRL)) {
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

	dlap = (struct hmedladdr *) (mp->b_rptr + off);

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
		ether_copy(&hmep->hme_ouraddr, &headerp->ether_shost);
		sap = dlap->dl_sap;
		freeb(mp);
		mp = nmp;
	} else {
		DB_TYPE(mp) = M_DATA;
		headerp = (struct ether_header *) mp->b_rptr;
		mp->b_wptr = mp->b_rptr + sizeof (struct ether_header);
		ether_copy(&dlap->dl_phys, &headerp->ether_dhost);
		ether_copy(&hmep->hme_ouraddr, &headerp->ether_shost);
		sap = dlap->dl_sap;
	}

	/*
	 * In 802.3 mode, the driver looks at the
	 * sap field of the DL_BIND_REQ being 0 in addition to the destination
	 * sap field in the range [0-1500]. If either is true, then the driver
	 * computes the length of the message, not including initial M_PROTO
	 * mblk (message block), of all subsequent DL_UNITDATA_REQ messages and
	 * transmits 802.3 frames that have this value in the MAC frame header
	 * length field.
	 */
	if (sap <= ETHERMTU || (sbp->sb_sap == 0))
		headerp->ether_type =
			(msgsize(mp) - sizeof (struct ether_header));
	else
		headerp->ether_type = sap;

	(void) hmestart(wq, mp, hmep);
}

/* Sampath -- HW Operations */

static
hmestart_slow(wq, mp, hmep)
queue_t *wq;
register	mblk_t	*mp;
register	struct hme	*hmep;
{
	volatile	struct	hme_tmd	*tmdp1 = NULL;
	volatile	struct	hme_tmd	*ntmdp = NULL;
	mblk_t	*nmp = NULL;
	mblk_t  *bp;
	int	len1;
	int	flags, i;
	ddi_dma_cookie_t c;

	TRACE_1(TR_FAC_BE, TR_BE_START_START, "hmestart start:  wq %X", wq);

	flags = hmep->hme_flags;

	if (flags & HMEPROMISC)
		if ((nmp = copymsg(mp)) == NULL)
			hmep->hme_allocbfail++;


	mutex_enter(&hmep->hme_xmitlock);

	if (hmep->hme_tnextp > hmep->hme_tcurp) {
		if ((hmep->hme_tnextp - hmep->hme_tcurp) > HMETPENDING)
			hmereclaim(hmep);
	} else {
/* sampath - fix here */
		if ((hmep->hme_tcurp - hmep->hme_tnextp) < HMETPENDING)
			hmereclaim(hmep);
	}

	tmdp1 = hmep->hme_tnextp;
	if ((ntmdp = NEXTTMD(hmep, tmdp1)) == hmep->hme_tcurp)
		goto notmds;

	i = tmdp1 - hmep->hme_tmdp;
	if (mp->b_cont == NULL) {
		len1 = mp->b_wptr - mp->b_rptr;
#ifdef HMEDEBUG
	if (hmedebug)
	hmerror(hmep->hme_dip, "hmestart_slow: 1 buf: len = %d b_rptr = %X",
							len1, mp->b_rptr);
#endif

		if (ddi_dma_addr_setup(hmep->hme_dip, (struct as *)0,
			(caddr_t)mp->b_rptr, len1, DDI_DMA_RDWR,
			DDI_DMA_DONTWAIT, 0, &hme_dma_limits,
			&hmep->hme_dmaxh[i])) {
			hmerror(hmep->hme_dip, "ddi_dma_addr_setup failed");
			goto done;
		} else {
			if (ddi_dma_htoc(hmep->hme_dmaxh[i],
				0, &c))
				panic("hme:  ddi_dma_htoc buf failed");
		}

		ddi_dma_sync(hmep->hme_dmaxh[i], (off_t)0, len1,
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->tmd_addr = c.dmac_address;
#ifdef HMEDEBUG
	if (hmedebug)
	hmerror(hmep->hme_dip, "hmestart_slow: 1 buf: tmd = %X \
dmac_adrs = %X", tmdp1, c.dmac_address);
#endif
/* sampath: TBD: checksum handling */
		tmdp1->tmd_flags = (u_int)(HMETMD_SOP | HMETMD_EOP |
						(u_int)HMETMD_OWN | len1);
#ifdef HMEDEBUG
	if (hmedebug)
	hmerror(hmep->hme_dip,
			"hmestart_slow: 1 buf: tmd_flags = %X, mp = %X",
						tmdp1->tmd_flags, mp);
#endif
		HMESYNCIOPB(hmep, tmdp1, sizeof (struct hme_tmd),
		    DDI_DMA_SYNC_FORDEV);
		hmep->hme_tmblkp[i] = mp;
	} else {
		len1 = msgsize(mp);
		if ((bp = allocb(len1 + 3 * HMEBURSTSIZE, BPRI_HI)) == NULL) {
			hmep->hme_allocbfail++;
			goto bad;
		}

		(void) mcopymsg(mp, (u_char *)bp->b_rptr);
		mp = bp;
		hmep->hme_tmblkp[i] = mp;

#ifdef HMEDEBUG
	if (hmedebug)
	hmerror(hmep->hme_dip, "hmestart_slow: >1 buf: len = %d b_rptr = %X",
							len1, mp->b_rptr);
#endif
		if (ddi_dma_addr_setup(hmep->hme_dip, (struct as *)0,
			(caddr_t)mp->b_rptr, len1, DDI_DMA_RDWR,
			DDI_DMA_DONTWAIT, 0, &hme_dma_limits,
			&hmep->hme_dmaxh[i])) {
			hmerror(hmep->hme_dip, "ddi_dma_addr_setup failed");
			goto done;
		} else {
			if (ddi_dma_htoc(hmep->hme_dmaxh[i],
				0, &c))
				panic("hme:  ddi_dma_htoc buf failed");
		}

		ddi_dma_sync(hmep->hme_dmaxh[i], (off_t)0, len1,
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->tmd_addr = c.dmac_address;
#ifdef HMEDEBUG
	if (hmedebug)
	hmerror(hmep->hme_dip, "hmestart_slow: > 1 buf: tmd = %X \
dmac_adrs = %X", tmdp1, c.dmac_address);
#endif
/* sampath: TBD: checksum handling */
		tmdp1->tmd_flags = HMETMD_SOP | HMETMD_EOP | HMETMD_OWN | len1;
		HMESYNCIOPB(hmep, tmdp1, sizeof (struct hme_tmd),
		    DDI_DMA_SYNC_FORDEV);
	}

	hmep->hme_tnextp = ntmdp;
	hmep->hme_etxregp->txpend = HMET_TXPEND_TDMD;

	mutex_exit(&hmep->hme_xmitlock);
	TRACE_1(TR_FAC_BE, TR_BE_START_END, "hmestart end:  wq %X", wq);

	if ((flags & HMEPROMISC) && nmp)
		hmesendup(hmep, nmp, hmepaccept);

	return (0);

bad:
	mutex_exit(&hmep->hme_xmitlock);
	if (nmp)
		freemsg(nmp);
	freemsg(mp);
	return (1);

notmds:
	hmep->hme_notmds++;
	hmep->hme_wantw = 1;
	hmep->hme_tnextp = tmdp1;
	hmereclaim(hmep);
done:
	mutex_exit(&hmep->hme_xmitlock);
	if (nmp)
		freemsg(nmp);
	(void) putbq(wq, mp);

	TRACE_1(TR_FAC_BE, TR_BE_START_END, "hmestart end:  wq %X", wq);
	return (1);
}

/*
 * Start transmission.
 * Return zero on success,
 * otherwise put msg on wq, set 'want' flag and return nonzero.
 */

static
hmestart(wq, mp, hmep)
queue_t *wq;
register	mblk_t	*mp;
register	struct hme	*hmep;
{
	volatile	struct	hme_tmd	*tmdp1 = NULL;
	volatile	struct	hme_tmd	*tmdp2 = NULL;
	volatile	struct	hme_tmd	*ntmdp = NULL;
	mblk_t	*nmp = NULL;
	mblk_t  *bp;
	int	len1, len2;
	int	flags, i, j;
	ddi_dma_cookie_t c;



	TRACE_1(TR_FAC_BE, TR_BE_START_START, "hmestart start:  wq %X", wq);

	if (!hmep->hme_linkup) {
		freemsg(mp);
		return (0);
	}

	if (hmep->hme_dvmaxh == NULL)
		return (hmestart_slow(wq, mp, hmep));

	flags = hmep->hme_flags;

	if (flags & HMEPROMISC)
		if ((nmp = copymsg(mp)) == NULL)
			hmep->hme_allocbfail++;

	mutex_enter(&hmep->hme_xmitlock);

/*
 * reclaim if there are more than HMETPENDING descriptors to be reclaimed.
 */
	if (hmep->hme_tnextp > hmep->hme_tcurp) {
		if ((hmep->hme_tnextp - hmep->hme_tcurp) > HMETPENDING) {
			hmereclaim(hmep);
		}
	} else {
		i = hmep->hme_tcurp - hmep->hme_tnextp;
		if (i && (i < (HME_TMDMAX - HMETPENDING))) {
			hmereclaim(hmep);
		}
	}

	tmdp1 = hmep->hme_tnextp;
	if ((ntmdp = NEXTTMD(hmep, tmdp1)) == hmep->hme_tcurp)
		goto notmds;

	i = tmdp1 - hmep->hme_tmdp;

	/*
	 * here we deal with 3 cases.
	 * 	1. pkt has exactly one mblk
	 * 	2. pkt has exactly two mblks
	 * 	3. pkt has more than 2 mblks. Since this almost
	 *	   always never happens, we copy all of them into
	 *	   a msh with one mblk.
	 * for each mblk in the message, we allocate a tmd and
	 * figure out the tmd index. This index also passed to
	 * dvma_kaddr_load(), which establishes the IO mapping
	 * for the mblk data. This index is used as a index into
	 * the ptes reserved by dvma_reserve
	 */

	bp = mp->b_cont;
	len1 = mp->b_wptr - mp->b_rptr;
	if (bp == NULL) {
		(void) dvma_kaddr_load(hmep->hme_dvmaxh, (caddr_t)mp->b_rptr,
		    len1, 2 * i, &c);
		(void) dvma_sync(hmep->hme_dvmaxh, 2 * i,
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->tmd_addr = c.dmac_address;
/* sampath: TBD: modify for checksum */
		tmdp1->tmd_flags = HMETMD_SOP | HMETMD_EOP | HMETMD_OWN | len1;
		HMESYNCIOPB(hmep, tmdp1, sizeof (struct hme_tmd),
		    DDI_DMA_SYNC_FORDEV);
		hmep->hme_tmblkp[i] = mp;
	} else if ((bp->b_cont == NULL) &&
			((len2 = bp->b_wptr - bp->b_rptr) >= 4)) {
/*
 * sampath: Check with HW: The minimum len restriction different for 64-bit
 * burst ?
 */
		tmdp2 = ntmdp;
		if ((ntmdp = NEXTTMD(hmep, tmdp2)) == hmep->hme_tcurp)
			goto notmds;
		j = tmdp2 - hmep->hme_tmdp;
		mp->b_cont = NULL;
		hmep->hme_tmblkp[i] = mp;
		hmep->hme_tmblkp[j] = bp;
		(void) dvma_kaddr_load(hmep->hme_dvmaxh, (caddr_t)mp->b_rptr,
		    len1, 2 * i, &c);

		(void) dvma_sync(hmep->hme_dvmaxh, 2 * i,
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->tmd_addr = c.dmac_address;
		(void) dvma_kaddr_load(hmep->hme_dvmaxh, (caddr_t)bp->b_rptr,
		    len2, 2 * j, &c);
		(void) dvma_sync(hmep->hme_dvmaxh, 2 * j,
		    DDI_DMA_SYNC_FORDEV);
		tmdp2->tmd_addr = c.dmac_address;
/* sampath: TBD: fix for checksum */
		tmdp2->tmd_flags = HMETMD_EOP | HMETMD_OWN | len2;
		HMESYNCIOPB(hmep, tmdp2, sizeof (struct hme_tmd),
		    DDI_DMA_SYNC_FORDEV);
/* sampath: TBD: fix for checksum */
		tmdp1->tmd_flags = HMETMD_SOP | HMETMD_OWN | len1;
		HMESYNCIOPB(hmep, tmdp1, sizeof (struct hme_tmd),
		    DDI_DMA_SYNC_FORDEV);

	} else {
		len1 = msgsize(mp);

		if ((bp = allocb(len1 + 3 * HMEBURSTSIZE, BPRI_HI)) == NULL) {
			hmep->hme_allocbfail++;
			goto bad;
		}

		(void) mcopymsg(mp, (u_char *)bp->b_rptr);
		mp = bp;
		hmep->hme_tmblkp[i] = mp;

		(void) dvma_kaddr_load(hmep->hme_dvmaxh, (caddr_t)mp->b_rptr,
		    len1, 2 * i, &c);
		(void) dvma_sync(hmep->hme_dvmaxh, 2 * i,
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->tmd_addr = c.dmac_address;
/* sampath: TBD: fix for checksum */
		tmdp1->tmd_flags = HMETMD_SOP | HMETMD_EOP | HMETMD_OWN | len1;
		HMESYNCIOPB(hmep, tmdp1, sizeof (struct hme_tmd),
		    DDI_DMA_SYNC_FORDEV);
	}

	hmep->hme_tnextp = ntmdp;
	hmep->hme_etxregp->txpend = HMET_TXPEND_TDMD;
#ifdef HMEDEBUG1
	if (hmedebug) {
		hmerror(hmep->hme_dip, "hmestart:  Transmitted a frame");
	}
#endif

	mutex_exit(&hmep->hme_xmitlock);
	TRACE_1(TR_FAC_BE, TR_BE_START_END, "hmestart end:  wq %X", wq);

	if ((flags & HMEPROMISC) && nmp)
		hmesendup(hmep, nmp, hmepaccept);


	return (0);

bad:
	mutex_exit(&hmep->hme_xmitlock);
	if (nmp)
		freemsg(nmp);
	freemsg(mp);
	return (1);

notmds:
	hmep->hme_notmds++;
	hmep->hme_wantw = 1;
	hmep->hme_tnextp = tmdp1;
	hmereclaim(hmep);
done:
	mutex_exit(&hmep->hme_xmitlock);
	if (nmp)
		freemsg(nmp);
	(void) putbq(wq, mp);

	TRACE_1(TR_FAC_BE, TR_BE_START_END, "hmestart end:  wq %X", wq);
	return (1);

}

/*
 * Initialize channel.
 * Return 0 on success, nonzero on error.
 *
 * The recommended sequence for initialization is:
 * 1. Issue a Global Reset command to the Ethernet Channel.
 * 2. Poll the Global_Reset bits until the execution of the reset has been
 *    completed.
 * 2(a). Use the MIF Frame/Output register to reset the transceiver.
 *	 Poll Register 0 to till the Resetbit is 0.
 * 2(b). Use the MIF Frame/Output register to set the PHY in in Normal-Op,
 *	 100Mbps and Non-Isolated mode. The main point here is to bring the
 *	 PHY out of Isolate mode so that it can generate the rx_clk and tx_clk
 *	 to the MII interface so that the Bigmac core can correctly reset
 *	 upon a software reset.
 * 2(c).  Issue another Global Reset command to the Ethernet Channel and poll
 *	  the Global_Reset bits till completion.
 * 3. Set up all the data structures in the host memory.
 * 4. Program the TX_MAC registers/counters (excluding the TX_MAC Configuration
 *    Register).
 * 5. Program the RX_MAC registers/counters (excluding the RX_MAC Configuration
 *    Register).
 * 6. Program the Transmit Descriptor Ring Base Address in the ETX.
 * 7. Program the Receive Descriptor Ring Base Address in the ERX.
 * 8. Program the Global Configuration and the Global Interrupt Mask Registers.
 * 9. Program the ETX Configuration register (enable the Transmit DMA channel).
 * 10. Program the ERX Configuration register (enable the Receive DMA channel).
 * 11. Program the XIF Configuration Register (enable the XIF).
 * 12. Program the RX_MAC Configuartion Register (Enable the RX_MAC).
 * 13. Program the TX_MAC Configuration Register (Enable the TX_MAC).
 */


#ifdef FEPS_URUN_BUG
int hme_palen = 32;
#endif


static int
hmeinit(hmep)
struct	hme	*hmep;
{
	volatile struct	hme_global	*globp;
	volatile struct	hme_etx		*etxp;
	volatile struct	hme_erx		*erxp;
	volatile struct	hme_bmac	*bmacp;
	volatile struct hme_mif		*mifp;
	struct	hmestr	*sbp;
	mblk_t	*bp;
	u_char	ladrf[8];
	dev_info_t	*dip;
	int		i;
	ddi_dma_cookie_t c;

	TRACE_1(TR_FAC_BE, TR_BE_INIT_START, "hmeinit start:  hmep %X", hmep);

	while (hmep->hme_flags & HMESUSPENDED)
		ddi_dev_is_needed(hmep->hme_dip, 0, 1);

#ifdef HMEDEBUG
	if (hmedebug) {
		hmerror(hmep->hme_dip, "hmeinit:  Entered");
	}
#endif HMEDEBUG

	globp = hmep->hme_globregp;
	etxp = hmep->hme_etxregp;
	erxp = hmep->hme_erxregp;
	bmacp = hmep->hme_bmacregp;
	mifp = hmep->hme_mifregp;
	dip = hmep->hme_dip;

	mutex_enter(&hmep->hme_intrlock);
	rw_enter(&hmestruplock, RW_WRITER);
	mutex_enter(&hmep->hme_xmitlock);
	hme_stop_timer(hmep);	/* acquire hme_linklock */

	hmep->hme_flags = 0;
	hmep->hme_wantw = 0;
	hmep->hme_inits++;

	if (hmep->hme_inits > 1)
		hmesavecntrs(hmep);

	hme_stop_mifpoll(hmep);
	/*
	 * Perform Global reset of the FEPS ENET channel.
	 */
	(void) hmestop(hmep);

	/*
	 * An ugly anti-DDI hack for performance.
	 */
#ifdef	notdef	/* XXX Jupiter does not support sun4e.  Fix later. */
	if (((cputype & CPU_ARCH) == SUN4C_ARCH) ||
		((cputype & CPU_ARCH) == SUN4E_ARCH))
#endif
	/*
	 *	Where is cputype defined and declared?
	 */
	if ((cputype & CPU_ARCH) == SUN4C_ARCH)
		hmep->hme_flags |= HMESUN4C;

	/*
	 * Reject this device if it's in a slave-only slot.
	 */
	if (ddi_slaveonly(dip) == DDI_SUCCESS) {
		hme_start_timer(hmep, hme_check_link, HME_LINKCHECK_TIMER);
		hmerror(dip, "not used - device in slave only slot");
		goto done;
	}

	/*
	 * Allocate data structures.
	 */
	hmeallocthings(hmep);
	hmefreebufs(hmep);

	/*
	 * Clear all descriptors.
	 */
	bzero((caddr_t)hmep->hme_rmdp, HME_RMDMAX * sizeof (struct hme_rmd));
	bzero((caddr_t)hmep->hme_tmdp, HME_TMDMAX * sizeof (struct hme_tmd));

	/*
	 * Hang out receive buffers.
	 */
	for (i = 0; i < HMERPENDING; i++) {
		if ((bp = allocb(HMEBUFSIZE, BPRI_LO)) == NULL) {
			hmerror(hmep->hme_dip, "hmeinit allocb failed");
			goto done;
		}
		/* XXX alignment ??? */
		bp->b_rptr = ROUNDUP2(bp->b_rptr, HMEBURSTSIZE);
		if (hmep->hme_dvmarh)
			(void) dvma_kaddr_load(hmep->hme_dvmarh,
					    (caddr_t)bp->b_rptr,
					    (u_int)HMEBUFSIZE,
					    2 * i, &c);
		else {
			/* slow case */
			if (ddi_dma_addr_setup(hmep->hme_dip, (struct as *)0,
				(caddr_t)bp->b_rptr, HMEBUFSIZE,
				DDI_DMA_RDWR, DDI_DMA_DONTWAIT, 0,
				&hme_dma_limits,
				&hmep->hme_dmarh[i]))

				panic("hme: ddi_dma_addr_setup of bufs failed");
			else {
				if (ddi_dma_htoc
				    (hmep->hme_dmarh[i],
				    0, &c))
					panic("hme:  ddi_dma_htoc buf failed");
			}
		}
		hmermdinit(&hmep->hme_rmdp[i], c.dmac_address);
		hmep->hme_rmblkp[i] = bp;	/* save for later use */
	}

	/*
	 * DMA sync descriptors.
	 */
	HMESYNCIOPB(hmep, hmep->hme_rmdp, (HME_RMDMAX * sizeof (struct hme_rmd)
		+ HME_TMDMAX * sizeof (struct hme_tmd)), DDI_DMA_SYNC_FORDEV);


	/*
	 * Reset RMD and TMD 'walking' pointers.
	 */
	hmep->hme_rnextp = hmep->hme_rmdp;
	hmep->hme_rlastp = hmep->hme_rmdp + HMERPENDING - 1;
	hmep->hme_tcurp = hmep->hme_tmdp;
	hmep->hme_tnextp = hmep->hme_tmdp;

	/*
	 * Determine if promiscuous mode.
	 */
	for (sbp = hmestrup; sbp; sbp = sbp->sb_nextp) {
		if ((sbp->sb_hmep == hmep) && (sbp->sb_flags & HMESALLPHYS)) {
			hmep->hme_flags |= HMEPROMISC;
			break;
		}
	}


/* This is the right place to initialize MIF !!! */

	mifp->mif_imask = HME_MIF_INTMASK;	/* mask all interrupts */

	if (!hmep->hme_frame_enable)
		mifp->mif_cfg = mifp->mif_cfg | HME_MIF_CFGBB;
						/* enable bit-bang mode */
	else
		mifp->mif_cfg = mifp->mif_cfg & ~HME_MIF_CFGBB;
						/* enable frame mode */
/*
 * Depending on the transceiver detected, select the source of the clocks
 * for the MAC. Without the clocks, TX_MAC does not reset. When the
 * Global Reset is issued to the FEPS chip, it selects Internal
 * by default.
 */
	hme_check_transceiver(hmep);
	if (hmep->hme_transceiver == HME_NO_TRANSCEIVER)
		goto done;	/* abort initialization */
	if (hmep->hme_transceiver == HME_INTERNAL_TRANSCEIVER)
		bmacp->xifc = 0;
	else
		bmacp->xifc = (BMAC_XIFC_MIIBUFDIS);
				/* Isolate the Int. xcvr */
/*
 * Perform transceiver reset and speed selection only if the link is down.
 */
	if (!hmep->hme_linkcheck) {
		if (hme_reset_transceiver(hmep)) {
			hmerror(hmep->hme_dip, "hmeinit: phy reset failure");
			hme_start_timer(hmep, hme_check_link, HME_TICKS*10);
			goto done;
		}

		hmep->hme_force_linkdown = HME_FORCE_LINKDOWN;
		hmep->hme_linkcheck = 0;
		hmep->hme_linkup = 0;
		hmep->hme_delay = 0;
		hmep->hme_linkup_10 = 0;
		hmep->hme_linkup_cnt = 0;
		hme_setup_link_control(hmep);
		hme_start_timer(hmep, hme_check_link, HME_LINKCHECK_TIMER);
		switch (hmep->hme_mode) {
		case HME_AUTO_SPEED:
			hmep->hme_ntries = HME_NTRIES_LOW;
			hmep->hme_nlasttries = HME_NTRIES_LOW;
			hmep->hme_autoneg = HME_HWAN_TRY;
			hme_try_speed(hmep);
			break;
		case HME_FORCE_SPEED:
			hme_force_speed(hmep);
			break;
		default:
			break;
		}
	} else {
		if (hmep->hme_linkup)
			hme_start_mifpoll(hmep);
		hme_start_timer(hmep, hme_check_link, HME_LINKCHECK_TIMER);
	}


	/*
	 * Initialize BigMAC registers.
	 * First set the tx enable bit in tx config reg to 0 and poll on
	 * it till it turns to 0. Same for rx config, hash and address
	 * filter reg.
	 * Here is the sequence per the spec.
	 * MADD2 - MAC Address 2
	 * MADD1 - MAC Address 1
	 * MADD0 - MAC Address 0
	 * HASH3, HASH2, HASH1, HASH0 for group address
	 * AFR2, AFR1, AFR0 and AFMR for address filter mask
	 * Program RXMIN and RXMAX for packet length if not 802.3
	 * RXCFG - Rx config for not stripping CRC
	 * XXX Anything else to hme configured in RXCFG
	 * IPG1, IPG2, ALIMIT, SLOT, PALEN, PAPAT, TXSFD, JAM, TXMAX, TXMIN
	 * if not 802.3 compliant
	 * XIF register for speed selection
	 * MASK  - Interrupt mask
	 * Set bit 0 of TXCFG
	 * Set bit 0 of RXCFG
	 */

	/*
	 * BigMAC requires that we confirm that tx, rx and hash are in
	 * quiescent state.
	 * XXX Does hmerxreset take care of hash and address filters also?
	 * MAC will not reset succesfully if the transceiver is not reset and
	 * brought out of Isolate mode correctly. TXMAC reset may fail if the
	 * ext. transceiver is just disconnected. If it fails, try again by
	 * checking the transceiver.
	 */

	if (hmetxreset(hmep)) {
		hmerror(hmep->hme_dip, "txmac did not reset");
		hme_stop_timer(hmep);	/* acquire hme_linklock */
		hmep->hme_linkup = 0;	/* force init again */
		hme_start_timer(hmep, hme_check_link, HME_TICKS*10);
		goto done;
	}
	if (hmerxreset(hmep)) {
		hmerror(hmep->hme_dip, "rxmac did not reset");
		hme_stop_timer(hmep);	/* acquire hme_linklock */
		hmep->hme_linkup = 0;	/* force init again */
		hme_start_timer(hmep, hme_check_link, HME_TICKS*10);
		goto done;
	}

	/* Initialize the TX_MAC registers */
	/* Howard added initialization of jamsize to work around rx crc bug */
	bmacp->jam = jamsize;

#ifdef FEPS_URUN_BUG
	if (hme_urun_fix)
		bmacp->palen = hme_palen;
#endif

		bmacp->ipg1 = hme_param_ipg1;
		bmacp->ipg2 = hme_param_ipg2;
#ifdef HME_IPG_DEBUG
	if (hmedebug)
		hmerror(hmep->hme_dip,
			"hmeinit: ipg1 = %d ipg2 = %d",
				hme_param_ipg1, hme_param_ipg2);
#endif

	bmacp->rseed = ((hmep->hme_ouraddr.ether_addr_octet[0] << 8) & 0x3) | \
		hmep->hme_ouraddr.ether_addr_octet[1];

	/* Initialize the RX_MAC registers */

	/*
	 * Program BigMAC with local individual ethernet address.
	 */
	bmacp->madd2 = (hmep->hme_ouraddr.ether_addr_octet[4] << 8) | \
		hmep->hme_ouraddr.ether_addr_octet[5];
	bmacp->madd1 = (hmep->hme_ouraddr.ether_addr_octet[2] << 8) | \
		hmep->hme_ouraddr.ether_addr_octet[3];
	bmacp->madd0 = (hmep->hme_ouraddr.ether_addr_octet[0] << 8) | \
		hmep->hme_ouraddr.ether_addr_octet[1];

	/*
	 * Set up multicast address filter by passing all multicast
	 * addresses through a crc generator, and then using the
	 * low order 6 bits as a index into the 64 bit logical
	 * address filter. The high order three bits select the word,
	 * while the rest of the bits select the bit within the word.
	 */
	bzero((caddr_t)ladrf, 8 * sizeof (u_char));
	for (sbp = hmestrup; sbp; sbp = sbp->sb_nextp) {
		if (sbp->sb_hmep == hmep) {
			if ((sbp->sb_mccount == 0) &&
					!(sbp->sb_flags & HMESALLMULTI))
				continue;

			if (sbp->sb_flags & HMESALLMULTI) {
				for (i = 0; i < 8; i++) {
					ladrf[i] = 0xff;
				}
				break;	/* All bits are already on */
			}

			for (i = 0; i < sbp->sb_mccount; i++) {
				register u_char *cp;
				register u_long crc;
				register u_long c;
				register int len;
				int	j;

				cp = (unsigned char *) &sbp->sb_mctab[i];
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
				/*
				 * Just want the 6 most significant bits.
				 * Note that these 6 bits are really the
				 * 6 least significant bits of the CRC-32
				 * result hmecause we shift right in our CRC
				 * computation.
				 */
				crc >>= 26;

				/*
				 * Turn on the corresponding bit
				 * in the address filter.
				 */
				ladrf[crc >> 3] |= 1 << (crc & 0x7);
			}
		}
	}

	bmacp->hash0 = (ladrf[0] | (ladrf[1] << 8));
	bmacp->hash1 = (ladrf[2] | (ladrf[3] << 8));
	bmacp->hash2 = (ladrf[4] | (ladrf[5] << 8));
	bmacp->hash3 = (ladrf[6] | (ladrf[7] << 8));

	/*
	 * Set up the address filter now?
	 */

	/*
	 * Initialize HME Global registers, ETX registers and ERX registers.
	 */

	etxp->txring = ((u_int) HMEIOPBIOADDR(hmep, hmep->hme_tmdp));
	erxp->rxring  = ((u_int) HMEIOPBIOADDR(hmep, hmep->hme_rmdp));

#ifdef FEPS_ERX_BUG
/*
	FEPS_ERX_BUG

	ERX registers can be written only if they have even no. of bits set.
	So, if the value written is not read back, set the lsb and write again.
*/
	if (hme_erx_fix) {
			u_int temp;
			temp  = ((u_int) HMEIOPBIOADDR(hmep, hmep->hme_rmdp));
			if (erxp->rxring != temp)
				erxp->rxring = temp | 4;
	}
#endif

#ifdef HME_ERX_DEBUG
	if (hme_erx_debug) {
		hmerror(hmep->hme_dip, "rxring written = %X",
			((u_int) HMEIOPBIOADDR(hmep, hmep->hme_rmdp)));
		hmerror(hmep->hme_dip, "rxring read = %X", erxp->rxring);
	}
#endif

	if (!(hmep->hme_burstsizes = hmeburstsizes(hmep->hme_dip)))
		goto done;	/* failure exit */

/*
 * Use user-configurable parameter for enabling 64-bit transfers
 */
	if (i = (hmep->hme_burstsizes >> 16)) {
		hmep->hme_burstsizes = i;
		hmep->hme_64bit_xfer = hme_64bit_enable;
						/* user configured value */
	}

	i = HMEG_CONFIG_BURST16;
	if (hmep->hme_burstsizes & 0x40)
		i = HMEG_CONFIG_BURST64;
	else if (hmep->hme_burstsizes & 0x20)
		i = HMEG_CONFIG_BURST32;

#ifdef MPSAS
	globp->config = HMEG_CONFIG_BURST32;
#else
	globp->config = i | (hmep->hme_64bit_xfer << HMEG_CONFIG_64BIT_SHIFT);
#endif

	/*
	 * XXX Significant performence improvements can be achieved by
	 * disabling transmit interrupt. Thus TMD's are reclaimed only
	 * when we run out of them in hmestart().
	 */
	globp->intmask = HMEG_MASK_INTR | HMEG_MASK_TINT;

	etxp->txring_size = (HME_TMDMAX -1)>> HMET_RINGSZ_SHIFT;
	etxp->config |= HMET_CONFIG_TXDMA_EN;

	/* get the rxring size bits */
	switch (HME_RMDMAX) {
		case 32: i = HMER_CONFIG_RXRINGSZ32;
			break;
		case 64: i = HMER_CONFIG_RXRINGSZ64;
			break;
		case 128: i = HMER_CONFIG_RXRINGSZ128;
			break;
		case 256: i = HMER_CONFIG_RXRINGSZ256;
			break;
	}
	i |= (HME_FSTBYTE_OFFSET << HMER_CONFIG_FBO_SHIFT)
			| HMER_CONFIG_RXDMA_EN;
	erxp->config = i;

#ifdef FEPS_ERX_BUG
/*
 * Bug related to the parity handling in ERX. When erxp-config is read back,
 * FEPS drives the parity bit. This value is used while writing again.
 * This fixes the RECV problem in SS5.
 */
	if (hme_erx_fix) {
		int temp;
		temp = erxp->config;
		erxp->config = i;
		if (erxp->config != i)
			hmerror(hmep->hme_dip,
			"error:temp = %X erxp->config = %X, should be %X",
					temp, erxp->config, i);
	}
#endif

	/*
	 * Set up the rxconfig, txconfig and seed register without enabling
	 * them the former two at this time
	 *
	 * BigMAC strips the CRC bytes by default. Since this is
	 * contrary to other pieces of hardware, this bit needs to
	 * enabled to tell BigMAC not to strip the CRC bytes.
	 * Do not filter this node's own packets.
	 */

#ifdef	MPSAS
	bmacp->rxcfg = (BMAC_RXCFG_CRC);
#else	MPSAS
	if (hme_reject_own) {
		bmacp->rxcfg =
			((hmep->hme_flags & HMEPROMISC ? BMAC_RXCFG_PROMIS : 0)\
				| BMAC_RXCFG_MYOWN | BMAC_RXCFG_HASH);
	} else {
		bmacp->rxcfg =
			((hmep->hme_flags & HMEPROMISC ? BMAC_RXCFG_PROMIS : 0)\
				| BMAC_RXCFG_HASH);
	}
#endif	MPSAS
	drv_usecwait(10);	/* wait after setting Hash Enable bit */

	if (hme_ngu_enable)
		bmacp->txcfg = (hmep->hme_fdx ? BMAC_TXCFG_FDX: 0) |
								BMAC_TXCFG_NGU;
	else
		bmacp->txcfg = (hmep->hme_fdx ? BMAC_TXCFG_FDX: 0);
	hmep->hme_macfdx = hmep->hme_fdx;


#ifdef MPSAS
	bmacp->xifc = (BMAC_XIFC_ENAB | BMAC_XIFC_XIFLPBK);
#else
	i = 0;
	if ((hme_param_lance_mode) && (hmep->hme_lance_mode_enable))
		i = ((hme_param_ipg0) << BMAC_XIFC_IPG0_SHIFT)
					| BMAC_XIFC_LANCE_ENAB;
	if (hmep->hme_transceiver == HME_INTERNAL_TRANSCEIVER)
		bmacp->xifc = i | (BMAC_XIFC_ENAB);
	else
		bmacp->xifc = i | (BMAC_XIFC_ENAB | BMAC_XIFC_MIIBUFDIS);
#endif
#ifdef HME_LANCE_MODE_DEBUG
		hmerror(dip, "hmeinit : xifc = %X", bmacp->xifc);
#endif

	bmacp->rxcfg |= BMAC_RXCFG_ENAB;
	bmacp->txcfg |= BMAC_TXCFG_ENAB;


	hmep->hme_flags |= HMERUNNING;

	hmewenable(hmep);

done:
	mutex_exit(&hmep->hme_xmitlock);
	rw_exit(&hmestruplock);
	mutex_exit(&hmep->hme_intrlock);

#ifdef HMEDEBUG
	if (hmedebug)
		if (!(hmep->hme_flags & HMERUNNING))
			hmerror(dip, "hmeinit failed");
		else
			hmerror(dip, "hmeinit - succesful exit");
#endif
	return (!(hmep->hme_flags & HMERUNNING));
}

/*
 * Calculate the dvma burstsize by setting up a dvma temporarily.  Return
 * 0 as burstsize upon failure as it signifies no burst size.
 * Requests for 64-bit transfer setup, if the platform supports it.
 */
static int
hmeburstsizes(dev_info_t *dip)
{
	caddr_t	addr;
	u_int size;
	int burstsizes;
	ddi_dma_handle_t handle;

	size = HMEBUFSIZE;
	addr = kmem_alloc(size, KM_SLEEP);
	if (ddi_dma_addr_setup(dip, (struct as *) 0, addr, size,
		DDI_DMA_RDWR | DDI_DMA_SBUS_64BIT, DDI_DMA_DONTWAIT,
					0, &hme_dma_limits, &handle)) {
		hmerror(dip, "ddi_dma_addr_setup failed");
		return (0);
	}
	burstsizes = ddi_dma_burstsizes(handle);
	(void) ddi_dma_free(handle);
	kmem_free(addr, size);
	return (burstsizes);
}

static void
hmefreebufs(hmep)
struct hme *hmep;
{
	int		i;

	/*
	 * Free and dvma_unload pending xmit and recv buffers.
	 * Maintaining the 1-to-1 ordered sequence of
	 * dvma_load() followed by dvma_unload() is critical.
	 * Always unload anything before loading it again.
	 * Never unload anything twice.  Always unload
	 * before freeing the buffer.  We satisfy these
	 * requirements by unloading only those descriptors
	 * which currently have an mblk associated with them.
	 */
	for (i = 0; i < HME_TMDMAX; i++) {
		if (hmep->hme_tmblkp[i]) {
			if (hmep->hme_dvmaxh)
				dvma_unload(hmep->hme_dvmaxh,
						2 * i, DONT_FLUSH);
			freeb(hmep->hme_tmblkp[i]);
			hmep->hme_tmblkp[i] = NULL;
		}
	}

	for (i = 0; i < HME_RMDMAX; i++) {
		if (hmep->hme_rmblkp[i]) {
			if (hmep->hme_dvmarh)
				dvma_unload(hmep->hme_dvmarh, 2 * HMERINDEX(i),
					DDI_DMA_SYNC_FORKERNEL);
			freeb(hmep->hme_rmblkp[i]);
			hmep->hme_rmblkp[i] = NULL;
		}
	}

	if (hmep->hme_dmarh) {
		/* slow case */
		for (i = 0; i < HME_TMDMAX; i++) {
			if (hmep->hme_dmaxh[i]) {
				(void) ddi_dma_free(hmep->hme_dmaxh[i]);
				hmep->hme_dmaxh[i] = NULL;
			}
		}
		for (i = 0; i < HMERPENDING; i++) {
			if (hmep->hme_dmarh[i]) {
				(void) ddi_dma_free(hmep->hme_dmarh[i]);
				hmep->hme_dmarh[i] = NULL;
			}
		}
	}
}

/*
 * hme_start_mifpoll() - Enables the polling of the BMSR register of the PHY.
 * After enabling the poll, delay for atleast 62us for one poll to be done.
 * Then read the MIF status register to auto-clear the MIF status field.
 * Then program the MIF interrupt mask register to enable interrupts for the
 * LINK_STATUS and JABBER_DETECT bits.
 */

static void
hme_start_mifpoll(hmep)
struct hme *hmep;
{
	volatile struct hme_mif		*mifp;
	register cfg;

	if (!hmep->hme_mifpoll_enable)
		return;
	mifp = hmep->hme_mifregp;
	cfg = (mifp->mif_cfg & ~(HME_MIF_CFGPD | HME_MIF_CFGPR));
	mifp->mif_cfg = cfg = (cfg | (hmep->hme_phyad << HME_MIF_CFGPD_SHIFT) |
			(HME_PHY_BMSR << HME_MIF_CFGPR_SHIFT) | HME_MIF_CFGPE);
	drv_usecwait(HME_MIF_POLL_DELAY);
	hmep->hme_polling_on = 1;
	hmep->hme_mifpoll_flag = 0;
	hmep->hme_mifpoll_data = (mifp->mif_bsts >> 16);

	/* Do not poll for Jabber Detect for 100 Mbps speed */
	if (((hmep->hme_mode == HME_AUTO_SPEED) &&
		(hmep->hme_tryspeed == HME_SPEED_100)) ||
		((hmep->hme_mode == HME_FORCE_SPEED) &&
		(hmep->hme_forcespeed == HME_SPEED_100)))
		mifp->mif_imask = (u_short)~(PHY_BMSR_LNKSTS);
	else
		mifp->mif_imask = (u_short)~(PHY_BMSR_LNKSTS | PHY_BMSR_JABDET);
#ifdef HME_MIFPOLL_DEBUG
	if (hmedebug)
		hmerror(hmep->hme_dip,
		"mifpoll started: mif_cfg = %X mif_bsts = %X",
			cfg, mifp->mif_bsts);
#endif
}

static void
hme_stop_mifpoll(hmep)
struct hme *hmep;
{
	volatile struct hme_mif		*mifp;

	if ((!hmep->hme_mifpoll_enable) || (!hmep->hme_polling_on))
		return;
	mifp = hmep->hme_mifregp;
	mifp->mif_imask = 0xffff;	/* mask interrupts */
	mifp->mif_cfg = (mifp->mif_cfg & ~HME_MIF_CFGPE);
	hmep->hme_polling_on = 0;
	drv_usecwait(HME_MIF_POLL_DELAY);
}

static u_int
hmetxreset(hmep)
struct hme *hmep;
{
	volatile	u_int	*txrstp;
	register int	n;

	txrstp = &(hmep->hme_bmacregp->txrst);
	*txrstp = 0;
#ifdef MPSAS
	n = BMACTXRSTDELAY / hme_mpsas_period;
#else
	n = BMACTXRSTDELAY / HMEWAITPERIOD;
#endif
	while (--n > 0) {
		if (*txrstp == 0)
			return (0);
#ifdef MPSAS
		drv_usecwait(hme_mpsas_period);
#else
		drv_usecwait(HMEWAITPERIOD);
#endif
	}
	return (1);
}

static u_int
hmerxreset(hmep)
struct hme *hmep;
{
	volatile	u_int	*rxrstp;
	register int	n;

	rxrstp = &(hmep->hme_bmacregp->rxrst);
	*rxrstp = 0;
#ifdef MPSAS
	n = BMACTXRSTDELAY / hme_mpsas_period;
#else
	n = BMACRXRSTDELAY / HMEWAITPERIOD;
#endif
	while (--n > 0) {
		if (*rxrstp == 0)
			return (0);
#ifdef MPSAS
		drv_usecwait(hme_mpsas_period);
#else
		drv_usecwait(HMEWAITPERIOD);
#endif
	}
	return (1);
}

/*
 * Un-initialize (STOP) HME channel.
 */
static void
hmeuninit(struct hme *hmep)
{
	/*
	 * Allow up to 'HMEDRAINTIME' for pending xmit's to complete.
	 */
	HMEDELAY((hmep->hme_tcurp == hmep->hme_tnextp), HMEDRAINTIME);

	mutex_enter(&hmep->hme_intrlock);
	mutex_enter(&hmep->hme_xmitlock);

	hmep->hme_flags &= ~HMERUNNING;

	(void) hmestop(hmep);

	mutex_exit(&hmep->hme_xmitlock);
	mutex_exit(&hmep->hme_intrlock);
}

/*
 * Allocate CONSISTENT memory for rmds and tmds with appropriate alignemnt and
 * map it in IO space.
 * For Fast-DMA interface allocate pagetables for transmit and receive buffers.
 * If unsuccesful, allocate space for transmit and receive ddi_dma_handle
 * structures to use the slow-DMA interface.
 */
static void
hmeallocthings(hmep)
struct	hme	*hmep;
{
	u_long	a;
	int		size;
	ddi_dma_cookie_t	c;

	/*
	 * Return if resources are already allocated.
	 */
	if (hmep->hme_rmdp)
		return;

	/*
	 * Allocate the TMD and RMD descriptors and extra for alignments.
	 */
	size = (HME_RMDMAX * sizeof (struct hme_rmd)
		+ HME_TMDMAX * sizeof (struct hme_tmd)) + HME_HMDALIGN;
	if (ddi_iopb_alloc(hmep->hme_dip, &hme_dma_limits,
		(u_int) size,
		(caddr_t *)&hmep->hme_iopbkbase)) {
		panic("hmeallocthings:  out of iopb space");
		/*NOTREACHED*/
	}
	a = hmep->hme_iopbkbase;
	a = ROUNDUP(a, HME_HMDALIGN);
	hmep->hme_rmdp = (struct hme_rmd *) a;
	a += HME_RMDMAX * sizeof (struct hme_rmd);
	hmep->hme_tmdp = (struct hme_tmd *) a;

	/*
	 * IO map this and get an "iopb" dma handle.
	 */
	if (ddi_dma_addr_setup(hmep->hme_dip, (struct as *) 0,
		(caddr_t)hmep->hme_iopbkbase, size,
		DDI_DMA_RDWR|DDI_DMA_CONSISTENT,
		DDI_DMA_DONTWAIT, 0, &hme_dma_limits,
		&hmep->hme_iopbhandle))
		panic("hme:  ddi_dma_addr_setup iopb failed");

	/*
	 * Initialize iopb io virtual address.
	 */
	if (ddi_dma_htoc(hmep->hme_iopbhandle, 0, &c))
		panic("hme:  ddi_dma_htoc iopb failed");
	hmep->hme_iopbiobase = c.dmac_address;

	/*
	 * dvma_reserve() reserves DVMA space for private management by a
	 * device driver. Specifically we reserve n (HME_TMDMAX + HME_RMDMAX)
	 * pagetable enteries. Therefore we have 2 ptes for each
	 * descriptor. Since the ethernet buffers are 1518 bytes
	 * so they can at most use 2 ptes. The pte are updated when
	 * we do a dvma_kaddr_load.
	 */
	if (((dvma_reserve(hmep->hme_dip, &hme_dma_limits, (HME_TMDMAX * 2),
		&hmep->hme_dvmaxh)) != DDI_SUCCESS) ||
	    (hme_force_dma)) {
		/*
		 * The reserve call has failed. This implies
		 * that we have to fall back to the older interface
		 * which will do a ddi_dma_addr_setup for each bufer
		 */
		hmep->hme_dmaxh = (ddi_dma_handle_t *)
		    kmem_zalloc(((HME_TMDMAX +  HMERPENDING) *
			(sizeof (ddi_dma_handle_t))), KM_SLEEP);
		hmep->hme_dmarh = hmep->hme_dmaxh + HME_TMDMAX;
		hmep->hme_dvmaxh = hmep->hme_dvmarh = NULL;
	} else {
		/*
		 * reserve dvma space for the receive side. If this call
		 * fails, we have to release the resources and fall
		 * back to slow case
		 */
		if ((dvma_reserve(hmep->hme_dip, &hme_dma_limits,
			(HMERPENDING * 2), &hmep->hme_dvmarh)) != DDI_SUCCESS) {
			(void) dvma_release(hmep->hme_dvmaxh);

			hmep->hme_dmaxh = (ddi_dma_handle_t *)
				kmem_zalloc(((HME_TMDMAX +  HMERPENDING) *
				    (sizeof (ddi_dma_handle_t))), KM_SLEEP);
			hmep->hme_dmarh = hmep->hme_dmaxh + HME_TMDMAX;
			hmep->hme_dvmaxh = hmep->hme_dvmarh = NULL;
		}
	}

#ifdef HME_DVMA_DEBUG
	if (hmedebug) {
		if (!hmep->hme_dvmaxh || !hmep->hme_dvmarh)
			hmerror(hmep->hme_dip,
				"SLOW DMA interface being used");
		else
			hmerror(hmep->hme_dip,
				"FAST DMA interface being used");
	}
#endif

	/*
	 * Keep handy limit values for RMD, TMD, and Buffers.
	 */
	hmep->hme_rmdlimp = &((hmep->hme_rmdp)[HME_RMDMAX]);
	hmep->hme_tmdlimp = &((hmep->hme_tmdp)[HME_TMDMAX]);

	/*
	 * Zero out xmit and rcv holders.
	 */
	bzero((caddr_t)hmep->hme_tmblkp, sizeof (hmep->hme_tmblkp));
	bzero((caddr_t)hmep->hme_rmblkp, sizeof (hmep->hme_rmblkp));
}


/*
 *	First check to see if it our device interrupting.
 */
static u_int
hmeintr(hmep)
register	struct	hme	*hmep;
{
	register u_int hmesbits;
	volatile struct hme_mif		*mifp;
	u_int mif_status;
	u_int	serviced = DDI_INTR_UNCLAIMED;


	mutex_enter(&hmep->hme_intrlock);

/* The status register auto-clears on read except for MIF Interrupt bit */
	hmesbits = hmep->hme_globregp->status;

	TRACE_1(TR_FAC_BE, TR_BE_INTR_START, "hmeintr start:  hmep %X", hmep);
#ifdef HMEDEBUG1

	if (hmedebug)
		hmerror(hmep->hme_dip, "hmeintr: start:  hmep %X status = %X",
							hmep, hmesbits);
#endif HMEDEBUG

/* Note: TINT is sometimes enabled in thr hmereclaim() */

	if ((hmesbits & (HMEG_STATUS_INTR | HMEG_STATUS_TINT)) == 0) {
						/* No interesting interrupt */
		mutex_exit(&hmep->hme_intrlock);
		TRACE_2(TR_FAC_BE, TR_BE_INTR_END,
		"hmeintr end: hmep %X serviced %d", hmep, serviced);
		return (serviced);
	}

	serviced = DDI_INTR_CLAIMED;

	if (!(hmep->hme_flags & HMERUNNING)) {
		mutex_exit(&hmep->hme_intrlock);
		hmeuninit(hmep);
#ifdef HMEDEBUG
	if (hmedebug)
		hmerror(hmep->hme_dip, "hmeintr: hme not running");
#endif
		return (serviced);
	}

	if (hmesbits & (HMEG_STATUS_FATAL_ERR | HMEG_STATUS_NONFATAL_ERR)) {
		if (hmesbits & HMEG_STATUS_FATAL_ERR) {
#ifdef HMEDEBUG
	if (hmedebug)
		hmerror(hmep->hme_dip,
			"hmeintr: fatal error: hmesbits = %X", hmesbits);
#endif
			(void) hme_fatal_err(hmep, hmesbits);
			mutex_exit(&hmep->hme_intrlock);
			(void) hmeinit(hmep);
			return (serviced);
		}
#ifdef HMEDEBUG
	if (hmedebug)
		hmerror(hmep->hme_dip, "hmeintr: non-fatal error: \
hmesbits = %X", hmesbits);
#endif
		(void) hme_nonfatal_err(hmep, hmesbits);
	}

	if (hmesbits & HMEG_STATUS_MIF_INTR) {
		mifp = hmep->hme_mifregp;
		mif_status = (mifp->mif_bsts >> 16);
		if (!(mif_status & PHY_BMSR_LNKSTS)) {
#ifdef HME_MIFPOLL_DEBUG
			if (hmedebug)
				hmerror(hmep->hme_dip,
				"hmeintr: mif interrupt: Link Down");
#endif
			hmep->hme_linkup_msg = 1;
			hmep->hme_mifpoll_flag = 1;
			mutex_exit(&hmep->hme_intrlock);
			(void) hme_check_link(hmep);
			return (serviced);
		}
		if (mif_status & PHY_BMSR_JABDET) {
			hmep->hme_jab++;
			if (hmedebug)
				hmerror(hmep->hme_dip,
				"hmeintr: mif interrupt: Jabber detected");
		}
		hme_start_mifpoll(hmep);
	}

	if (hmesbits & HMEG_STATUS_TX_ALL) {
		mutex_enter(&hmep->hme_xmitlock);
#ifdef HMEDEBUG1
	if (hmedebug)
		hmerror(hmep->hme_dip, "hmeintr: packet transmitted");
#endif
		hmereclaim(hmep);
		mutex_exit(&hmep->hme_xmitlock);
	}

	if (hmesbits & HMEG_STATUS_RINT) {
		volatile struct	hme_rmd	*rmdp;

		rmdp = hmep->hme_rnextp;
#ifdef HMEDEBUG1
	if (hmedebug)
		hmerror(hmep->hme_dip,
			"hmeintr: packet received: rmdp = %X", rmdp);
#endif

		/*
		 * Sync RMD before looking at it.
		 */
		HMESYNCIOPB(hmep, rmdp, sizeof (struct hme_rmd),
			DDI_DMA_SYNC_FORCPU);

		/*
		 * Loop through each RMD.
		 */
		while ((rmdp->rmd_flags & HMERMD_OWN) == 0) {
			hmeread(hmep, rmdp);
			/*
			 * Increment to next RMD.
			 */
			hmep->hme_rnextp = rmdp = NEXTRMD(hmep, rmdp);

			/*
			 * Sync the next RMD before looking at it.
			 */
			HMESYNCIOPB(hmep, rmdp, sizeof (struct hme_rmd),
				DDI_DMA_SYNC_FORCPU);
		}
	}

	mutex_exit(&hmep->hme_intrlock);

	TRACE_1(TR_FAC_BE, TR_BE_INTR_END, "hmeintr end:  hmep %X", hmep);

	return (serviced);
}

/*
 * Transmit completion reclaiming.
 */
static void
hmereclaim(hmep)
struct	hme	*hmep;
{
	volatile struct	hme_tmd	*tmdp;
	int	i;
#ifdef	LATER
	int			nbytes;
#endif	LATER

	tmdp = hmep->hme_tcurp;

#ifdef	LATER
	/*
	 * Sync TMDs before looking at it.
	 */
	if (hmep->hme_tnextp > hmep->hme_tcurp) {
		nbytes = ((hmep->hme_tnextp - hmep->hme_tcurp)
				* sizeof (struct hme_tmd));
		HMESYNCIOPB(hmep, tmdp, nbytes, DDI_DMA_SYNC_FORCPU);
	} else {
		nbytes = ((hmep->hme_tmdlimp - hmep->hme_tcurp)
				* sizeof (struct hme_tmd));
		HMESYNCIOPB(hmep, tmdp, nbytes, DDI_DMA_SYNC_FORCPU);
		nbytes = ((hmep->hme_tnextp - hmep->hme_tmdp)
				* sizeof (struct hme_tmd));
		HMESYNCIOPB(hmep, hmep->hme_tmdp, nbytes, DDI_DMA_SYNC_FORCPU);
	}
#endif	LATER

	/*
	 * Loop through each TMD.
	 */
	while ((tmdp->tmd_flags & (HMETMD_OWN)) == 0 &&
		(tmdp != hmep->hme_tnextp)) {

	/*
	 * count a chained packet only once.
	 */
		if (tmdp->tmd_flags & (HMETMD_SOP))
			hmep->hme_opackets++;

		i = tmdp - hmep->hme_tmdp;
#ifdef HMEDEBUG1
	if (hmedebug)
		hmerror(hmep->hme_dip,
			"hmereclaim: tmdp = %X index = %d", tmdp, i);
#endif
		if (hmep->hme_dvmaxh)
			(void) dvma_unload(hmep->hme_dvmaxh, 2 * i,
						(u_int) DONT_FLUSH);
		else {
			ddi_dma_free(hmep->hme_dmaxh[i]);
			hmep->hme_dmaxh[i] = NULL;
		}

		if (hmep->hme_tmblkp[i]) {
			freeb(hmep->hme_tmblkp[i]);
			hmep->hme_tmblkp[i] = NULL;
		}

		tmdp = NEXTTMD(hmep, tmdp);
	}

	if (tmdp != hmep->hme_tcurp) {
		/*
		 * we could reclaim some TMDs so turn off interupts
		 */
		hmep->hme_tcurp = tmdp;
		if (hmep->hme_wantw) {
			hmep->hme_globregp->intmask |= HMEG_MASK_TINT;
			hmewenable(hmep);
		}
	} else {
		/*
		 * enable TINTS: so that even if there is no further activity
		 * hmereclaim will get called
		 */
		if (hmep->hme_wantw)
			hmep->hme_globregp->intmask &= ~HMEG_MASK_TINT;
	}

}


/*
 * Send packet upstream.
 * Assume mp->b_rptr points to ether_header.
 */
static void
hmesendup(hmep, mp, acceptfunc)
struct	hme	*hmep;
mblk_t	*mp;
struct	hmestr	*(*acceptfunc)();
{
	int	type;
	struct	ether_addr	*dhostp, *shostp;
	struct	hmestr	*sbp, *nsbp;
	mblk_t	*nmp;
	ulong	isgroupaddr;

	TRACE_0(TR_FAC_BE, TR_BE_SENDUP_START, "hmesendup start");

	dhostp = &((struct ether_header *) mp->b_rptr)->ether_dhost;
	shostp = &((struct ether_header *) mp->b_rptr)->ether_shost;
	type = ((struct ether_header *) mp->b_rptr)->ether_type;

	isgroupaddr = dhostp->ether_addr_octet[0] & 01;

	/*
	 * While holding a reader lock on the linked list of streams structures,
	 * attempt to match the address criteria for each stream
	 * and pass up the raw M_DATA ("fastpath") or a DL_UNITDATA_IND.
	 */

	rw_enter(&hmestruplock, RW_READER);

	if ((sbp = (*acceptfunc)(hmestrup, hmep, type, dhostp)) == NULL) {
		rw_exit(&hmestruplock);
		freemsg(mp);
		TRACE_0(TR_FAC_BE, TR_BE_SENDUP_END, "hmesendup end");
		return;
	}

	/*
	 * Loop on matching open streams until (*acceptfunc)() returns NULL.
	 */
	for (; nsbp = (*acceptfunc)(sbp->sb_nextp, hmep, type, dhostp);
		sbp = nsbp)
		if (canput(sbp->sb_rq->q_next))
			if (nmp = dupmsg(mp)) {
				if ((sbp->sb_flags & HMESFAST) &&
							!isgroupaddr) {
					nmp->b_rptr +=
						sizeof (struct ether_header);
					putnext(sbp->sb_rq, nmp);
				} else if (sbp->sb_flags & HMESRAW)
					putnext(sbp->sb_rq, nmp);
				else if ((nmp = hmeaddudind(hmep, nmp, shostp,
						dhostp, type, isgroupaddr)))
						putnext(sbp->sb_rq, nmp);
			} else
				hmep->hme_allocbfail++;
		else
			hmep->hme_nocanput++;


	/*
	 * Do the last one.
	 */
	if (canput(sbp->sb_rq->q_next)) {
		if ((sbp->sb_flags & HMESFAST) && !isgroupaddr) {
			mp->b_rptr += sizeof (struct ether_header);
			putnext(sbp->sb_rq, mp);
		} else if (sbp->sb_flags & HMESRAW)
			putnext(sbp->sb_rq, mp);
		else if ((mp = hmeaddudind(hmep, mp, shostp, dhostp,
			type, isgroupaddr)))
			putnext(sbp->sb_rq, mp);
	} else {
		freemsg(mp);
		hmep->hme_nocanput++;
	}

	rw_exit(&hmestruplock);
	TRACE_0(TR_FAC_BE, TR_BE_SENDUP_END, "hmesendup end");
}

/*
 * Test upstream destination sap and address match.
 */
static struct hmestr *
hmeaccept(sbp, hmep, type, addrp)
register	struct	hmestr	*sbp;
register	struct	hme	*hmep;
int	type;
struct	ether_addr	*addrp;
{
	int	sap;
	int	flags;

	for (; sbp; sbp = sbp->sb_nextp) {
		sap = sbp->sb_sap;
		flags = sbp->sb_flags;

		if ((sbp->sb_hmep == hmep) && HMESAPMATCH(sap, type, flags))
			if ((ether_cmp(addrp, &hmep->hme_ouraddr) == 0) ||
				(ether_cmp(addrp, &etherbroadcastaddr) == 0) ||
				(flags & HMESALLPHYS) ||
				hmemcmatch(sbp, addrp))
				return (sbp);
	}

	return (NULL);
}

/*
 * Test upstream destination sap and address match for HMESALLPHYS only.
 */
/* ARGSUSED3 */
static struct hmestr *
hmepaccept(sbp, hmep, type, addrp)
register	struct	hmestr	*sbp;
register	struct	hme	*hmep;
int	type;
struct	ether_addr	*addrp;
{
	int	sap;
	int	flags;

	for (; sbp; sbp = sbp->sb_nextp) {
		sap = sbp->sb_sap;
		flags = sbp->sb_flags;

		if ((sbp->sb_hmep == hmep) &&
			HMESAPMATCH(sap, type, flags) &&
			(flags & HMESALLPHYS))
			return (sbp);
	}

	return (NULL);
}

static void
hmesetipq(hmep)
struct	hme	*hmep;
{
	struct	hmestr	*sbp;
	int	ok = 1;
	queue_t	*ipq = NULL;

	rw_enter(&hmestruplock, RW_READER);

	for (sbp = hmestrup; sbp; sbp = sbp->sb_nextp)
		if (sbp->sb_hmep == hmep) {
			if (sbp->sb_flags & (HMESALLPHYS|HMESALLSAP))
				ok = 0;
			if (sbp->sb_sap == ETHERTYPE_IP)
				if (ipq == NULL)
					ipq = sbp->sb_rq;
				else
					ok = 0; }

	rw_exit(&hmestruplock);

	if (ok)
		hmep->hme_ipq = ipq;
	else
		hmep->hme_ipq = NULL;
}

/*
 * Prefix msg with a DL_UNITDATA_IND mblk and return the new msg.
 */
static mblk_t *
hmeaddudind(hmep, mp, shostp, dhostp, type, isgroupaddr)
struct	hme	*hmep;
mblk_t	*mp;
struct	ether_addr	*shostp, *dhostp;
int	type;
ulong	isgroupaddr;
{
	dl_unitdata_ind_t	*dludindp;
	struct	hmedladdr	*dlap;
	mblk_t	*nmp;
	int	size;

	TRACE_0(TR_FAC_BE, TR_BE_ADDUDIND_START, "hmeaddudind start");

	mp->b_rptr += sizeof (struct ether_header);

	/*
	 * Allocate an M_PROTO mblk for the DL_UNITDATA_IND.  */
	size = sizeof (dl_unitdata_ind_t) + HMEADDRL + HMEADDRL;
	if ((nmp = allocb(HMEHEADROOM + size, BPRI_LO)) == NULL) {
		hmep->hme_allocbfail++;
		hmep->hme_ierrors++;
		if (hmedebug)
			hmerror(hmep->hme_dip, "allocb failed");
		freemsg(mp);
		TRACE_0(TR_FAC_BE, TR_BE_ADDUDIND_END, "hmeaddudind end");
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
	dludindp->dl_dest_addr_length = HMEADDRL;
	dludindp->dl_dest_addr_offset = sizeof (dl_unitdata_ind_t);
	dludindp->dl_src_addr_length = HMEADDRL;
	dludindp->dl_src_addr_offset = sizeof (dl_unitdata_ind_t) + HMEADDRL;
	dludindp->dl_group_address = isgroupaddr;

	dlap = (struct hmedladdr *) (nmp->b_rptr + sizeof (dl_unitdata_ind_t));
	ether_copy(dhostp, &dlap->dl_phys);
	dlap->dl_sap = (u_short) type;

	dlap = (struct hmedladdr *) (nmp->b_rptr + sizeof (dl_unitdata_ind_t)
		+ HMEADDRL);
	ether_copy(shostp, &dlap->dl_phys);
	dlap->dl_sap = (u_short) type;

	/*
	 * Link the M_PROTO and M_DATA together.
	 */
	nmp->b_cont = mp;
	TRACE_0(TR_FAC_BE, TR_BE_ADDUDIND_END, "hmeaddudind end");
	return (nmp);
}

/*
 * Return TRUE if the given multicast address is one
 * of those that this particular Stream is interested in.
 */
static
hmemcmatch(sbp, addrp)
register	struct	hmestr	*sbp;
register	struct	ether_addr	*addrp;
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
	if (sbp->sb_flags & HMESALLMULTI)
		return (1);

	/*
	 * Return FALSE if no multicast addresses enabled for this Stream.
	 */
	if (sbp->sb_mccount == 0)
		return (0);

	/*
	 * Otherwise, find it in the table.
	 */

	mccount = sbp->sb_mccount;
	mctab = sbp->sb_mctab;

	for (i = 0; i < mccount; i++)
		if (!ether_cmp(addrp, &mctab[i]))
			return (1);

	return (0);
}

/*
 * Handle interrupts for fatal errors
 * Need reinitialization of the ENET channel.
 */
static void
hme_fatal_err(hmep, hmesbits)
struct	hme	*hmep;
u_int	hmesbits;
{

	dev_info_t		*dip = hmep->hme_dip;

	if (hmesbits & HMEG_STATUS_SLV_PAR_ERR) {
		if (hmedebug)
			hmerror(dip, "sbus slave parity error");
		hmep->hme_slvparerr++;
	}

	if (hmesbits & HMEG_STATUS_SLV_ERR_ACK) {
		if (hmedebug)
			hmerror(dip, "sbus slave error ack");
		hmep->hme_slverrack++;
	}

	if (hmesbits & HMEG_STATUS_TX_TAG_ERR) {
		if (hmedebug)
			hmerror(dip, "tx tag error");
		hmep->hme_txtagerr++;
		hmep->hme_oerrors++;
	}

	if (hmesbits & HMEG_STATUS_TX_PAR_ERR) {
		if (hmedebug)
			hmerror(dip, "sbus tx parity error");
		hmep->hme_txparerr++;
		hmep->hme_oerrors++;
	}

	if (hmesbits & HMEG_STATUS_TX_LATE_ERR) {
		if (hmedebug)
			hmerror(dip, "sbus tx late error");
		hmep->hme_txlaterr++;
		hmep->hme_oerrors++;
	}

	if (hmesbits & HMEG_STATUS_TX_ERR_ACK) {
		if (hmedebug)
			hmerror(dip, "sbus tx error ack");
		hmep->hme_txerrack++;
		hmep->hme_oerrors++;
	}

	if (hmesbits & HMEG_STATUS_EOP_ERR) {
		if (hmedebug)
			hmerror(dip, "chained packet descriptor error");
		hmep->hme_eoperr++;
	}

	if (hmesbits & HMEG_STATUS_RX_TAG_ERR) {
		if (hmedebug)
			hmerror(dip, "rx tag error");
		hmep->hme_rxtagerr++;
		hmep->hme_ierrors++;
	}

	if (hmesbits & HMEG_STATUS_RX_PAR_ERR) {
		if (hmedebug)
			hmerror(dip, "sbus rx parity error");
		hmep->hme_rxparerr++;
		hmep->hme_ierrors++;
	}

	if (hmesbits & HMEG_STATUS_RX_LATE_ERR) {
		if (hmedebug)
			hmerror(dip, "sbus rx late error");
		hmep->hme_rxlaterr++;
		hmep->hme_ierrors++;
	}

	if (hmesbits & HMEG_STATUS_RX_ERR_ACK) {
		if (hmedebug)
			hmerror(dip, "sbus rx error ack");
		hmep->hme_rxerrack++;
		hmep->hme_ierrors++;
	}

}

/*
 * Handle interrupts regarding non-fatal errors.
 */
static void
hme_nonfatal_err(hmep, hmesbits)
struct	hme	*hmep;
u_int	hmesbits;
{
	dev_info_t		*dip = hmep->hme_dip;

	if (hmesbits & HMEG_STATUS_RX_DROP) {
		if (hmedebug)
			hmerror(dip, "rx pkt dropped/no free descriptor error");
		hmep->hme_missed++;
		hmep->hme_ierrors++;
	}

	if (hmesbits & HMEG_STATUS_DEFTIMR_EXP) {
#ifdef HME_STAT_DEBUG
		if (hmedebug)
			hmerror(dip, "defer timer expired");
#endif
		hmep->hme_defer++;
	}

	if (hmesbits & HMEG_STATUS_FSTCOLC_EXP) {
#ifdef HME_STAT_DEBUG
		if (hmedebug)
			hmerror(dip, "first collision counter expired");
#endif
		hmep->hme_fstcol += 256;
	}

	if (hmesbits & HMEG_STATUS_LATCOLC_EXP) {
		hmerror(dip, "late collision");
		hmep->hme_tlcol += 256;
		hmep->hme_oerrors += 256;
	}

	if (hmesbits & HMEG_STATUS_EXCOLC_EXP) {
#ifdef HME_STAT_DEBUG
		if (hmedebug)
			hmerror(dip, "retry error");
#endif
		hmep->hme_trtry += 256;
		hmep->hme_oerrors += 256;
	}

	if (hmesbits & HMEG_STATUS_NRMCOLC_EXP) {
#ifdef HME_STAT_DEBUG
		if (hmedebug)
			hmerror(dip, "first collision counter expired");
#endif
		hmep->hme_coll += 256;
	}

	if (hmesbits & HMEG_STATUS_MXPKTSZ_ERR) {
		if (hmedebug)
			hmerror(dip, "babble");
		hmep->hme_babl++;
		hmep->hme_oerrors++;
	}

	/*
	 * sampath:TBD: check HW:
	 * XXX Per Shimon, this error is fatal and the board needs to
	 * be reinitialized. Comments?
	 */
	if (hmesbits & HMEG_STATUS_TXFIFO_UNDR) {
		if (hmedebug)
			hmerror(dip, "tx fifo underflow");
		hmep->hme_uflo++;
		hmep->hme_oerrors++;
	}

	if (hmesbits & HMEG_STATUS_SQE_TST_ERR) {
		if (hmedebug)
			hmerror(dip, "sqe test error");
		hmep->hme_sqerr++;
	}

	if (hmesbits & HMEG_STATUS_RCV_CNT_EXP) {
		if (hmep->hme_rxcv_enable) {
			if (hmedebug)
				hmerror(dip, "code violation counter expired");
			hmep->hme_cvc += 256;
		}
	}

	if (hmesbits & HMEG_STATUS_RXFIFO_OVFL) {
		if (hmedebug)
			hmerror(dip, "rx fifo overflow");
		hmep->hme_oflo++;
		hmep->hme_ierrors++;
	}

	if (hmesbits & HMEG_STATUS_LEN_CNT_EXP) {
		if (hmedebug)
			hmerror(dip, "length error counter expired");
		hmep->hme_lenerr += 256;
		hmep->hme_ierrors += 256;
	}

	if (hmesbits & HMEG_STATUS_ALN_CNT_EXP) {
		if (hmedebug)
			hmerror(dip, "rx framing/alignment error");
		hmep->hme_fram += 256;
		hmep->hme_ierrors += 256;
	}

	if (hmesbits & HMEG_STATUS_CRC_CNT_EXP) {
		if (hmedebug)
			hmerror(dip, "rx crc error");
		hmep->hme_crc += 256;
		hmep->hme_ierrors += 256;
	}
}

static void
hmeread_slow(hmep, rmdp)
register	struct	hme	*hmep;
volatile	struct	hme_rmd	*rmdp;
{
	register	int	rmdi;
	u_int		dvma_rmdi, dvma_nrmdi;
	register	mblk_t	*bp, *nbp;
	volatile	struct	hme_rmd	*nrmdp;
	struct		ether_header	*ehp;
	queue_t		*ipq;
	int	len;
	int	nrmdi;
	ddi_dma_cookie_t c;

	TRACE_0(TR_FAC_BE, TR_BE_READ_START, "hmeread start");

	rmdi = rmdp - hmep->hme_rmdp;
	bp = hmep->hme_rmblkp[rmdi];
	nrmdp = NEXTRMD(hmep, hmep->hme_rlastp);
	hmep->hme_rlastp = nrmdp;
	nrmdi = nrmdp - hmep->hme_rmdp;
	len = (rmdp->rmd_flags & HMERMD_BUFSIZE) >> HMERMD_BUFSIZE_SHIFT;
	dvma_rmdi = HMERINDEX(rmdi);
	dvma_nrmdi = HMERINDEX(nrmdi);

/*
 * Check for short packet
 * and check for overflow packet also. The processing is the
 * same for both the cases - reuse the buffer. Update the Buffer overflow
 * counter.
 */
	if ((len < ETHERMIN) || (rmdp->rmd_flags & HMERMD_OVFLOW)) {
		if (len < ETHERMIN)
			hmep->hme_drop++;
		else
			hmep->hme_buff++;
		hmep->hme_ierrors++;
		nrmdp->rmd_addr = rmdp->rmd_addr;
		nrmdp->rmd_flags = (u_int)((HMEBUFSIZE << HMERMD_BUFSIZE_SHIFT)
							| HMERMD_OWN);
		hmep->hme_rmblkp[nrmdi] = bp;
		hmep->hme_rmblkp[rmdi] = NULL;
		HMESYNCIOPB(hmep, nrmdp, sizeof (struct hme_rmd),
			DDI_DMA_SYNC_FORDEV);
		TRACE_0(TR_FAC_BE, TR_BE_READ_END, "hmeread end");
		return;
	}


	/*
	 * Sync the received buffer before looking at it.
	 */
	ddi_dma_sync(hmep->hme_dmarh[dvma_rmdi], 0, len, DDI_DMA_SYNC_FORCPU);


	if ((nbp = allocb(HMEBUFSIZE, BPRI_LO))) {
		nbp->b_rptr = ROUNDUP2(nbp->b_rptr, HMEBURSTSIZE);

		/*
		 * tear down the old mapping then setup a new one
		 */
		ddi_dma_free(hmep->hme_dmarh[dvma_rmdi]);
		hmep->hme_dmarh[dvma_rmdi] = NULL;

		if (ddi_dma_addr_setup(hmep->hme_dip, (struct as *)0,
			(caddr_t)nbp->b_rptr, HMEBUFSIZE, DDI_DMA_RDWR,
			DDI_DMA_DONTWAIT, 0, &hme_dma_limits,
			&hmep->hme_dmarh[dvma_nrmdi]))

				panic("hme: ddi_dma_addr_setup of bufs failed");
		else {
			if (ddi_dma_htoc(hmep->hme_dmarh[dvma_nrmdi], 0, &c))

				panic("hme:  ddi_dma_htoc buf failed");
		}
		nrmdp->rmd_addr = (u_int)(c.dmac_address);
		nrmdp->rmd_flags = (u_int)((HMEBUFSIZE << HMERMD_BUFSIZE_SHIFT)
								| HMERMD_OWN);
		hmep->hme_rmblkp[nrmdi] = nbp;
		hmep->hme_rmblkp[rmdi] = NULL;
		hmep->hme_ipackets++;

		HMESYNCIOPB(hmep, nrmdp, sizeof (struct hme_rmd),
					DDI_DMA_SYNC_FORDEV);

/*  Add the First Byte offset to the b_rptr */
		bp->b_rptr += HME_FSTBYTE_OFFSET;
		bp->b_wptr = bp->b_rptr + len;
		ehp = (struct ether_header *)bp->b_rptr;
		ipq = hmep->hme_ipq;

		if ((ehp->ether_type == ETHERTYPE_IP) &&
		    ((ehp->ether_dhost.ether_addr_octet[0] & 01) == 0) &&
		    (ipq) &&
		    canput(ipq->q_next)) {
			bp->b_rptr += sizeof (struct ether_header);
			putnext(ipq, bp);
		} else {
			/* Strip the PADs for 802.3 */
			if (ehp->ether_type + sizeof (struct ether_header)
								< ETHERMIN)
				bp->b_wptr = bp->b_rptr
						+ sizeof (struct ether_header)
						+ ehp->ether_type;
			hmesendup(hmep, bp, hmeaccept);
		}
	} else {
		hmep->hme_dmarh[dvma_nrmdi] =
			hmep->hme_dmarh[dvma_rmdi];
		hmep->hme_dmarh[dvma_rmdi]  = NULL;

		nrmdp->rmd_addr = rmdp->rmd_addr;
		hmep->hme_rmblkp[nrmdi] = bp;
		hmep->hme_rmblkp[rmdi] = NULL;
		nrmdp->rmd_flags = (u_int)((HMEBUFSIZE << HMERMD_BUFSIZE_SHIFT)
								| HMERMD_OWN);
		HMESYNCIOPB(hmep, nrmdp, sizeof (struct hme_rmd),
					DDI_DMA_SYNC_FORDEV);

		hmep->hme_ierrors++;
		hmep->hme_allocbfail++;
		if (hmedebug)
			hmerror(hmep->hme_dip, "allocb fail");
	}

	TRACE_0(TR_FAC_BE, TR_BE_READ_END, "hmeread end");
}

#ifdef HME_SYNC_DEBUG
/* For Electron Debug */

int
hme_check_buffer(hmep, buf, len)
register	struct	hme	*hmep;
u_int		*buf;
int		len;
{
	int len1;
	int i;

	len1 = len >> 2;
	if (buf[len1-1] == 0xbaddcafe) {
		hmerror(hmep->hme_dip,
			"buffer %X not synced. pkt len = %X", buf, len);

		for (i = 0; i < len1; i++)
			cmn_err(CE_CONT, "%X ", buf[i]);
		cmn_err(CE_CONT, "\n");
		cmn_err(CE_CONT, "=========== End of BUffer ===========\n");
		cmn_err(CE_CONT, "\n");
		return (0);
	}
	return (1);

}
#endif HME_SYNC_DEBUG

static void
hmeread(hmep, rmdp)
register	struct	hme	*hmep;
volatile	struct	hme_rmd	*rmdp;
{
	register	int	rmdi;
	register	mblk_t	*bp, *nbp;
	u_int		dvma_rmdi, dvma_nrmdi;
	volatile 	struct	hme_rmd	*nrmdp;
	struct		ether_header	*ehp;
	queue_t		*ipq;
	int	len;
	int	nrmdi;
	ddi_dma_cookie_t	c;

	TRACE_0(TR_FAC_BE, TR_BE_READ_START, "hmeread start");

	if (hmep->hme_dvmaxh == NULL) {
		hmeread_slow(hmep, rmdp);
		return;
	}

	rmdi = rmdp - hmep->hme_rmdp;
	dvma_rmdi = HMERINDEX(rmdi);
	bp = hmep->hme_rmblkp[rmdi];
	nrmdp = NEXTRMD(hmep, hmep->hme_rlastp);
	hmep->hme_rlastp = nrmdp;
	nrmdi = nrmdp - hmep->hme_rmdp;
	dvma_nrmdi = HMERINDEX(rmdi);

	ASSERT(dvma_rmdi == dvma_nrmdi);


	/*
	 * HMERMD_OWN has been cleared by the Hapymeal hardware.
	 */
	len = (rmdp->rmd_flags & HMERMD_BUFSIZE) >> HMERMD_BUFSIZE_SHIFT;

/*
 * sampath: check for overflow packet also. The processing is the
 * same for both the cases - reuse the buffer. Update the Buffer overflow
 * counter.
 */
	if ((len < ETHERMIN) || (rmdp->rmd_flags & HMERMD_OVFLOW)) {
		if (len < ETHERMIN)
			hmep->hme_drop++;
		else
			hmep->hme_buff++;
		hmep->hme_ierrors++;
		nrmdp->rmd_addr = rmdp->rmd_addr;
		nrmdp->rmd_flags = (u_int)((HMEBUFSIZE << HMERMD_BUFSIZE_SHIFT)
								| HMERMD_OWN);
		HMESYNCIOPB(hmep, nrmdp, sizeof (struct hme_rmd),
			DDI_DMA_SYNC_FORDEV);
		hmep->hme_rmblkp[nrmdi] = bp;
		hmep->hme_rmblkp[rmdi] = NULL;
		TRACE_0(TR_FAC_BE, TR_BE_READ_END, "hmeread end");
		return;
	}

	dvma_unload(hmep->hme_dvmarh, 2 * dvma_rmdi, DDI_DMA_SYNC_FORKERNEL);

	if ((nbp = allocb(HMEBUFSIZE + HMEBURSTSIZE, BPRI_LO))) {
		nbp->b_rptr = ROUNDUP2(nbp->b_rptr, HMEBURSTSIZE);

		(void) dvma_kaddr_load(hmep->hme_dvmarh,
		    (caddr_t)nbp->b_rptr, HMEBUFSIZE, 2 * dvma_nrmdi, &c);

		nrmdp->rmd_addr = (u_int)(c.dmac_address);
		nrmdp->rmd_flags = (u_int)((HMEBUFSIZE << HMERMD_BUFSIZE_SHIFT)
								| HMERMD_OWN);
		HMESYNCIOPB(hmep, nrmdp, sizeof (struct hme_rmd),
							DDI_DMA_SYNC_FORDEV);

		hmep->hme_rmblkp[nrmdi] = nbp;
		hmep->hme_rmblkp[rmdi] = NULL;
		hmep->hme_ipackets++;

/* Add the First Byte offset to the b_rptr */
		bp->b_rptr += HME_FSTBYTE_OFFSET;
		bp->b_wptr = bp->b_rptr + len;
		ehp = (struct ether_header *)bp->b_rptr;
		ipq = hmep->hme_ipq;

		if ((ehp->ether_type == ETHERTYPE_IP) &&
		    ((ehp->ether_dhost.ether_addr_octet[0] & 01) == 0) &&
		    (ipq) &&
		    canput(ipq->q_next)) {
			bp->b_rptr += sizeof (struct ether_header);
			putnext(ipq, bp);
		} else {
			/* Strip the PADs for 802.3 */
			if (ehp->ether_type + sizeof (struct ether_header)
								< ETHERMIN)
				bp->b_wptr = bp->b_rptr
						+ sizeof (struct ether_header)
						+ ehp->ether_type;
			hmesendup(hmep, bp, hmeaccept);
		}
	} else {
		(void) dvma_kaddr_load(hmep->hme_dvmarh, (caddr_t)bp->b_rptr,
		    HMEBUFSIZE, 2 * dvma_nrmdi, &c);
		nrmdp->rmd_addr = (u_int)(c.dmac_address);
		hmep->hme_rmblkp[nrmdi] = bp;
		hmep->hme_rmblkp[rmdi] = NULL;
		nrmdp->rmd_flags = (u_int)((HMEBUFSIZE << HMERMD_BUFSIZE_SHIFT)
								| HMERMD_OWN);
		HMESYNCIOPB(hmep, nrmdp, sizeof (struct hme_rmd),
					DDI_DMA_SYNC_FORDEV);

		hmep->hme_ierrors++;
		hmep->hme_allocbfail++;
		if (hmedebug)
			hmerror(hmep->hme_dip, "allocb fail");
	}

	TRACE_0(TR_FAC_BE, TR_BE_READ_END, "hmeread end");
}

/*
 * Start xmit on any msgs previously enqueued on any write queues.
 */
static void
hmewenable(hmep)
struct	hme	*hmep;
{
	struct	hmestr	*sbp;
	queue_t	*wq;

	/*
	 * Order of wantw accesses is important.
	 */
	do {
		hmep->hme_wantw = 0;
		for (sbp = hmestrup; sbp; sbp = sbp->sb_nextp)
			if ((wq = WR(sbp->sb_rq))->q_first)
				qenable(wq);
	} while (hmep->hme_wantw);
}

/*
 * Initialize RMD.
 */
static void
hmermdinit(rmdp, dvma_addr)
volatile struct	hme_rmd	*rmdp;
u_long		dvma_addr;
{
	rmdp->rmd_addr = (u_int) dvma_addr;
	rmdp->rmd_flags = (u_int)((HMEBUFSIZE << HMERMD_BUFSIZE_SHIFT)
								| HMERMD_OWN);
}

/*VARARGS*/
static void
hme_display_msg(struct hme *hmep, dev_info_t *dip, char *fmt, ...)
{
	static	long	last;
	static	char	*lastfmt;
	char		msg_buffer[255];
	va_list		ap;

	mutex_enter(&hmelock);

	/*
	 * Don't print same error message too often.
	 */
	if ((last == (hrestime.tv_sec & ~1)) && (lastfmt == fmt)) {
		mutex_exit(&hmelock);
		return;
	}
	last = hrestime.tv_sec & ~1;
	lastfmt = fmt;

	va_start(ap, fmt);
	vsprintf(msg_buffer, fmt, ap);
	if (hmep->hme_linkup_msg)
		cmn_err(CE_CONT, "%s%d: %s\n", ddi_get_name(dip),
				ddi_get_instance(dip),
				msg_buffer);
	else
		cmn_err(CE_CONT, "?%s%d: %s\n", ddi_get_name(dip),
				ddi_get_instance(dip),
				msg_buffer);
	va_end(ap);

	mutex_exit(&hmelock);
}

/*VARARGS*/
static void
hmerror(dev_info_t *dip, char *fmt, ...)
{
	static	long	last;
	static	char	*lastfmt;
	char		msg_buffer[255];
	va_list		ap;

	mutex_enter(&hmelock);

	/*
	 * Don't print same error message too often.
	 */
	if ((last == (hrestime.tv_sec & ~1)) && (lastfmt == fmt)) {
		mutex_exit(&hmelock);
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

	mutex_exit(&hmelock);
}

/*
 * if this is the first init do not bother to save the
 * counters. They should hme 0, but do not count on it.
 */
static void
hmesavecntrs(hmep)
struct	hme *hmep;
{
	volatile struct	hme_bmac	*bmacp = hmep->hme_bmacregp;
	int	fecnt, aecnt, lecnt, rxcv;
	int	ltcnt, excnt;

	/* XXX What all gets added in ierrors and oerrors? */
	fecnt = bmacp->fecnt;
	bmacp->fecnt = 0;

	aecnt = bmacp->aecnt;
	hmep->hme_fram += aecnt;
	bmacp->aecnt = 0;

	lecnt = bmacp->lecnt;
	hmep->hme_lenerr += lecnt;
	bmacp->lecnt = 0;

	rxcv = bmacp->rxcv;
#ifdef HME_CODEVIOL_BUG
/* Ignore rxcv errors for FEPS 2.1 or earlier */
	if (!hmep->hme_rxcv_enable) {
		rxcv = 0;
	}
#endif
	hmep->hme_cvc += rxcv;
	bmacp->rxcv = 0;

	ltcnt = bmacp->ltcnt;
	hmep->hme_tlcol += ltcnt;
	bmacp->ltcnt = 0;

	excnt = bmacp->excnt;
	hmep->hme_trtry += excnt;
	bmacp->excnt = 0;

	hmep->hme_crc += fecnt;
	hmep->hme_ierrors += (fecnt + aecnt + lecnt);
	hmep->hme_oerrors += (ltcnt + excnt);
	hmep->hme_coll += (bmacp->nccnt + ltcnt);
	bmacp->nccnt = 0;
}

/*
 * ndd support functions to get/set parameters
 */
/* Free the Named Dispatch Table by calling hme_nd_free */
staticf void
hme_param_cleanup(hmep)
struct hme	*hmep;
{
	if (hmep->hme_g_nd)
		(void) hme_nd_free(&hmep->hme_g_nd);
}

/*
 * Extracts the value from the hme parameter array and prints the
 * parameter value. cp points to the required parameter.
 */
static int
hme_param_get(q, mp, cp)
	queue_t * q;
	mblk_t	* mp;
	caddr_t cp;
{
	hmeparam_t	* hmepa = (hmeparam_t *)ALIGN32(cp);

#ifdef  lint
	q = q;
#endif
	(void) mi_mpprintf(mp, "%ld", hmepa->hme_param_val);
	return (0);
}

/*
 * Register each element of the parameter array with the
 * named dispatch handler. Each element is loaded using
 * hme_nd_load()
*/
static int
hme_param_register(hmep, hmepa, cnt)
struct hme	*hmep;
reg	hmeparam_t	* hmepa;
int	cnt;		/* cnt gives the count of the number of */
			/* elements present in the parameter array */
{
	int i;

#ifdef  lint
	cnt = cnt;
#endif
	/* First 4 elements are read-only */
	for (i = 0; i < 4; i++, hmepa++)
		if (!hme_nd_load(&hmep->hme_g_nd, hmepa->hme_param_name,
			(pfi_t)hme_param_get, (pfi_t)0, (caddr_t)hmepa)) {
			(void) hme_nd_free(&hmep->hme_g_nd);
			return (false);
		}
	/* Next 9 elements are read and write */
	for (i = 0; i < 9; i++, hmepa++)
		if (hmepa->hme_param_name && hmepa->hme_param_name[0]) {
			if (!hme_nd_load(&hmep->hme_g_nd,
				hmepa->hme_param_name,
				(pfi_t)	hme_param_get,
				(pfi_t)hme_param_set, (caddr_t)hmepa)) {
				(void) hme_nd_free(&hmep->hme_g_nd);
				return (false);
			}
		}
	/* last 10 elements are read-only */
	for (i = 0; i < 10; i++, hmepa++)
		if (!hme_nd_load(&hmep->hme_g_nd, hmepa->hme_param_name,
			(pfi_t)hme_param_get, (pfi_t)0, (caddr_t)hmepa)) {
			(void) hme_nd_free(&hmep->hme_g_nd);
			return (false);
		}
	/* register "instance" parameter as read/write */
	for (i = 0; i < 3; i++, hmepa++)
		if (hmepa->hme_param_name && hmepa->hme_param_name[0]) {
			if (!hme_nd_load(&hmep->hme_g_nd,
				hmepa->hme_param_name,
				(pfi_t)	hme_param_get,
				(pfi_t)hme_param_set, (caddr_t)hmepa)) {
				(void) hme_nd_free(&hmep->hme_g_nd);
				return (false);
			}
		}

	return (true);
}

/*
 * Sets the hme parameter to the value in the hme_param_register using
 * hme_nd_load().
 */
static int
hme_param_set(q, mp, value, cp)
	queue_t * q;
	mblk_t	* mp;
	char * value;
	caddr_t cp;
{
	char * end;
	long new_value;
	hmeparam_t	* hmepa = (hmeparam_t *)ALIGN32(cp);

#ifdef  lint
	q = q;
	mp = mp;
#endif
	new_value = mi_strtol(value, &end, 10);
	if (end == value || new_value < hmepa->hme_param_min ||
		new_value > hmepa->hme_param_max) {
			return (EINVAL);
	}
	hmepa->hme_param_val = new_value;
	return (0);

}

/*
	functions from mi.c for ndd parameter handling
 */


#define	ISDIGIT(ch)	((ch) >= '0' && (ch) <= '9')
#define	ISUPPER(ch)	((ch) >= 'A' && (ch) <= 'Z')
#define	tolower(ch)	('a' + ((ch) - 'A'))

static caddr_t
mi_alloc(size, pri)
	uint	size;
	int	pri;
{
reg	MBLKP	mp;

	if ((mp = allocb(size + sizeof (MBLKP), pri)) != NULL) {
		((MBLKP *)mp->b_rptr)[0] = mp;
		mp->b_rptr += sizeof (MBLKP);
		mp->b_wptr = mp->b_rptr + size;
		return ((caddr_t)mp->b_rptr);
	}
	return (nil(caddr_t));
}

static void
mi_free(ptr)
	char	* ptr;
{
	MBLKP	* mpp;

	if ((mpp = (MBLKP *)ptr) && mpp[-1])
		freeb(mpp[-1]);
}

#define	USE_STDARG

static int
mi_iprintf(fmt, ap, putc_func, cookie)
reg	char	* fmt;
	va_list	ap;
	pfi_t	putc_func;
	char	* cookie;
{
reg	int	base;
	char	buf[(sizeof (long) * 3) + 1];
static	char	hex_val[] = "0123456789abcdef";
	int	ch;
	int	count;
	char	* cp1;
	int	digits;
reg	char	* fcp;
	boolean_t	is_long;
	ulong	uval;
	long	val;
	boolean_t	zero_filled;

	if (!fmt)
		return (-1);
	count = 0;
	while (*fmt) {
		if (*fmt != '%' || *++fmt == '%') {
			count += (*putc_func)(cookie, *fmt++);
			continue;
		}
		if (*fmt == '0') {
			zero_filled = true;
			fmt++;
			if (!*fmt)
				break;
		} else
			zero_filled = false;
		base = 0;
		for (digits = 0; ISDIGIT(*fmt); fmt++) {
			digits *= 10;
			digits += (*fmt - '0');
		}
		if (!*fmt)
			break;
		is_long = false;
		if (*fmt == 'l') {
			is_long = true;
			fmt++;
		}
		if (!*fmt)
			break;
		ch = *fmt++;
		if (ISUPPER(ch)) {
			ch = tolower(ch);
			is_long = true;
		}
		switch (ch) {
		case 'c':
			count += (*putc_func)(cookie, va_arg(ap, int *));
			continue;
		case 'd':
			base = 10;
			break;
		case 'm':	/* Print out memory, 2 hex chars per byte */
			if (is_long)
				fcp = va_arg(ap, char *);
			else {
				if ((cp1 = va_arg(ap, char *)) != NULL)
					fcp = (char *)cp1;
				else
					fcp = nilp(char);
			}
			if (!fcp) {
				for (fcp = (char *)"(NULL)"; *fcp; fcp++)
					count += (*putc_func)(cookie, *fcp);
			} else {
				while (digits--) {
					int u1 = *fcp++ & 0xFF;
					count += (*putc_func)(cookie,
							hex_val[(u1>>4)& 0xF]);
					count += (*putc_func)(cookie,
							hex_val[u1& 0xF]);
				}
			}
			continue;
		case 'o':
			base = 8;
			break;
		case 'x':
			base = 16;
			break;
		case 's':
			if (is_long)
				fcp = va_arg(ap, char *);
			else {
				if ((cp1 = va_arg(ap, char *)) != NULL)
					fcp = (char *)cp1;
				else
					fcp = nilp(char);
			}
			if (!fcp)
				fcp = (char *)"(NULL)";
			while (*fcp) {
				count += (*putc_func)(cookie, *fcp++);
				if (digits && --digits == 0)
					break;
			}
			while (digits > 0) {
				count += (*putc_func)(cookie, ' ');
				digits--;
			}
			continue;
		case 'u':
			base = 10;
			break;
		default:
			return (count);
		}
		if (is_long)
			val = va_arg(ap, long);
		else
			val = va_arg(ap, int);
		if (base == 10 && ch != 'u') {
			if (val < 0) {
				count += (*putc_func)(cookie, '-');
				val = -val;
			}
			uval = val;
		} else {
			if (is_long)
				uval = val;
			else
				uval = (unsigned)val;
		}
		/* Hand overload/restore the register variable 'fmt' */
		cp1 = fmt;
		fmt = A_END(buf);
		*--fmt = '\0';
		do {
			if (fmt > buf)
				*--fmt = hex_val[uval % base];
			if (digits && --digits == 0)
				break;
		} while (uval /= base);
		if (zero_filled) {
			while (digits > 0 && fmt > buf) {
				*--fmt = '0';
				digits--;
			}
		}
		while (*fmt)
			count += (*putc_func)(cookie, *fmt++);
		fmt = cp1;
	}
	return (count);
}


#ifdef USE_STDARG
static int
mi_mpprintf(MBLKP mp, char * fmt, ...)
#else
static int
mi_mpprintf(mp, fmt, va_alist)
	MBLKP	mp;
	char	* fmt;
	va_dcl
#endif
{
	va_list	ap;
	int	count = -1;
#ifdef USE_STDARG
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	if (mp) {
		count = mi_iprintf(fmt, ap, (pfi_t)mi_mpprintf_putc,
								(char *)mp);
		if (count != -1)
			(void) mi_mpprintf_putc((char *)mp, '\0');
	}
	va_end(ap);
	return (count);
}


static long
mi_strtol(str, ptr, base)
	char	* str;
	char	** ptr;
	int	base;
{
reg	char	* cp;
	long	digits, value;
	boolean_t	is_negative;

	cp = str;
	while (*cp == ' ' || *cp == '\t' || *cp == '\n')
		cp++;
	is_negative = (*cp == '-');
	if (is_negative)
		cp++;
	if (base == 0) {
		base = 10;
		if (*cp == '0') {
			base = 8;
			cp++;
			if (*cp == 'x' || *cp == 'X') {
				base = 16;
				cp++;
			}
		}
	}
	value = 0;
	for (; *cp; cp++) {
		if (*cp >= '0' && *cp <= '9')
			digits = *cp - '0';
		else if (*cp >= 'a' && *cp <= 'f')
			digits = *cp - 'a' + 10;
		else if (*cp >= 'A' && *cp <= 'F')
			digits = *cp - 'A' + 10;
		else
			break;
		if (digits >= base)
			break;
		value = (value * base) + digits;
	}
	if (ptr)
		*ptr = cp;
	if (is_negative)
		value = -value;
	return (value);
}

static int
mi_mpprintf_putc(cookie, ch)
	char	* cookie;
	int	ch;
{
reg	MBLKP	mp = (MBLKP)ALIGN32(cookie);

	while (mp->b_cont)
		mp = mp->b_cont;
	if (mp->b_wptr >= mp->b_datap->db_lim) {
		mp->b_cont = allocb(1024, BPRI_HI);
		mp = mp->b_cont;
		if (!mp)
			return (0);
	}
	*mp->b_wptr++ = (unsigned char)ch;
	return (1);
}


static int
mi_strcmp(cp1, cp2)
reg	char	* cp1;
reg	char	* cp2;
{
	while (*cp1++ == *cp2++) {
		if (!cp2[-1])
			return (0);
	}
	return (((uint)cp2[-1]  & 0xFF) - ((uint)cp1[-1] & 0xFF));
}

/*
	functions from ndd.c for parameter handling
 */

#define	ND_BASE		('N' << 8)	/* base */
#define	ND_GET		(ND_BASE + 0)	/* Get a value */
#define	ND_SET		(ND_BASE + 1)	/* Set a value */


/* Named dispatch table entry */
typedef	struct	nde_s {
	char	* nde_name;
	pfi_t	nde_get_pfi;
	pfi_t	nde_set_pfi;
	caddr_t	nde_data;
} NDE;

/* Name dispatch table */
typedef	struct	nd_s {
	int	nd_free_count; /* number of unused nd table entries */
	int	nd_size;	/* size (in bytes) of current table */
	NDE	* nd_tbl;	/* pointer to table in heap */
} ND;

typedef	struct iocblk	* IOCP;

#define	NDE_ALLOC_COUNT	4
#define	NDE_ALLOC_SIZE	(sizeof (NDE) * NDE_ALLOC_COUNT)


/* Free the table pointed to by 'ndp' */
static void
hme_nd_free(nd_pparam)
	caddr_t	* nd_pparam;
{
	ND	* nd;

	if ((nd = (ND *)ALIGN32(*nd_pparam)) != NULL) {
		if (nd->nd_tbl)
			mi_free((char *)nd->nd_tbl);
		mi_free((char *)nd);
		*nd_pparam = nil(caddr_t);
	}
}

static int
hme_nd_getset(q, nd_param, mp)
	queue_t	* q;
	caddr_t	nd_param;
reg	MBLKP	mp;
{
	int	err;
	IOCP	iocp;
	MBLKP	mp1;
	ND	* nd;
reg	NDE	* nde;
	char	* valp;
	long	avail;

	if (!nd_param)
		return (false);
	nd = (ND *)ALIGN32(nd_param);
	iocp = (IOCP)ALIGN32(mp->b_rptr);
	if ((iocp->ioc_count == 0) || !(mp1 = mp->b_cont)) {
		mp->b_datap->db_type = M_IOCACK;
		iocp->ioc_count = 0;
		iocp->ioc_error = EINVAL;
		return (true);
	}
	/*
	 * NOTE - logic throughout nd_xxx assumes single data block for ioctl.
	 *	However, existing code sends in some big buffers.
	 */
	avail = iocp->ioc_count;
	if (mp1->b_cont) {
		freemsg(mp1->b_cont);
		mp1->b_cont = nil(MBLKP);
	}

	mp1->b_datap->db_lim[-1] = '\0';	/* Force null termination */
	valp = (char *)mp1->b_rptr;
	for (nde = nd->nd_tbl; ; nde++) {
		if (!nde->nde_name)
			return (false);
		if (mi_strcmp(nde->nde_name, valp) == 0)
			break;
	}
	err = EINVAL;
	while (*valp++)
		noop;
	if (!*valp || valp >= (char *)mp1->b_wptr)
		valp = nilp(char);
	switch (iocp->ioc_cmd) {
	case ND_GET:
/*
 * (temporary) hack: "*valp" is size of user buffer for copyout. If result
 * of action routine is too big, free excess and return ioc_rval as buffer
 * size needed.  Return as many mblocks as will fit, free the rest.  For
 * backward compatibility, assume size of original ioctl buffer if "*valp"
 * bad or not given.
 */
		if (valp)
			avail = mi_strtol(valp, (char **)0, 10);
		/* We overwrite the name/value with the reply data */
		{
			mblk_t *mp2 = mp1;

			while (mp2) {
				mp2->b_wptr = mp2->b_rptr;
				mp2 = mp2->b_cont;
			}
		}
		err = (*nde->nde_get_pfi)(q, mp1, nde->nde_data);
		if (!err) {
			int	size_out;
			int	excess;

			iocp->ioc_rval = 0;

			/* Tack on the null */
			(void) mi_mpprintf_putc((char *)mp1, '\0');
			size_out = msgdsize(mp1);
			excess = size_out - avail;
			if (excess > 0) {
				iocp->ioc_rval = size_out;
				size_out -= excess;
				adjmsg(mp1, -(excess + 1));
				(void) mi_mpprintf_putc((char *)mp1, '\0');
			}
			iocp->ioc_count = size_out;
		}
		break;

	case ND_SET:
		if (valp) {
			err = (*nde->nde_set_pfi)(q, mp1, valp, nde->nde_data);
			iocp->ioc_count = 0;
			freemsg(mp1);
			mp->b_cont = nil(MBLKP);
		}
		break;

	default:
		break;
	}
	iocp->ioc_error = err;
	mp->b_datap->db_type = M_IOCACK;
	return (true);
}

/* ARGSUSED */
staticf int
nd_get_default(q, mp, data)
	queue_t	* q;
	MBLKP	mp;
	caddr_t	data;
{
	return (EACCES);
}

#ifdef notdef
/*
 * This routine may be used as the get dispatch routine in nd tables
 * for long variables.  To use this routine instead of a module
 * specific routine, call hme_nd_load as
 *	hme_nd_load(&nd_ptr, "name", hme_nd_get_long, set_pfi, &long_variable)
 * The name of the variable followed by a space and the value of the
 * variable will be printed in response to a get_status call.
 */
/* ARGSUSED */
static int
hme_nd_get_long(q, mp, data)
	queue_t	* q;
	MBLKP	mp;
	caddr_t	data;
{
	ulong	* lp;

	lp = (ulong *)ALIGN32(data);
	(void) mi_mpprintf(mp, "%ld", *lp);
	return (0);
}
#endif

/* ARGSUSED */
staticf int
nd_get_names(q, mp, nd_param)
	queue_t	* q;
	MBLKP	mp;
	caddr_t	nd_param;
{
	ND	* nd;
reg	NDE	* nde;
	char	* rwtag;
	boolean_t	get_ok, set_ok;

	nd = (ND *)ALIGN32(nd_param);
	if (!nd)
		return (ENOENT);
	for (nde = nd->nd_tbl; nde->nde_name; nde++) {
		get_ok = nde->nde_get_pfi != nd_get_default;
		set_ok = nde->nde_set_pfi != nd_set_default;
		if (get_ok) {
			if (set_ok)
				rwtag = "read and write";
			else
				rwtag = "read only";
		} else if (set_ok)
			rwtag = "write only";
		else
			rwtag = "no read or write";
		(void) mi_mpprintf(mp, "%s (%s)", nde->nde_name, rwtag);
	}
	return (0);
}

/*
 * Load 'name' into the named dispatch table pointed to by 'ndp'.
 * 'ndp' should be the address of a char pointer cell.  If the table
 * does not exist (*ndp == 0), a new table is allocated and 'ndp'
 * is stuffed.  If there is not enough space in the table for a new
 * entry, more space is allocated.
 */
static boolean_t
hme_nd_load(nd_pparam, name, get_pfi, set_pfi, data)
	caddr_t	* nd_pparam;
	char	* name;
	pfi_t	get_pfi;
	pfi_t	set_pfi;
	caddr_t	data;
{
	ND	* nd;
reg	NDE	* nde;

	if (!nd_pparam)
		return (false);
	if ((nd = (ND *)ALIGN32(*nd_pparam)) == NULL) {
		if ((nd = (ND *)ALIGN32(mi_alloc(sizeof (ND), BPRI_MED)))
		    == NULL)
			return (false);
		bzero((caddr_t)nd, sizeof (ND));
		*nd_pparam = (caddr_t)nd;
	}
	if (nd->nd_tbl) {
		for (nde = nd->nd_tbl; nde->nde_name; nde++) {
			if (mi_strcmp(name, nde->nde_name) == 0)
				goto fill_it;
		}
	}
	if (nd->nd_free_count <= 1) {
		if ((nde = (NDE *)ALIGN32(mi_alloc(nd->nd_size +
					NDE_ALLOC_SIZE, BPRI_MED))) == NULL)
			return (false);
		bzero((char *)nde, nd->nd_size + NDE_ALLOC_SIZE);
		nd->nd_free_count += NDE_ALLOC_COUNT;
		if (nd->nd_tbl) {
			bcopy((char *)nd->nd_tbl, (char *)nde, nd->nd_size);
			mi_free((char *)nd->nd_tbl);
		} else {
			nd->nd_free_count--;
			nde->nde_name = "?";
			nde->nde_get_pfi = nd_get_names;
			nde->nde_set_pfi = nd_set_default;
		}
		nde->nde_data = (caddr_t)nd;
		nd->nd_tbl = nde;
		nd->nd_size += NDE_ALLOC_SIZE;
	}
	for (nde = nd->nd_tbl; nde->nde_name; nde++)
		noop;
	nd->nd_free_count--;
fill_it:
	nde->nde_name = name;
	nde->nde_get_pfi = get_pfi ? get_pfi : nd_get_default;
	nde->nde_set_pfi = set_pfi ? set_pfi : nd_set_default;
	nde->nde_data = data;
	return (true);
}

/* ARGSUSED */
staticf int
nd_set_default(q, mp, value, data)
	queue_t	* q;
	MBLKP	mp;
	char	* value;
	caddr_t	data;
{
	return (EACCES);
}

#ifdef notdef
/* ARGSUSED */
static int
hme_nd_set_long(q, mp, value, data)
	queue_t	* q;
	MBLKP	mp;
	char	* value;
	caddr_t	data;
{
	char	* end;
	ulong	* lp;
	long	new_value;

	new_value = mi_strtol(value, &end, 10);
	if (end == value)
		return (EINVAL);
	lp = (ulong *)ALIGN32(data);
	*lp = new_value;
	return (0);
}
#endif
