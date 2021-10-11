/*
 * Copyright (c) 1990-1995, by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)ddi_v9_asm.s 1.6     95/03/31 SMI"

#include <sys/asi.h>
#include <sys/asm_linkage.h>
#ifndef lint
#include <assym.s>
#endif

#if defined(lint)
#include <sys/types.h>
#include <sys/sunddi.h>
#endif  /* lint */

/*
 * This file implements the following ddi common access 
 * functions:
 *
 *	ddi_get{b,h,l,ll}
 *	ddi_put{b,h,l.ll}
 *
 * Assumptions:
 *
 *	There is no need to check the access handle.  We assume
 *	byte swapping will be done by the mmu and the address is
 *	always accessible via ld/st instructions.
 */

#if defined(lint)
/*ARGSUSED*/
uchar_t
ddi_getb(ddi_acc_handle_t handle, uchar_t *addr) { return (0); }

/*ARGSUSED*/
uchar_t
ddi_mem_getb(ddi_acc_handle_t handle, uchar_t *addr) { return (0); }

/*ARGSUSED*/
uchar_t
ddi_io_getb(ddi_acc_handle_t handle, int dev_port) { return (0); }

/*ARGSUSED*/
ushort_t
ddi_getw(ddi_acc_handle_t handle, ushort_t *addr) { return (0); }

/*ARGSUSED*/
ushort_t
ddi_mem_getw(ddi_acc_handle_t handle, ushort_t *addr) { return (0); }

/*ARGSUSED*/
ushort_t ddi_io_getw(ddi_acc_handle_t handle, int dev_port) { return (0); }

/*ARGSUSED*/
ulong_t
ddi_getl(ddi_acc_handle_t handle, ulong_t *addr) { return (0); }

/*ARGSUSED*/
ulong_t
ddi_mem_getl(ddi_acc_handle_t handle, ulong_t *addr) {return (0); }

/*ARGSUSED*/
ulong_t
ddi_io_getl(ddi_acc_handle_t handle, int dev_port) { return (0); }

/*ARGSUSED*/
unsigned long long
ddi_getll(ddi_acc_handle_t handle, unsigned long long *addr) { return (0); }

/*ARGSUSED*/
unsigned long long
ddi_mem_getll(ddi_acc_handle_t handle, unsigned long long *addr) { return (0); }

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
ddi_puth(ddi_acc_handle_t handle, ushort_t *addr, ushort_t value) {}

/*ARGSUSED*/
void
ddi_mem_puth(ddi_acc_handle_t handle, ushort_t *addr, ushort_t value) {}

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
	unsigned long long value) {}

/*ARGSUSED*/
void
ddi_mem_putll(ddi_acc_handle_t handle, unsigned long long *addr,
	unsigned long long value) {}

