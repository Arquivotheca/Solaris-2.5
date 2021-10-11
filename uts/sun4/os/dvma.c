/*
 * Copyright (c) 1990-1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)dvma.c 1.2	94/01/12 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/debug.h>

unsigned long
dvma_pagesize(dev_info_t *dip)
{
	auto unsigned long dvmapgsz;

	(void) ddi_ctlops(dip, dip, DDI_CTLOPS_DVMAPAGESIZE,
	    NULL, (void *) &dvmapgsz);
	return (dvmapgsz);
}

/*ARGSUSED*/
int
dvma_reserve(dev_info_t *dip,  ddi_dma_lim_t *limp, u_int pages,
    ddi_dma_handle_t *handlep)
{
	/*
	 * This call doesn't make sense on this platform. It's just
	 * too hard to manage dvma in an easy way. So the nexus driver
	 * has to provide the service via ddi_dma_setup(9F)
	 */
	return (DDI_DMA_NORESOURCES);
}

/*ARGSUSED*/
void
dvma_release(ddi_dma_handle_t h)
{
}

/*ARGSUSED*/
void
dvma_kaddr_load(ddi_dma_handle_t h, caddr_t a, u_int len, u_int index,
	ddi_dma_cookie_t *cp)
{
}

/*ARGSUSED*/
void
dvma_unload(ddi_dma_handle_t h, u_int objindex, u_int type)
{
}

/*ARGSUSED*/
void
dvma_sync(ddi_dma_handle_t h, u_int objindex, u_int type)
{
}
