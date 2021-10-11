/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)ddi_v7.c	1.6	94/12/05 SMI"

/*
 * sparc v7 specific DDI implementation
 */

/*
 * indicate that this is the implementation code.
 */
#define	SUNDDI_IMPL

#include <sys/types.h>
#include <sys/kmem.h>

#include <sys/dditypes.h>
#include <sys/ddidmareq.h>
#include <sys/devops.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_isa.h>
#include <sys/ddi_implfuncs.h>

/*
 * DDI(Sun) Function and flag definitions:
 */

static int impl_acc_hdl_id = 0;

/*
 * access handle allocator
 */
ddi_acc_hdl_t *
impl_acc_hdl_get(ddi_acc_handle_t hdl)
{
	/*
	 * recast to ddi_acc_hdl_t instead of
	 * casting to ddi_acc_impl_t and then return the ah_platform_private
	 *
	 * this optimization based on the ddi_acc_hdl_t is the
	 * first member of the ddi_acc_impl_t.
	 */
	return ((ddi_acc_hdl_t *)hdl);
}

ddi_acc_handle_t
impl_acc_hdl_alloc(int (*waitfp)(caddr_t), caddr_t arg)
{
	ddi_acc_impl_t *hp;
	int sleepflag;

	sleepflag = ((waitfp == (int (*)())KM_SLEEP) ? KM_SLEEP : KM_NOSLEEP);
	/*
	 * Allocate and initialize the data access handle.
	 */
	hp = (ddi_acc_impl_t *)kmem_zalloc(sizeof (ddi_acc_impl_t), sleepflag);
	if (!hp) {
		if ((waitfp != (int (*)())KM_SLEEP) &&
			(waitfp != (int (*)())KM_NOSLEEP))
			ddi_set_callback(waitfp, arg, &impl_acc_hdl_id);
		return (NULL);
	}

	hp->ahi_common.ah_platform_private = (void *)hp;
	return ((ddi_acc_handle_t)hp);
}

void
impl_acc_hdl_free(ddi_acc_handle_t handle)
{
	ddi_acc_impl_t *hp;

	hp = (ddi_acc_impl_t *)handle;
	if (hp) {
		kmem_free(hp, sizeof (*hp));
		if (impl_acc_hdl_id)
			ddi_run_callback(&impl_acc_hdl_id);
	}
}

void
impl_acc_hdl_init(ddi_acc_hdl_t *handlep)
{
	ddi_acc_impl_t *hp;

	if (!handlep)
		return;
	hp = (ddi_acc_impl_t *)handlep->ah_platform_private;

	/*
	 * check for SW byte-swapping
	 */
	hp->ahi_getb = i_ddi_getb;
	hp->ahi_putb = i_ddi_putb;
	hp->ahi_rep_getb = i_ddi_rep_getb;
	hp->ahi_rep_putb = i_ddi_rep_putb;
	if (handlep->ah_acc.devacc_attr_endian_flags & DDI_STRUCTURE_LE_ACC) {
		hp->ahi_getw = i_ddi_swap_getw;
		hp->ahi_getl = i_ddi_swap_getl;
		hp->ahi_getll = i_ddi_swap_getll;
		hp->ahi_putw = i_ddi_swap_putw;
		hp->ahi_putl = i_ddi_swap_putl;
		hp->ahi_putll = i_ddi_swap_putll;
		hp->ahi_rep_getw = i_ddi_swap_rep_getw;
		hp->ahi_rep_getl = i_ddi_swap_rep_getl;
		hp->ahi_rep_getll = i_ddi_swap_rep_getll;
		hp->ahi_rep_putw = i_ddi_swap_rep_putw;
		hp->ahi_rep_putl = i_ddi_swap_rep_putl;
		hp->ahi_rep_putll = i_ddi_swap_rep_putll;
	} else {
		hp->ahi_getw = i_ddi_getw;
		hp->ahi_getl = i_ddi_getl;
		hp->ahi_getll = i_ddi_getll;
		hp->ahi_putw = i_ddi_putw;
		hp->ahi_putl = i_ddi_putl;
		hp->ahi_putll = i_ddi_putll;
		hp->ahi_rep_getw = i_ddi_rep_getw;
		hp->ahi_rep_getl = i_ddi_rep_getl;
		hp->ahi_rep_getll = i_ddi_rep_getll;
		hp->ahi_rep_putw = i_ddi_rep_putw;
		hp->ahi_rep_putl = i_ddi_rep_putl;
		hp->ahi_rep_putll = i_ddi_rep_putll;
	}
}

