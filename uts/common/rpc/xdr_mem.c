/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)xdr_mem.c	1.14	95/01/26 SMI"	/* SVr4.0 1.3	*/

/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *  		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc.
 *  	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		  All rights reserved.
 */

/*
 * xdr_mem.h, XDR implementation using memory buffers.
 *
 * If you have some data to be interpreted as external data representation
 * or to be converted to external data representation in a memory buffer,
 * then this is the package for you.
 *
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>

#include <rpc/types.h>
#include <rpc/xdr.h>

static struct xdr_ops *xdrmem_ops(void);

/*
 * The procedure xdrmem_create initializes a stream descriptor for a
 * memory buffer.
 */
void
xdrmem_create(register XDR *xdrs, caddr_t addr, u_int size, enum xdr_op op)
{

	xdrs->x_op = op;
	xdrs->x_ops = xdrmem_ops();
	xdrs->x_private = xdrs->x_base = addr;
	xdrs->x_handy = size;
	xdrs->x_public = NULL;
}

/* ARGSUSED */
static void
xdrmem_destroy(XDR *xdrs)
{
}

static bool_t
xdrmem_getlong(register XDR *xdrs, long *lp)
{

	if ((xdrs->x_handy -= sizeof (long)) < 0)
		return (FALSE);
	/* LINTED pointer alignment */
	*lp = (long)ntohl((u_long)(*((long *)(xdrs->x_private))));
	xdrs->x_private += sizeof (long);
	return (TRUE);
}

static bool_t
xdrmem_putlong(register XDR *xdrs, long *lp)
{

	if ((xdrs->x_handy -= sizeof (long)) < 0)
		return (FALSE);
	/* LINTED pointer alignment */
	*(long *)xdrs->x_private = (long)htonl((u_long)(*lp));
	xdrs->x_private += sizeof (long);
	return (TRUE);
}

static bool_t
xdrmem_getbytes(register XDR *xdrs, caddr_t addr, register int len)
{

	if ((xdrs->x_handy -= len) < 0)
		return (FALSE);
	bcopy(xdrs->x_private, addr, len);
	xdrs->x_private += len;
	return (TRUE);
}

static bool_t
xdrmem_putbytes(register XDR *xdrs, caddr_t addr, register int len)
{

	if ((xdrs->x_handy -= len) < 0)
		return (FALSE);
	bcopy(addr, xdrs->x_private, len);
	xdrs->x_private += len;
	return (TRUE);
}

static u_int
xdrmem_getpos(register XDR *xdrs)
{

	return ((u_int)xdrs->x_private - (u_int)xdrs->x_base);
}

static bool_t
xdrmem_setpos(register XDR *xdrs, u_int pos)
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
xdrmem_inline(register XDR *xdrs, int len)
{
	long *buf = NULL;

	if (xdrs->x_handy >= len) {
		xdrs->x_handy -= len;
		/* LINTED pointer alignment */
		buf = (long *) xdrs->x_private;
		xdrs->x_private += len;
	}
	return (buf);
}

static bool_t
xdrmem_control(register XDR *xdrs, int request, void *info)
{
	long *lp;
	int len;

	switch (request) {
	case XDR_PEEK:
		/*
		 * Return the next 4 byte long in the XDR stream.
		 */
		if (xdrs->x_handy < sizeof (long)) {
			return (FALSE);
		}
		lp = (long *)info;
		*lp = (long)ntohl((u_long)(*((long *)(xdrs->x_private))));
		return (TRUE);

	case XDR_SKIPBYTES:
		/*
		 * Skip the next N bytes in the XDR stream.
		 */
		lp = (long *)info;
		len = RNDUP((int)(*lp));
		if ((xdrs->x_handy -= len) < 0)
			return (FALSE);
		xdrs->x_private += len;
		return (TRUE);

	default:
		return (FALSE);
	}
}

static struct xdr_ops *
xdrmem_ops(void)
{
	static struct xdr_ops ops;

	if (ops.x_getlong == NULL) {
		ops.x_putlong = xdrmem_putlong;
		ops.x_getbytes = xdrmem_getbytes;
		ops.x_putbytes = xdrmem_putbytes;
		ops.x_getpostn = xdrmem_getpos;
		ops.x_setpostn = xdrmem_setpos;
		ops.x_inline = xdrmem_inline;
		ops.x_destroy = xdrmem_destroy;
		ops.x_getlong = xdrmem_getlong;
		ops.x_control = xdrmem_control;
	}
	return (&ops);
}
