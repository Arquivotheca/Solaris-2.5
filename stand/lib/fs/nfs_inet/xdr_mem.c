#ident	"@(#)xdr_mem.c	1.4	92/01/16 SMI" /* from SunOS 4.1 1.23 90/03/30 */

/*
 * xdr_mem.h, XDR implementation using memory buffers.
 *
 * Modified for the use of the boot program.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * If you have some data to be interpreted as external data representation
 * or to be converted to external data representation in a memory buffer,
 * then this is the package for you.
 *
 */

#include <sys/param.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <in.h>

static struct xdr_ops *xdrmem_ops();

/*
 * The procedure xdrmem_create initializes a stream descriptor for a
 * memory buffer.
 */
void
xdrmem_create(XDR *xdrs, caddr_t addr, u_int size, enum xdr_op op)
{
	xdrs->x_op = op;
	xdrs->x_ops = xdrmem_ops();
	xdrs->x_private = xdrs->x_base = addr;
	xdrs->x_handy = size;
}

/* ARGSUSED */
static void
xdrmem_destroy(XDR *xdrs)
{
}

static bool_t
xdrmem_getlong(XDR *xdrs, long *lp)
{
	if ((xdrs->x_handy -= sizeof (long)) < 0)
		return (FALSE);
	*lp = (long)ntohl((u_long)(*((long *)(xdrs->x_private))));
	xdrs->x_private += sizeof (long);
	return (TRUE);
}

static bool_t
xdrmem_putlong(XDR *xdrs, long *lp)
{
	if ((xdrs->x_handy -= sizeof (long)) < 0)
		return (FALSE);
	*(long *)xdrs->x_private = (long)htonl((u_long)(*lp));
	xdrs->x_private += sizeof (long);
	return (TRUE);
}

static bool_t
xdrmem_getbytes(XDR *xdrs, caddr_t addr, int len)
{
	if ((xdrs->x_handy -= len) < 0)
		return (FALSE);
	bcopy(xdrs->x_private, addr, len);
	xdrs->x_private += len;
	return (TRUE);
}

static bool_t
xdrmem_putbytes(XDR *xdrs, caddr_t addr, int len)
{
	if ((xdrs->x_handy -= len) < 0)
		return (FALSE);
	bcopy(addr, xdrs->x_private, len);
	xdrs->x_private += len;
	return (TRUE);
}

static u_int
xdrmem_getpos(XDR *xdrs)
{
	return ((u_int)xdrs->x_private - (u_int)xdrs->x_base);
}

static bool_t
xdrmem_setpos(XDR *xdrs, u_int pos)
{
	register caddr_t newaddr = xdrs->x_base + pos;
	register caddr_t lastaddr = xdrs->x_private + xdrs->x_handy;

	if ((long)newaddr > (long)lastaddr)
		return (FALSE);
	xdrs->x_private = newaddr;
	xdrs->x_handy = (int)lastaddr - (int)newaddr;
	return (TRUE);
}

static long *
xdrmem_inline(XDR *xdrs, int len)
{
	long *buf = 0;

	if (xdrs->x_handy >= len) {
		xdrs->x_handy -= len;
		buf = (long *) xdrs->x_private;
		xdrs->x_private += len;
	}
	return (buf);
}

static struct xdr_ops *
xdrmem_ops(void)
{
	static struct xdr_ops ops;

	if (ops.x_getlong == NULL) {
		ops.x_getlong = xdrmem_getlong;
		ops.x_putlong = xdrmem_putlong;
		ops.x_getbytes = xdrmem_getbytes;
		ops.x_putbytes = xdrmem_putbytes;
		ops.x_getpostn = xdrmem_getpos;
		ops.x_setpostn = xdrmem_setpos;
		ops.x_inline = xdrmem_inline;
		ops.x_destroy = xdrmem_destroy;
	}
	return (&ops);
}