/*ARGSUSED*/
void
i_ddi_rep_getb(ddi_acc_impl_t *hp, uchar_t *host_addr, uchar_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
	uchar_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*h++ = *d;
	else
		while (repcount--)
			*h++ = *d++;
}

/*ARGSUSED*/
void
i_ddi_rep_getw(ddi_acc_impl_t *hp, ushort_t *host_addr, ushort_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
	ushort_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*h++ = *d;
	else
		while (repcount--)
			*h++ = *d++;
}

/*ARGSUSED*/
void
i_ddi_swap_rep_getw(ddi_acc_impl_t *hp, ushort_t *host_addr,
	ushort_t *dev_addr, uint_t repcount, ulong_t flags)
{
	ushort_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*h++ = ddi_swap_ushort(*d);
	else
		while (repcount--)
			*h++ = ddi_swap_ushort(*d++);
}

/*ARGSUSED*/
void
i_ddi_rep_getl(ddi_acc_impl_t *hp, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
	ulong_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*h++ = hp->ahi_getl(hp, d);
	else
		while (repcount--)
			*h++ = hp->ahi_getl(hp, d++);
}

/*ARGSUSED*/
void
i_ddi_swap_rep_getl(ddi_acc_impl_t *hp, ulong_t *host_addr,
	ulong_t *dev_addr, uint_t repcount, ulong_t flags)
{
	ulong_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*h++ = ddi_swap_ulong(*d);
	else
		while (repcount--)
			*h++ = ddi_swap_ulong(*d++);
}

/*ARGSUSED*/
void
i_ddi_rep_getll(ddi_acc_impl_t *hp, unsigned long long *host_addr,
	unsigned long long *dev_addr, uint_t repcount, ulong_t flags)
{
	unsigned long long *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*h++ = *d;
	else
		while (repcount--)
			*h++ = *d++;
}

/*ARGSUSED*/
void
i_ddi_swap_rep_getll(ddi_acc_impl_t *hp, unsigned long long *host_addr,
	unsigned long long *dev_addr, uint_t repcount, ulong_t flags)
{
	unsigned long long *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*h++ = ddi_swap_ulonglong(*d);
	else
		while (repcount--)
			*h++ = ddi_swap_ulonglong(*d++);
}

/*ARGSUSED*/
void
i_ddi_rep_putb(ddi_acc_impl_t *hp, uchar_t *host_addr, uchar_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
	uchar_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*d = *h++;
	else
		while (repcount--)
			*d++ = *h++;
}

/*ARGSUSED*/
void
i_ddi_rep_putw(ddi_acc_impl_t *hp, ushort_t *host_addr, ushort_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
	ushort_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*d = *h++;
	else
		while (repcount--)
			*d++ = *h++;
}

/*ARGSUSED*/
void
i_ddi_swap_rep_putw(ddi_acc_impl_t *hp, ushort_t *host_addr,
	ushort_t *dev_addr, uint_t repcount, ulong_t flags)
{
	ushort_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*d = ddi_swap_ushort(*h++);
	else
		while (repcount--)
			*d++ = ddi_swap_ushort(*h++);
}

/*ARGSUSED*/
void
i_ddi_rep_putl(ddi_acc_impl_t *hp, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
	ulong_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*d = *h++;
	else
		while (repcount--)
			*d++ = *h++;
}

/*ARGSUSED*/
void
i_ddi_swap_rep_putl(ddi_acc_impl_t *hp, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
	ulong_t *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*d = ddi_swap_ulong(*h++);
	else
		while (repcount--)
			*d++ = ddi_swap_ulong(*h++);
}

/*ARGSUSED*/
void
i_ddi_rep_putll(ddi_acc_impl_t *hp, unsigned long long *host_addr,
	unsigned long long *dev_addr, uint_t repcount, ulong_t flags)
{
	unsigned long long *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*d = *h++;
	else
		while (repcount--)
			*d++ = *h++;
}

