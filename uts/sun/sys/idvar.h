/*
 * Copyright (c) 1991, 1992 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_IDVAR_H
#define	_SYS_IDVAR_H

#pragma ident	"@(#)idvar.h	1.13	94/08/08 SMI"

#include <sys/dkio.h>
#include <sys/hdio.h>
#include <sys/dkmpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * In minor device number, lower 3 bits is logical partition;
 * rest is instance number. Instance number is then divided
 * by ID_NUNIT to get slave ID (and thus index into id_ctlr),
 * and the remainder is used to index into c->c_un.
 */
#define	ID_NUNIT	8
#define	ID_MINOR(dev)	geteminor(dev)
#define	ID_LPART(dev)	(ID_MINOR(dev) % NDKMAP)
#define	ID_INST(dev)	(ID_MINOR(dev) / NDKMAP)
#define	ID_CINST(dev)	(ID_INST(dev) / ID_NUNIT)
#define	ID_UINST(dev)	(ID_INST(dev) % ID_NUNIT)

#define	ID_MAXBUFSIZE	(63 * 1024)	/* Maximum ioctl command buffer size */

/*
 * Slave structure.
 *
 * We manange most aspects of the slave here.
 * Actually, the c_dip points to our parent
 * device, not one of the facilities which
 * we are actually driving.
 */

typedef struct id_ctlr {
	u_int		c_flags;	/* state information */
	struct id_unit	*c_un[ID_NUNIT];	/* unit pointers */
	ipiq_t		*c_rqp;		/* recovery ipiq_t */
	ipiq_t		*c_retry_q;	/* requests waiting retry */
	kmutex_t	c_lock;		/* lock on this structure */

	kcondvar_t	c_cv;		/* inititialization condvar */
	ipi_config_t	c_icp;		/* IPI Address */
#define	c_ipi_addr	c_icp.ic_addr
	dev_info_t	*c_dip;		/* slave dev_info (parent) */

	char		*c_name;	/* slave name (same as ddi_get_name) */
	struct buf 	*c_crashbuf;	/* for crash dumps */


	u_short		c_fac_flags;	/* bit per facility attached */

	u_char		c_instance;	/* slave instance */
	u_char		c_ctype;	/* Controller type (see dkio.h) */
	u_char		c_caplim;	/* misc capabilities/limits */
	/*
	 * Next facility to start search from.
	 * Not lock protected, just an optimization.
	 */
	u_char		c_nextfac;

	struct reconf_bs_parm c_reconf_bs_parm;	/* slave reconfig parm */

} id_ctlr_t;

/*
 * Flags for controller.
 */
#define	ID_C_PRESENT	0x1	/* initialized */
#define	ID_SLAVE_WAIT	0x20    /* waiting for slave queue dreain */
#define	ID_RECOVER_WAIT	0x40	/* waiting for recovery to complete */
/*	0xffffff00	bits 31-8 reserved. flags from ipi_error.h */

/*
 * Capabilities/Limits
 */
#define	ID_SEEK_ALG	0x1	/* slave does head scheduling */
#define	ID_NO_RECONF	0x2	/* slave reconfiguration attr not supported */
#define	ID_LIMIT_CMDS	0x4	/* Panther f/w bug  */

/*
 * Some shorthand
 */
#define	IDC_CONTROL(c, cmd, arg, rp)	\
	(*c->c_icp.ic_vector.id_ctrl)	\
	(c->c_icp.ic_addr, cmd, (void *)arg, (int *)rp)

#define	IDC_ALLOC(c, bp, callback, arg, qp)	\
	(*c->c_icp.ic_vector.id_setup) \
	(c->c_icp.ic_addr, (struct buf *)bp, callback, (caddr_t)arg, qp)

#define	IDC_RELSE(c, q)	(*c->c_icp.ic_vector.id_relse) (q)
#define	IDC_START(c, q)	(*c->c_icp.ic_vector.id_cmd) (q)


/*
 * Structures to manage opens/closes
 */
struct ocinfo {
	/*
	* Types BLK, MNT, CHR, SWP,
	* assumed to be types 0-3.
	*/
	u_long  lyr_open[NDKMAP];
	u_char  reg_open[OTYPCNT - 1];
};
#define	OCSIZE  sizeof (struct ocinfo)
union ocmap {
	u_char chkd[OCSIZE];
	struct ocinfo rinfo;
};
#define	lyropen rinfo.lyr_open
#define	regopen rinfo.reg_open

/*
 * Unit structure.
 *
 * XXX: un_flags covered by c_lock in slave structure.
 */
typedef struct id_unit {
	u_int		un_flags;	/* state information */
	dev_info_t	*un_dip;	/* dev_info for this unit */
	ipi_config_t	un_cfg;		/* IPI configuration information */
#define	un_ipi_addr	un_cfg.ic_addr

	ksema_t		un_ocsema;	/* open/close semaphore */
	ksema_t		un_sbs;		/* special buffer semaphore */
	struct buf 	*un_sbufp;

	u_int		un_phys_bsize;	/* physical block size */
	u_int		un_log_bsize;	/* logical block size */
	u_int		un_log_bshift;	/* log(2) un_log_bsize */
	u_int		un_first_block;	/* starting block number */

	struct kstat	*un_stats;

	kmutex_t	un_qlock;	/* mutex for queued buffers */
	kmutex_t	un_slock;	/* mutex for iostats (spin) */
	struct diskhd	un_bufs;	/* queued buffers */
	struct hdk_diag	un_diag;	/* diagnostic information */

	/*
	 * Logical partition information.
	 */

	struct dk_geom	un_g;		/* disk geometry info */
	struct un_lpart {
		struct dk_map un_map;	/* first cylinder and block count */
		u_int	un_blkno;	/* first block for partition */
	} un_lpart[NDKMAP];
	struct dk_vtoc	un_vtoc;	/* vtoc stuff from label */
	char un_asciilabel[LEN_DKL_ASCII]; /* old ascii label */

	/*
	 * Open/Close stats
	 */
	union ocmap	un_ocmap;
	u_int		un_idha_status;	/* status for multi hosted drives */
} id_unit_t;

