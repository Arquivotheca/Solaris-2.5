/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)unix.c	1.5	93/05/25 SMI"	/* SVr4.0 1.2 */

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 * 		All rights reserved.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stream.h>
#include <sys/tiuser.h>
#include <sys/socketvar.h>
#define	_KERNEL
#include <sys/sockmod.h>
#undef _KERNEL
#include <sys/un.h>

extern void	readmem(long, int, int, char *, unsigned, char*);
static int	first = 1;
static char	*typetoname();

/*
 * Print a summary of connections related to a unix protocol.
 */
void
unixpr(off)
	off_t	off;
{
	struct so_so		*prev;
	struct so_so		*so_ux_list;
	struct so_so		so;
	struct so_so		*oso;
	struct sockaddr_un	*sa;

	if (off == 0) {
		return;
	}
	readmem(off, 1, 0, (char *)&so_ux_list, sizeof (struct so_so *),
			"so_ux_list");
	if (so_ux_list == (struct so_so *)NULL)
		return;

	/*
	 * Dummy up the first one.
	 */
	so.so_ux.next = so_ux_list;
	prev = (struct so_so *)NULL;
	while (so.so_ux.next != (struct so_so *)NULL) {
		oso = so.so_ux.next;
		readmem((long)so.so_ux.next, 1, 0, (char *)&so,
				sizeof (struct so_so), "so_ux_list");
		if (so.so_ux.prev != prev) {
			(void) printf("Corrupt control block chain\n");
			break;
		}

		if (first) {
			(void) printf("Active UNIX domain sockets\n");
			(void) printf("%-8.8s %-10.10s %8.8s %8.8s Addr\n",
				"Address", "Type", "Vnode", "Conn");
			first = 0;
		}
		(void) printf("%8lx ", (long)oso);
		(void) printf("%-10.10s ", typetoname(so.udata.servtype));
		(void) printf("%8lu ", so.lux_dev.addr.tu_addr.ino);
		(void) printf("%8lx ", (long)so.so_conn);
		if (so.laddr.len) {

			/*
			 * Read in the address (it's a netbuf).
			 */
			if ((sa =
		(struct sockaddr_un *)calloc(1, so.laddr.len)) == NULL) {
				(void) printf("\n");
				continue;
			}
			readmem((long)so.laddr.buf, 1, 0, (char *)sa,
					sizeof (*sa), "so.laddr");
			(void) printf("%.*s\n",
				so.laddr.len - sizeof (sa->sun_family),
				sa->sun_path);
			free(sa);
			sa = NULL;
		} else (void) printf("\n");

		prev = oso;
	}
}

static char *
typetoname(type)
{
	switch (type) {
	case T_CLTS:
		return ("dgram");

	case T_COTS:
		return ("stream");

	case T_COTS_ORD:
		return ("stream-ord");

	default:
		return ("");
	}
}
