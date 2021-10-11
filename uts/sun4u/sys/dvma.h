/*
 * Copyright (c) 1991-1995, by Sun Microsystems, Inc.
 */

#ifndef _SYS_DVMA_H
#define	_SYS_DVMA_H

#pragma ident	"@(#)dvma.h	1.1	95/05/18 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	DVMAO_REV	1

struct dvma_ops  {
#ifdef __STDC__
	int dvmaops_rev;		/* rev of this structure */
	void (*dvma_kaddr_load)(ddi_dma_handle_t h, caddr_t a,
			    u_int len, u_int index, ddi_dma_cookie_t *cp);
	void (*dvma_unload)(ddi_dma_handle_t h, u_int objindex,
			    u_int view);
	void (*dvma_sync)(ddi_dma_handle_t h, u_int objindex,
			    u_int view);
#else /* __STDC__ */
	int dvmaops_rev;
	void (*dvma_kaddr_load)();
	void (*dvma_unload)();
	void (*dvma_sync)();
#endif /* __STDC__ */
};

struct	fast_dvma	{
	caddr_t softsp;
	u_int *pagecnt;
	struct dvma_ops *ops;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DVMA_H */
