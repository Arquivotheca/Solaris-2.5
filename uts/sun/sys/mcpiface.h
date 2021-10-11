/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MCPIFACE_H
#define	_SYS_MCPIFACE_H

#pragma ident	"@(#)mcpiface.h	1.6	94/01/06 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct mcpiface {
	caddr_t			priv;
	dev_info_t		*pdip;

	struct _dma_chan_ * (*dma_getchan)(caddr_t, int, int, int);
	unsigned short	(*dma_getwc)(struct _dma_chan_ *);
	int		(*dma_start)(struct _dma_chan_ *, char *, short);
	int		(*dma_halt)(struct _dma_chan_ *);
	void		(*zs_program)(struct zs_prog *);

} mcp_iface_t;

#define	MCP_DMA_GETCHAN(x, state, port, dir, scc) \
	(*(x)->dma_getchan)(state, port, dir, scc)
#define	MCP_DMA_GETWC(x, cp)		(*(x)->dma_getwc)(cp)
#define	MCP_DMA_START(x, cp, addr, len) (*(x)->dma_start)(cp, addr, len)
#define	MCP_DMA_HALT(x, cp)		(*(x)->dma_halt)(cp)
#define	MCP_ZS_PROGRAM(x, zspp)		(*(x)->zs_program)(zspp)

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_MCPIFACE_H */
