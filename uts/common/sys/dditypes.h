/*
 * Copyright (c) by 1990-1994, Sun Microsystems, Inc.
 */

#ifndef	_SYS_DDITYPES_H
#define	_SYS_DDITYPES_H

#pragma ident	"@(#)dditypes.h	1.14	95/03/22 SMI"

#include <sys/isa_defs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * DMA types
 *
 * A dma handle represent a "dma object".  A dma object is an abstraction
 * that represents the potential source or destination of dma transfers to
 * or from a device.  The dma object is the highest level description of
 * the source or destination and is not suitable for the actual transfer.
 *
 * Note, that we avoid the specific references to "mapping". The fact that
 * a transfer requires mapping is an artifact of the specific architectural
 * implementation.
 */

typedef	void *ddi_dma_handle_t;


/*
 * A dma window type represents a "dma window".  A dma window is a portion
 * of a dma object or might be the entire object. A dma window has had system
 * resources allocated to it and is prepared to be transfered into or
 * out of. Examples of system resouces are DVMA mapping resources and
 * intermediate transfer buffer resources.
 *
 * We always require a dma window for an object. If the dma window
 * represents the whole dma object it has all system resources allocated
 * required by the the entire dma object. However if there is a limit to
 * system resouces necessary to setup an entire dma object, the driver
 * writer may allow the system to allocate system resources for less than
 * the entire dma object by specifying the DDI_DMA_PARTIAL flag as a
 * parameter to ddi_dma_buf_setup or ddi_dma_addr_setup or as part of a
 * ddi_dma_req structure in a call to ddi_dma_setup.
 *
 * Only one window is valid at any one time. The currently valid window is the
 * one that was most recently returned from ddi_dma_nextwin().
 *
 * Furthermore, because a call ddi_dma_nextwin will reallocate system
 * resouces to the new currently valid window, any previous window will
 * become invalid.  So, sematically it is an error to call ddi_dma_nextwin
 * before any transfers into the previous window are complete.
 */

typedef	void *ddi_dma_win_t;


/*
 * A dma segment type represents a "dma segment".  A dma segment is a
 * contiguous portion of a dma window which is entirely addressable by the
 * device for a transfer operation.  One example where dma segments are
 * required is where the system does not contain DVMA capability and
 * the object or window may be non-contiguous.  In this example the
 * object or window will be broken into smaller contiguous segments.
 * Another example is where a device or some intermediary bus adapter has
 * some upper limit on its transfer size (i.e. an 8-bit address register)
 * and has expressed this in the "dma limits" structure.  In this example
 * the object or window will be broken into smaller addressable segments.
 */

typedef	void *ddi_dma_seg_t;


typedef struct {
	union {
		u_longlong_t	_dmac_ll;	/* 64 bit dma address */
		unsigned long	_dmac_la[2];    /* 2x32 bit addres */
	} _dmu;
	u_int		dmac_size;	/* 32 bit size */
	u_int		dmac_type;	/* bus specific type bits */
} ddi_dma_cookie_t;

#define	dmac_laddress	_dmu._dmac_ll
#ifdef _LONG_LONG_HTOL
#define	dmac_notused    _dmu._dmac_la[0]
#define	dmac_address    _dmu._dmac_la[1]
#else
#define	dmac_address	_dmu._dmac_la[0]
#define	dmac_notused	_dmu._dmac_la[1]
#endif

/*
 * Interrupt types
 */

typedef void *	ddi_iblock_cookie_t;	/* lock initialization type */
typedef union {
	struct {
		u_short	_idev_vector;	/* vector - bus dependent */
		u_short	_idev_priority;	/* priority - bus dependent */
	} idu;
	u_long	idev_softint;	/* Soft interrupt register bit(s) */
} ddi_idevice_cookie_t;
#define	idev_vector	idu._idev_vector
#define	idev_priority	idu._idev_priority

/*
 * Other types
 */

typedef void *	ddi_regspec_t;		/* register specification for now  */
typedef void *	ddi_intrspec_t;		/* interrupt specification for now */
typedef void *	ddi_softintr_t;		/* soft interrupt id */
typedef void *	dev_info_t;		/* opaque device info handle */
typedef void *  ddi_devmap_data_t;	/* Mapping cookie for devmap(9E) */
typedef void *	ddi_mapdev_handle_t;	/* Mapping cookie for ddi_mapdev() */

/*
 * Define ddi_devmap_cmd types. This should probably be elsewhere.
 */

typedef enum {
	DDI_DEVMAP_VALIDATE = 0		/* Check mapping, but do nothing */
} ddi_devmap_cmd_t;

#ifdef	_KERNEL

/*
 * Device Access Attributes
 */

typedef struct ddi_device_acc_attr {
	ushort_t devacc_attr_version;
	uchar_t devacc_attr_endian_flags;
	uchar_t devacc_attr_dataorder;
} ddi_device_acc_attr_t;

#define	DDI_DEVICE_ATTR_V0 	0x0001

/*
 * endian-ness flags
 */

#define	 DDI_NEVERSWAP_ACC	0x00
#define	 DDI_STRUCTURE_LE_ACC	0x01
#define	 DDI_STRUCTURE_BE_ACC	0x02

/*
 * Data ordering values
 */
#define	DDI_STRICTORDER_ACC	0x00
#define	DDI_UNORDERED_OK_ACC    0x01
#define	DDI_MERGING_OK_ACC	0x02
#define	DDI_LOADCACHING_OK_ACC  0x03
#define	DDI_STORECACHING_OK_ACC 0x04

/*
 * Data size
 */
#define	DDI_DATA_SZ01_ACC	1
#define	DDI_DATA_SZ02_ACC	2
#define	DDI_DATA_SZ04_ACC	4
#define	DDI_DATA_SZ08_ACC	8

/*
 * Data Access Handle
 */

#define	VERS_ACCHDL 			0x0001

typedef void *ddi_acc_handle_t;

typedef struct ddi_acc_hdl {
	int	ah_vers;		/* version number */
	void	*ah_bus_private;	/* bus private pointer */
	void 	*ah_platform_private; 	/* platform private pointer */
	dev_info_t *ah_dip;		/* requesting device */

	uint_t	ah_rnumber;		/* register number */
	caddr_t	ah_addr;		/* address of mapping */

	off_t	ah_offset;		/* offset of mapping */
	off_t	ah_len;			/* length of mapping */
	uint_t	ah_hat_flags;		/* hat flags used to map object */
	uint_t	ah_pfn;			/* physical page frame number */
	uint_t	ah_pnum;		/* number of contiguous pages */
	ulong_t	ah_xfermodes;		/* data transfer modes */
	ddi_device_acc_attr_t ah_acc;	/* device access attributes */
} ddi_acc_hdl_t;

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DDITYPES_H */
