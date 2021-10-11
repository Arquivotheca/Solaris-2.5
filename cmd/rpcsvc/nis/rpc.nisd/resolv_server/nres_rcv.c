/* Copyright (c) 1993 Sun Microsystems Inc */

/* Taken from 4.1.3 ypserv resolver code. */

/*
 * Send query to name server and wait for reply.
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <syslog.h>
#include <stdio.h>
#include <errno.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include "nres.h"
#include "prnt.h"

#ifdef TDRPC
extern char* strerror();
#endif


#ifndef FD_SET
#define	NFDBITS		32
#define	FD_SETSIZE	32
#define	FD_SET(n, p)	((p)->fds_bits[(n)/NFDBITS] |= (1 << ((n) % NFDBITS)))
#define	FD_CLR(n, p)	((p)->fds_bits[(n)/NFDBITS] &= ~(1 << ((n) % NFDBITS)))
#define	FD_ISSET(n, p)	((p)->fds_bits[(n)/NFDBITS] & (1 << ((n) % NFDBITS)))
#define	FD_ZERO(p)	bzero((char *)(p), sizeof (*(p)))
#endif

#define	KEEPOPEN (RES_USEVC|RES_STAYOPEN)


nres_rcv(tnr)
	struct nres    *tnr;
{
	register int    n;
	int		resplen;
	u_short		len;
	char		*cp;
	HEADER		*hp = (HEADER *) tnr->question;
	HEADER		*anhp = (HEADER *) tnr->answer;
	int		s;
	int		truncated = 0;
	char		junk[512];
	if (tnr->using_tcp == 0) {
		s = tnr->udp_socket;

		if ((resplen = recv(s, tnr->answer, MAXPACKET, 0)) <= 0) {
			prnt(P_ERR, "recv failed: %s.\n", strerror(errno));
			return (-1);
		}
		if (hp->id != anhp->id) {
			/*
			 * response from old query, ignore it
			 */
			prnt(P_INFO, "old answer.\n");
			if (verbose && verbose_out) p_query(tnr->answer);
			return (0);	/* wait again */
		}
		if (!(_res.options & RES_IGNTC) && anhp->tc) {
			/*
			 * get rest of answer
			 */
			prnt(P_INFO, "truncated answer..\n");
			(void) close(tnr->udp_socket);
			tnr->udp_socket = -1;
			tnr->using_tcp = 1;
			return (-1);
		}
		tnr->answer_len = resplen;
		return (1);
	} else {
		/* tcp case */
		s = tnr->tcp_socket;
		/*
		 * Receive length & response
		 */
		cp = tnr->answer;
		len = sizeof (short);
		while (len != 0 && (n = read(s, (char *) cp, (int) len)) > 0) {
			cp += n;
			len -= n;
		}
		if (n <= 0) {
			prnt(P_ERR, "read failed: %s.\n", strerror(errno));
			(void) close(s);
			tnr->tcp_socket = -1;
			return (-1);
		}
		cp = tnr->answer;
		if ((resplen = ntohs(*(u_short *) cp)) > MAXPACKET) {
			prnt(P_INFO, "response truncated.\n");
			len = MAXPACKET;
			truncated = 1;
		} else
			len = resplen;
		while (len != 0 && (n = read(s, (char *) cp, (int) len)) > 0) {
			cp += n;
			len -= n;
		}
		if (n <= 0) {
			prnt(P_ERR, "read failed: %s.\n", strerror(errno));
			(void) close(s);
			tnr->tcp_socket = -1;
			return (-1);
		}
		if (truncated) {
			/*
			 * Flush rest of answer so connection stays in synch.
			 */
			anhp->tc = 1;
			len = resplen - MAXPACKET;
			while (len != 0) {
				n = (len > sizeof (junk) ? sizeof (junk) : len);
				if ((n = read(s, junk, n)) > 0)
					len -= n;
				else
					break;
			}
		}
		if (hp->id != anhp->id) {
			/*
			 * response from old query, ignore it
			 */
			prnt(P_INFO, "old answer.\n");
			if (verbose && verbose_out) p_query(tnr->answer);
			return (0);	/* wait again */

		}
		tnr->answer_len = resplen;
		return (1);
	}
}
