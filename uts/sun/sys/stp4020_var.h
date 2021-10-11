/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _STP4020_VAR_H
#define	_STP4020_VAR_H

#pragma ident	"@(#)stp4020_var.h	1.13	95/01/26 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	DRT_REG_SETS	8

struct drt_window {
	int	drtw_flags;
	int	drtw_status;
	int	drtw_ctl0;
	int	drtw_speed;	/* unprocessed speed */
	caddr_t drtw_base;	/* address in host memory */
	int	drtw_len;
	caddr_t drtw_addr;	/* address on PC Card */
};
#define	DRW_IO		0x01	/* window is an I/O window */
#define	DRW_MAPPED	0x02	/* window is mapped */
#define	DRW_ENABLED	0x04

typedef struct drt_socket {
	int	drt_flags;
	int	drt_state;
	int	drt_intmask;
	int	drt_vcc;
	int	drt_vpp1;
	int	drt_vpp2;
	int	drt_irq;	/* high or low */
	struct drt_window drt_windows[DRWINDOWS];
} drt_socket_t;

#define	DRT_SOCKET_IO		0x01
#define	DRT_CARD_ENABLED	0x02
#define	DRT_CARD_PRESENT	0x04
#define	DRT_BATTERY_DEAD	0x08
#define	DRT_BATTERY_LOW		0x10
#define	DRT_INTR_ENABLED	0x20
#define	DRT_INTR_HIPRI		0x40

#define	DRT_NUM_POWER	3	/* number of power table entries */

typedef
struct drtdev {
	u_int	pc_flags;
	int	pc_type;
	dev_info_t *pc_devinfo;
	ddi_iblock_cookie_t pc_icookie_hi;
	ddi_idevice_cookie_t pc_dcookie_hi;
	ddi_iblock_cookie_t pc_icookie_lo;
	ddi_idevice_cookie_t pc_dcookie_lo;
	kmutex_t pc_lock;
	kmutex_t pc_intr;
	caddr_t pc_addr;	/* temporary address map */
	int	pc_numsockets;
	int   (*pc_callback)(); /* used to inform nexus of events */
	int	pc_cb_arg;
	int pc_numpower;
	struct power_entry *pc_power;
	drt_socket_t pc_sockets[DRSOCKETS];
	stp4020_regs_t *pc_csr;
	struct inthandler *pc_handlers;
	u_long	pc_timestamp;	/* last time touched */
	kmutex_t pc_tslock;
	volatile struct stp4020_socket_csr_t	saved_socket[DRSOCKETS];
} drt_dev_t;

#define	PCF_CALLBACK	0x0001
#define	PCF_INTRENAB	0x0002
#define	PCF_SUSPENDED	0x0004	/* driver has been suspended */
#define	PCF_AUDIO	0x0008	/* allow audio */

struct inthandler {
	struct inthandler *next;
	u_int		  (*intr)(caddr_t);
	uint		  handler_id;
	void		  *arg;
	unsigned	  socket;
	unsigned	  irq;
	unsigned	  priority;
	ddi_softintr_t	  softid;
	ddi_iblock_cookie_t	  iblk_cookie;
	ddi_idevice_cookie_t	  idev_cookie;
};

#define	DRT_DEFAULT_INT_CAPS (SBM_CD|SBM_BVD1|SBM_BVD2|SBM_RDYBSY|SBM_WP)
#define	DRT_DEFAULT_RPT_CAPS DRT_DEFAULT_INT_CAPS
#define	DRT_DEFAULT_CTL_CAPS (0)

#define	PC_CALLBACK(drt, arg, x, e, s) (*drt->pc_callback)(arg, x, e, s)

/*
 * The following two defines are for CPR support
 */
#define	DRT_SAVE_HW_STATE	1
#define	DRT_RESTORE_HW_STATE	2

#ifdef	__cplusplus
}
#endif

#endif	/* _STP4020_VAR_H */
