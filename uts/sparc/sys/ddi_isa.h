/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_DDI_ISA_H
#define	_SYS_DDI_ISA_H

#pragma ident	"@(#)ddi_isa.h	1.3	94/12/03 SMI"

#include <sys/dditypes.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * These are the data access functions which the platform
 * can choose to define as functions or macro's.
 */

/*
 * DDI interfaces defined as macro's
 */

/*
 * DDI interfaces defined as functions
 */

#ifdef	__STDC__

uchar_t
ddi_mem_getb(ddi_acc_handle_t handle, uchar_t *host_addr);

ushort_t
ddi_mem_getw(ddi_acc_handle_t handle, ushort_t *host_addr);

ulong_t
ddi_mem_getl(ddi_acc_handle_t handle, ulong_t *host_addr);

unsigned long long
ddi_mem_getll(ddi_acc_handle_t handle, unsigned long long *host_addr);

void
ddi_mem_rep_getb(ddi_acc_handle_t handle, uchar_t *host_addr,
	uchar_t *dev_addr, uint_t repcount, ulong_t flags);

void
ddi_mem_rep_getw(ddi_acc_handle_t handle, ushort_t *host_addr,
	ushort_t *dev_addr, uint_t repcount, ulong_t flags);

void
ddi_mem_rep_getl(ddi_acc_handle_t handle, ulong_t *host_addr,
	ulong_t *dev_addr, uint_t repcount, ulong_t flags);

void
ddi_mem_rep_getll(ddi_acc_handle_t handle, unsigned long long *host_addr,
	unsigned long long *dev_addr, uint_t repcount, ulong_t flags);

void
ddi_mem_putb(ddi_acc_handle_t handle, uchar_t *dev_addr, uchar_t value);

void
ddi_mem_putw(ddi_acc_handle_t handle, ushort_t *dev_addr, ushort_t value);

void
ddi_mem_putl(ddi_acc_handle_t handle, ulong_t *dev_addr, ulong_t value);

void
ddi_mem_putll(ddi_acc_handle_t handle, unsigned long long *dev_addr,
	unsigned long long value);

void
ddi_mem_rep_putb(ddi_acc_handle_t handle, uchar_t *host_addr,
	uchar_t *dev_addr, uint_t repcount, ulong_t flags);

void
ddi_mem_rep_putw(ddi_acc_handle_t handle, ushort_t *host_addr,
	ushort_t *dev_addr, uint_t repcount, ulong_t flags);

void
ddi_mem_rep_putl(ddi_acc_handle_t handle, ulong_t *host_addr,
	ulong_t *dev_addr, uint_t repcount, ulong_t flags);

void
ddi_mem_rep_putll(ddi_acc_handle_t handle, unsigned long long *host_addr,
	unsigned long long *dev_addr, uint_t repcount, ulong_t flags);

uchar_t
ddi_io_getb(ddi_acc_handle_t handle, int dev_port);

ushort_t
ddi_io_getw(ddi_acc_handle_t handle, int dev_port);

ulong_t
ddi_io_getl(ddi_acc_handle_t handle, int dev_port);

void
ddi_io_rep_getb(ddi_acc_handle_t handle,
	uchar_t *host_addr, int dev_port, uint_t repcount);

void
ddi_io_rep_getw(ddi_acc_handle_t handle,
	ushort_t *host_addr, int dev_port, uint_t repcount);

void
ddi_io_rep_getl(ddi_acc_handle_t handle,
	ulong_t *host_addr, int dev_port, uint_t repcount);

void
ddi_io_putb(ddi_acc_handle_t handle, int dev_port, uchar_t value);

void
ddi_io_putw(ddi_acc_handle_t handle, int dev_port, ushort_t value);

void
ddi_io_putl(ddi_acc_handle_t handle, int dev_port, ulong_t value);

void
ddi_io_rep_putb(ddi_acc_handle_t handle,
	uchar_t *host_addr, int dev_port, uint_t repcount);

void
ddi_io_rep_putw(ddi_acc_handle_t handle,
	ushort_t *host_addr, int dev_port, uint_t repcount);

void
ddi_io_rep_putl(ddi_acc_handle_t handle,
	ulong_t *host_addr, int dev_port, uint_t repcount);

#endif	/* __STDC__ */

/*
 * The implementation specific ddi access handle is the same for
 * all sparc v7 platforms.
 */

