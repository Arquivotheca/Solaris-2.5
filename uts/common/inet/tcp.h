/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_INET_TCP_H
#define	_INET_TCP_H

#pragma ident	"@(#)tcp.h	1.7	93/02/04 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	TCP_IOC_DEFAULT_Q	(('T' << 8) + 51)

#define	TCP_NODELAY	1
#define	TCP_MAXSEG	2

/* TCP states */
#define	TCPS_CLOSED		-6
#define	TCPS_IDLE		-5	/* idle (opened, but not bound) */
#define	TCPS_BOUND		-4	/* bound, ready to connect or accept */
#define	TCPS_LISTEN		-3	/* listening for connection */
#define	TCPS_SYN_SENT		-2	/* active, have sent syn */
#define	TCPS_SYN_RCVD		-1	/* have received syn (and sent ours) */
/* states < TCPS_ESTABLISHED are those where connections not established */
#define	TCPS_ESTABLISHED	0	/* established */
#define	TCPS_CLOSE_WAIT		1	/* rcvd fin, waiting for close */
/* states > TCPS_CLOSE_WAIT are those where user has closed */
#define	TCPS_FIN_WAIT_1		2	/* have closed and sent fin */
#define	TCPS_CLOSING		3	/* closed, xchd FIN, await FIN ACK */
#define	TCPS_LAST_ACK		4	/* had fin and close; await FIN ACK */
/* states > TCPS_CLOSE_WAIT && < TCPS_FIN_WAIT_2 await ACK of FIN */
#define	TCPS_FIN_WAIT_2		5	/* have closed, fin is acked */
#define	TCPS_TIME_WAIT		6	/* in 2*msl quiet wait after close */

/* TCP Protocol header */
typedef	struct tcphdr_s {
	u_char	th_lport[2];		/* Source port */
	u_char	th_fport[2];		/* Destination port */
	u_char	th_seq[4];		/* Sequence number */
	u_char	th_ack[4];		/* Acknowledgement number */
	u_char	th_offset_and_rsrvd[1]; /* Offset to the packet data */
	u_char	th_flags[1];
	u_char	th_win[2];		/* Allocation number */
	u_char	th_sum[2];		/* TCP checksum */
	u_char	th_urp[2];		/* Urgent pointer */
} tcph_t;

/* TCP Protocol header (used if the header is known to be 32-bit aligned) */
typedef	struct tcphdra_s {
	u_short	tha_lport;		/* Source port */
	u_short	tha_fport;		/* Destination port */
	u_long	tha_seq;		/* Sequence number */
	u_long	tha_ack;		/* Acknowledgement number */
	u_char	tha_offset_and_reserved; /* Offset to the packet data */
	u_char	tha_flags;
	u_short	tha_win;		/* Allocation number */
	u_short	tha_sum;		/* TCP checksum */
	u_short	tha_urp;		/* Urgent pointer */
} tcpha_t;

extern int	tcpdevflag;

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_TCP_H */
