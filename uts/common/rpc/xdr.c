/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)xdr.c	1.20	95/03/28 SMI"	/* SVr4.0 1.4	*/

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
 *  	Copyright 1986-1989, 1994  Sun Microsystems, Inc.
 *  	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		  All rights reserved.
 */

/*
 * xdr.c, generic XDR routines implementation.
 * These are the "generic" xdr routines used to serialize and de-serialize
 * most common data items.  See xdr.h for more info on the interface to
 * xdr.
 */

#include <sys/param.h>
#include <sys/cmn_err.h>
#include <sys/types.h>
#include <sys/systm.h>

#include <rpc/types.h>
#include <rpc/xdr.h>

#if defined(sparc)
#define	BIG_ENDIAN
#endif
#if defined(__alpha)
#define	LITTLE_ENDIAN
#endif
#if defined(vax)
#define	LITTLE_ENDIAN
#endif
#if defined(i386)
#define	LITTLE_ENDIAN
#endif
#if !defined(BIG_ENDIAN) && !defined(LITTLE_ENDIAN)
Porting problem!  Pick one of BIG_ENDIAN or LITTLE_ENDIAN depending
upon the byte order of the target system.
#endif
#if defined(BIG_ENDIAN) && defined(LITTLE_ENDIAN)
Porting problem!  Pick only one of BIG_ENDIAN or LITTLE_ENDIAN depending
upon the byte order of the target system.
#endif

/*
 * constants specific to the xdr "protocol"
 */
#define	XDR_FALSE	((long) 0)
#define	XDR_TRUE	((long) 1)
#define	LASTUNSIGNED	((u_int) 0-1)

/*
 * for unit alignment
 */
static char xdr_zero[BYTES_PER_XDR_UNIT] = { 0, 0, 0, 0 };

/*
 * Free a data structure using XDR
 * Not a filter, but a convenient utility nonetheless
 */
void
xdr_free(proc, objp)
	xdrproc_t proc;
	char *objp;
{
	XDR x;

	x.x_op = XDR_FREE;
	(*proc)(&x, objp);
}

/*
 * XDR nothing
 */
bool_t
xdr_void(/* xdrs, addr */)
	/* XDR *xdrs; */
	/* caddr_t addr; */
{

	return (TRUE);
}

/*
 * XDR integers
 */
bool_t
xdr_int(XDR *xdrs, int *ip)
{

#ifdef lint
	(void) (xdr_short(xdrs, (short *)ip));
	return (xdr_long(xdrs, (long *)ip));
#else
	if (sizeof (int) == sizeof (long)) {
		return (xdr_long(xdrs, (long *)ip));
	} else {
		return (xdr_short(xdrs, (short *)ip));
	}
#endif
}

/*
 * XDR unsigned integers
 */
bool_t
xdr_u_int(XDR *xdrs, u_int *up)
{

#ifdef lint
	(void) (xdr_short(xdrs, (short *)up));
	return (xdr_u_long(xdrs, (u_long *)up));
#else
	if (sizeof (u_int) == sizeof (u_long)) {
		return (xdr_u_long(xdrs, (u_long *)up));
	} else {
		return (xdr_short(xdrs, (short *)up));
	}
#endif
}

/*
 * XDR long long integers
 */
bool_t
xdr_longlong_t(XDR *xdrs, longlong_t *hp)
{

	if (xdrs->x_op == XDR_ENCODE) {
#if defined(LITTLE_ENDIAN)
		if (XDR_PUTLONG(xdrs, (long *)((char *) hp +
			BYTES_PER_XDR_UNIT)) == TRUE) {
			return (XDR_PUTLONG(xdrs, (long *) hp));
		}
#endif
#if defined(BIG_ENDIAN)
		if (XDR_PUTLONG(xdrs, (long *) hp) == TRUE) {
			return (XDR_PUTLONG(xdrs, (long *)((char *) hp +
				BYTES_PER_XDR_UNIT)));
		}
#endif
		return (FALSE);

	}
	if (xdrs->x_op == XDR_DECODE) {
#if defined(LITTLE_ENDIAN)
		if (XDR_GETLONG(xdrs, (long *) ((char *) hp +
			BYTES_PER_XDR_UNIT)) == TRUE) {
			return (XDR_GETLONG(xdrs, (long *) hp));
		}
#endif
#if defined(BIG_ENDIAN)
		if (XDR_GETLONG(xdrs, (long *)hp) == TRUE) {
			return (XDR_GETLONG(xdrs, (long *)((char *) hp +
				BYTES_PER_XDR_UNIT)));
		}
#endif
		return (FALSE);
	}
	return (TRUE);
}

