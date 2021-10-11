/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MCPI8237_H
#define	_SYS_MCPI8237_H

#pragma ident	"@(#)mcpi8237.h	1.6	94/01/06 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Register Description for the Multiprotocol Communications Processor
 * (MCP).
 */

/*
 * Register description for Intel 8237 DMA Controller
 */

struct _addr_wc_ {
	volatile u_char	baddr;	/* Base and current Address Register. */
	volatile u_char	wc;	/* Base and current word count register. */
};

#define	N_ADDR_WC	4
#define	N_DMA_CHIPS	6

typedef struct _dma_device_ {
	struct	_addr_wc_ addr_wc[N_ADDR_WC];
	volatile u_char	csr;		/* W - command port, R - status port. */
	volatile u_char	request;	/* Single Request Port. */
	volatile u_char	mask;		/* Single Mask Port. */
	volatile u_char	mode;		/* Mode Port. */
	volatile u_char	clrff;		/* Clear first/last flip-flop. */
	volatile u_char	reset;		/* Master Clear */
	volatile u_char	clr_mask;
	volatile u_char	w_all_mask;
} dma_device_t;

/*
 * Register Offset Definitions.
 */

#define	DMA_OFF_CMD		8	/* w - command port */
#define	DMA_OFF_STAT		8	/* r - status port */
#define	DMA_OFF_REQ		9	/* w - single request port */
#define	DMA_OFF_MASK		10	/* w - single mask port */
#define	DMA_OFF_MODE		11	/* w - mode port */
#define	DMA_OFF_CLRFF		12	/* w - clear first/last flip-flop */
#define	DMA_OFF_RESET		13	/* w - master clear */
#define	DMA_OFF_ADDR(dma)	((dma)->d_myport)
#define	DMA_OFF_COUNT(dma)	((dma)->d_myport+1)

/*
 * Register Bit definitions for the Intel 8237
 */

#define	DMA_CSR_DISABLE		0x04	/* Controller Disable */
#define	DMA_CSR_COMP		0x08	/* Compressed Timing. */
#define	DMA_CSR_ROTPRI		0x10	/* Rotating Priority */
#define	DMA_CSR_EXTWRT		0x20	/* Extended Write. */
#define	DMA_CSR_DREQLOW		0x40	/* DREQ active low */
#define	DMA_CSR_DHACKHI		0x80	/* DACK active high */

#define	DMA_MODE_DEMAND		0x00	/* Demand xfer mode select */
#define	DMA_MODE_SINGLE		0x40	/* Single xfer mode select */
#define	DMA_MODE_BLOCK		0x80	/* Block xfer mode select */
#define	DMA_MODE_CASCADE	0xC0	/* Cascade Mode select */
#define	DMA_MODE_WRITE		0x04	/* Direction is Write. */
#define	DMA_MODE_READ		0x08	/* Direction is Read. */
#define	DMA_MODE_AUTO		0x10	/* Auto init enable */
#define	DMA_MODE_DECR		0x20	/* Decrement Address */

#define	DMA_MASK_CLEAR		0x00	/* Channel + Clear mask bit. */
#define	DMA_MASK_SET		0x04	/* Channel + set mask bit. */

/*
 * 8237 DMA Channel Information.
 */

typedef struct _dma_chan_ {
	struct _dma_chip_ *d_chip;	/* Pointer back to DMA chip */
	struct _addr_wc_  *d_myport;	/* I/O port of channel addr and wc */
	volatile char	d_chan;		/* Channel Number (0 - 3) */
	volatile char	d_dir;		/* Direction of DMA, 0 is read */
	char	xxx[2];
} dma_chan_t;

/*
 * DMA Chip information
 */

typedef struct _dma_chip_ {
	struct _dma_device_	*d_ioaddr;	/* DMA chip address */
	struct _dma_chan_	d_chans[4];	/* DMA channel (4 per chip) */
	volatile u_char		d_mask;		/* Mask of active channels. */
	u_char			xxx[3];
	u_char			*rbase;		/* Start of ram buffer. */

	caddr_t			d_priv;		/* Pointer to private data. */
} dma_chip_t;

#define	TX_DIR	0x1
#define	RX_DIR	0x0

#define	SCC_DMA	0x0
#define	PR_DMA	0x1

#define	CHIP(x)	((x) >> 2)
#define	CHAN(x)	((x) & 0x03)

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_MCPI8237_H */
