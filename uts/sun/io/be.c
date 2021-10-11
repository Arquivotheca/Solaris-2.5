/*
 * Copyright (c) 1992, 1994 by Sun Microsystems, Inc.
 * All Rights Reserved
 */

#pragma ident	"@(#)be.c 1.42     95/10/19 SMI"

/*
 * SunOS MT STREAMS BigMAC Ethernet Device Driver
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
#include	<sys/dlpi.h>
#include	<sys/ethernet.h>
#include	<sys/bmac.h>
#include	<sys/qec.h>
#include	<sys/be.h>

typedef int	(*fptri_t)();
typedef void	(*fptrv_t)();

/*
 * Function prototypes.
 */
static	int beidentify(dev_info_t *);
static	int beattach(dev_info_t *, ddi_attach_cmd_t);
static	int bedetach(dev_info_t *, ddi_detach_cmd_t);
static	int bebmacstop(struct be *);
#ifndef	MPSAS
static	void bestatinit(struct be *);
#endif	MPSAS
static	int beinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static	void beallocthings(struct be *);
static	void befreebufs(struct be *);
static	int beopen(queue_t *, dev_t *, int, int, cred_t *);
static	int beclose(queue_t *);
static	int bewput(queue_t *, mblk_t *);
static	int bewsrv(queue_t *);
static	void beproto(queue_t *, mblk_t *);
static	void beioctl(queue_t *, mblk_t *);
static	void be_dl_ioc_hdr_info(queue_t *, mblk_t *);
static	void beareq(queue_t *, mblk_t *);
static	void bedreq(queue_t *, mblk_t *);
static	void bedodetach(struct bestr *);
static	void bebreq(queue_t *, mblk_t *);
static	void beubreq(queue_t *, mblk_t *);
static	void beireq(queue_t *, mblk_t *);
static	void beponreq(queue_t *, mblk_t *);
static	void bepoffreq(queue_t *, mblk_t *);
static	void beemreq(queue_t *, mblk_t *);
static	void bedmreq(queue_t *, mblk_t *);
static	void bepareq(queue_t *, mblk_t *);
static	void bespareq(queue_t *, mblk_t *);
static	void beudreq(queue_t *, mblk_t *);
static	int bestart(queue_t *, mblk_t *, struct be *);
static	u_int beintr();
static	void bewenable(struct be *);
static	void bereclaim(struct be *);
static	int beinit(struct be *);
static	void beuninit(struct be *bep);
static	void bermdinit(volatile struct qmd *, u_long);
static	void berror(dev_info_t *dip, char *fmt, ...);
static	void beqecerr(struct be *, u_int);
static	void bebmacerr(struct be *);
static	u_int betxreset(struct be *);
static	u_int berxreset(struct be *);
static	mblk_t *beaddudind(struct be *, mblk_t *, struct ether_addr *,
	struct ether_addr *, int, ulong);
static	struct bestr *beaccept(struct bestr *, struct be *, int,
	struct ether_addr *);
static	struct bestr *bepaccept(struct bestr *, struct be *, int,
	struct	ether_addr *);
static	void besetipq(struct be *);
static	int bemcmatch(struct bestr *, struct ether_addr *);
static	void besendup(struct be *, mblk_t *, struct bestr * (*)());
static 	void beread(struct be *, volatile struct qmd *);
static	void besavecntrs(struct be *);
static void	 be_check_link(struct be *);
static void	 be_try_speed(struct be *);
static void	 be_force_speed(struct be *);

static	struct	module_info	beminfo = {
	BEIDNUM,	/* mi_idnum */
	BENAME,		/* mi_idname */
	BEMINPSZ,	/* mi_minpsz */
	BEMAXPSZ,	/* mi_maxpsz */
	BEHIWAT,	/* mi_hiwat */
	BELOWAT		/* mi_lowat */
};

static	struct	qinit	berinit = {
	NULL,		/* qi_putp */
	NULL,		/* qi_srvp */
	beopen,		/* qi_qopen */
	beclose,	/* qi_qclose */
	NULL,		/* qi_qadmin */
	&beminfo,	/* qi_minfo */
	NULL		/* qi_mstat */
};

static	struct	qinit	bewinit = {
	bewput,		/* qi_putp */
	bewsrv,		/* qi_srvp */
	NULL,		/* qi_qopen */
	NULL,		/* qi_qclose */
	NULL,		/* qi_qadmin */
	&beminfo,	/* qi_minfo */
	NULL		/* qi_mstat */
};

static struct	streamtab	be_info = {
	&berinit,	/* st_rdinit */
	&bewinit,	/* st_wrinit */
	NULL,		/* st_muxrinit */
	NULL		/* st_muxwrinit */
};

static	struct	cb_ops	cb_be_ops = {
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
	&be_info,	/* cb_stream */
	D_MP		/* cb_flag */
};

static	struct	dev_ops	be_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	beinfo,			/* devo_getinfo */
	beidentify,		/* devo_identify */
	nulldev,		/* devo_probe */
	beattach,		/* devo_attach */
	bedetach,		/* devo_detach */
	nodev,			/* devo_reset */
	&cb_be_ops,		/* devo_cb_ops */
	(struct bus_ops *)NULL	/* devo_bus_ops */
};

/*
 * Claim the device is ultra-capable of burst in the beginning.  Use
 * the value returned by ddi_dma_burstsizes() to actually set the QEC
 * global control register later.
 */
#define	QEDLIMADDRLO	(0x00000000)
#define	QEDLIMADDRHI	(0xffffffff)