/*
 * XDR unsigned long long integers
 */
bool_t
xdr_u_longlong_t(XDR *xdrs, u_longlong_t *hp)
{

	if (xdrs->x_op == XDR_ENCODE) {
#if defined(LITTLE_ENDIAN)
		if (XDR_PUTLONG(xdrs, (long *)((char *) hp +
			BYTES_PER_XDR_UNIT)) == TRUE) {
			return (XDR_PUTLONG(xdrs, (long *) hp));
		}
#else
		if (XDR_PUTLONG(xdrs, (long *) hp) == TRUE) {
			return (XDR_PUTLONG(xdrs, (long *)((char *) hp +
				BYTES_PER_XDR_UNIT)));
		}
#endif
		return (FALSE);

	}
	if (xdrs->x_op == XDR_DECODE) {
#if defined(LITTLE_ENDIAN)
		if (XDR_GETLONG(xdrs, (long *) ((char *) hp +
			BYTES_PER_XDR_UNIT)) == TRUE) {
			return (XDR_GETLONG(xdrs, (long *) hp));
		}
#else
		if (XDR_GETLONG(xdrs, (long *)hp) == TRUE) {
			return (XDR_GETLONG(xdrs, (long *)((char *) hp +
				BYTES_PER_XDR_UNIT)));
		}
#endif
		return (FALSE);
	}
	return (TRUE);
}

/*
 * XDR long integers
 * same as xdr_u_long - open coded to save a proc call!
 */
bool_t
xdr_long(XDR *xdrs, long *lp)
{

	if (xdrs->x_op == XDR_ENCODE)
		return (XDR_PUTLONG(xdrs, lp));

	if (xdrs->x_op == XDR_DECODE)
		return (XDR_GETLONG(xdrs, lp));

	if (xdrs->x_op == XDR_FREE)
		return (TRUE);

#ifdef DEBUG
	printf("xdr_long: FAILED\n");
#endif
	return (FALSE);
}

/*
 * XDR unsigned long integers
 * same as xdr_long - open coded to save a proc call!
 */
bool_t
xdr_u_long(XDR *xdrs, u_long *ulp)
{

	if (xdrs->x_op == XDR_DECODE)
		return (XDR_GETLONG(xdrs, (long *)ulp));
	if (xdrs->x_op == XDR_ENCODE)
		return (XDR_PUTLONG(xdrs, (long *)ulp));
	if (xdrs->x_op == XDR_FREE)
		return (TRUE);
#ifdef DEBUG
	printf("xdr_u_long: FAILED\n");
#endif
	return (FALSE);
}

/*
 * XDR short integers
 */
