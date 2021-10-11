/*
 * Copyright (c) 1991, 1992, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_ISVAR_H
#define	_SYS_ISVAR_H

#pragma ident	"@(#)isvar.h	1.7	93/02/04 SMI"

#include <sys/ipi_chan.h>
#include <sys/isdev.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * is's IPI Command packet definition.
 *
 * Define a structure that enfolds the standard ipiq_t type
 * with information pertinent to this driver.
 *
 * It should be noted that the pn_resp array contributes
 * to kernel bloat. However, we have to store this data
 * somewhere, and the time we get it (at interrupt level)
 * is not only inconvenient for the purposes of allocating
 * it then, but also is impossible to defer since the
 * information is volatile and is paired with the command.
 */

typedef struct pnpkt {
	ipiq_t	pn_ipiq;	/* ipiq_t - must be first due to punning */
	long	pn_time;	/* value copied from pn_ipiq.q_timeout */
	ddi_dma_handle_t pn_dh;	/* dma handle for command (null if no dma) */
	caddr_t	pn_daddr;	/* TNP dma address value */
	u_short	pn_refseq;	/* reference sequence number seed */
	u_short pn_lastref;	/* last reference number generated */
	char	pn_resp[PN_RESP_SIZE];	/* response storage area */
} pnpkt_t;

/*
 * This hews to a much simpler command allocation scheme.
 *
 * 1. Since all pnpkts come from an array, the offset into pnp_iopbbase
 * for the actual commands can be determined arithmetically. Since their
 * size is fixed (at PN_MAX_CMD_SIZE), ddi_dma_sync on the iopbs is easy.
 *
 * 2. Since all pnpkts come from an array that is less than 256 wide,
 * it is easy to generate unique sequenced IPI packet reference numbers.
 * The reference number selected will be the offset into the pnp_pool
 * of the pnpkt in the low byte, plus a monotonically increasing number
 * in the high byte. This has the decided added bonus of making it easy
 * identify which packet is completing when an interrupt occurs. It also
 * is easy in the case of failure when only the address of the failed
 * iopb is known.
 *
 */

#define	PN_IOPB_ALLOC_SIZE	PN_MAX_CMDS * PN_CMD_SIZE

typedef struct pnpool {
	kmutex_t pnp_lock;		/* lock on freelist */
	kcondvar_t pnp_cv;		/* condition variable for waiters */
	int pnp_waiters;		/* waiters */
	int pnp_clist;			/* ddi callback list variable */
	pnpkt_t *pnp_free;		/* free list of commands */
	ddi_dma_handle_t pnp_ih;	/* dma handle for iopbs */
	ddi_dma_cookie_t pnp_ic;	/* dma cookie for iopbs */
	caddr_t	pnp_iopbbase;		/* base virtual address of cmds */
	pnpkt_t	pnp_pool[PN_MAX_CMDS];	/* actual pool of pnpkt_t structs */
} pnpool_t;

/*
 * Sun VME IPI string controller/channel:  controller and device structure.
 *
 * This all assumes only one channel or string controller per board.
 */
typedef struct is_ctlr {
	u_char		is_flags;	/* flags */
	u_char		is_maxq;	/* max commands to queue per panther */
	u_char		is_qact;	/* current number running */
	u_char		is_eicrwatch;	/* watchdog for EICRNB interrupts */
	is_reg_t	*is_reg;	/* I/O registers */
	kmutex_t	is_lock;	/* lock on controller */
	ipiq_t		*is_qhead;	/* head of wait queue */
	ipiq_t		*is_qtail;	/* tail of wait queue */
	dev_info_t	*is_dip;	/* controller dev_info */
	ddi_idevice_cookie_t is_id;	/* interrupt device cookie */
	ipi_config_t	is_cf;		/* IPI config stuff */
	void		(*is_slvint)();	/* handler for slave completions */
	void		(*is_facint)();	/* handler for facility completions */
	pnpool_t	is_pool;	/* command pool */
} is_ctlr_t;

/*
 * some shorthand
 */
#define	is_intpri	is_id.idev_priority
#define	is_vector	is_id.idev_vector
#define	is_addr		is_cf.ic_addr

/*
 * Flags in is_ctlr.
 */
#define	IS_PRESENT	0x1	/* controller present and online */
#define	IS_INRESET	0x10	/* in process of reset (just started) */
#define	IS_INRESET1	0x20	/* reset, after slave/fac driver notify */
#define	IS_RESETWAIT	0x40	/* waiting after doing a reset */
#define	IS_RESETWAIT1	0x80	/* waiting again to be sure */

#define	IS_RESETTING	(IS_INRESET|IS_INRESET1|IS_RESETWAIT|IS_RESETWAIT1)

#define	IS_RESET_WAIT	20		/* delay in seconds after reset */
#define	IS_INI_DELAY	45000000	/* initial reset delay, usecs */

#define	IS_EICRWATCH	10		/* seconds to wait for an EICR int */

/*
 * Union for aligning responses appropriately
 */
typedef union {
	struct ipi3resp hdr;		/* to access header fields */
	u_char	c[PN_RESP_SIZE];	/* maximum response size */
	u_long	l[PN_RESP_SIZE>>2];	/* copy a word at a time */
} respu_t;


#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_ISVAR_H */
