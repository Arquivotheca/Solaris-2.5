/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MCPVAR_H
#define	_SYS_MCPVAR_H

#pragma ident	"@(#)mcpvar.h	1.7	94/01/06 SMI"

#include <sys/ser_async.h>
#include <sys/mcpreg.h>
#include <sys/mcpcom.h>
#include <sys/mcpiface.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Define some local driver macros
 */

#ifndef TRUE
#define	TRUE	1
#define	FALSE	0
#endif

/*
 * Define State Macros
 */

#define	N_MCP		8
#define	N_MCP_ZSDEVS	16		/* 16 per MCP device. */
#define	N_MCP_DMA	6		/* 6 DMA chips per */

#define	MCP_OUTLINE	0x100
#define	MCP_INSTANCE(x) ((getminor(x) & ~MCP_OUTLINE) / (N_MCP_ZSDEVS))
#define	MCP_ZS_UNIT(x)  ((getminor(x) & ~MCP_OUTLINE) % (N_MCP_ZSDEVS))

/*
 * Define the device state structure and device state macros.
 */

#define	SOFT_DMA_MTX(sotfp)	(&(softp)->dma_chip_mtx)

typedef struct _mcp_state_ {
	mcp_dev_t		*devp;	/* Pointer to dev registers. */
	dev_info_t		*dip;	/* Device dev_info_t */

	int			mcpsoftCAR;

	mcpcom_t		mcpcom[N_MCP_ZSDEVS + 1];
	struct asyncline	mcpaline[N_MCP_ZSDEVS + 1];
	struct zs_prog		zsp[N_MCP_ZSDEVS];

	dma_chip_t		dma_chips[N_MCP_DMA];
	kmutex_t		dma_chip_mtx;

	/* Interrupt Cookies for MCP. */
	ddi_iblock_cookie_t	iblk_cookie;
	ddi_idevice_cookie_t	idev_cookie;

	dev_t			pdev;

	mcp_iface_t		iface;
} mcp_state_t;

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_MCPVAR_H */
