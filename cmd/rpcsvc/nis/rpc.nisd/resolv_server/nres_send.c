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
nres_xmit(tnr)
	struct nres    *tnr;
{
	char		*buf;
	int		buflen;
	int		v_circuit;
	u_short		len;

	struct iovec    iov[2];

	buf = tnr->question;
	buflen = tnr->question_len;

	prnt(P_INFO, "nres_xmit().\n");
	if (verbose && verbose_out) p_query(buf);
	if (!(_res.options & RES_INIT))
		if (res_init() == -1) {
			return (-1);
		}
	v_circuit = (_res.options & RES_USEVC) || buflen > PACKETSZ;
	if (tnr->using_tcp)
		v_circuit = 1;
	if (v_circuit)
		tnr->using_tcp = 1;

	prnt(P_INFO, "this is retry %d.\n", tnr->retries);

	if (tnr->retries >= _res.retry - 1) {
		prnt(P_INFO,
			"nres_xmit -- retries exausted %d.\n", _res.retry);
		return (-1);
	}
	if (tnr->current_ns >= _res.nscount) {
		tnr->current_ns = 0;
		tnr->retries = tnr->retries + 1;
	}
	tnr->nres_rpc_as.as_timeout_remain.tv_sec = (_res.retrans <<
						(tnr->retries)) / _res.nscount;
	tnr->nres_rpc_as.as_timeout_remain.tv_usec = 0;
	if (tnr->nres_rpc_as.as_timeout_remain.tv_sec < 1)
		tnr->nres_rpc_as.as_timeout_remain.tv_sec = 1;

	for (; tnr->current_ns < _res.nscount; tnr->current_ns++) {
		prnt(P_INFO,
		"Querying server (# %d) address = %s.\n", tnr->current_ns + 1,
			inet_ntoa(_res.nsaddr_list[tnr->current_ns].sin_addr));
		if (v_circuit) {

			/*
			 * Use virtual circuit.
			 */
			if (tnr->tcp_socket < 0) {
				tnr->tcp_socket = socket(AF_INET,
							SOCK_STREAM, 0);
				if (tnr->tcp_socket < 0) {
					prnt(P_ERR, "socket failed: %s.\n",
							strerror(errno));
					if (tnr->udp_socket < 0) return (-1);
				}
				if (connect(tnr->tcp_socket,
		(struct sockaddr *) &(_res.nsaddr_list[tnr->current_ns]),
					    sizeof (struct sockaddr)) < 0) {
					prnt(P_ERR, "connect failed: %s.\n",
							strerror(errno));
					(void) close(tnr->tcp_socket);
					tnr->tcp_socket = -1;
					continue;
				}
			}
			/*
			 * Send length & message
			 */
			len = htons((u_short) buflen);
			iov[0].iov_base = (caddr_t) & len;
			iov[0].iov_len = sizeof (len);
			iov[1].iov_base = tnr->question;
			iov[1].iov_len = tnr->question_len;
			if (writev(tnr->tcp_socket, iov, 2) !=
					sizeof (len) + buflen) {
				prnt(P_ERR, "write failed: %s.\n",
							strerror(errno));
				(void) close(tnr->tcp_socket);
				tnr->tcp_socket = -1;
				continue;
			}
			/* reply will come on tnr->tcp_socket */
		} else {
			/*
			 * Use datagrams.
			 */
			if (tnr->udp_socket < 0)
				tnr->udp_socket = socket(AF_INET,
							SOCK_DGRAM, 0);
			if (tnr->udp_socket < 0) return (-1);

			if (sendto(tnr->udp_socket, buf, buflen, 0,
			(struct sockaddr *) &_res.nsaddr_list[tnr->current_ns],
					sizeof (struct sockaddr)) != buflen) {
				prnt(P_ERR, "sendto failed: %s.\n",
							strerror(errno));
				continue;
			} else {
				if (tnr->retries == 0)
					return (0);
			}
		}
	}
	return (0);
}
