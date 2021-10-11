/*
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ident	"@(#)ddi_v7_asm.s	1.6	94/12/05 SMI"

#if defined(lint)
#include <sys/types.h>
#include <sys/sunddi.h>
#else
#include <sys/asm_linkage.h>
#include "assym.s"
#endif

#ifdef lint
/*ARGSUSED*/
uchar_t
ddi_getb(ddi_acc_handle_t handle, uchar_t *addr)
{
	return (0);
}

uchar_t
/*ARGSUSED*/
ddi_mem_getb(ddi_acc_handle_t handle, uchar_t *addr)
{
	return (0);
}

/*ARGSUSED*/
uchar_t
ddi_io_getb(ddi_acc_handle_t handle, int dev_port)
{
	return (0);
}

/*ARGSUSED*/
ushort_t
ddi_getw(ddi_acc_handle_t handle, ushort_t *addr)
{
	return (0);
}

/*ARGSUSED*/
ushort_t
ddi_mem_getw(ddi_acc_handle_t handle, ushort_t *addr)
{
	return (0);
}

/*ARGSUSED*/
ushort_t
ddi_io_getw(ddi_acc_handle_t handle, int dev_port)
{
	return (0);
}

/*ARGSUSED*/
ulong_t
ddi_getl(ddi_acc_handle_t handle, ulong_t *addr)
{
	return (0);
}

/*ARGSUSED*/
ulong_t
ddi_mem_getl(ddi_acc_handle_t handle, ulong_t *addr)
{
	return (0);
}

/*ARGSUSED*/
ulong_t
ddi_io_getl(ddi_acc_handle_t handle, int dev_port)
{
	return (0);
}

/*ARGSUSED*/
unsigned long long
ddi_getll(ddi_acc_handle_t handle, unsigned long long *addr)
{
	return (0);
}

/*ARGSUSED*/
unsigned long long
ddi_mem_getll(ddi_acc_handle_t handle, unsigned long long *addr)
{
	return (0);
}

/*ARGSUSED*/
void
ddi_putb(ddi_acc_handle_t handle, uchar_t *addr, uchar_t value) {}

/*ARGSUSED*/
void
ddi_mem_putb(ddi_acc_handle_t handle, uchar_t *addr, uchar_t value) {}

/*ARGSUSED*/
void
ddi_io_putb(ddi_acc_handle_t handle, int dev_port, uchar_t value) {}

/*ARGSUSED*/
void
ddi_putw(ddi_acc_handle_t handle, ushort_t *addr, ushort_t value) {}

/*ARGSUSED*/
void
ddi_mem_putw(ddi_acc_handle_t handle, ushort *addr, ushort_t value) {}

/*ARGSUSED*/
void
ddi_io_putw(ddi_acc_handle_t handle, int dev_port, ushort_t value) {}

/*ARGSUSED*/
void
ddi_putl(ddi_acc_handle_t handle, ulong_t *addr, ulong_t value) {}

/*ARGSUSED*/
void
ddi_mem_putl(ddi_acc_handle_t handle, ulong_t *addr, ulong_t value) {}

/*ARGSUSED*/
void
ddi_io_putl(ddi_acc_handle_t handle, int dev_port, ulong_t value) {}

/*ARGSUSED*/
void
ddi_putll(ddi_acc_handle_t handle, unsigned long long *addr,
	unsigned long long value)
{
}

/*ARGSUSED*/
void
ddi_mem_putll(ddi_acc_handle_t handle, unsigned long long *addr,
	unsigned long long value)
{
}