/*ARGSUSED*/
void
i_ddi_swap_rep_putll(ddi_acc_impl_t *hp, unsigned long long *host_addr,
	unsigned long long *dev_addr, uint_t repcount, ulong_t flags)
{
	unsigned long long *h, *d;

	h = host_addr;
	d = dev_addr;

	if (flags == DDI_DEV_NO_AUTOINCR)
		while (repcount--)
			*d = ddi_swap_ulonglong(*h++);
	else
		while (repcount--)
			*d++ = ddi_swap_ulonglong(*h++);
}

/*ARGSUSED*/
ushort_t
i_ddi_swap_getw(ddi_acc_impl_t *hdlp, ushort *addr)
{
	return (ddi_swap_ushort(*addr));
}

/*ARGSUSED*/
ulong_t
i_ddi_swap_getl(ddi_acc_impl_t *hdlp, ulong_t *addr)
{
	return (ddi_swap_ulong(*addr));
}

/*ARGSUSED*/
unsigned long long
i_ddi_swap_getll(ddi_acc_impl_t *hdlp, unsigned long long *addr)
{
	return (ddi_swap_ulonglong(*addr));
}

/*ARGSUSED*/
void
i_ddi_swap_putw(ddi_acc_impl_t *hdlp, ushort_t *addr, ushort_t value)
{
	*addr = ddi_swap_ushort(value);
}

/*ARGSUSED*/
void
i_ddi_swap_putl(ddi_acc_impl_t *hdlp, ulong_t *addr, ulong_t value)
{
	*addr = ddi_swap_ulong(value);
}

/*ARGSUSED*/
void
i_ddi_swap_putll(ddi_acc_impl_t *hdlp, unsigned long long *addr,
	unsigned long long value)
{
	*addr = ddi_swap_ulonglong(value);
}

void
ddi_io_rep_getb(ddi_acc_handle_t handle,
	uchar_t *host_addr, int dev_port, uint_t repcount)
{
	(((ddi_acc_impl_t *)handle)->ahi_rep_getb)
		((ddi_acc_impl_t *)handle, host_addr, (uchar_t *)dev_port,
		repcount, DDI_DEV_NO_AUTOINCR);
}

void
ddi_io_rep_getw(ddi_acc_handle_t handle,
	ushort_t *host_addr, int dev_port, uint_t repcount)
{
	(((ddi_acc_impl_t *)handle)->ahi_rep_getw)
		((ddi_acc_impl_t *)handle, host_addr, (ushort_t *)dev_port,
		repcount, DDI_DEV_NO_AUTOINCR);
}

void
ddi_io_rep_getl(ddi_acc_handle_t handle,
	ulong_t *host_addr, int dev_port, uint_t repcount)
{
	(((ddi_acc_impl_t *)handle)->ahi_rep_getl)
		((ddi_acc_impl_t *)handle, host_addr, (ulong_t *)dev_port,
		repcount, DDI_DEV_NO_AUTOINCR);
}

void
ddi_io_rep_putb(ddi_acc_handle_t handle,
	uchar_t *host_addr, int dev_port, uint_t repcount)
{
	(((ddi_acc_impl_t *)handle)->ahi_rep_putb)
		((ddi_acc_impl_t *)handle, host_addr, (uchar_t *)dev_port,
		repcount, DDI_DEV_NO_AUTOINCR);
}

void
ddi_io_rep_putw(ddi_acc_handle_t handle,
	ushort_t *host_addr, int dev_port, uint_t repcount)
{
	(((ddi_acc_impl_t *)handle)->ahi_rep_putw)
		((ddi_acc_impl_t *)handle, host_addr, (ushort_t *)dev_port,
		repcount, DDI_DEV_NO_AUTOINCR);
}

void
ddi_io_rep_putl(ddi_acc_handle_t handle,
	ulong_t *host_addr, int dev_port, uint_t repcount)
{
	(((ddi_acc_impl_t *)handle)->ahi_rep_putl)
		((ddi_acc_impl_t *)handle, host_addr, (ulong_t *)dev_port,
		repcount, DDI_DEV_NO_AUTOINCR);
}
