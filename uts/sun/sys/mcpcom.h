/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_MCPCOM_H
#define	_SYS_MCPCOM_H

#pragma ident	"@(#)mcpcom.h	1.9	94/12/19 SMI"

/*
 * Sun MCP Operations and Com structure definitions.
 */

#include <sys/ser_async.h>
#include <sys/mcpbuf.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZAS_REFUSE	0x00008000

typedef struct asyncline mcpaline_t;

struct __pad__ {
	char __pad_1__;
	char __pad_2__;
};

/*
 * Define the Common data structure.
 */

typedef struct mcpcom {
	struct mcp_device	*mcp_addr;	/* Addr of Register Set */
	struct zscc_device	*zs_addr;	/* Addr of Half ZS chip */
	short			zs_unit;
	short			mc_unit;
	caddr_t			zs_priv;	/* Private Protocol Data. */
	struct mcpops 		*mcp_ops;	/* Interrupt Ops Vector */

	struct _dma_chan_	*mcp_rxdma;	/* Receive DMA channel */
	struct _dma_chan_	*mcp_txdma;	/* Transmit DMA channel */

	u_char			zs_wreg[16];	/* Shadow of Write Regs. */
	u_char			zs_mask;	/* mask for char length */
	char			zs_flags;	/* Random Flags. */
	short			zs_rerror;	/* Receive Error */

	time_t			zs_dtrlow;	/* Time DTR went low. */

	kmutex_t		zs_excl;	/* ZS apadtive mutex. */
	kmutex_t		zs_excl_hi;	/* ZS spinlock mutex. */

	caddr_t			zs_state;	/* Pointer to MCP state. */
} mcpcom_t;

/*
 * Define MCP Interrupt Ops Vector.
 */

typedef struct mcpops {
	int	(*mcpop_txint)(mcpcom_t *);	/* Xmit buffer empty */
	int	(*mcpop_xsint)(mcpcom_t *);	/* External status */
	int	(*mcpop_rxint)(mcpcom_t *);	/* Receive Char Available. */
	int	(*mcpop_srint)(mcpcom_t *);	/* Special Receive Condition */
	int	(*mcpop_txend)(mcpcom_t *);	/* Transmit DMA Done. */
	int	(*mcpop_rxend)(mcpcom_t *);	/* Receive DMA done. */
	int	(*mcpop_rxchar)(mcpcom_t *, unsigned char);
	int	(*mcpop_pe)();		/* PE: printer out of paper */
	int	(*mcpop_slct)();	/* SLCT: printer is on-line. */
} mcpops_t;

#define	MCPOP_TXINT(x)		(*x->mcp_ops->mcpop_txint)(x)
#define	MCPOP_XSINT(x)		(*x->mcp_ops->mcpop_xsint)(x)
#define	MCPOP_RXINT(x)		(*x->mcp_ops->mcpop_rxint)(x)
#define	MCPOP_SRINT(x)		(*x->mcp_ops->mcpop_srint)(x)
#define	MCPOP_TXEND(x)		(*x->mcp_ops->mcpop_txend)(x)
#define	MCPOP_RXEND(x)		(*x->mcp_ops->mcpop_rxend)(x)
#define	MCPOP_RXCHAR(x, c)	(*x->mcp_ops->mcpop_rxchar)(x, c)
#define	MCPOP_PE(x)		(*x->mcp_ops->mcpop_pe)(x)
#define	MCPOP_SLCT(x)		(*x->mcp_ops->mcpop_slct)(x)

/*
 * Define Other Macros
 */

#define	MCP_WAIT_DMA		0x01
#define	MCP_PORT_ATTACHED	0x02


#define	TXENABLE	0x01

/*
 * Define Interrupt Vector Table for MCP
 */

#define	CIO_PBD0_TXEND	0x11
#define	CIO_PBD1_TXEND	0x13
#define	CIO_PBD2_TXEND	0x15
#define	CIO_PBD3_TXEND	0x17
#define	CIO_PBD4_RXEND	0x19
#define	CIO_PBD5_PPTX	0x1b
#define	CIO_PBD6_PE	0x1d
#define	CIO_PBD7_SLCT	0x1f

#define	CIO_PAD0_DSRDM		0x1
#define	CIO_PAD1_DSRDM		0x3
#define	CIO_PAD2_DSRDM		0x5
#define	CIO_PAD3_DSRDM		0x7
#define	CIO_PAD4_FIFO_E		0x9
#define	CIO_PAD5_FIFO_HF	0xb
#define	CIO_PAD6_FIFO_F		0xd

#define	SCC0_TXINT	0x0
#define	SCC0_XSINT	0x2
#define	SCC0_RXINT	0x4
#define	SCC0_SRINT	0x6
#define	SCC1_TXINT	0x8
#define	SCC1_XSINT	0xa
#define	SCC1_RXINT	0xc
#define	SCC1_SRINT	0xe

#define	SCC2_TXINT	0x10
#define	SCC2_XSINT	0x12
#define	SCC2_RXINT	0x14
#define	SCC2_SRINT	0x16
#define	SCC3_TXINT	0x18
#define	SCC3_XSINT	0x1a
#define	SCC3_RXINT	0x1c
#define	SCC3_SRINT	0x1e

#define	SCC4_TXINT	0x20
#define	SCC4_XSINT	0x22
#define	SCC4_RXINT	0x24
#define	SCC4_SRINT	0x26
#define	SCC5_TXINT	0x28
#define	SCC5_XSINT	0x2a
#define	SCC5_RXINT	0x2c
#define	SCC5_SRINT	0x2e

#define	SCC6_TXINT	0x30
#define	SCC6_XSINT	0x32
#define	SCC6_RXINT	0x34
#define	SCC6_SRINT	0x36
#define	SCC7_TXINT	0x38
#define	SCC7_XSINT	0x3a
#define	SCC7_RXINT	0x3c
#define	SCC7_SRINT	0x3e

#define	SCC8_TXINT	0x40
#define	SCC8_XSINT	0x42
#define	SCC8_RXINT	0x44
#define	SCC8_SRINT	0x46
#define	SCC9_TXINT	0x48
#define	SCC9_XSINT	0x4a
#define	SCC9_RXINT	0x4c
#define	SCC9_SRINT	0x4e

#define	SCC10_TXINT	0x50
#define	SCC10_XSINT	0x52
#define	SCC10_RXINT	0x54
#define	SCC10_SRINT	0x56
#define	SCC11_TXINT	0x58
#define	SCC11_XSINT	0x5a
#define	SCC11_RXINT	0x5c
#define	SCC11_SRINT	0x5e

#define	SCC12_TXINT	0x60
#define	SCC12_XSINT	0x62
#define	SCC12_RXINT	0x64
#define	SCC12_SRINT	0x66
#define	SCC13_TXINT	0x68
#define	SCC13_XSINT	0x6a
#define	SCC13_RXINT	0x6c
#define	SCC13_SRINT	0x6e

#define	SCC14_TXINT	0x70
#define	SCC14_XSINT	0x72
#define	SCC14_RXINT	0x74
#define	SCC14_SRINT	0x76
#define	SCC15_TXINT	0x78
#define	SCC15_XSINT	0x7a
#define	SCC15_RXINT	0x7c
#define	SCC15_SRINT	0x7e

/*
 * Define Some masks for the vectors.
 */
#define	TXINT	0x0
#define	XSINT	0x2
#define	RXINT	0x4
#define	SRINT	0x6

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_MCPCOM_H */