typedef struct ddi_acc_impl {
	ddi_acc_hdl_t	ahi_common;

	uchar_t
		(*ahi_getb)(struct ddi_acc_impl *handle, uchar_t *addr);
	ushort_t
		(*ahi_getw)(struct ddi_acc_impl *handle, ushort_t *addr);
	ulong_t
		(*ahi_getl)(struct ddi_acc_impl *handle, ulong_t *addr);
	unsigned long long
		(*ahi_getll)(struct ddi_acc_impl *handle,
			unsigned long long *addr);

	void	(*ahi_putb)(struct ddi_acc_impl *handle, uchar_t *addr,
			uchar_t value);
	void	(*ahi_putw)(struct ddi_acc_impl *handle, ushort_t *addr,
			ushort_t value);
	void	(*ahi_putl)(struct ddi_acc_impl *handle, ulong_t *addr,
			ulong_t value);
	void	(*ahi_putll)(struct ddi_acc_impl *handle,
			unsigned long long *addr,
			unsigned long long value);

	void	(*ahi_rep_getb)(struct ddi_acc_impl *handle,
			uchar_t *host_addr, uchar_t *dev_addr,
			uint_t repcount, ulong_t flags);
	void	(*ahi_rep_getw)(struct ddi_acc_impl *handle,
			ushort_t *host_addr, ushort_t *dev_addr,
			uint_t repcount, ulong_t flags);
	void	(*ahi_rep_getl)(struct ddi_acc_impl *handle,
			ulong_t *host_addr, ulong_t *dev_addr,
			uint_t repcount, ulong_t flags);
	void	(*ahi_rep_getll)(struct ddi_acc_impl *handle,
			unsigned long long *host_addr,
			unsigned long long *dev_addr,
			uint_t repcount, ulong_t flags);

	void	(*ahi_rep_putb)(struct ddi_acc_impl *handle,
			uchar_t *host_addr, uchar_t *dev_addr,
			uint_t repcount, ulong_t flags);
	void	(*ahi_rep_putw)(struct ddi_acc_impl *handle,
			ushort_t *host_addr, ushort_t *dev_addr,
			uint_t repcount, ulong_t flags);
	void	(*ahi_rep_putl)(struct ddi_acc_impl *handle,
			ulong_t *host_addr, ulong_t *dev_addr,
			uint_t repcount, ulong_t flags);
	void	(*ahi_rep_putll)(struct ddi_acc_impl *handle,
			unsigned long long *host_addr,
			unsigned long long *dev_addr,
			uint_t repcount, ulong_t flags);
} ddi_acc_impl_t;

/*
 * Input functions to memory mapped IO
 */
uchar_t
i_ddi_getb(ddi_acc_impl_t *hdlp, uchar_t *addr);

ushort_t
i_ddi_getw(ddi_acc_impl_t *hdlp, ushort *addr);

ulong_t
i_ddi_getl(ddi_acc_impl_t *hdlp, ulong_t *addr);

unsigned long long
i_ddi_getll(ddi_acc_impl_t *hdlp, unsigned long long *addr);

ushort_t
i_ddi_swap_getw(ddi_acc_impl_t *hdlp, ushort_t *addr);

ulong_t
i_ddi_swap_getl(ddi_acc_impl_t *hdlp, ulong_t *addr);

unsigned long long
i_ddi_swap_getll(ddi_acc_impl_t *hdlp, unsigned long long *addr);

/*
 * Output functions to memory mapped IO
 */
void
i_ddi_putb(ddi_acc_impl_t *hdlp, uchar_t *addr, uchar_t value);

void
i_ddi_putw(ddi_acc_impl_t *hdlp, ushort_t *addr, ushort_t value);

void
i_ddi_putl(ddi_acc_impl_t *hdlp, ulong_t *addr, ulong_t value);

void
i_ddi_putll(ddi_acc_impl_t *hdlp, unsigned long long *addr,
	unsigned long long value);

void
i_ddi_swap_putw(ddi_acc_impl_t *hdlp, ushort_t *addr, ushort_t value);

void
i_ddi_swap_putl(ddi_acc_impl_t *hdlp, ulong_t *addr, ulong_t value);

void
i_ddi_swap_putll(ddi_acc_impl_t *hdlp, unsigned long long *addr,
	unsigned long long value);

/*
 * Repeated input functions for memory mapped IO
 */
void
i_ddi_rep_getb(ddi_acc_impl_t *hdlp, uchar_t *host_addr, uchar_t *dev_addr,
	uint_t repcount, ulong_t flags);

void
i_ddi_rep_getw(ddi_acc_impl_t *hdlp, ushort_t *host_addr, ushort_t *dev_addr,
	uint_t repcount, ulong_t flags);

void
i_ddi_rep_getl(ddi_acc_impl_t *hdlp, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags);

void
i_ddi_rep_getll(ddi_acc_impl_t *hdlp,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags);

void
i_ddi_swap_rep_getw(ddi_acc_impl_t *hdlp, ushort_t *host_addr,
	ushort_t *dev_addr, uint_t repcount, ulong_t flags);

void
i_ddi_swap_rep_getl(ddi_acc_impl_t *hdlp, ulong_t *host_addr,
	ulong_t *dev_addr, uint_t repcount, ulong_t flags);

void
i_ddi_swap_rep_getll(ddi_acc_impl_t *hdlp,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags);

/*
 * Repeated output functions for memory mapped IO
 */
void
i_ddi_rep_putb(ddi_acc_impl_t *hdlp, uchar_t *host_addr, uchar_t *dev_addr,
	uint_t repcount, ulong_t flags);

void
i_ddi_rep_putw(ddi_acc_impl_t *hdlp, ushort_t *host_addr, ushort_t *dev_addr,
	uint_t repcount, ulong_t flags);

void
i_ddi_rep_putl(ddi_acc_impl_t *hdl, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags);

void
i_ddi_rep_putll(ddi_acc_impl_t *hdl,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags);

void
i_ddi_swap_rep_putw(ddi_acc_impl_t *hdlp, ushort_t *host_addr,
	ushort_t *dev_addr, uint_t repcount, ulong_t flags);

void
i_ddi_swap_rep_putl(ddi_acc_impl_t *hdl, ulong_t *host_addr,
	ulong_t *dev_addr, uint_t repcount, ulong_t flags);

void
i_ddi_swap_rep_putll(ddi_acc_impl_t *hdl, unsigned long long *host_addr,
	unsigned long long *dev_addr, uint_t repcount, ulong_t flags);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DDI_ISA_H */
