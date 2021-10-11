/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MCPREG_H
#define	_SYS_MCPREG_H

#pragma ident	"@(#)mcpreg.h	1.4	94/01/06 SMI"

#include <sys/zsdev.h>
#include <sys/mcpi8237.h>
#include <sys/mcpz8536.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Register and Bit decriptions for MCP registers.
 */

/*
 * Ram Buffers descriptions
 */

#define	ASYNC_BSZ	256
#define	PR_BSZ		4096

/*
 * Ram Buffer Struture
 */

struct rambuf {
	u_char	syncbuf[4][1024 * 12];		/* For Sync Driver. */
	u_char	asyncbuf[16][ASYNC_BSZ];	/* For Async driver. */
};

/*
 * Logical data structure for each fifo word on MCP
 */

struct	_fifo_val_ {
	u_char	data;		/* Received FIFO Character */
	u_char	port;		/* Second half of FIFO 16 bits word. */
				/* Bit 8 to 13 indicate Port Addr. */
				/* Bit 15 indicates FIFO empty status. */
};

/*
 * Data structure for 8 bit r/w within 32 bit words
 */

struct longword {
	u_char	ctr;		/* Valid Char. */
	u_char	res[3];		/* unused */
};

/*
 * Defines each xoff word of the xbuf on the MCP
 */

struct xbuf {
	u_char	xoff;
	u_char	res[3];
};

/*
 * Registers and chips on the MCP
 */

#define	N_DMA_CHIPS	6

/*
 * Define Register Offset Macros.
 */

#define	MCP_OFFSET_ZS		0x0000
#define	MCP_OFFSET_DMA		0x0100
#define	MCP_OFFSET_DEVCTL	0x0200
#define	MCP_OFFSET_XBUF		0x0300
#define	MCP_OFFSET_CIO		0x0400
#define	MCP_OFFSET_DEVECTOR	0x0500
#define	MCP_OFFSET_IVECTOR	0x0603
#define	MCP_OFFSET_RESET	0x0700
#define	MCP_OFFSET_BID		0x0700
#define	MCP_OFFSET_FIFO		0x1000
#define	MCP_OFFSET_RAM		0x2000

typedef struct mcp_device {
	struct zscc_device	sccs[64];	/* MCP offset 0x0 */
	struct _dma_device_	dmas[0x6];	/* MCP offset 0x100 */
	u_char			aaa[0xa0];

	struct longword		devctl[64];	/* MCP offset 0x200 */
	struct xbuf		xbuf[64];	/* MCP offset 0x300 */

	struct _ciochip_	cio;		/* MCP offset 0x400 */
	u_char			bbb[0xf8];	/* Fill in the Gap */

	struct longword		devvector;	/* MCP offset 0x500 */
	u_char			ccc[0xfc];

	u_char			fff[0x3];
	u_char			ivec;		/* MCP Offset 0x600 */
	u_char			ddd[0xfc];

	u_short			reset_bid;	/* Board id on Rd, Rest on W */
	u_char			eee[0x8fe];

	u_short			fifo[2048];	/* MCP offset 0x1000 */
	struct rambuf		ram;		/* MCP offset 0x2000 */
	u_char	printer_buf[PR_BSZ];		/* MCP offset 0xf000 */

} mcp_dev_t;

#define	FIFO_EMPTY	0xffff		/* Value indicates FIFO is empty */
#define	MCP_NOINTR	0xff

#define	DISABLE_XOFF	0x80		/* Turn Off Xoff Capability. */
#define	PCLK		(19660800/4)	/* Basic CLK rate for UARTS */

#define	MCP_DTR_ON	0x02		/* Turn on the DTR Signal (RS232) */
#define	EN_FIFO_RX	0x04		/* FIFO receive enable */
#define	EN_RS449_TX	0x10		/* Enable RS449 Drivers */

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_MCPREG_H */
