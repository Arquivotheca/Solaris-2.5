/*
 * Copyright (c) 1990-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)dvma.c 1.4	94/11/19 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/debug.h>

#define	HD	((ddi_dma_impl_t *)h)->dmai_rdip

unsigned long
dvma_pagesize(dev_info_t *dip)
{
	auto unsigned long dvmapgsz;

	(void) ddi_ctlops(dip, dip, DDI_CTLOPS_DVMAPAGESIZE,
	    NULL, (void *) &dvmapgsz);
	return (dvmapgsz);
}

int
dvma_reserve(dev_info_t *dip,  ddi_dma_lim_t *limp, u_int pages,
    ddi_dma_handle_t *handlep)
{
	auto ddi_dma_lim_t dma_lim;
	auto ddi_dma_impl_t implhdl;
	struct ddi_dma_req dmareq;
	ddi_dma_handle_t reqhdl;
	ddi_dma_impl_t *mp;
	u_int np;
	int ret;

	if (limp == (ddi_dma_lim_t *)0) {
		return (DDI_DMA_BADLIMITS);
	} else {
		dma_lim = *limp;
	}
	dmareq.dmar_limits = &dma_lim;
	dmareq.dmar_object.dmao_size = pages;
	/*
	 * pass in a dummy handle. This avoids the problem when
	 * somebody is dereferencing the handle before checking
	 * the operation. This can be avoided once we separate
	 * handle allocation and actual operation.
	 */
	bzero((caddr_t)&implhdl, sizeof (ddi_dma_impl_t));
	reqhdl = (ddi_dma_handle_t)&implhdl;

	ret = ddi_dma_mctl(dip, dip, reqhdl, DDI_DMA_RESERVE, (off_t *)&dmareq,
	    0, (caddr_t *)handlep, 0);

	if (ret == DDI_SUCCESS) {
		mp = (ddi_dma_impl_t *)(*handlep);
		np = mp->dmai_ndvmapages;
		if (mp->dmai_rflags & DMP_BYPASSNEXUS) {
			mp->dmai_minfo = kmem_alloc(
				2 * np * sizeof (u_long), KM_SLEEP);
		} else {
			mp->dmai_mapping = (u_long) kmem_alloc(
				sizeof (ddi_dma_lim_t), KM_SLEEP);
			bcopy((char *)&dma_lim, (char *)mp->dmai_mapping,
			    sizeof (ddi_dma_lim_t));
			mp->dmai_minfo = kmem_alloc(
				np * sizeof (ddi_dma_handle_t), KM_SLEEP);
		}
	}
	return (ret);
}

void
dvma_release(ddi_dma_handle_t h)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)h;
	u_int np = mp->dmai_ndvmapages;

	if (mp->dmai_rflags & DMP_BYPASSNEXUS) {
		kmem_free(mp->dmai_minfo, 2 * np * sizeof (u_long));
	} else {
		kmem_free((void *)mp->dmai_mapping, sizeof (ddi_dma_lim_t));
		kmem_free(mp->dmai_minfo, np * sizeof (ddi_dma_handle_t));
	}
	(void) ddi_dma_mctl(HD, HD, h, DDI_DMA_RELEASE, 0, 0, 0, 0);
}

void
dvma_kaddr_load(ddi_dma_handle_t h, caddr_t a, u_int len, u_int index,
	ddi_dma_cookie_t *cp)
{
	register ddi_dma_impl_t *mp = (ddi_dma_impl_t *)h;

	if (mp->dmai_rflags & DMP_BYPASSNEXUS) {
		u_long *up;

		cp->dmac_address = (u_long)a;
		cp->dmac_size = len;
		up = (u_long *)mp->dmai_minfo + 2*index;
		*up++ = (u_long)a;
		*up = (u_long)len;
	} else {
		ddi_dma_handle_t handle;
		ddi_dma_lim_t *limp;

		limp = (ddi_dma_lim_t *)mp->dmai_mapping;
		(void) ddi_dma_addr_setup(HD, NULL, a, len, DDI_DMA_RDWR,
			DDI_DMA_SLEEP, NULL, limp, &handle);
		((ddi_dma_handle_t *)mp->dmai_minfo)[index] = handle;
		(void) ddi_dma_htoc(handle, 0, cp);
	}
}

void
dvma_unload(ddi_dma_handle_t h, u_int objindex, u_int type)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)h;

	if (mp->dmai_rflags & DMP_BYPASSNEXUS) {
		u_long len, addr, *up;

		if (type != DDI_DMA_SYNC_FORKERNEL)
			return;

		up = (u_long *)mp->dmai_minfo + 2*objindex;
		addr = *up++;
		len = *up;
		if (len < MMU_PAGESIZE) {
			vac_flush((caddr_t)addr, len);
		} else {
			len = mmu_btopr(len + (addr & MMU_PAGEOFFSET));
			addr &= ~MMU_PAGEOFFSET;
			while (len != 0) {
				vac_pageflush((caddr_t)addr);
				addr += MMU_PAGESIZE;
				len--;
			}
		}
	} else {
		ddi_dma_handle_t handle;

		handle = ((ddi_dma_handle_t *)mp->dmai_minfo)[objindex];
		(void) ddi_dma_free(handle);
	}
}

void
dvma_sync(ddi_dma_handle_t h, u_int objindex, u_int type)
{
	register ddi_dma_impl_t *mp = (ddi_dma_impl_t *)h;

	if (type == DDI_DMA_SYNC_FORDEV)
		return;

	if (mp->dmai_rflags & DMP_BYPASSNEXUS) {
		u_long len, addr, *up;

		up = (u_long *)mp->dmai_minfo + 2*objindex;
		addr = *up++;
		len = *up;
		if (len < MMU_PAGESIZE) {
			vac_flush((caddr_t)addr, len);
		} else {
			len = mmu_btopr(len + (addr & MMU_PAGEOFFSET));
			addr &= ~MMU_PAGEOFFSET;
			while (len != 0) {
				vac_pageflush((caddr_t)addr);
				addr += MMU_PAGESIZE;
				len--;
			}
		}
	} else {
		ddi_dma_handle_t handle;

		handle = ((ddi_dma_handle_t *)mp->dmai_minfo)[objindex];
		(void) ddi_dma_sync(handle, 0, 0, type);
	}
}