static ddi_dma_lim_t qe_dma_limits = {
	(u_long) QEDLIMADDRLO,	/* dlim_addr_lo */
	(u_long) QEDLIMADDRHI,	/* dlim_addr_hi */
	(u_int) QEDLIMADDRHI,	/* dlim_cntr_max */
	(u_int) 0x7f,		/* dlim_burstsizes */
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
	"BigMAC Ethernet Driver v1.42",
	&be_ops,	/* driver ops */
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
static	kmutex_t	beautolock;

/*
 * Linked list of active (inuse) driver Streams.
 */
static	struct	bestr	*bestrup = NULL;
static	krwlock_t	bestruplock;

/*
 * Single private "global" lock for the few rare conditions
 * we want single-threaded.
 */
static	kmutex_t	belock;


/* <<<<<<<<<<<<<<<<<<<<<<  Configuration Parameters >>>>>>>>>>>>>>>>>>>>> */

#define	BMAC_DEFAULT_JAMSIZE	(0x04)		/* jamsize equals 4 */
#define	BMAC_LONG_JAMSIZE	(0x10)		/* jamsize equals 0x10 */
int 	jamsize = BMAC_DEFAULT_JAMSIZE;

/* <<<<<<<<<<<<<<<<<<<<<<<<  Bit Bang Operations >>>>>>>>>>>>>>>>>>>>>>>> */

static void
send_bit(bep, x)
struct be	*bep;
u_int		x;
{
	volatile struct bmactcvr	*tcvrp = bep->be_tcvrregp;
	u_int	y;

	if (bep->be_transceiver == BE_INTERNAL_TRANSCEIVER) {
		/* Piggy Back Transceiver */
		tcvrp->pal2 = (x << BMAC_TPAL2_PGYBAC_MDIO_BIT_POS)
						| BMAC_TPAL2_MII_MDIO
						| BMAC_TPAL2_MDIO_EN;
		y = tcvrp->pal2;
		tcvrp->pal2 = (x << BMAC_TPAL2_PGYBAC_MDIO_BIT_POS)
						| BMAC_TPAL2_MII_MDIO
						| BMAC_TPAL2_MDIO_EN
						| BMAC_TPAL2_MDC;
		y = tcvrp->pal2;
	} else if (bep->be_transceiver == BE_EXTERNAL_TRANSCEIVER) {
		/* External Transceiver */
		tcvrp->pal2 = (x << BMAC_TPAL2_MII_MDIO_BIT_POS)
						| BMAC_TPAL2_PGYBAC_MDIO
						| BMAC_TPAL2_MDIO_EN;

		y = tcvrp->pal2;
		tcvrp->pal2 = (x << BMAC_TPAL2_MII_MDIO_BIT_POS)
						| BMAC_TPAL2_PGYBAC_MDIO
						| BMAC_TPAL2_MDIO_EN
						| BMAC_TPAL2_MDC;
		y = tcvrp->pal2;
	}
}

static u_int
get_bit(bep)
struct be	*bep;
{
	volatile struct bmactcvr	*tcvrp = bep->be_tcvrregp;
	u_int	x, y;

	if (bep->be_transceiver == BE_INTERNAL_TRANSCEIVER) {
		/* Piggy Back Transceiver */
		tcvrp->pal2 = BMAC_TPAL2_MII_MDIO | 0;
		y = tcvrp->pal2;
		tcvrp->pal2 = BMAC_TPAL2_MII_MDIO | BMAC_TPAL2_MDC;
		y = tcvrp->pal2;
		x = tcvrp->pal2;
		x = ((x & BMAC_TPAL2_PGYBAC_MDIO) >>
			BMAC_TPAL2_PGYBAC_MDIO_BIT_POS);
	} else if (bep->be_transceiver == BE_EXTERNAL_TRANSCEIVER) {
		/* External Transceiver */
		tcvrp->pal2 = BMAC_TPAL2_PGYBAC_MDIO | 0;
		y = tcvrp->pal2;
		tcvrp->pal2 = BMAC_TPAL2_PGYBAC_MDIO | BMAC_TPAL2_MDC;
		y = tcvrp->pal2;
		x = tcvrp->pal2;
		x = ((x & BMAC_TPAL2_MII_MDIO) >> BMAC_TPAL2_MII_MDIO_BIT_POS);
	}
	return (x);
}

/*
 * To read the MII register bits according to the Standard
 * Used with Babybac2, Babybac3 or with External Transceivers
 */
static u_int
get_bit_std(bep)
struct be	*bep;
{
	volatile struct bmactcvr	*tcvrp = bep->be_tcvrregp;
	u_int	x, y;

	if (bep->be_transceiver == BE_INTERNAL_TRANSCEIVER) {
		/* Piggy Back Transceiver */
		tcvrp->pal2 = BMAC_TPAL2_MII_MDIO | 0;
		y = tcvrp->pal2;
		x = tcvrp->pal2;
		x = ((x & BMAC_TPAL2_PGYBAC_MDIO) >>
			BMAC_TPAL2_PGYBAC_MDIO_BIT_POS);
		tcvrp->pal2 = BMAC_TPAL2_MII_MDIO | BMAC_TPAL2_MDC;
		y = tcvrp->pal2;
	} else if (bep->be_transceiver == BE_EXTERNAL_TRANSCEIVER) {
		/* External Transceiver */
		tcvrp->pal2 = BMAC_TPAL2_PGYBAC_MDIO | 0;
		y = tcvrp->pal2;
		x = tcvrp->pal2;
		x = ((x & BMAC_TPAL2_MII_MDIO) >> BMAC_TPAL2_MII_MDIO_BIT_POS);
		tcvrp->pal2 = BMAC_TPAL2_PGYBAC_MDIO | BMAC_TPAL2_MDC;
		y = tcvrp->pal2;
	}
	return (x);
}

#define	SEND_BIT(x)		send_bit(bep, x)
#define	GET_BIT(x)		x = get_bit(bep)
#define	GET_BIT_STD(x)		x = get_bit_std(bep)

static void
be_force_idle(bep)
struct be	*bep;
{
	volatile struct bmactcvr	*tcvrp = bep->be_tcvrregp;
	int		i;
	u_int		y;

	for (i = 0; i < 33; i++) {
		tcvrp->pal2 = BMAC_TPAL2_PGYBAC_MDIO
						| BMAC_TPAL2_MII_MDIO
						| BMAC_TPAL2_MDIO_EN;
		y = tcvrp->pal2;
		tcvrp->pal2 = BMAC_TPAL2_PGYBAC_MDIO
						| BMAC_TPAL2_MII_MDIO
						| BMAC_TPAL2_MDIO_EN
						| BMAC_TPAL2_MDC;
		y = tcvrp->pal2;
	}
}

static void
be_mii_write(bep, regad, data)
struct be	*bep;
u_char		regad;
u_short		data;
{
	u_char	phyad;
	int		i;

	switch (bep->be_transceiver) {
	case BE_INTERNAL_TRANSCEIVER:
		phyad = BMAC_INTERNAL_PHYAD;
		break;
	case BE_EXTERNAL_TRANSCEIVER:
		phyad = BMAC_EXTERNAL_PHYAD;
		break;
	}
	be_force_idle(bep);
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
}

/* Return 0 if OK, 1 if error (Transceiver does not talk management) */
static int
be_mii_read(bep, regad, datap)
struct be	*bep;
u_char		regad;
u_short		*datap;
{
	u_char	phyad;
	int		i;
	u_int	x;
	u_int	y;

	*datap = 0;

	switch (bep->be_transceiver) {
	case BE_INTERNAL_TRANSCEIVER:
		phyad = BMAC_INTERNAL_PHYAD;
		break;
	case BE_EXTERNAL_TRANSCEIVER:
		phyad = BMAC_EXTERNAL_PHYAD;
		break;
	}
	be_force_idle(bep);
	SEND_BIT(0); SEND_BIT(1);	/* <ST> */
	SEND_BIT(1); SEND_BIT(0);	/* <OP> */
	for (i = 4; i >= 0; i--) {		/* <AAAAA> */
		SEND_BIT((phyad >> i) & 1);
	}
	for (i = 4; i >= 0; i--) {		/* <RRRRR> */
		SEND_BIT((regad >> i) & 1);
	}

	if (bep->be_transceiver == BE_EXTERNAL_TRANSCEIVER) {

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
		for (i = 0xf; i >= 0; i--) {    /* <DDDDDDDDDDDDDDDD> */
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
be_delay(usec)
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

static void
be_stop_timer(bep)
struct be	*bep;
{
	if (bep->be_timerid) {
		untimeout(bep->be_timerid);
		bep->be_timerid = 0;
	}
}

static void
be_start_timer(bep, func, msec)
struct be	*bep;
fptrv_t		func;
int			msec;
{
	if (bep->be_timerid)
		untimeout(bep->be_timerid);
	bep->be_timerid = timeout(func, (caddr_t) bep, drv_usectohz(1000*msec));
}

/* Return 1 if the link is up, 0 otherwise */
static int
be_select_speed(bep, speed)
struct be	*bep;
int			speed;
{
	u_short		stat;

	switch (speed) {
	case BE_SPEED_100:
		switch (bep->be_transceiver) {
		case BE_INTERNAL_TRANSCEIVER:
			be_mii_write(bep, BMAC_MII_CTLREG,
					BMAC_MII_CTL_SPEED_100);
			break;
		case BE_EXTERNAL_TRANSCEIVER:
			if (bep->be_delay == 0) {
				be_mii_write(bep, BMAC_MII_CTLREG,
						BMAC_MII_CTL_SPEED_100);
			}
			/* Wait 100 ms */
			if (bep->be_delay < 1) {
				bep->be_delay++;
				return (0);
			} else {
				bep->be_delay = 0;
			}
			break;
		default:
			break;
		}
		break;
	case BE_SPEED_10:
		if (bep->be_delay == 0) {
			be_mii_write(bep, BMAC_MII_CTLREG, 0);
		}
		/* Wait 400 ms */
		if (bep->be_delay < 4) {
			bep->be_delay++;
			return (0);
		} else {
			bep->be_delay = 0;
		}
		break;
	default:
		return (0);
	}

	(void) be_mii_read(bep, BMAC_MII_STATREG, &stat);
	if (be_mii_read(bep, BMAC_MII_STATREG, &stat) == 1) {
		/* Transceiver does not talk mii */
		be_stop_timer(bep);
		return (1);
	}

	if (stat & BMAC_MII_STAT_LINKUP)
		return (1);
	else
		return (0);
}

static void
be_check_transceiver(bep)
struct be	*bep;
{
	volatile struct bmactcvr	*tcvrp;
	int		pal2;
	u_int		y;

	/*
	 * Transceiver selection for the P1.5 boards.
	 * First check to see what transceivers are out there.
	 * If an external transceiver is present
	 * then use it, regardless of whether there is a babybac.
	 * If babybac is present and there is no external transceiver
	 * then use babybac.
	 * If there is no external transceiver present, and no babybac,
	 * then something is wrong so print an error message.
	 */

	tcvrp = bep->be_tcvrregp;
	be_force_idle(bep);
	tcvrp->pal2 = BMAC_TPAL2_PGYBAC_MDIO |
					BMAC_TPAL2_MII_MDIO |
					BMAC_TPAL2_MDC;
	y = tcvrp->pal2;
	tcvrp->pal2 = BMAC_TPAL2_PGYBAC_MDIO |
					BMAC_TPAL2_MII_MDIO;
	y = tcvrp->pal2;
	/*
	 * Wait 10 usec for MII_MDIO to decay.  be_delay was used
	 * to do this, but as part of the fix for bug 1216408,
	 * removed be_delay.  Use drv_usecwait instead
	 */

	drv_usecwait(20);	/* bug 1216408 */

	pal2 = tcvrp->pal2;
	if (pal2 & BMAC_TPAL2_MII_MDIO) {
		tcvrp->pal1 = (u_int) (~(BMAC_TPAL1_CLK_FSTSLW
						| BMAC_TPAL1_CLK_LOOP_EN
						| BMAC_TPAL1_LOOP_EN)
						| BMAC_TPAL1_PGYBAC_DIS);
		y = tcvrp->pal1;
		bep->be_transceiver = BE_EXTERNAL_TRANSCEIVER;
	} else if (pal2 & BMAC_TPAL2_PGYBAC_MDIO) {
		tcvrp->pal1 = (u_int) (~(BMAC_TPAL1_CLK_FSTSLW
						| BMAC_TPAL1_CLK_LOOP_EN
						| BMAC_TPAL1_LOOP_EN
						| BMAC_TPAL1_PGYBAC_DIS));
		y = tcvrp->pal1;
		bep->be_transceiver = BE_INTERNAL_TRANSCEIVER;
	} else {
		berror(bep->be_dip, "No transceiver found!");
	}
}

static void
be_check_link(bep)
struct be	*bep;
{
	u_short		stat;

	be_stop_timer(bep);
	if (!bep->be_linkcheck)
		return;

	be_check_transceiver(bep);
	if (be_mii_read(bep, BMAC_MII_STATREG, &stat) == 1) {
		/* Transceiver does not talk mii */
		be_start_timer(bep, be_check_link, BE_LINKCHECK_TIMER);
		return;
	}
	if (stat & BMAC_MII_STAT_LINKUP) {
		be_start_timer(bep, be_check_link, BE_LINKCHECK_TIMER);
	} else {

		/* link up/down message bug fix */

		be_mii_read(bep, BMAC_MII_STATREG, &stat);
		if (stat & BMAC_MII_STAT_LINKUP) {
			be_start_timer(bep, be_check_link, BE_LINKCHECK_TIMER);
			return;
		}

		berror(bep->be_dip, "Link Down");
		bep->be_linkup = 0;
		bep->be_delay = 0;
		switch (bep->be_mode) {
		case BE_AUTO_SPEED:
			bep->be_linkup_10 = 0;
			bep->be_tryspeed = BE_SPEED_100;
			bep->be_ntries = bep->be_nlasttries = BE_NTRIES_LOW;
			be_try_speed(bep);
			break;
		case BE_FORCE_SPEED:
			be_force_speed(bep);
			break;
		default:
			break;
		}
	}
}

static void
be_display_transceiver(bep)
struct be	*bep;
{
	switch (bep->be_transceiver) {
	case BE_INTERNAL_TRANSCEIVER:
		berror(bep->be_dip, "Using Onboard Transceiver");
		break;
	case BE_EXTERNAL_TRANSCEIVER:
		berror(bep->be_dip, "Using External Transceiver");
		break;
	default:
		berror(bep->be_dip, "No transceiver found!");
		break;
	}
}

static void
be_force_speed(bep)
struct be	*bep;
{
	int		linkup;

	be_stop_timer(bep);
	be_check_transceiver(bep);
	linkup = be_select_speed(bep, bep->be_forcespeed);
	if (linkup) {
		bep->be_linkup = 1;
		be_display_transceiver(bep);
		switch (bep->be_forcespeed) {
		case BE_SPEED_100:
			berror(bep->be_dip, "100 Mbps Link Up");
			break;
		case BE_SPEED_10:
			berror(bep->be_dip, "10 Mbps Link Up");
			break;
		default:
			break;
		}
		be_start_timer(bep, be_check_link, BE_LINKCHECK_TIMER);
	} else {
		be_start_timer(bep, be_force_speed, BE_TICKS);
	}
}

static void
be_try_speed(bep)
struct be	*bep;
{
	int		linkup;

	be_stop_timer(bep);
	be_check_transceiver(bep);
	linkup = be_select_speed(bep, bep->be_tryspeed);
	if (linkup) {
		switch (bep->be_tryspeed) {
		case BE_SPEED_100:
			bep->be_ntries = bep->be_nlasttries = BE_NTRIES_LOW;
			bep->be_linkup = 1;
			be_display_transceiver(bep);
			berror(bep->be_dip, "100 Mbps Link Up");
			be_start_timer(bep, be_check_link, BE_LINKCHECK_TIMER);
			break;
		case BE_SPEED_10:
			if (bep->be_linkup_10) {
				bep->be_linkup_10 = 0;
				bep->be_ntries = BE_NTRIES_LOW;
				bep->be_nlasttries = BE_NTRIES_LOW;
				bep->be_linkup = 1;
				be_display_transceiver(bep);
				berror(bep->be_dip, "10 Mbps Link Up");
				be_start_timer(bep, be_check_link,
							BE_LINKCHECK_TIMER);
			} else {
				bep->be_linkup_10 = 1;
				bep->be_tryspeed = BE_SPEED_100;
				bep->be_ntries = BE_NTRIES_LOW;
				bep->be_nlasttries = BE_NTRIES_LOW;
				be_start_timer(bep, be_try_speed, BE_TICKS);
			}
			break;
		default:
			break;
		}
		return;
	}

	bep->be_ntries--;
	if (bep->be_ntries == 0) {
		if (bep->be_nlasttries < BE_NTRIES_HIGH)
			bep->be_nlasttries <<= 1;
		bep->be_ntries = bep->be_nlasttries;
		switch (bep->be_tryspeed) {
		case BE_SPEED_100:
			bep->be_tryspeed = BE_SPEED_10;
			break;
		case BE_SPEED_10:
			bep->be_tryspeed = BE_SPEED_100;
			break;
		default:
			break;
		}
	}
	be_start_timer(bep, be_try_speed, BE_TICKS);
}

/* <<<<<<<<<<<<<<<<<<<<<<<<<<<  LOADABLE ENTRIES  >>>>>>>>>>>>>>>>>>>>>>> */

int
_init(void)
{
	int	status;

	mutex_init(&beautolock, "be autoconfig lock", MUTEX_DRIVER, NULL);
	status = mod_install(&modlinkage);
	if (status != 0) {
		mutex_destroy(&beautolock);
		return (status);
	}
	return (0);
}

int
_fini(void)
{
	int	status;

	rw_enter(&bestruplock, RW_READER);
	if (bestrup) {
		rw_exit(&bestruplock);
		return (EBUSY);
	}
	rw_exit(&bestruplock);

	status = mod_remove(&modlinkage);
	if (status != 0)
		return (status);

	mutex_destroy(&belock);
	rw_destroy(&bestruplock);
	mutex_destroy(&beautolock);
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
static	int	bedebug = 0;

#define	BERINDEX(i)		(i % BERPENDING)

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
#define	QEBUFIOADDR(bep, a) \
	((bep)->be_bufiobase + ((u_long)(a) - (bep)->be_bufkbase))
#define	QEIOPBIOADDR(bep, a) \
	((bep)->be_iopbiobase + ((u_long)(a) - (bep)->be_iopbkbase))

/*
 * XXX
 * Define QESYNCIOPB to nothing for now.
 * If/when we have PSO-mode kernels running which really need
 * to sync something during a ddi_dma_sync() of iopb-allocated memory,
 * then this can go back in, but for now we take it out
 * to save some microseconds.
 */
#define	QESYNCIOPB(bep, a, size, who)

#ifdef	notdef
/*
 * ddi_dma_sync() a TMD or RMD descriptor.
 */
#define	QESYNCIOPB(bep, a, size, who) \
	(void) ddi_dma_sync((bep)->be_iopbhandle, \
		((u_long)(a) - (bep)->be_iopbkbase), \
		(size), \
		(who))
#endif	notdef

#define	QESAPMATCH(sap, type, flags) ((sap == type)? 1 : \
	((flags & BESALLSAP)? 1 : \
	((sap <= ETHERMTU) && (sap > 0) && (type <= ETHERMTU))? 1 : 0))

/*
 * Ethernet broadcast address definition.
 */
static	struct ether_addr	etherbroadcastaddr = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/*
 * Linked list of be structures - one per card.
 */
static struct be *beup = NULL;

/*
 * force the fallback to ddi_dma routines
 */
static int be_force_dma = 0;

/*
 * Our DL_INFO_ACK template.
 */
static	dl_info_ack_t beinfoack = {
	DL_INFO_ACK,				/* dl_primitive */
	ETHERMTU,				/* dl_max_sdu */
	0,					/* dl_min_sdu */
	BEADDRL,				/* dl_addr_length */
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
	sizeof (dl_info_ack_t) + BEADDRL,	/* dl_brdcst_addr_offset */
	0					/* dl_growth */
};

/*
 * Identify device.
 */
static int
beidentify(dev_info_t	*dip)
{
	if (strcmp(ddi_get_name(dip), "be") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/*
 * Interface exists: make available by filling in network interface
 * record.  System will initialize the interface when it is ready
 * to accept packets.
 */
static int
beattach(dip, cmd)
dev_info_t	*dip;
ddi_attach_cmd_t	cmd;
{
	register struct be *bep;
	struct qec_soft *qsp;
	static	once = 1;
	int regno;
	int	board_version = 0;
	int	prop_len = sizeof (int);

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	bep = (struct be *) NULL;

	/*
	 * Allocate soft device data structure
	 */
	if ((bep = GETSTRUCT(struct be, 1)) == NULL) {
		berror(dip, "beattach:  kmem_alloc be failed");
		return (DDI_FAILURE);
	}

	/*
	 * Map in the device registers.
	 *
	 * Reg # 0 is the QEC per-channel register set.
	 * Reg # 1 is the BigMAC register set.
	 * Reg # 2 is the BigMAC transceiver register
	 */
#ifdef	MPSAS
	if (ddi_dev_nregs(dip, &regno) != (DDI_SUCCESS) || (regno != 2)) {
		berror(dip, "ddi_dev_nregs failed, returned %d", (char *)regno);
		goto bad;
#else	MPSAS
	if (ddi_dev_nregs(dip, &regno) != (DDI_SUCCESS) || (regno != 3)) {
		berror(dip, "ddi_dev_nregs failed, returned %d", (char *)regno);
		goto bad;
#endif	MPSAS
	}
	if (ddi_map_regs(dip, 0, (caddr_t *) &(bep->be_chanregp), 0, 0)) {
		berror(dip, "ddi_map_regs for qec per-channel reg failed");
		goto bad;
	}
	if (ddi_map_regs(dip, 1, (caddr_t *) &(bep->be_bmacregp), 0, 0)) {
		berror(dip, "ddi_map_regs for bmac reg failed");
		goto bad;
	}

#ifndef	MPSAS
	if (ddi_map_regs(dip, 2, (caddr_t *) &(bep->be_tcvrregp), 0, 0)) {
		berror(dip, "ddi_map_regs for bmac transceiver reg failed");
		goto bad;
	}
#endif	MPSAS

	bep->be_boardrev = P1_0;
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, 0, "board-version",
				(caddr_t) &board_version, &prop_len)
				== DDI_PROP_SUCCESS) {
		switch (board_version) {
		case 1:
			bep->be_boardrev = P1_5;
			break;
		default:
			break;
		}
	}
	bep->be_mode = BE_AUTO_SPEED;
	bep->be_tryspeed = BE_SPEED_100;
	bep->be_forcespeed = BE_SPEED_100;

	bep->be_dip = dip;

	/* save ptr to parent global control register */
	qsp = (struct qec_soft *) ddi_get_driver_private(ddi_get_parent(dip));
	bep->be_globregp = qsp->qs_globregp;

	/*
	 * Add interrupt to system
	 */
	if (ddi_add_intr(dip, 0, &bep->be_cookie, 0, beintr, (caddr_t) bep)) {
		berror(dip, "ddi_add_intr failed");
		goto bad;
	}

	/*
	 * At this point, we are *really* here.
	 */
	ddi_set_driver_private(dip, (caddr_t) bep);

	/*
	 * Initialize mutex's for this device.
	 */
	mutex_init(&bep->be_xmitlock, "be xmit lock", MUTEX_DRIVER,
		(void *)bep->be_cookie);
	mutex_init(&bep->be_intrlock, "be intr lock", MUTEX_DRIVER,
		(void *)bep->be_cookie);

	/*
	 * One time only driver initializations.
	 */
	if (once) {
		once = 0;
		rw_init(&bestruplock, "be streams linked list lock",
			RW_DRIVER, (void *)bep->be_cookie);
		mutex_init(&belock, "belock lock", MUTEX_DRIVER,
			(void *)bep->be_cookie);
	}

	/*
	 * Get the local ethernet address.
	 */
	localetheraddr((struct ether_addr *)NULL, &bep->be_ouraddr);

	/*
	 * Create the filesystem device node.
	 */
	if (ddi_create_minor_node(dip, "be", S_IFCHR,
		ddi_get_instance(dip), DDI_NT_NET, CLONE_DEV) == DDI_FAILURE) {
		berror(dip, "ddi_create_minor_node failed");
		mutex_destroy(&bep->be_xmitlock);
		mutex_destroy(&bep->be_intrlock);
		goto bad;
	}

	mutex_enter(&beautolock);
	if (once) {
		once = 0;
		rw_init(&bestruplock, "be streams linked list lock",
			RW_DRIVER, (void *)bep->be_cookie);
		mutex_init(&belock, "be global lock", MUTEX_DRIVER,
			(void *)bep->be_cookie);
	}
	mutex_exit(&beautolock);

	/* lock be structure while manipulating link list of be structs */
	mutex_enter(&belock);
	bep->be_nextp = beup;
	beup = bep;
	mutex_exit(&belock);

#ifndef	MPSAS
	bestatinit(bep);
#endif	MPSAS
	ddi_report_dev(dip);
	return (DDI_SUCCESS);

bad:
	if (bep->be_cookie)
		ddi_remove_intr(dip, 0, bep->be_cookie);
	if (bep->be_chanregp)
		ddi_unmap_regs(dip, 0, (caddr_t *) &bep->be_chanregp, 0, 0);
	if (bep->be_bmacregp)
		ddi_unmap_regs(dip, 1, (caddr_t *) &bep->be_bmacregp, 0, 0);
#ifndef	MPSAS
	if (bep->be_tcvrregp)
		ddi_unmap_regs(dip, 2, (caddr_t *) &bep->be_tcvrregp, 0, 0);
#endif	MPSAS
	if (bep)
		kmem_free((caddr_t) bep, sizeof (*bep));

	return (DDI_FAILURE);
}

static int
bedetach(dip, cmd)
dev_info_t	*dip;
ddi_detach_cmd_t	cmd;
{
	register struct be 		*bep, *betmp, **prevbep;
	volatile struct bmac 		*bmacp;
	volatile struct qecb_chan 	*chanp;
#ifndef	MPSAS
	volatile struct bmactcvr	*tcvrp;
#endif	MPSAS

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	bep = (struct be *)ddi_get_driver_private(dip);
	bmacp = bep->be_bmacregp;
	chanp = bep->be_chanregp;
#ifndef	MPSAS
	tcvrp = bep->be_tcvrregp;
#endif	MPSAS

	if (!bep)			/* No resources allocated */
		return (DDI_SUCCESS);

	/*
	 * Make driver quiescent
	 */
	if (betxreset(bep))
		berror(bep->be_dip, "txmac did not reset");
	if (berxreset(bep))
		berror(bep->be_dip, "rxmac did not reset");

	ddi_remove_minor_node(dip, NULL);

	/*
	 * Remove instance of the intr
	 */
	ddi_remove_intr(dip, 0, bep->be_cookie);

	/*
	 * Destroy all mutexes and data structures allocated during
	 * attach time.
	 */
	if (chanp)
		(void) ddi_unmap_regs(dip, 0, (caddr_t *) &chanp, 0, 0);
	if (bmacp)
		(void) ddi_unmap_regs(dip, 1, (caddr_t *) &bmacp, 0, 0);
#ifndef	MPSAS
	if (tcvrp)
		(void) ddi_unmap_regs(dip, 2, (caddr_t *) &tcvrp, 0, 0);
#endif	MPSAS

	/*
	 * Remove bep from the link list of device structures
	 */

	for (prevbep = &beup; (betmp = *prevbep) != NULL;
		prevbep = &betmp->be_nextp)
		if (betmp == bep) {
#ifndef	MPSAS
			kstat_delete(betmp->be_ksp);
#endif	MPSAS
			*prevbep = betmp->be_nextp;
			be_stop_timer(betmp);
			mutex_destroy(&betmp->be_xmitlock);
			mutex_destroy(&betmp->be_intrlock);
			if (betmp->be_iopbhandle)
				ddi_dma_free(betmp->be_iopbhandle);
			if (betmp->be_iopbkbase)
				ddi_iopb_free((caddr_t)betmp->be_iopbkbase);

			befreebufs(betmp);

			if (betmp->be_dvmarh) {
				(void) dvma_release(betmp->be_dvmarh);
				(void) dvma_release(betmp->be_dvmaxh);
				betmp->be_dvmarh = betmp->be_dvmaxh = NULL;
			}
			if (betmp->be_dmarh) {
				kmem_free((caddr_t)betmp->be_dmaxh,
				    (QEC_QMDMAX + BERPENDING) *
				    (sizeof (ddi_dma_handle_t)));
				betmp->be_dmarh = betmp->be_dmaxh = NULL;
			}

			kmem_free((caddr_t) betmp, sizeof (struct be));
			break;
		}
	return (DDI_SUCCESS);
}

/*
 * Return 0 upon success, 1 on failure.
 * Should be called with qec_soft pointer, as it has the global
 * register definition which in turn has the RESET bit
 */
static int
bebmacstop(bep)
struct	be	*bep;
{
	volatile u_int *qgcrp = &(bep->be_globregp->control);

	*qgcrp = QECG_CONTROL_RST;
	QECDELAY((*qgcrp & QECG_CONTROL_RST) == 0, QECMAXRSTDELAY);
	if (*qgcrp & QECG_CONTROL_RST) {
		berror(bep->be_dip, "cannot stop bmac");
		return (1);
	} else
		return (0);
}

#ifndef	MPSAS
static int
bestat_kstat_update(kstat_t *ksp, int rw)
{
	struct be *bep;
	struct bekstat *bkp;

	bep = (struct be *) ksp->ks_private;
	bkp = (struct bekstat *) ksp->ks_data;

	/*
	 * Update all the stats by reading all the counter registers.
	 * Counter register stats do not updated till they overflow
	 * and interrupt.
	 */
	besavecntrs(bep);

	if (rw == KSTAT_WRITE) {
#ifdef	kstat
		bep->be_ipackets	= bkp->bk_ipackets.value.ul;
		bep->be_ierrors		= bkp->bk_ierrors.value.ul;
		bep->be_opackets	= bkp->bk_opackets.value.ul;
		bep->be_oerrors		= bkp->bk_oerrors.value.ul;
		bep->be_coll		= bkp->bk_collisions.value.ul;
		bep->be_fram		= bkp->bk_fram.value.ul;
		bep->be_crc		= bkp->bk_crc.value.ul;
		bep->be_buff		= bkp->bk_buff.value.ul;
		bep->be_oflo		= bkp->bk_oflo.value.ul;
		bep->be_uflo		= bkp->bk_uflo.value.ul;
		bep->be_missed		= bkp->bk_missed.value.ul;
		bep->be_tlcol		= bkp->bk_tlcol.value.ul;
		bep->be_trtry		= bkp->bk_trtry.value.ul;
		bep->be_tnocar		= bkp->bk_tnocar.value.ul;
		bep->be_inits		= bkp->bk_inits.value.ul;
		bep->be_nocanput	= bkp->bk_nocanput.value.ul;
		bep->be_allocbfail	= bkp->bk_allocbfail.value.ul;
		bep->be_runt		= bkp->bk_runt.value.ul;
		bep->be_jab		= bkp->bk_jab.value.ul;
		bep->be_babl		= bkp->bk_babl.value.ul;
		bep->be_tmder 		= bkp->bk_tmder.value.ul;
		bep->be_laterr		= bkp->bk_laterr.value.ul;
		bep->be_parerr		= bkp->bk_parerr.value.ul;
		bep->be_errack		= bkp->bk_errack.value.ul;
		bep->be_notmds		= bkp->bk_notmds.value.ul;
		bep->be_notbufs		= bkp->bk_notbufs.value.ul;
		bep->be_norbufs		= bkp->bk_norbufs.value.ul;
		bep->be_clsn		= bkp->bk_clsn.value.ul;
#else
		return (EACCES);
#endif	kstat
	} else {
		bkp->bk_ipackets.value.ul	= bep->be_ipackets;
		bkp->bk_ierrors.value.ul	= bep->be_ierrors;
		bkp->bk_opackets.value.ul	= bep->be_opackets;
		bkp->bk_oerrors.value.ul	= bep->be_oerrors;
		bkp->bk_coll.value.ul		= bep->be_coll;
		bkp->bk_fram.value.ul		= bep->be_fram;
		bkp->bk_crc.value.ul		= bep->be_crc;
		bkp->bk_buff.value.ul		= bep->be_buff;
		bkp->bk_oflo.value.ul		= bep->be_oflo;
		bkp->bk_uflo.value.ul		= bep->be_uflo;
		bkp->bk_missed.value.ul		= bep->be_missed;
		bkp->bk_tlcol.value.ul		= bep->be_tlcol;
		bkp->bk_trtry.value.ul		= bep->be_trtry;
		bkp->bk_tnocar.value.ul		= bep->be_tnocar;
		bkp->bk_inits.value.ul		= bep->be_inits;
		bkp->bk_nocanput.value.ul	= bep->be_nocanput;
		bkp->bk_allocbfail.value.ul	= bep->be_allocbfail;
		bkp->bk_runt.value.ul		= bep->be_runt;
		bkp->bk_jab.value.ul		= bep->be_jab;
		bkp->bk_babl.value.ul		= bep->be_babl;
		bkp->bk_tmder.value.ul		= bep->be_tmder;
		bkp->bk_laterr.value.ul		= bep->be_laterr;
		bkp->bk_parerr.value.ul		= bep->be_parerr;
		bkp->bk_errack.value.ul		= bep->be_errack;
		bkp->bk_notmds.value.ul		= bep->be_notmds;
		bkp->bk_notbufs.value.ul	= bep->be_notbufs;
		bkp->bk_norbufs.value.ul	= bep->be_norbufs;
		bkp->bk_clsn.value.ul		= bep->be_clsn;
	}
	return (0);
}
static void
bestatinit(bep)
struct	be	*bep;
{
	struct	kstat	*ksp;
	struct	bekstat	*bkp;

#ifdef	kstat
	if ((ksp = kstat_create("be", ddi_get_instance(bep->be_dip),
		NULL, "net", KSTAT_TYPE_NAMED,
		sizeof (struct bekstat) / sizeof (kstat_named_t),
		KSTAT_FLAG_PERSISTENT)) == NULL) {
#else
	if ((ksp = kstat_create("be", ddi_get_instance(bep->be_dip),
	    NULL, "net", KSTAT_TYPE_NAMED,
	    sizeof (struct bekstat) / sizeof (kstat_named_t), 0)) == NULL) {
#endif	kstat
		berror(bep->be_dip, "kstat_create failed");
		return;
	}

	bep->be_ksp = ksp;
	bkp = (struct bekstat *) (ksp->ks_data);
	kstat_named_init(&bkp->bk_ipackets,		"ipackets",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_ierrors,		"ierrors",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_opackets,		"opackets",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_oerrors,		"oerrors",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_coll,			"collisions",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_fram,			"framming",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_crc,			"crc",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_buff,			"buff",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_oflo,			"oflo",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_uflo,			"uflo",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_missed,		"missed",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_tlcol,		"tx_late_collisions",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_trtry,		"retry_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_tnocar,		"nocarrier",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_inits,		"inits",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_nocanput,		"nocanput",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_allocbfail,		"allocbfail",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_runt,			"runt",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_jab,			"jabber",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_babl,			"babble",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_tmder,		"tmd_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_laterr,		"late_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_parerr,		"parity_error",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_errack,		"error_ack",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_notmds,		"no_tmds",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_notbufs,		"no_tbufs",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_norbufs,		"no_rbufs",
		KSTAT_DATA_ULONG);
	kstat_named_init(&bkp->bk_clsn,			"rx_late_collisions",
		KSTAT_DATA_ULONG);
	ksp->ks_update = bestat_kstat_update;
	ksp->ks_private = (void *) bep;
	kstat_install(ksp);
}
#endif	MPSAS

/*
 * Translate "dev_t" to a pointer to the associated "dev_info_t".
 */
/* ARGSUSED */
static int
beinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	dev_t	dev = (dev_t) arg;
	int	instance, rc;
	struct	bestr	*sbp;

	instance = getminor(dev);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		rw_enter(&bestruplock, RW_READER);
		dip = NULL;
		for (sbp = bestrup; sbp; sbp = sbp->sb_nextp)
			if (sbp->sb_minor == instance)
				break;
		if (sbp && sbp->sb_bep)
			dip = sbp->sb_bep->be_dip;
		rw_exit(&bestruplock);

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
beopen(rq, devp, flag, sflag, credp)
queue_t	*rq;
dev_t	*devp;
int	flag;
int	sflag;
cred_t	*credp;
{
	register	struct	bestr	*sbp;
	register	struct	bestr	**prevsbp;
	int	minordev;
	int	rc = 0;

	ASSERT(sflag != MODOPEN);
	TRACE_1(TR_FAC_BE, TR_BE_OPEN, "beopen:  rq %X", rq);

	/*
	 * Serialize all driver open and closes.
	 */
	rw_enter(&bestruplock, RW_WRITER);

	/*
	 * Determine minor device number.
	 */
	prevsbp = &bestrup;
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

	if ((sbp = GETSTRUCT(struct bestr, 1)) == NULL) {
		rc = ENOMEM;
		goto done;
	}

	sbp->sb_minor = minordev;
	sbp->sb_rq = rq;
	sbp->sb_state = DL_UNATTACHED;
	sbp->sb_sap = 0;
	sbp->sb_flags = 0;
	sbp->sb_bep = NULL;
	sbp->sb_mccount = 0;
	sbp->sb_mctab = NULL;
	mutex_init(&sbp->sb_lock, "be stream lock", MUTEX_DRIVER, (void *)0);

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
	rw_exit(&bestruplock);
	qprocson(rq);
	return (rc);
}

static
beclose(rq)
queue_t	*rq;
{
	register	struct	bestr	*sbp;
	register	struct	bestr	**prevsbp;

	TRACE_1(TR_FAC_BE, TR_BE_CLOSE, "beclose:  rq %X", rq);
	ASSERT(rq->q_ptr);

	qprocsoff(rq);

	sbp = (struct bestr *) rq->q_ptr;

	/*
	 * Implicit detach Stream from interface.
	 */
	if (sbp->sb_bep)
		bedodetach(sbp);

	rw_enter(&bestruplock, RW_WRITER);

	/*
	 * Unlink the per-Stream entry from the active list and free it.
	 */
	for (prevsbp = &bestrup; (sbp = *prevsbp) != NULL;
		prevsbp = &sbp->sb_nextp)
		if (sbp == (struct bestr *) rq->q_ptr)
			break;
	ASSERT(sbp);
	*prevsbp = sbp->sb_nextp;

	mutex_destroy(&sbp->sb_lock);
	kmem_free((char *) sbp, sizeof (struct bestr));

	rq->q_ptr = WR(rq)->q_ptr = NULL;

	rw_exit(&bestruplock);
	return (0);
}

static
bewput(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	register	struct	bestr	*sbp = (struct bestr *) wq->q_ptr;
	struct	be	*bep;

	TRACE_2(TR_FAC_BE, TR_BE_WPUT_START,
		"bewput start:  wq %X db_type %o", wq, DB_TYPE(mp));

	switch (DB_TYPE(mp)) {
		case M_DATA:		/* "fastpath" */
			bep = sbp->sb_bep;
			if ((sbp->sb_flags & (BESFAST|BESRAW) == 0) ||
				(sbp->sb_state != DL_IDLE) ||
				(bep == NULL)) {
				merror(wq, mp, EPROTO);
				break;
			}

			/*
			 * If any msgs already enqueued or the interface will
			 * loop back up the message (due to BEPROMISC), then
			 * enqueue the msg.  Otherwise just xmit it directly.
			 */
			if (wq->q_first) {
				(void) putq(wq, mp);
				bep->be_wantw = 1;
				qenable(wq);
			} else if (bep->be_flags & BEPROMISC) {
				(void) putq(wq, mp);
				qenable(wq);
			} else
				(void) bestart(wq, mp, bep);

			break;

		case M_PROTO:
		case M_PCPROTO:
			/*
			 * Break the association between the current thread and
			 * the thread that calls beproto() to resolve the
			 * problem of beintr() threads which loop back around
			 * to call beproto and try to recursively acquire
			 * internal locks.
			 */
			(void) putq(wq, mp);
			qenable(wq);
			break;

		case M_IOCTL:
			beioctl(wq, mp);
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
	TRACE_1(TR_FAC_BE, TR_BE_WPUT_END, "bewput end:  wq %X", wq);
	return (0);
}

/*
 * Enqueue M_PROTO/M_PCPROTO (always) and M_DATA (sometimes) on the wq.
 *
 * Processing of some of the M_PROTO/M_PCPROTO msgs involves acquiring
 * internal locks that are held across upstream putnext calls.
 * Specifically there's the problem of beintr() holding be_intrlock
 * and bestruplock when it calls putnext() and that thread looping
 * back around to call bewput and, eventually, beinit() to create a
 * recursive lock panic.  There are two obvious ways of solving this
 * problem: (1) have beintr() do putq instead of putnext which provides
 * the loopback "cutout" right at the rq, or (2) allow beintr() to putnext
 * and put the loopback "cutout" around beproto().  We choose the latter
 * for performance reasons.
 *
 * M_DATA messages are enqueued on the wq *only* when the xmit side
 * is out of tbufs or tmds.  Once the xmit resource is available again,
 * wsrv() is enabled and tries to xmit all the messages on the wq.
 */
static
bewsrv(wq)
queue_t	*wq;
{
	mblk_t	*mp;
	struct	bestr	*sbp;
	struct	be	*bep;

	TRACE_1(TR_FAC_BE, TR_BE_WSRV_START, "bewsrv start:  wq %X", wq);

	sbp = (struct bestr *) wq->q_ptr;
	bep = sbp->sb_bep;

	while (mp = getq(wq))
		switch (DB_TYPE(mp)) {
			case	M_DATA:
				if (bep) {
					if (bestart(wq, mp, bep))
						goto done;
				} else
					freemsg(mp);
				break;

			case	M_PROTO:
			case	M_PCPROTO:
				beproto(wq, mp);
				break;

			default:
				ASSERT(0);
				break;
		}

done:
	TRACE_1(TR_FAC_BE, TR_BE_WSRV_END, "bewsrv end:  wq %X", wq);
	return (0);
}

static void
beproto(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	union	DL_primitives	*dlp;
	struct	bestr	*sbp;
	u_long	prim;

	sbp = (struct bestr *) wq->q_ptr;
	dlp = (union DL_primitives *) mp->b_rptr;
	prim = dlp->dl_primitive;

	TRACE_2(TR_FAC_BE, TR_BE_PROTO_START,
		"beproto start:  wq %X dlprim %X", wq, prim);

	mutex_enter(&sbp->sb_lock);

	switch (prim) {
		case	DL_UNITDATA_REQ:
			beudreq(wq, mp);
			break;

		case	DL_ATTACH_REQ:
			beareq(wq, mp);
			break;

		case	DL_DETACH_REQ:
			bedreq(wq, mp);
			break;

		case	DL_BIND_REQ:
			bebreq(wq, mp);
			break;

		case	DL_UNBIND_REQ:
			beubreq(wq, mp);
			break;

		case	DL_INFO_REQ:
			beireq(wq, mp);
			break;

		case	DL_PROMISCON_REQ:
			beponreq(wq, mp);
			break;

		case	DL_PROMISCOFF_REQ:
			bepoffreq(wq, mp);
			break;

		case	DL_ENABMULTI_REQ:
			beemreq(wq, mp);
			break;

		case	DL_DISABMULTI_REQ:
			bedmreq(wq, mp);
			break;

		case	DL_PHYS_ADDR_REQ:
			bepareq(wq, mp);
			break;

		case	DL_SET_PHYS_ADDR_REQ:
			bespareq(wq, mp);
			break;

		default:
			dlerrorack(wq, mp, prim, DL_UNSUPPORTED, 0);
			break;
	}

	TRACE_2(TR_FAC_BE, TR_BE_PROTO_END,
		"beproto end:  wq %X dlprim %X", wq, prim);

	mutex_exit(&sbp->sb_lock);
}

static void
beioctl(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	iocblk	*iocp = (struct iocblk *) mp->b_rptr;
	struct	bestr	*sbp = (struct bestr *) wq->q_ptr;
	struct	be		*bep = sbp->sb_bep;
	be_ioc_cmd_t	*ioccmdp;

	switch (iocp->ioc_cmd) {
	case DLIOCRAW:		/* raw M_DATA mode */
		sbp->sb_flags |= BESRAW;
		miocack(wq, mp, 0, 0);
		break;

/* XXX Remove this line in mars */
#define	DL_IOC_HDR_INFO	(DLIOC|10)	/* XXX reserved */

	case DL_IOC_HDR_INFO:	/* M_DATA "fastpath" info request */
		be_dl_ioc_hdr_info(wq, mp);
		break;

	case BE_IOC:
		ioccmdp = (be_ioc_cmd_t *) mp->b_cont->b_rptr;
		switch (ioccmdp->hdr.cmd) {
			case BE_IOC_GET_SPEED:
				ioccmdp->mode = bep->be_mode;
				switch (bep->be_mode) {
				case BE_AUTO_SPEED:
					ioccmdp->speed = bep->be_tryspeed;
					break;
				case BE_FORCE_SPEED:
					ioccmdp->speed = bep->be_forcespeed;
					break;
				default:
					break;
				}
				miocack(wq, mp, msgsize(mp->b_cont), 0);
				break;
			case BE_IOC_SET_SPEED:
				if (bep->be_boardrev == P1_0) {
					miocack(wq, mp, 0, 0);
					break;
				}
				bep->be_mode = ioccmdp->mode;
				bep->be_linkup = 0;
				bep->be_delay = 0;
				switch (bep->be_mode) {
				case BE_AUTO_SPEED:
					berror(bep->be_dip, "Link Down");
					bep->be_linkup_10 = 0;
					bep->be_tryspeed = BE_SPEED_100;
					bep->be_ntries = BE_NTRIES_LOW;
					bep->be_nlasttries = BE_NTRIES_LOW;
					be_try_speed(bep);
					break;
				case BE_FORCE_SPEED:
					berror(bep->be_dip, "Link Down");
					bep->be_forcespeed = ioccmdp->speed;
					be_force_speed(bep);
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
be_dl_ioc_hdr_info(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	mblk_t	*nmp;
	struct	bestr	*sbp;
	struct	qedladdr	*dlap;
	dl_unitdata_req_t	*dludp;
	struct	ether_header	*headerp;
	struct	be	*bep;
	int	off, len;
	int	minsize;

	sbp = (struct bestr *) wq->q_ptr;
	minsize = sizeof (dl_unitdata_req_t) + BEADDRL;

	/*
	 * Sanity check the request.
	 */
	if ((mp->b_cont == NULL) ||
		(MBLKL(mp->b_cont) < minsize) ||
		(*((u_long *) mp->b_cont->b_rptr) != DL_UNITDATA_REQ) ||
		((bep = sbp->sb_bep) == NULL)) {
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
	if (!MBLKIN(mp->b_cont, off, len) || (len != BEADDRL)) {
		miocnak(wq, mp, 0, EINVAL);
		return;
	}

	dlap = (struct qedladdr *) (mp->b_cont->b_rptr + off);

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
	ether_copy(&bep->be_ouraddr, &headerp->ether_shost);
	headerp->ether_type = dlap->dl_sap;

	/*
	 * Link new mblk in after the "request" mblks.
	 */
	linkb(mp, nmp);

	sbp->sb_flags |= BESFAST;
	miocack(wq, mp, msgsize(mp->b_cont), 0);
}

static void
beareq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;
	union	DL_primitives	*dlp;
	struct	be	*bep;
	int	ppa;

	sbp = (struct bestr *) wq->q_ptr;
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
	mutex_enter(&belock);
	for (bep = beup; bep; bep = bep->be_nextp)
		if (ppa == ddi_get_instance(bep->be_dip))
			break;
	mutex_exit(&belock);

	if (bep == NULL) {
		dlerrorack(wq, mp, dlp->dl_primitive, DL_BADPPA, 0);
		return;
	}

	/* Has device been initialized?  Do so if necessary. */
	if ((bep->be_flags & BERUNNING) == 0) {
		if (beinit(bep)) {
			dlerrorack(wq, mp, dlp->dl_primitive, DL_INITFAILED, 0);
			return;
		}
	}

	/* Set link to device and update our state. */
	sbp->sb_bep = bep;
	sbp->sb_state = DL_UNBOUND;

	dlokack(wq, mp, DL_ATTACH_REQ);
}

static void
bedreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;

	sbp = (struct bestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_DETACH_REQ_SIZE) {
		dlerrorack(wq, mp, DL_DETACH_REQ, DL_BADPRIM, 0);
		return;
	}

	if (sbp->sb_state != DL_UNBOUND) {
		dlerrorack(wq, mp, DL_DETACH_REQ, DL_OUTSTATE, 0);
		return;
	}

	bedodetach(sbp);
	dlokack(wq, mp, DL_DETACH_REQ);
}

/*
 * Detach a Stream from an interface.
 */
static void
bedodetach(sbp)
struct	bestr	*sbp;
{
	struct	bestr	*tsbp;
	struct	be	*bep;
	int	reinit = 0;

	ASSERT(sbp->sb_bep);

	bep = sbp->sb_bep;
	sbp->sb_bep = NULL;

	/* Disable promiscuous mode if on. */
	if (sbp->sb_flags & BESALLPHYS) {
		sbp->sb_flags &= ~BESALLPHYS;
		reinit = 1;
	}

	/* Disable ALLMULTI mode if on. */
	if (sbp->sb_flags & BESALLMULTI) {
		sbp->sb_flags &= ~BESALLMULTI;
		reinit = 1;
	}

	/* Disable any Multicast Addresses. */
	sbp->sb_mccount = 0;
	if (sbp->sb_mctab) {
		kmem_free(sbp->sb_mctab, BEMCALLOC);
		sbp->sb_mctab = NULL;
		reinit = 1;
	}

	sbp->sb_state = DL_UNATTACHED;

	/*
	 * Detach from device structure.
	 * Uninit the device when no other streams are attached to it.
	 */
	for (tsbp = bestrup; tsbp; tsbp = tsbp->sb_nextp)
		if (tsbp->sb_bep == bep)
			break;
#ifdef	MPSAS
	if (tsbp == NULL) {
		bep->be_bmacregp->rxcfg &= ~BMAC_RXCFG_ENAB;
		bep->be_flags &= ~BERUNNING;

		/*
		 * The frame may still be in QEC local memory waiting to be
		 * transmitted.
		 */
		drv_usecwait(QEDRAINTIME);
	}
#else
	if (tsbp == NULL)
		beuninit(bep);
#endif	MPSAS
	else if (reinit)
		(void) beinit(bep);

	besetipq(bep);

}

static void
bebreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;
	union	DL_primitives	*dlp;
	struct	be	*bep;
	struct	qedladdr	beaddr;
	u_long	sap;
	int	xidtest;

	sbp = (struct bestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_BIND_REQ_SIZE) {
		dlerrorack(wq, mp, DL_BIND_REQ, DL_BADPRIM, 0);
		return;
	}

	if (sbp->sb_state != DL_UNBOUND) {
		dlerrorack(wq, mp, DL_BIND_REQ, DL_OUTSTATE, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	bep = sbp->sb_bep;
	sap = dlp->bind_req.dl_sap;
	xidtest = dlp->bind_req.dl_xidtest_flg;

	ASSERT(bep);

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

	beaddr.dl_sap = sap;
	ether_copy(&bep->be_ouraddr, &beaddr.dl_phys);
	dlbindack(wq, mp, sap, &beaddr, BEADDRL, 0, 0);

	besetipq(bep);

}

static void
beubreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;

	sbp = (struct bestr *) wq->q_ptr;

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

	besetipq(sbp->sb_bep);
}

static void
beireq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;
	dl_info_ack_t	*dlip;
	struct	qedladdr	*dlap;
	struct	ether_addr	*ep;
	int	size;

	sbp = (struct bestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_INFO_REQ_SIZE) {
		dlerrorack(wq, mp, DL_INFO_REQ, DL_BADPRIM, 0);
		return;
	}

	/* Exchange current msg for a DL_INFO_ACK. */
	size = sizeof (dl_info_ack_t) + BEADDRL + ETHERADDRL;
	if ((mp = mexchange(wq, mp, size, M_PCPROTO, DL_INFO_ACK)) == NULL)
		return;

	/* Fill in the DL_INFO_ACK fields and reply. */
	dlip = (dl_info_ack_t *) mp->b_rptr;
	*dlip = beinfoack;
	dlip->dl_current_state = sbp->sb_state;
	dlap = (struct qedladdr *) (mp->b_rptr + dlip->dl_addr_offset);
	dlap->dl_sap = sbp->sb_sap;
	if (sbp->sb_bep) {
		ether_copy(&sbp->sb_bep->be_ouraddr, &dlap->dl_phys);
	} else {
		bzero((caddr_t) &dlap->dl_phys, ETHERADDRL);
	}
	ep = (struct ether_addr *) (mp->b_rptr + dlip->dl_brdcst_addr_offset);
	ether_copy(&etherbroadcastaddr, ep);

	qreply(wq, mp);
}

static void
beponreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;

	sbp = (struct bestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_PROMISCON_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PROMISCON_REQ, DL_BADPRIM, 0);
		return;
	}

	switch (((dl_promiscon_req_t *) mp->b_rptr)->dl_level) {
		case DL_PROMISC_PHYS:
			sbp->sb_flags |= BESALLPHYS;
			break;

		case DL_PROMISC_SAP:
			sbp->sb_flags |= BESALLSAP;
			break;

		case DL_PROMISC_MULTI:
			sbp->sb_flags |= BESALLMULTI;
			break;

		default:
			dlerrorack(wq, mp, DL_PROMISCON_REQ,
						DL_NOTSUPPORTED, 0);
			return;
	}

	if (sbp->sb_bep)
		(void) beinit(sbp->sb_bep);

	if (sbp->sb_bep)
		besetipq(sbp->sb_bep);

	dlokack(wq, mp, DL_PROMISCON_REQ);
}

static void
bepoffreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;
	int	flag;

	sbp = (struct bestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_PROMISCOFF_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PROMISCOFF_REQ, DL_BADPRIM, 0);
		return;
	}

	switch (((dl_promiscoff_req_t *) mp->b_rptr)->dl_level) {
		case DL_PROMISC_PHYS:
			flag = BESALLPHYS;
			break;

		case DL_PROMISC_SAP:
			flag = BESALLSAP;
			break;

		case DL_PROMISC_MULTI:
			flag = BESALLMULTI;
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
	if (sbp->sb_bep)
		(void) beinit(sbp->sb_bep);

	if (sbp->sb_bep)
		besetipq(sbp->sb_bep);

	dlokack(wq, mp, DL_PROMISCOFF_REQ);
}

static void
beemreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;
	union	DL_primitives	*dlp;
	struct	ether_addr	*addrp;
	int	off;
	int	len;
	int	i;

	sbp = (struct bestr *) wq->q_ptr;

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

	if ((sbp->sb_mccount + 1) >= BEMAXMC) {
		dlerrorack(wq, mp, DL_ENABMULTI_REQ, DL_TOOMANY, 0);
		return;
	}

	/*
	 * Allocate table on first request.
	 */
	if (sbp->sb_mctab == NULL)
		if ((sbp->sb_mctab = (struct ether_addr *)
			kmem_alloc(BEMCALLOC, KM_SLEEP)) == NULL) {
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

	(void) beinit(sbp->sb_bep);
	dlokack(wq, mp, DL_ENABMULTI_REQ);
}

static void
bedmreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;
	union	DL_primitives	*dlp;
	struct	ether_addr	*addrp;
	int	off;
	int	len;
	int	i;

	sbp = (struct bestr *) wq->q_ptr;

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
			bcopy((caddr_t) &sbp->sb_mctab[i+1],
				(caddr_t) &sbp->sb_mctab[i],
				((sbp->sb_mccount - i) *
				sizeof (struct ether_addr)));
			sbp->sb_mccount--;
			(void) beinit(sbp->sb_bep);
			dlokack(wq, mp, DL_DISABMULTI_REQ);
			return;
		}
	dlerrorack(wq, mp, DL_DISABMULTI_REQ, DL_NOTENAB, 0);
}

static void
bepareq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;
	union	DL_primitives	*dlp;
	int	type;
	struct	be	*bep;
	struct	ether_addr	addr;

	sbp = (struct bestr *) wq->q_ptr;

	if (MBLKL(mp) < DL_PHYS_ADDR_REQ_SIZE) {
		dlerrorack(wq, mp, DL_PHYS_ADDR_REQ, DL_BADPRIM, 0);
		return;
	}

	dlp = (union DL_primitives *) mp->b_rptr;
	type = dlp->physaddr_req.dl_addr_type;
	bep = sbp->sb_bep;

	if (bep == NULL) {
		dlerrorack(wq, mp, DL_PHYS_ADDR_REQ, DL_OUTSTATE, 0);
		return;
	}

	switch (type) {
		case	DL_FACT_PHYS_ADDR:
			localetheraddr((struct ether_addr *) NULL, &addr);
			break;

		case	DL_CURR_PHYS_ADDR:
			ether_copy(&bep->be_ouraddr, &addr);
			break;

		default:
			dlerrorack(wq, mp, DL_PHYS_ADDR_REQ,
						DL_NOTSUPPORTED, 0);
			return;
	}

	dlphysaddrack(wq, mp, &addr, ETHERADDRL);
}

static void
bespareq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;
	union	DL_primitives	*dlp;
	int	off;
	int	len;
	struct	ether_addr	*addrp;
	struct	be	*bep;

	sbp = (struct bestr *) wq->q_ptr;

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
	if ((bep = sbp->sb_bep) == NULL) {
		dlerrorack(wq, mp, DL_SET_PHYS_ADDR_REQ, DL_OUTSTATE, 0);
		return;
	}

	/*
	 * Set new interface local address and re-init device.
	 * This is destructive to any other streams attached
	 * to this device.
	 */
	ether_copy(addrp, &bep->be_ouraddr);
	(void) beinit(sbp->sb_bep);

	dlokack(wq, mp, DL_SET_PHYS_ADDR_REQ);
}

static void
beudreq(wq, mp)
queue_t	*wq;
mblk_t	*mp;
{
	struct	bestr	*sbp;
	register	struct	be	*bep;
	register	dl_unitdata_req_t	*dludp;
	mblk_t	*nmp;
	struct	qedladdr	*dlap;
	struct	ether_header	*headerp;
	ulong	off, len;
	ulong	sap;

	sbp = (struct bestr *) wq->q_ptr;
	bep = sbp->sb_bep;

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
	if (!MBLKIN(mp, off, len) || (len != BEADDRL)) {
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

	dlap = (struct qedladdr *) (mp->b_rptr + off);

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
		ether_copy(&bep->be_ouraddr, &headerp->ether_shost);
		sap = dlap->dl_sap;
		freeb(mp);
		mp = nmp;
	} else {
		DB_TYPE(mp) = M_DATA;
		headerp = (struct ether_header *) mp->b_rptr;
		mp->b_wptr = mp->b_rptr + sizeof (struct ether_header);
		ether_copy(&dlap->dl_phys, &headerp->ether_dhost);
		ether_copy(&bep->be_ouraddr, &headerp->ether_shost);
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

	(void) bestart(wq, mp, bep);
}

static
bestart_slow(wq, mp, bep)
queue_t *wq;
register	mblk_t	*mp;
register	struct be	*bep;
{
	volatile	struct	qmd	*tmdp1 = NULL;
	volatile	struct	qmd	*ntmdp = NULL;
	mblk_t	*nmp = NULL;
	mblk_t  *bp;
	int	len1;
	int	flags, i;
	ddi_dma_cookie_t c;

	TRACE_1(TR_FAC_BE, TR_BE_START_START, "bestart start:  wq %X", wq);

	flags = bep->be_flags;

	if (flags & BEPROMISC)
		if ((nmp = copymsg(mp)) == NULL)
			bep->be_allocbfail++;


	mutex_enter(&bep->be_xmitlock);

	if (bep->be_tnextp > bep->be_tcurp) {
		if ((bep->be_tnextp - bep->be_tcurp) > BETPENDING)
			bereclaim(bep);
	} else {
		i = bep->be_tcurp - bep->be_tnextp;
		if (i && (i < (QEC_QMDMAX - BETPENDING)))
			bereclaim(bep);
	}

	tmdp1 = bep->be_tnextp;
	if ((ntmdp = NEXTTMD(bep, tmdp1)) == bep->be_tcurp)
		goto notmds;

	i = tmdp1 - bep->be_tmdp;
	if (mp->b_cont == NULL) {
		len1 = mp->b_wptr - mp->b_rptr;

		if (ddi_dma_addr_setup(bep->be_dip, (struct as *)0,
			(caddr_t)mp->b_rptr, len1, DDI_DMA_RDWR,
			DDI_DMA_DONTWAIT, 0, &qe_dma_limits,
			&bep->be_dmaxh[i])) {
			berror(bep->be_dip, "ddi_dma_addr_setup failed");
			goto done;
		} else {
			if (ddi_dma_htoc(bep->be_dmaxh[i],
				0, &c))
				panic("be:  ddi_dma_htoc buf failed");
		}

		ddi_dma_sync(bep->be_dmaxh[i], (off_t)0, len1,
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->qmd_addr = c.dmac_address;
		tmdp1->qmd_flags = QMD_SOP | QMD_EOP | QMD_OWN | len1;
		QESYNCIOPB(bep, tmdp1, sizeof (struct qmd),
		    DDI_DMA_SYNC_FORDEV);
		bep->be_tmblkp[i] = mp;
	} else {
		len1 = msgsize(mp);
		if ((bp = allocb(len1 + 3 * QEBURSTSIZE, BPRI_HI)) == NULL) {
			bep->be_allocbfail++;
			goto bad;
		}

		(void) mcopymsg(mp, (u_char *)bp->b_rptr);
		mp = bp;
		bep->be_tmblkp[i] = mp;

		if (ddi_dma_addr_setup(bep->be_dip, (struct as *)0,
			(caddr_t)mp->b_rptr, len1, DDI_DMA_RDWR,
			DDI_DMA_DONTWAIT, 0, &qe_dma_limits,
			&bep->be_dmaxh[i])) {
			berror(bep->be_dip, "ddi_dma_addr_setup failed");
			goto done;
		} else {
			if (ddi_dma_htoc(bep->be_dmaxh[i],
				0, &c))
				panic("be:  ddi_dma_htoc buf failed");
		}

		ddi_dma_sync(bep->be_dmaxh[i], (off_t)0, len1,
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->qmd_addr = c.dmac_address;
		tmdp1->qmd_flags = QMD_SOP | QMD_EOP | QMD_OWN | len1;
		QESYNCIOPB(bep, tmdp1, sizeof (struct qmd),
		    DDI_DMA_SYNC_FORDEV);
	}

	bep->be_tnextp = ntmdp;
	bep->be_chanregp->control = QECB_CONTROL_TDMD;

	mutex_exit(&bep->be_xmitlock);
	TRACE_1(TR_FAC_BE, TR_BE_START_END, "bestart end:  wq %X", wq);

	if ((flags & BEPROMISC) && nmp)
		besendup(bep, nmp, bepaccept);

	return (0);

bad:
	mutex_exit(&bep->be_xmitlock);
	if (nmp)
		freemsg(nmp);
	freemsg(mp);
	return (1);

notmds:
	bep->be_notmds++;
	bep->be_wantw = 1;
	bep->be_tnextp = tmdp1;
	bereclaim(bep);
done:
	mutex_exit(&bep->be_xmitlock);
	if (nmp)
		freemsg(nmp);
	(void) putbq(wq, mp);

	TRACE_1(TR_FAC_BE, TR_BE_START_END, "bestart end:  wq %X", wq);
	return (1);
}

/*
 * Start transmission.
 * Return zero on success,
 * otherwise put msg on wq, set 'want' flag and return nonzero.
 */

static
bestart(wq, mp, bep)
queue_t *wq;
register	mblk_t	*mp;
register	struct be	*bep;
{
	volatile	struct	qmd	*tmdp1 = NULL;
	volatile	struct	qmd	*tmdp2 = NULL;
	volatile	struct	qmd	*ntmdp = NULL;
	mblk_t	*nmp = NULL;
	mblk_t  *bp;
	int	len1, len2;
	int	flags, i, j;
	ddi_dma_cookie_t c;



	TRACE_1(TR_FAC_BE, TR_BE_START_START, "bestart start:  wq %X", wq);

	if (bep->be_linkcheck) {
		if (!bep->be_linkup) {
			freemsg(mp);
			berror(bep->be_dip, "Link Down - cable problem?");
			return (0);
		}
	}

	if (bep->be_dvmaxh == NULL)
		return (bestart_slow(wq, mp, bep));

	flags = bep->be_flags;

	if (flags & BEPROMISC)
		if ((nmp = copymsg(mp)) == NULL)
			bep->be_allocbfail++;

	mutex_enter(&bep->be_xmitlock);

	if (bep->be_tnextp > bep->be_tcurp) {
		if ((bep->be_tnextp - bep->be_tcurp) > BETPENDING) {
			bereclaim(bep);
		}
	} else {
		i = bep->be_tcurp - bep->be_tnextp;
		if (i && (i < (QEC_QMDMAX - BETPENDING))) {
			bereclaim(bep);
		}
	}

	tmdp1 = bep->be_tnextp;
	if ((ntmdp = NEXTTMD(bep, tmdp1)) == bep->be_tcurp)
		goto notmds;

	i = tmdp1 - bep->be_tmdp;

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
		(void) dvma_kaddr_load(bep->be_dvmaxh, (caddr_t)mp->b_rptr,
		    len1, 2 * i, &c);
		(void) dvma_sync(bep->be_dvmaxh, 2 * i,
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->qmd_addr = c.dmac_address;
		tmdp1->qmd_flags = QMD_SOP | QMD_EOP | QMD_OWN | len1;
		QESYNCIOPB(bep, tmdp1, sizeof (struct qmd),
		    DDI_DMA_SYNC_FORDEV);
		bep->be_tmblkp[i] = mp;
	} else if ((bp->b_cont == NULL) &&
			((len2 = bp->b_wptr - bp->b_rptr) >= 4)) {
		tmdp2 = ntmdp;
		if ((ntmdp = NEXTTMD(bep, tmdp2)) == bep->be_tcurp)
			goto notmds;
		j = tmdp2 - bep->be_tmdp;
		mp->b_cont = NULL;
		bep->be_tmblkp[i] = mp;
		bep->be_tmblkp[j] = bp;
		(void) dvma_kaddr_load(bep->be_dvmaxh, (caddr_t)mp->b_rptr,
		    len1, 2 * i, &c);

		(void) dvma_sync(bep->be_dvmaxh, 2 * i,
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->qmd_addr = c.dmac_address;
		(void) dvma_kaddr_load(bep->be_dvmaxh, (caddr_t)bp->b_rptr,
		    len2, 2 * j, &c);
		(void) dvma_sync(bep->be_dvmaxh, 2 * j,
		    DDI_DMA_SYNC_FORDEV);
		tmdp2->qmd_addr = c.dmac_address;
		tmdp2->qmd_flags = QMD_EOP | QMD_OWN | len2;
		QESYNCIOPB(bep, tmdp2, sizeof (struct qmd),
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->qmd_flags = QMD_SOP | QMD_OWN | len1;
		QESYNCIOPB(bep, tmdp1, sizeof (struct qmd),
		    DDI_DMA_SYNC_FORDEV);
	} else {
		len1 = msgsize(mp);

		if ((bp = allocb(len1 + 3 * QEBURSTSIZE, BPRI_HI)) == NULL) {
			bep->be_allocbfail++;
			goto bad;
		}

		(void) mcopymsg(mp, (u_char *)bp->b_rptr);
		mp = bp;
		bep->be_tmblkp[i] = mp;

		(void) dvma_kaddr_load(bep->be_dvmaxh, (caddr_t)mp->b_rptr,
		    len1, 2 * i, &c);
		(void) dvma_sync(bep->be_dvmaxh, 2 * i,
		    DDI_DMA_SYNC_FORDEV);
		tmdp1->qmd_addr = c.dmac_address;
		tmdp1->qmd_flags = QMD_SOP | QMD_EOP | QMD_OWN | len1;
		QESYNCIOPB(bep, tmdp1, sizeof (struct qmd),
		    DDI_DMA_SYNC_FORDEV);
	}

	bep->be_tnextp = ntmdp;
	bep->be_chanregp->control = QECB_CONTROL_TDMD;

	mutex_exit(&bep->be_xmitlock);
	TRACE_1(TR_FAC_BE, TR_BE_START_END, "bestart end:  wq %X", wq);

	if ((flags & BEPROMISC) && nmp)
		besendup(bep, nmp, bepaccept);


	return (0);

bad:
	mutex_exit(&bep->be_xmitlock);
	if (nmp)
		freemsg(nmp);
	freemsg(mp);
	return (1);

notmds:
	bep->be_notmds++;
	bep->be_wantw = 1;
	bep->be_tnextp = tmdp1;
	bereclaim(bep);
done:
	mutex_exit(&bep->be_xmitlock);
	if (nmp)
		freemsg(nmp);
	(void) putbq(wq, mp);

	TRACE_1(TR_FAC_BE, TR_BE_START_END, "bestart end:  wq %X", wq);
	return (1);

}

/*
 * Initialize channel.
 * Return 0 on success, nonzero on error.
 */
static int
beinit(bep)
struct	be	*bep;
{
	volatile struct	qecb_chan	*bcp;
	volatile struct	bmac	*bmacp;
	volatile struct bmactcvr	*tcvrp;
	struct	bestr	*sbp;
	struct	qec_soft	*qsp;
	mblk_t	*bp;
	u_char	ladrf[8];
	dev_info_t	*dip;
	int		i;
	int		skip_linkcheck;
	ddi_dma_cookie_t c;

	TRACE_1(TR_FAC_BE, TR_BE_INIT_START, "beinit start:  bep %X", bep);



	bcp = bep->be_chanregp;
	bmacp = bep->be_bmacregp;
#ifndef	MPSAS
	tcvrp = bep->be_tcvrregp;
#endif	MPSAS
	dip = bep->be_dip;
	qsp = (struct qec_soft *) ddi_get_driver_private(ddi_get_parent(dip));

	mutex_enter(&bep->be_intrlock);
	rw_enter(&bestruplock, RW_WRITER);
	mutex_enter(&bep->be_xmitlock);

	bep->be_flags = 0;
	bep->be_wantw = 0;
	bep->be_inits++;
	bep->be_intr_flag = 1;

	besavecntrs(bep);

	/*
	 * Call the qecgreset() and qecinit() routine in qec
	 * Make sure the TDMD bit is clear as it indicates a valid TMD
	 * that QEC can dma on. This is in-lieu of the per channel
	 * reset performed in QED.
	 */
	if (bcp->control == 0) {
		if (bcp->control & QECB_CONTROL_TDMD) {
			berror(bep->be_dip, "TDMD did not clear");
			goto done;
		}
	}
	if ((*qsp->qs_reset_func)(qsp->qs_reset_arg)) {
		berror(dip, "global reset failed");
		goto done;
	}
	if ((*qsp->qs_init_func)(qsp->qs_init_arg)) {
		berror(dip, "qec init failed");
		goto done;
	}

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
		bep->be_flags |= BESUN4C;

	/*
	 * Reject this device if it's in a slave-only slot.
	 */
	if (ddi_slaveonly(dip) == DDI_SUCCESS) {
		berror(dip, "not used - device in slave only slot");
		goto done;
	}

	/*
	 * Allocate data structures.
	 */
	beallocthings(bep);
	befreebufs(bep);


	/*
	 * Reset RMD and TMD 'walking' pointers.
	 */
	bep->be_rnextp = bep->be_rmdp;
	bep->be_rlastp = bep->be_rmdp + BERPENDING - 1;
	bep->be_tcurp = bep->be_tmdp;
	bep->be_tnextp = bep->be_tmdp;

	/*
	 * Determine if promiscuous mode.
	 */
	for (sbp = bestrup; sbp; sbp = sbp->sb_nextp) {
		if ((sbp->sb_bep == bep) && (sbp->sb_flags & BESALLPHYS)) {
			bep->be_flags |= BEPROMISC;
			break;
		}
	}

	skip_linkcheck = 0;
	for (sbp = bestrup; sbp; sbp = sbp->sb_nextp) {
		if ((sbp->sb_bep == bep) && ((sbp->sb_state == DL_UNBOUND) ||
				(sbp->sb_state == DL_IDLE))) {
						skip_linkcheck = 1;
						break;
		}
	}

	if (!skip_linkcheck) {
		switch (bep->be_boardrev) {
		case P1_0:
			/*
			 * First check XIF mode. If mode is serial and media
			 * sense is 0, set speed to 10M, else set speed to
			 * 100M. Determine speed of the media by reading
			 * transceiver conf reg.
			 */
#ifdef	MPSAS
			bmacp->xifc = (BMAC_XIFC_ENAB |
					BMAC_XIFC_LPBK | BMAC_XIFC_RSVD);
#else	MPSAS
			bep->be_linkup = 1;
			if (!(tcvrp->pal1 & BMAC_TPAL1_MS)) {
				tcvrp->pal1 = BMAC_TPAL1_LINKTESTEN;
				berror(bep->be_dip, "100Mbit mode");
			} else {
				tcvrp->pal1 = (BMAC_TPAL1_XM_SERIAL |
						BMAC_TPAL1_LINKTESTEN);
				berror(bep->be_dip,
				"Cannot find 100Mbit tcvr, using 10Mbit");
				/*
				 * The driver needs to give ample time for
				 * the BigMAC to start sending pulses to the
				 * hub to mark the link state up.
				 * Loop here and check of the link state has
				 * gone into a pass state.
				 */
				if (tcvrp->pal1 & BMAC_TPAL1_LINKSTATUS) {
					QECDELAY((tcvrp->pal1 &
						BMAC_TPAL1_LINKSTATUS),
							BMACLNKTIME);
					if (tcvrp->pal1 & BMAC_TPAL1_LINKSTATUS)
						berror(bep->be_dip,
							"Link state down");
				}

			}
#endif	MPSAS
			break;

		case P1_5:
			be_check_transceiver(bep);
			bep->be_linkcheck = 1;
			bep->be_linkup = 0;
			bep->be_delay = 0;
			switch (bep->be_mode) {
			case BE_AUTO_SPEED:
				bep->be_linkup_10 = 0;
				bep->be_tryspeed = BE_SPEED_100;
				bep->be_ntries = BE_NTRIES_LOW;
				bep->be_nlasttries = BE_NTRIES_LOW;
				be_try_speed(bep);
				break;
			case BE_FORCE_SPEED:
				be_force_speed(bep);
				break;
			default:
				break;
			}

			break;

		default:
			break;
		}
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
	 * XXX Anything else to be configured in RXCFG
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
	 * XXX Does berxreset take care of hash and address filters also?
	 */

	if (betxreset(bep)) {
		berror(bep->be_dip, "txmac did not reset");
		goto done;
	}
	if (berxreset(bep)) {
		berror(bep->be_dip, "rxmac did not reset");
		goto done;
	}


	/*
	 * Program BigMAC with local individual ethernet address.
	 */
	bmacp->madd2 = (bep->be_ouraddr.ether_addr_octet[4] << 8) | \
		bep->be_ouraddr.ether_addr_octet[5];
	bmacp->madd1 = (bep->be_ouraddr.ether_addr_octet[2] << 8) | \
		bep->be_ouraddr.ether_addr_octet[3];
	bmacp->madd0 = (bep->be_ouraddr.ether_addr_octet[0] << 8) | \
		bep->be_ouraddr.ether_addr_octet[1];

	/*
	 * Set up multicast address filter by passing all multicast
	 * addresses through a crc generator, and then using the
	 * low order 6 bits as a index into the 64 bit logical
	 * address filter. The high order three bits select the word,
	 * while the rest of the bits select the bit within the word.
	 */
	bzero((caddr_t) ladrf, 8 * sizeof (u_char));
	for (sbp = bestrup; sbp; sbp = sbp->sb_nextp) {
		if (sbp->sb_bep == bep) {
			if ((sbp->sb_mccount == 0) &&
					!(sbp->sb_flags & BESALLMULTI))
				continue;

			if (sbp->sb_flags & BESALLMULTI) {
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
				 * result because we shift right in our CRC
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
	 * Set up the rxconfig, txconfig and seed register without enabling
	 * them the former two at this time
	 *
	 * BigMAC strips the CRC bytes by default. Since this is
	 * contrary to other pieces of hardware, this bit needs to
	 * enabled to tell BigMAC not to strip the CRC bytes.
	 * Per Shimon and Don driver filter one's own packets.
	 */
#ifdef	MPSAS
	bmacp->rxcfg = (BMAC_RXCFG_CRC | BMAC_RXCFG_FIFO);
#else	MPSAS
	bmacp->rxcfg = ((bep->be_flags & BEPROMISC ? BMAC_RXCFG_PROMIS : 0) \
				| BMAC_RXCFG_FIFO | BMAC_RXCFG_HASH);
#endif	MPSAS
	drv_usecwait(10);	/* bug 1208018 */
	bmacp->txcfg = BMAC_TXCFG_FIFO;
	bmacp->rseed = ((bep->be_ouraddr.ether_addr_octet[0] << 8) & 0x3) | \
		bep->be_ouraddr.ether_addr_octet[1];

	bmacp->xifc = (BMAC_XIFC_ENAB | BMAC_XIFC_RSVD);

	/*
	 * Clear all descriptors.
	 */
	bzero((caddr_t) bep->be_rmdp, QEC_QMDMAX * sizeof (struct qmd));
	bzero((caddr_t) bep->be_tmdp, QEC_QMDMAX * sizeof (struct qmd));

	/*
	 * Hang out receive buffers.
	 */
	for (i = 0; i < BERPENDING; i++) {
		if ((bp = allocb(BEBUFSIZE, BPRI_LO)) == NULL) {
			berror(bep->be_dip, "beinit allocb failed");
			goto done;
		}
		/* XXX alignment ??? */
		bp->b_rptr = BEROUNDUP2(bp->b_rptr, QEBURSTSIZE);
		if (bep->be_dvmarh)
			(void) dvma_kaddr_load(bep->be_dvmarh,
					    (caddr_t)bp->b_rptr,
					    (u_int)BEBUFSIZE,
					    2 * i, &c);
		else {
			/* slow case */
			if (ddi_dma_addr_setup(bep->be_dip, (struct as *)0,
				(caddr_t)bp->b_rptr, BEBUFSIZE,
				DDI_DMA_RDWR, DDI_DMA_DONTWAIT, 0,
				&qe_dma_limits,
				&bep->be_dmarh[i]))

				panic("be: ddi_dma_addr_setup of bufs failed");
			else {
				if (ddi_dma_htoc
				    (bep->be_dmarh[i],
				    0, &c))
					panic("be:  ddi_dma_htoc buf failed");
			}
		}
		bermdinit(&bep->be_rmdp[i], c.dmac_address);
		bep->be_rmblkp[i] = bp;	/* save for later use */
	}

	/*
	 * DMA sync descriptors.
	 */
	QESYNCIOPB(bep, bep->be_rmdp, 2 * QEC_QMDMAX * sizeof (struct qmd),
		DDI_DMA_SYNC_FORDEV);

	/*
	 * Initialize QEC channel registers.
	 */

	bcp->lmrxwrite = bcp->lmrxread = 0;
	bcp->lmtxwrite = bcp->lmtxread = qsp->qs_globregp->rxsize;

	bcp->rxring = (u_int) QEIOPBIOADDR(bep, bep->be_rmdp);
	bcp->txring = (u_int) QEIOPBIOADDR(bep, bep->be_tmdp);
	bmacp->mask = BMAC_MASK_TINT_RINT;
	bcp->rintm = 0;
	bcp->tintm = 0;
	/*
	 * XXX Significant performence improvements can be achieved by
	 * disabling transmit interrupt. Thus TMD's are reclaimed only
	 * when we run out of them in qestart().
	 */
	bcp->tintm = 1;

	bcp->qecerrm = 0;
	bcp->bmacerrm = 0;

	/* Howard added initialization of jamsize to work around rx crc bug */
	bmacp->jam = jamsize;

	bmacp->rxcfg |= BMAC_RXCFG_ENAB;
	bmacp->txcfg |= BMAC_TXCFG_ENAB;


	bep->be_flags |= BERUNNING;

	bewenable(bep);

done:
	mutex_exit(&bep->be_xmitlock);
	rw_exit(&bestruplock);
	mutex_exit(&bep->be_intrlock);

	return (!(bep->be_flags & BERUNNING));
}

static void
befreebufs(bep)
struct be *bep;
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
	for (i = 0; i < QEC_QMDMAX; i++) {
		if (bep->be_tmblkp[i]) {
			if (bep->be_dvmaxh)
				dvma_unload(bep->be_dvmaxh, 2 * i, DONT_FLUSH);
			freeb(bep->be_tmblkp[i]);
			bep->be_tmblkp[i] = NULL;
		}
		if (bep->be_rmblkp[i]) {
			if (bep->be_dvmarh)
				dvma_unload(bep->be_dvmarh, 2 * BERINDEX(i),
					DDI_DMA_SYNC_FORKERNEL);
			freeb(bep->be_rmblkp[i]);
			bep->be_rmblkp[i] = NULL;
		}
	}

	if (bep->be_dmarh) {
		/* slow case */
		for (i = 0; i < QEC_QMDMAX; i++) {
			if (bep->be_dmaxh[i]) {
				(void) ddi_dma_free(bep->be_dmaxh[i]);
				bep->be_dmaxh[i] = NULL;
			}
		}
		for (i = 0; i < BERPENDING; i++) {
			if (bep->be_dmarh[i]) {
				(void) ddi_dma_free(bep->be_dmarh[i]);
				bep->be_dmarh[i] = NULL;
			}
		}
	}
}

static u_int
betxreset(bep)
struct be *bep;
{
	volatile	u_int	*txconfp;
	register int	n;

	txconfp = &(bep->be_bmacregp->txcfg);
	*txconfp = 0;
	n = BMACTXRSTDELAY / QECWAITPERIOD;
	while (--n > 0) {
		if (*txconfp == 0)
			return (0);
		drv_usecwait(QECWAITPERIOD);
	}
	return (1);
}

static u_int
berxreset(bep)
struct be *bep;
{
	volatile	u_int	*rxconfp;
	register int	n;

	rxconfp = &(bep->be_bmacregp->rxcfg);
	*rxconfp = 0;
	n = BMACRXRSTDELAY / QECWAITPERIOD;
	while (--n > 0) {
		if (*rxconfp == 0)
			return (0);
		drv_usecwait(QECWAITPERIOD);
	}
	return (1);
}

/*
 * Un-initialize (STOP) QED channel.
 */
static void
beuninit(struct be *bep)
{
	/*
	 * Allow up to 'QEDRAINTIME' for pending xmit's to complete.
	 */

	QECDELAY((bep->be_tcurp == bep->be_tnextp), QEDRAINTIME);

	mutex_enter(&bep->be_intrlock);
	mutex_enter(&bep->be_xmitlock);

	bep->be_flags &= ~BERUNNING;

	(void) bebmacstop(bep);

	mutex_exit(&bep->be_xmitlock);
	mutex_exit(&bep->be_intrlock);
}

static void
beallocthings(bep)
struct	be	*bep;
{
	u_long	a;
	int		size;
	ddi_dma_cookie_t	c;

	/*
	 * Return if resources are already allocated.
	 */
	if (bep->be_rmdp)
		return;

	/*
	 * Allocate the TMD and RMD descriptors and extra for alignments.
	 * XXX Why add QEC_QMDALIGN if we are going to do a BEROUNDUP?
	 */
	size = (2 * QEC_QMDMAX * sizeof (struct qmd)) + QEC_QMDALIGN;
	if (ddi_iopb_alloc(bep->be_dip, &qe_dma_limits,
		(u_int) size,
		(caddr_t *) &bep->be_iopbkbase)) {
		panic("beallocthings:  out of iopb space");
		/*NOTREACHED*/
	}
	a = bep->be_iopbkbase;
	a = BEROUNDUP(a, QEC_QMDALIGN);
	bep->be_rmdp = (struct qmd *) a;
	a += QEC_QMDMAX * sizeof (struct qmd);
	bep->be_tmdp = (struct qmd *) a;

	/*
	 * IO map this and get an "iopb" dma handle.
	 */
	if (ddi_dma_addr_setup(bep->be_dip, (struct as *) 0,
		(caddr_t) bep->be_iopbkbase, size,
		DDI_DMA_RDWR|DDI_DMA_CONSISTENT,
		DDI_DMA_DONTWAIT, 0, &qe_dma_limits,
		&bep->be_iopbhandle))
		panic("be:  ddi_dma_addr_setup iopb failed");

	/*
	 * Initialize iopb io virtual address.
	 */
	if (ddi_dma_htoc(bep->be_iopbhandle, 0, &c))
		panic("be:  ddi_dma_htoc iopb failed");
	bep->be_iopbiobase = c.dmac_address;

	/*
	 * dvma_reserve() reserves DVMA space for private management by a
	 * device driver. Specifically we reserve n (QEC_QMDMAX * 2)
	 * pagetable enteries. Therefore we have 2 ptes for each
	 * descriptor. Since the ethernet buffers are 1518 bytes
	 * so they can at most use 2 ptes. The pte are updated when
	 * we do a dvma_kaddr_load.
	 */
	if (((dvma_reserve(bep->be_dip, &qe_dma_limits, (QEC_QMDMAX * 2),
		&bep->be_dvmaxh)) != DDI_SUCCESS) ||
	    (be_force_dma)) {
		/*
		 * The reserve call has failed. This implies
		 * that we have to fall back to the older interface
		 * which will do a ddi_dma_addr_setup for each bufer
		 */
		bep->be_dmaxh = (ddi_dma_handle_t *)
		    kmem_zalloc(((QEC_QMDMAX +  BERPENDING) *
			(sizeof (ddi_dma_handle_t))), KM_SLEEP);
		bep->be_dmarh = bep->be_dmaxh + QEC_QMDMAX;
		bep->be_dvmaxh = bep->be_dvmarh = NULL;
	} else {
		/*
		 * reserve dvma space for the receive side. If this call
		 * fails, we have ro release the resources and fall
		 * back to slow case
		 */
		if ((dvma_reserve(bep->be_dip, &qe_dma_limits,
			(BERPENDING * 2), &bep->be_dvmarh)) != DDI_SUCCESS) {
			(void) dvma_release(bep->be_dvmaxh);

			bep->be_dmaxh = (ddi_dma_handle_t *)
				kmem_zalloc(((QEC_QMDMAX +  BERPENDING) *
				    (sizeof (ddi_dma_handle_t))), KM_SLEEP);
			bep->be_dmarh = bep->be_dmaxh + QEC_QMDMAX;
			bep->be_dvmaxh = bep->be_dvmarh = NULL;
		}
	}

	/*
	 * Keep handy limit values for RMD, TMD, and Buffers.
	 */
	bep->be_rmdlimp = &((bep->be_rmdp)[QEC_QMDMAX]);
	bep->be_tmdlimp = &((bep->be_tmdp)[QEC_QMDMAX]);

	/*
	 * Zero out xmit and rcv holders.
	 */
	bzero((caddr_t) bep->be_tmblkp, sizeof (bep->be_tmblkp));
	bzero((caddr_t) bep->be_rmblkp, sizeof (bep->be_rmblkp));
}

/*
 *	First check to see if it our device interrupting.
 */
static u_int
beintr(bep)
register	struct	be	*bep;
{
	register u_int	qecsbits;
	u_int	serviced = DDI_INTR_UNCLAIMED;

	mutex_enter(&bep->be_intrlock);

	qecsbits = bep->be_chanregp->status; /* auto-clears on read */

	TRACE_1(TR_FAC_BE, TR_BE_INTR_START, "beintr start:  bep %X", bep);

	if (bep->be_intr_flag) {
		serviced = DDI_INTR_CLAIMED;
		bep->be_intr_flag = 0;
	}
	if ((qecsbits & QECB_STATUS_INTR) == 0) {
		/*
		 * XXX Because QEC forgot to add the BigMAC intr bit in
		 * QEC per channel status register, we will have to
		 * check the qec global status register to see if
		 * is a BigMAC intr.
		 */
		if (bep->be_globregp->status & QECG_STATUS_BMINT)
			bebmacerr(bep);
		else {
			mutex_exit(&bep->be_intrlock);
			TRACE_2(TR_FAC_BE, TR_BE_INTR_END,
			"beintr end: bep %X serviced %d", bep, serviced);
			return (serviced);
		}
	}

	serviced = DDI_INTR_CLAIMED;

	if (!(bep->be_flags & BERUNNING)) {
		mutex_exit(&bep->be_intrlock);
		beuninit(bep);
		return (serviced);
	}

	if (qecsbits & QECB_STATUS_TINT) {
		mutex_enter(&bep->be_xmitlock);
		bereclaim(bep);
		mutex_exit(&bep->be_xmitlock);
	}

	if (qecsbits & QECB_STATUS_ERR)
		if (qecsbits & QECB_STATUS_QEC)
			beqecerr(bep, qecsbits);

	if (qecsbits & QECB_STATUS_RINT) {
		volatile struct	qmd	*rmdp;

		rmdp = bep->be_rnextp;

		/*
		 * Sync RMD before looking at it.
		 */
		QESYNCIOPB(bep, rmdp, sizeof (struct qmd),
			DDI_DMA_SYNC_FORCPU);

		/*
		 * Loop through each RMD.
		 */
		while ((rmdp->qmd_flags & QMD_OWN) == 0) {

			beread(bep, rmdp);

			/*
			 * Increment to next RMD.
			 */
			bep->be_rnextp = rmdp = NEXTRMD(bep, rmdp);

			/*
			 * Sync the next RMD before looking at it.
			 */
			QESYNCIOPB(bep, rmdp, sizeof (struct qmd),
				DDI_DMA_SYNC_FORCPU);

		}
	}

	mutex_exit(&bep->be_intrlock);

	TRACE_1(TR_FAC_BE, TR_BE_INTR_END, "beintr end:  bep %X", bep);

	/* XXX Should we not be using DDI_INTR_CLAIMED/DDI_INTR_UNCLAIMED */
	return (serviced);
}

/*
 * Transmit completion reclaiming.
 */
static void
bereclaim(bep)
struct	be	*bep;
{
	volatile struct	qmd	*tmdp;
	int	i;
#ifdef	LATER
	int			nbytes;
#endif	LATER

	tmdp = bep->be_tcurp;

#ifdef	LATER
	/*
	 * Sync TMDs before looking at it.
	 */
	if (bep->be_tnextp > bep->be_tcurp) {
		nbytes = ((bep->be_tnextp - bep->be_tcurp)
				* sizeof (struct qmd));
		QESYNCIOPB(qep, tmdp, nbytes, DDI_DMA_SYNC_FORCPU);
	} else {
		nbytes = ((bep->be_tmdlimp - bep->be_tcurp)
				* sizeof (struct qmd));
		QESYNCIOPB(qep, tmdp, nbytes, DDI_DMA_SYNC_FORCPU);
		nbytes = ((bep->be_tnextp - bep->be_tmdp)
				* sizeof (struct qmd));
		QESYNCIOPB(qep, bep->be_tmdp, nbytes, DDI_DMA_SYNC_FORCPU);
	}
#endif	LATER

	/*
	 * Loop through each TMD.
	 */
	while ((tmdp->qmd_flags & (QMD_OWN)) == 0 &&
		(tmdp != bep->be_tnextp)) {

		bep->be_opackets++;

		i = tmdp - bep->be_tmdp;
		if (bep->be_dvmaxh)
			(void) dvma_unload(bep->be_dvmaxh, 2 * i,
						(u_int) DONT_FLUSH);
		else {
			ddi_dma_free(bep->be_dmaxh[i]);
			bep->be_dmaxh[i] = NULL;
		}

		if (bep->be_tmblkp[i]) {
			freeb(bep->be_tmblkp[i]);
			bep->be_tmblkp[i] = NULL;
		}

		tmdp = NEXTTMD(bep, tmdp);
	}

	if (tmdp != bep->be_tcurp) {
		/*
		 * we could recaim some TMDs so turn off interupts
		 */
		bep->be_tcurp = tmdp;
		if (bep->be_wantw) {
			bep->be_chanregp->tintm = 1;
			bewenable(bep);
		}
	}
	/*
	 * enable TINTS: so that even if there is no further activity
	 * qereclaim will get called
	 */
	if (bep->be_wantw)
		bep->be_chanregp->tintm = 0;
}

/*
 * Send packet upstream.
 * Assume mp->b_rptr points to ether_header.
 */
static void
besendup(bep, mp, acceptfunc)
struct	be	*bep;
mblk_t	*mp;
struct	bestr	*(*acceptfunc)();
{
	int	type;
	struct	ether_addr	*dhostp, *shostp;
	struct	bestr	*sbp, *nsbp;
	mblk_t	*nmp;
	ulong	isgroupaddr;

	TRACE_0(TR_FAC_BE, TR_BE_SENDUP_START, "besendup start");

	dhostp = &((struct ether_header *) mp->b_rptr)->ether_dhost;
	shostp = &((struct ether_header *) mp->b_rptr)->ether_shost;
	type = ((struct ether_header *) mp->b_rptr)->ether_type;

	isgroupaddr = dhostp->ether_addr_octet[0] & 01;

	/*
	 * While holding a reader lock on the linked list of streams structures,
	 * attempt to match the address criteria for each stream
	 * and pass up the raw M_DATA ("fastpath") or a DL_UNITDATA_IND.
	 */

	rw_enter(&bestruplock, RW_READER);

	if ((sbp = (*acceptfunc)(bestrup, bep, type, dhostp)) == NULL) {
		rw_exit(&bestruplock);
		freemsg(mp);
		TRACE_0(TR_FAC_BE, TR_BE_SENDUP_END, "besendup end");
		return;
	}

	/*
	 * Loop on matching open streams until (*acceptfunc)() returns NULL.
	 */
	for (; nsbp = (*acceptfunc)(sbp->sb_nextp, bep, type, dhostp);
		sbp = nsbp)
		if (canput(sbp->sb_rq->q_next))
			if (nmp = dupmsg(mp)) {
				if ((sbp->sb_flags & BESFAST) && !isgroupaddr) {
					nmp->b_rptr +=
						sizeof (struct ether_header);
					putnext(sbp->sb_rq, nmp);
				} else if (sbp->sb_flags & BESRAW)
					putnext(sbp->sb_rq, nmp);
				else if ((nmp = beaddudind(bep, nmp, shostp,
						dhostp, type, isgroupaddr)))
						putnext(sbp->sb_rq, nmp);
			} else
				bep->be_allocbfail++;
		else
			bep->be_nocanput++;


	/*
	 * Do the last one.
	 */
	if (canput(sbp->sb_rq->q_next)) {
		if ((sbp->sb_flags & BESFAST) && !isgroupaddr) {
			mp->b_rptr += sizeof (struct ether_header);
			putnext(sbp->sb_rq, mp);
		} else if (sbp->sb_flags & BESRAW)
			putnext(sbp->sb_rq, mp);
		else if ((mp = beaddudind(bep, mp, shostp, dhostp,
			type, isgroupaddr)))
			putnext(sbp->sb_rq, mp);
	} else {
		freemsg(mp);
		bep->be_nocanput++;
	}

	rw_exit(&bestruplock);
	TRACE_0(TR_FAC_BE, TR_BE_SENDUP_END, "besendup end");
}

/*
 * Test upstream destination sap and address match.
 */
static struct bestr *
beaccept(sbp, bep, type, addrp)
register	struct	bestr	*sbp;
register	struct	be	*bep;
int	type;
struct	ether_addr	*addrp;
{
	int	sap;
	int	flags;

	for (; sbp; sbp = sbp->sb_nextp) {
		sap = sbp->sb_sap;
		flags = sbp->sb_flags;

		if ((sbp->sb_bep == bep) && QESAPMATCH(sap, type, flags))
			if ((ether_cmp(addrp, &bep->be_ouraddr) == 0) ||
				(ether_cmp(addrp, &etherbroadcastaddr) == 0) ||
				(flags & BESALLPHYS) ||
				bemcmatch(sbp, addrp))
				return (sbp);
	}

	return (NULL);
}

/*
 * Test upstream destination sap and address match for BESALLPHYS only.
 */
/* ARGSUSED3 */
static struct bestr *
bepaccept(sbp, bep, type, addrp)
register	struct	bestr	*sbp;
register	struct	be	*bep;
int	type;
struct	ether_addr	*addrp;
{
	int	sap;
	int	flags;

	for (; sbp; sbp = sbp->sb_nextp) {
		sap = sbp->sb_sap;
		flags = sbp->sb_flags;

		if ((sbp->sb_bep == bep) &&
			QESAPMATCH(sap, type, flags) &&
			(flags & BESALLPHYS))
			return (sbp);
	}

	return (NULL);
}

static void
besetipq(bep)
struct	be	*bep;
{
	struct	bestr	*sbp;
	int	ok = 1;
	queue_t	*ipq = NULL;

	rw_enter(&bestruplock, RW_READER);

	for (sbp = bestrup; sbp; sbp = sbp->sb_nextp)
		if (sbp->sb_bep == bep) {
			if (sbp->sb_flags & (BESALLPHYS|BESALLSAP))
				ok = 0;
			if (sbp->sb_sap == ETHERTYPE_IP)
				if (ipq == NULL)
					ipq = sbp->sb_rq;
				else
					ok = 0; }

	rw_exit(&bestruplock);

	if (ok)
		bep->be_ipq = ipq;
	else
		bep->be_ipq = NULL;
}

/*
 * Prefix msg with a DL_UNITDATA_IND mblk and return the new msg.
 */
static mblk_t *
beaddudind(bep, mp, shostp, dhostp, type, isgroupaddr)
struct	be	*bep;
mblk_t	*mp;
struct	ether_addr	*shostp, *dhostp;
int	type;
ulong	isgroupaddr;
{
	dl_unitdata_ind_t	*dludindp;
	struct	qedladdr	*dlap;
	mblk_t	*nmp;
	int	size;

	TRACE_0(TR_FAC_BE, TR_BE_ADDUDIND_START, "beaddudind start");

	mp->b_rptr += sizeof (struct ether_header);

	/*
	 * Allocate an M_PROTO mblk for the DL_UNITDATA_IND.  */
	size = sizeof (dl_unitdata_ind_t) + BEADDRL + BEADDRL;
	if ((nmp = allocb(QEHEADROOM + size, BPRI_LO)) == NULL) {
		bep->be_allocbfail++;
		bep->be_ierrors++;
		if (bedebug)
			berror(bep->be_dip, "allocb failed");
		freemsg(mp);
		TRACE_0(TR_FAC_BE, TR_BE_ADDUDIND_END, "beaddudind end");
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
	dludindp->dl_dest_addr_length = BEADDRL;
	dludindp->dl_dest_addr_offset = sizeof (dl_unitdata_ind_t);
	dludindp->dl_src_addr_length = BEADDRL;
	dludindp->dl_src_addr_offset = sizeof (dl_unitdata_ind_t) + BEADDRL;
	dludindp->dl_group_address = isgroupaddr;

	dlap = (struct qedladdr *) (nmp->b_rptr + sizeof (dl_unitdata_ind_t));
	ether_copy(dhostp, &dlap->dl_phys);
	dlap->dl_sap = (u_short) type;

	dlap = (struct qedladdr *) (nmp->b_rptr + sizeof (dl_unitdata_ind_t)
		+ BEADDRL);
	ether_copy(shostp, &dlap->dl_phys);
	dlap->dl_sap = (u_short) type;

	/*
	 * Link the M_PROTO and M_DATA together.
	 */
	nmp->b_cont = mp;
	TRACE_0(TR_FAC_BE, TR_BE_ADDUDIND_END, "beaddudind end");
	return (nmp);
}

/*
 * Return TRUE if the given multicast address is one
 * of those that this particular Stream is interested in.
 */
static
bemcmatch(sbp, addrp)
register	struct	bestr	*sbp;
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
	if (sbp->sb_flags & BESALLMULTI)
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
 * Handle interrupts for QEC errors
 */
static void
beqecerr(bep, qecsbits)
struct	be	*bep;
u_int	qecsbits;
{
	dev_info_t	*dip = bep->be_dip;
	int	reinit = 0;

#ifdef	notdef
	/*
	 * XXX Need to distinguish between a broken cable and a link test
	 * disabled hub.
	 * This bit yet to be defined.
	 */
	if (qecsbits & QEC_STATUS_LCAR ||
		qep->qe_maceregp->phycc & MACE_PHYCC_LNKST) {
		if (qedebug)
			berror(dip, "loss of carrier error");
		bep->be_tnocar++;
		bep->be_oerrors++;
	}

	if (qecsbits & QEC_STATUS_COLCO)	/* XXX What is this bit for? */
		qep->qe_coll += 256;

#endif	notdef

	if (qecsbits & QECB_STATUS_TMDER) {
		if (bedebug)
			berror(dip, "chained packet descriptor error");
		bep->be_tmder++;
		bep->be_oerrors++;
		reinit++;
	}

	if (qecsbits & QECB_STATUS_TXLATERR) {
		if (bedebug)
			berror(dip, "sbus tx late error");
		bep->be_laterr++;
		bep->be_oerrors++;
		reinit++;
	}

	if (qecsbits & QECB_STATUS_TXPARERR) {
		if (bedebug)
			berror(dip, "sbus tx parity error");
		bep->be_parerr++;
		bep->be_oerrors++;
		reinit++;
	}

	if (qecsbits & QECB_STATUS_TXERRACK) {
		if (bedebug)
			berror(dip, "sbus tx error ack");
		bep->be_errack++;
		bep->be_oerrors++;
		reinit++;
	}

#ifdef	notdef
	if (qecsbits & QEC_STATUS_RVCCO)
		;

	if (bmacsbits & QEC_STATUS_MPCO) {
		qep->qe_missed += 256;
		qep->qe_ierrors += 256;
	}
#endif	notdef

	/* XXX Jerry, what is late collision error on the rx side? */
	if (qecsbits & QECB_STATUS_RXLATERR) {
		if (bedebug)
			berror(dip, "rx late collision error");
		bep->be_clsn++;
		bep->be_ierrors++;
	}

	if (qecsbits & QECB_STATUS_DROP) {
		if (bedebug)
			berror(dip, "rx pkt missed/drop error");
		bep->be_missed++;
		bep->be_ierrors++;
	}

	/* XXX Jerry, why are buff errors being accounted in crc? */
	if (qecsbits & QECB_STATUS_BUFF) {
		if (bedebug)
			berror(dip, "rx pkt buff error");
		bep->be_buff++;
		bep->be_ierrors++;
	}

	if (qecsbits & QECB_STATUS_RXLATERR) {
		if (bedebug)
			berror(dip, "sbus rx late error");
		bep->be_laterr++;
		bep->be_ierrors++;
		reinit++;
	}

	if (qecsbits & QECB_STATUS_RXPARERR) {
		if (bedebug)
			berror(dip, "sbus rx parity error");
		bep->be_parerr++;
		bep->be_ierrors++;
		reinit++;
	}

	if (qecsbits & QECB_STATUS_RXERRACK) {
		if (bedebug)
			berror(dip, "sbus rx error ack");
		bep->be_errack++;
		bep->be_ierrors++;
		reinit++;
	}

	if (reinit) {
		mutex_exit(&bep->be_intrlock);
		(void) beinit(bep);
		mutex_enter(&bep->be_intrlock);
	}
}

/*
 * Handle interrupts regarding BigMAC errors.
 */
static void
bebmacerr(bep)
struct	be	*bep;
{
	dev_info_t		*dip = bep->be_dip;
	u_short			bmacsbits = 0;
	volatile struct bmac	*bmacp = bep->be_bmacregp;

	bmacsbits = bmacp->stat;

	if (bmacsbits & BMAC_STAT_EXCCOLL) {
		if (bedebug)
			berror(dip, "retry error");
		bep->be_trtry += bmacp->excnt;
		bep->be_oerrors += bmacp->excnt;
		bmacp->excnt = 0;
	}

	if (bmacsbits & BMAC_STAT_LCOL) {
		if (bedebug)
			berror(dip, "late collision");
		bep->be_tlcol += bmacp->ltcnt;
		bep->be_oerrors += bmacp->ltcnt;
		bmacp->ltcnt = 0;
	}

	/*
	 * XXX Per Shimon, this error is fatal and the board needs to
	 * be reinitialized. Comments?
	 */
	if (bmacsbits & BMAC_STAT_UFLO) {
		if (bedebug)
			berror(dip, "tx fifo underflow");
		bep->be_uflo++;
		bep->be_oerrors++;
	}

#ifdef	notdef
	/*
	 * XXX This bit yet to be defined. It is part of the MCU interface
	 * and will be documented as part of the MCU spec.
	 */
	if (bmacsbits & QEC_STATUS_JAB) {
		if (qedebug)
			berror(dip, "jabber");
		qep->qe_jab++;
		qep->qe_oerrors++;
	}

#endif	notdef

	if (bmacsbits & BMAC_STAT_MAXPKT) {
		if (bedebug)
			berror(dip, "babble");
		bep->be_babl++;
		bep->be_oerrors++;
	}

	if (bmacsbits & BMAC_STAT_OFLO) {
		if (bedebug)
			berror(dip, "rx fifo overflow");
		bep->be_oflo++;
		bep->be_ierrors++;
	}

	if (bmacsbits & BMAC_STAT_ALNERR) {
		if (bedebug)
			berror(dip, "rx framing/alignment error");
		bep->be_fram += bmacp->aecnt;
		bep->be_ierrors += bmacp->aecnt;
		bmacp->aecnt = 0;
	}

	if (bmacsbits & BMAC_STAT_CRC) {
		if (bedebug)
			berror(dip, "rx crc error");
		bep->be_crc += bmacp->fecnt;
		bep->be_ierrors += bmacp->fecnt;
		bmacp->fecnt = 0;
	}
}

static void
beread_slow(bep, rmdp)
register	struct	be	*bep;
volatile	struct	qmd	*rmdp;
{
	register	int	rmdi;
	u_int		dvma_rmdi, dvma_nrmdi;
	register	mblk_t	*bp, *nbp;
	volatile	struct	qmd	*nrmdp;
	struct		ether_header	*ehp;
	queue_t		*ipq;
	int	len;
	int	nrmdi;
	ddi_dma_cookie_t c;

	TRACE_0(TR_FAC_BE, TR_BE_READ_START, "beread start");

	rmdi = rmdp - bep->be_rmdp;
	bp = bep->be_rmblkp[rmdi];
	nrmdp = NEXTRMD(bep, bep->be_rlastp);
	bep->be_rlastp = nrmdp;
	nrmdi = nrmdp - bep->be_rmdp;
	len = rmdp->qmd_flags;
	bp->b_wptr = bp->b_rptr + len;
	dvma_rmdi = BERINDEX(rmdi);
	dvma_nrmdi = BERINDEX(nrmdi);

	/*
	 * Check for short packet
	 */
	if (len < ETHERMIN) {
		nrmdp->qmd_addr = rmdp->qmd_addr;
		nrmdp->qmd_flags = BEBUFSIZE | QMD_OWN;
		bep->be_rmblkp[nrmdi] = bp;
		bep->be_rmblkp[rmdi] = NULL;
		QESYNCIOPB(bep, nrmdp, sizeof (struct qmd),
		    DDI_DMA_SYNC_FORDEV);
		bep->be_ierrors++;
		bep->be_drop++;
		TRACE_0(TR_FAC_BE, TR_BE_READ_END, "beread end");
		return;
	}

	/*
	 * Sync the received buffer before looking at it.
	 */
	ddi_dma_sync(bep->be_dmarh[dvma_rmdi], 0, len, DDI_DMA_SYNC_FORCPU);

	if ((nbp = allocb(BEBUFSIZE, BPRI_LO))) {
		nbp->b_rptr = BEROUNDUP2(nbp->b_rptr, QEBURSTSIZE);

		/*
		 * tear down the old mapping then setup a new one
		 */
		ddi_dma_free(bep->be_dmarh[dvma_rmdi]);
		bep->be_dmarh[dvma_rmdi] = NULL;

		if (ddi_dma_addr_setup(bep->be_dip, (struct as *)0,
			(caddr_t)nbp->b_rptr, BEBUFSIZE, DDI_DMA_RDWR,
			DDI_DMA_DONTWAIT, 0, &qe_dma_limits,
			&bep->be_dmarh[dvma_nrmdi]))

				panic("be: ddi_dma_addr_setup of bufs failed");
		else {
			if (ddi_dma_htoc(bep->be_dmarh[dvma_nrmdi], 0, &c))

				panic("be:  ddi_dma_htoc buf failed");
		}
		nrmdp->qmd_addr = (u_int)(c.dmac_address);
		nrmdp->qmd_flags = BEBUFSIZE | QMD_OWN;
		bep->be_rmblkp[nrmdi] = nbp;
		bep->be_rmblkp[rmdi] = NULL;
		bep->be_ipackets++;

		QESYNCIOPB(bep, nrmdp, sizeof (struct qmd),
					DDI_DMA_SYNC_FORDEV);

		ehp = (struct ether_header *)bp->b_rptr;
		ipq = bep->be_ipq;

		if ((ehp->ether_type == ETHERTYPE_IP) &&
		    ((ehp->ether_dhost.ether_addr_octet[0] & 01) == 0) &&
		    (ipq) &&
		    canput(ipq->q_next)) {

		/*
		 * bigmac bug fix 1217171 for workaround made for 1206989.
		 */

			if (ether_cmp(ehp, &bep->be_ouraddr)) {
				bep->be_dmarh[dvma_nrmdi] =
					bep->be_dmarh[dvma_rmdi];
				bep->be_dmarh[dvma_rmdi]  = NULL;
				nrmdp->qmd_addr = rmdp->qmd_addr;
				bep->be_rmblkp[nrmdi] = bp;
				bep->be_rmblkp[rmdi] = NULL;
				nrmdp->qmd_flags = BEBUFSIZE | QMD_OWN;
				QESYNCIOPB(bep, nrmdp, sizeof (struct qmd),
					DDI_DMA_SYNC_FORDEV);
				TRACE_0(TR_FAC_BE, TR_BE_READ_END,
					"beread end");
				return;
			} else {
				bp->b_rptr += sizeof (struct ether_header);
				putnext(ipq, bp);
			}
		} else {
			/* Strip the PADs for 802.3 */
			if (ehp->ether_type + sizeof (struct ether_header)
								< ETHERMIN)
				bp->b_wptr = bp->b_rptr
						+ sizeof (struct ether_header)
						+ ehp->ether_type;
			besendup(bep, bp, beaccept);
		}
	} else {
		bep->be_dmarh[dvma_nrmdi] =
			bep->be_dmarh[dvma_rmdi];
		bep->be_dmarh[dvma_rmdi]  = NULL;

		nrmdp->qmd_addr = rmdp->qmd_addr;
		bep->be_rmblkp[nrmdi] = bp;
		bep->be_rmblkp[rmdi] = NULL;
		nrmdp->qmd_flags = BEBUFSIZE | QMD_OWN;
		QESYNCIOPB(bep, nrmdp, sizeof (struct qmd),
					DDI_DMA_SYNC_FORDEV);

		bep->be_ierrors++;
		bep->be_allocbfail++;
		if (bedebug)
			berror(bep->be_dip, "allocb fail");
	}

	TRACE_0(TR_FAC_BE, TR_BE_READ_END, "beread end");
}

static void
beread(bep, rmdp)
register	struct	be	*bep;
volatile	struct	qmd	*rmdp;
{
	register	int	rmdi;
	register	mblk_t	*bp, *nbp;
	u_int		dvma_rmdi, dvma_nrmdi;
	volatile 	struct	qmd	*nrmdp;
	struct		ether_header	*ehp;
	queue_t		*ipq;
	int	len;
	int	nrmdi;
	ddi_dma_cookie_t	c;

	TRACE_0(TR_FAC_BE, TR_BE_READ_START, "beread start");

	if (bep->be_dvmaxh == NULL) {
		beread_slow(bep, rmdp);
		return;
	}

	rmdi = rmdp - bep->be_rmdp;
	dvma_rmdi = BERINDEX(rmdi);
	bp = bep->be_rmblkp[rmdi];
	nrmdp = NEXTRMD(bep, bep->be_rlastp);
	bep->be_rlastp = nrmdp;
	nrmdi = nrmdp - bep->be_rmdp;
	dvma_nrmdi = BERINDEX(rmdi);

	ASSERT(dvma_rmdi == dvma_nrmdi);

	/*
	 * QMD_OWN has been cleared by the qec hardware.
	 */
	len = rmdp->qmd_flags;

	if (len < ETHERMIN) {
		nrmdp->qmd_addr = rmdp->qmd_addr;
		nrmdp->qmd_flags = BEBUFSIZE | QMD_OWN;
		QESYNCIOPB(bep, nrmdp, sizeof (struct qmd),
		    DDI_DMA_SYNC_FORDEV);
		bep->be_rmblkp[nrmdi] = bp;
		bep->be_rmblkp[rmdi] = NULL;
		bep->be_ierrors++;
		bep->be_drop++;
		TRACE_0(TR_FAC_BE, TR_BE_READ_END, "beread end");
		return;
	}

	bp->b_wptr = bp->b_rptr + len;

	dvma_unload(bep->be_dvmarh, 2 * dvma_rmdi, DDI_DMA_SYNC_FORKERNEL);

	if ((nbp = allocb(BEBUFSIZE + QEBURSTSIZE, BPRI_LO))) {
		nbp->b_rptr = BEROUNDUP2(nbp->b_rptr, QEBURSTSIZE);

		(void) dvma_kaddr_load(bep->be_dvmarh,
		    (caddr_t)nbp->b_rptr, BEBUFSIZE, 2 * dvma_nrmdi, &c);

		nrmdp->qmd_addr = (u_int)(c.dmac_address);
		nrmdp->qmd_flags = BEBUFSIZE | QMD_OWN;
		QESYNCIOPB(bep, nrmdp, sizeof (struct qmd),
							DDI_DMA_SYNC_FORDEV);

		bep->be_rmblkp[nrmdi] = nbp;
		bep->be_rmblkp[rmdi] = NULL;
		bep->be_ipackets++;

		ehp = (struct ether_header *)bp->b_rptr;
		ipq = bep->be_ipq;

		if ((ehp->ether_type == ETHERTYPE_IP) &&
		    ((ehp->ether_dhost.ether_addr_octet[0] & 01) == 0) &&
		    (ipq) &&
		    canput(ipq->q_next)) {

		/*
		 * bigmac bug fix 1217171 for workaround made for 1206989.
		 */

			if (ether_cmp(ehp, &bep->be_ouraddr)) {
				(void) dvma_kaddr_load(bep->be_dvmarh,
					(caddr_t)bp->b_rptr, BEBUFSIZE,
						2 * dvma_nrmdi, &c);
				nrmdp->qmd_addr = (u_int)(c.dmac_address);
				bep->be_rmblkp[nrmdi] = bp;
				bep->be_rmblkp[rmdi] = NULL;
				nrmdp->qmd_flags = BEBUFSIZE | QMD_OWN;
				QESYNCIOPB(bep, nrmdp, sizeof (struct qmd),
							DDI_DMA_SYNC_FORDEV);
				TRACE_0(TR_FAC_BE, TR_BE_READ_END,
					"beread end");
				return;
			} else {
				bp->b_rptr += sizeof (struct ether_header);
				putnext(ipq, bp);
			}
		} else {
			/* Strip the PADs for 802.3 */
			if (ehp->ether_type + sizeof (struct ether_header)
								< ETHERMIN)
				bp->b_wptr = bp->b_rptr
						+ sizeof (struct ether_header)
						+ ehp->ether_type;
			besendup(bep, bp, beaccept);
		}
	} else {
		(void) dvma_kaddr_load(bep->be_dvmarh, (caddr_t)bp->b_rptr,
		    BEBUFSIZE, 2 * dvma_nrmdi, &c);
		nrmdp->qmd_addr = (u_int)(c.dmac_address);
		bep->be_rmblkp[nrmdi] = bp;
		bep->be_rmblkp[rmdi] = NULL;
		nrmdp->qmd_flags = BEBUFSIZE | QMD_OWN;
		QESYNCIOPB(bep, nrmdp, sizeof (struct qmd),
					DDI_DMA_SYNC_FORDEV);

		bep->be_ierrors++;
		bep->be_allocbfail++;
		if (bedebug)
			berror(bep->be_dip, "allocb fail");
	}

	TRACE_0(TR_FAC_BE, TR_BE_READ_END, "beread end");
}

/*
 * Start xmit on any msgs previously enqueued on any write queues.
 */
static void
bewenable(bep)
struct	be	*bep;
{
	struct	bestr	*sbp;
	queue_t	*wq;

	/*
	 * Order of wantw accesses is important.
	 */
	do {
		bep->be_wantw = 0;
		for (sbp = bestrup; sbp; sbp = sbp->sb_nextp)
			if ((wq = WR(sbp->sb_rq))->q_first)
				qenable(wq);
	} while (bep->be_wantw);
}

/*
 * Initialize RMD.
 */
static void
bermdinit(rmdp, dvma_addr)
volatile struct	qmd	*rmdp;
u_long		dvma_addr;
{
	rmdp->qmd_addr = (u_int) dvma_addr;
	rmdp->qmd_flags = BEBUFSIZE | QMD_OWN;
}

/*VARARGS*/
static void
berror(dev_info_t *dip, char *fmt, ...)
{
	static	long	last;
	static	char	*lastfmt;
	char		msg_buffer[255];
	va_list	ap;

	mutex_enter(&belock);

	/*
	 * Don't print same error message too often.
	 */
	if ((last == (hrestime.tv_sec & ~1)) && (lastfmt == fmt)) {
		mutex_exit(&belock);
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

	mutex_exit(&belock);
}

/*
 * if this is the first init do not bother to save the
 * counters. They should be 0, but do not count on it.
 */
static void
besavecntrs(bep)
struct	be *bep;
{
	volatile struct	bmac	*bmacp = bep->be_bmacregp;

	/* XXX What all gets added in ierrors and oerrors? */
	bep->be_ierrors += (bmacp->fecnt + bmacp->aecnt + bmacp->lecnt);
	bep->be_oerrors += (bmacp->ltcnt + bmacp->excnt);
	bep->be_coll += (bmacp->nccnt + bmacp->ltcnt);
	bmacp->nccnt = 0;
	bep->be_fram += bmacp->aecnt;
	bmacp->aecnt = 0;
	bep->be_crc += bmacp->fecnt;
	bmacp->fecnt = 0;
	bep->be_tlcol += bmacp->ltcnt;
	bmacp->ltcnt = 0;
	bep->be_trtry += bmacp->excnt;
	bmacp->excnt = 0;
}