/*
 * Flags for unit.
 *
 * Most of the rest of the status for the drive comes from
 * IPI bits (IE_PAV, etc.) in the upper 3 bytes of the
 * flags word.
 */
#define	ID_FORMATTED	0x1	/* drive appears to be formatted */
#define	ID_LABEL_VALID	0x2	/* label read and valid */
/*	0xffffff00	bits 31-8 reserved. flags from ipi_error.h */

/*
 * Flags in ipiq (see ipi_driver.h)
 */

#define	IP_DIAGNOSE	IP_DD_FLAG0	/* conditional success is an error */
#define	IP_SILENT	IP_DD_FLAG1	/* suppress messages about errors */
#define	IP_WAKEUP	IP_DD_FLAG2	/* wakeup on q after completion */
#define	IP_ABS_BLOCK	IP_DD_FLAG3	/* for ioctl rdwr - no block mapping */
#define	IP_BYTE_EXT	IP_DD_FLAG4	/* extent is in bytes, not blocks */
#define	IP_NO_RETRY	IP_DD_FLAG5	/* do not retry command on error */
#define	IP_DRV_NOREADY_OK IP_DD_FLAG7	/* its ok even if drive is not ready */

/*
 * Some shorthand
 */
#define	IDU_CONTROL(un, cmd, arg, rp)	\
	(*un->un_cfg.ic_vector.id_ctrl)	\
	(un->un_cfg.ic_addr, cmd, (void *)arg, (int *)rp)

#define	IDU_ALLOC(un, bp, callback, arg, qp)	\
	(*un->un_cfg.ic_vector.id_setup) \
	(un->un_cfg.ic_addr, bp, callback, (caddr_t)arg, qp)

#define	IDU_RELSE(un, q)	(*un->un_cfg.ic_vector.id_relse) (q)
#define	IDU_START(un, q)	(*un->un_cfg.ic_vector.id_cmd) (q)

/*
 * Error codes.
 *
 * The error code has three parts, the media/nonmedia flag, the DK severity
 * (from sun/dkio.h) and the id-driver error code.
 */
#define	IDE_CODE(media, sev, err) ((media) << 15 | (sev) << 8 | (err))
#define	IDE_MEDIA(code)		((code) & (1<<15))
#define	IDE_SEV(code)		(((code) >> 8) & 0xf)
#define	IDE_ERRNO(code)		((code) & 0xff)

#define	IDE_NOERROR	0
#define	IDE_FATAL	IDE_CODE(0, HDK_FATAL,	   1)	/* unspecified bad */
#define	IDE_CORR	IDE_CODE(1, HDK_CORRECTED,  2)	/* corrected data err */
#define	IDE_UNCORR	IDE_CODE(1, HDK_FATAL,	    3)	/* hard data error */
#define	IDE_DATA_RETRIED IDE_CODE(1, HDK_RECOVERED, 4)	/* media retried OK */
#define	IDE_RETRIED	IDE_CODE(0, HDK_RECOVERED,  5)	/* retried OK */

/*
 * Defines for how to set up for printing errors.
 * Values chosen high to or in with cmn_err definitions.
 */

#define	ID_EP_UN	0x10000	/* print unit number */
#define	ID_EP_PART	0x20000	/* print partition (and block number) */
#define		ID_EP_FAC	0x30000

/*
 * Misc
 */

#define	NBLKS(n, un)	((u_int)(n) >> (un)->un_log_bshift)

/*
 * Time limits in seconds.
 */
#define	ID_TIMEOUT	120	/* time limit on commands (in seconds) */
#define	ID_REC_TIMEOUT	16	/* time limit on recovery commands (seconds) */
#define	ID_DUMP_TIMEOUT	120	/* time limit on ISP-80 dump */
#define	ID_IDLE_TIME	60	/* wait time for ctlr to go idle */


/*
 * Maximum byte count for an I/O operation.
 */
#define	ID_MAXPHYS	(63 * 1024)	/* XXXX? */

/*
 * Constant used to determine equivalent facility parameters from
 * the slave parameter ID.  The facility ID is always 0x10 more.
 */
#define	IPI_FAC_PARM	0x10

#define	ID_NRETRY	3

/*
 * Shorthand for stats
 */
#define	IOSP	KSTAT_IO_PTR(un->un_stats)

/*
 * Reservation Status's for use by High Availability related ioclts, see mhd.h
 */
#define	IDHA_RELEASE		0x00000
#define	IDHA_RESERVE		0x00001
#define	IDHA_LOST_RESERVE	0x00002
#define	IDHA_FAILFAST		0x00004
#define	IDHA_RESET		0x00008

/* facility reset parm */
struct	fac_rst_parm {
	u_char	rst_type;	/* type of reset */
};

#ifdef __cplusplus
}
#endif

#endif	/* !_SYS_IDVAR_H */