bool_t
xdr_short(XDR *xdrs, short *sp)
{
	long l;

	switch (xdrs->x_op) {

	case XDR_ENCODE:
		l = (long) *sp;
		return (XDR_PUTLONG(xdrs, &l));

	case XDR_DECODE:
		if (!XDR_GETLONG(xdrs, &l)) {
			return (FALSE);
		}
		*sp = (short) l;
		return (TRUE);

	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR unsigned short integers
 */
bool_t
xdr_u_short(XDR *xdrs, u_short *usp)
{
	u_long l;

	switch (xdrs->x_op) {

	case XDR_ENCODE:
		l = (u_long) *usp;
		return (XDR_PUTLONG(xdrs, (long *)&l));

	case XDR_DECODE:
		if (!XDR_GETLONG(xdrs, (long *)&l)) {
#ifdef DEBUG
			printf("xdr_u_short: decode FAILED\n");
#endif
			return (FALSE);
		}
		*usp = (u_short) l;
		return (TRUE);

	case XDR_FREE:
		return (TRUE);
	}
#ifdef DEBUG
	printf("xdr_u_short: bad op FAILED\n");
#endif
	return (FALSE);
}


/*
 * XDR a char
 */
bool_t
xdr_char(XDR *xdrs, char *cp)
{
	int i;

	i = (*cp);
	if (!xdr_int(xdrs, &i)) {
		return (FALSE);
	}
	*cp = (char)i;
	return (TRUE);
}

/*
 * XDR booleans
 */
bool_t
xdr_bool(XDR *xdrs, bool_t *bp)
{
	long lb;

	switch (xdrs->x_op) {

	case XDR_ENCODE:
		lb = *bp ? XDR_TRUE : XDR_FALSE;
		return (XDR_PUTLONG(xdrs, &lb));

	case XDR_DECODE:
		if (!XDR_GETLONG(xdrs, &lb)) {
#ifdef DEBUG
			printf("xdr_bool: decode FAILED\n");
#endif
			return (FALSE);
		}
		*bp = (lb == XDR_FALSE) ? FALSE : TRUE;
		return (TRUE);

	case XDR_FREE:
		return (TRUE);
	}
#ifdef DEBUG
	printf("xdr_bool: bad op FAILED\n");
#endif
	return (FALSE);
}

/*
 * XDR enumerations
 */
#ifndef lint
enum sizecheck { SIZEVAL } sizecheckvar;	/* used to find the size of */
						/* an enum */
#endif
bool_t
xdr_enum(XDR *xdrs, enum_t *ep)
{

#ifndef lint
	/*
	 * enums are treated as ints
	 */
	if (sizeof (sizecheckvar) == sizeof (long)) {
		return (xdr_long(xdrs, (long *)ep));
	} else if (sizeof (sizecheckvar) == sizeof (short)) {
		return (xdr_short(xdrs, (short *)ep));
	} else {
		return (FALSE);
	}
#else
	(void) (xdr_short(xdrs, (short *)ep));
	return (xdr_long(xdrs, (long *)ep));
#endif
}

/*
 * XDR opaque data
 * Allows the specification of a fixed size sequence of opaque bytes.
 * cp points to the opaque object and cnt gives the byte length.
 */
bool_t
xdr_opaque(XDR *xdrs, caddr_t cp, const u_int cnt)
{
	register u_int rndup;
	static crud[BYTES_PER_XDR_UNIT];

	/*
	 * if no data we are done
	 */
	if (cnt == 0)
		return (TRUE);

	/*
	 * round byte count to full xdr units
	 */
	rndup = cnt % BYTES_PER_XDR_UNIT;
	if (rndup != 0)
		rndup = BYTES_PER_XDR_UNIT - rndup;

	if (xdrs->x_op == XDR_DECODE) {
		if (!XDR_GETBYTES(xdrs, cp, cnt)) {
#ifdef DEBUG
			printf("xdr_opaque: decode FAILED\n");
#endif
			return (FALSE);
		}
		if (rndup == 0)
			return (TRUE);
		return (XDR_GETBYTES(xdrs, (caddr_t) crud, rndup));
	}

	if (xdrs->x_op == XDR_ENCODE) {
		if (!XDR_PUTBYTES(xdrs, cp, cnt)) {
#ifdef DEBUG
			printf("xdr_opaque: encode FAILED\n");
#endif
			return (FALSE);
		}
		if (rndup == 0)
			return (TRUE);
		return (XDR_PUTBYTES(xdrs, xdr_zero, rndup));
	}

	if (xdrs->x_op == XDR_FREE) {
		return (TRUE);
	}

#ifdef DEBUG
	printf("xdr_opaque: bad op FAILED\n");
#endif
	return (FALSE);
}

/*
 * XDR counted bytes
 * *cpp is a pointer to the bytes, *sizep is the count.
 * If *cpp is NULL maxsize bytes are allocated
 */
bool_t
xdr_bytes(XDR *xdrs, char **cpp, u_int *sizep, const u_int maxsize)
{
	register char *sp = *cpp;  /* sp is the actual string pointer */
	register u_int nodesize;

	/*
	 * first deal with the length since xdr bytes are counted
	 */
	if (! xdr_u_int(xdrs, sizep)) {
#ifdef DEBUG
		printf("xdr_bytes: size FAILED\n");
#endif
		return (FALSE);
	}
	nodesize = *sizep;
	if ((nodesize > maxsize) && (xdrs->x_op != XDR_FREE)) {
#ifdef DEBUG
		printf("xdr_bytes: bad size (%d) FAILED (%d max)\n",
		    nodesize, maxsize);
#endif
		return (FALSE);
	}

	/*
	 * now deal with the actual bytes
	 */
	switch (xdrs->x_op) {

	case XDR_DECODE:
		if (nodesize == 0) {
			return (TRUE);
		}
		if (sp == NULL) {
			*cpp = sp = (char *)mem_alloc(nodesize);
		}
	/* FALLTHROUGH */
	case XDR_ENCODE:
		return (xdr_opaque(xdrs, sp, nodesize));

	case XDR_FREE:
		if (sp != NULL) {
			mem_free(sp, nodesize);
			*cpp = NULL;
		}
		return (TRUE);
	}
#ifdef DEBUG
	printf("xdr_bytes: bad op FAILED\n");
#endif
	return (FALSE);
}

/*
 * Implemented here due to commonality of the object.
 */
bool_t
xdr_netobj(XDR *xdrs, struct netobj *np)
{

	return (xdr_bytes(xdrs, &np->n_bytes, &np->n_len, MAX_NETOBJ_SZ));
}

/*
 * XDR a descriminated union
 * Support routine for discriminated unions.
 * You create an array of xdrdiscrim structures, terminated with
 * an entry with a null procedure pointer.  The routine gets
 * the discriminant value and then searches the array of xdrdiscrims
 * looking for that value.  It calls the procedure given in the xdrdiscrim
 * to handle the discriminant.  If there is no specific routine a default
 * routine may be called.
 * If there is no specific or default routine an error is returned.
 */
bool_t
xdr_union(XDR *xdrs, enum_t *dscmp, char *unp,
	const struct xdr_discrim *choices, const xdrproc_t dfault)
{
	register enum_t dscm;

	/*
	 * we deal with the discriminator;  it's an enum
	 */
	if (! xdr_enum(xdrs, dscmp)) {
#ifdef DEBUG
		printf("xdr_enum: dscmp FAILED\n");
#endif
		return (FALSE);
	}
	dscm = *dscmp;

	/*
	 * search choices for a value that matches the discriminator.
	 * if we find one, execute the xdr routine for that value.
	 */
	for (; choices->proc != NULL_xdrproc_t; choices++) {
		if (choices->value == dscm)
			return ((*(choices->proc))(xdrs, unp, LASTUNSIGNED));
	}

	/*
	 * no match - execute the default xdr routine if there is one
	 */
	return ((dfault == NULL_xdrproc_t) ? FALSE :
	    (*dfault)(xdrs, unp, LASTUNSIGNED));
}


/*
 * Non-portable xdr primitives.
 * Care should be taken when moving these routines to new architectures.
 */


/*
 * XDR null terminated ASCII strings
 * xdr_string deals with "C strings" - arrays of bytes that are
 * terminated by a NULL character.  The parameter cpp references a
 * pointer to storage; If the pointer is null, then the necessary
 * storage is allocated.  The last parameter is the max allowed length
 * of the string as specified by a protocol.
 */
bool_t
xdr_string(XDR *xdrs, char **cpp, const u_int maxsize)
{
	register char *sp = *cpp;  /* sp is the actual string pointer */
	u_int size;
	u_int nodesize;

	/*
	 * first deal with the length since xdr strings are counted-strings
	 */
	switch (xdrs->x_op) {
	case XDR_FREE:
		if (sp == NULL) {
			return (TRUE);	/* already free */
		}
	/* FALLTHROUGH */
	case XDR_ENCODE:
		size = strlen(sp);
		break;
	}
	if (! xdr_u_int(xdrs, &size)) {
#ifdef DEBUG
		printf("xdr_string: size FAILED\n");
#endif
		return (FALSE);
	}
	if (size > maxsize) {
#ifdef DEBUG
		printf("xdr_string: bad size FAILED\n");
#endif
		return (FALSE);
	}
	nodesize = size + 1;

	/*
	 * now deal with the actual bytes
	 */
	switch (xdrs->x_op) {

	case XDR_DECODE:
		if (nodesize == 0)
			return (TRUE);
		if (sp == NULL)
			sp = (char *)mem_alloc(nodesize);
		sp[size] = 0;
		if (! xdr_opaque(xdrs, sp, size)) {
			mem_free(sp, nodesize);
			return (FALSE);
		}
		if (strlen(sp) != size) {
			mem_free(sp, nodesize);
			return (FALSE);
		}
		*cpp = sp;
		return (TRUE);

	case XDR_ENCODE:
		return (xdr_opaque(xdrs, sp, size));

	case XDR_FREE:
		mem_free(sp, nodesize);
		*cpp = NULL;
		return (TRUE);
	}
#ifdef DEBUG
	printf("xdr_string: bad op FAILED\n");
#endif
	return (FALSE);
}

/*
 * Wrapper for xdr_string that can be called directly from
 * routines like clnt_call
 */
bool_t
xdr_wrapstring(XDR *xdrs, char **cpp)
{

	if (xdr_string(xdrs, cpp, LASTUNSIGNED))
		return (TRUE);
	return (FALSE);
}