/*ARGSUSED*/
void
ddi_rep_getb(ddi_acc_handle_t handle, uchar_t *host_addr, uchar_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_rep_getw(ddi_acc_handle_t handle, ushort_t *host_addr, ushort_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_rep_getl(ddi_acc_handle_t handle, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_rep_getll(ddi_acc_handle_t handle,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_rep_putb(ddi_acc_handle_t handle, uchar_t *host_addr, uchar_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_rep_putw(ddi_acc_handle_t handle, ushort_t *host_addr, ushort_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_rep_putl(ddi_acc_handle_t handle, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_rep_putll(ddi_acc_handle_t handle,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_mem_rep_getb(ddi_acc_handle_t handle, uchar_t *host_addr,
	uchar_t *dev_addr, uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_mem_rep_getw(ddi_acc_handle_t handle, ushort_t *host_addr,
	ushort_t *dev_addr, uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_mem_rep_getl(ddi_acc_handle_t handle, ulong_t *host_addr,
	ulong_t *dev_addr, uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_mem_rep_getll(ddi_acc_handle_t handle,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_mem_rep_putb(ddi_acc_handle_t handle, uchar_t *host_addr,
	uchar_t *dev_addr, uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_mem_rep_putw(ddi_acc_handle_t handle, ushort_t *host_addr,
	ushort_t *dev_addr, uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_mem_rep_putl(ddi_acc_handle_t handle, ulong_t *host_addr,
	ulong_t *dev_addr, uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
void
ddi_mem_rep_putll(ddi_acc_handle_t handle,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags)
{
}

/*ARGSUSED*/
uchar_t
i_ddi_getb(ddi_acc_impl_t *hdlp, uchar_t *addr)
{
	return (0);
}

/*ARGSUSED*/
ushort_t
i_ddi_getw(ddi_acc_impl_t *hdlp, ushort_t *addr)
{
	return (0);
}

/*ARGSUSED*/
ushort_t
i_ddi_swap_getw(ddi_acc_impl_t *hdlp, ushort_t *addr)
{
	return (0);
}

/*ARGSUSED*/
ulong_t
i_ddi_getl(ddi_acc_impl_t *hdlp, ulong_t *addr)
{
	return (0);
}

/*ARGSUSED*/
unsigned long long
i_ddi_getll(ddi_acc_impl_t *hdlp, unsigned long long *addr)
{
	return (0);
}

/*ARGSUSED*/
void
i_ddi_putb(ddi_acc_impl_t *hdlp, uchar_t *addr, uchar_t value)
{
}

/*ARGSUSED*/
void
i_ddi_putw(ddi_acc_impl_t *hdlp, ushort *addr, ushort_t value)
{
}

/*ARGSUSED*/
void
i_ddi_putl(ddi_acc_impl_t *hdlp, ulong_t *addr, ulong_t value)
{
}

/*ARGSUSED*/
void
i_ddi_putll(ddi_acc_impl_t *hdlp, unsigned long long *addr,
	unsigned long long value)
{
}
#else
	ENTRY(ddi_getb)
	ALTENTRY(ddi_mem_getb)
	ALTENTRY(ddi_io_getb)
	ld	[%o0 + AHI_GETB], %g1	! f = handle->ahi_getb
	jmpl	%g1, %g0		! f(...)
	nop
	SET_SIZE(ddi_getb)

	ENTRY(ddi_getw)
	ALTENTRY(ddi_mem_getw)
	ALTENTRY(ddi_io_getw)
	ld	[%o0 + AHI_GETW], %g1	! f = handle->ahi_getw
	jmpl	%g1, %g0		! f(...)
	nop
	SET_SIZE(ddi_getw)

	ENTRY(ddi_getl)
	ALTENTRY(ddi_mem_getl)
	ALTENTRY(ddi_io_getl)
	ld	[%o0 + AHI_GETL], %g1	! f = handle->ahi_getl
	jmpl	%g1, %g0		! f(...)
	nop
	SET_SIZE(ddi_getl)

	ENTRY(ddi_getll)
	ALTENTRY(ddi_mem_getll)
	ld	[%o0 + AHI_GETLL], %g1	! f = handle->ahi_getll
	jmpl	%g1, %g0		! f(...)
	nop
	SET_SIZE(ddi_getll)

	ENTRY(ddi_putb)
	ALTENTRY(ddi_mem_putb)
	ALTENTRY(ddi_io_putb)
	ld	[%o0 + AHI_PUTB], %g1	! f = handle->ahi_putb
	jmpl	%g1, %g0		! f(...)
	nop
	SET_SIZE(ddi_putb)

	ENTRY(ddi_putw)
	ALTENTRY(ddi_mem_putw)
	ALTENTRY(ddi_io_putw)
	ld	[%o0 + AHI_PUTW], %g1	! f = handle->ahi_putw
	jmpl	%g1, %g0		! f(...)
	nop
	SET_SIZE(ddi_putw)

	ENTRY(ddi_putl)
	ALTENTRY(ddi_mem_putl)
	ALTENTRY(ddi_io_putl)
	ld	[%o0 + AHI_PUTL], %g1	! f = handle->ahi_putl
	jmpl	%g1, %g0		! f(...)
	nop
	SET_SIZE(ddi_putl)

	ENTRY(ddi_putll)
	ALTENTRY(ddi_mem_putll)
	ld	[%o0 + AHI_PUTLL], %g1	! f = handle->ahi_putll
	jmpl	%g1, %g0		! f(...)
	nop
	SET_SIZE(ddi_putll)

	ENTRY(ddi_rep_getb)
	ALTENTRY(ddi_mem_rep_getb)
	ld	[%o0 + AHI_REP_GETB], %g1
	jmpl	%g1, %g0
	nop
	SET_SIZE(ddi_rep_getb)

	ENTRY(ddi_rep_getw)
	ALTENTRY(ddi_mem_rep_getw)
	ld	[%o0 + AHI_REP_GETW], %g1
	jmpl	%g1, %g0
	nop
	SET_SIZE(ddi_rep_getw)

	ENTRY(ddi_rep_getl)
	ALTENTRY(ddi_mem_rep_getl)
	ld	[%o0 + AHI_REP_GETL], %g1
	jmpl	%g1, %g0
	nop
	SET_SIZE(ddi_rep_getl)

	ENTRY(ddi_rep_getll)
	ALTENTRY(ddi_mem_rep_getll)
	ld	[%o0 + AHI_REP_GETLL], %g1
	jmpl	%g1, %g0
	nop
	SET_SIZE(ddi_rep_getll)

	ENTRY(ddi_rep_putb)
	ALTENTRY(ddi_mem_rep_putb)
	ld	[%o0 + AHI_REP_PUTB], %g1
	jmpl	%g1, %g0
	nop
	SET_SIZE(ddi_rep_putb)

	ENTRY(ddi_rep_putw)
	ALTENTRY(ddi_mem_rep_putw)
	ld	[%o0 + AHI_REP_PUTW], %g1
	jmpl	%g1, %g0
	nop
	SET_SIZE(ddi_rep_putw)

	ENTRY(ddi_rep_putl)
	ALTENTRY(ddi_mem_rep_putl)
	ld	[%o0 + AHI_REP_PUTL], %g1
	jmpl	%g1, %g0
	nop
	SET_SIZE(ddi_rep_putl)

	ENTRY(ddi_rep_putll)
	ALTENTRY(ddi_mem_rep_putll)
	ld	[%o0 + AHI_REP_PUTLL], %g1
	jmpl	%g1, %g0
	nop
	SET_SIZE(ddi_rep_putll)

	ENTRY(i_ddi_getb)
	retl
	ldub	[%o1], %o0
	SET_SIZE(i_ddi_getb)

	ENTRY(i_ddi_getw)
	retl
	lduh	[%o1], %o0
	SET_SIZE(i_ddi_getw)

	ENTRY(i_ddi_getl)
	retl
	ld	[%o1], %o0
	SET_SIZE(i_ddi_getl)

	ENTRY(i_ddi_getll)
	retl
	ldd	[%o1], %o0
	SET_SIZE(i_ddi_getll)

	ENTRY(i_ddi_putb)
	retl
	stb	%o2, [%o1]
	SET_SIZE(i_ddi_putb)

	ENTRY(i_ddi_putw)
	retl
	sth	%o2, [%o1]
	SET_SIZE(i_ddi_putw)

	ENTRY(i_ddi_putl)
	retl
	st	%o2, [%o1]
	SET_SIZE(i_ddi_putl)

	ENTRY(i_ddi_putll)
	retl
	std	%o2, [%o1]
	SET_SIZE(i_ddi_putll)
#endif
