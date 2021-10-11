/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)rpc_subr.c	1.14	95/02/06 SMI"	/* SVr4.0 1.1	*/

/*
 * Miscellaneous support routines for kernel implementation of RPC.
 *
 */

#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strsubr.h>
#include <sys/socket.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <sys/tiuser.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>

#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

static int		strtoi(char *, char **);

#ifdef RPCDEBUG

/*
 * Kernel level debugging aid. The global variable "rpclog" is a bit
 * mask which allows various types of debugging messages to be printed
 * out.
 *
 *	rpclog & 1 	will cause actual failures to be printed.
 *	rpclog & 2	will cause informational messages to be
 *			printed on the client side of rpc.
 *	rpclog & 4	will cause informational messages to be
 *			printed on the server side of rpc.
 */

int rpclog = 0;

int
rpc_log(u_long level, register char *str, register int a1)
{

	if (level & rpclog)
		printf(str, a1);
	return (0);
}
#endif /* RPCDEBUG */

/*
 *  Revoke the authentication key in the given AUTH handle by setting
 *  it to NULL.  If newkey is true, then generate a new key instead of
 *  nulling out the old one.  This is necessary for AUTH_DES because
 *  the new key will be used next time the user does a keylogin.  If
 *  the zero'd key is used as actual key, then it cannot be revoked
 *  again!  This is not a problem for AUTH_KERB because the key is
 *  embedded in the ticket, which is cached.
 */

void
rpc_revoke_key(register AUTH *auth, int newkey)
{

	if (auth) {
		if (newkey) {
			if (key_gendes(&auth->ah_key) != RPC_SUCCESS) {
				/* failed to get new key, munge the old one */
				auth->ah_key.key.high ^= auth->ah_key.key.low;
				auth->ah_key.key.low  += auth->ah_key.key.high;
			}
		} else {
			/* null out old key */
			auth->ah_key.key.high = 0;
			auth->ah_key.key.low  = 0;
		}
	}
}

void
rpc_poptimod(vnode_t *vp)
{
	int error, isfound, ret;

	error = strioctl(vp, I_FIND, (int) "timod", 0, K_TO_K, CRED(),
			&isfound);
	if (error) {
		RPCLOG(1, "rpc_poptimod: I_FIND strioctl error %d\n", error);
		return;
	}
	if (isfound) {
		error = strioctl(vp, I_POP, (int) "timod", 0, K_TO_K, CRED(),
				&ret);
		if (error) {
			RPCLOG(1, "rpc_poptimod: I_POP strioctl error %d\n",
				error);
			return;
		}
	}
}

/*
 * Return a port number from a sockaddr_in expressed in universal address
 * format.  Note that this routine does not work for address families other
 * than INET.  Eventually, we should replace this routine with one that
 * contacts the rpcbind running locally.
 */
int
rpc_uaddr2port(char *addr)
{
	int p1;
	int p2;
	char *next;

	/*
	 * A struct sockaddr_in expressed in universal address
	 * format looks like:
	 *
	 *	"IP.IP.IP.IP.PORT[top byte].PORT[bottom byte]"
	 *
	 * Where each component expresses as a character,
	 * the corresponding part of the IP address
	 * and port number.
	 * Thus 127.0.0.1, port 2345 looks like:
	 *
	 *	49 50 55 46 48 46 48 46 49 46 57 46 52 49
	 *	1  2  7  .  0  .  0  .  1  .  9  .  4  1
	 *
	 * 2345 = 929base16 = 9.32+9 = 9.41
	 */
	(void) strtoi(addr, &next);
	(void) strtoi(next, &next);
	(void) strtoi(next, &next);
	(void) strtoi(next, &next);
	p1 = strtoi(next, &next);
	p2 = strtoi(next, &next);

	return ((p1 << 8) + p2);
}

/*
 * Modified strtol(3).  Should we be using mi_strtol() instead?
 */
static int
strtoi(char *str, char **ptr)
{
	int c;
	int val;

	for (val = 0, c = *str++; c >= '0' && c <= '9'; c = *str++) {
		val *= 10;
		val += c - '0';
	}
	*ptr = str;
	return (val);
}