/*ARGSUSED*/
void
ddi_rep_getb(ddi_acc_handle_t handle, uchar_t *host_addr, uchar_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_rep_getw(ddi_acc_handle_t handle, ushort_t *host_addr, ushort_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_rep_getl(ddi_acc_handle_t handle, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_rep_getll(ddi_acc_handle_t handle,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_rep_putb(ddi_acc_handle_t handle, uchar_t *host_addr, uchar_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_rep_putw(ddi_acc_handle_t handle, ushort_t *host_addr, ushort_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_rep_putl(ddi_acc_handle_t handle, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_rep_putll(ddi_acc_handle_t handle,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_mem_rep_getb(ddi_acc_handle_t handle, uchar_t *host_addr, uchar_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_mem_rep_getw(ddi_acc_handle_t handle, ushort_t *host_addr, ushort_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_mem_rep_getl(ddi_acc_handle_t handle, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_mem_rep_getll(ddi_acc_handle_t handle,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_mem_rep_putb(ddi_acc_handle_t handle, uchar_t *host_addr, uchar_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_mem_rep_putw(ddi_acc_handle_t handle, ushort_t *host_addr, ushort_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_mem_rep_putl(ddi_acc_handle_t handle, ulong_t *host_addr, ulong_t *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_mem_rep_putll(ddi_acc_handle_t handle,
	unsigned long long *host_addr, unsigned long long *dev_addr,
	uint_t repcount, ulong_t flags) {}

/*ARGSUSED*/
void
ddi_io_rep_getb(ddi_acc_handle_t handle,
	uchar_t *host_addr, int dev_port, uint_t repcount) {}

/*ARGSUSED*/
void
ddi_io_rep_getw(ddi_acc_handle_t handle,
	ushort_t *host_addr, int dev_port, uint_t repcount) {}

/*ARGSUSED*/
void
ddi_io_rep_getl(ddi_acc_handle_t handle,
	ulong_t *host_addr, int dev_port, uint_t repcount) {}

/*ARGSUSED*/
void
ddi_io_rep_putb(ddi_acc_handle_t handle,
	uchar_t *host_addr, int dev_port, uint_t repcount) {}

/*ARGSUSED*/
void
ddi_io_rep_putw(ddi_acc_handle_t handle,
	ushort_t *host_addr, int dev_port, uint_t repcount) {}

/*ARGSUSED*/
void
ddi_io_rep_putl(ddi_acc_handle_t handle,
	ulong_t *host_addr, int dev_port, uint_t repcount) {}
#else
	ENTRY(ddi_getb)
	ALTENTRY(ddi_mem_getb)
	ALTENTRY(ddi_io_getb)
	retl
	ldub	[%o1], %o0
	SET_SIZE(ddi_getb)

	ENTRY(ddi_getw)
	ALTENTRY(ddi_mem_getw)
	ALTENTRY(ddi_io_getw)
	retl
	lduh	[%o1], %o0
	SET_SIZE(ddi_getw)

	ENTRY(ddi_getl)
	ALTENTRY(ddi_mem_getl)
	ALTENTRY(ddi_io_getl)
	retl
	ld	[%o1], %o0
	SET_SIZE(ddi_getl)

	ENTRY(ddi_getll)
	ALTENTRY(ddi_mem_getll)
	ALTENTRY(ddi_io_getll)
	retl
	ldd	[%o1], %o0
	SET_SIZE(ddi_getll)

	ENTRY(ddi_putb)
	ALTENTRY(ddi_mem_putb)
	ALTENTRY(ddi_io_putb)
	retl
	stub	%o2, [%o1]
	SET_SIZE(ddi_putb)

	ENTRY(ddi_putw)
	ALTENTRY(ddi_mem_putw)
	ALTENTRY(ddi_io_putw)
	retl
	stuh	%o2, [%o1]
	SET_SIZE(ddi_putw)

	ENTRY(ddi_putl)
	ALTENTRY(ddi_mem_putl)
	ALTENTRY(ddi_io_putl)
	retl
	st	%o2, [%o1]
	SET_SIZE(ddi_putl)

	ENTRY(ddi_putll)
	ALTENTRY(ddi_mem_putll)
	ALTENTRY(ddi_io_putll)
	retl
	std	%o2, [%o1]
	SET_SIZE(ddi_putll)

#define DDI_REP_GET(n,s)		\
	mov %o1, %g1;			\
	mov %o2, %g2;			\
	cmp %o4, 1;			\
	be 2f;				\
	mov %o3, %g3;			\
1:	tst	%g3;			\
	be	3f;			\
	nop;				\
	ld/**/s	[%g2], %g4;		\
	st/**/s	%g4, [%g1];		\
	add	%g1, n, %g1;		\
	ba	1b;			\
	dec	%g3;			\
2:	tst	%g3;			\
	be	3f;			\
	nop;				\
	ld/**/s	[%g2], %g4;		\
	st/**/s	%g4, [%g1];		\
	add	%g1, n, %g1;		\
	add	%g2, n, %g2;		\
	ba	2b;			\
	dec	%g3;			\
3:	retl;				\
	nop

	ENTRY(ddi_rep_getb)
	ALTENTRY(ddi_mem_rep_getb)
	DDI_REP_GET(1,ub)
	SET_SIZE(ddi_rep_getb)

	ENTRY(ddi_rep_getw)
	ALTENTRY(ddi_mem_rep_getw)
	DDI_REP_GET(2,uh)
	SET_SIZE(ddi_rep_getw)

	ENTRY(ddi_rep_getl)
	ALTENTRY(ddi_mem_rep_getl)
	DDI_REP_GET(4,/**/)
	SET_SIZE(ddi_rep_getl)

	ENTRY(ddi_rep_getll)
	ALTENTRY(ddi_mem_rep_getll)
	DDI_REP_GET(8,x)
	SET_SIZE(ddi_rep_getll)

#define DDI_REP_PUT(n,s)		\
	mov %o1, %g1;			\
	mov %o2, %g2;			\
	cmp %o4, 1;			\
	be 2f;				\
	mov %o3, %g3;			\
1:	tst	%g3;			\
	be	3f;			\
	nop;				\
	ld/**/s	[%g1], %g4;		\
	st/**/s	%g4, [%g2];		\
	add	%g1, n, %g1;		\
	ba	1b;			\
	dec	%g3;			\
2:	tst	%g3;			\
	be	3f;			\
	nop;				\
	ld/**/s	[%g1], %g4;		\
	st/**/s	%g4, [%g2];		\
	add	%g1, n, %g1;		\
	add	%g2, n, %g2;		\
	ba	2b;			\
	dec	%g3;			\
3:	retl;				\
	nop

	ENTRY(ddi_rep_putb)
	ALTENTRY(ddi_mem_rep_putb)
	DDI_REP_PUT(1,ub)
	SET_SIZE(ddi_rep_putb)

	ENTRY(ddi_rep_putw)
	ALTENTRY(ddi_mem_rep_putw)
	DDI_REP_PUT(2,uh)
	SET_SIZE(ddi_rep_putw)

	ENTRY(ddi_rep_putl)
	ALTENTRY(ddi_mem_rep_putl)
	DDI_REP_PUT(4,/**/)
	SET_SIZE(ddi_rep_putl)

	ENTRY(ddi_rep_putll)
	ALTENTRY(ddi_mem_rep_putll)
	DDI_REP_PUT(8,x)
	SET_SIZE(ddi_rep_putll)

#define DDI_IO_REP_GET(n,s)		\
	mov %o1, %g1;			\
	mov %o2, %g2;			\
	mov %o3, %g3;			\
1:	tst	%g3;			\
	be	2f;			\
	nop;				\
	ld/**/s	[%g2], %g4;		\
	st/**/s	%g4, [%g1];		\
	add	%g1, n, %g1;		\
	ba	1b;			\
	dec	%g3;			\
2:	retl;				\
	nop

	ENTRY(ddi_io_rep_getb)
	DDI_IO_REP_GET(1,ub)
	SET_SIZE(ddi_io_rep_getb)

	ENTRY(ddi_io_rep_getw)
	DDI_IO_REP_GET(2,uh)
	SET_SIZE(ddi_io_rep_getw)

	ENTRY(ddi_io_rep_getl)
	DDI_IO_REP_GET(4,/**/)
	SET_SIZE(ddi_io_rep_getl)

#define DDI_IO_REP_PUT(n,s)		\
	mov %o1, %g1;			\
	mov %o2, %g2;			\
	mov %o3, %g3;			\
1:	tst	%g3;			\
	be	2f;			\
	nop;				\
	ld/**/s	[%g1], %g4;		\
	st/**/s	%g4, [%g2];		\
	add	%g1, n, %g1;		\
	ba	1b;			\
	dec	%g3;			\
2:	retl;				\
	nop

	ENTRY(ddi_io_rep_putb)
	DDI_IO_REP_PUT(1,ub)
	SET_SIZE(ddi_io_rep_putb)

	ENTRY(ddi_io_rep_putw)
	DDI_IO_REP_PUT(2,uh)
	SET_SIZE(ddi_io_rep_putw)

	ENTRY(ddi_io_rep_putl)
	DDI_IO_REP_PUT(4,/**/)
	SET_SIZE(ddi_io_rep_putl)
#endif
