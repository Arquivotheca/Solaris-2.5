/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)tcp.c	1.106	95/10/25 SMI"

/* #define TCP_PERF	*/	/* Gather stats */
/* #define TCP_PERF_LEN	*/	/* Trace mblk lengths etc */

#ifndef	MI_HDRS

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strlog.h>
#include <sys/tihdr.h>
#include <sys/tiuser.h>
#include <sys/timod.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/vtrace.h>

#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/isa_defs.h>
#include <netinet/in.h>

#include <inet/common.h>
#include <inet/ip.h>
#include <inet/mi.h>
#include <inet/mib2.h>
#include <inet/nd.h>
#include <inet/optcom.h>
#include <inet/snmpcom.h>

#else

#include <types.h>
#include <stream.h>
#include <stropts.h>
#include <strstat.h>
#include <strlog.h>
#include <tihdr.h>
#include <tiuser.h>
#include <timod.h>

#include <errno.h>
#include <signal.h>
#include <socket.h>
#include <isa_defs.h>
#include <in.h>

#include <common.h>
#include <ip.h>
#include <mi.h>
#include <mib2.h>
#include <nd.h>
#include <lsystm.h>
#include <optcom.h>
#include <snmpcom.h>

#endif

/*
 * KLUDGE ALERT: a general purpose Synchronous STREAMS solution is needed
 *		 to deal with the switch over from normal STREAMS mode to
 *		 synchronous mode. For now the following ioctl is used.
 *
 * Synchronous STREAMS M_IOCTL is used by a read-side synchronous barrier
 * queue to synchronise with the streamhead the arrival of all previously
 * putnext()ed mblks. When the M_IOCTL mblk is processed by the stremahead
 * it will be sent back down the write-side as a M_IOCNAK.
 */
#define	I_SYNCSTR	-1		/* The unsupported ioctl */

/* TODO:
 *	security
 *	precedence
 *	3d matrix testing, implementations+bandwidth+noise
 *	streamlined rput/wput replacements for use in ESTABLISHED state
 *	higher perf for MP SUNOS (and other non MPS, non single thread)
 *	T_unitdata sends (bind/connect/write), this implies data on SYN pkt
 */

/*
 * Synchronization notes:
 *
 * The global data in tcp is protected by a single, global readers/writer lock.
 * Each tcp instance is protected by the D_MTQPAIR perimeter.
 *
 * The global lock is never held while calling putnext and friends. However, it
 * is held while calling lateral_putq().
 */

/*
 * At any point in the code where we want to do a putnext into a queue other
 * than the one that we rode in on, we use the macro lateral_putnext rather
 * than call putnext directly.  In multithread streams environments that need
 * to do explicit dance steps to enter a particular stream, this macro can
 * be redefined to do the necessary synchronization work, or call a routine
 * to do it.  Note that we pass both the queue we are currently processing
 * and the one we want to hand to putnext, since they may be in the same,
 * or indeed the same queue, and we leave it to the implementation dependent
 * code to figure it out.
 */
#ifndef	lateral_putnext
#define	lateral_putnext(ourq,nextq,mp)	putnext(nextq,mp)
#endif
#ifndef	lateral_put
#define	lateral_put(ourq,nextq,mp)	put(nextq,mp)
#endif
#ifndef	lateral_putq
#define	lateral_putq(ourq,nextq,mp)	(void) putq(nextq,mp)
#endif

/* Macros for TCP sequence number comparisons */
#define	SEQ_LT(a,b)	((int)((a)-(b)) < 0)
#define	SEQ_LEQ(a,b)	((int)((a)-(b)) <= 0)
#define	SEQ_GT(a,b)	((int)((a)-(b)) > 0)
#define	SEQ_GEQ(a,b)	((int)((a)-(b)) >= 0)

/*
 * Parameters for TCP Initial Sequence Number (ISS) generation.  The
 * ISS is calculated by adding two components: a time component which
 * grows by 128,000 every second; and an "extra" component that grows
 * either by 64,000 or by a random amount centered approximately on
 * 64,000, for every connection.  This causes the the ISS generator to
 * cycle every 9 hours if no TCP connections are made, and faster if
 * connections are made.
 */
#define ISS_INCR      (125*1024)
#define ISS_NSEC_DIV  (8000)

static u_long tcp_iss_incr_extra;     /* Incremented for each connection */
 
/*
 * Private (and possibly temporary) ioctl used by configuration code
 * to lock in the "default" stream for detached closes.
 */
#define	TCP_IOC_DEFAULT_Q	(('T' << 8) + 51)	/**/

#define	TCP_IS_DETACHED(tcp)		((tcp)&&((tcp)->tcp_detached))
#define	TCP_IS_DETACHING(tcp)		((tcp)&&((tcp)->tcp_detaching))

#define	TCP_HDR_LENGTH(tcph) (((tcph)->th_offset_and_rsrvd[0] >>2 )&(0xF << 2))
#define	TCP_MAX_COMBINED_HEADER_LENGTH	(64 + 64) /* Maxed out ip + tcp */
#define	TCP_MAX_IP_OPTIONS_LENGTH	(64 - IP_SIMPLE_HDR_LENGTH)
#define	TCP_MAX_HDR_LENGTH		64
#define	TCP_MAX_TCP_OPTIONS_LENGTH	(64 - sizeof(tcph_t))
#define	TCP_MIN_HEADER_LENGTH		20

#define	TCPIP_HDR_LENGTH(mp,n)					\
	(n) = IPH_HDR_LENGTH((mp)->b_rptr),			\
	(n) += TCP_HDR_LENGTH((tcph_t *)&(mp)->b_rptr[(n)])

/*
 * TCP reassembly macros.  We hide starting and ending sequence numbers in
 * b_next and b_prev of messages on the reassembly queue.  The messages are
 * chained using b_cont.  These macros are used in tcp_reass() so we don't
 * have to see the ugly casts and assignments.
 */
#define	TCP_REASS_SEQ(mp)		((u_long)((mp)->b_next))
#define	TCP_REASS_SET_SEQ(mp, u)	((mp)->b_next = (mblk_t *)(u))
#define	TCP_REASS_END(mp)		((u_long)((mp)->b_prev))
#define	TCP_REASS_SET_END(mp, u)	((mp)->b_prev = (mblk_t *)(u))

#define	TCP_TIMER_RESTART(tcp,intvl)			\
	((tcp)->tcp_ms_we_have_waited = 0,		\
	mi_timer((tcp)->tcp_wq, (tcp)->tcp_timer_mp,	\
		(tcp)->tcp_timer_interval = intvl))

#define	TCP_XMIT_LOWATER	2048
#define	TCP_XMIT_HIWATER	8192
#define	TCP_RECV_LOWATER	2048
#define	TCP_RECV_HIWATER	8192

/* IPPROTO_TCP User Set/Get Options */
#define	TCP_NODELAY	1				/**/
#define	TCP_MAXSEG	2				/**/

#define TCP_NOTIFY_THRESHOLD		0x10		/**/
#define TCP_ABORT_THRESHOLD		0x11		/**/
#define TCP_CONN_NOTIFY_THRESHOLD	0x12		/**/
#define TCP_CONN_ABORT_THRESHOLD	0x13		/**/

#ifndef	T_CURRENT
#define	T_CURRENT	MI_T_CURRENT
#endif

#ifndef	STRMSGSZ
#define	STRMSGSZ	4096
#endif

#define	TCP_CONN_HASH(faddr, ports)					\
		((unsigned)((faddr)[3] ^ (ports)[0] ^ (ports)[1]	\
		^ (ports)[2] ^ (ports)[3]) % A_CNT(tcp_conn_fanout))
#define	TCP_BIND_HASH(lport)						\
		((unsigned)((lport)[0] ^ (lport)[1]) % A_CNT(tcp_bind_fanout))
#define	TCP_LISTEN_HASH(lport)						\
		((unsigned)((lport)[0] ^ (lport)[1]) % A_CNT(tcp_listen_fanout))
#define	TCP_QUEUE_HASH(queue)						\
		(((int)(queue) >> 8) % A_CNT(tcp_queue_fanout))

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

/** TCP Protocol header */
typedef	struct tcphdr_s {		/**/
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

/* Bit values in 'th_flags' field of the TCP packet header */
#define	TH_FIN			0x01	/* Sender will not send more */
#define	TH_SYN			0x02	/* Synchronize sequence numbers */
#define	TH_RST			0x04	/* Reset the connection */
#define	TH_PSH			0x08	/* This segment requests a push */
#define	TH_ACK			0x10	/* Acknowledgement field is valid */
#define	TH_URG			0x20	/* Urgent pointer field is valid */
/*
 * Internal flags used in conjunction with the packet header flags above.
 * Used in tcp_rput to keep track of what needs to be done.
 */
#define	TH_ACK_ACCEPTABLE	0x0400
#define	TH_XMIT_NEEDED		0x0800	/* Window opened - send queued data */
#define	TH_REXMIT_NEEDED	0x1000	/* Time expired for unacked data */
#define	TH_ACK_NEEDED		0x2000	/* Send an ack now. */
#define	TH_READJ_CWND		0x4000	/* Adjust cwnd after sending data */
#define	TH_TIMER_NEEDED		0x8000	/* Start the delayed ack/push bit timer */
#define	TH_ORDREL_NEEDED	0x10000	/* Generate an ordrel indication */

/** TCP Protocol header (used if the header is known to be 32-bit aligned) */
typedef	struct tcphdra_s {		/**/
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

/* Control structure for each open TCP stream */
typedef struct tcp_s {
	struct	tcp_s	* tcp_bind_hash;/* Bind hash chain */
	struct	tcp_s	** tcp_ptpbhn;	/* Pointer to previous bind hash next. */
	struct	tcp_s	* tcp_listen_hash;/* Listen hash chain */
	struct	tcp_s	** tcp_ptplhn;	/* Pointer to previous listen hash next. */
	struct	tcp_s	* tcp_conn_hash;/* Connect hash chain */
	struct	tcp_s	** tcp_ptpchn;	/* Pointer to previous conn hash next. */
	struct	tcp_s	* tcp_queue_hash;/* Queue hash chain */
	struct	tcp_s	** tcp_ptpqhn;	/* Pointer to previous queue hash next. */
	int	tcp_state;
	queue_t	* tcp_rq;		/* Our upstream neighbor (client) */
	queue_t	* tcp_wq;		/* Our downstream neighbor */

	/* Fields arranged in approximate access order along main paths */
	mblk_t	* tcp_xmit_head;	/* Head of rexmit list */
	mblk_t	* tcp_xmit_last;	/* last valid data seen by tcp_wput */
	u_long	tcp_unsent;		/* # of bytes in hand that are unsent*/
	mblk_t	* tcp_xmit_tail;	/* Last rexmit data sent */
	u_long	tcp_xmit_tail_unsent;	/* # of unsent bytes in xmit_tail */

	u_long	tcp_snxt;		/* Senders next seq num */
	u_long	tcp_suna;		/* Sender unacknowledged */
	u_long	tcp_swnd;		/* Senders window (relative to suna) */
	u_long	tcp_cwnd;		/* Congestion window */

	u_long	tcp_ibsegs;		/* Inbound segments on this stream */
	u_long	tcp_obsegs;		/* Outbound segments on this stream */

	u_long	tcp_mss;		/* Max segment size */
	u_long	tcp_naglim;		/* Tunable nagle limit */
	int	tcp_hdr_len;		/* Byte len of combined TCP/IP hdr */
	tcph_t	* tcp_tcph;		/* tcp header within combined hdr */
	int	tcp_tcp_hdr_len;	/* tcp header len within combined */
	unsigned int	tcp_valid_bits;
#define	TCP_ISS_VALID	0x1	/* Is the tcp_iss seq num active? */
#define	TCP_FSS_VALID	0x2	/* Is the tcp_fss seq num active? */
#define	TCP_URG_VALID	0x4	/* If the tcp_urg seq num active? */


	int	tcp_xmit_hiwater;	/* Send buffer high water mark. */
	mblk_t	* tcp_flow_mp;		/* mp to exert flow control upstream */

	long	tcp_ms_we_have_waited;	/* Total # of ms we have waited */
	mblk_t	* tcp_timer_mp;		/* Control block for timer service */
	u_long	tcp_timer_interval;	/* # of ms we waited for this timer */

	unsigned int
		tcp_urp_old : 1,	/* Use RFC793 style urgent pointers */
		tcp_urp_sig_sent : 1,	/* If we've sent M_SIG for current OOB*/
		tcp_hard_binding : 1,	/* If we've started a full bind */
		tcp_hard_bound : 1,	/* If we've done a full bind with IP */
		
		tcp_priv_stream : 1, 	/* If stream was opened by priv user */
		tcp_passive_open : 1,	/* Was this a passive open? */
		tcp_fin_acked : 1,	/* Has our FIN been acked? */
		tcp_fin_rcvd : 1,	/* Have we seen a FIN? */
		
		tcp_fin_sent : 1,	/* Have we sent our FIN yet? */
		tcp_ordrel_done : 1,	/* Have we sent the ord_rel upstream? */
		tcp_flow_stopped : 1,	/* Have we flow controlled xmitter? */
		tcp_debug : 1,		/* SO_DEBUG "socket" option. */
		
		tcp_dontroute : 1,	/* SO_DONTROUTE "socket" option. */
		tcp_broadcast : 1,	/* SO_BROADCAST "socket" option. */
		tcp_useloopback : 1,	/* SO_USELOOPBACK "socket" option. */
		tcp_reuseaddr : 1,	/* SO_REUSEADDR "socket" option. */
		
		tcp_oobinline : 1,	/* SO_OOBINLINE "socket" option. */
		tcp_detached : 1,	/* If we're detached from a stream */
		tcp_detaching : 1,	/* If we're detaching from a stream */
		tcp_bind_pending : 1,	/* Client is waiting for bind ack */

		tcp_unbind_pending : 1, /* Client sent T_UNBIND_REQ */
		tcp_deferred_clean_death : 1,
					/* defer tcp endpoint cleanup etc. */
		tcp_co_wakeq_done : 1,	/* A strwakeq() has been done */
		tcp_co_wakeq_force : 1,	/* A strwakeq() must be done */

		tcp_co_norm : 1,	/* In normal mode, putnext() done */
		tcp_co_wakeq_need : 1,	/* A strwakeq() needs to be done */

		tcp_zero_win_probe: 1,	/* Zero win probing is in progress */

		tcp_junk_fill_thru_bit_31 : 5;

	u_long	tcp_rnxt;		/* Seq we expect to recv next */
	u_long	tcp_rwnd;		/* Current receive window */
	u_long	tcp_rwnd_max;		/* Maximum receive window */
	long	tcp_credit;		/* Recv window credit before canput. */
	long	tcp_credit_init;	/* Recv window credit initializer. */

	mblk_t	* tcp_reass_head;	/* Out of order reassembly list head */
	mblk_t	* tcp_reass_tail;	/* Out of order reassembly list tail */

	mblk_t	* tcp_rcv_head;		/* Queued until push, urgent data or */
	mblk_t	* tcp_rcv_tail;		/* the count exceeds */
	u_long	tcp_rcv_cnt;		/* tcp_rcv_push_wait. */

	mblk_t	* tcp_co_head;		/* Co (copyout) queue head */
	mblk_t	* tcp_co_tail;		/*  "     "       "   tail */
	mblk_t	* tcp_co_tmp;		/* Co timer mblk */
	/*
	 * Note: tcp_co_imp is used to both indicate the read-side stream
	 *	 data flow state (synchronous/asynchronous) as well as a
	 *	 pointer to a reusable iocblk sized mblk.
	 *
	 *	 The mblk is allocated (if need be) at initialization time
	 *	 and is used by the read-side when a copyout queue eligible
	 *	 mblk arrives (synchronous data flow) but previously one or
	 *	 more mblk(s) have been putnext()ed (asynchronous data flow).
	 *	 In this case, the mblk pointed to by tcp_co_imp is putnext()ed
	 *	 as an M_IOCTL of I_SYNCSTR after first nullifying tcp_co_imp.
	 *	 The streamhead will putnext() the mblk down the write-side
	 *	 stream as an M_IOCNAK of I_SYNCSTR and when (if) it arrives at
	 *	 the write-side its pointer will be saved again in tcp_co_imp.
	 *
	 *	 If an instance of tcp is closed while its tcp_co_imp is null,
	 *	 then the mblk will be freed elsewhere in the stream. Else, it
	 *	 will be freed (close) or saved (reinit) for future use.
	 */
	mblk_t	* tcp_co_imp;		/* Co ioctl mblk */
	long	tcp_co_tintrvl;		/* Co timer interval */
	u_long	tcp_co_rnxt;		/* Co seq we expect to recv next */
	u_long	tcp_co_cnt;		/* Co enqued byte count */

	u_long	tcp_cwnd_ssthresh;	/* Congestion window */
	u_long	tcp_cwnd_bytes_acked;
	u_long	tcp_cwnd_max;
	u_long	tcp_csuna;		/* Clear (no rexmits in window) suna */

	long	tcp_rto;		/* Round trip timeout */
	long	tcp_rtt_sa;		/* Round trip timeout param */
	long	tcp_rtt_sd;		/* Round trip timeout param */
	long	tcp_rtt_nt;		/* Round trip next lbolt */
	long	tcp_rtt_mt;		/* Round trip maximum lbolt */

	u_long	tcp_swl1;		/* These help us avoid using stale */
	u_long	tcp_swl2;		/*  packets to update state */

	u_long	tcp_rack;		/* Seq # we have acked */
	u_long	tcp_rack_cnt;		/* # of bytes we have deferred ack */
	u_long	tcp_rack_cur_max;	/* # bytes we may defer ack for now */
	u_long	tcp_rack_abs_max;	/* # of bytes we may defer ack ever */

	u_long	tcp_max_swnd;		/* Maximum swnd we have seen */

	struct tcp_s * tcp_listener;	/* Our listener */

	int	tcp_xmit_lowater;	/* Send buffer low water mark. */

	u_long	tcp_irs;		/* Initial recv seq num */
	u_long	tcp_iss;		/* Initial send seq num */
	u_long	tcp_fss;		/* Final/fin send seq num */
	u_long	tcp_urg;		/* Urgent data seq num */

	u_long	tcp_conn_req_cnt;	/* # of pending conn reqs we have */
	u_long	tcp_conn_req_max;	/* # of pending conn reqs we allow */
	u_long	tcp_conn_req_seqnum;	/* Incrementing pending conn req ID */

	int	tcp_ip_hdr_len;		/* Byte len of our current IP header */
	long	tcp_first_timer_threshold;  /* When to prod IP */
	long	tcp_second_timer_threshold; /* When to give up completely */
	long	tcp_first_ctimer_threshold; /* 1st threshold while connecting */
	long	tcp_second_ctimer_threshold;/* 2nd threshold while connecting */

	/* TODO: tcp_linger is tracked but otherwise unimplemented */
	int	tcp_linger;		/* Close linger time (in seconds) */

	int	tcp_urp;		/* Urgent 'pointer' from inbound OOB */
	mblk_t	* tcp_urp_mp;		/* Last mblk seen with TH_URG set */

	struct tcp_s * tcp_eager_next;	/* Chain of detached guys on listener */
	mblk_t	* tcp_eager_conn_ind;	/* T_CONN_IND waiting for 3rd ack. */
	long	tcp_keepalive_intrvl;	/* Zero means don't bother */
	mblk_t	* tcp_keepalive_mp;	/* Timer block for keepalive */

	int	tcp_client_errno;	/* How the client screwed up */

	union {
		iph_t	tcp_u_iph;		/* template ip header */
		ipha_t	tcp_u_ipha;
		char	tcp_u_buf[TCP_MAX_COMBINED_HEADER_LENGTH];
		double	tcp_u_aligner;
	} tcp_u;
#define	tcp_iph		tcp_u.tcp_u_iph
#define	tcp_ipha	tcp_u.tcp_u_ipha
#define	tcp_iphc	tcp_u.tcp_u_buf
	u_long	tcp_sum;		/* checksum to compensate for source 
					 * routed packets. */
	u32	tcp_remote;		/* true remote address - needed for
					 * source routing. */
	u32	tcp_bound_source;	/* IP address in bind_req */
	u_short	tcp_last_sent_len;	/* Record length for nagle */
	u_short	tcp_dupack_cnt;		/* # of consequtive duplicate acks */

	SYNC_CHK_DCL
	MI_HRT_DCL(tcp_rtime)
	MI_HRT_DCL(tcp_wtime)
} tcp_t;

/* Named Dispatch Parameter Management Structure */
typedef struct tcpparam_s {
	u_long	tcp_param_min;
	u_long	tcp_param_max;
	u_long	tcp_param_val;
	char	* tcp_param_name;
} tcpparam_t;

/* TCP Timer control structure */
typedef struct tcpt_s {
	pfv_t	tcpt_pfv;	/* The routine we are to call */
	tcp_t	* tcpt_tcp;	/* The parameter we are to pass in */
} tcpt_t;

/* TCP Keepalive Timer Block */
typedef struct tcpka_s {
	tcpt_t	tcpka_tcpt;
	u_long	tcpka_snxt;	/* snxt at previous interval */
	u_long	tcpka_rnxt;	/* rnxt at previous interval */
	u_long	tcpka_seq;	/* retransmitted sequence number */
} tcpka_t;
#define	tcpka_tcp	tcpka_tcpt.tcpt_tcp

staticf long	tcp_random( void );
staticf	void	tcp_accept(   queue_t * q, mblk_t * mp   );
staticf void	tcp_accept_comm(   tcp_t * listener, tcp_t * acceptor,
				   mblk_t * cr_pkt   );
staticf	void	tcp_adapt(   tcp_t * tcp, mblk_t * ire_mp   );
staticf	u_short	tcp_bindi(   u_long port_param, u_char *addr,
			     int reuseaddr   );
staticf	int	tcp_close(   queue_t * q   );
staticf	void	tcp_closei(   tcp_t * tcp   );
staticf void	tcp_close_detached(   tcp_t * tcp   );
staticf boolean_t tcp_conn_con(   tcp_t * tcp, iph_t * iph, tcph_t * tcph   );
staticf	void	tcp_connect(   queue_t * q, mblk_t * mp   );
staticf void	tcp_conn_request(   tcp_t * tcp, mblk_t * mp   );
staticf	void	tcp_clean_death(   tcp_t * tcp, int err   );
staticf	void	tcp_def_q_set(   queue_t * q, mblk_t * mp   );
staticf boolean_t tcp_detach (   tcp_t * tcp   );
staticf	void	tcp_disconnect(   queue_t * q, mblk_t * mp   );
staticf	char	* tcp_display(   tcp_t * tcp   );
staticf	boolean_t tcp_eager_blowoff(   tcp_t * listener, long seqnum   );
staticf	void	tcp_eager_cleanup(   tcp_t * listener   );
staticf	void	tcp_eager_swap(   tcp_t * acceptor, tcp_t * eager   );
staticf void	tcp_eager_swap_fixup(   tcp_t * newt, tcp_t * oldt   );
staticf	void	tcp_err_ack(   queue_t * q, mblk_t * mp, int tlierr,
			       int unixerr   );
staticf	void	tcp_err_ack_prim(   queue_t * q, mblk_t * mp, int primitive,
			       int tlierr, int unixerr   );
staticf	int	tcp_getstate(   tcp_t * tcp   );
staticf	void	tcp_bind_hash_insert(   tcp_t ** tcpp, tcp_t * tcp   );
staticf	void	tcp_bind_hash_remove(   tcp_t * tcp   );
staticf	void	tcp_listen_hash_insert(   tcp_t ** tcpp, tcp_t * tcp   );
staticf	void	tcp_listen_hash_remove(   tcp_t * tcp   );
staticf	void	tcp_conn_hash_insert(   tcp_t ** tcpp, tcp_t * tcp   );
staticf	void	tcp_conn_hash_remove(   tcp_t * tcp   );
staticf	tcp_t	* tcp_queue_hash_lookup(   queue_t * driverq   );
staticf	void	tcp_queue_hash_insert(   tcp_t ** tcpp, tcp_t * tcp   );
staticf	void	tcp_queue_hash_remove(   tcp_t * tcp   );
staticf	void	tcp_info_req(   tcp_t * tcp, mblk_t * mp   );
staticf	int	tcp_init(   tcp_t * tcp, queue_t * q, mblk_t * timer_mp,
			mblk_t *flow_mp, mblk_t *co_tmp, mblk_t *co_imp   );
staticf mblk_t	* tcp_ip_bind_mp(   tcp_t * tcp, int addr_length   );
staticf void	tcp_ip_notify(   tcp_t * tcp   );
staticf void	tcp_ip_unbind(   queue_t * wq   );
staticf	mblk_t	* tcp_ire_mp(   mblk_t * mp   );
staticf	void	tcp_keepalive_killer(   tcp_t * tcp   );
staticf	void	tcp_lift_anchor(   tcp_t * tcp   );
staticf	tcp_t	* tcp_lookup(   iph_t * iph, tcph_t * tcph, int min_state   );
staticf	tcp_t	* tcp_lookup_by_port(   tcp_t * prev_tcp, u_char * lport,
					u_char * laddr, int min_state   );
staticf tcp_t	* tcp_lookup_match(u8 *lport, u8 *laddr, u8 *fport, u8 *faddr,
					int min_state);
staticf tcp_t	* tcp_lookup_listener(   u_char * lport, u_char * laddr   );
staticf	tcp_t	* tcp_lookup_reversed(   iph_t * iph, tcph_t * tcph,
				int min_state   );
staticf void	tcp_maxpsz_set(   tcp_t * tcp   );
staticf u_long	tcp_mss_get(   tcph_t * tcph   );
staticf void	tcp_mss_set(   tcp_t * tcp, u_long size   );
staticf	int	tcp_open(   queue_t * q, dev_t * devp, int flag, int sflag,
			    cred_t * credp   );
staticf tcp_t	* tcp_open_detached(   queue_t * q   );
staticf	boolean_t tcp_opt_chk(   int level, int name   );
staticf	int	tcp_opt_get(   queue_t * q, int level, int name,
			u_char * ptr   );
staticf	int	tcp_opt_get_user(   iph_t * iph, u_char * ptr   );
staticf	int	tcp_opt_set(   queue_t * q, int level, int name, u_char * ptr,
			       int len   );
staticf void	tcp_opt_reverse(   tcp_t * tcp, iph_t * iph   );
staticf int	tcp_opt_set_header(   tcp_t * tcp, u_char * ptr, u_int len   );
staticf	void	tcp_param_cleanup(   void   );
staticf int	tcp_param_get(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf boolean_t tcp_param_register(   tcpparam_t * tcppa, int cnt   );
staticf int	tcp_param_set(   queue_t * q, mblk_t * mp, char * value,
				 caddr_t cp   );
staticf mblk_t	* tcp_reass(   tcp_t * tcp, mblk_t * mp, u_long start,
			    u_int *flagsp);
staticf void	tcp_reass_elim_overlap(   tcp_t * tcp, mblk_t * mp   );
staticf void	tcp_reinit(   tcp_t * tcp   );
staticf	void	tcp_rput(   queue_t * q, mblk_t * mp   );
staticf void	tcp_rput_data(   queue_t * q, mblk_t * mp , int isput  );
staticf void	tcp_rput_other(   queue_t * q, mblk_t * mp   );
staticf	void	tcp_rsrv(   queue_t * q   );
staticf	int	tcp_rwnd_set(   tcp_t * tcp, u_long rwnd   );
#ifdef notdef
staticf boolean_t tcp_security_ok(   tcp_t * tcp, tcph_t * tcph   );
#endif /* notdef */
staticf	int	tcp_snmp_get(   queue_t * q, mblk_t * mpctl   );
staticf	int	tcp_snmp_set(   queue_t * q, int level, int name, u_char * ptr,
			       int len   );
staticf int	tcp_snmp_state(   tcp_t * tcp   );
staticf	int	tcp_status_report(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf	int	tcp_bind_hash_report(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf	int	tcp_listen_hash_report(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf	int	tcp_conn_hash_report(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf	int	tcp_queue_hash_report(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf	void	tcp_timer(   tcp_t * tcp   );
staticf mblk_t	* tcp_timer_alloc(   tcp_t * tcp, pfv_t func, int extra   );
#ifdef	MI_HRTIMING
staticf	int	tcp_time_report(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf	int	tcp_time_reset(   queue_t * q, mblk_t * mp, caddr_t cp   );
#endif
staticf	void	tcp_wput(   queue_t * q, mblk_t * mp   );
staticf	void	tcp_wput_slow(   tcp_t * tcp, mblk_t * mp   );
staticf	void	tcp_wput_flush(   queue_t * q, mblk_t * mp   );
staticf	void	tcp_wput_iocdata(   queue_t * q, mblk_t * mp   );
staticf	void	tcp_wput_ioctl(   queue_t * q, mblk_t * mp   );
staticf	void	tcp_wput_proto(   queue_t * q, mblk_t * mp   );
staticf	void	tcp_wsrv(   queue_t * q   );
staticf int	tcp_xmit_end(   tcp_t * tcp   );
staticf	void	tcp_xmit_listeners_reset(   queue_t * rq, mblk_t * mp   );

staticf mblk_t	* tcp_xmit_mp(   tcp_t * tcp, mblk_t * mp,
			 u_long max_to_send, u_long seq, int sendall   );
staticf mblk_t	* tcp_ack_mp( tcp_t * tcp );
staticf void	tcp_xmit_early_reset(   char * str, queue_t * q, mblk_t * mp,
				        u_long seq, u_long ack, int ctl   );
staticf void	tcp_xmit_ctl(   char * str, tcp_t * tcp, mblk_t * mp,
				u_long seq, u_long ack, int ctl   );
staticf	void	tcp_co_drain(   tcp_t * tcp   );
staticf	void	tcp_co_timer(   tcp_t * tcp   );
staticf	int	tcp_rinfop(   queue_t * q, infod_t * dp   );
staticf	int	tcp_rrw(   queue_t * q, struiod_t * dp   );
staticf	int	tcp_winfop(   queue_t * q, infod_t * dp   );
staticf	int	tcp_wrw(   queue_t * q, struiod_t * dp   );

staticf void	strwakeqclr(    queue_t * q, int flag   );
staticf int	struio_ioctl(	queue_t * rq, mblk_t * mp	);
staticf int	setmaxps(queue_t * q , int maxpsz);

static struct module_info tcp_rinfo =  {
#define	MODULE_ID	5105
	MODULE_ID, "tcp", 0, INFPSZ, TCP_RECV_HIWATER, TCP_RECV_LOWATER
};

static struct module_info tcp_winfo =  {
#define	MODULE_ID	5105
	MODULE_ID, "tcp", 0, INFPSZ, 127, 16
};

static struct qinit rinit = {
	(pfi_t)tcp_rput, (pfi_t)tcp_rsrv, tcp_open, tcp_close, (pfi_t)0, 
	&tcp_rinfo, (struct module_stat *)0, tcp_rrw, tcp_rinfop, STRUIOT_IP
};

static struct qinit winit = {
	(pfi_t)tcp_wput, (pfi_t)tcp_wsrv, (pfi_t)0, (pfi_t)0, (pfi_t)0, 
	&tcp_winfo, (struct module_stat *)0, tcp_wrw, tcp_winfop, STRUIOT_IP
};

struct streamtab tcpinfo = {
	&rinit, &winit
};

	int	tcpdevflag = 0;

/* Protected by tcp_g_lock */
static	queue_t	* tcp_g_q;	/* Default queue used during detached closes */
static	void	* tcp_g_head;	/* Head of TCP instance data chain */
static	caddr_t	tcp_g_nd;	/* Head of 'named dispatch' variable list */
static	u_long	tcp_next_port_to_try;

/* TCP connection hash list - contains all tcp_t in connected states. */
static	tcp_t	* tcp_conn_fanout[256];
/* TCP bind hash list - all tcp_t with state >= BOUND. */
static	tcp_t	* tcp_bind_fanout[256];
/* TCP listen hash list - all tcp_t that has been in LISTEN state. */
static	tcp_t	* tcp_listen_fanout[256];
/* TCP queue hash list - all tcp_t in case they will be an acceptor. */
static	tcp_t	* tcp_queue_fanout[256];

/*
 * The global lock. 
 *
 * The global data in tcp is protected by a single, global readers/writer lock.
 * Each tcp instance is protected by the D_MTQPAIR perimeter.
 *
 * The global lock is never held while calling putnext and friends. However, it
 * is held while calling lateral_putq().
 */
	krwlock_t tcp_g_lock;

#define TCP_LOCK_READ()		rw_enter(&tcp_g_lock, RW_READER)
#define	TCP_UNLOCK_READ()	rw_exit(&tcp_g_lock)
#define TCP_LOCK_WRITE()	rw_enter(&tcp_g_lock, RW_WRITER)
#define	TCP_UNLOCK_WRITE()	rw_exit(&tcp_g_lock)
#define	TCP_WRITE_HELD()	rw_write_held(&tcp_g_lock)
#define	TCP_READ_HELD()		rw_read_held(&tcp_g_lock)


	MI_HRT_DCL(tcp_g_rtime)	/* (gh bait) */
	MI_HRT_DCL(tcp_g_wtime)	/* (gh bait) */

/*
 * MIB-2 stuff for SNMP
 * Note: tcpInErrs {tcp 15} is accumulated in ip.c
 */
static	mib2_tcp_t	tcp_mib;	/* SNMP fixed size info */
extern	mib2_ip_t	ip_mib;

/* Template for response to info request. */
static	struct T_info_ack tcp_g_t_info_ack =
	{ T_INFO_ACK, 0, 1, -2, -2, sizeof(ipa_t), 0, STRMSGSZ,  T_COTS_ORD,
	  TCPS_IDLE };

#define	MS	1L
#define	SECONDS	(1000 * MS)
#define	MINUTES	(60 * SECONDS)
#define	HOURS	(60 * MINUTES)
#define	DAYS	(24 * HOURS)
#define PARAM_MAX ((u_long) -1)

/* Max size IP datagram is 64k - 1 */
#define TCP_MSS_MAX	((64L * 1024 - 1) - (sizeof(iph_t) + sizeof(tcph_t)))

/* Largest TCP port number */
#define TCP_MAX_PORT	(64L * 1024 - 1)

/*
 * All of these are alterable, within the min/max values given, at run time.
 * Note that the default value of "tcp_close_wait_interval" is four minutes,
 * per the TCP spec.
 */
static	tcpparam_t	tcp_param_arr[] = {
 /*min		max		value		name */
 { 1*SECONDS,	10*MINUTES,	4*MINUTES,	"tcp_close_wait_interval"},
 { 1,		1024,		32,		"tcp_conn_req_max" },
 { 1*MS,	20*SECONDS,	500*MS,		"tcp_conn_grace_period" },
 { 128, 	65535,		65535,		"tcp_cwnd_max" },
 { 0,		10,		0,		"tcp_debug" },
 { 1024,	(32*1024),	1024,		"tcp_smallest_nonpriv_port"},
 { 1*SECONDS,	PARAM_MAX,	3*MINUTES,	"tcp_ip_abort_cinterval"},
 { 500*MS,	PARAM_MAX,	8*MINUTES,	"tcp_ip_abort_interval"},
 { 1*SECONDS,	PARAM_MAX,	10*SECONDS,	"tcp_ip_notify_cinterval"},
 { 500*MS,	PARAM_MAX,	10*SECONDS,	"tcp_ip_notify_interval"},
 { 1L,		255L,		255L,		"tcp_ip_ttl"},
 { 10*SECONDS,	10*DAYS,	2*HOURS,	"tcp_keepalive_interval"},
 { 1,		100,		1,		"tcp_maxpsz_multiplier" },
 { 1L,		TCP_MSS_MAX,	536L,		"tcp_mss_def"},
 { 1L,		TCP_MSS_MAX,	TCP_MSS_MAX,	"tcp_mss_max"},
 { 1L,		TCP_MSS_MAX,	1L,		"tcp_mss_min"},
 { 1L,		(64L*1024)-1,	(4L*1024)-1,	"tcp_naglim_def"},
 { 1,		1,		1,		"tcp_old_urp_interpretation" },
 { 1*MS,	20*SECONDS,	500*MS,		"tcp_rexmit_interval_initial"},
 { 1*MS,	2*HOURS,	1*MINUTES,	"tcp_rexmit_interval_max"},
 { 1*MS,	2*HOURS,	200*MS,		"tcp_rexmit_interval_min"},
 { 0,		256,		32,		"tcp_wroff_xtra" },
 { 1*MS,	1*MINUTES,	50*MS,		"tcp_deferred_ack_interval" },
 { 0,		16,		0,		"tcp_snd_lowat_fraction" },
 { 0,		128000,		0,		"tcp_sth_rcv_hiwat" },
 { 0,		128000,		0,		"tcp_sth_rcv_lowat" },
 { 0,		10,		3,		"tcp_dupack_fast_retransmit" },
 { 0,		1,		0,		"tcp_ignore_path_mtu" },
 { 1,		100,		50,		"tcp_rwin_credit_pct" },
 { 0,		128*1024,	16384,		"tcp_rcv_push_wait" },
 { 1024,	TCP_MAX_PORT,	32*1024,	"tcp_smallest_anon_port"},
 { 1024,	TCP_MAX_PORT,	TCP_MAX_PORT,	"tcp_largest_anon_port"},
 { TCP_XMIT_HIWATER, 65536, TCP_XMIT_HIWATER,	"tcp_xmit_hiwat"},
 { TCP_XMIT_LOWATER, 65536, TCP_XMIT_LOWATER,	"tcp_xmit_lowat"},
 { TCP_RECV_HIWATER, 65536, TCP_RECV_HIWATER,	"tcp_recv_hiwat"},
 { 1,		65536,		4,		"tcp_recv_hiwat_minmss"},
 { 1*SECONDS,	PARAM_MAX,	675*SECONDS,	"tcp_fin_wait_2_flush_interval"},
 { 0,		TCP_MSS_MAX,	64,		"tcp_co_min"},
 { 65536,	1024*1024*1024,	256*1024,	"tcp_max_buf"},
 { 1,		PARAM_MAX,	1,		"tcp_zero_win_probesize"},
 { 0,		1,		1,		"tcp_strong_iss"},
};

#define	tcp_close_wait_interval			tcp_param_arr[0].tcp_param_val
#define	tcp_conn_req_max_			tcp_param_arr[1].tcp_param_val
#define	tcp_conn_grace_period			tcp_param_arr[2].tcp_param_val
#define	tcp_cwnd_max_				tcp_param_arr[3].tcp_param_val
#define	tcp_dbg					tcp_param_arr[4].tcp_param_val
#define	tcp_smallest_nonpriv_port		tcp_param_arr[5].tcp_param_val
#define	tcp_ip_abort_cinterval			tcp_param_arr[6].tcp_param_val
#define	tcp_ip_abort_interval			tcp_param_arr[7].tcp_param_val
#define	tcp_ip_notify_cinterval			tcp_param_arr[8].tcp_param_val
#define	tcp_ip_notify_interval			tcp_param_arr[9].tcp_param_val
#define	tcp_ip_ttl				tcp_param_arr[10].tcp_param_val
#define	tcp_keepalive_interval			tcp_param_arr[11].tcp_param_val
#define	tcp_maxpsz_multiplier			tcp_param_arr[12].tcp_param_val
#define	tcp_mss_def				tcp_param_arr[13].tcp_param_val
#define	tcp_mss_max				tcp_param_arr[14].tcp_param_val
#define	tcp_mss_min				tcp_param_arr[15].tcp_param_val
#define	tcp_naglim_def				tcp_param_arr[16].tcp_param_val
#define	tcp_old_urp_interpretation		tcp_param_arr[17].tcp_param_val
#define	tcp_rexmit_interval_initial		tcp_param_arr[18].tcp_param_val
#define	tcp_rexmit_interval_max			tcp_param_arr[19].tcp_param_val
#define	tcp_rexmit_interval_min			tcp_param_arr[20].tcp_param_val
#define	tcp_wroff_xtra				tcp_param_arr[21].tcp_param_val
#define	tcp_deferred_ack_interval		tcp_param_arr[22].tcp_param_val
#define	tcp_snd_lowat_fraction			tcp_param_arr[23].tcp_param_val
#define	tcp_sth_rcv_hiwat			tcp_param_arr[24].tcp_param_val
#define	tcp_sth_rcv_lowat			tcp_param_arr[25].tcp_param_val
#define	tcp_dupack_fast_retransmit		tcp_param_arr[26].tcp_param_val
#define	tcp_ignore_path_mtu			tcp_param_arr[27].tcp_param_val
#define	tcp_rwin_credit_pct			tcp_param_arr[28].tcp_param_val
#define	tcp_rcv_push_wait			tcp_param_arr[29].tcp_param_val
#define	tcp_smallest_anon_port			tcp_param_arr[30].tcp_param_val
#define	tcp_largest_anon_port			tcp_param_arr[31].tcp_param_val
#define	tcp_xmit_hiwat				tcp_param_arr[32].tcp_param_val
#define	tcp_xmit_lowat				tcp_param_arr[33].tcp_param_val
#define	tcp_recv_hiwat				tcp_param_arr[34].tcp_param_val
#define	tcp_recv_hiwat_minmss			tcp_param_arr[35].tcp_param_val
#define	tcp_fin_wait_2_flush_interval		tcp_param_arr[36].tcp_param_val
#define	tcp_co_min				tcp_param_arr[37].tcp_param_val
#define	tcp_max_buf				tcp_param_arr[38].tcp_param_val
#define	tcp_zero_win_probesize			tcp_param_arr[39].tcp_param_val
#define tcp_strong_iss				tcp_param_arr[40].tcp_param_val

#define	TCPERR_UNCLAIMED	1

#ifdef TCP_PERF
int	tcp_rput_cnt, tcp_rput_putnext, tcp_rput_queue, tcp_head_free, 
	tcp_head_timer;
int	tcp_canput_cnt, tcp_rsrv_cnt;
int	tcp_rwnd_cnt, tcp_psh_cnt;
int	tcp_wput_nomem;

int 	tcp_must_alloc_tail, tcp_must_alloc_allign, tcp_must_alloc_ref,
	tcp_must_alloc_space, tcp_inline;
int	tcp_wput_cnt_1, tcp_wput_cnt_2, tcp_wput_cnt_3, tcp_wput_cnt_4;
int	tcp_xmit_cnt_1, tcp_xmit_cnt_2, tcp_xmit_cnt_3, tcp_xmit_cnt_4;
int	tcp_snd_flow_on, tcp_snd_flow_off, tcp_rcv_flow_on, tcp_rcv_flow_off;
int	tcp_rput_putnext, tcp_rput_putq, tcp_rput_append, tcp_rput_drop;
int	tcp_rsrv_putnext;

#ifdef	 TCP_PERF_LEN

#define MAX_LENGTH 1500
#define MAX_CNT 4
int tcp_lens[MAX_CNT][MAX_LENGTH];
int *tcp_len1 = tcp_lens[0];
int *tcp_len2 = tcp_lens[1];
int *tcp_len3 = tcp_lens[2];
int *tcp_len4 = tcp_lens[3];

staticf void
tcp_count_len (mp)
	mblk_t	* mp;
{
	int cnt, i, len;
	mblk_t *mp1;

	for (cnt = -1, mp1 = mp; mp1; mp1 = mp1->b_cont)
		cnt++;
	if (cnt >= MAX_CNT)
		cnt = MAX_CNT-1;
	for (i = 0, mp1 = mp; i <= cnt; i++, mp1 = mp1->b_cont) {
		len = mp1->b_wptr - mp1->b_rptr;
		if (len >= MAX_LENGTH)
			len = MAX_LENGTH-1;
		tcp_lens[i][len]++;
	}
}
#endif /* TCP_PERF_LEN */
#endif /* TCP_PERF */

/*
 * Reply to a clients T_CONN_RES TPI message
 */
staticf void
tcp_accept (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	tcp_t	* listener = (tcp_t *)q->q_ptr;
	tcp_t	* acceptor;
	tcp_t	* eager;
	struct T_conn_res	* tcr;
	long	seqnum;
	mblk_t	* bind_mp;
	mblk_t	* ok_mp;

	tcr = (struct T_conn_res *)ALIGN32(mp->b_rptr);
	/*
	 * The stream head points tcr->QUEUE_ptr at the read side queue
	 * of the streams device underneath us i.e. the read side queue
	 * of 'ip'.  Here we walk back to get at our instance data.
	 *
	 * Hold the write lock for the duration of the accept processing.
	 *
	 * This prevents any thread from entering the acceptor queue from
	 * below (since it has not been hard bound yet i.e. any inbound
	 * packets will arrive on the default tcp queue and go through
	 * tcp_lookup).
	 * It also prevents the acceptor queue from closing.
	 *
	 * XXX It is still possible for a tli application to send down data
	 * on the accepting stream while another thread calls t_accept.
	 * If the accepting fd is the same as the listening fd, avoid
	 * queue hash lookup since that will return an eager listener in a
	 * already established state.
	 */
	TCP_LOCK_WRITE();
	if (RD(q->q_next) == (queue_t *) tcr->QUEUE_ptr) {
		acceptor = listener;
		if ((listener->tcp_conn_req_cnt != 1) ||
			((listener->tcp_conn_req_cnt == 1) &&
			listener->tcp_eager_next->tcp_conn_req_seqnum !=
			tcr->SEQ_number)) {
			TCP_UNLOCK_WRITE();
			tcp_err_ack(listener->tcp_wq, mp, TBADF, 0);
			return;
		}
		ASSERT((listener->tcp_conn_req_cnt == 1) &&
			(listener->tcp_eager_next != NULL));
		listener->tcp_eager_next->tcp_conn_req_max =
			listener->tcp_conn_req_max;
	}
	else
		acceptor = tcp_queue_hash_lookup(tcr->QUEUE_ptr);
	if (!acceptor) {
		TCP_UNLOCK_WRITE();
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			  "tcp_accept: did not find queue 0x%x\n",
			  tcr->QUEUE_ptr);
		tcp_err_ack(listener->tcp_wq, mp, TBADF, 0);
		return;
	}

	if (tcr->OPT_length != 0) {	/*???*/
		TCP_UNLOCK_WRITE();
		tcp_err_ack(listener->tcp_wq, mp, TBADOPT, 0);
		return;
	}
	switch (acceptor->tcp_state) {
	case TCPS_IDLE:
	case TCPS_BOUND:
	case TCPS_LISTEN:
		if (listener->tcp_state == TCPS_LISTEN)
			break;
		/* FALLTHRU */
	default:
		TCP_UNLOCK_WRITE();
		tcp_err_ack(listener->tcp_wq, mp, TOUTSTATE, 0);
		return;
	}
	ok_mp = mi_tpi_ok_ack_alloc(mp);
	if (!ok_mp) {
		TCP_UNLOCK_WRITE();
		tcp_err_ack_prim(listener->tcp_wq, mp, T_CONN_RES,
				TSYSERR, ENOMEM);
		return;
	}

	/*
	 * Rendezvous with an eager connection request packet hanging off
	 * 'tcp' that has the 'seqnum' tag.  We tagged the detached open 
	 * tcp structure when the connection packet arrived in 
	 * tcp_conn_request().
	 */
	seqnum = tcr->SEQ_number;
	eager = listener;
	do {
		eager = eager->tcp_eager_next;
		if (!eager) {
			TCP_UNLOCK_WRITE();
			tcp_err_ack_prim(listener->tcp_wq, ok_mp, T_CONN_RES, 
				    TBADSEQ, 0);
			return;
		}
	} while (eager->tcp_conn_req_seqnum != seqnum);

	bind_mp = tcp_ip_bind_mp(eager, 12);
	if (!bind_mp) {
		TCP_UNLOCK_WRITE();
		tcp_err_ack_prim(listener->tcp_wq, ok_mp, T_CONN_RES, 
				 TSYSERR, ENOMEM);
		return;
	}

	/*
	 * We do not want to see another IRE come back in tcp_rput()
	 * since we have already adapted to this guy and we could
	 * mess up our tcp_rwnd.  We simply remove the IRE_DB_REQ here.
	 */ 
	freeb(tcp_ire_mp(bind_mp));

	tcp_eager_swap(acceptor, eager);
	tcp_conn_hash_insert(&tcp_conn_fanout[
			TCP_CONN_HASH((u8 *)&acceptor->tcp_remote,
				acceptor->tcp_tcph->th_lport)], acceptor);
	tcp_bind_hash_insert(&tcp_bind_fanout[
			TCP_BIND_HASH(acceptor->tcp_tcph->th_lport)],
				acceptor);
	if (!TCP_IS_DETACHED(listener) && !TCP_IS_DETACHED(acceptor)) {
		acceptor->tcp_rq->q_hiwat = listener->tcp_rq->q_hiwat;
		acceptor->tcp_rq->q_lowat = listener->tcp_rq->q_lowat;
	}
	/* 
	 * In case we already received a FIN we have to make tcp_rsrv send the
	 * ordrel_ind. This will also send up a window update if we have
	 * aleady overdrawn the credit.
	 * We put the T_BIND_REQ on the read side queue as an indication that
	 * this was just accepted. This tells tcp_rsrv to pass up any data 
	 * queued in tcp_rcv_head and do a tcp_mss_set.
	 */
	(void) putq(acceptor->tcp_rq, bind_mp);

	/* Possible for the acceptor queue to close after this point. */
	TCP_UNLOCK_WRITE();

	tcp_clean_death(eager, 0);

	putnext(listener->tcp_rq, ok_mp);
}

/*
 * Common accept code.  Called by tcp_conn_request.
 */
staticf void
tcp_accept_comm (listener, acceptor, cr_pkt)
	tcp_t	* listener;
	tcp_t	* acceptor;
	mblk_t	* cr_pkt;
{
	ipha_t	* ipha;
	u_long	mss;
	tcph_t	* tcph;

	/* Copy the IP+TCP header template from listener to acceptor */
	bcopy(listener->tcp_iphc, acceptor->tcp_iphc, listener->tcp_hdr_len);
	acceptor->tcp_hdr_len = listener->tcp_hdr_len;
	acceptor->tcp_ip_hdr_len = listener->tcp_ip_hdr_len;
	acceptor->tcp_tcp_hdr_len = listener->tcp_tcp_hdr_len;
	acceptor->tcp_tcph =
		(tcph_t *)&acceptor->tcp_iphc[acceptor->tcp_ip_hdr_len];
	/* Copy our new dest and fport from the connection request packet */
	ipha = (ipha_t *)ALIGN32(cr_pkt->b_rptr);
	acceptor->tcp_ipha.ipha_dst = ipha->ipha_src;
	acceptor->tcp_remote = ipha->ipha_src;
	acceptor->tcp_ipha.ipha_src = ipha->ipha_dst;
	tcph = (tcph_t *)&cr_pkt->b_rptr[IPH_HDR_LENGTH(ipha)];
	bcopy((char *)tcph->th_lport, (char *)acceptor->tcp_tcph->th_fport, 2);
	TCP_LOCK_WRITE();
	tcp_conn_hash_insert(&tcp_conn_fanout[
		      TCP_CONN_HASH((u8 *)&acceptor->tcp_remote,
				     acceptor->tcp_tcph->th_lport)], acceptor);
	tcp_bind_hash_insert(&tcp_bind_fanout[
			TCP_BIND_HASH(acceptor->tcp_tcph->th_lport)],
				acceptor);
	TCP_UNLOCK_WRITE();
	/* Source routing and other valid option copyover */
	tcp_opt_reverse(acceptor, (iph_t *)ipha);
	/* No need to check for multicast destination since ip will only pass
	 * up multicasts to those that have expressed interest
	 * TODO: what about rejecting broadcasts?
	 * Also check that source is not a multicast or broadcast address.
	 */
	acceptor->tcp_state = TCPS_LISTEN;

	/*
	 * tcp_conn_request() asked IP to append an IRE describing our
	 * peer onto the connection request packet.  Here we adapt our
	 * mss, ttl, ... according to information provided in that IRE.
	 */
	tcp_adapt(acceptor, tcp_ire_mp(cr_pkt));
	mss = tcp_mss_get(tcph);
	if (acceptor->tcp_mss > mss)
		tcp_mss_set(acceptor, mss);
}

/*
 * Adapt to the mss, rtt and src_addr of this ire mblk and then free it.
 * Assumes the ire in the mblk is 32 bit aligned.
 */
staticf void
tcp_adapt (tcp, ire_mp)
	tcp_t	* tcp;
	mblk_t	* ire_mp;
{
	ire_t	* ire = (ire_t *)ALIGN32(ire_mp->b_rptr);
	u_long	mss = tcp_mss_def;

	if (ire->ire_rtt)
		tcp->tcp_rto = ire->ire_rtt;
	if (ire->ire_max_frag)
		mss = ire->ire_max_frag - tcp->tcp_hdr_len;
	if (tcp->tcp_ipha.ipha_src == 0)
		tcp->tcp_ipha.ipha_src = ire->ire_src_addr;
	switch (ire->ire_type) {
	case IRE_LOOPBACK:
	case IRE_LOCAL:
		mi_set_sth_wroff(tcp->tcp_rq, tcp->tcp_hdr_len);
		strqset(tcp->tcp_rq, QSTRUIOT, 0, STRUIOT_STANDARD);
		strqset(tcp->tcp_wq, QSTRUIOT, 0, STRUIOT_STANDARD);
		if (tcp->tcp_co_head)
			tcp_co_drain(tcp);
		break;
	default:
		break;
	}

	freeb(ire_mp);
	tcp_mss_set(tcp, mss);
}

/*
 * tcp_bind is called (holding the writer lock) by tcp_wput_slow to process a
 * T_BIND_REQ message.
 */
staticf void
tcp_bind (q, mp)
	queue_t	* q;
reg	mblk_t	* mp;
{
reg	ipa_t	* ipa;
	mblk_t	* mp1;
	u_short	port;
reg	struct T_bind_req	* tbr;
	tcp_t	* tcp;

	tcp = (tcp_t *)q->q_ptr;
	if ((mp->b_wptr - mp->b_rptr) < sizeof(*tbr)) {
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			  "tcp_bind: bad req, len %d", mp->b_wptr - mp->b_rptr);
		tcp_err_ack(q, mp, TBADADDR, 0);
		return;
	}
	mp1 = mi_reallocb(mp, sizeof(struct T_bind_ack) + sizeof(ipa_t) + 1);
	if (!mp1) {
		tcp_err_ack(q, mp, TSYSERR, ENOMEM);
		return;
	}
	mp = mp1;
	tbr = (struct T_bind_req *)ALIGN32(mp->b_rptr);
	if ( tcp->tcp_state >= TCPS_BOUND ) {
		if (tcp->tcp_state == TCPS_BOUND &&
		    tcp->tcp_conn_req_max == 0 &&
		    tbr->CONIND_number > 0) {
			/*
			 * Handle listen() increasing CONIND_number.
			 * This is more "liberal" then what the TPI spec
			 * requires but is needed to avoid a t_unbind
			 * when handling listen() since the port number
			 * might be "stolen" between the unbind and bind.
			 */
			TCP_LOCK_WRITE();
			goto do_bind;
		}
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			  "tcp_bind: bad state, %d", tcp->tcp_state);
		tcp_err_ack(q, mp, TOUTSTATE, 0);
		return;
	}
	switch (tbr->ADDR_length) {
	case 0:			/* request for a generic port */
		tbr->ADDR_offset = sizeof(struct T_bind_req);
		tbr->ADDR_length = sizeof(ipa_t);
		ipa = (ipa_t *)&tbr[1];
		bzero((char *)ipa, sizeof(ipa_t));
		ipa->ip_family = AF_INET;
		mp->b_wptr = (u_char *)&ipa[1];
		port = 0;
		break;
	case sizeof(ipa_t):	/* Complete IP address */
		ipa = (ipa_t *)ALIGN32(mi_offset_param(mp, tbr->ADDR_offset,
					       sizeof(ipa_t)));
		if (!ipa) {
			mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			  "tcp_bind: bad address parameter, offset %d, len %d",
				  tbr->ADDR_offset, tbr->ADDR_length);
			tcp_err_ack(q, mp, TSYSERR, EINVAL);
			return;
		}
		port = BE16_TO_U16(ipa->ip_port);
		break;
	default:
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			  "tcp_bind: bad address length, %d",tbr->ADDR_length);
		tcp_err_ack(q, mp, TBADADDR, 0);
		return;
	}

	/*
	 * Acquire and hold the lock while checking for that the port is
	 * unused until the tcp_t is added to the hash lists.
	 */
	TCP_LOCK_WRITE();
	/* Initialize or re-initialize should tcp_smallest_nonpriv_port increase */
	if (tcp_next_port_to_try < tcp_smallest_nonpriv_port)
		tcp_next_port_to_try = tcp_smallest_nonpriv_port;
	if (port == 0)
		port = tcp_next_port_to_try;
	/*
	 * If the port is in the well-known privileged range,
	 * verify that the stream was opened by a privileged user.
	 */
	if (port < tcp_smallest_nonpriv_port  &&  !tcp->tcp_priv_stream) {
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			  "tcp_bind: no priv for port %d", port);
		TCP_UNLOCK_WRITE();
		tcp_err_ack(q, mp, TACCES, 0);
		return;
	}
	bcopy((char *)ipa->ip_addr, (char *)tcp->tcp_iph.iph_src, 4);
	tcp->tcp_bound_source = tcp->tcp_ipha.ipha_src;
	/*
	 * Verify that the target port/addr is available, or choose
	 * another.
	 */
	port = tcp_bindi((u_long)port, ipa->ip_addr, tcp->tcp_reuseaddr);

	/* If we are out of ports, fail the bind. */
	if (port == 0) {
		TCP_UNLOCK_WRITE();
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			"tcp_bind: out of ports?");
		tcp_err_ack(q, mp, TNOADDR, 0);
		return;
	}
do_bind:		
	mp1 = tcp_ip_bind_mp(tcp, IP_ADDR_LEN);
	if (!mp1) {
		TCP_UNLOCK_WRITE();
		tcp_err_ack(q, mp, TSYSERR, ENOMEM);
		return;
	}
	tbr->PRIM_type = T_BIND_ACK;
	mp->b_datap->db_type = M_PCPROTO;

	/* Chain in the reply mp for tcp_rput() */
	mp1->b_cont = mp;
	mp = mp1;

	if (tcp->tcp_state != TCPS_BOUND) {
		U16_TO_BE16(port, ipa->ip_port);
		U16_TO_ABE16(port, ALIGN16(tcp->tcp_tcph->th_lport));
	}

	tcp->tcp_conn_req_max = tbr->CONIND_number;
	if (tcp->tcp_conn_req_max > tcp_conn_req_max_)
		tcp->tcp_conn_req_max = tcp_conn_req_max_;
	tcp->tcp_state = tcp->tcp_conn_req_max ? TCPS_LISTEN : TCPS_BOUND;
	tcp_bind_hash_insert(&tcp_bind_fanout[
			TCP_BIND_HASH(tcp->tcp_tcph->th_lport)],
				     tcp);
	if (tcp->tcp_state == TCPS_LISTEN)
		tcp_listen_hash_insert(&tcp_listen_fanout[
				TCP_LISTEN_HASH(tcp->tcp_tcph->th_lport)],
				tcp);
	TCP_UNLOCK_WRITE();

	/*
	 * Bind processing continues in tcp_rput() where IP passes us back
	 * an M_PROTO/T_BIND_ACK followed by the reply mp we chained in above.
	 */
	putnext(q, mp);
}

/*
 * If the requested port number is available, return it.  If not, return
 * the first anonymous port we happen across.  If no anonymous ports are
 * available, return 0. addr is the requested local address, if any.
 */
staticf u_short
tcp_bindi (port_param, addr, reuseaddr)
	u_long	port_param;
	u_char	*addr;
	int reuseaddr;
{
	/* number of times we have run arround the loop*/
reg	int count = 0;
	/* maximum number of times to run arround the loop */
	int loopmax = (tcp_largest_anon_port - tcp_smallest_anon_port + 1);
	u_short	port = (u_short)port_param;
	tcp_t	* tcp;

	ASSERT(TCP_WRITE_HELD());
	do {
		u_char	a[2];

		U16_TO_BE16(port, a);
		if (reuseaddr) {
			/*
			 * Check for any tcp instance bound to this local port 
			 * and local address that is in LISTEN or BOUND state
			 */
			tcp = nilp(tcp_t);
			while (tcp = tcp_lookup_by_port(tcp, a, addr,
							TCPS_BOUND))
				if (tcp->tcp_state == TCPS_LISTEN
				|| tcp->tcp_state == TCPS_BOUND)
					break;
		} else {
			tcp = tcp_lookup_by_port(nilp(tcp_t), a, addr,
						 TCPS_BOUND);
		}
		if ( !tcp ) {
			/* this port is ours */

			/*
			 * port should never get set to a value less
			 * than tcp_smallest_nonpriv_port, so this is defensive
			 * programming.  We don't ever want to let
			 * tcp_next_port_to_try to end up less than 
			 * tcp_smallest_nonpriv_port since that would be a
			 * security hole.
			 */
			if (port < tcp_smallest_nonpriv_port)
				return port;

			/*
			 * We don't want tcp_next_port_to_try to "inherit"
			 * a port number supplied by the user in a bind.
			 */
			if ((count == 0) && (port != tcp_next_port_to_try))
				return port;
			    
			tcp_next_port_to_try = port + 1;
			if (tcp_next_port_to_try > tcp_largest_anon_port)
				tcp_next_port_to_try = tcp_smallest_anon_port;
			return port;
		}

		if ((count == 0) && (port != tcp_next_port_to_try))
			port = tcp_next_port_to_try;
		else
			port++;
			
		if ((port > tcp_largest_anon_port
		||  port < tcp_smallest_anon_port))
			port = tcp_smallest_anon_port;
		/*
		 * Don't let this loop run forever in the case where
		 * all of the anonymous ports are in use.
		 */
	} while (++count < loopmax);
	return 0;
}

/*
 * We are dieing for some reason.  Try to do it gracefully.  (May be called
 * as writer.)
 */
staticf void
tcp_clean_death (tcp, err)
reg	tcp_t	* tcp;
	int	err;
{
	mblk_t	* mp;
	queue_t	* q;

	/*
	 * Because they have no upstream client to rebind or tcp_close()
	 * them later, we axe detached state vectors here and now.
	 */
	if (TCP_IS_DETACHED(tcp)) {
		tcp_close_detached(tcp);
		return;
	}

	/*
	 * If T_ORDREL_IND has not been sent yet (done when service routine
	 * is run) postpone cleaning up the endpoint until service routine
	 * has sent up the T_ORDREL_IND.
	 */
	if (tcp->tcp_fin_rcvd && !tcp->tcp_ordrel_done) {
		tcp->tcp_client_errno = err;
		tcp->tcp_deferred_clean_death = true;
		return;
	}

	q = tcp->tcp_rq;

	/* Trash all inbound data */
	flushq(q, FLUSHALL);

	/*
	 * If we are at least part way open and there is error
	 * (err==0 implies no error)
	 * notify our client by a T_DISCON_IND.
	 */
	if ((tcp->tcp_state >= TCPS_SYN_SENT) && err) {
		if (tcp->tcp_state >= TCPS_ESTABLISHED) {
			/* Send M_FLUSH according to TPI */
			putnextctl1(q, M_FLUSH, FLUSHRW);
		}
		mi_strlog(q, 1, SL_TRACE|SL_ERROR,
			  "tcp_clean_death: discon err %d",
			  err);
		mp = mi_tpi_discon_ind(nilp(mblk_t), err, 0);
		if (mp) {
			putnext(q, mp);
		} else {
			mi_strlog(q, 1, SL_ERROR|SL_TRACE
			 , "tcp_clean_death, sending M_ERROR");
			(void)putctl1(q, M_ERROR, EPROTO);
		}
		if (tcp->tcp_state <= TCPS_SYN_RCVD) {
			/* SYN_SENT or SYN_RCVD */
			BUMP_MIB(tcp_mib.tcpAttemptFails);
		} else if (tcp->tcp_state <= TCPS_CLOSE_WAIT) {
			/* ESTABLISHED or CLOSE_WAIT */
			BUMP_MIB(tcp_mib.tcpEstabResets);
		}
	}

	/* Cancel outstanding timers */
	if (tcp->tcp_co_tintrvl != -1l) {
		tcp->tcp_co_tintrvl = -1l;
		mi_timer(tcp->tcp_wq, tcp->tcp_co_tmp, -1L);
	}
	mi_timer(tcp->tcp_wq, tcp->tcp_timer_mp, -1L);
	if (tcp->tcp_keepalive_mp)
		mi_timer(tcp->tcp_wq, tcp->tcp_keepalive_mp, -1L);

	/*
	 * Get tcp_close_mp off the write queue.
	 */
	if (tcp->tcp_flow_stopped) {
		ASSERT(tcp->tcp_flow_mp->b_prev != NULL ||
		       tcp->tcp_flow_mp->b_next != NULL ||
		       tcp->tcp_wq->q_first == tcp->tcp_flow_mp);
		rmvq(tcp->tcp_wq, tcp->tcp_flow_mp);
		tcp->tcp_flow_stopped = 0;
	}
	ASSERT(tcp->tcp_flow_mp == NULL ||
	       (tcp->tcp_flow_mp->b_prev == NULL &&
		tcp->tcp_flow_mp->b_next == NULL &&
		tcp->tcp_wq->q_first != tcp->tcp_flow_mp));

	/*
	 * Reset everything in the state vector, after updating global
	 * MIB data from instance counters.
	 */
	UPDATE_MIB(tcp_mib.tcpInSegs, tcp->tcp_ibsegs);
	UPDATE_MIB(tcp_mib.tcpOutSegs, tcp->tcp_obsegs);
	tcp_reinit(tcp);
}

/*
 * Called by streams when our client blows off her descriptor, we take
 * this to mean:
 *  "close the stream state NOW, close the tcp connection politely"
 */
staticf int
tcp_close (q)
	queue_t	* q;
{
	char	* msg;
reg	tcp_t	* tcp = (tcp_t *)q->q_ptr;

	qprocsoff(q);

	if (!tcp)
		return 0;
	/*
	 * If we might anchor some detached tcp structures, let them
	 * know their anchor is lifting.
	 */
	if (q == tcp_g_q  ||  tcp->tcp_eager_next) {
		if (q == tcp_g_q) {
			TCP_LOCK_WRITE();
			tcp_g_q = nilp(queue_t);
			TCP_UNLOCK_WRITE();
			tcp_lift_anchor(tcp);
		} else {
			tcp_eager_cleanup(tcp);
		}
	}
	MI_HRT_ACCUMULATE(tcp_g_rtime, tcp->tcp_rtime);
	MI_HRT_ACCUMULATE(tcp_g_wtime, tcp->tcp_wtime);
	msg = nilp(char);
	switch (tcp->tcp_state) {
	case TCPS_CLOSED:
	case TCPS_IDLE:
	case TCPS_BOUND:
	case TCPS_LISTEN:
		break;
	case TCPS_SYN_SENT:
		msg = "tcp_close, during connect";
		break;
	case TCPS_SYN_RCVD:
		/*
		 * Close during the connect 3-way handshake
		 * but here there may or may not be pending data
		 * already on queue. Process almost same as in
		 * the ESTABLISHED state.
		 */
		/* FALLTHRU */
	default:
		/* Abort connection if there is unread data queued. */
		if ( tcp->tcp_rcv_head
		  || tcp->tcp_reass_head
		  || tcp->tcp_co_head ) {
			msg = "tcp_close, unread data";
			break;
		}
		/* 
		 * Send an unbind to IP to make new packets (such as the final
		 * ACK) to arrive on the default TCP queue instead of ending
		 * up on our syncq (and being freed by the STREAMS framework
		 * as part of the close.
		 */
		tcp_ip_unbind(tcp->tcp_wq);
		
		/*
		 * Set detaching so that tcp_rput does not lateral_putq
		 * messages onto the closing queue. Instead it will put
		 * them on the default queue for later processing - after
		 * the qenable below. tcp_detach will set tcp_detached
		 * and clear tcp_detaching.
		 */
		tcp->tcp_detaching = true;
		/*
		 * Transmit the FIN before detacing the tcp_t.
		 * After tcp_detach returns this queue/perimeter no longer
		 * owns the tcp_t thus others can modify it.
		 */
		(void) tcp_xmit_end(tcp);

		/* Attempt to detach tcp for polite connection termination. */
		if ( tcp_detach(tcp) ) {
			/* Process any late messages */
			q = tcp_g_q;
			if (q && q->q_first)
				qenable(q);
			return 0;
		}
		/* Detach failed, abort unless we're in TIME_WAIT state */
		if (tcp->tcp_state != TCPS_TIME_WAIT) {
			msg = "tcp_close, couldn't detach";
		}
		break;
	}
	if (msg) {
		if (tcp->tcp_state == TCPS_ESTABLISHED
		    || tcp->tcp_state == TCPS_CLOSE_WAIT)
			BUMP_MIB(tcp_mib.tcpEstabResets);
		if (tcp->tcp_state == TCPS_SYN_SENT
		    || tcp->tcp_state == TCPS_SYN_RCVD)
			BUMP_MIB(tcp_mib.tcpAttemptFails);
		tcp_xmit_ctl(msg, tcp, nilp(mblk_t), tcp->tcp_snxt, 0, TH_RST);
	}
	TCP_LOCK_WRITE();
	tcp_closei(tcp);
	mi_close_comm(&tcp_g_head, q);
	tcp_param_cleanup();
	TCP_UNLOCK_WRITE();
	return 0;
}

/*
 * Clean up the b_next and b_prev fields of every mblk pointed at by *mpp.
 * Some stream heads get upset if they see these later on as anything but nil.
 */
staticf void
tcp_close_mpp (mpp)
	mblk_t	** mpp;
{
	mblk_t	* mp;

	if ((mp = *mpp) != NULL) {
		do {
			mp->b_next = nilp(mblk_t);
			mp->b_prev = nilp(mblk_t);
		} while ((mp = mp->b_cont) != NULL);
		freemsg(*mpp);
		*mpp = nilp(mblk_t);
	}
}

/*
 * Get the write lock and do the detached close.
 */
staticf void
tcp_close_detached (tcp)
	tcp_t *tcp;
{
	TCP_LOCK_WRITE();
	tcp_closei(tcp);
	mi_close_detached(&tcp_g_head, (IDP)tcp);
	tcp_param_cleanup();
	TCP_UNLOCK_WRITE();
}

/*
 * This is the true death, the swan dive into Cygnus X-1, the ...
 * Reclaim all resources associated with this state vector except
 * the 'tcp' structure itself.  (Always called holding the write lock.)
 */
staticf void
tcp_closei (tcp)
	tcp_t	* tcp;
{
	mblk_t	* mp;
	tcp_t	* listener;
	queue_t	* wq = tcp->tcp_wq;

	ASSERT(TCP_WRITE_HELD());

	if ((mp = tcp->tcp_timer_mp) != NULL) {
		mi_timer_free(mp);
		tcp->tcp_timer_mp = nilp(mblk_t);
	}
	if ((mp = tcp->tcp_co_tmp) != NULL) {
		tcp->tcp_co_tintrvl = -1l;
		mi_timer_free(mp);
		tcp->tcp_co_tmp = nilp(mblk_t);
	}
	if ((mp = tcp->tcp_co_imp) != NULL) {
		tcp->tcp_co_imp = nilp(mblk_t);
		freemsg(mp);
	}
	if ((mp = tcp->tcp_keepalive_mp) != NULL) {
		mi_timer_free(mp);
		tcp->tcp_keepalive_mp = nilp(mblk_t);
	}
	if (tcp->tcp_flow_stopped) {
		ASSERT(tcp->tcp_flow_mp->b_prev != NULL ||
		       tcp->tcp_flow_mp->b_next != NULL ||
		       tcp->tcp_wq->q_first == tcp->tcp_flow_mp);
		rmvq(wq, tcp->tcp_flow_mp);
	}
	ASSERT(tcp->tcp_flow_mp == NULL ||
	       (tcp->tcp_flow_mp->b_prev == NULL &&
		tcp->tcp_flow_mp->b_next == NULL &&
		tcp->tcp_wq->q_first != tcp->tcp_flow_mp));
	tcp_close_mpp(&tcp->tcp_flow_mp);
	tcp_close_mpp(&tcp->tcp_xmit_head);
	tcp_close_mpp(&tcp->tcp_reass_head);
	tcp->tcp_reass_tail = nilp(mblk_t);
	if ((mp = tcp->tcp_co_head) != NULL) {
		mblk_t	* mp1;
		do {
			mp1 = mp->b_next;
			mp->b_next = nilp(mblk_t);
			freemsg(mp);
		} while ((mp = mp1) != NULL);
		tcp->tcp_co_head = nilp(mblk_t);
		tcp->tcp_co_tail = nilp(mblk_t);
		tcp->tcp_co_cnt = 0;
	}
	if (tcp->tcp_rcv_head) {
#ifdef TCP_PERF
		tcp_head_free++;
#endif
		freemsg(tcp->tcp_rcv_head);
		tcp->tcp_rcv_head = nilp(mblk_t);
		tcp->tcp_rcv_cnt = 0;
	}
	tcp->tcp_state = TCPS_CLOSED;
	UPDATE_MIB(tcp_mib.tcpInSegs, tcp->tcp_ibsegs);
	tcp->tcp_ibsegs = 0;
	UPDATE_MIB(tcp_mib.tcpOutSegs, tcp->tcp_obsegs);
	tcp->tcp_obsegs = 0;
	/*
	 * If we are an eager connection hanging off
	 * a listener that hasn't formally accepted
	 * the connection yet, get off his list and
	 * blow off any data that we have accumulated.
	 */
	if ((listener = tcp->tcp_listener) != NULL) {
		tcp_t	** tcpp = &listener->tcp_eager_next;
		for ( ; tcpp[0]; tcpp = &tcpp[0]->tcp_eager_next) {
			if (tcpp[0] == tcp) {
				tcpp[0] = tcp->tcp_eager_next;
				tcp->tcp_listener = nilp(tcp_t);
				tcp->tcp_eager_next = nilp(tcp_t);
				listener->tcp_conn_req_cnt--;
				break;
			}
		}
	}
	tcp_bind_hash_remove(tcp);
	tcp_listen_hash_remove(tcp);
	tcp_conn_hash_remove(tcp);
	tcp_queue_hash_remove(tcp);
	tcp_close_mpp(&tcp->tcp_eager_conn_ind);
}

/*
 * Put a connection confirmation message upstream built from the
 * address information within 'iph' and 'tcph'.  Report our success or failure.
 */
staticf boolean_t
tcp_conn_con (tcp, iph, tcph)
	tcp_t	* tcp;
	iph_t	* iph;
	tcph_t	* tcph;
{
	ipa_t	ipa;
	mblk_t	* mp;

	bzero((char *)&ipa, sizeof(ipa_t));
	bcopy((char *)iph->iph_src, (char *)ipa.ip_addr, sizeof(ipa.ip_addr));
	bcopy((char *)tcph->th_lport, (char *)ipa.ip_port, sizeof(ipa.ip_port));
	ipa.ip_family = AF_INET;
	mp = mi_tpi_conn_con(nilp(mblk_t), (char *)&ipa, sizeof(ipa_t),
			     nilp(char), 0);
	if (!mp)
		return false;
	putnext(tcp->tcp_rq, mp);
	return true;
}

/* Process the connection request packet, mp, directed at the listener 'tcp' */
staticf void
tcp_conn_request (tcp, mp)
	tcp_t	* tcp;
	mblk_t	* mp;
{
	ipa_t	ipa;
	iph_t	* iph;
	mblk_t	* mp1;
	tcph_t	* tcph;
	mblk_t	* tpi_mp;
	tcp_t	* eager;

	if (tcp->tcp_conn_req_cnt >= tcp->tcp_conn_req_max) {
		freemsg(mp);
		return;
	}
	mp1 = tcp_ire_mp(mp);
	if (!mp1) {
		if (mp1 = allocb(sizeof(ire_t), BPRI_HI)) {
			mp1->b_datap->db_type = IRE_DB_REQ_TYPE;
			mp1->b_cont = mp;
			putnext(tcp->tcp_wq, mp1);
		} else
			freemsg(mp);
		return;
	}
	linkb(mp, mp1);
	bzero((char *)&ipa, sizeof(ipa_t));
	iph = (iph_t *)mp->b_rptr;
	tcph = (tcph_t *)&mp->b_rptr[IPH_HDR_LENGTH(iph)];
	bcopy((char *)iph->iph_src, (char *)ipa.ip_addr, sizeof(ipa.ip_addr));
	bcopy((char *)tcph->th_lport, (char *)ipa.ip_port, sizeof(ipa.ip_port));
	ipa.ip_family = AF_INET;
	tpi_mp = mi_tpi_conn_ind(nilp(mblk_t), (char *)&ipa, sizeof(ipa_t),
				 nilp(char), 0, (long)tcp->tcp_conn_req_seqnum);
	if (!tpi_mp) {
		freemsg(mp);
		return;
	}
	/*
	 * We allow the connection to proceed
	 * by generating a detached tcp state vector which will be
	 * matched up with the accepting stream when/if the accept
	 * ever happens.  The message we are passed looks like:
	 *     TPI_CONN_IND --> packet
	 */
	eager = tcp_open_detached(tcp->tcp_rq);
	if (!eager) {
		freemsg(tpi_mp);
		return;	
	}
	eager->tcp_eager_next = tcp->tcp_eager_next;
	tcp->tcp_eager_next = eager;
	eager->tcp_listener = tcp;
	/*
	 * Tag this detached tcp vector for later retrieval
	 * by our listener client in tcp_accept().
	 */
	eager->tcp_conn_req_seqnum = tcp->tcp_conn_req_seqnum;
	tcp->tcp_conn_req_cnt++;
	tcp->tcp_conn_req_seqnum++;

	tcp_accept_comm(tcp, eager, mp);
	/*
	 * Defer passing up T_CONN_IND until the 3-way handshake is complete.
	 */
	eager->tcp_eager_conn_ind = tpi_mp;

	/* OK - same queue since we have eager acceptor on listener */
	ASSERT(tcp->tcp_rq == eager->tcp_rq);
	lateral_put(tcp->tcp_rq, eager->tcp_rq, mp);
}

/*
 * Successful connect request processing begins when our client passes
 * a T_CONN_REQ message into tcp_wput_slow() and ends when tcp_rput() passes
 * our T_OK_ACK reply message upstream.  The control flow looks like this:
 *   upstream -> tcp_wput_slow() -> tcp_connect -> IP
 *   upstream <- tcp_rput()                <- IP
 * After various error checks are completed, tcp_connect() lays
 * the target address and port into the composite header template,
 * preallocates the T_OK_ACK reply message, construct a full 12 byte bind
 * request followed by an IRE request, and passes the three mblk message
 * down to IP looking like this:
 *   T_BIND_REQ for IP  --> IRE req --> T_OK_ACK for our client
 * Processing continues in tcp_rput() when we receive the following message:
 *   T_BIND_ACK from IP --> IRE ack --> T_OK_ACK for our client
 * After consuming the first two mblks, tcp_rput() calls tcp_timer(),
 * to fire off the connection request, and then passes the T_OK_ACK mblk
 * upstream that we filled in below.  There are, of course, numerous
 * error conditions along the way which truncate the processing described
 * above.
 */
staticf void
tcp_connect (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	ipa_t	* ipa;
	u_short	port;
	mblk_t	* mp1;
reg	tcp_t	* tcp = (tcp_t *)q->q_ptr;
	tcph_t	* tcph;
	struct T_conn_req	* tcr;
	tcp_t	* ltcp;

	tcr = (struct T_conn_req *)ALIGN32(mp->b_rptr);
	/*
	 * Hold the lock while checking for duplicate (identical connections
	 * until the tcp_t is added to the hash list.
	 */
	TCP_LOCK_WRITE();
	switch (tcp->tcp_state) {
	case TCPS_IDLE:
		/*
		 * We support a quick connect capability here, allowing
		 * clients to transition directly from IDLE to SYN_SENT
		 */
		port = tcp_bindi((u_long)tcp_next_port_to_try, 
				 nilp(u_char), 0);
		if (port == 0) {
			mp = mi_tpi_err_ack_alloc(mp, TNOADDR, 0);
			break;
		}
		/* FALLTHRU */
	case TCPS_BOUND:
	case TCPS_LISTEN:
		/*
		 * TODO: If someone in TCPS_TIME_WAIT has this dst/port we
		 * should key on their sequence number and cut them loose.
		 */
		if ((mp->b_wptr - mp->b_rptr) < sizeof(*tcr)) {
			mp = mi_tpi_err_ack_alloc(mp, TBADADDR, 0);
			break;
		}
		ipa = (ipa_t *)ALIGN32(mi_offset_param(mp, tcr->DEST_offset,
					       tcr->DEST_length));
		/* Check for bogus destination address */
		if (!ipa
		||  (!ipa->ip_port[0] && !ipa->ip_port[1])) {
			mp = mi_tpi_err_ack_alloc(mp, TBADADDR, 0);
			break;
		}

		/* Check for attempt to connect to INADDR_ANY */
		if (!ipa->ip_addr[0] && !ipa->ip_addr[1] && !ipa->ip_addr[2]
		    && !ipa->ip_addr[3]) {
			/*
			 * SunOS 4.x and 4.3 BSD allow an application
			 * to connect a TCP socket to INADDR_ANY.
			 * When they do this, the kernel picks the
			 * address of one interface and uses it
			 * instead.  The kernel usually ends up
			 * picking the address of the loopback
			 * interface.  This is an undocumented feature.
			 * However, we provide the same thing here
			 * in order to have source and binary
			 * compatibility with SunOS 4.x.
			 */
			u_long inaddr_loopback = INADDR_LOOPBACK;

			bcopy ((char *) &inaddr_loopback, 
			       (char *) ipa->ip_addr,
			       IP_ADDR_LEN);
		}

		/* 
		 * Don't let an endpoint connect to itself.  Note that
		 * the test here does not catch the case where the
		 * source IP addr was left unspecified by the user. In
		 * this case, the source addr is set in tcp_adapt()
		 * using the reply to the T_BIND message that we send
		 * down to IP here.
		 */
		if (tcp->tcp_state == TCPS_IDLE) {
			if (BE32_EQL(ipa->ip_addr, tcp->tcp_iph.iph_src) &&
			    (bcmp((char *) ipa->ip_port, (char *) &port,
				  sizeof(port)) == 0)) {
				mp = mi_tpi_err_ack_alloc(mp, TBADADDR, 0);
				break;
			}
		} else {
			if (BE32_EQL(ipa->ip_addr, tcp->tcp_iph.iph_src) &&
			    BE16_EQL(ipa->ip_port, tcp->tcp_tcph->th_lport)) {
				mp = mi_tpi_err_ack_alloc(mp, TBADADDR, 0);
				break;
			}
		}

		/* 
		 * Don't allow this connection to completely duplicate
		 * an existing connection.
		 */
		/*
		 * The source address might not be set yet, so
		 * tcp_lookup_match will wildcard if ipha_src is 0.
		 */
		ltcp = tcp_lookup_match(tcp->tcp_tcph->th_lport,
					tcp->tcp_iph.iph_src,
					ipa->ip_port, ipa->ip_addr,
					TCPS_SYN_SENT);
		if (ltcp) {
			/* found a duplicate connection */
			mp = mi_tpi_err_ack_alloc(mp, TBADADDR, 0);
			break;
		}

		/*
		 * Question:  What if a src was specified in bind
		 * that does not agree with our current route?  Do we
		 * 	a) Fail the connect
		 *	b) Use the address specified in bind
		 *	c) Change the addr, making it visible here
		 * We implement c) below.
		 */
		bcopy((char *)ipa->ip_addr, (char *)tcp->tcp_iph.iph_dst, 4);
		tcp->tcp_remote = tcp->tcp_ipha.ipha_dst;
		/* 
		 * Massage a source route if any putting the first hop
		 * in iph_dst. Compute a starting value for the checksum which
		 * takes into account that the original iph_dst should be 
		 * included in the checksum but that ip will include the
		 * first hop in the source route in the tcp checksum.
		 */
		tcp->tcp_sum = ip_massage_options(&tcp->tcp_ipha);
		tcp->tcp_sum = (tcp->tcp_sum & 0xFFFF) + (tcp->tcp_sum >> 16);
		tcp->tcp_sum -= ((tcp->tcp_ipha.ipha_dst >> 16) +
				 (tcp->tcp_ipha.ipha_dst & 0xffff));
		if ((int)tcp->tcp_sum < 0)
			tcp->tcp_sum--;
		tcp->tcp_sum = (tcp->tcp_sum & 0xFFFF) + (tcp->tcp_sum >> 16);
		tcp->tcp_sum = (tcp->tcp_sum & 0xFFFF) + (tcp->tcp_sum >> 16);
		tcph = tcp->tcp_tcph;
		tcph->th_fport[0] = ipa->ip_port[0];
		tcph->th_fport[1] = ipa->ip_port[1];
		if (tcp->tcp_state == TCPS_IDLE)
			U16_TO_ABE16(port, ALIGN16(tcph->th_lport));
		/*
		 * Source address might not be set yet (not until tcp_adapt
		 * is run) but the source address is not part of the
		 * hash.
		 */
		tcp_conn_hash_insert(&tcp_conn_fanout[
				TCP_CONN_HASH((u8 *)&tcp->tcp_remote,
					      tcp->tcp_tcph->th_lport)], tcp);
		tcp_bind_hash_insert(&tcp_bind_fanout[
				TCP_BIND_HASH(tcp->tcp_tcph->th_lport)], tcp);
		/*
		 * TODO: allow data with connect requests
		 * by unlinking M_DATA trailers here and
		 * linking them in behind the T_OK_ACK mblk.
		 * The tcp_rput() bind ack handler would then
		 * feed them to tcp_wput_slow() rather than call
		 * tcp_timer().
		 */
		mp = mi_tpi_ok_ack_alloc(mp);
		if (!mp)
			break;
		mp1 = tcp_ip_bind_mp(tcp, 12);
		if (mp1) {
			TCP_UNLOCK_WRITE();
			tcp->tcp_state = TCPS_SYN_SENT;
			/* Hang onto the T_OK_ACK for later. */
			linkb(mp1, mp);
			putnext(tcp->tcp_wq, mp1);
			BUMP_MIB(tcp_mib.tcpActiveOpens);
			return;
		}
		mp = mi_tpi_err_ack_alloc(mp, TSYSERR, EAGAIN);
		break;
	default:
		mp = mi_tpi_err_ack_alloc(mp, TOUTSTATE, 0);
		break;
	}
	TCP_UNLOCK_WRITE();
	if (mp
	|| (mp = mi_tpi_err_ack_alloc(mp, TSYSERR, EAGAIN)))
		putnext(tcp->tcp_rq, mp);
}

/*
 * We need a stream q for detached closing tcp connections
 * to use.  Our client hereby indicates that this q is the
 * one to use.
 */
staticf void
tcp_def_q_set (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	struct iocblk * iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
	tcp_t	* tcp = (tcp_t *)q->q_ptr;

	TCP_LOCK_WRITE();
	mp->b_datap->db_type = M_IOCACK;
	iocp->ioc_count = 0;
	if ( tcp_g_q ) {
		iocp->ioc_error = EALREADY;
		TCP_UNLOCK_WRITE();
	} else {
		mblk_t * mp1 = tcp_ip_bind_mp(tcp, 0);
		if (mp1) {
			tcp_g_q = tcp->tcp_rq;
			/*
			 * noenable the queue for handling tcp_detaching
			 * guys - explicitely qenabled when the detaching
			 * has been completed to drain any messages that
			 * have been queued.
			 */
			noenable(tcp_g_q);
			iocp->ioc_error = 0;
			iocp->ioc_rval = 0;
			TCP_UNLOCK_WRITE();
			putnext(q, mp1);
		} else {
			iocp->ioc_error = EAGAIN;
			TCP_UNLOCK_WRITE();
		}
	}
	ASSERT(!TCP_WRITE_HELD());
	qreply(q, mp);
	return;
}

/*
 * tcp_detach is called from tcp_close when a stream is closing 
 * before completion of an orderly TCP close sequence.  We detach the tcp
 * structure from its queues and it at the default queue, tcp_g_q, instead.
 * The tcp structure and resources will be reclaimed eventually by
 * a call to tcp_closei.
 *
 * Have to hold the lock until done updating the tcp structure in order
 * to prevent a thread in the tcp_g_q inner perimeter to access the
 * tcp structure.
 */
staticf boolean_t
tcp_detach (tcp)
	tcp_t	* tcp;
{
	queue_t	* wq;

	if (!tcp_g_q)
		return false;
	wq = tcp->tcp_wq;

	TCP_LOCK_WRITE();
	/* Mark the instance structure detached. */
	mi_detach(&tcp_g_head, (IDP)tcp);
	tcp->tcp_rq = tcp_g_q;
	tcp->tcp_wq = WR(tcp_g_q);
	tcp->tcp_detached = true;
	tcp->tcp_detaching = false;

	/* Transfer the timers to tcp_g_q. */
	mi_timer(WR(tcp_g_q), tcp->tcp_timer_mp, -2L);
	mi_timer(WR(tcp_g_q), tcp->tcp_co_tmp, -2L);
	if (tcp->tcp_keepalive_mp)
		mi_timer(WR(tcp_g_q), tcp->tcp_keepalive_mp, -2L);

	/*
	 * mi_timer(,,-2) moves the timer mblk to the correct queue
	 * if the timer mblk has been putq'ed.
	 * The only things that can be on the write service queue 
	 * is the flow_mp.
	 */
	if (tcp->tcp_flow_stopped) {
		ASSERT(tcp->tcp_flow_mp->b_prev != NULL ||
		       tcp->tcp_flow_mp->b_next != NULL ||
		       wq->q_first == tcp->tcp_flow_mp);
		rmvq(wq, tcp->tcp_flow_mp);
		tcp->tcp_flow_stopped = 0;
	}
	ASSERT(tcp->tcp_flow_mp == NULL ||
	       (tcp->tcp_flow_mp->b_prev == NULL &&
		tcp->tcp_flow_mp->b_next == NULL &&
		wq->q_first != tcp->tcp_flow_mp));
	/*
	 * There is no need to leave tcp_flow_mp in place (to prevent
	 * tcp_reinit from failing) since once we are detached tcp_reinit
	 * will never be called.
	 */
	tcp_close_mpp(&tcp->tcp_flow_mp);
	TCP_UNLOCK_WRITE();

	/* Set q_ptr to nil */
	wq->q_ptr = NULL;
	RD(wq)->q_ptr = NULL;
	return true;
}

/*
 * Our client hereby directs us to reject the connection request
 * that tcp_conn_request() marked with 'seqnum'.  Rejection consists
 * of sending the appropriate RST, not an ICMP error.
 */
staticf void
tcp_disconnect (q, mp)
reg	queue_t	* q;
reg	mblk_t	* mp;
{
	tcp_t	* tcp = (tcp_t *)q->q_ptr;
	long	seqnum;

	seqnum = ((struct T_discon_req *)ALIGN32(mp->b_rptr))->SEQ_number;
	/* -1L means disconnect everything */
	if (seqnum == -1L) {
		int	old_state = tcp->tcp_state;
		mi_timer(tcp->tcp_wq, tcp->tcp_timer_mp, -1L);
		if (tcp->tcp_co_tintrvl != -1l) {
			tcp->tcp_co_tintrvl = -1l;
			mi_timer(tcp->tcp_wq, tcp->tcp_co_tmp, -1L);
		}
		if (tcp->tcp_keepalive_mp)
			mi_timer(tcp->tcp_wq, tcp->tcp_keepalive_mp, -1L);
		TCP_LOCK_READ();
		if (tcp->tcp_conn_req_max
		&& !tcp_lookup_listener(tcp->tcp_tcph->th_lport,
					tcp->tcp_iph.iph_src)) {
			tcp->tcp_state = TCPS_LISTEN;
		} else if (old_state >= TCPS_BOUND) {
			tcp->tcp_conn_req_max = 0;
			tcp->tcp_state = TCPS_BOUND;
		}
		TCP_UNLOCK_READ();
		if (old_state == TCPS_SYN_SENT || old_state == TCPS_SYN_RCVD)	
			BUMP_MIB(tcp_mib.tcpAttemptFails);
		else if (old_state == TCPS_ESTABLISHED
		|| old_state == TCPS_CLOSE_WAIT)
			BUMP_MIB(tcp_mib.tcpEstabResets);
		if (tcp->tcp_eager_next)
			tcp_lift_anchor(tcp);
		tcp_xmit_ctl("tcp_disconnect", tcp, nilp(mblk_t),
			     tcp->tcp_snxt, tcp->tcp_rnxt, TH_RST | TH_ACK);
		
		tcp_reinit(tcp);
		if (tcp->tcp_state >= TCPS_ESTABLISHED) {
			/* Send M_FLUSH according to TPI */
			putnextctl1(tcp->tcp_rq, M_FLUSH, FLUSHRW);
		}
		mp = mi_tpi_ok_ack_alloc(mp);
		if (mp)
			putnext(tcp->tcp_rq, mp);
		return;
	} else if (!tcp_eager_blowoff(tcp, seqnum)) {
		tcp_err_ack(tcp->tcp_wq, mp, TBADSEQ, 0);
		return;
	}
	if (tcp->tcp_state >= TCPS_ESTABLISHED) {
		/* Send M_FLUSH according to TPI */
		putnextctl1(tcp->tcp_rq, M_FLUSH, FLUSHRW);
	}
	mp = mi_tpi_ok_ack_alloc(mp);
	if (mp)
		putnext(tcp->tcp_rq, mp);
}

/* Diagnostic routine used to return a string associated with the tcp state. */
staticf char *
tcp_display (tcp)
reg	tcp_t	* tcp;
{
	char	buf1[30];
static	char	buf[80];
reg	char	* cp;

	if (!tcp)
		return "NULL_TCP";
	switch (tcp->tcp_state) {
	case TCPS_CLOSED:
		cp = "TCP_CLOSED";
		break;
	case TCPS_IDLE:
		cp = "TCP_IDLE";
		break;
	case TCPS_BOUND:
		cp = "TCP_BOUND";
		break;
	case TCPS_LISTEN:
		cp = "TCP_LISTEN";
		break;
	case TCPS_SYN_SENT:
 		cp = "TCP_SYN_SENT";
		break;
	case TCPS_SYN_RCVD:
		cp = "TCP_SYN_RCVD";
		break;
	case TCPS_ESTABLISHED:
		cp = "TCP_ESTABLISHED";
		break;
	case TCPS_CLOSE_WAIT:
		cp = "TCP_CLOSE_WAIT";
		break;
	case TCPS_FIN_WAIT_1:
		cp = "TCP_FIN_WAIT_1";
		break;
	case TCPS_CLOSING:
		cp = "TCP_CLOSING";
		break;
	case TCPS_LAST_ACK:
		cp = "TCP_LAST_ACK";
		break;
	case TCPS_FIN_WAIT_2:
		cp = "TCP_FIN_WAIT_2";
		break;
	case TCPS_TIME_WAIT:
		cp = "TCP_TIME_WAIT";
		break;
	default:
		mi_sprintf(buf1, "TCPUnkState(%d)", tcp->tcp_state);
		cp = buf1;
		break;
	}
	mi_sprintf(buf, "[%u, %u] %s"
	 , (uint)BE16_TO_U16(tcp->tcp_tcph->th_lport)
	 , (uint)BE16_TO_U16(tcp->tcp_tcph->th_fport), cp);
	return buf;
}


/*
 * Reset any eager connection hanging off this listener marked
 * with 'seqnum' and then reclaim it's resources.
 */
staticf boolean_t
tcp_eager_blowoff (listener, seqnum)
	tcp_t	* listener;
	long	seqnum;
{
	tcp_t	* eager;

	eager = listener;
	do {
		eager = eager->tcp_eager_next;
		if (!eager)
			return false;
	} while (eager->tcp_conn_req_seqnum != seqnum);
	tcp_xmit_ctl("tcp_eager_blowoff, can't wait",
		      eager, nilp(mblk_t), eager->tcp_snxt, 0, TH_RST);
	tcp_close_detached(eager);
	return true;
}

/*
 * Reset any eager connection hanging off this listener
 * and then reclaim it's resources.
 */
staticf void
tcp_eager_cleanup (listener)
	tcp_t	* listener;
{
	tcp_t	* eager;

	while ((eager = listener->tcp_eager_next) != NULL) {
		tcp_xmit_ctl("tcp_eager_cleanup, can't wait",
			     eager, nilp(mblk_t), eager->tcp_snxt, 0, TH_RST);
		tcp_close_detached(eager);
	}
}

/* Swap tcp state but leave most external attachments in place */
staticf	void
tcp_eager_swap (acceptor, eager)
	tcp_t	* acceptor;
	tcp_t	* eager;
{
	tcp_t	orig_eager;
	tcp_t	orig_acceptor;

	orig_eager = *eager;
	orig_acceptor = *acceptor;
	*eager = *acceptor;
	*acceptor = orig_eager;
	tcp_eager_swap_fixup(eager, &orig_eager);
	tcp_eager_swap_fixup(acceptor, &orig_acceptor);
	/*
	 * mi_timer(,,-2) moves the timer mblk to the correct queue
	 * if the timer mblk has been putq'ed.
	 */
}

/* Restore most of the external attachments */
staticf void
tcp_eager_swap_fixup (newt, oldt)
	tcp_t	* newt;
	tcp_t	* oldt;
{
	tcpt_t	* tcpt;

	newt->tcp_rq = oldt->tcp_rq;
	newt->tcp_wq = oldt->tcp_wq;
	newt->tcp_bind_hash = oldt->tcp_bind_hash;
	newt->tcp_ptpbhn = oldt->tcp_ptpbhn;
	newt->tcp_listen_hash = oldt->tcp_listen_hash;
	newt->tcp_ptplhn = oldt->tcp_ptplhn;
	newt->tcp_conn_hash = oldt->tcp_conn_hash;
	newt->tcp_ptpchn = oldt->tcp_ptpchn;
	newt->tcp_queue_hash = oldt->tcp_queue_hash;
	newt->tcp_ptpqhn = oldt->tcp_ptpqhn;
	newt->tcp_flow_mp = oldt->tcp_flow_mp;
	newt->tcp_detached = oldt->tcp_detached;
	newt->tcp_listener = oldt->tcp_listener;
	newt->tcp_eager_next = oldt->tcp_eager_next;
	newt->tcp_tcph = (tcph_t *)&newt->tcp_iphc[newt->tcp_ip_hdr_len];

	if (newt->tcp_timer_mp) {
		tcpt = (tcpt_t *)ALIGN32(newt->tcp_timer_mp->b_rptr);
		tcpt->tcpt_tcp = newt;
		mi_timer(newt->tcp_wq, newt->tcp_timer_mp, -2);
	}
	if (newt->tcp_co_tmp) {
		tcpt = (tcpt_t *)ALIGN32(newt->tcp_co_tmp->b_rptr);
		tcpt->tcpt_tcp = newt;
		mi_timer(newt->tcp_wq, newt->tcp_co_tmp, -2);
	}

	if (newt->tcp_keepalive_mp) {
		tcpt = (tcpt_t *)ALIGN32(newt->tcp_keepalive_mp->b_rptr);
		tcpt->tcpt_tcp = newt;
		mi_timer(newt->tcp_wq, newt->tcp_keepalive_mp, -2);
	}
}

/* Shorthand to generate and send TPI error acks to our client */
staticf void
tcp_err_ack (q, mp, t_error, sys_error)
	queue_t	* q;
	mblk_t	* mp;
	int	t_error;
	int	sys_error;
{
	if ((mp = mi_tpi_err_ack_alloc(mp, t_error, sys_error)) != NULL)
		qreply(q, mp);
}

/* Shorthand to generate and send TPI error acks to our client */
staticf void
tcp_err_ack_prim (q, mp, primitive, t_error, sys_error)
	queue_t	* q;
	mblk_t	* mp;
	int	primitive;
	int	t_error;
	int	sys_error;
{
	struct T_error_ack	* teackp;

	if ((mp = mi_tpi_ack_alloc(mp, sizeof(struct T_error_ack),
					T_ERROR_ACK)) != NULL) {
		teackp = (struct T_error_ack *)ALIGN32(mp->b_rptr);
		teackp->ERROR_prim = primitive;
		teackp->TLI_error = t_error;
		teackp->UNIX_error = sys_error;
		qreply(q, mp);
	}
}

/* Return the TPI/TLI equivalent of our current tcp_state */
staticf int
tcp_getstate (tcp)
	tcp_t	* tcp;
{
	switch (tcp->tcp_state) {
	case TCPS_IDLE:
		return TS_UNBND;
	case TCPS_BOUND:
	case TCPS_LISTEN:
		return TS_IDLE;
	case TCPS_SYN_SENT:
		return TS_WACK_CREQ;
	case TCPS_SYN_RCVD:
		return TS_WACK_CRES;
	case TCPS_ESTABLISHED:
		return TS_DATA_XFER;
	case TCPS_CLOSE_WAIT:
		return TS_WREQ_ORDREL;
	case TCPS_FIN_WAIT_1:
		return TS_WIND_ORDREL;
	case TCPS_CLOSING:
		return TS_WACK_DREQ7;
	case TCPS_FIN_WAIT_2:
		return TS_WACK_DREQ9;
	case TCPS_TIME_WAIT:
		return TS_WACK_DREQ10;
	case TCPS_LAST_ACK:
		return TS_WACK_DREQ11;
	default:
		cmn_err(CE_WARN, "tcp_getstate: strange state (%d) %s\n",
			tcp->tcp_state, tcp_display(tcp));
		return TS_UNBND;
	}
}

/* Respond to the TPI info request */
staticf void
tcp_info_req (tcp, mp)
	tcp_t	* tcp;
reg	mblk_t	* mp;
{
	struct T_info_ack * tia;

	mp = mi_tpi_ack_alloc(mp, sizeof(tcp_g_t_info_ack), T_INFO_ACK);
	if (!mp) {
		tcp_err_ack(tcp->tcp_wq, mp, TSYSERR, ENOMEM);
		return;
	}
	bcopy((char *)&tcp_g_t_info_ack, (char *)mp->b_rptr,
	      sizeof(tcp_g_t_info_ack));
	tia = (struct T_info_ack *)ALIGN32(mp->b_rptr);
	tia->CURRENT_state = tcp_getstate(tcp);
	if (tcp->tcp_mss == 0) /* Not yet set - tcp_open does not set mss */
		tia->TIDU_size = tcp_mss_def;
	else
		tia->TIDU_size = tcp->tcp_mss;
	/* TODO: Default ETSDU is 1.  Is that correct for tcp? */
	putnext(tcp->tcp_rq, mp);
}

/*
 * Handle reinitialization of a tcp structure. Allocate resources that
 * do not yet exist. Call tcp_init to initialize the state vector.
 * Maintain "binding state" resetting the state to BOUND, LISTEN, or IDLE.
 * (Have to acquire the write lock since it is temporarely setting tcp_tcph 
 * to NULL.)
 * tcp_reinit is guaranteed not to fail if tcp_flow_mp and tcp_timer_mp
 * and tcp_co_tmp and tcp_co_imp already exist.
 */
staticf void
tcp_reinit (tcp)
reg	tcp_t	* tcp;
{
	mblk_t	* flow_mp = tcp->tcp_flow_mp;
	mblk_t	* timer_mp = tcp->tcp_timer_mp;
	mblk_t	* co_tmp = tcp->tcp_co_tmp;
	mblk_t	* co_imp = tcp->tcp_co_imp;
	queue_t	* q = tcp->tcp_rq;
static	tcp_t	tcp_zero;
	int	ret;
	/* For saving state */
	int	state;
	ipa_t	ipas;
	ipa_t	* ipa = &ipas;
	ulong	conn_req_max;

	/* tcp_reinit should never be called for detached tcp_t's */
	ASSERT(!TCP_IS_DETACHED(tcp));

	/* 
	 * Save state associated with TCPS_BOUND or _LISTEN endpoints.
	 * tcp_bind stored source IP address in tcp_bound_source for this
	 * sole purpose.
	 */
	state = tcp->tcp_state;
	if (state > TCPS_IDLE) {
		conn_req_max = tcp->tcp_conn_req_max;
		bcopy((char *)tcp->tcp_tcph->th_lport,
		      (char *)ipa->ip_port, 2);
		bcopy((char *)&tcp->tcp_bound_source,
		      (char *)ipa->ip_addr, IP_ADDR_LEN);
	}

	if (!flow_mp)
		flow_mp = allocb(tcp_winfo.mi_hiwat + 1, BPRI_LO);
	if (!timer_mp)
		timer_mp = tcp_timer_alloc(tcp, tcp_timer, 0);
	if (!co_tmp)
		co_tmp = tcp_timer_alloc(tcp, tcp_co_timer, 0);
	if (!co_imp)
		co_imp = allocb(sizeof(struct iocblk), BPRI_HI);

	tcp_close_mpp(&tcp->tcp_xmit_head);
	tcp_close_mpp(&tcp->tcp_reass_head);
	tcp->tcp_reass_tail = nilp(mblk_t);
	if (tcp->tcp_co_head) {
		mblk_t	* mp = tcp->tcp_co_head;
		mblk_t	* mp1;
		do {
			mp1 = mp->b_next;
			mp->b_next = nilp(mblk_t);
			freemsg(mp);
		} while ((mp = mp1) != NULL);
		tcp->tcp_co_head = nilp(mblk_t);
		tcp->tcp_co_tail = nilp(mblk_t);
		tcp->tcp_co_cnt = 0;
	}
	if (tcp->tcp_rcv_head) {
#ifdef TCP_PERF
		tcp_head_free++;
#endif
		freemsg(tcp->tcp_rcv_head);
		tcp->tcp_rcv_head = nilp(mblk_t);
		tcp->tcp_rcv_cnt = 0;
	}

	if (tcp->tcp_keepalive_mp) {
		mi_timer_free(tcp->tcp_keepalive_mp);
		tcp->tcp_keepalive_mp = nilp(mblk_t);
	}

	/*
	 * Prevent anybody from tripping over the NULL tcp_tcph by
	 * holding the write lock.
	 */
	TCP_LOCK_WRITE();
	tcp_bind_hash_remove(tcp);
	tcp_listen_hash_remove(tcp);
	tcp_conn_hash_remove(tcp);
	tcp_queue_hash_remove(tcp);
	*tcp = tcp_zero;
	ret = tcp_init(tcp, q, timer_mp, flow_mp, co_tmp, co_imp);
	ASSERT(ret == 0);

	/* Restore state */
	if (state > TCPS_IDLE) {
		tcp->tcp_conn_req_max = conn_req_max;
		bcopy((char *)ipa->ip_port,
		      (char *)tcp->tcp_tcph->th_lport, 2);
		bcopy((char *)ipa->ip_addr, 
		      (char *)tcp->tcp_iph.iph_src, IP_ADDR_LEN);
		tcp->tcp_bound_source = tcp->tcp_ipha.ipha_src;
		tcp->tcp_state = tcp->tcp_conn_req_max ?
			TCPS_LISTEN : TCPS_BOUND;
		tcp_bind_hash_insert(&tcp_bind_fanout[
				TCP_BIND_HASH(tcp->tcp_tcph->th_lport)],
					tcp);
		if (tcp->tcp_state == TCPS_LISTEN)
			tcp_listen_hash_insert(&tcp_listen_fanout[
				TCP_LISTEN_HASH(tcp->tcp_tcph->th_lport)],
				tcp);
	}
	TCP_UNLOCK_WRITE();
	mi_set_sth_wroff(tcp->tcp_rq, tcp->tcp_hdr_len + tcp_wroff_xtra);
	tcp_mss_set(tcp, tcp_mss_def);
}

/*
 * Allocate necessary resources and initialize state vector.
 * Guaranteed not to fail if timer_mp and flow_mp and co_tmp
 * and co_imp are non-null.
 */
staticf int
tcp_init (tcp, q, timer_mp, flow_mp, co_tmp, co_imp)
	tcp_t	* tcp;
	queue_t	* q;
	mblk_t	* timer_mp;
	mblk_t	* flow_mp;
	mblk_t	* co_tmp;
	mblk_t	* co_imp;
{
	tcph_t	* tcph;
	u32	sum;
	hrtime_t hrt;

	ASSERT(TCP_WRITE_HELD());

	if (!timer_mp  ||  !flow_mp  ||  !co_tmp || !co_imp) {
		if (timer_mp)
			mi_timer_free(timer_mp);
		if (flow_mp)
			freemsg(flow_mp);
		if (co_tmp)
			mi_timer_free(co_tmp);
		if (co_imp)
			freemsg(co_imp);
		return ENOMEM;
	}
	/* Compatibility request for pre-RFC1122 urgent pointers */
	tcp->tcp_urp_old = tcp_old_urp_interpretation;
	tcp->tcp_co_tintrvl = -1l;
	tcp->tcp_co_tmp = co_tmp;
	tcp->tcp_co_imp = co_imp;
	tcp->tcp_co_norm = 1;
	tcp->tcp_timer_mp = timer_mp;
	tcp->tcp_flow_mp = flow_mp;
	flow_mp->b_wptr = flow_mp->b_datap->db_lim;

	tcp->tcp_rto = tcp_rexmit_interval_initial;
	tcp->tcp_cwnd_max = tcp_cwnd_max_;
	/* Sample the rtt of the first ACK */
	tcp->tcp_rtt_mt = 0;
	tcp->tcp_rtt_nt = lbolt;

	tcp->tcp_rq = q;
	tcp->tcp_wq = WR(q);
	tcp->tcp_first_timer_threshold = tcp_ip_notify_interval;
	tcp->tcp_first_ctimer_threshold = tcp_ip_notify_cinterval;
	tcp->tcp_second_timer_threshold = tcp_ip_abort_interval;
	tcp->tcp_second_ctimer_threshold = tcp_ip_abort_cinterval;
	tcp->tcp_keepalive_intrvl = tcp_keepalive_interval;

	tcp->tcp_naglim = tcp_naglim_def;
	tcp->tcp_state = TCPS_IDLE;
	if (tcp_strong_iss) {
		tcp_iss_incr_extra += tcp_random();
		hrt = gethrtime();
		tcp->tcp_iss = hrt/ISS_NSEC_DIV + tcp_iss_incr_extra;
	} else {
		tcp_iss_incr_extra += ISS_INCR/2;
		tcp->tcp_iss = (u_long)time_in_secs * ISS_INCR + 
			tcp_iss_incr_extra;
	}
	tcp->tcp_valid_bits = TCP_ISS_VALID;
	tcp->tcp_fss = tcp->tcp_iss - 1;
	tcp->tcp_suna = tcp->tcp_iss;
	tcp->tcp_snxt = tcp->tcp_iss + 1;
	tcp->tcp_csuna = tcp->tcp_snxt;
	tcp->tcp_hdr_len = sizeof(iph_t) + sizeof(tcph_t);
	tcp->tcp_tcp_hdr_len = sizeof(tcph_t);
	tcp->tcp_ip_hdr_len = sizeof(iph_t);

	tcp->tcp_xmit_lowater = tcp_xmit_lowat;
	tcp->tcp_xmit_hiwater = tcp_xmit_hiwat;

	/* Initialize the header template */
	U16_TO_BE16(sizeof(iph_t) + sizeof(tcph_t), tcp->tcp_iph.iph_length);
	tcp->tcp_iph.iph_version_and_hdr_length 
		= (IP_VERSION << 4) | IP_SIMPLE_HDR_LENGTH_IN_WORDS;
	tcp->tcp_iph.iph_ttl = tcp_ip_ttl;
	tcp->tcp_iph.iph_protocol = IPPROTO_TCP;

	tcph = (tcph_t *)&tcp->tcp_iphc[sizeof(iph_t)];
	tcp->tcp_tcph = tcph;
	tcph->th_offset_and_rsrvd[0] = (5 << 4);
	/*
	 * IP wants our header length in the checksum field to
	 * allow it to perform a single psuedo-header+checksum
	 * calculation on behalf of TCP.
	 * Include the adjustment for a source route if any.
	 */
	sum = sizeof(tcph_t) + tcp->tcp_sum;
	sum = (sum >> 16) + (sum & 0xFFFF);
	U16_TO_ABE16(sum, ALIGN16(tcph->th_sum));
	tcp_queue_hash_insert(&tcp_queue_fanout[
				TCP_QUEUE_HASH(backq(tcp->tcp_rq))], tcp);
	return 0;
}

/*
 * tcp_icmp_error is called by tcp_rput_other to process ICMP
 * messages passed up by IP.
 * We need to find a tcp_t that corresponds to the returned datagram.
 * Passes the message back in on the correct queue once it has located
 * the connection.
 */
staticf void
tcp_icmp_error (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	icmph_t * icmph;
	iph_t	* iph;
	int	iph_hdr_length;
	tcp_t	* tcp;
	tcph_t	* tcph;
	
	iph = (iph_t *)mp->b_rptr;
	iph_hdr_length = IPH_HDR_LENGTH(iph);
	icmph = (icmph_t *)ALIGN32(&mp->b_rptr[iph_hdr_length]);
	iph = (iph_t *)&icmph[1];
	iph_hdr_length = IPH_HDR_LENGTH(iph);
	tcph = (tcph_t *)((char *)iph + iph_hdr_length);
	TCP_LOCK_READ();
	tcp = tcp_lookup_reversed(iph, tcph, TCPS_LISTEN);
	if ( !tcp ) {
		TCP_UNLOCK_READ();
		freemsg(mp);
		return;
	}
	if (tcp->tcp_rq != q) {
		lateral_putq(q, tcp->tcp_rq, mp);
		TCP_UNLOCK_READ();
		return;
	}
	/* We are on the correct queue */
	TCP_UNLOCK_READ();
	switch (icmph->icmph_type ) {
	case ICMP_DEST_UNREACHABLE:
		switch ( icmph->icmph_code ) {
		case ICMP_FRAGMENTATION_NEEDED:
			/*
			 * Reduce the MSS based on the new MTU.  This will 
			 * eliminate any fragmentation locally.
			 * N.B.  There may well be some funny side-effects on 
			 * the local send policy and the remote receive policy.
			 * Pending further research, we provide 
			 * tcp_ignore_path_mtu just in case this proves 
			 * disasterous somewhere.
			 */
			if ( !tcp_ignore_path_mtu )
				tcp_mss_set(tcp,
				    icmph->icmph_du_mtu - tcp->tcp_hdr_len);
			break;
		case ICMP_PORT_UNREACHABLE:
		case ICMP_PROTOCOL_UNREACHABLE:
			tcp_clean_death(tcp, ECONNREFUSED);
			break;
		case ICMP_HOST_UNREACHABLE:
			/* Record the error in case we finally time out. */
			tcp->tcp_client_errno = EHOSTUNREACH;
			break;
		case ICMP_NET_UNREACHABLE:
			/* Record the error in case we finally time out. */
			tcp->tcp_client_errno = ENETUNREACH;
			break;
		default:
			break;
		}
		break;
	case ICMP_SOURCE_QUENCH:
		/* Reduce the sending rate as if we got a retransmit timeout */
		tcp->tcp_cwnd_ssthresh = tcp->tcp_cwnd >> 1;
		tcp->tcp_cwnd = tcp->tcp_mss;
		break;
	}
	freemsg(mp);
}

/*
 * There are three types of binds that IP recognizes.  If we send
 * down a 0 length address, IP will send us packets for which it
 * has no more specific target than "some TCP port".  If we send
 * down a 4 byte address, IP will verify that the address given
 * is a valid local address.  If we send down a full 12 byte address,
 * IP validates both addresses, and then begins to send us only those
 * packets that match completely.  IP will also fill in the IRE
 * request mblk with information regarding our peer.  In all three
 * cases, we notify IP of our protocol type by appending a single
 * protocol byte to the bind request.
 */
staticf mblk_t *
tcp_ip_bind_mp (tcp, addr_length)
	tcp_t	* tcp;
	int	addr_length;
{
	char	* cp;
	mblk_t	* mp;
	struct T_bind_req	* tbr;

	mp = allocb(sizeof(*tbr) + addr_length + 1, BPRI_HI);
	if (!mp)
		return mp;
	mp->b_datap->db_type = M_PROTO;
	tbr = (struct T_bind_req *)ALIGN32(mp->b_rptr);
	tbr->PRIM_type = T_BIND_REQ;
	tbr->ADDR_offset = sizeof(*tbr);
	tbr->CONIND_number = 0;
	tbr->ADDR_length = addr_length;
	cp = (char *)&tbr[1];
	switch (addr_length) {
	case 12:
		/* Append a request for an IRE */
		mp->b_cont = allocb(sizeof(ire_t), BPRI_HI);
		if (!mp->b_cont) {
			freemsg(mp);
			return nilp(mblk_t);
		}
		mp->b_cont->b_datap->db_type = IRE_DB_REQ_TYPE;

		/* cp known to be 32 bit aligned */
		*(u32 *)ALIGN32(cp) = tcp->tcp_ipha.ipha_src;
		*(u32 *)ALIGN32(cp + 4) = tcp->tcp_remote;
		*(u16 *)ALIGN16(cp + 8) =
			*(u16 *)ALIGN16(tcp->tcp_tcph->th_fport);
		*(u16 *)ALIGN16(cp + 10) =
			*(u16 *)ALIGN16(tcp->tcp_tcph->th_lport);
		tcp->tcp_hard_binding = 1;
		/* FALLTHRU */
	case IP_ADDR_LEN:
		*(u32 *)ALIGN32(cp) = tcp->tcp_ipha.ipha_src;
		break;
	}
	cp[addr_length] = (char)IPPROTO_TCP;
	mp->b_wptr = (u_char *)&cp[addr_length + 1];
	return mp;
}

/*
 * Notify IP that we are having trouble with this connection.  IP should
 * blow the IRE away and start over.
 */
staticf void
tcp_ip_notify (tcp)
	tcp_t	* tcp;
{
	struct iocblk	* iocp;
	iph_t	* iph;
	ipid_t	* ipid;
	mblk_t	* mp;

	mp = allocb(sizeof(struct iocblk), BPRI_HI);
	if (!mp)
		return;

	mp->b_datap->db_type = M_IOCTL;
	iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
	mp->b_wptr = (u_char *)&iocp[1];
	bzero((char *)iocp, sizeof(*iocp));
	iocp->ioc_cmd = IP_IOCTL;
	iocp->ioc_count = sizeof(ipid_t) + sizeof(iph->iph_dst);

	mp->b_cont = allocb(iocp->ioc_count, BPRI_HI);
	if (!mp->b_cont) {
		freeb(mp);
		return;
	}

	ipid = (ipid_t *)ALIGN32(mp->b_cont->b_rptr);
	mp->b_cont->b_wptr += iocp->ioc_count;
	bzero((char *)ipid, sizeof(*ipid));
	ipid->ipid_cmd = IP_IOC_IRE_DELETE_NO_REPLY;
	ipid->ipid_ire_type = IRE_ROUTE;
	ipid->ipid_addr_offset = sizeof(ipid_t);
	ipid->ipid_addr_length = sizeof(tcp->tcp_iph.iph_dst);
	iph = &tcp->tcp_iph;
	/* Note: in the case of source routing we want to blow away the
	 * route to the first source route hop.
	 */
	bcopy((char *)&iph->iph_dst, (char *)&ipid[1], sizeof(iph->iph_dst));

	putnext(tcp->tcp_wq, mp);
}

/*
 * Unbind this stream with IP
 */
staticf void
tcp_ip_unbind (wq)
	queue_t	*wq;
{
	mblk_t	* mp;
	struct T_unbind_req	* tur;

	mp = allocb(sizeof(*tur), BPRI_HI);
	if (!mp)
		return;
	mp->b_datap->db_type = M_PROTO;
	tur = (struct T_unbind_req *)ALIGN32(mp->b_rptr);
	tur->PRIM_type = T_UNBIND_REQ;
	mp->b_wptr += sizeof(*tur);
	putnext(wq, mp);
}

/* Unlink and return any mblk that looks like it contains an ire */
staticf mblk_t *
tcp_ire_mp (mp)
	mblk_t	* mp;
{
	mblk_t	* prev_mp;

	for (;;) {
		prev_mp = mp;
		mp = mp->b_cont;
		if (!mp)
			break;
		switch (mp->b_datap->db_type) {
		case IRE_DB_TYPE:
		case IRE_DB_REQ_TYPE:
			if (prev_mp)
				prev_mp->b_cont = mp->b_cont;
			mp->b_cont = nilp(mblk_t);
			return mp;
		default:
			break;
		}
	}
	return mp;
}

/*
 * Timer callback routine for keepalive probe.  This flavor of keepalive
 * generates a resend of the last sent byte.  This byte is retransmitted
 * until death as if it was new data.
 */
staticf void
tcp_keepalive_killer (tcp)
	tcp_t	* tcp;
{
	mblk_t	* mp;
	tcpka_t	* tcpka;

	if ( !tcp->tcp_keepalive_mp )
		return;
	BUMP_MIB(tcp_mib.tcpTimKeepalive);
	tcpka = (tcpka_t *)ALIGN32(tcp->tcp_keepalive_mp->b_rptr);
	if ( tcp->tcp_state >= TCPS_ESTABLISHED
	&&  tcp->tcp_state < TCPS_TIME_WAIT
	&&  tcpka->tcpka_rnxt == tcp->tcp_rnxt
	&&  tcpka->tcpka_snxt == tcp->tcp_snxt
	&&  tcp->tcp_snxt == tcp->tcp_suna ) {
		if ( tcp->tcp_fin_sent ) {
			tcp->tcp_fin_sent = false;
			mp = tcp_xmit_mp(tcp, nilp(mblk_t), 0,
					 tcp->tcp_fss, 0);
			if ( mp ) {
				tcp->tcp_fin_acked = false;
				tcpka->tcpka_seq = tcp->tcp_fss;
				putnext(tcp->tcp_wq, mp);
				BUMP_MIB(tcp_mib.tcpTimKeepaliveProbe);
			} else
				tcp->tcp_fin_sent = true;
		} else {
			/* Fake resend of last ACKed byte. */
			mblk_t	* mp1 = allocb(1, BPRI_LO);
			if ( mp1 ) {
			  mp = (MBLKP) mi_tpi_trailer_alloc(mp1, 
						    sizeof(struct T_data_req),
						    T_DATA_REQ);
				if ( mp ) {
					*mp1->b_wptr++ = '\0';
					tcp->tcp_snxt--;
					tcp->tcp_suna--;
					tcpka->tcpka_seq = tcp->tcp_snxt;
					tcp_wput_slow(tcp, mp);
					BUMP_MIB(tcp_mib.tcpTimKeepaliveProbe);
				} else
					freeb(mp1);
			}
		}
	} else {
		tcpka->tcpka_rnxt = tcp->tcp_rnxt;
		tcpka->tcpka_snxt = tcp->tcp_snxt;
	}
	mi_timer(tcp->tcp_wq, tcp->tcp_keepalive_mp,tcp->tcp_keepalive_intrvl);
}

/*
 * Walk the list of instantiations and blow off every detached
 * tcp depending on the anchor passed in.
 * Only used when tcp_g_q closes.
 */
staticf void
tcp_lift_anchor (tcp)
	tcp_t	* tcp;
{
	tcp_t	* tcp1, * tcp2;

	TCP_LOCK_READ();
restart:
	for (tcp2 = (tcp_t *)ALIGN32(mi_first_ptr(&tcp_g_head));
	     (tcp1 = tcp2) != NULL; ) {
		tcp2 = (tcp_t *)ALIGN32(mi_next_ptr(&tcp_g_head, (IDP)tcp2));
		if (!TCP_IS_DETACHED(tcp1))
			continue;
		if (tcp1->tcp_wq != tcp->tcp_wq
		&&  tcp1->tcp_rq != tcp->tcp_rq)
			continue;
		/*
		 * We can drop the lock without tcp1 going away since
		 * it is associated with the same queue as tcp.
		 * However, we have to start the scan all over since
		 * tcp2 might no longer be valid.
		 */
		TCP_UNLOCK_READ();
		if (tcp1->tcp_state != TCPS_TIME_WAIT) {
			tcp_xmit_ctl("tcp_lift_anchor, can't wait",
				     tcp1, nilp(mblk_t), tcp1->tcp_snxt,
				     0, TH_RST);
		}
		tcp_close_detached(tcp1);
		TCP_LOCK_READ();
		goto restart;
	}
	TCP_UNLOCK_READ();
}

/* Find an exact src/dst/lport/fport match for an incoming datagram. */
staticf tcp_t *
tcp_lookup (iph, tcph, min_state)
	iph_t	* iph;
	tcph_t	* tcph;
	int	min_state;
{
	tcp_t	* tcp;
	tcph_t	* tcph1;
	iph_t	* iph1;
	u_short	ports[2];

	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	ports[0] = BE16_TO_U16(tcph->th_fport);
	ports[1] = BE16_TO_U16(tcph->th_lport);
		
	tcp = tcp_conn_fanout[TCP_CONN_HASH(iph->iph_src, (u8 *)ports)];
	for (; tcp != nilp(tcp_t); tcp = tcp->tcp_conn_hash) {
		tcph1 = tcp->tcp_tcph;
		iph1 = &tcp->tcp_iph;
		if (BE16_EQL(tcph1->th_fport, tcph->th_lport) &&
		    BE16_EQL(tcph1->th_lport, tcph->th_fport) &&
		    BE32_EQL(iph1->iph_src, iph->iph_dst) &&
		    BE32_EQL(&tcp->tcp_remote, iph->iph_src) &&
		    tcp->tcp_state >= min_state)
			break;
	}
	return tcp;
}

staticf tcp_t *
tcp_lookup_match (lport, laddr, fport, faddr, min_state)
	u8	*lport, *laddr, *fport, *faddr;
	int	min_state;
{
	tcp_t	* tcp;
	tcph_t	* tcph1;
	u_short	ports[2];
	u32	src = 0;

	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	if ( laddr ) {
		/* we want the address as is, not swapped */
		UA32_TO_U32(laddr, src);
	}

	ports[0] = BE16_TO_U16(lport);
	ports[1] = BE16_TO_U16(fport);
		
	tcp = tcp_conn_fanout[TCP_CONN_HASH(faddr, (u8 *)ports)];
	for (; tcp != nilp(tcp_t); tcp = tcp->tcp_conn_hash) {
		tcph1 = tcp->tcp_tcph;
		if (BE16_EQL(tcph1->th_fport, fport) &&
		    BE16_EQL(tcph1->th_lport, lport) &&
		    (src == 0 || tcp->tcp_ipha.ipha_src == 0 ||
			src == tcp->tcp_ipha.ipha_src) &&
		    BE32_EQL(&tcp->tcp_remote, faddr) &&
		    tcp->tcp_state >= min_state)
			break;
	}
	return tcp;
}

/* Find an exact src/dst/lport/fport match for a bounced datagram. */
staticf tcp_t *
tcp_lookup_reversed (iph, tcph, min_state)
	iph_t	* iph;
	tcph_t	* tcph;
	int	min_state;
{
	tcp_t	* tcp;
	tcph_t	* tcph1;
	iph_t	* iph1;

	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	tcp = tcp_conn_fanout[TCP_CONN_HASH(iph->iph_dst, tcph->th_lport)];
	for (; tcp != nilp(tcp_t); tcp = tcp->tcp_conn_hash) {
		tcph1 = tcp->tcp_tcph;
		iph1 = &tcp->tcp_iph;
		if (BE16_EQL(tcph1->th_fport, tcph->th_fport) &&
		    BE16_EQL(tcph1->th_lport, tcph->th_lport) &&
		    BE32_EQL(iph1->iph_src, iph->iph_src) &&
		    BE32_EQL(&tcp->tcp_remote, iph->iph_dst) &&
		    tcp->tcp_state >= min_state)
			break;
	}
	return tcp;
}



/*
 * Return the next active tcp instance bound to a particular lport, following
 * the tcp passed in.  Pass in a nil pointer to get started.
 * Give preference to exact matches on the IP address.
 */
staticf tcp_t *
tcp_lookup_by_port (tcp, lport, laddr, min_state)
	tcp_t	* tcp;
	u_char	* lport;
	u_char	* laddr;
	int	min_state;
{
	u32	src = 0;

	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	if ( laddr ) {
		/* we want the address as is, not swapped */
		UA32_TO_U32(laddr, src);
	}

	if (tcp == NULL)
		tcp = tcp_bind_fanout[TCP_BIND_HASH(lport)];
	else
		tcp = tcp->tcp_bind_hash;
	for (; tcp != nilp(tcp_t); tcp = tcp->tcp_bind_hash) {
		if (BE16_EQL(lport, tcp->tcp_tcph->th_lport) &&
		    (src == INADDR_ANY ||
		     tcp->tcp_ipha.ipha_src == INADDR_ANY ||
		     tcp->tcp_ipha.ipha_src == src) &&
		    tcp->tcp_state >= min_state)
			break;
	}
	return tcp;
}

/*
 * Find a tcp listening on the specified port.
 * Give preference to exact matches on the IP address.
 */
staticf tcp_t *
tcp_lookup_listener (lport, laddr)
	u_char	* lport;
	u_char	* laddr;
{
	tcp_t	* tcp;
	u32	src = 0;

	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());
	if ( laddr ) {
		/* we want the address as is, not swapped */
		UA32_TO_U32(laddr, src);
	}
	/*
	 * Avoid false matches for packets sent to an IP destination of
	 * all zeros.
	 */
	if (src == 0)
		return nilp(tcp_t);

	/*
	 * Skip all eager connections which might be in LISTEN state
	 * by checking that tcp_listener is non-NULL.
	 */
	tcp = tcp_listen_fanout[TCP_LISTEN_HASH(lport)];
	for (; tcp != nilp(tcp_t); tcp = tcp->tcp_listen_hash) {
		if (BE16_EQL(lport, tcp->tcp_tcph->th_lport) &&
		    (tcp->tcp_ipha.ipha_src == INADDR_ANY ||
		     tcp->tcp_ipha.ipha_src == src) &&
		    tcp->tcp_state == TCPS_LISTEN &&
		    tcp->tcp_listener == NULL)
			break;
	}
	return tcp;
}

/*
 * Adjust the maxpsz (and maxblk) variables in the stream head to
 * match this connection.
 */
staticf void
tcp_maxpsz_set (tcp)
	tcp_t	* tcp;
{
	if ( !TCP_IS_DETACHED(tcp) ) {
		queue_t	* q = tcp->tcp_rq;
		int	mss = tcp->tcp_mss;
		int 	maxpsz;

		/* Set maxpsz to approx half the (receivers) buffer (and a 
		 * multiple of the mss). This will allow acks to enter
		 * when the sending thread leaves tcp_wput_slow to go up
		 * to the stream head for more data.
		 * TODO: we don't know the receive buffer size at the remote so
		 * we approximate that with our sendbuf size!
		 */
		maxpsz = tcp_maxpsz_multiplier * mss;
		if (maxpsz > tcp->tcp_xmit_hiwater/2) {
			maxpsz = tcp->tcp_xmit_hiwater/2;
			/* Round up to nearest mss */
			maxpsz = (((maxpsz - 1) / mss) + 1) * mss;
		}
		/*
		 * XXX see comments for setmaxps
		 */
		(void)setmaxps(q, maxpsz);
		tcp->tcp_wq->q_maxpsz = maxpsz;

		mi_set_sth_maxblk(tcp->tcp_rq, mss);
	}
}

/*
 * Extract an mss option value from a tcp header.  If there is no mss option,
 * we return the tunable value found in tcp_mss_def.
 */
staticf u_long
tcp_mss_get (tcph)
	tcph_t	* tcph;
{
	u_char	* endp;
	int	len;
	u_long	mss;
	u_char	* up = (u_char *)tcph;

	endp = up + TCP_HDR_LENGTH(tcph);
	up += 20;
	while (up < endp) {
		len = endp - up;
		switch (*up) {
		case 0:
			break;
		case 1:
			up++;
			continue;
		case 2:
			if (len < 4  ||  up[1] != 4)
				break;
			up += 2;
			mss = BE16_TO_U16(up);
			if (mss < tcp_mss_min)
				return tcp_mss_min;
			if (mss > tcp_mss_max)
				return tcp_mss_max;
			return mss;

		default:
			if (len <= 1
			||  len < (int)(up[1] & 0xFF)
			||  up[1] == 0)
				break;
			up += (up[1] & 0xFF);
			continue;
		}
		break;
	}
	return tcp_mss_def;
}

/*
 * Set the mss associated with a particular tcp based on its current value,
 * and a new one passed in. Observe minimums and maximums, and reset
 * other state variables that we want to view as multiples of mss.
 */
staticf void
tcp_mss_set (tcp, mss)
	tcp_t	* tcp;
reg	u_long	mss;
{
reg	u_long	rwnd;

	if (mss < tcp_mss_min)
		mss = tcp_mss_min;
	if (mss > tcp_mss_max)
		mss = tcp_mss_max;
	/*
	 * Unless naglim has been set by our client to
	 * a non-mss value, force naglim to track mss.
	 * This can help to aggregate small writes.
	 */
	if (mss < tcp->tcp_naglim
	||  tcp->tcp_mss == tcp->tcp_naglim)
		tcp->tcp_naglim = mss;
	if (mss > tcp->tcp_xmit_hiwater)
		tcp->tcp_xmit_hiwater = mss;
	tcp->tcp_mss = mss;
	tcp->tcp_cwnd_ssthresh = mss << 3;
	tcp->tcp_cwnd = MIN(mss, tcp->tcp_cwnd_max);
	/* The receive window is q_hiwat rounded up to an integral mss. */
	if ( !TCP_IS_DETACHED(tcp) )
		rwnd = tcp->tcp_rq->q_hiwat;
	else
		rwnd = MAX(tcp_recv_hiwat, tcp_recv_hiwat_minmss * mss);

	(void)tcp_rwnd_set(tcp, rwnd);
	tcp_maxpsz_set(tcp);
}

/*
 * Called by both STREAMS and tcp_open_detached() to associate a new
 * tcp instantiation with the q passed in.
 * Uses credp == NULL to determine if it was called from tcp_open_detached.
 */
staticf int
tcp_open (q, devp, flag, sflag, credp)
	queue_t	* q;
	dev_t	* devp;
	int	flag;
	int	sflag;
	cred_t	* credp;
{
	int	err;
	tcp_t	* tcp;

	if (q->q_ptr)
		return 0;
	if (sflag != MODOPEN)
		return EINVAL;

	/*
	 * We are D_MTQPAIR so it is safe to do qprocson before allocating
	 * q_ptr.
	 */
	if (credp != NULL)
		qprocson(q);
	TCP_LOCK_WRITE();
	if (!tcp_g_nd
	&&  !tcp_param_register(tcp_param_arr, A_CNT(tcp_param_arr))) {
		TCP_UNLOCK_WRITE();
		if (credp != NULL)
			qprocsoff(q);
		return ENOMEM;
	}

	err = mi_open_comm(&tcp_g_head, sizeof(tcp_t), q, devp, flag, sflag,
			   credp);
	if (!err) {
		tcp = (tcp_t *)q->q_ptr;
		err = tcp_init(tcp, q,
				tcp_timer_alloc(tcp, tcp_timer, 0),
				allocb(tcp_winfo.mi_hiwat + 1, BPRI_LO),
				tcp_timer_alloc(tcp, tcp_co_timer, 0),
				allocb(sizeof(struct iocblk), BPRI_HI));
		if (!err) {
			TCP_UNLOCK_WRITE();
			mi_set_sth_wroff(tcp->tcp_rq,
			    tcp->tcp_hdr_len + tcp_wroff_xtra);
			tcp->tcp_mss = tcp_mss_def;
			RD(q)->q_hiwat = MAX(tcp_recv_hiwat,
					tcp_recv_hiwat_minmss * tcp->tcp_mss);
			if (credp  &&  drv_priv(credp) == 0)
				tcp->tcp_priv_stream = 1;
			return 0;
		}
		tcp_queue_hash_remove(tcp);
		mi_close_comm(&tcp_g_head, q);
	}
	tcp_param_cleanup();
	TCP_UNLOCK_WRITE();
	if (credp != NULL)
		qprocsoff(q);
	return err;
}

/*
 * Called when we need a new tcp instantiation but don't really have a
 * new q to hang it off of.  We make the proxy q up to look like a fresh q
 * supplied by STREAMS, call tcp_open(), retrieve the new instantiation,
 * and finally restore the proxy to its original state returning the new
 * instantiation as the function value.
 */
staticf tcp_t *
tcp_open_detached (q)
	queue_t	* q;
{
	dev_t	dev = 0;
	char	* ptr1;
	char	* ptr2;
	tcp_t	* tcp;
	int	saved_hiwat;	/* tcp_open resets q_hiwat to the default */

	ptr1 = q->q_ptr;
	ptr2 = OTHERQ(q)->q_ptr;
	q->q_ptr = nilp(char);
	OTHERQ(q)->q_ptr = nilp(char);
	saved_hiwat = RD(q)->q_hiwat;
	(void)tcp_open(q, &dev, 0, MODOPEN, nilp(cred_t));
	TCP_LOCK_WRITE();
	tcp = (tcp_t *)q->q_ptr;
	if (tcp) {
		mi_detach(&tcp_g_head, (IDP)(IDP)tcp);
		tcp->tcp_detached = true;
	}
	TCP_UNLOCK_WRITE();
	q->q_ptr = ptr1;
	OTHERQ(q)->q_ptr = ptr2;
	RD(q)->q_hiwat = saved_hiwat;
	return tcp;
}

staticf	boolean_t
tcp_opt_chk (level, name)
	int	level;
	int	name;
{
	switch ( level ) {
	case SOL_SOCKET:
		switch ( name ) {
		case SO_LINGER:
		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_DONTROUTE:
		case SO_USELOOPBACK:
		case SO_BROADCAST:
		case SO_REUSEADDR:
		case SO_OOBINLINE:
		case SO_TYPE:
		case SO_SNDBUF:
		case SO_RCVBUF:
		case SO_SNDLOWAT:
		case SO_RCVLOWAT:
			return true;
		default:
			return false;
		}
		/*NOTREACHED*/
	case IPPROTO_TCP:
		switch ( name ) {
		case TCP_NODELAY:
		case TCP_MAXSEG:
		case TCP_NOTIFY_THRESHOLD:
		case TCP_ABORT_THRESHOLD:
		case TCP_CONN_NOTIFY_THRESHOLD:
		case TCP_CONN_ABORT_THRESHOLD:
			return true;
		default:
			return false;
		}
		/*NOTREACHED*/
	case IPPROTO_IP:
		switch ( name ) {
		case IP_OPTIONS:
		case IP_TOS:
		case IP_TTL:
			return true;
		default:
			return false;
		}
		/*NOTREACHED*/
	default:
		return false;
	}
	/*NOTREACHED*/
}

staticf	int
tcp_opt_get (q, level, name, ptr)
	queue_t	* q;
	int	level;
	int	name;
	u_char	* ptr;
{
	int	* i1 = (int *)ALIGN32(ptr);
	tcp_t	* tcp = (tcp_t *)q->q_ptr;
	
	switch ( level ) {
	case SOL_SOCKET:
		switch ( name ) {
		case SO_LINGER: {
			struct linger * lgr = (struct linger *)ALIGN32(ptr);
			
			lgr->l_onoff = tcp->tcp_linger ? 1 : 0;
			lgr->l_linger = tcp->tcp_linger;
			}
			return sizeof(struct linger);
		case SO_DEBUG:
			*i1 = tcp->tcp_debug ? SO_DEBUG : 0;
			break;
		case SO_KEEPALIVE:
			*i1 = tcp->tcp_keepalive_mp ? SO_KEEPALIVE : 0;
			break;
		case SO_DONTROUTE:
			*i1 = tcp->tcp_dontroute ? SO_DONTROUTE : 0;
			break;
		case SO_USELOOPBACK:
			*i1 = tcp->tcp_useloopback ? SO_USELOOPBACK : 0;
			break;
		case SO_BROADCAST:
			*i1 = tcp->tcp_broadcast ? SO_BROADCAST : 0;
			break;
		case SO_REUSEADDR:
			*i1 = tcp->tcp_reuseaddr ? SO_REUSEADDR : 0;
			break;
		case SO_OOBINLINE:
			*i1 = tcp->tcp_oobinline ? SO_OOBINLINE : 0;
			break;
		case SO_TYPE:
			*i1 = SOCK_STREAM;
			break;
		case SO_SNDBUF:
			*i1 = tcp->tcp_xmit_hiwater;
			break;
		case SO_RCVBUF:
			*i1 = RD(q)->q_hiwat;
			break;
		case SO_SNDLOWAT:
			*i1 = tcp->tcp_xmit_lowater;
			break;
		case SO_RCVLOWAT:
			*i1 = RD(q)->q_lowat;
			break;
		}
		break;
	case IPPROTO_TCP:
		switch ( name ) {
		case TCP_NODELAY:
			*i1 = (tcp->tcp_naglim == 1) ? TCP_NODELAY : 0;
			break;
		case TCP_MAXSEG:
			*i1 = tcp->tcp_mss;
			break;
		case TCP_NOTIFY_THRESHOLD:
			*i1 = (int) tcp->tcp_first_timer_threshold;
			break;
		case TCP_ABORT_THRESHOLD:
			*i1 = tcp->tcp_second_timer_threshold;
			break;
		case TCP_CONN_NOTIFY_THRESHOLD:
			*i1 = tcp->tcp_first_ctimer_threshold;
			break;
		case TCP_CONN_ABORT_THRESHOLD:
			*i1 = tcp->tcp_second_ctimer_threshold;
			break;
		}
		break;
	case IPPROTO_IP:
		switch ( name ) {
		case IP_OPTIONS: {
			/* This is compatible with BSD in that in only return 
			 * the reverse source route with the final destination
			 * as the last entry. The first 4 bytes of the option
			 * will contain the final destination.
			 */
			char	* opt_ptr;
			int	opt_len;
			opt_ptr = (char *)&tcp->tcp_iph + IP_SIMPLE_HDR_LENGTH;
			opt_len = (char *)tcp->tcp_tcph - opt_ptr;
			/* Caller ensures enough space */
			if (opt_len > 0) {
				/* TODO: Do we have to handle getsockopt on an
				 * initiator as well?
				 */
				return tcp_opt_get_user(&tcp->tcp_iph, ptr);
			}
			return 0;
			}
		case IP_TOS:
			*i1 = (int) tcp->tcp_iph.iph_type_of_service;
			break;
		case IP_TTL:
			*i1 = (int) tcp->tcp_iph.iph_ttl;
			break;
		}
		break;
	}
	return sizeof(long);
}

/* We declare as 'int' rather than 'void' to satisfy pfi_t arg requirements */
staticf	int
tcp_opt_set (q, level, name, ptr, len)
	queue_t	* q;
	int	level;
	int	name;
	u_char	* ptr;
	int	len;
{
        mblk_t  * mp1;
	int	* i1 = (int *)ALIGN32(ptr);
	tcp_t	* tcp = (tcp_t *)q->q_ptr;
        tcpka_t * tcpka;

	switch ( level ) {
	case SOL_SOCKET:	
		switch ( name ) {
		case SO_LINGER: {
			struct linger * lgr = (struct linger *)ALIGN32(ptr);
			
			tcp->tcp_linger = lgr->l_onoff ? lgr->l_linger : 0;
			/*
			 * TODO:  What do we want to do with a non-zero value?
			 * By default, we hold the connection in a detached
			 * state as long as it takes to reach a final
			 * condition.  Should we set the stream head close
			 * time?  Can it be set from downstream using SETOPTS?
			 */
			}
			break;
		case SO_DEBUG:
			tcp->tcp_debug = *i1 ? 1 : 0;
			break;
		case SO_KEEPALIVE:
			mp1 = tcp->tcp_keepalive_mp;
			if ( *i1 ) {
				if ( !mp1 ) {
					/* Crank up the keepalive timer */
					mp1 = tcp_timer_alloc(tcp,
						tcp_keepalive_killer,
						sizeof(tcpka_t));
					if ( !mp1 )
						return ENOMEM;
					tcpka = (tcpka_t *)ALIGN32(mp1->b_rptr);
					tcpka->tcpka_tcp = tcp;
					tcpka->tcpka_rnxt = tcp->tcp_rnxt;
					tcpka->tcpka_snxt = tcp->tcp_snxt;
					tcp->tcp_keepalive_mp = mp1;
					mi_timer(tcp->tcp_wq,
						tcp->tcp_keepalive_mp,
						tcp->tcp_keepalive_intrvl);
					
				}
			} else {
				if ( mp1 ) {
					/* Shut down the keepalive timer */
					mi_timer_free(mp1);
					tcp->tcp_keepalive_mp = nilp(mblk_t);
				}
			}
			break;
		case SO_DONTROUTE:
			/*
			 * SO_DONTROUTE, SO_USELOOPBACK and SO_BROADCAST are
			 * only of interest to IP.  We track them here only so
			 * that we can report their current value.
			 */
			tcp->tcp_dontroute = *i1 ? 1 : 0;
			break;
		case SO_USELOOPBACK:
			tcp->tcp_useloopback = *i1 ? 1 : 0;
			break;
		case SO_BROADCAST:
			tcp->tcp_broadcast = *i1 ? 1 : 0;
			break;
		case SO_REUSEADDR:
			tcp->tcp_reuseaddr = *i1 ? 1 : 0;
			break;
		case SO_OOBINLINE:
			/* TODO: Do something with this. */
			tcp->tcp_oobinline = *i1 ? 1 : 0;
			break;
		case SO_SNDBUF:
			if (*i1 > tcp_max_buf)
				return ENOBUFS;
			tcp->tcp_xmit_hiwater = *i1;
			if (tcp_snd_lowat_fraction != 0)
				tcp->tcp_xmit_lowater = tcp->tcp_xmit_hiwater /
					tcp_snd_lowat_fraction;
			tcp_maxpsz_set(tcp);
			break;
		case SO_RCVBUF:
			if (*i1 > tcp_max_buf)
				return ENOBUFS;
			RD(q)->q_hiwat = *i1;
			(void) tcp_rwnd_set(tcp, *i1);
			break;
		case SO_SNDLOWAT:
			tcp->tcp_xmit_lowater = *i1;
			break;
		case SO_RCVLOWAT:
			RD(q)->q_lowat = *i1;
			break;
		}
		break;
	case IPPROTO_TCP:
		switch ( name ) {
		case TCP_NODELAY:
			tcp->tcp_naglim = *i1 ? 1 : tcp->tcp_mss;
			break;
		case TCP_NOTIFY_THRESHOLD:
			tcp->tcp_first_timer_threshold = (u_long) *i1;
			break;
		case TCP_ABORT_THRESHOLD:
			tcp->tcp_second_timer_threshold = (u_long) *i1;
			break;
		case TCP_CONN_NOTIFY_THRESHOLD:
			tcp->tcp_first_ctimer_threshold = (u_long) *i1;
			break;
		case TCP_CONN_ABORT_THRESHOLD:
			tcp->tcp_second_ctimer_threshold = (u_long) *i1;
			break;
		}
		break;
	case IPPROTO_IP:
		switch ( name ) {
		case IP_OPTIONS:
			return tcp_opt_set_header(tcp, ptr, (u_int)len);
		case IP_TOS:
			tcp->tcp_iph.iph_type_of_service = (u8) *i1;
			break;
		case IP_TTL:
			tcp->tcp_iph.iph_ttl = (u8) *i1;
			break;
		}
		break;
	}
	return 0;
}

/*
 * Use the outgoing IP header to create an IP_OPTIONS option the way 
 * it was passed down from the application.
 */
staticf int
tcp_opt_get_user (iph, buf)
	iph_t	* iph;
	u_char	* buf;
{
	u_char	* opt;
	int	totallen;
	u32	optval;
	u32	optlen;
	u32	len = 0;
	u_char	* buf1 = buf;
	
	buf += IP_ADDR_LEN;	/* Leave room for final destination */
	len += IP_ADDR_LEN;

	totallen = iph->iph_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	opt = (u_char *)&iph[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			goto done;
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}
		if (optlen == 0 || optlen > totallen)
			break;

		switch (optval) {
			int	off;
		case IPOPT_SSRR:
		case IPOPT_LSRR: 

			/* 
			 * Insert iph_dst as the first entry in the source
			 * route and move down the entries on step.
			 * The last entry gets placed at buf1.
			 */
			buf[IPOPT_POS_VAL] = optval;
			buf[IPOPT_POS_LEN] = optlen;
			buf[IPOPT_POS_OFF] = optlen;

			off = optlen - IP_ADDR_LEN;
			if (off < 0) {
				/* No entries in source route */
				break;
			}
			/* Last entry in source route */
			bcopy((char *)opt + off, (char *)buf1, IP_ADDR_LEN);
			off -= IP_ADDR_LEN;
			
			while (off > 0) {
				bcopy((char *)opt + off, 
				      (char *)buf + off + IP_ADDR_LEN, 
				      IP_ADDR_LEN);
				off -= IP_ADDR_LEN;
			}
			/* iph_dst into first slot */
			bcopy((char *)iph->iph_dst, 
			      (char *)buf + off + IP_ADDR_LEN,
			      IP_ADDR_LEN);
			buf += optlen;
			len += optlen;
			break;
		default:
			bcopy((char *)opt, (char *)buf, optlen);
			buf += optlen;
			len += optlen;
			break;
		}
		totallen -= optlen;
		opt += optlen;
	}
 done:
	/* Pad the resulting options */
	while (len & 0x3) {
		*buf++ = IPOPT_EOL;
		len++;
	}
	return len;
}

/*
 * Transfer any source route option from iph to buf/dst in reversed
 * form.
 */
staticf int
tcp_opt_rev_src_route (iph, buf, dst)
	iph_t	* iph;
	char	* buf;
	u_char	* dst;
{
	u32	totallen;
	u_char	* opt;
	u32	optval;
	u32	optlen;
	u32	len = 0;

	totallen = iph->iph_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	opt = (u_char *)&iph[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			goto done;
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}
		if (optlen == 0 || optlen > totallen)
			break;

		switch (optval) {
			int	off1, off2;
		case IPOPT_SSRR:
		case IPOPT_LSRR: 

			/* Reverse source route */
			/* First entry should be the next to last one in the
			 * current source route (the last entry is our 
			 * address.)
			 * The last entry should be the final destination.
			 */
			buf[IPOPT_POS_VAL] = (u8)optval;
			buf[IPOPT_POS_LEN] = (u8)optlen;
			off1 = IPOPT_MINOFF_SR - 1;
			off2 = opt[IPOPT_POS_OFF] - IP_ADDR_LEN - 1;
			if (off2 < 0) {
				/* No entries in source route */
				break;
			}
			bcopy((char *)opt + off2, (char *)dst, IP_ADDR_LEN);
			/* Note: use src since iph has not had its src
			 * and dst reversed (it is in the state it was
			 * received.
			 */
			bcopy((char *)iph->iph_src, buf + off2, IP_ADDR_LEN);
			off2 -= IP_ADDR_LEN;
			
			while (off2 > 0) {
				bcopy((char *)opt + off2, buf + off1, 
				      IP_ADDR_LEN);
				off1 += IP_ADDR_LEN;
				off2 -= IP_ADDR_LEN;
			}
			buf[IPOPT_POS_OFF] = IPOPT_MINOFF_SR;
			buf += optlen;
			len += optlen;
			break;
		}
		totallen -= optlen;
		opt += optlen;
	}
 done:
	/* Pad the resulting options */
	while (len & 0x3) {
		*buf++ = IPOPT_EOL;
		len++;
	}
	return len;
}


/*
 * Extract and revert a source route from iph (if any)
 * and then update the relevant fields in both tcp_t and the standard header.
 */
staticf void
tcp_opt_reverse (tcp, iph)
	tcp_t	* tcp;
	iph_t	* iph;
{
	char	buf[TCP_MAX_HDR_LENGTH];
	u_int	tcph_len;
	int	len;

	len = IPH_HDR_LENGTH(iph);
	if (len == IP_SIMPLE_HDR_LENGTH)
		/* Nothing to do */
		return;
	if (len > IP_SIMPLE_HDR_LENGTH + TCP_MAX_IP_OPTIONS_LENGTH  ||  
	    (len & 0x3))
		return;

	tcph_len = tcp->tcp_tcp_hdr_len;
	bcopy((char *)tcp->tcp_tcph, buf, tcph_len);
	tcp->tcp_sum = (tcp->tcp_ipha.ipha_dst >> 16) +
		(tcp->tcp_ipha.ipha_dst & 0xffff);
	len = tcp_opt_rev_src_route(iph, &tcp->tcp_iphc[IP_SIMPLE_HDR_LENGTH],
				    tcp->tcp_iph.iph_dst);
	len += IP_SIMPLE_HDR_LENGTH;
	tcp->tcp_sum -= ((tcp->tcp_ipha.ipha_dst >> 16) +
			 (tcp->tcp_ipha.ipha_dst & 0xffff));
	if ((int)tcp->tcp_sum < 0)
		tcp->tcp_sum--;
	tcp->tcp_sum = (tcp->tcp_sum & 0xFFFF) + (tcp->tcp_sum >> 16);
	tcp->tcp_sum = (tcp->tcp_sum & 0xFFFF) + (tcp->tcp_sum >> 16);
	tcp->tcp_tcph = (tcph_t *)&tcp->tcp_iphc[len];
	bcopy(buf, (char *)tcp->tcp_tcph, tcph_len);
	tcp->tcp_ip_hdr_len = len;
	tcp->tcp_iph.iph_version_and_hdr_length =
		(IP_VERSION << 4) | (len >> 2);
	len += tcph_len;
	tcp->tcp_hdr_len = len;
	if (!TCP_IS_DETACHED(tcp))
		mi_set_sth_wroff(tcp->tcp_rq, len + tcp_wroff_xtra);
	return;
}

/*
 * Copy the standard header into its new location,
 * lay in the new options and then update the relevant
 * fields in both tcp_t and the standard header.
 * NOTE: this could be simpler if we trusted bcopy()
 * with overlapping src/dst.
 */
staticf int
tcp_opt_set_header (tcp, ptr, len)
	tcp_t	* tcp;
	u_char	* ptr;
	u_int	len;
{
	char	buf[TCP_MAX_HDR_LENGTH];
	u_int	tcph_len;

	if (len > TCP_MAX_IP_OPTIONS_LENGTH  ||  (len & 0x3))
		return EINVAL;
	tcph_len = tcp->tcp_tcp_hdr_len;
	bcopy((char *)tcp->tcp_tcph, buf, tcph_len);
	bcopy((char *)ptr, &tcp->tcp_iphc[IP_SIMPLE_HDR_LENGTH], len);
	len += IP_SIMPLE_HDR_LENGTH;
	tcp->tcp_tcph = (tcph_t *)&tcp->tcp_iphc[len];
	bcopy(buf, (char *)tcp->tcp_tcph, tcph_len);
	tcp->tcp_ip_hdr_len = len;
	tcp->tcp_iph.iph_version_and_hdr_length =
		(IP_VERSION << 4) | (len >> 2);
	len += tcph_len;
	tcp->tcp_hdr_len = len;
	if (!TCP_IS_DETACHED(tcp))
		mi_set_sth_wroff(tcp->tcp_rq, len + tcp_wroff_xtra);
	return 0;
}

/*
 * Called during close processing.  If the last instance has gone away,
 * free the Named Dispatch table.
 */
staticf void
tcp_param_cleanup ()
{
	ASSERT(TCP_WRITE_HELD());
	if (!tcp_g_head)
		nd_free(&tcp_g_nd);
}

/* Get callback routine passed to nd_load by tcp_param_register */
/* ARGSUSED */
staticf int
tcp_param_get (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	tcpparam_t	* tcppa = (tcpparam_t *)ALIGN32(cp);

	/* Lock held before calling nd_getset */
	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	mi_mpprintf(mp, "%ld", tcppa->tcp_param_val);
	return 0;
}

/*
 * Walk through the param array specified registering each element with the
 * named dispatch handler.
 */
staticf boolean_t
tcp_param_register (tcppa, cnt)
reg	tcpparam_t	* tcppa;
	int	cnt;
{
	ASSERT(TCP_WRITE_HELD());
	for ( ; cnt-- > 0; tcppa++) {
		if (tcppa->tcp_param_name  &&  tcppa->tcp_param_name[0]) {
			if (!nd_load(&tcp_g_nd, tcppa->tcp_param_name,
				     tcp_param_get, tcp_param_set,
				     (caddr_t)tcppa)) {
				nd_free(&tcp_g_nd);
				return false;
			}
		}
	}
	if ( !nd_load(&tcp_g_nd, "tcp_status", tcp_status_report, nil(pfi_t),
		nil(caddr_t)) ) {
		nd_free(&tcp_g_nd);
		return false;
	}
	if ( !nd_load(&tcp_g_nd, "tcp_bind_hash", tcp_bind_hash_report,
		nil(pfi_t), nil(caddr_t)) ) {
		nd_free(&tcp_g_nd);
		return false;
	}
	if ( !nd_load(&tcp_g_nd, "tcp_listen_hash", tcp_listen_hash_report,
		nil(pfi_t), nil(caddr_t)) ) {
		nd_free(&tcp_g_nd);
		return false;
	}
	if ( !nd_load(&tcp_g_nd, "tcp_conn_hash", tcp_conn_hash_report,
		nil(pfi_t), nil(caddr_t)) ) {
		nd_free(&tcp_g_nd);
		return false;
	}
	if ( !nd_load(&tcp_g_nd, "tcp_queue_hash", tcp_queue_hash_report,
		nil(pfi_t), nil(caddr_t)) ) {
		nd_free(&tcp_g_nd);
		return false;
	}
#ifdef	MI_HRTIMING
	if ( !nd_load(&tcp_g_nd, "tcp_times", tcp_time_report, tcp_time_reset,
		      nil(caddr_t)) ) {
		nd_free(&tcp_g_nd);
		return false;
	}
#endif
	return true;
}

/* Set callback routine passed to nd_load by tcp_param_register */
/* ARGSUSED */
staticf int
tcp_param_set (q, mp, value, cp)
	queue_t	* q;
	mblk_t	* mp;
	char	* value;
	caddr_t	cp;
{
	char	* end;
	long	new_value;
	tcpparam_t	* tcppa = (tcpparam_t *)ALIGN32(cp);

	/* Lock held before calling nd_getset */
	ASSERT(TCP_WRITE_HELD());

	new_value = mi_strtol(value, &end, 10);
	if (end == value
	||  new_value < tcppa->tcp_param_min
	||  new_value > tcppa->tcp_param_max)
		return EINVAL;
	tcppa->tcp_param_val = new_value;
	return 0;
}

/*
 * Add a new piece to the tcp reassembly queue.  If the gap at the beginning
 * is filled, return as much as we can.  The message passed in may be
 * multi-part, chained using b_cont.
 */
staticf mblk_t *
tcp_reass (tcp, mp, start, flagsp)
	tcp_t	* tcp;
	mblk_t	* mp;
	u_long	start;	/* Starting sequence number for this piece. */
	u_int	* flagsp;
{
	u_long	end;
reg	mblk_t	* mp1;
	mblk_t	* mp2;
	mblk_t	* next_mp;
	u_long	u1;

	/* Walk through all the new pieces. */
	do {
		end = start + (mp->b_wptr - mp->b_rptr);
		next_mp = mp->b_cont;
		if (start == end) {
			/* Empty.  Blast it. */
			freeb(mp);
			continue;
		}
		mp->b_cont = nilp(mblk_t);
		TCP_REASS_SET_SEQ(mp, start);
		TCP_REASS_SET_END(mp, end);
		mp1 = tcp->tcp_reass_tail;
		if ( !mp1 ) {
			tcp->tcp_reass_tail = mp;
			tcp->tcp_reass_head = mp;
 			/* A fresh gap.  Make sure we get an ACK out. */
 			*flagsp |= TH_ACK_NEEDED;
			BUMP_MIB(tcp_mib.tcpInDataUnorderSegs);
			UPDATE_MIB(tcp_mib.tcpInDataUnorderBytes, end - start);
			continue;
		}
		/* New stuff completely beyond tail? */
		if ( SEQ_GEQ(start, TCP_REASS_END(mp1)) ) {
			/* Link it on end. */
			mp1->b_cont = mp;
			tcp->tcp_reass_tail = mp;
			BUMP_MIB(tcp_mib.tcpInDataUnorderSegs);
			UPDATE_MIB(tcp_mib.tcpInDataUnorderBytes, end - start);
			continue;
		}
		mp1 = tcp->tcp_reass_head;
		u1 = TCP_REASS_SEQ(mp1);
		/* New stuff at the front? */
		if ( SEQ_LT(start, u1) ) {
			/* Yes... Check for overlap. */
			mp->b_cont = mp1;
			tcp->tcp_reass_head = mp;
			tcp_reass_elim_overlap(tcp, mp);
			continue;
		}
		/*
		 * The new piece fits somewhere between the head and tail.
		 * We find our slot, where mp1 preceeds us and mp2 trails.
		 */
		for ( ; (mp2 = mp1->b_cont) != NULL; mp1 = mp2) {
			u1 = TCP_REASS_SEQ(mp2);
			if (SEQ_LEQ(start, u1))
				break;
		}
		/* Link ourselves in */
		mp->b_cont = mp2;
		mp1->b_cont = mp;

		/* Trim overlap with following mblk(s) first */
		tcp_reass_elim_overlap(tcp, mp);

		/* Trim overlap with preceeding mblk */
		tcp_reass_elim_overlap(tcp, mp1);

	} while ( start = end, mp = next_mp );
	mp1 = tcp->tcp_reass_head;
	/* Anything ready to go? */
	if ( TCP_REASS_SEQ(mp1) != tcp->tcp_rnxt )
		return nilp(mblk_t);
	/* Eat what we can off the queue */
	for(;;) {
		mp = mp1->b_cont;
		end = TCP_REASS_END(mp1);
		TCP_REASS_SET_SEQ(mp1, 0);
		TCP_REASS_SET_END(mp1, 0);
		if ( !mp ) {
			tcp->tcp_reass_tail = nilp(mblk_t);
			break;
		}
		if ( end != TCP_REASS_SEQ(mp) ) {
			mp1->b_cont = nilp(mblk_t);
			break;
		}
		mp1 = mp;
	}
	mp1 = tcp->tcp_reass_head;
	tcp->tcp_reass_head = mp;
	if ( mp ) {
		/*
		 * We filled in the hole at the front, but there is still
		 * a gap.  Make sure another ACK goes out.
		 */
		*flagsp |= TH_ACK_NEEDED;
	}
	return mp1;
}

/* Eliminate any overlap that mp may have over later mblks */
staticf void
tcp_reass_elim_overlap (tcp, mp)
	tcp_t	* tcp;
	mblk_t	* mp;
{
	u_long	end;
	mblk_t	* mp1;
	u_long	u1;

	end = TCP_REASS_END(mp);
	while ((mp1 = mp->b_cont) != NULL){
		u1 = TCP_REASS_SEQ(mp1);
		if ( !SEQ_GT(end, u1) )
			break;
		if ( !SEQ_GEQ(end, TCP_REASS_END(mp1)) ) {
			mp->b_wptr -= (int)(end - u1);
			TCP_REASS_SET_END(mp, u1);
			BUMP_MIB(tcp_mib.tcpInDataPartDupSegs);
			UPDATE_MIB(tcp_mib.tcpInDataPartDupBytes, end - u1);
			break;
		}
		mp->b_cont = mp1->b_cont;
		TCP_REASS_SET_SEQ(mp1, 0);
		TCP_REASS_SET_END(mp1, 0);
		freeb(mp1);
		BUMP_MIB(tcp_mib.tcpInDataDupSegs);
		UPDATE_MIB(tcp_mib.tcpInDataDupBytes, end - u1);
	}
	if (!mp1)
		tcp->tcp_reass_tail = mp;
}

/*
 * tcp_co_drain is called to drain the co queue.
 */

#define CCS_STATS 0

#if CCS_STATS
struct {
	struct {
		long long count, bytes;
	} hit, rrw, mis, spc, seq, len, uio, tim, uer, cer, oth;
} rrw_stats;
#endif

staticf void
tcp_co_drain (tcp)
	tcp_t	* tcp;
{
	queue_t	* q = tcp->tcp_rq;
	mblk_t	* mp;
	mblk_t	* mp1;

	if (tcp->tcp_co_tintrvl != -1l) {
		/*
		 * Cancle outstanding co timer.
		 */ 
		tcp->tcp_co_tintrvl = -1l;
		mi_timer(tcp->tcp_wq, tcp->tcp_co_tmp, -1L);
	}
	/*
	 * About to putnext() some message(s) causing a transition to
	 * normal STREAMS mode, so cleanup any co queue state first.
	 */
	tcp->tcp_co_wakeq_force = 0;
	tcp->tcp_co_wakeq_need = 0;
	tcp->tcp_co_wakeq_done = 0;
	while ((mp = tcp->tcp_co_head) != NULL) {
		if ((tcp->tcp_co_head = mp->b_next) == NULL)
			tcp->tcp_co_tail = nilp(mblk_t);
		mp->b_next = nilp(mblk_t);
		if (mp->b_datap->db_struioflag & STRUIO_IP) {
			/*
			 * Delayed IP checksum required.
			 */
			int off = mp->b_datap->db_struiobase - mp->b_rptr;

			if (IP_CSUM(mp, off, 0)) {
				/*
				 * Checksum error, so drop it.
				 */
				BUMP_MIB(ip_mib.tcpInErrs);
				ip1dbg(("tcp_co_drain: drop pkt\n"));
				freemsg(mp);
				continue;
			}
		}
		/*
		 * A normal mblk now, so clear the struioflag and rput() it.
		 */
		mp1 = mp;
		do
			mp1->b_datap->db_struioflag = 0;
		while ((mp1 = mp1->b_cont) != NULL);
		tcp_rput_data(q, mp, -1);
	}
	tcp->tcp_co_cnt = 0;
	if ((mp = tcp->tcp_rcv_head) != NULL) {
		/*
		 * The rcv push queue has mblks, putnext() them.
		 */
		tcp->tcp_rcv_head = nilp(mblk_t);
		tcp->tcp_rcv_cnt = 0;
		tcp->tcp_co_norm = 1;
		putnext(q, mp);
	}
}

/*
 * tcp_co_timer is the timer service routine for the co queue.
 * It handles timer events for a tcp instance setup by tcp_rput().
 */
staticf void
tcp_co_timer (tcp)
	tcp_t	* tcp;
{
	tcp->tcp_co_tintrvl = -1l;
#if CCS_STATS
	rrw_stats.tim.count++;
	rrw_stats.tim.bytes += tcp->tcp_co_cnt;
#endif
	tcp_co_drain(tcp);
}

/* The read side info procedure. */
staticf int
tcp_rinfop (q, dp)
	queue_t	* q;
	infod_t	* dp;
{
	tcp_t		* tcp = (tcp_t *)q->q_ptr;
	mblk_t		* mp = tcp->tcp_rcv_head;
	unsigned	cmd = dp->d_cmd;
	unsigned	res = 0;
	int		list = 0;
	int		n;

	/*
	 * We have two list of mblk(s) to scan (rcv (push) & co).
	 *
	 * Note: mblks on the co still have IP and TCP headers, so
	 *	 the header lengths must be accounted for !!!
	 */
	if (! mp) {
		list++;
		mp = tcp->tcp_co_head;
	}
	if (mp) {
		/*
		 * We have enqued mblk(s).
		 */
		if (cmd & INFOD_COUNT) {
			/*
			 * Count one msg for either push or co queue, and two
			 * for both. The co queue may end up being pushed as
			 * multiple messages, but underestimating should be ok.
			 */
			dp->d_count++;
			if (! list && tcp->tcp_co_head)
				dp->d_count++;
			res |= INFOD_COUNT;
		}
		if (cmd & INFOD_FIRSTBYTES) {
			dp->d_bytes = msgdsize(mp);
			if (list) {
				TCPIP_HDR_LENGTH(mp, n);
				dp->d_bytes -= n;
			}
			res |= INFOD_FIRSTBYTES;
			dp->d_cmd &= ~INFOD_FIRSTBYTES;
		}
		if (cmd & INFOD_COPYOUT) {
			mblk_t	* mp1 = mp;
			u_char	* rptr = mp1->b_rptr;
			int error;

			if (list) {
				if (mp->b_datap->db_struioflag & STRUIO_IP) {
					/*
					 * Delayed IP checksum required.
					 */
					int off = mp->b_datap->db_struiobase
						- mp->b_rptr;

					if (IP_CSUM(mp, off, 0))
						/*
						 * Bad checksum, so just
						 * skip the INFOD_COPYOUT.
						 */
						goto skip;
				}
				TCPIP_HDR_LENGTH(mp1, n);
				rptr += n;
			}
			while (dp->d_uiop->uio_resid) {
				n = MIN(dp->d_uiop->uio_resid,
					mp1->b_wptr - rptr);
				if (n != 0 && (error = uiomove((char *)rptr, n,
				    UIO_READ, dp->d_uiop)) != 0)
					return (error);
				if ((mp1 = mp1->b_cont) == NULL)
					break;
				rptr = mp1->b_rptr;
			}
			res |= INFOD_COPYOUT;
			dp->d_cmd &= ~INFOD_COPYOUT;
		skip:;
		}
		if (cmd & INFOD_BYTES) {
			do {
				if (cmd & INFOD_BYTES) {
					dp->d_bytes += msgdsize(mp);
					if (list) {
						TCPIP_HDR_LENGTH(mp, n);
						dp->d_bytes -= n;
					}
				}
				if (list)
					mp = mp->b_next;
				else {
					list++;
					mp = tcp->tcp_co_head;
				}
			} while (mp);
			res |= INFOD_BYTES;
		}
	}
	if (res)
		dp->d_res |= res;

	if (isuioq(q))
		/*
		 * This is the struio() Q (last), nothing more todo.
		 */
		return (0);

	if (dp->d_cmd)
		/*
		 * Need to look at all mblk(s) or haven't completed
		 * all cmds, so pass info request on.
		 */
		return infonext(q, dp);

	return (0);
}

/* The read side r/w procedure. */
staticf int
tcp_rrw (q, dp)
	queue_t	* q;
	struiod_t * dp;
{
	tcp_t	* tcp = (tcp_t *)q->q_ptr;
	uio_t	* uiop = &dp->d_uio;
	int	needstruio = 0;
	int	needcksum = 0;
	mblk_t	* mp;
	mblk_t	* mp1;
	mblk_t	* mp2;
	int	err = 0;

	if (tcp->tcp_co_norm || tcp->tcp_co_imp == NULL)
		/*
		 * The stream is currently in normal mode or in transition to
		 * synchronous mode, so just return EBUSY.
		 */
		return (EBUSY);
	if ((mp = tcp->tcp_rcv_head) != NULL) {
		/*
		 * The rcv push queue has mblk(s), so account for them in the
		 * uio, they will be returned ahead of any struioput() mblk(s)
		 * below.
		 *
		 * Note: we assume that the rcv push queue count is
		 *	 accurate and only M_DATA mblk(s) are enqued.
		 */
		uioskip(uiop, tcp->tcp_rcv_cnt);
	}
	/*
	 * While uio and a co enqued segment:
	 */
	while (uiop->uio_resid > 0 && (mp = tcp->tcp_co_head)) {
		if (tcp->tcp_co_tintrvl != -1l) {
			/*
			 * Cancle outstanding co timer.
			 */ 
			tcp->tcp_co_tintrvl = -1l;
			mi_timer(tcp->tcp_wq, tcp->tcp_co_tmp, -1L);
		}
		/*
		 * Deque segment from the co queue.
		 */
		if ((tcp->tcp_co_head = mp->b_next) == NULL)
			tcp->tcp_co_tail = nilp(mblk_t);
		mp->b_next = nilp(mblk_t);
		/*
		 * Find last mblk of the segment (mblk chain),
		 * also see if we need to do struioput() and if
		 * we need to check the resulting IP checksum.
		 */
		mp1 = mp;
		do {
			if (mp1->b_datap->db_struioflag & STRUIO_SPEC) {
				needstruio++;
				if (mp1->b_datap->db_struioflag & STRUIO_IP)
					needcksum++;
			}
			mp2 = mp1;
		} while ((mp1 = mp1->b_cont) != NULL);

		if (needstruio && (err = struioput(q, mp, dp))) {
			/*
			 * Uio error of some sort, so process
			 * this segment and drain the co queue.
			 */
			int off = mp->b_datap->db_struiobase - mp->b_rptr;

			if (IP_CSUM(mp, off, 0)) {
				/*
				 * Checksum error, so drop it.
				 */
				BUMP_MIB(ip_mib.tcpInErrs);
				ip1dbg(("tcp_co_drain: drop pkt\n"));
				freemsg(mp);
			} else
				tcp_rput_data(q, mp, 0);
#if CCS_STATS
			rrw_stats.uer.count++;
			rrw_stats.uer.bytes += tcp->tcp_co_cnt;
#endif
			if (tcp->tcp_co_head)
				tcp_co_drain(tcp);
			break;
		}
		if (needcksum) {
			int off = mp->b_datap->db_struiobase - mp->b_rptr;

			if (IP_CSUM(mp, off, 0)) {
				/*
				 * Checksum error, so drop it.
				 */
				BUMP_MIB(ip_mib.tcpInErrs);
				ip1dbg(("tcp_co_drain: drop pkt\n"));
				freemsg(mp);
#if CCS_STATS
				rrw_stats.cer.count++;
				rrw_stats.cer.bytes += tcp->tcp_co_cnt;
#endif
				if (tcp->tcp_co_head)
					tcp_co_drain(tcp);
				break;
			}
		}
		/*
		 * Process the segment, our segment will be added to the
		 * end of the rcv push queue if no putnext()s are done.
		 */
		tcp_rput_data(q, mp, 0);
		if (tcp->tcp_rcv_tail != mp2) {
		 	/*
			 * The last mblk of the rcv push queue isn't the last
			 * mblk of the segement just processed, so its time to
			 * return the rcv push list.
			 */
			break;
		}
	}

	if ((mp = tcp->tcp_co_head) != NULL) {
		/*
		 * The co queue still has mblk(s).
		 */
		if (tcp->tcp_co_tintrvl == -1l) {
			/*
			 * Restart a timer to drain the co queue,
			 * in case a readnext() doesn't make its
			 * way down here again in a timely fashion.
			 */
			tcp->tcp_co_tintrvl = tcp->tcp_timer_interval >> 1;
			mi_timer(tcp->tcp_wq, tcp->tcp_co_tmp,
				 tcp->tcp_co_tintrvl);
		}
		if (tcp->tcp_co_cnt >= tcp->tcp_rack_cur_max ||
		   tcp->tcp_co_wakeq_force) {
			/*
			 * A readnext() from the streamhead is
			 * still needed, so do a strwakeq().
			 */
			strwakeq(tcp->tcp_rq, QWANTR);
			tcp->tcp_co_wakeq_done = 1;
		} else {
			strwakeqclr(tcp->tcp_rq, QWANTR);
			tcp->tcp_co_wakeq_done = 0;
		}
	} else {
		strwakeqclr(tcp->tcp_rq, QWANTR);
		tcp->tcp_co_wakeq_done = 0;
		tcp->tcp_co_wakeq_force = 0;
	}

	if ((mp = tcp->tcp_rcv_head) != NULL) {
		/*
		 * The rcv push queue has mblk(s), return them.
		 */
#if CCS_STATS
		rrw_stats.rrw.count++;
		rrw_stats.rrw.bytes += tcp->tcp_rcv_cnt;
#endif
		tcp->tcp_rcv_head = nilp(mblk_t);
		tcp->tcp_rcv_cnt = 0;
	}
	dp->d_mp = mp;
	return (err);
}

/*
 * The read side put procedure.
 * The packets passed up by ip are assume to be aligned according to
 * OK_32PTR and the IP+TCP headers fitting in the first mblk.
 */
staticf void
tcp_rput (q, mp)
	queue_t	* q;
reg	mblk_t	* mp;
{
reg	tcp_t	* tcp = (tcp_t *)q->q_ptr;
#ifdef	SYNC_CHK
	tcp_t	* otcp = (tcp_t *)q->q_ptr;
#endif

	SYNC_CHK_IN(otcp, "tcp_rput");
        TRACE_3(TR_FAC_TCP, TR_TCP_RPUT_IN,
                "tcp_rput start:  q %X mp %X db_type 0%o",
                 q, mp, mp->b_datap->db_type);

	if (mp->b_datap->db_type == M_DATA) {
		tcp_rput_data(q, mp, 1);
		TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT, "tcp_rput end:  q %X", q);
		return;
	}

#if CCS_STATS
	rrw_stats.oth.count++;
	rrw_stats.oth.bytes += tcp->tcp_co_cnt;
#endif
	if (tcp->tcp_co_head)
		tcp_co_drain(tcp);

	tcp_rput_other(q, mp);

	SYNC_CHK_OUT(otcp, "tcp_rput");
	TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT, "tcp_rput end:  q %X", q);

	return;
}

staticf void
tcp_rput_data (q, mp, isput)
	queue_t	* q;
reg	mblk_t	* mp;
	int	isput;	/* -1 = recursive rput, 0 = co queue, 1 = rput */
{
	i32	bytes_acked;
	long	gap;
reg	mblk_t	* mp1;
reg	u_int	flags;
	u_int	new_swnd;
	long	old_rto;
	u_char	* orptr = mp->b_rptr;
	u_char	* rptr = mp->b_rptr;
	long	rgap;
	u_long	seg_ack;
	int	seg_len;
	u_long	seg_seq;
reg	tcp_t	* tcp = (tcp_t *)q->q_ptr;
	tcph_t	* tcph;
	int	urp;
	int	rcv_cnt;
	MI_HRT_DCL(rtime)
	dblk_t	* dp = mp->b_datap;

	MI_HRT_SET(rtime);	

	seg_len = IPH_HDR_LENGTH(rptr);
	tcph = (tcph_t *)&rptr[seg_len];
	if (!OK_32PTR(rptr)) {
		seg_seq = BE32_TO_U32(tcph->th_seq);
		seg_ack = BE32_TO_U32(tcph->th_ack);
	} else {
		seg_seq = ABE32_TO_U32(ALIGN32(tcph->th_seq));
		seg_ack = ABE32_TO_U32(ALIGN32(tcph->th_ack));
	}
	seg_len += TCP_HDR_LENGTH(tcph);
	seg_len = -seg_len;
	seg_len += mp->b_wptr - rptr;
	if ((mp1 = mp->b_cont) != NULL) {
		do {
			seg_len += mp1->b_wptr - mp1->b_rptr;
		} while ((mp1 = mp1->b_cont) != NULL);
	}

	if (isput > 0) {
		/*
		 * Called from put, so check for co eligability.
		 */
		if (! (dp->db_struioflag & STRUIO_SPEC)
		    || seg_seq != (tcp->tcp_co_tail ? tcp->tcp_co_rnxt
						    : tcp->tcp_rnxt)
		    || seg_len < tcp_co_min
		    || tcp->tcp_reass_head
		    || (tcph->th_flags[0] & TH_URG)
		    || ! isuioq(q)) {
			/*
			 * Segment not co eligable, drain co queue,
			 * if need be, then process this segment.
			 */
#if CCS_STATS
			rrw_stats.mis.count++;
			rrw_stats.mis.bytes += seg_len;
			if (! (dp->db_struioflag & STRUIO_SPEC)) {
				rrw_stats.spc.count++;
				rrw_stats.spc.bytes += seg_len;
				rrw_stats.spc.bytes += tcp->tcp_co_cnt;
			} else if (seg_seq!=(tcp->tcp_co_tail? tcp->tcp_co_rnxt
							    : tcp->tcp_rnxt)) {
				rrw_stats.seq.count++;
				rrw_stats.seq.bytes += seg_len;
				rrw_stats.seq.bytes += tcp->tcp_co_cnt;
			} else if (seg_len < tcp_co_min) {
				rrw_stats.len.count++;
				rrw_stats.len.bytes += seg_len;
				rrw_stats.len.bytes += tcp->tcp_co_cnt;
			} else {
				rrw_stats.uio.count++;
				rrw_stats.uio.bytes += seg_len;
				rrw_stats.uio.bytes += tcp->tcp_co_cnt;
			}
#endif
			if (tcp->tcp_co_head)
				tcp_co_drain(tcp);

			if (dp->db_struioflag & STRUIO_IP) {
				/*
				 * Delayed IP checksum required.
				 */
				int off = dp->db_struiobase - rptr;

				if (IP_CSUM(mp, off, 0)) {
					/*
					 * Checksum error, so drop it.
					 */
					BUMP_MIB(ip_mib.tcpInErrs);
					ip1dbg(("tcp_co_drain: drop pkt\n"));
					freemsg(mp);
					return;
				}
				dp->db_struiobase += TCP_HDR_LENGTH(tcph);
				dp->db_struioptr += TCP_HDR_LENGTH(tcph);
			}
			/*
			 * A normal mblk now, so clear the struioflag.
			 */
			mp1 = mp;
			do
				mp1->b_datap->db_struioflag = 0;
			while ((mp1 = mp1->b_cont) != NULL);
		} else {
			/*
			 * Segment is co eligable, enque it on the co queue.
			 */
#if CCS_STATS
			rrw_stats.hit.count++;
			rrw_stats.hit.bytes += seg_len;
#endif
			if (dp->db_struioflag & STRUIO_IP) {
				/*
				 * IP postponded checksum, IP header has been
				 * checksumed so do the TCP header and init
				 * the mblk (chain) for struioput().
				 */
				int off = dp->db_struiobase - rptr;

				dp->db_struiobase += TCP_HDR_LENGTH(tcph);
				mp1 = mp;
				do
					mp1->b_datap->db_struioptr =
					    mp1->b_datap->db_struiolim;
				while ((mp1 = mp1->b_cont) != NULL);
				*(u_short *)dp->db_struioun.data =
				    IP_CSUM_PARTIAL(mp, off, 0);
				mp1 = mp;
				do
					mp1->b_datap->db_struioptr =
					    mp1->b_datap->db_struiobase;
				while ((mp1 = mp1->b_cont) != NULL);
			} else {
				/*
				 * IP has inited the mblk (chain) for use by
				 * struioput(), so adjust the first (only)
				 * mblk to account for the TCP header.
				 */
				dp->db_struiobase += TCP_HDR_LENGTH(tcph);
				dp->db_struioptr = dp->db_struiobase;
			}
			if (tcp->tcp_co_tail) {
				/*
				 * Another seg, enque on tail, and update rnxt.
				 */
				tcp->tcp_co_tail->b_next = mp;
				tcp->tcp_co_rnxt += seg_len;
			} else {
				/*
				 * First seg, enque, and init rnxt.
				 */
				tcp->tcp_co_head = mp;
				tcp->tcp_co_rnxt = tcp->tcp_rnxt + seg_len;
				if (tcp->tcp_co_norm &&
				    (mp1 = tcp->tcp_co_imp) != NULL) {
					/*
					 * May be putnext()ed mblk(s) in flight
					 * above us, so send up an M_IOCTL and
					 * the streamhead will send an M_IOCNAK
					 * back done. Until then no rwnext().
					 */
					tcp->tcp_co_imp = nilp(mblk_t);
					tcp->tcp_co_norm = 0;
					(void) struio_ioctl(tcp->tcp_rq, mp1);
				}
			}
			tcp->tcp_co_tail = mp;
			mp->b_next = 0;
			tcp->tcp_co_cnt += seg_len;
			if (tcp->tcp_co_tintrvl == -1l) {
				/*
				 * Start a timer to drain the co queue,
				 * in case a readnext() doesn't make its
				 * way down here in a timely fashion.
				 */
				tcp->tcp_co_tintrvl = tcp->tcp_timer_interval >> 1;
				mi_timer(tcp->tcp_wq, tcp->tcp_co_tmp,
					 tcp->tcp_co_tintrvl);
			}
			if ((! tcp->tcp_co_wakeq_done &&
			     tcp->tcp_co_cnt >= tcp->tcp_rack_cur_max)
			  || (tcph->th_flags[0] & (TH_PSH|TH_FIN))
			  || seg_len < tcp->tcp_mss) {
				/*
				 * A readnext() from the streamhead is needed.
				 */
				if (tcph->th_flags[0] & (TH_PSH|TH_FIN))
					tcp->tcp_co_wakeq_force = 1;
				if (tcp->tcp_co_imp == NULL) {
					/*
					 * Waiting for the I_SYNCSTR from above
					 * to return, so defer the strwakeq().
					 */
					tcp->tcp_co_wakeq_need = 1;
				} else {
					tcp->tcp_co_wakeq_done = 1;
					strwakeq(tcp->tcp_rq, QWANTR);
				}
			}
			return;
		}
	} else if (isput == 0)
		/*
		 * Called to process a dequed co queue
		 * segment, adjust co queue byte count.
		 */
		tcp->tcp_co_cnt -= seg_len;
	
	/*
	 * If hard bound, the message is known to be for the instance on
	 * which it was delivered.  Otherwise, we have to find out which
	 * queue it really belongs on, and deliver it there.  IP only knows
	 * how to fan out to the fully established connections.  Anything
	 * else, it hands to the first non-hard-bound stream.
	 */
	if (!tcp->tcp_hard_bound) {
		tcp_t	* tcp1;
		
		/*
		 * Try for an exact match.  If we find one, and it is the
		 * stream we are already on, then get to work.
		 */
		TCP_LOCK_READ();
		tcp1 = tcp_lookup((iph_t *)rptr, tcph, TCPS_LISTEN);
		if (tcp1 == tcp && tcp != NULL) {
			/* Already on correct queue */
			ASSERT(tcp->tcp_rq == q);
			TCP_UNLOCK_READ();
			goto do_detached;
		}
		/*
		 * If this queue is in process of detaching we put
		 * the message on the default queue and let it
		 * sit until the detach is complete.  Since the default
		 * queue is noenabled this will not loop.  Eventually
		 * tcp_detach will do an explicit qenable on the default
		 * queue to actually process this message.
		 */
		if ( TCP_IS_DETACHING(tcp1) && tcp_g_q != NULL ) {
			/*
			 * The close is in progress. Putq on the default
			 * read queue (which is noenabled to avoid an infinite 
			 * loop. tcp_close will qenable the default
			 * read queue to process the message.
			 */
			lateral_putq(q, tcp_g_q, mp);
			TCP_UNLOCK_READ();
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
		if ( TCP_IS_DETACHED(tcp1) ) {
			if ( tcp1->tcp_listener ) {
				/*
				 * Stuff for an eager connection
				 * riding on the listener queue.
				 */
				if ( tcp == tcp1->tcp_listener ) {
					tcp = tcp1;
					ASSERT(tcp->tcp_rq == q);
					TCP_UNLOCK_READ();
					goto do_detached;
				}
				
				ASSERT(q != tcp1->tcp_listener->tcp_rq);
				lateral_putq(q,
				    tcp1->tcp_listener->tcp_rq,
				    mp);
				TCP_UNLOCK_READ();
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
				return;
			}
			ASSERT(tcp->tcp_rq == q);
			TCP_UNLOCK_READ();
			/*
			 * Stuff for any detached close comes up the
			 * default queue.  If there is any new data in
			 * this segment, it is time to put an end to
			 * this.  We do a special check here where it
			 * is out of the main line, rather than check
			 * if we are detached every time we see new
			 * data down below.
			 */
			if ( seg_len > 0
			&&  SEQ_GT(seg_seq + seg_len, tcp1->tcp_rnxt)) {
				BUMP_MIB(tcp_mib.tcpInClosed);
				tcp_xmit_ctl("new data when detached",
					     tcp1, mp, seg_ack, 0, TH_RST);
				tcp_clean_death(tcp1, EPROTO);
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
				return;
			}
			/*
			 * We're in the right context, and MT safe,
			 * so do it now.
			 */
			tcp = tcp1;
			goto do_detached;
		}
		if (tcp1) {
			/*
			 * Found it.  Put the message on the right
			 * queue to ensure proper synchronization.
			 */
			ASSERT(q != tcp1->tcp_rq);
			lateral_putq(q, tcp1->tcp_rq, mp);
			TCP_UNLOCK_READ();
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
		/* Try to find a listener */
		tcp1 = tcp_lookup_listener(tcph->th_fport, 
					   ((iph_t *)rptr)->iph_dst);
		if (!tcp1) {
			/* No takers.  Generate a proper Reset. */
			TCP_UNLOCK_READ();
			tcp_xmit_listeners_reset(q, mp);
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
		/*
		 * Found someone interested.  If it isn't the current
		 * queue, put it on the right one.
		 */
		if ( tcp1 != tcp ) {
			ASSERT(q != tcp1->tcp_rq);
			lateral_putq(q, tcp1->tcp_rq, mp);
			TCP_UNLOCK_READ();
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
		ASSERT(tcp->tcp_rq == q);
		TCP_UNLOCK_READ();
	}
do_detached:;
	BUMP_LOCAL(tcp->tcp_ibsegs);
	flags = (unsigned int)tcph->th_flags[0] & 0xFF;
	if ((flags & TH_URG) && isput != -1) {
		/*
		 * TCP can't handle urgent pointers that arrive before
		 * the connection has been accept()ed since it can't
		 * buffer OOB data.  Discard segment if this happens.
		 *
		 * Nor can it reassemble urgent pointers, so discard
		 * if it's not the next segment expected.
		 *
		 * Otherwise, collapse chain into one mblk (discard if
		 * that fails).  This makes sure the headers, retransmitted
		 * data, and new data all are in the same mblk.
		 */
		ASSERT(mp != NULL);
		if (tcp->tcp_listener || SEQ_GT(seg_seq, tcp->tcp_rnxt)
		    || !pullupmsg(mp, -1)) {
			freemsg(mp);
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
		/* Update pointers into message */
		orptr = rptr = mp->b_rptr;
		tcph = (tcph_t *)&rptr[IPH_HDR_LENGTH(rptr)];
	}

	switch (tcp->tcp_state) {
	case TCPS_LISTEN:
		if ((flags & (TH_RST | TH_ACK | TH_SYN)) != TH_SYN) {
			if (flags & TH_RST) {
				freemsg(mp);
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
				return;
			}
			if (flags & TH_ACK) {
				tcp_xmit_early_reset("TCPS_LISTEN-TH_ACK",
						     q, mp, seg_ack, 0, TH_RST);
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
				return;
			}
			if (!(flags & TH_SYN)) {
				freemsg(mp);
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
				return;
			}
		}
		if (tcp->tcp_conn_req_max > 0) {
			tcp_conn_request(tcp, mp);
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
		tcp->tcp_irs = seg_seq;
		tcp->tcp_rack = seg_seq;
		tcp->tcp_rnxt = seg_seq + 1;
		U32_TO_ABE32(tcp->tcp_rnxt, ALIGN32(tcp->tcp_tcph->th_ack));
		BUMP_MIB(tcp_mib.tcpPassiveOpens);
		goto syn_rcvd;
	case TCPS_SYN_SENT:
		if (flags & TH_ACK) {
			if (SEQ_LEQ(seg_ack, tcp->tcp_iss)
			||  SEQ_GT(seg_ack, tcp->tcp_snxt)) {
				if (flags & TH_RST) {
					freemsg(mp);
					SYNC_CHK_OUT(otcp, "tcp_rput");
					TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT, "tcp_rput end:  q %X", q);
					return;
				}
				tcp_xmit_ctl("TCPS_SYN_SENT-Bad_seq",
					     tcp, mp, seg_ack, 0, TH_RST);
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
				return;
			}
			if (SEQ_LEQ(tcp->tcp_suna, seg_ack))
				flags |= TH_ACK_ACCEPTABLE;
		}
		if (flags & TH_RST) {
			freemsg(mp);
			if (flags & TH_ACK_ACCEPTABLE)
				tcp_clean_death(tcp, ECONNREFUSED);
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
		if (!(flags & TH_SYN)) {
			freemsg(mp);
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
		{
		u_long mss = tcp_mss_get(tcph);
		if (mss != tcp->tcp_mss)
			tcp_mss_set(tcp, MIN(mss, tcp->tcp_mss));
		}
		tcp->tcp_irs = seg_seq;
		tcp->tcp_rack = seg_seq;
		tcp->tcp_rnxt = seg_seq + 1;
		U32_TO_ABE32(tcp->tcp_rnxt, ALIGN32(tcp->tcp_tcph->th_ack));
		if (flags & TH_ACK_ACCEPTABLE) {
			/*
			 * If we can't get the confirmation upstream, pretend
			 * we didn't even see this one.
			 */
/* XXX: how can we pretend we didn't see it if we have updated rnxt et. al. */
			if (!tcp_conn_con(tcp, (iph_t *)mp->b_rptr, tcph)) {
				freemsg(mp);
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
				return;
			}
			/* One for the SYN */
			tcp->tcp_suna = tcp->tcp_iss + 1;
			tcp->tcp_valid_bits &= ~TCP_ISS_VALID;
			tcp->tcp_state = TCPS_ESTABLISHED;

			tcp->tcp_swl1 = seg_seq;
			tcp->tcp_swl2 = seg_ack;

			new_swnd = BE16_TO_U16(tcph->th_win);
			tcp->tcp_swnd = new_swnd;
			if (new_swnd > tcp->tcp_max_swnd)
				tcp->tcp_max_swnd = new_swnd;

			/*
			 * Always send the three-way handshake ack
			 * immediately in order to make the
			 * connection complete as soon as possible
			 * on the accepting host.
			 */
			flags |= TH_ACK_NEEDED;
			flags &= ~(TH_SYN | TH_ACK_ACCEPTABLE);
			seg_seq++;
			break;
		}
		syn_rcvd:
		tcp->tcp_state = TCPS_SYN_RCVD;
		mp1 = tcp_xmit_mp(tcp, tcp->tcp_xmit_head, tcp->tcp_mss,
				  tcp->tcp_iss, 0);
		if (mp1) {
			putnext(tcp->tcp_wq, mp1);
			TCP_TIMER_RESTART(tcp,
					  tcp->tcp_rto + tcp_conn_grace_period);
		}
#if 0
		/* TODO: check out the entire 'quick connect' sequence */
		if (seg_len > 0  ||  (flags & TH_FIN)) {
			/* TODO: how do we get this off again? */
			noenable(tcp->tcp_rq);
			(void) putq(tcp->tcp_rq, mp);
		} else
#endif
			freemsg(mp);
		SYNC_CHK_OUT(otcp, "tcp_rput");
		TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT, "tcp_rput end:  q %X", q);
		return;
	case TCPS_TIME_WAIT:
 		if (!(flags & TH_SYN))
			break;
		gap = seg_seq - tcp->tcp_rnxt;
		rgap = tcp->tcp_rwnd - (gap + seg_len);
		if (gap > 0 && rgap < 0) {
			/* Make sure that when we accept the connection pick 
			 * a number greater then the rnxt for the old 
			 * connection.
			 *
			 * First, calculate a minimal iss value.
			 */
 			long adj = (tcp->tcp_rnxt + ISS_INCR);

			if (tcp_strong_iss) {
				/* Subtract out min random next iss */
				adj -= gethrtime()/ISS_NSEC_DIV;
				adj -= tcp_iss_incr_extra + 1;
			} else {
				/* Subtract out next iss */
				adj -= ISS_INCR/2;
	 			adj -= (u_long)time_in_secs * ISS_INCR + 
					tcp_iss_incr_extra;
			}
 			if (adj > 0)
				/* New iss not guaranteed to be ISS_INCR
				 * ahead of the current rnxt, so add the
				 * difference to incr_extra just in case.
				 */
 				tcp_iss_incr_extra += adj;
			tcp_clean_death(tcp, 0);
			/*
			 * Have to pass the packet back in since
			 * tcp_clean_death might defer the
			 * cleaning up of this tcp_t.
			 */
			lateral_put(q, q, mp);
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
		break;
	default:
		break;
	}
	mp->b_rptr = (u_char *)tcph + TCP_HDR_LENGTH(tcph);
	urp = BE16_TO_U16(tcph->th_urp) - tcp->tcp_urp_old;
	new_swnd = BE16_TO_U16(tcph->th_win);
try_again:;
	gap = seg_seq - tcp->tcp_rnxt;
	rgap = tcp->tcp_rwnd - (gap + seg_len);
	/*
	 * gap is the amount of sequence space between what we expect to see
	 * and what we got for seg_seq.  A positive value for gap means
	 * something got lost.  A negative value means we got some old stuff.
	 */
	if (gap < 0) {
		/* Old stuff present.  Is the SYN in there? */
		if ( seg_seq == tcp->tcp_irs  &&  (flags & TH_SYN) &&
		    (seg_len != 0)) {
			flags &= ~TH_SYN;
			seg_seq++;
			urp--;
			/* Recompute the gaps after noting the SYN. */
			goto try_again;
		}
		/* Remove the old stuff from seg_len. */
		seg_len += gap;
		/* Anything left? */
		if (seg_len <= 0) {
			seg_seq += (seg_len - gap);
			if (SEQ_GEQ(seg_seq, tcp->tcp_rack)
			&& tcp->tcp_rack_cnt >= tcp->tcp_mss
			&& tcp->tcp_rack_abs_max > (tcp->tcp_mss << 1))
				tcp->tcp_rack_abs_max -= tcp->tcp_mss;
unacceptable:;
			/*
			 * This segment is "unacceptable".  None of its
			 * sequence space lies within our advertized window.
			 * 
			 * Adjust seg_len to be the original value.
			 */
			if (gap < 0)
				seg_len -= gap;
mi_strlog(tcp->tcp_rq, 1, SL_TRACE,
"tcp_rput: unacceptable, gap %d, rgap %d, flags 0x%x, seg_seq %u, seg_len %d, rnxt %u, %s\n"
, gap, rgap, flags, seg_seq, seg_len, tcp->tcp_rnxt, tcp_display(tcp));
  
			tcp->tcp_rack_cur_max = tcp->tcp_mss;

			/*
			 * Resets are only valid if they lie within our offered
			 * window.  If the RST bit is set, we just ignore this
			 * segment.
			 */
			if (flags & TH_RST) {
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
				freemsg(mp);
				return;
			}

			/*
			 * Arrange to send an ACK in response to the
			 * unacceptable segment per RFC 793 page 69.  However,
			 * don't send an ACK if the unacceptable segment was
			 * was an ACK only packet (i.e. carried no data), since
			 * doing so may cause "ACK wars".
                         */
			if (seg_len != 0
			    || gap != 0
			    || rgap != tcp->tcp_rwnd
			    || (flags & (TH_SYN | TH_FIN | TH_URG))
			    != 0)
				flags |=  TH_ACK_NEEDED;

                        /*
			 * Continue processing this segment in order to use the
			 * ACK information it contains, but skip all other
			 * sequence-number processing.  Processing the ACK
			 * information is necessary is necessary in order to
			 * re-syncronize connections that may have lost
			 * syncronization.
			 * We clear seg_len and flag fields related to
			 * sequence number processing as they are not
			 * to be trusted for an unacceptable segment.
			 */
			seg_len = 0;
			flags &= ~(TH_SYN | TH_FIN | TH_URG);
			goto process_ack;
		}

		/* Fix seg_seq, and chew the gap off the front. */
		seg_seq = tcp->tcp_rnxt;
		urp += gap;
		do {
			mblk_t	* mp2;
			gap += mp->b_wptr - mp->b_rptr;
			if (gap > 0) {
				mp->b_rptr = mp->b_wptr - gap;
				break;
			}
			mp2 = mp;
			mp = mp->b_cont;
			freeb(mp2);
		} while (gap < 0);
		/*
		 * If the urgent data has already been acknowledged, we
		 * should ignore TH_URG below
		 */
		if (urp < 0)
			flags &= ~TH_URG;
	}
	/*
	 * rgap is the amount of stuff received out of window.  A negative
	 * value is the amount out of window.
	 */
	if (rgap < 0) {
		mblk_t	* mp2;
		if (tcp->tcp_rwnd == 0)
			BUMP_MIB(tcp_mib.tcpInWinProbe);
		else {
			BUMP_MIB(tcp_mib.tcpInDataPastWinSegs);
			UPDATE_MIB(tcp_mib.tcpInDataPastWinBytes, -rgap);
		}
		if (flags & TH_FIN) {
			/*
			 * seg_len does not include the FIN, so if more than
			 * just the FIN is out of window, we act like we don't
			 * see it.  (If just the FIN is out of window, rgap
			 * will be zero and we will go ahead and acknowledge
			 * the FIN.)
			 */
			flags &= ~TH_FIN;
		}
		/* Fix seg_len and make sure there is something left. */
		seg_len += rgap;
		if (seg_len <= 0) {
			/* Adjust seg_len to be the original value */
			seg_len -= rgap;
			goto unacceptable;
		}
		/* Pitch out of window stuff off the end. */
		rgap = seg_len;
		mp2 = mp;
		do {
			rgap -= mp2->b_wptr - mp2->b_rptr;
			if (rgap < 0) {
				mp2->b_wptr += rgap;
				if ((mp1 = mp2->b_cont) != NULL) {
					mp2->b_cont = nilp(mblk_t);
					freemsg(mp1);
				}
				break;
			}
		} while ((mp2 = mp2->b_cont) != NULL);
	}
ok:;
	if (seg_seq != tcp->tcp_rnxt  ||  tcp->tcp_reass_head) {
		/*
		 * Clear the FIN bit in case it was set since we can not
		 * handle out-of-order FIN yet. This will cause the remote
		 * to retransmit the FIN.
		 * TODO: record the out-of-order FIN in the reassembly
		 * queue to avoid the remote having to retransmit.
		 */	
		flags &= ~TH_FIN;
		if ( seg_len > 0 ) {
			u_int	lflags = flags;
 			/*
 			 * Attempt reassembly and see if we have something
 			 * ready to go.
 			 */
			mp = tcp_reass(tcp, mp, seg_seq, &lflags);
			flags = lflags;
			/* Always ack out of order packets */
			flags |= TH_ACK_NEEDED | TH_PSH;
			if ( mp ) {
				seg_len = mp->b_cont ? msgdsize(mp) :
					mp->b_wptr - mp->b_rptr;
				seg_seq = tcp->tcp_rnxt;
			} else {
				/*
				 * Keep going even with nil mp.
				 * There may be a useful ACK or something else 
				 * we don't want to miss.
				 */
				seg_len = 0;
			}
		}
	} else if (seg_len > 0) {
		BUMP_MIB(tcp_mib.tcpInDataInorderSegs);
		UPDATE_MIB(tcp_mib.tcpInDataInorderBytes, seg_len);
	}
	if ((flags & (TH_RST | TH_SYN | TH_URG | TH_ACK)) != TH_ACK) {
	if (flags & TH_RST) {
		if ( mp )
			freemsg(mp);
		switch (tcp->tcp_state) {
		case TCPS_SYN_RCVD:
			tcp_clean_death(tcp, ECONNREFUSED);
			break;
		case TCPS_ESTABLISHED:
		case TCPS_FIN_WAIT_1:
		case TCPS_FIN_WAIT_2:
		case TCPS_CLOSE_WAIT:
			tcp_clean_death(tcp, ECONNRESET);
			break;
		case TCPS_CLOSING:
		case TCPS_LAST_ACK:
		case TCPS_TIME_WAIT:
			tcp_clean_death(tcp, 0);
			break;
		default:
			tcp_clean_death(tcp, ENXIO);
			break;
		}
		SYNC_CHK_OUT(otcp, "tcp_rput");
		TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT, "tcp_rput end:  q %X", q);
		return;
	}
	if (flags & TH_SYN) {
		if (seg_seq == tcp->tcp_irs) {
			flags &= ~TH_SYN;
			seg_seq++;
		} else {
			if (mp != NULL) {
				freemsg(mp);
			}
			if (tcp->tcp_state == TCPS_SYN_RCVD
			&&  tcp->tcp_passive_open) {
				tcp_clean_death(tcp, ECONNREFUSED);
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
				return;
			}
			tcp_xmit_ctl("TH_SYN", tcp, NULL, seg_ack, 0, TH_RST);
			tcp_clean_death(tcp, ECONNRESET);
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
	}
	/*
	 * urp could be -1 when the urp field in the packet is 0
	 * and tcp_urp_old is set. This implies that the urgent byte
	 * was at seg_seq - 1, in which case we ignore the urgent flag.
	 */
	if (flags & TH_URG && urp >= 0) {
		if ( !tcp->tcp_urp_sig_sent ) {
			/* If we haven't generated the signal yet, do it now. */
			if (!putctl1(q, M_PCSIG, SIGURG)) {
				/* Try again on the rexmit. */
				freemsg(mp);
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
				return;
			}
			tcp->tcp_urp_sig_sent = true;
		}
		/*
		 * If the urgent byte is in this segment, make sure that it is
		 * all by itself.  This makes it much easier to deal with the
		 * possibility of an allocation failure on the T_exdata_ind.
		 * Note that seg_len is the number of bytes in the segment, and
		 * urp is the offset into the segment of the urgent byte.
		 * urp < seg_len means that the urgent byte is in this segment.
		 */
		if ( urp < seg_len ) {
			if ( seg_len != 1 ) {
				/* Break it up and feed it back in. */
				mp->b_rptr = orptr;
				if ( urp > 0 ) {
					/*
					 * There is stuff before the urgent
					 * byte.
					 */
					mp1 = dupmsg(mp);
					if ( !mp1 ) {
						/*
						 * Trim from urgent byte on.
						 * The rest will come back.
						 */
						adjmsg(mp, urp - seg_len);
						tcp_rput_data(q, mp, -1);
						SYNC_CHK_OUT(otcp, "tcp_rput");
					TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
						return;
					}
					adjmsg(mp1, urp - seg_len);
					/* Feed this piece back in. */
					tcp_rput_data(q, mp1, -1);
				}
				if ( urp != seg_len - 1 ) {
					/*
					 * There is stuff after the urgent
					 * byte.
					 */
					mp1 = dupmsg(mp);
					if ( !mp1 ) {
						/*
						 * Trim everything beyond the
						 * urgent byte.  The rest will
						 * come back.
						 */
						adjmsg(mp, urp + 1 - seg_len);
						tcp_rput_data(q, mp, -1);
						SYNC_CHK_OUT(otcp, "tcp_rput");
					TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
						return;
					}
					adjmsg(mp1, urp + 1 - seg_len);
					tcp_rput_data(q, mp1, -1);
				}
				tcp_rput_data(q, mp, -1);
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
				return;
			}
			/*
			 * This segment contains only the urgent byte.  We
			 * have to allocate the T_exdata_ind, if we can.
			 */
			if ( !tcp->tcp_urp_mp ) {
				struct T_exdata_ind * tei;
				mp1 = allocb(sizeof(struct T_exdata_ind),
					BPRI_MED);
				if ( !mp1 ) {
					/* Sigh... It'll be back. */
					freemsg(mp);
					SYNC_CHK_OUT(otcp, "tcp_rput");
					TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
					"tcp_rput end:  q %X", q);
					return;
				}
				mp1->b_datap->db_type = M_PROTO;
				tei = (struct T_exdata_ind *)ALIGN32(
								mp1->b_rptr);
				tei->PRIM_type = T_EXDATA_IND;
				tei->MORE_flag = 0;
				mp1->b_wptr = (u_char *)&tei[1];
				tcp->tcp_urp_mp = mp1;
			}
			/*
			 * Now we are all set.  On the next putnext upstream,
			 * tcp_urp_mp will be non-nil and will get prepended
			 * to what has to be this piece containing the urgent
			 * byte.  If for any reason we abort this segment below,
			 * if it comes back, we will have this ready, or it
			 * will get blown off in close.
			 */
		}
	}

 process_ack:
	if (!(flags & TH_ACK)) {
		if ( mp )
			freemsg(mp);
		goto xmit_check;
	}
	}
	bytes_acked = (i32)(seg_ack - tcp->tcp_suna);
	if (tcp->tcp_state == TCPS_SYN_RCVD) {
		/*
		 * NOTE: RFC 793 pg. 72 says this should be 'bytes_acked < 0'
		 * but that would mean we have an ack that ignored our SYN.
		 */
		if (bytes_acked < 1
		|| SEQ_GT(seg_ack, tcp->tcp_snxt)) {
			freemsg(mp);
			tcp_xmit_ctl("TCPS_SYN_RCVD-bad_ack", tcp, NULL,
			    seg_ack, 0, TH_RST);
			SYNC_CHK_OUT(otcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				"tcp_rput end:  q %X", q);
			return;
		}
		tcp->tcp_suna = tcp->tcp_iss + 1;	/* One for the SYN */
		bytes_acked--;
		if (tcp->tcp_eager_conn_ind) {
			/* 3-way handshake complete - pass up the T_CONN_IND */
			putnext(tcp->tcp_rq, tcp->tcp_eager_conn_ind);
			tcp->tcp_eager_conn_ind = nilp(mblk_t);
		}
		/*
		 * We set the send window to zero here.
		 * This is needed if there is data to be
		 * processed already on the queue.
		 * Later (at swnd_update label), the
		 * "new_swnd > tcp_swnd" condition is satisfied
		 * the XMIT_NEEDED flag is set in the current
		 * (SYN_RCVD) state. This ensures tcp_wput_slow is
		 * called if there is already data on queue in
		 * this state.
		 */
		tcp->tcp_swnd = 0;

		if (new_swnd > tcp->tcp_max_swnd)
			tcp->tcp_max_swnd = new_swnd;
		tcp->tcp_swl1 = seg_seq;
		tcp->tcp_swl2 = seg_ack;
		tcp->tcp_state = TCPS_ESTABLISHED;
		tcp->tcp_valid_bits &= ~TCP_ISS_VALID;
#if 0
		/*
		 * TODO: check out entire 'quick connect' sequence, we are
		 * probably better off hand concatenating these two than
		 * recurring.
		 */
		enableok(tcp->tcp_rq);
		if (mp1 = getq(tcp->tcp_rq)) {
			rptr = mp1->b_rptr;
			tcph = (tcph_t *)&rptr[IPH_HDR_LENGTH((iph_t *)rptr)];
			if (!(tcph->th_flags[0] & TH_ACK)) {
				u_long	dummy_ack = tcp->tcp_suna - 1;
				U32_TO_BE32(dummy_ack, tcph->th_ack);
				tcph->th_flags[0] |= TH_ACK;
			}
			tcph->th_flags[0] &= ~TH_SYN;
			/* TODO: recursion problems?? */
			tcp_rput(tcp->tcp_rq, mp1);
		}
#endif
	}
	if (bytes_acked < 0)
		goto est;
	mp1 = tcp->tcp_xmit_head;
	if (bytes_acked == 0) {
		if (new_swnd == tcp->tcp_swnd && seg_len == 0 &&
		    seg_ack == tcp->tcp_suna) {
			BUMP_MIB(tcp_mib.tcpInDupAck);
			/*
			 * Fast retransmit.  When we have seen exactly three
			 * indentical ACKs while we have unACKed data 
			 * outstanding we take it as a hint that our peer 
			 * dropped something.
			 */
			if (mp1 && tcp->tcp_suna != tcp->tcp_snxt) {
				if (++tcp->tcp_dupack_cnt == 
				    tcp_dupack_fast_retransmit) {
					BUMP_MIB(tcp_mib.tcpOutFastRetrans);
					/*
					 * Adjust cwnd since the duplicate 
					 * ack indicates that a packet was
					 * dropped (due to congestion.)
					 */
					tcp->tcp_cwnd_ssthresh = tcp->tcp_cwnd >> 1;
					tcp->tcp_cwnd = tcp->tcp_mss;
					/* 
					 * After the packet has been sent we
					 * increase cwnd.
					 */
					flags |= TH_REXMIT_NEEDED | 
						TH_READJ_CWND;
				} else if (tcp->tcp_dupack_cnt >
					   tcp_dupack_fast_retransmit) {
					/* 
					 * We know that one more packet has
					 * left the pipe thus we can update
					 * cwnd.
					 */
					/*
					 * TODO: reno unconditionally does
					 * cwnd += mss and re-adjusts this
					 * when it stops getting duplicate
					 * acks!
					 */
					u_long cwnd = tcp->tcp_cwnd;
					if (cwnd < tcp->tcp_cwnd_ssthresh
					    || ((tcp->tcp_cwnd_bytes_acked += 
						 bytes_acked) >= cwnd)) {
						tcp->tcp_cwnd_bytes_acked = 0;
						cwnd += tcp->tcp_mss;
						if (cwnd > tcp->tcp_cwnd_max)
							cwnd = tcp->tcp_cwnd_max;
						tcp->tcp_cwnd = cwnd;
					}
				}
			} else
				tcp->tcp_dupack_cnt = 0;
		} else if (tcp->tcp_zero_win_probe && new_swnd != 0) {
			/* tcp_suna != tcp_snxt */
			/* Packet contains a window update */
			BUMP_MIB(tcp_mib.tcpInWinUpdate);
			tcp->tcp_zero_win_probe = 0;
			tcp->tcp_dupack_cnt = 0;
			/* Transmit starting with tcp_suna */
			flags |= TH_REXMIT_NEEDED;
		} else
			tcp->tcp_dupack_cnt = 0;
		goto swnd_update;
	} 
	tcp->tcp_dupack_cnt = 0;

	/*
	 * Check for "acceptability" of ACK value per RFC 793, pages 72 - 73.
	 * If the ACK value acks something that we have not yet sent, it might
	 * be an old duplicate segment.  Send an ACK to re-syncronize the other
	 * side.  Note: reset in response to unacceptable ACK in SYN_RECEIVE
	 * state is handled above, so we can always just drop the segment and
	 * send an ACK here.
	 *
	 * Should we send ACKs in response to ACK only segments?
	 */
	if (SEQ_GT(seg_ack, tcp->tcp_snxt)) {
		/* drop the received segment */
		if (mp)
			freemsg(mp);
		
		/* send an ACK */
		mp = tcp_ack_mp(tcp);
		if (mp) {
			putnext(tcp->tcp_wq, mp);
			BUMP_LOCAL(tcp->tcp_obsegs);
			BUMP_MIB(tcp_mib.tcpOutAck);
		}
		SYNC_CHK_OUT(otcp, "tcp_rput");
		TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
			"tcp_rput end:  q %X", q);
		return;
	}

	BUMP_MIB(tcp_mib.tcpInAckSegs);
	UPDATE_MIB(tcp_mib.tcpInAckBytes, bytes_acked);
	tcp->tcp_suna = seg_ack;
	tcp->tcp_zero_win_probe = 0;
	if ( !mp1 ) {
		/*
		 * Something was acked, but we had no transmitted data
		 * outstanding.  Either the FIN or the SYN must have been
		 * acked.  If it was the FIN, we want to note that.
		 * The TCP_FSS_VALID bit will be on in this case.  Otherwise
		 * it must have been the SYN acked, and we have nothing
		 * special to do here.
		 */
		if ( tcp->tcp_fin_sent )
			tcp->tcp_fin_acked = true;
		else if ( !(tcp->tcp_valid_bits & TCP_ISS_VALID ) )
			BUMP_MIB(tcp_mib.tcpInAckUnsent);
		goto swnd_update;
	}

	/* Update the congestion window */
	{
	u_long cwnd = tcp->tcp_cwnd;
	if (cwnd < tcp->tcp_cwnd_ssthresh
	|| ((tcp->tcp_cwnd_bytes_acked += bytes_acked) >= cwnd)) {
		tcp->tcp_cwnd_bytes_acked = 0;
		cwnd += tcp->tcp_mss;
		if (cwnd > tcp->tcp_cwnd_max)
			cwnd = tcp->tcp_cwnd_max;
		tcp->tcp_cwnd = cwnd;
	}
	}

	/* See if the latest urgent data has been acknowledged */
	if ((tcp->tcp_valid_bits & TCP_URG_VALID)
	&&  SEQ_GT(seg_ack, tcp->tcp_urg))
		tcp->tcp_valid_bits &= ~TCP_URG_VALID;

	old_rto = tcp->tcp_rto;
	/* No rexmits in flight */
	if (SEQ_GT(seg_ack, tcp->tcp_csuna)) {
		/*
		 * We have a first time ACK for a sequence of byte(s)
		 * for one (or more) unACKed segment(s). Each segment
		 * is time stamped as transmited by storing current
		 * tick value in the corresponding mblks b_prev member.
		 * The rtt is the difference of the current tick value
		 * and the first unACKed segment (this could be an ACK
		 * for more then one segment).
		 *
		 * Save this rtt if it is the maximum rtt for the current
		 * rtt sample period. The current rtt sample period is
		 * based on the current smoothed rtt average.
		 *
		 * If the current rtt sample peroid is up, then use the
		 * maximum rtt to update the smoothed average and vairance.
		 *
		 * Note: the standard method for updating the smoothed rtt
		 *	 values is to measure the rtt of an arbitrary seg.
		 *	 Instead, we measure the rtt of all segment(s) and
		 *	 once per smoothed rtt period use the maximum rtt.
		 *	 This yields a more conservative rto when there are
		 *	 spikes in the rtt that last less than a full rtt.
		 *
		 * Note: we handle the lbolt wrapping buy using integer
		 *	 subraction for comparing two lbolt values, this
		 *	 inturn assumes that the longest rtt sample period
		 *	 in ticks is less then 1/2 the largest unsigned long,
		 *	 for a 32 bit unsigned long and a 100Hz ticker this
		 *	 is > 248 days.
		 */
		long rtt = lbolt - (long)mp1->b_prev;
		/* Save new rtt as maximum */
		if (rtt > tcp->tcp_rtt_mt)
			tcp->tcp_rtt_mt = rtt;
		/* Remeber the last sequence to be ACKed */
		tcp->tcp_csuna = seg_ack;
		/* Update smoothed rtt avarage and vairance */
		if ((lbolt - tcp->tcp_rtt_nt) >= 0) {
			long m = LBOLT_TO_MS(tcp->tcp_rtt_mt);
			long sa = tcp->tcp_rtt_sa;
			long sd;
			BUMP_MIB(tcp_mib.tcpRttUpdate);
			m -= sa >> 3;
			sa += m;
			if (m < 0)
				m = -m;
			sd = tcp->tcp_rtt_sd;
			m -= sd >> 2;
			sd += m;
			tcp->tcp_rtt_sa = sa;
			tcp->tcp_rtt_sd = sd;
			sa >>= 3;	/* rto = sa + sd * 4 */
			sa += sd;
			tcp->tcp_rto = sa;
			/* Set next Rtt sample period to current smoothed rtt */
			tcp->tcp_rtt_nt = lbolt + MS_TO_TICKS(tcp->tcp_rto);
			tcp->tcp_rtt_mt = 0;
		}
	} else {
		BUMP_MIB(tcp_mib.tcpRttNoUpdate);
	}
	/*
	 * Keep the timer interval within bounds.  We crowbar tcp_rto since
	 * we set the timer from it, even though this may warp our round trip
	 * estimate at the extremes.
	 */
	if (tcp->tcp_rto < tcp_rexmit_interval_min)
		tcp->tcp_rto = tcp_rexmit_interval_min;
	else if (tcp->tcp_rto > tcp_rexmit_interval_max)
		tcp->tcp_rto = tcp_rexmit_interval_max;

	/* Eat acknowledged bytes off the xmit queue. */
	for (;;) {
		mblk_t	* mp2;
		u_char	* wptr;

		wptr = mp1->b_wptr;
		bytes_acked -= wptr - mp1->b_rptr;
		if (bytes_acked < 0) {
			mp1->b_rptr = wptr + bytes_acked;
			break;
		}
		mp1->b_prev = nilp(mblk_t);
		mp2 = mp1;
		mp1 = mp1->b_cont;
		freeb(mp2);
		if (bytes_acked == 0) {
			if (!mp1)
				goto pre_swnd_update;
			if (mp2 != tcp->tcp_xmit_tail)
				break;
			tcp->tcp_xmit_tail = mp1;
			tcp->tcp_xmit_tail_unsent = mp1->b_wptr - mp1->b_rptr;
			break;
		}
		if (!mp1) {
			/* TODO check that tcp_fin_sent has been set */
			/*
			 * More was acked but there is nothing more
			 * outstanding.  This means that the FIN was
			 * just acked or that we're talking to a clown.
			 */
			if (tcp->tcp_fin_sent)
				tcp->tcp_fin_acked = true;
			else
				BUMP_MIB(tcp_mib.tcpInAckUnsent);
			goto pre_swnd_update;
		}
	}
	/* Is the next packet on the rexmit list already past due? */
	if (tcp->tcp_suna != tcp->tcp_snxt
	&& LBOLT_TO_MS((long)lbolt - (long)mp1->b_prev) > old_rto)
		flags |= TH_REXMIT_NEEDED;
	else if ( tcp->tcp_unsent )
		flags |= TH_XMIT_NEEDED;
pre_swnd_update:
	tcp->tcp_xmit_head = mp1;
swnd_update:
	if (SEQ_LT(tcp->tcp_swl1, seg_seq)
	||  (tcp->tcp_swl1 == seg_seq  &&  SEQ_LEQ(tcp->tcp_swl2, seg_ack))) {
		if (tcp->tcp_unsent  &&  new_swnd > tcp->tcp_swnd)
			flags |= TH_XMIT_NEEDED;
		/*
		 * When we receive an ack e.g. during zero-window probe.
		 * Stop timer to avoid giving up.
		 */
		tcp->tcp_ms_we_have_waited = 0;
		tcp->tcp_swnd = new_swnd;
		tcp->tcp_swl1 = seg_seq;
		tcp->tcp_swl2 = seg_ack;
	}
est:
	if (tcp->tcp_state > TCPS_ESTABLISHED) {
		switch (tcp->tcp_state) {
		case TCPS_FIN_WAIT_1:
			if (tcp->tcp_fin_acked) {
				tcp->tcp_state = TCPS_FIN_WAIT_2;
				/*
				 * We implement the non-standard BSD/SunOS
				 * FIN_WAIT_2 flushing algorithm.
				 * If there is no user attached to this
				 * TCP endpoint, then this TCP struct
				 * could hang arround forever in FIN_WAIT_2
				 * state if the peer forgets to send us
				 * a FIN.  To prevent this, we wait only
				 * 2*MSL (a convenient time value) for
				 * the FIN to arrive.  If it doesn't show up,
				 * we flush the TCP endpoint.  This algorithm,
				 * though a violation of RFC-793, has worked
				 * for over 10 years in BSD systems.
				 * Note: SunOS 4.x waits 675 seconds before
				 * flushing the FIN_WAIT_2 connection.
				 */
				TCP_TIMER_RESTART(tcp,
					tcp_fin_wait_2_flush_interval);
			}
			break;
		case TCPS_FIN_WAIT_2:
			break;	/* Shutdown hook? */
		case TCPS_LAST_ACK:
			if ( mp ) {
				freemsg(mp);
				mp = NULL;
			}
			if (tcp->tcp_fin_acked) {
				tcp_clean_death(tcp, 0);
				SYNC_CHK_OUT(otcp, "tcp_rput");
				TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT,
				    "tcp_rput end:  q %X", q);
				return;
			}
			goto xmit_check;
		case TCPS_CLOSING:
			if (tcp->tcp_fin_acked) {
				tcp->tcp_state = TCPS_TIME_WAIT;
				TCP_TIMER_RESTART(tcp, tcp_close_wait_interval);
			}
			/*FALLTHRU*/
		case TCPS_TIME_WAIT:
		case TCPS_CLOSE_WAIT:
			if ( mp ) {
				freemsg(mp);
				mp = NULL;
			}
			goto xmit_check;
		default:
			break;
		}
	}
	if (flags & TH_FIN) {
		/* Make sure we ack the fin */
		flags |= TH_ACK_NEEDED;
		if (!tcp->tcp_fin_rcvd) {
			tcp->tcp_fin_rcvd = true;
			tcp->tcp_rnxt++;
			tcph = tcp->tcp_tcph;
			U32_TO_ABE32(tcp->tcp_rnxt, ALIGN32(tcph->th_ack));

			/*
			 * Generate the ordrel_ind at the end unless we
			 * are an eager guy.
			 * In the eager case tcp_rsrv will do this when run
			 * after tcp_accept is done.
			 */
			if (tcp->tcp_listener == NULL)
				flags |= TH_ORDREL_NEEDED;
			switch (tcp->tcp_state) {
			case TCPS_SYN_RCVD:
			case TCPS_ESTABLISHED:
				tcp->tcp_state = TCPS_CLOSE_WAIT;
				/* Keepalive? */
				break;
			case TCPS_FIN_WAIT_1:
				if (!tcp->tcp_fin_acked) {
					tcp->tcp_state = TCPS_CLOSING;
					break;
				}
				/* FALLTHRU */
			case TCPS_FIN_WAIT_2:
				tcp->tcp_state = TCPS_TIME_WAIT;
				TCP_TIMER_RESTART(tcp, tcp_close_wait_interval);
				if ( seg_len ) {
					/*
					 * implies data piggybacked on FIN.
					 * break to handle data.
					 */
					break;
				}
				if ( mp )
					freemsg(mp);
				mp = NULL;
				goto ack_check;
			}
		}
	}
	if ( !mp )
		goto xmit_check;
	if (seg_len == 0) {
		freemsg(mp);
		goto xmit_check;
	}
	tcp->tcp_rack_cnt += seg_len;
	{
		u_long cur_max;
		cur_max = tcp->tcp_rack_cur_max;
		if (tcp->tcp_rack_cnt >= cur_max) {
			/*
			 * We have more unacked data than we should - send
			 * an ACK now.
			 */
			flags |= TH_ACK_NEEDED;
			cur_max += tcp->tcp_mss;
			if (cur_max > tcp->tcp_rack_abs_max)
				cur_max = tcp->tcp_rack_abs_max;
			tcp->tcp_rack_cur_max = cur_max;
		} else if ( seg_len < tcp->tcp_mss ) {
			/*
			 * If we get a segment that is less than an mss, and
			 * we already have unacknowledged data, and the amount
			 * unacknowledged is not a multiple of mss, then we
			 * better generate an ACK now.
			 * Otherwise, this may be the tail piece of a
			 * transaction, and we would rather wait for
			 * the response.
			 */
			u_long udif = tcp->tcp_rnxt - tcp->tcp_rack;
			if ( udif  &&  (udif % tcp->tcp_mss) )
				flags |= TH_ACK_NEEDED;
			else
				flags |= TH_TIMER_NEEDED;
		} else {
			/* Start delayed ack timer */
			flags |= TH_TIMER_NEEDED;
		}
	}
	tcp->tcp_rnxt += seg_len;
	tcph = tcp->tcp_tcph;
	U32_TO_ABE32(tcp->tcp_rnxt, ALIGN32(tcph->th_ack));

	if ( tcp->tcp_urp_mp ) {
		tcp->tcp_urp_mp->b_cont = mp;
		mp = tcp->tcp_urp_mp;
		tcp->tcp_urp_mp = nilp(mblk_t);
		tcp->tcp_urp_sig_sent = false;
	}

	if ( tcp->tcp_listener ) {
		/* XXX: handle urgent data here? */
		/*
		 * Side queue inbound data until the accept happens.
		 * tcp_rsrv drains this when the accept happens.
		 */
		if ( tcp->tcp_rcv_head == nilp(mblk_t) )
			tcp->tcp_rcv_head = mp;
		else
			tcp->tcp_rcv_tail->b_cont = mp;
		while (mp->b_cont)
			mp = mp->b_cont;
		tcp->tcp_rcv_tail = mp;

		tcp->tcp_rcv_cnt += seg_len;
		tcp->tcp_credit -= seg_len;
		tcp->tcp_rwnd -= seg_len;
		tcph = tcp->tcp_tcph;
		U32_TO_ABE16(tcp->tcp_rwnd, ALIGN16(tcph->th_win));
	} else {
		int credit = tcp->tcp_credit - seg_len;

		tcp->tcp_credit = credit;
		if ( credit < 0 ) {
			/*
			 * We only call canput periodically.  The frequency
			 * is determined by the value of tcp_credit_init,
			 * which is some multiple of tcp->tcp_mss.  If canput
			 * fails, we continue to call putnext, since there is
			 * no benefit to holding on to the data here.  However,
			 * we do begin to shrink the advertised receive window.
			 * When we get back-enabled, we reopen the window, and
			 * reinitialize tcp->tcp_credit, and we will restart
			 * periodic calls to canput.
			 */
			if ( (credit + seg_len) >= 0
			&&   (
#ifdef TCP_PERF
			      tcp_canput_cnt++,
#endif
			      (isput == 0 || canput(q->q_next))) ) {
				tcp->tcp_credit = tcp->tcp_credit_init;
				goto put_it;
			}
			/*
			 * Flow control from above.  Shrink the receive window.
			 */
			tcp->tcp_rwnd -= seg_len;
			U32_TO_ABE16(tcp->tcp_rwnd,
				     ALIGN16(tcp->tcp_tcph->th_win));
		}
put_it:;
#ifdef TCP_PERF
		tcp_rput_cnt++;
#endif
		rcv_cnt = tcp->tcp_rcv_cnt;
		if (mp->b_datap->db_type != M_DATA) {
			if (rcv_cnt != 0) {
#ifdef TCP_PERF
				tcp_rput_putnext++;
#endif
				MI_HRT_INCREMENT(tcp->tcp_rtime, rtime, 0);
				putnext(q, tcp->tcp_rcv_head);
				MI_HRT_SET(rtime);
			}
			tcp->tcp_rcv_head = nilp(mblk_t);
			rcv_cnt = 0;
#ifdef TCP_PERF
			tcp_rput_putnext++;
#endif
			MI_HRT_INCREMENT(tcp->tcp_rtime, rtime, 0);
			putnext(q, mp);
			MI_HRT_SET(rtime);
			tcp->tcp_co_norm = 1;
		} else if ((flags & (TH_PSH|TH_FIN)) ||
			   rcv_cnt + seg_len >= tcp_rcv_push_wait) {
			if (isput) {
#ifdef TCP_PERF
				if ((flags & (TH_PSH|TH_FIN)) == 0)
					tcp_rwnd_cnt++;
				else
					tcp_psh_cnt++;
#endif
				if (rcv_cnt != 0) {
					tcp->tcp_rcv_tail->b_cont = mp;
					mp = tcp->tcp_rcv_head;
				}
				tcp->tcp_rcv_head = nilp(mblk_t);
				rcv_cnt = 0;
				MI_HRT_INCREMENT(tcp->tcp_rtime, rtime, 0);
				putnext(q, mp);
				MI_HRT_SET(rtime);
				tcp->tcp_co_norm = 1;
			} else {
				/*
				 * Processing an mblk from the co queue, just
				 * enque it, the caller will do the push later.
				 */
#ifdef TCP_PERF
				tcp_rput_queue++;
#endif
				if (rcv_cnt != 0) {
					tcp->tcp_rcv_tail->b_cont = mp;
				} else {
					tcp->tcp_rcv_head = mp;
				}
				while (mp->b_cont)
					mp = mp->b_cont;
				tcp->tcp_rcv_tail = mp;
				rcv_cnt += seg_len;
			}
		} else {
#ifdef TCP_PERF
			tcp_rput_queue++;
#endif
			if (rcv_cnt != 0) {
				tcp->tcp_rcv_tail->b_cont = mp;
			} else {
				tcp->tcp_rcv_head = mp;
			}
			while (mp->b_cont)
				mp = mp->b_cont;
			tcp->tcp_rcv_tail = mp;
			rcv_cnt += seg_len;
		}
		tcp->tcp_rcv_cnt = rcv_cnt;
		/*
		 * Make sure the timer is running if we have data waiting
		 * for a push bit. This provides resiliency against
		 * implementations that do not correctly generate push bits.
		 */
		if (rcv_cnt && isput)
			flags |= TH_TIMER_NEEDED;
	}
xmit_check:
	/* Is there anything left to do? */
	if ((flags & (TH_REXMIT_NEEDED|TH_XMIT_NEEDED|TH_ACK_NEEDED|
		      TH_TIMER_NEEDED|TH_ORDREL_NEEDED)) == 0)
		goto done;

	/* Any transmit work to do and a non-zero window? */
	if ((flags & (TH_REXMIT_NEEDED|TH_XMIT_NEEDED)) &&
	    tcp->tcp_swnd != 0) {
		if (flags & TH_REXMIT_NEEDED) {
			u_long mss = tcp->tcp_snxt - tcp->tcp_suna;
			if (mss > tcp->tcp_mss)
				mss = tcp->tcp_mss;
			if (mss > tcp->tcp_swnd)
				mss = tcp->tcp_swnd;
			/*
			 * Make sure we send all of mss in order to
			 * send things in order when both REXMIT and XMIT
			 * are needed. This occurs when we get a window update
			 * after having probed a zero window.
			 */
			mp1 = tcp_xmit_mp(tcp, tcp->tcp_xmit_head, mss,
					  tcp->tcp_suna, 1);
			if (mp1) {
				tcp->tcp_xmit_head->b_prev = (mblk_t *)lbolt;
				tcp->tcp_csuna = tcp->tcp_snxt;
				BUMP_MIB(tcp_mib.tcpRetransSegs);
				UPDATE_MIB(tcp_mib.tcpRetransBytes,
					   msgdsize(mp1->b_cont));
				MI_HRT_INCREMENT(tcp->tcp_rtime, rtime, 1);
				putnext(tcp->tcp_wq, mp1);
#ifdef	MI_HRTIMING
			} else {
				MI_HRT_INCREMENT(tcp->tcp_rtime, rtime, 1);
#endif /* MI_HRTIMING */
			}
			if (flags & TH_READJ_CWND) {
				/* We were doing a fast retransmit */
				tcp->tcp_cwnd = tcp->tcp_cwnd_ssthresh + 
					tcp->tcp_mss * tcp->tcp_dupack_cnt;
			}
		}
		if (flags & TH_XMIT_NEEDED)
			tcp_wput_slow(tcp, nilp(mblk_t));

		/* Anything more to do? */
		if ((flags &
		     (TH_ACK_NEEDED|TH_TIMER_NEEDED|TH_ORDREL_NEEDED)) == 0)
			goto done;
	}
ack_check:
	if (flags & TH_ACK_NEEDED) {
		/*
		 * Time to send an ack for some reason.
		 */
		mp1 = tcp_ack_mp(tcp);

		if ( mp1 ) {
			MI_HRT_INCREMENT(tcp->tcp_rtime, rtime, 1);
			putnext(tcp->tcp_wq, mp1);
			BUMP_LOCAL(tcp->tcp_obsegs);
			BUMP_MIB(tcp_mib.tcpOutAck);
		}
	}
	if (flags & TH_TIMER_NEEDED) {
		/*
		 * Arrange for deferred ACK or push wait timeout.
		 * Start timer if it is not already running.
		 * TODO: Have a tcp_timer_running bit to simplify this test?
		 */
		if (tcp->tcp_snxt == tcp->tcp_suna
		&&  tcp->tcp_unsent == 0
		&& (!(tcp->tcp_valid_bits & TCP_FSS_VALID)
		  ||  tcp->tcp_fin_acked))
			TCP_TIMER_RESTART(tcp, tcp_deferred_ack_interval);
	}
	if (flags & TH_ORDREL_NEEDED) {
		/*
		 * Send up the ordrel_ind unless we are an eager guy.
		 * In the eager case tcp_rsrv will do this when run
		 * after tcp_accept is done.
		 */
		ASSERT(tcp->tcp_listener == NULL);
		if ((mp = tcp->tcp_rcv_head) != nilp(mblk_t)) {
			/*
			* Push any mblk(s) enqued from co processing.
			*/
#ifdef TCP_PERF
			tcp_psh_cnt++;
#endif
			tcp->tcp_rcv_head = nilp(mblk_t);
			tcp->tcp_rcv_cnt = 0;
			MI_HRT_INCREMENT(tcp->tcp_rtime, rtime, 0);
			putnext(q, mp);
			MI_HRT_SET(rtime);
			tcp->tcp_co_norm = 1;
		}
		ASSERT(tcp->tcp_rcv_head == NULL);
		mp1 = mi_tpi_ordrel_ind();
		if (mp1) {
			tcp->tcp_ordrel_done = true;
			putnext(q, mp1);
			tcp->tcp_co_norm = 1;
			if (tcp->tcp_deferred_clean_death) {
				/*
				 * tcp_clean_death was deferred
				 * for T_ORDREL_IND - do it now
				 */
				tcp_clean_death(tcp, tcp->tcp_client_errno);
				tcp->tcp_deferred_clean_death =	false;
			}
		} else {
			/*
			 * Run the orderly release in the
			 * service routine.
			 */
			qenable(q);
			/*
			 * Caveat(XXX): The machine may be so
			 * overloaded that tcp_rsrv() is not scheduled
			 * until after the endpoint has transitioned
			 * to TCPS_TIME_WAIT
			 * and tcp_close_wait_interval expires. Then
			 * tcp_timer() will blow away state in tcp_t
			 * and T_ORDREL_IND will never be delivered
			 * upstream. Unlikely but potentially
			 * a problem.
			 */
		}
	}
done:
	MI_HRT_INCREMENT(tcp->tcp_rtime, rtime, 1);
	SYNC_CHK_OUT(otcp, "tcp_rput");
	TRACE_1(TR_FAC_TCP, TR_TCP_RPUT_OUT, "tcp_rput end:  q %X", q);
}

/*
 * tcp_rput_other is called by tcp_rput to handle everything other than M_DATA
 * messages.
 */
staticf void
tcp_rput_other (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	mblk_t	* mp1;
	u_char	* rptr = mp->b_rptr;
	tcp_t	* tcp = (tcp_t *)q->q_ptr;
	tcph_t	* tcph;
	struct T_error_ack * tea;

	switch (mp->b_datap->db_type) {
	case M_PROTO:
	case M_PCPROTO:
		if ((mp->b_wptr - rptr) < sizeof(long))
			break;
		tea = (struct T_error_ack *)ALIGN32(rptr);
		switch (tea->PRIM_type) {
		case T_BIND_ACK:
			if (tcp->tcp_hard_binding) {
				tcp->tcp_hard_binding = 0;
				tcp->tcp_hard_bound = 1;
			}
			if (mp1 = tcp_ire_mp(mp)) {
				tcp_adapt(tcp, mp1);
				if (tcp->tcp_state == TCPS_SYN_SENT) {
					tcp->tcp_timer_interval
					 = (tcp->tcp_rto << 1)
					  + tcp_conn_grace_period;
					tcp_timer(tcp);
				}
				/*
				 * TODO: allow data with connect requests
				 * by picking off M_DATA trailers here and
				 * feeding them into tcp_wput_slow() rather than
				 * calling tcp_timer().
				 */
			}
			/*
			 * A trailer mblk indicates a waiting client upstream.
			 * We complete here the processing begun in
			 * either tcp_bind() or tcp_connect() by passing
			 * upstream the reply message they supplied.
			 */
			mp1 = mp;
			mp = mp->b_cont;
			freeb(mp1);
			if (mp)
				break;
			return;
		case T_ERROR_ACK:
			mi_strlog(q, 1, SL_TRACE|SL_ERROR,
			"tcp_rput_other: case T_ERROR_ACK, ERROR_prim == %d",
				tea->ERROR_prim);
			if (tea->ERROR_prim == T_BIND_REQ) {
				if ( tcp->tcp_state >= TCPS_SYN_SENT )
					tea->ERROR_prim = T_CONN_REQ;
				tcp->tcp_state = TCPS_IDLE;
				tcp->tcp_ipha.ipha_src = 0;
				tcph = tcp->tcp_tcph;
				tcph->th_lport[0] = 0;
				tcph->th_lport[1] = 0;
			} else if (tea->ERROR_prim == T_UNBIND_REQ) {
				if (tcp->tcp_unbind_pending) 
					tcp->tcp_unbind_pending = 0;
				else {
					/* From tcp_ip_unbind() - free */
					freemsg(mp);
					return;
				}
			}
			break;
		case T_OK_ACK: {
			struct T_ok_ack * toa;

			if ((mp->b_wptr - rptr) < sizeof(long))
				break;
			toa = (struct T_ok_ack *)ALIGN32(rptr);
			
			if (toa->CORRECT_prim == T_UNBIND_REQ) {
				if (tcp->tcp_unbind_pending) 
					tcp->tcp_unbind_pending = 0;
				else {
					/* From tcp_ip_unbind() - free */
					freemsg(mp);
					return;
				}
			}
			break;
		}
		default:
			break;
		}
		break;
	case M_CTL:
		/*
		 * ICMP messages.
		 * tcp_icmp_error passes the message back in on the correct
		 * queue if it arrived on the wrong one.
		 */
		tcp_icmp_error(q, mp);
		return;
	case M_FLUSH:
		if (*rptr & FLUSHR)
			flushq(q, FLUSHDATA);
		break;
	default:
		break;
	}
	putnext(q, mp);
	tcp->tcp_co_norm = 1;
}

/*
 * The read side service routine is called mostly when we get back-enabled as a
 * result of flow control relief.  Since we don't actually queue anything in
 * TCP, we have no data to send out of here.  What we do is clear the receive
 * window, send out a window update, and reset the tcp_credit to count down
 * for the next canput call in tcp_rput.
 * This routine is also called to drive an orderly release message upstream.
 */
staticf void
tcp_rsrv (q)
reg	queue_t	* q;
{
	tcp_t	* tcp = (tcp_t *)q->q_ptr;
	mblk_t	* mp;
	mblk_t	* lastmp;

	TRACE_1(TR_FAC_TCP, TR_TCP_RSRV_IN, "tcp_rsrv start:  q %X", q);

#ifdef TCP_PERF
	tcp_rsrv_cnt++;
#endif
	SYNC_CHK_IN(tcp, "tcp_rsrv");
	/*
	 * Move out anything inserted by lateral_putq()
	 * These are either M_DATAs which are IP datagrams received from the
	 * net or an M_PROTO containing a T_BIND_REQ. The M_DATAs are just
	 * fed into tcp_rput. The T_BIND_REQ is taken as an indication that
	 * we just accepted a connection and we do the final part of
	 * the accept processing.
	 */
	lastmp = q->q_last;
	while ((mp = getq(q)) != NULL) {

		if (mp->b_datap->db_type == M_PROTO &&
		   ((struct T_bind_req *)ALIGN32(mp->b_rptr))->PRIM_type ==
		    T_BIND_REQ) {
			/*
			 * The connection was just accepted. Send the 
			 * T_BIND_REQ down to ip.
			 */
			putnext(tcp->tcp_wq, mp);

			tcp_mss_set(tcp, tcp->tcp_mss);

			/*
			 * Pass up queued data that has accumulated before 
			 * the connection was accepted.
			 */
			if ((mp = tcp->tcp_rcv_head) != nilp(mblk_t)) {
				putnext(q, mp);
				tcp->tcp_co_norm = 1;
				tcp->tcp_rcv_head = nilp(mblk_t);
				tcp->tcp_rcv_cnt = 0;
			}

		} else
			tcp_rput(q, mp);
		/*
		 * When processing the default queue, only process the
		 * mblks that were present on the queue when we entered.
		 * This is necessary in order to prevent an infinite loop
		 * because in some cases, tcp_rput_data(), called by
		 * tcp_rput(), may put the message back on the queue.
		 */
		if ((q == tcp_g_q) && (mp == lastmp))
			break;
	}

	/* Nothing more to do for the default queue */
	if ( q == tcp_g_q ) {
		SYNC_CHK_OUT(tcp, "tcp_rsrv");
		TRACE_1(TR_FAC_TCP, TR_TCP_RSRV_OUT, "tcp_rsrv end:  q %X", q);
		return;
	}

	if ( canput(q->q_next) ) {
		u_long rwnd = tcp->tcp_rwnd;
		tcp->tcp_credit = tcp->tcp_credit_init;
		tcp->tcp_rwnd = tcp->tcp_rwnd_max;
		U32_TO_ABE16(tcp->tcp_rwnd, ALIGN16(tcp->tcp_tcph->th_win));
		if ( tcp->tcp_state >= TCPS_ESTABLISHED
		&&  (rwnd <= tcp->tcp_rwnd_max >> 1
		    ||  rwnd < tcp->tcp_mss) ) {
			tcp_xmit_ctl(nilp(char), tcp, nilp(mblk_t),
				(tcp->tcp_swnd == 0) ? tcp->tcp_suna :
				     tcp->tcp_snxt, tcp->tcp_rnxt, TH_ACK);
			BUMP_MIB(tcp_mib.tcpOutAck);
			BUMP_MIB(tcp_mib.tcpOutWinUpdate);
		}
	}
	if ( tcp->tcp_fin_rcvd  &&  !tcp->tcp_ordrel_done ) {
		ASSERT(tcp->tcp_listener == NULL);
		if ((mp = tcp->tcp_rcv_head) != nilp(mblk_t)) {
			putnext(q, mp);
			tcp->tcp_co_norm = 1;
			tcp->tcp_rcv_head = nilp(mblk_t);
			tcp->tcp_rcv_cnt = 0;
		}
		mp = mi_tpi_ordrel_ind();
		if (mp) {
			tcp->tcp_ordrel_done = true;
			putnext(q, mp);
			tcp->tcp_co_norm = 1;
			if (tcp->tcp_deferred_clean_death) {
				/*
				 * tcp_clean_death was deferred for
				 * T_ORDREL_IND - do it now
				 */
				tcp_clean_death(tcp, tcp->tcp_client_errno);
				tcp->tcp_deferred_clean_death = false;
			}
		} else {
			mi_bufcall(q, sizeof(struct T_ordrel_ind), BPRI_HI);
		}
	}
	SYNC_CHK_OUT(tcp, "tcp_rsrv");
	TRACE_1(TR_FAC_TCP, TR_TCP_RSRV_OUT, "tcp_rsrv end:  q %X", q);
}

/*
 * tcp_rwnd_set is called to adjust the receive window to a desired value.
 * We do not allow the receive window to shrink.  If the requested value is
 * not a multiple of the current mss, we round it up to the next higher
 * multiple of mss.  It is assumed that this routine is called prior to the
 * start of data transfer.
 */
staticf int
tcp_rwnd_set (tcp, rwnd)
	tcp_t	* tcp;
reg	u_long	rwnd;
{
	int	mss = tcp->tcp_mss;
	u_long	old_max_rwnd = tcp->tcp_rwnd_max;
	
	/* Insist on a receive window that is a multiple of mss. */
	rwnd = (((rwnd - 1) / mss) + 1) * mss;
	/* Monotonically increasing tcp_rwnd ensures no reneg */
	if (rwnd < old_max_rwnd)
		rwnd = (((old_max_rwnd - 1) / mss) + 1) * mss;
	if (rwnd > 65535) {
		rwnd = 65535 - (65535 % mss);
		if (rwnd < mss)
			rwnd = 65535;
	}
	tcp->tcp_rack_abs_max = (rwnd / mss / 2) * mss;
	if (tcp->tcp_rack_cur_max > tcp->tcp_rack_abs_max)
		tcp->tcp_rack_cur_max = tcp->tcp_rack_abs_max;
	else
		tcp->tcp_rack_cur_max = mss;
	/* 
	 * Increment the current rwnd by the amount the maximum grew (we
	 * can not overwrite it since we might be in the middle of a
	 * connection.)
	 */
	tcp->tcp_rwnd += rwnd - old_max_rwnd;
	U32_TO_ABE16(tcp->tcp_rwnd, ALIGN16(tcp->tcp_tcph->th_win));
	tcp->tcp_rwnd_max = rwnd;
	if ( TCP_IS_DETACHED(tcp) )
		return rwnd;
	/*
	 * We set the maximum receive window into rq->q_hiwat.
	 * This is not actually used for flow control.
	 */
	tcp->tcp_rq->q_hiwat = rwnd;
	/*
	 * Set the amount of data that we will allow upstream before checking
	 * for flow control by calling canput.  tcp_rwin_credit_pct is the
	 * percentage of a full window that is advanced as credit.
	 */
	tcp->tcp_credit_init = (tcp_rwin_credit_pct * rwnd) / 100;
	if ( tcp->tcp_credit >= 0 )
		tcp->tcp_credit = tcp->tcp_credit_init;
	/*
	 * Set the Stream head high and low water marks.  This doesn't have
	 * to be here, since we are simply using default values, but we would
	 * prefer to choose these values algorithmically, with a likely
	 * relationship to rwnd.
	 */
	mi_set_sth_hiwat(tcp->tcp_rq, MAX(rwnd, tcp_sth_rcv_hiwat));
	if (tcp_sth_rcv_lowat)
		mi_set_sth_lowat(tcp->tcp_rq, tcp_sth_rcv_lowat);
	return rwnd;
}

/* Return SNMP stuff in buffer in mpdata. */
staticf	int
tcp_snmp_get (q, mpctl)
	queue_t		* q;
	mblk_t		* mpctl;
{
	mblk_t			* mpdata;
	mblk_t			* mp2ctl;
	mblk_t			* mp2data;
	struct opthdr		* optp;
	IDP			idp;
	tcp_t			* tcp;
	char			buf[sizeof(mib2_tcpConnEntry_t)];
	mib2_tcpConnEntry_t	* tce = (mib2_tcpConnEntry_t *)ALIGN32(buf);

	if (!mpctl 
	|| !(mpdata = mpctl->b_cont)
	|| !(mp2ctl = copymsg(mpctl)))
		return 0;

	/* build table of connections -- need count in fixed part */
	mp2data = mp2ctl->b_cont;
	SET_MIB(tcp_mib.tcpRtoAlgorithm, 4);   /* vanj */
	SET_MIB(tcp_mib.tcpRtoMin, tcp_rexmit_interval_min);
	SET_MIB(tcp_mib.tcpRtoMax, tcp_rexmit_interval_max);
	SET_MIB(tcp_mib.tcpMaxConn, -1);
	SET_MIB(tcp_mib.tcpCurrEstab, 0);
	TCP_LOCK_READ();
	for (idp = mi_first_ptr(&tcp_g_head);
	     (tcp = (tcp_t *)ALIGN32(idp)) != NULL;
	     idp = mi_next_ptr(&tcp_g_head, idp)) {
		UPDATE_MIB(tcp_mib.tcpInSegs, tcp->tcp_ibsegs);
		tcp->tcp_ibsegs = 0;
		UPDATE_MIB(tcp_mib.tcpOutSegs, tcp->tcp_obsegs);
		tcp->tcp_obsegs = 0;
		tce->tcpConnState = tcp_snmp_state(tcp);
		if (tce->tcpConnState == MIB2_TCP_established
		|| tce->tcpConnState == MIB2_TCP_closeWait)
			BUMP_MIB(tcp_mib.tcpCurrEstab);
		bcopy((char *)tcp->tcp_iph.iph_src,
			(char *)&tce->tcpConnLocalAddress, sizeof(IpAddress));
		tce->tcpConnLocalPort =
			(int)BE16_TO_U16(tcp->tcp_tcph->th_lport);
		bcopy((char *)&tcp->tcp_remote,
			(char *)&tce->tcpConnRemAddress, sizeof(IpAddress));
		tce->tcpConnRemPort = (int)BE16_TO_U16(tcp->tcp_tcph->th_fport);
		tce->tcpConnEntryInfo.ce_snxt = tcp->tcp_snxt;
		tce->tcpConnEntryInfo.ce_suna = tcp->tcp_suna;
		tce->tcpConnEntryInfo.ce_swnd = tcp->tcp_swnd;
		tce->tcpConnEntryInfo.ce_rnxt = tcp->tcp_rnxt;
		tce->tcpConnEntryInfo.ce_rack = tcp->tcp_rack;
		tce->tcpConnEntryInfo.ce_rwnd = tcp->tcp_rwnd;
		tce->tcpConnEntryInfo.ce_rto =  tcp->tcp_rto;
		tce->tcpConnEntryInfo.ce_mss =  tcp->tcp_mss;	
		tce->tcpConnEntryInfo.ce_state =tcp->tcp_state;
		snmp_append_data(mp2data, buf, sizeof buf);
	}
	TCP_UNLOCK_READ();
	/* fixed length structure... */
	SET_MIB(tcp_mib.tcpConnTableSize, sizeof(mib2_tcpConnEntry_t));
	optp = (struct opthdr *)ALIGN32(&mpctl->b_rptr[
						sizeof(struct T_optmgmt_ack)]);
	optp->level = MIB2_TCP;
	optp->name = 0;
	snmp_append_data(mpdata, (char *)&tcp_mib, sizeof(tcp_mib));
	optp->len = msgdsize(mpdata);
	qreply(q, mpctl);

	/* table of connections... */
	optp = (struct opthdr *)ALIGN32(&mp2ctl->b_rptr[
						sizeof(struct T_optmgmt_ack)]);
	optp->level = MIB2_TCP;
	optp->name = MIB2_TCP_13;
	optp->len = msgdsize(mp2data);
	qreply(q, mp2ctl);
	return 1;
}

/* Return 0 if invalid set request, 1 otherwise, including non-tcp requests  */
/* ARGSUSED */
staticf	int
tcp_snmp_set (q, level, name, ptr, len)
	queue_t	* q;
	int	level;
	int	name;
	u_char	* ptr;
	int	len;
{
	mib2_tcpConnEntry_t	* tce = (mib2_tcpConnEntry_t *)ALIGN32(ptr);

	switch ( level ) {
	case MIB2_TCP:
		switch (name) {
		case 13:
			if (tce->tcpConnState != MIB2_TCP_deleteTCB)
				return 0;
			/* TODO: delete entry defined by tce */
			return 1;
		default:
			return 0;
		}
	default:
		return 1;
	}
}

/* Translate TCP state to MIB2 TCP state. */ 
staticf int
tcp_snmp_state (tcp)
reg	tcp_t	* tcp;
{
	if (!tcp)
		return 0;
	switch (tcp->tcp_state) {
	case TCPS_CLOSED:
	case TCPS_IDLE:	/* RFC1213 doesn't have analogue for IDLE & BOUND */
	case TCPS_BOUND:
		return MIB2_TCP_closed;
	case TCPS_LISTEN:
		return MIB2_TCP_listen;
	case TCPS_SYN_SENT:
		return MIB2_TCP_synSent;
	case TCPS_SYN_RCVD:
		return MIB2_TCP_synReceived;
	case TCPS_ESTABLISHED:
		return MIB2_TCP_established;
	case TCPS_CLOSE_WAIT:
		return MIB2_TCP_closeWait;
	case TCPS_FIN_WAIT_1:
		return MIB2_TCP_finWait1;
	case TCPS_CLOSING:
		return MIB2_TCP_closing;
	case TCPS_LAST_ACK:
		return MIB2_TCP_lastAck;
	case TCPS_FIN_WAIT_2:
		return MIB2_TCP_finWait2;
	case TCPS_TIME_WAIT:
		return MIB2_TCP_timeWait;
	default:
		return 0;
	}
}

/* TCP status report triggered via the Named Dispatch mechanism. */ 	
/* ARGSUSED */
staticf	int
tcp_status_report (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	IDP	idp;
	tcp_t	* tcp;

	/* Lock held before calling nd_getset */
	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	mi_mpprintf(mp,
	 "TCP      dest            snxt     suna     swnd  rnxt     rack     rwnd  rto   mss   [lport,fport] state");
        /* 01234567 xxx.xxx.xxx.xxx 01234567 01234567 12345 01234567 01234567 12345 12345 12345 [1234,1234] TCPS_ESTABLISHED */
	for (idp = mi_first_ptr(&tcp_g_head);
	     (tcp = (tcp_t *)ALIGN32(idp)) != NULL;
	     idp = mi_next_ptr(&tcp_g_head, idp)) {
		mi_mpprintf(mp,
			    "%08x %3d.%3d.%3d.%3d %8u %8u %5d %8u %8u %5d %5d %5d %s%c",
			    tcp,
			    tcp->tcp_iph.iph_dst[0] & 0xFF,
			    tcp->tcp_iph.iph_dst[1] & 0xFF,
			    tcp->tcp_iph.iph_dst[2] & 0xFF,
			    tcp->tcp_iph.iph_dst[3] & 0xFF,
			    tcp->tcp_snxt, tcp->tcp_suna, tcp->tcp_swnd,
			    tcp->tcp_rnxt,
			    tcp->tcp_rack, tcp->tcp_rwnd, tcp->tcp_rto,
			    tcp->tcp_mss, tcp_display(tcp),
			    TCP_IS_DETACHED(tcp) ? '*' : ' ');
	}
	return 0;
}

/* TCP status report triggered via the Named Dispatch mechanism. */ 	
/* ARGSUSED */
staticf	int
tcp_bind_hash_report (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	tcp_t	* tcp;
	int	i;

	/* Lock held before calling nd_getset */
	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	mi_mpprintf(mp,
	 "TCP      dest            snxt     suna     swnd  rnxt     rack     rwnd  rto   mss   [lport,fport] state");
        /* 01234567 xxx.xxx.xxx.xxx 01234567 01234567 12345 01234567 01234567 12345 12345 12345 [1234,1234] TCPS_ESTABLISHED */
	for (i = 0; i < A_CNT(tcp_bind_fanout); i++) {
		for (tcp = tcp_bind_fanout[i]; tcp != NULL;
		     tcp = tcp->tcp_bind_hash)  {
			mi_mpprintf(mp,
			    "%03d %08x %3d.%3d.%3d.%3d %8u %8u %5d %8u %8u %5d %5d %5d %s%c",
			    i,
			    tcp,
			    tcp->tcp_iph.iph_dst[0] & 0xFF,
			    tcp->tcp_iph.iph_dst[1] & 0xFF,
			    tcp->tcp_iph.iph_dst[2] & 0xFF,
			    tcp->tcp_iph.iph_dst[3] & 0xFF,
			    tcp->tcp_snxt, tcp->tcp_suna, tcp->tcp_swnd,
			    tcp->tcp_rnxt,
			    tcp->tcp_rack, tcp->tcp_rwnd, tcp->tcp_rto,
			    tcp->tcp_mss, tcp_display(tcp),
			    TCP_IS_DETACHED(tcp) ? '*' : ' ');
		}
	}
	return 0;
}

/* TCP status report triggered via the Named Dispatch mechanism. */ 	
/* ARGSUSED */
staticf	int
tcp_listen_hash_report (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	tcp_t	* tcp;
	int	i;

	/* Lock held before calling nd_getset */
	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	mi_mpprintf(mp,
	 "TCP      dest            snxt     suna     swnd  rnxt     rack     rwnd  rto   mss   [lport,fport] state");
        /* 01234567 xxx.xxx.xxx.xxx 01234567 01234567 12345 01234567 01234567 12345 12345 12345 [1234,1234] TCPS_ESTABLISHED */
	for (i = 0; i < A_CNT(tcp_listen_fanout); i++) {
		for (tcp = tcp_listen_fanout[i]; tcp != NULL;
		     tcp = tcp->tcp_listen_hash)  {
			mi_mpprintf(mp,
			    "%03d %08x %3d.%3d.%3d.%3d %8u %8u %5d %8u %8u %5d %5d %5d %s%c",
			    i,
			    tcp,
			    tcp->tcp_iph.iph_dst[0] & 0xFF,
			    tcp->tcp_iph.iph_dst[1] & 0xFF,
			    tcp->tcp_iph.iph_dst[2] & 0xFF,
			    tcp->tcp_iph.iph_dst[3] & 0xFF,
			    tcp->tcp_snxt, tcp->tcp_suna, tcp->tcp_swnd,
			    tcp->tcp_rnxt,
			    tcp->tcp_rack, tcp->tcp_rwnd, tcp->tcp_rto,
			    tcp->tcp_mss, tcp_display(tcp),
			    TCP_IS_DETACHED(tcp) ? '*' : ' ');
		}
	}
	return 0;
}

/* TCP status report triggered via the Named Dispatch mechanism. */ 	
/* ARGSUSED */
staticf	int
tcp_conn_hash_report (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	tcp_t	* tcp;
	int	i;

	/* Lock held before calling nd_getset */
	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	mi_mpprintf(mp,
	 "TCP      dest            snxt     suna     swnd  rnxt     rack     rwnd  rto   mss   [lport,fport] state");
        /* 01234567 xxx.xxx.xxx.xxx 01234567 01234567 12345 01234567 01234567 12345 12345 12345 [1234,1234] TCPS_ESTABLISHED */
	for (i = 0; i < A_CNT(tcp_conn_fanout); i++) {
		for (tcp = tcp_conn_fanout[i]; tcp != NULL;
		     tcp = tcp->tcp_conn_hash)  {
			mi_mpprintf(mp,
			    "%03d %08x %3d.%3d.%3d.%3d %8u %8u %5d %8u %8u %5d %5d %5d %s%c",
			    i,
			    tcp,
			    tcp->tcp_iph.iph_dst[0] & 0xFF,
			    tcp->tcp_iph.iph_dst[1] & 0xFF,
			    tcp->tcp_iph.iph_dst[2] & 0xFF,
			    tcp->tcp_iph.iph_dst[3] & 0xFF,
			    tcp->tcp_snxt, tcp->tcp_suna, tcp->tcp_swnd,
			    tcp->tcp_rnxt,
			    tcp->tcp_rack, tcp->tcp_rwnd, tcp->tcp_rto,
			    tcp->tcp_mss, tcp_display(tcp),
			    TCP_IS_DETACHED(tcp) ? '*' : ' ');
		}
	}
	return 0;
}

/* TCP status report triggered via the Named Dispatch mechanism. */ 	
/* ARGSUSED */
staticf	int
tcp_queue_hash_report (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	tcp_t	* tcp;
	int	i;

	/* Lock held before calling nd_getset */
	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	mi_mpprintf(mp,
	 "TCP      dest            snxt     suna     swnd  rnxt     rack     rwnd  rto   mss   [lport,fport] state");
        /* 01234567 xxx.xxx.xxx.xxx 01234567 01234567 12345 01234567 01234567 12345 12345 12345 [1234,1234] TCPS_ESTABLISHED */
	for (i = 0; i < A_CNT(tcp_queue_fanout); i++) {
		for (tcp = tcp_queue_fanout[i]; tcp != NULL;
		     tcp = tcp->tcp_queue_hash)  {
			mi_mpprintf(mp,
			    "%03d %08x %3d.%3d.%3d.%3d %8u %8u %5d %8u %8u %5d %5d %5d %s%c",
			    i,
			    tcp,
			    tcp->tcp_iph.iph_dst[0] & 0xFF,
			    tcp->tcp_iph.iph_dst[1] & 0xFF,
			    tcp->tcp_iph.iph_dst[2] & 0xFF,
			    tcp->tcp_iph.iph_dst[3] & 0xFF,
			    tcp->tcp_snxt, tcp->tcp_suna, tcp->tcp_swnd,
			    tcp->tcp_rnxt,
			    tcp->tcp_rack, tcp->tcp_rwnd, tcp->tcp_rto,
			    tcp->tcp_mss, tcp_display(tcp),
			    TCP_IS_DETACHED(tcp) ? '*' : ' ');
		}
	}
	return 0;
}

/*
 * tcp_timer is the timer service routine.  It handles all timer events for
 * a tcp instance except keepalives.  It figures out from the state of the
 * tcp instance what kind of action needs to be done at the time it is called.
 */
staticf void
tcp_timer (tcp)
	tcp_t	* tcp;
{
	u_long	first_threshold;
	mblk_t	* mp;
	long	ms;
	u_long	mss;
	u_long	second_threshold;

	if ((mp = tcp->tcp_rcv_head) != nilp(mblk_t) &&
	    tcp->tcp_co_head == nilp(mblk_t) &&
	    tcp->tcp_listener == nilp(tcp_t)) {
#ifdef TCP_PERF
		tcp_head_timer++;
#endif
		putnext(tcp->tcp_rq, mp);
		tcp->tcp_co_norm = 1;
		tcp->tcp_rcv_head = nilp(mblk_t);
		tcp->tcp_rcv_cnt = 0;
	}

	first_threshold =  tcp->tcp_first_timer_threshold;
	second_threshold = tcp->tcp_second_timer_threshold;
	switch (tcp->tcp_state) {
	case TCPS_IDLE:
	case TCPS_BOUND:
	case TCPS_LISTEN:
		tcp->tcp_ms_we_have_waited = 0;
		return;
	case TCPS_SYN_SENT:
	case TCPS_SYN_RCVD:
		first_threshold =  tcp->tcp_first_ctimer_threshold;
		second_threshold = tcp->tcp_second_ctimer_threshold;
		break;
	case TCPS_ESTABLISHED:
	case TCPS_FIN_WAIT_1:
	case TCPS_CLOSING:
	case TCPS_CLOSE_WAIT:
	case TCPS_LAST_ACK:
		/* If we have data to rexmit */
		if (tcp->tcp_suna != tcp->tcp_snxt) {
			long	time_to_wait;
			BUMP_MIB(tcp_mib.tcpTimRetrans);
			if (!tcp->tcp_xmit_head)
				break;
			time_to_wait = lbolt - (long)tcp->tcp_xmit_head->b_prev;
			time_to_wait = MS_TO_TICKS(tcp->tcp_rto) - time_to_wait;
			if (time_to_wait > 0) {
				/*
				 * Timer fired too early.  We need to restart
				 * but do not clear ms_we_have_waited.
				 */
				tcp->tcp_timer_interval =
				    LBOLT_TO_MS(time_to_wait);
				mi_timer(tcp->tcp_wq, tcp->tcp_timer_mp,
				    tcp->tcp_timer_interval);
				return;
			}
			/*
			 * When we probe zero windows, we force the swnd open.
			 * If our peer acks with a closed window swnd will be
			 * set to zero by tcp_rput(). As long as we are
			 * receiving acks tcp_rput will 
			 * reset 'tcp_ms_we_have_waited' so as not to trip the
			 * first and second interval actions.  NOTE: the timer
			 * interval is allowed to continue its exponential
			 * backoff.
			 */
			if (tcp->tcp_swnd == 0 || tcp->tcp_zero_win_probe) {
				mi_strlog(tcp->tcp_rq, 1, SL_TRACE,
					  "tcp_timer: zero win");
			} else {
				tcp->tcp_cwnd_ssthresh = tcp->tcp_cwnd >> 1;
				tcp->tcp_cwnd = tcp->tcp_mss;
			}
			if (tcp->tcp_timer_interval < tcp->tcp_rto)
				tcp->tcp_timer_interval = tcp->tcp_rto;
			break;
		}
		/* TODO: source quench, sender silly window, ... */
		/* If we have a zero window */
		if (tcp->tcp_unsent  &&  tcp->tcp_swnd == 0) {
			/* Extend window for probe */
			tcp->tcp_swnd += MIN(tcp->tcp_mss,
					tcp_zero_win_probesize);
			tcp->tcp_zero_win_probe = 1;
			BUMP_MIB(tcp_mib.tcpOutWinProbe);
			tcp_wput_slow(tcp, nilp(mblk_t));
			return;
		}
		/* Handle timeout from sender SWS avoidance. */
		if ( tcp->tcp_unsent ) {
			/* Reset our knowledge of the max send window since
			 * the receiver might have reduced its receive buffer.
			 */
			tcp->tcp_max_swnd = tcp->tcp_swnd; /* We know this is > 0 */
			tcp_wput_slow(tcp, nilp(mblk_t));
			return;
		}
		/* Is there a FIN that needs to be to re retransmitted? */
		if ((tcp->tcp_valid_bits & TCP_FSS_VALID)
		&&  !tcp->tcp_fin_acked)
			break;
		/* If we have nothing to do, return without restarting timer. */
		if ( tcp->tcp_rnxt == tcp->tcp_rack )
			return;
		/* Otherwise we have a deferred ack */
		if ( (tcp->tcp_rnxt - tcp->tcp_rack) > tcp->tcp_mss ) {
			/*
			 * Make sure we don't allow deferred ACKs to result in
			 * timer-based ACKing.  If we have held off an ACK
			 * when there was more than an mss here, and the timer
			 * goes off, we have to worry about the possibility
			 * that the sender isn't doing slow-start, or is out
			 * of step with us for some other reason.  We fall
			 * permanently back in the direction of
			 * ACK-every-other-packet as suggested in RFC 1122.
			 */
			if ( tcp->tcp_rack_cnt >= tcp->tcp_mss
			&&  tcp->tcp_rack_abs_max > (tcp->tcp_mss << 1) )
				tcp->tcp_rack_abs_max -= tcp->tcp_mss;
			tcp->tcp_rack_cur_max = tcp->tcp_mss;
			/* Adapt to our peers self imposed send window */
			if (tcp->tcp_max_swnd == tcp->tcp_rack_cnt
			&&  tcp->tcp_max_swnd >= (tcp->tcp_mss << 1)
			&&  tcp->tcp_rack_abs_max > (tcp->tcp_max_swnd >> 1))
				tcp->tcp_rack_abs_max = tcp->tcp_max_swnd >> 1;
		}
		mp = tcp_ack_mp(tcp);
		if (mp) {
			putnext(tcp->tcp_wq, mp);
			BUMP_LOCAL(tcp->tcp_obsegs);
			BUMP_MIB(tcp_mib.tcpOutAck);
			BUMP_MIB(tcp_mib.tcpOutAckDelayed);
		}
		tcp->tcp_ms_we_have_waited = 0;
		return;
	case TCPS_FIN_WAIT_2:
		/* 
		 * User closed the TCP endpoint and peer ACK'ed our FIN.
		 * We waited some time for for peer's FIN, but it hasn't
		 * arrived.  We flush the connection now to avoid
		 * case where the peer has rebooted.
		 */
		if (TCP_IS_DETACHED(tcp))
			tcp_clean_death(tcp, 0);
		else
			TCP_TIMER_RESTART(tcp, tcp_fin_wait_2_flush_interval);
		return;
 	case TCPS_TIME_WAIT:
		tcp_clean_death(tcp, 0);
		return;
	default:
		mi_strlog(tcp->tcp_wq, 1, SL_TRACE|SL_ERROR
		 , "tcp_timer: strange state (%d) %s"
		 , tcp->tcp_state, tcp_display(tcp));
		tcp->tcp_ms_we_have_waited = 0;
		return;
	}
	ms = tcp->tcp_timer_interval + tcp->tcp_ms_we_have_waited;
	tcp->tcp_ms_we_have_waited = ms;
	if (ms >= first_threshold
	&& (ms - tcp->tcp_timer_interval) < first_threshold) {
		tcp_ip_notify(tcp);
	} else if (ms >= second_threshold) {
                if ( tcp->tcp_keepalive_mp
                &&  tcp->tcp_snxt - 1
                == ((tcpka_t *)ALIGN32(tcp->tcp_keepalive_mp->b_rptr))->tcpka_seq )
                        BUMP_MIB(tcp_mib.tcpTimKeepaliveDrop);
                else
                        BUMP_MIB(tcp_mib.tcpTimRetransDrop);
		tcp_clean_death(tcp, tcp->tcp_client_errno ?
			tcp->tcp_client_errno : ETIMEDOUT);
		return;
	}
	ms = tcp->tcp_timer_interval;
	ms += ms;
 	if (ms > tcp_rexmit_interval_max)
		ms = tcp_rexmit_interval_max;
	tcp->tcp_timer_interval = ms;
	mss = tcp->tcp_snxt - tcp->tcp_suna;
	if (mss > tcp->tcp_mss)
		mss = tcp->tcp_mss;
	if (mss > tcp->tcp_swnd  &&  tcp->tcp_swnd != 0)
		mss = tcp->tcp_swnd;
	mi_timer(tcp->tcp_wq, tcp->tcp_timer_mp, tcp->tcp_timer_interval);
	if ((mp = tcp->tcp_xmit_head) != NULL)
		mp->b_prev = (mblk_t *)lbolt;
	mp = tcp_xmit_mp(tcp, mp, mss, tcp->tcp_suna, 1);
	if (mp) {
		tcp->tcp_csuna = tcp->tcp_snxt;
		BUMP_MIB(tcp_mib.tcpRetransSegs);	
		UPDATE_MIB(tcp_mib.tcpRetransBytes, msgdsize(mp->b_cont));
		putnext(tcp->tcp_wq, mp);
	}
}

/*
 * tcp_timer_alloc is called by tcp_init to allocate and initialize a
 * tcp timer.
 */
staticf mblk_t * 
tcp_timer_alloc (tcp, func, extra)
	tcp_t	* tcp;
	pfv_t	func;
	int extra;
{
	mblk_t	* mp;

	mp = mi_timer_alloc(sizeof(tcpt_t) + extra);
	if (mp) {
		tcpt_t	* tcpt = (tcpt_t *)ALIGN32(mp->b_rptr);
		tcpt->tcpt_tcp = tcp;
		tcpt->tcpt_pfv = func;
	}
	return mp;
}

/* tcp_unbind is called by tcp_wput_proto to handle T_UNBIND_REQ messages. */
staticf void
tcp_unbind (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	tcp_t	* tcp;

	tcp = (tcp_t *)q->q_ptr;
	switch (tcp->tcp_state) {
	case TCPS_BOUND:
	case TCPS_LISTEN:
		break;
	default:
		tcp_err_ack(tcp->tcp_wq, mp, TOUTSTATE, 0);
		return;
	}
	tcp->tcp_state = TCPS_IDLE;
	tcp->tcp_unbind_pending = 1;
	/* Send M_FLUSH according to TPI */
	putnextctl1(tcp->tcp_rq, M_FLUSH, FLUSHRW);
	/* Pass the unbind to IP */
	putnext(q, mp);
}

#ifdef	MI_HRTIMING
/* Report rput and wput average times. */
staticf	int
tcp_time_report (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	long	ops;
	long	tot;
	long	usecs;

	/* Report the average times per operation, in microseconds. */
	MI_HRT_TO_USECS(tcp_g_rtime, usecs);
	ops = MI_HRT_OPS(tcp_g_rtime);
	tot = ops * usecs;
	mi_mpprintf(mp, "tcp rput:  %d operations, avg %d usecs.", ops, usecs);
	MI_HRT_TO_USECS(tcp_g_wtime, usecs);
	ops = MI_HRT_OPS(tcp_g_wtime);
	tot += ops * usecs;
	mi_mpprintf(mp, "tcp wput:  %d operations, avg %d usecs.", ops, usecs);

	/* Report the total time spent in TCP, in milliseconds. */
	mi_mpprintf(mp, "(tcp total time: %d msecs.)", tot / 1000);

	/*
	 * Report the elapsed time attributable to timer operations.  This
	 * overhead is NOT included in the averages and total reported above.
	 */
	mi_mpprintf(mp, "(tcp timer overhead: %d msecs.)",
		(MI_HRT_OHD(tcp_g_rtime) + MI_HRT_OHD(tcp_g_wtime)) / 1000);

	return 0;
}

/* Reset rput and wput timers. */
staticf	int
tcp_time_reset (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	MI_HRT_CLEAR(tcp_g_rtime);
	MI_HRT_CLEAR(tcp_g_wtime);
	return 0;
}
#endif

/* The write side info procedure. */
staticf int
tcp_winfop (q, dp)
	queue_t	* q;
	infod_t	* dp;
{
	return infonext(q, dp);
}

/* The write side r/w procedure. */

#if CCS_STATS
struct {
	struct {
		long long count, bytes;
	} tot, hit;
} wrw_stats;
#endif

staticf int
tcp_wrw (q, dp)
	queue_t	* q;
	struiod_t * dp;
{
	mblk_t	* mp = dp->d_mp;
	int	error;

	if (isuioq(q) && (error = struioget(q, mp, dp)))
		/*
		 * Uio error of some sort, so just return the error.
		 */
		return (error);
	/*
	 * Pass the mblk (chain) onto wput(), then return success.
	 */
	dp->d_mp = 0;
#if CCS_STATS
	wrw_stats.hit.count++;
	wrw_stats.hit.bytes += msgdsize(mp);
#endif
	tcp_wput(q, mp);
	return (0);
}

/*
 * The TCP fast path write put procedure.
 * NOTE: the logic of the fast path is duplicated from tcp_wput_slow()
 */
staticf void
tcp_wput (q, mp)
	register queue_t *q;
	register mblk_t  *mp;
{
	register tcp_t	*tcp;
	int	len;
	int	hdrlen;
	mblk_t	* mp1;
	u_char	* rptr;
	u32	snxt;
	tcph_t	*tcph;
	struct datab *db;
	u_long	suna;
	u_long	mss;

	TRACE_3(TR_FAC_TCP, TR_TCP_WPUT_IN,
		"tcp_wput start:  q %X db_type 0%o tcp %X",
		q, mp->b_datap->db_type, q->q_ptr);

	tcp = (tcp_t *)q->q_ptr;
	mss = tcp->tcp_mss;
	len = mp->b_wptr - mp->b_rptr;

	SYNC_CHK_IN(tcp, "tcp_rput");

	/*
	 * Criteria for fast path:
	 *
	 *   1. mblk type is M_DATA
	 *   2. single mblk in request
	 *   3. connection established
	 *   4. no unsent data
	 *   5. data in mblk
	 *   6. len <= mss
	 *   7. no tcp_valid bits
	 */

	if ((mp->b_datap->db_type == M_DATA) &&
	    (mp->b_cont == 0) &&
	    (tcp->tcp_state == TCPS_ESTABLISHED) &&
	    (tcp->tcp_unsent == 0) &&
	    (len > 0) &&
	    (len <= mss) &&
	    (tcp->tcp_valid_bits == 0)) {

		ASSERT(tcp->tcp_xmit_tail_unsent == 0);
		ASSERT(tcp->tcp_fin_sent == 0);

		/* queue new packet onto retransmission queue */
		if (tcp->tcp_xmit_head == 0) {
			tcp->tcp_xmit_head = mp;
		} else {
			tcp->tcp_xmit_last->b_cont = mp;
		}
		tcp->tcp_xmit_last = mp;
		tcp->tcp_xmit_tail = mp;

		/* find out how much we can send */
		/*
		 *    un-acked           usable
		 *  |--------------|-----------------|
		 *  tcp_suna       tcp_snxt          tcp_suna+tcp_swnd
		 */

		/* start sending from tcp_snxt */
		snxt = tcp->tcp_snxt;

		{
		int	usable;
		usable = tcp->tcp_swnd;		/* tcp window size */
		if (usable > tcp->tcp_cwnd)
			usable = tcp->tcp_cwnd;	/* congestion window smaller */
		usable -= snxt;		/* subtract stuff already sent */
		suna = tcp->tcp_suna;
		usable += suna;
		/* usable can be < 0 if the congestion window is smaller */
		if (len > usable)
			/* Can't send complete M_DATA in one shot */
			goto slow;
		}

		/*
		 * determine if anything to send (Nagle).
		 *
		 *   1. len < tcp_mss (i.e. small)
		 *   2. unacknowledged data present
		 *   3. len < nagle limit
		 *   4. last packet sent < nagle limit (previous packet sent)
		 */
		if ((len < mss) &&
		    (snxt != suna) &&
		    (len < (int)tcp->tcp_naglim) &&
		    (tcp->tcp_last_sent_len < tcp->tcp_naglim)) {
			/*
			 * This was the first unsent packet and normally
			 * mss < xmit_hiwater so there is no need to worry
			 * about flow control. The next packet will go
			 * through the flow control check in tcp_wput_slow.
			 */
			/* leftover work from above */
			tcp->tcp_unsent = len;
			tcp->tcp_xmit_tail_unsent = len;

			SYNC_CHK_OUT(tcp, "tcp_rput");
			TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_OUT,
			    "tcp_wput end (nagle):  q %X", q);
			return;
		}

		/* len <= tcp->tcp_mss && len == unsent so no silly window */

		if (snxt == suna) {
			TCP_TIMER_RESTART(tcp, tcp->tcp_rto);
		}

		/* we have always sent something */
		tcp->tcp_rack_cnt = 0;

		tcp->tcp_snxt = snxt + len;
		tcp->tcp_rack = tcp->tcp_rnxt;

		if ((mp1 = dupb(mp)) == 0)
			goto no_memory;
		mp->b_prev = (mblk_t *)lbolt;

		/*
		 * adjust tcp header information
		 */
		tcph = tcp->tcp_tcph;
		tcph->th_flags[0] = (TH_ACK|TH_PSH);

		{
		u32	sum;
		sum = len + tcp->tcp_tcp_hdr_len + tcp->tcp_sum;
		sum = (sum >> 16) + (sum & 0xFFFF);
		U16_TO_ABE16(sum, ALIGN16(tcph->th_sum));
		}

		U32_TO_ABE32(snxt, ALIGN32(tcph->th_seq));

		BUMP_MIB(tcp_mib.tcpOutDataSegs);
		UPDATE_MIB(tcp_mib.tcpOutDataBytes, len);
		BUMP_LOCAL(tcp->tcp_obsegs);

		tcp->tcp_last_sent_len = (u_short)len;

		hdrlen = len + tcp->tcp_hdr_len;
		U16_TO_ABE16(hdrlen, ALIGN16(tcp->tcp_iph.iph_length));

		/* see if we need to allocate a mblk for the headers */
		hdrlen = tcp->tcp_hdr_len;
		rptr = mp1->b_rptr - hdrlen;
		db = mp1->b_datap;
		if ((db->db_ref != 2) ||
		    ((rptr - db->db_base) < 0) ||
		    (!OK_32PTR(rptr))) {
			/* NOTE: we assume allocb returns an OK_32PTR */
#ifdef TCP_PERF
			if (!OK_32PTR(rptr))
				tcp_must_alloc_allign++;
			else if (db->db_ref != 2)
				tcp_must_alloc_ref++;
			else if ((rptr - db->db_base) < 0)
				tcp_must_alloc_space++;
#endif
			mp = allocb(TCP_MAX_COMBINED_HEADER_LENGTH +
				tcp_wroff_xtra, BPRI_MED);
			if (!mp) {
				freemsg(mp1);
				goto no_memory;
			}
			mp->b_cont = mp1;
			mp1 = mp;
			/* Leave room for Link Level header */
			/* hdrlen = tcp->tcp_hdr_len; */
			rptr = &mp1->b_rptr[tcp_wroff_xtra];
			mp1->b_wptr = &rptr[hdrlen];
#ifdef TCP_PERF
		} else {
			tcp_inline++;
#endif
		}
		mp1->b_rptr = rptr;

		/* copy header into outgoing packet */
		{
			u32	* dst = (u32 *)ALIGN32(rptr);
			u32	* src = (u32 *)ALIGN32(tcp->tcp_iphc);
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = src[3];
			dst[4] = src[4];
			dst[5] = src[5];
			dst[6] = src[6];
			dst[7] = src[7];
			dst[8] = src[8];
			dst[9] = src[9];
			if (hdrlen -= 40) {
				hdrlen >>= 2;
				dst += 10;
				src += 10;
				do {
					*dst++ = *src++;
				} while (--hdrlen);
			}
		}
#ifdef TCP_PERF
		tcp_wput_cnt_1++;
#ifdef TCP_PERF_LEN
		tcp_count_len(mp1);
#endif
#endif
		TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_OUT, "tcp_wput end:  q %X", q);
		putnext(tcp->tcp_wq, mp1);

		SYNC_CHK_OUT(tcp, "tcp_rput");
		return;

		/*
		 * If we ran out of memory, we pretend to have sent the packet
		 * and that it was lost on the wire.
		 */
no_memory:;

		SYNC_CHK_OUT(tcp, "tcp_rput");
		TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_OUT, "tcp_wput end:  q %X", q);
		return;

slow:;
		/* leftover work from above */
		tcp->tcp_unsent = len;
		tcp->tcp_xmit_tail_unsent = len;
		TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_OUT, "tcp_wput end:  q %X", q);
		tcp_wput_slow(tcp, (mblk_t *)0);
		SYNC_CHK_OUT(tcp, "tcp_rput");
		return;
	}

	TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_OUT, "tcp_wput end:  q %X", q);
	tcp_wput_slow(tcp, mp);
	SYNC_CHK_OUT(tcp, "tcp_rput");
}

/*
 * The TCP slow path write put procedure.
 * NOTE: the logic of the fast path is duplicated from tcp_wput_slow()
 */
staticf void
tcp_wput_slow (tcp, mp)
reg	tcp_t	* tcp;
reg	mblk_t	* mp;
{
	int	len;
	mblk_t	* local_time;
	mblk_t	* mp1;
	u_char	* rptr;
	u32	snxt;
	int	tail_unsent;
	int	tcp_state;
	int	usable;
	mblk_t	* xmit_tail;
	queue_t * q = tcp->tcp_wq;

	if (!mp) {
		/* Really tacky... but we need this for detached closes. */
		len = tcp->tcp_unsent;
		TRACE_3(TR_FAC_TCP, TR_TCP_WPUT_SLOW_IN,
			"tcp_wput_slow start:  q %X db_type 0%o tcp %X",
			0, 0, tcp);
	} else {
#if CCS_STATS
	wrw_stats.tot.count++;
	wrw_stats.tot.bytes += msgdsize(mp);
#endif
	tcp = (tcp_t *)q->q_ptr;
	TRACE_3(TR_FAC_TCP, TR_TCP_WPUT_SLOW_IN,
		"tcp_wput_slow start:  q %X db_type 0%o tcp %X",
		q, mp->b_datap->db_type, tcp);

	switch (mp->b_datap->db_type) {
	case M_DATA:
		break;
	case M_PROTO:
	case M_PCPROTO:
		rptr = mp->b_rptr;
		if ((mp->b_wptr - rptr) >= sizeof (long)) {
			long type;

			type = ((union T_primitives *)ALIGN32(rptr))->type;
			if (type == T_EXDATA_REQ) {
				len = msgdsize(mp->b_cont) - 1;
				if (len < 0) {
					freemsg(mp);
					TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
					"tcp_wput_slow end:  q %X", q);
					return;
				}
				len += tcp->tcp_unsent;
				len += tcp->tcp_snxt;
				tcp->tcp_urg = len;
				tcp->tcp_valid_bits |= TCP_URG_VALID;
			} else if (type != T_DATA_REQ) {
				tcp_wput_proto(q, mp);
				TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
				"tcp_wput_slow end:  q %X", q);
				return;
			}
			/* TODO: options, flags, ... from user */
			/* Set length to zero for reclamation below */
			mp->b_wptr = mp->b_rptr;
			break;
		}
		/* FALLTHRU */
	default:
		if (q->q_next) {
			putnext(q, mp);
			TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
				"tcp_wput_slow end:  q %X", q);
			return;
		}
		mi_strlog(q, 1, SL_ERROR|SL_TRACE, "tcp_wput_slow, dropping one...");
		freemsg(mp);
		TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
			"tcp_wput_slow end:  q %X", q);
		return;
	case M_IOCTL:
		tcp_wput_ioctl(q, mp);
		TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
			"tcp_wput_slow end:  q %X", q);
		return;
	case M_IOCDATA:
		tcp_wput_iocdata(q, mp);
		TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
			"tcp_wput_slow end:  q %X", q);
		return;
	case M_FLUSH:
		tcp_wput_flush(q, mp);
		TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
			"tcp_wput_slow end:  q %X", q);
		return;
	case M_IOCNAK:
		if (((struct iocblk *)mp->b_rptr)->ioc_cmd == I_SYNCSTR) {
			/*
			 * The M_IOCTL of I_SYNCSTR has made
			 * it back done as an M_IOCNAK.
			 */
			if (tcp->tcp_co_norm && isuioq(tcp->tcp_rq)) {
				/*
				 * Additional mblk(s) have been putnext()ed
				 * since the I_SYNCSTR was putnext()ed and we
				 * are still in SYNCSTR mode, so do it again.
				 */
				tcp->tcp_co_norm = 0;
				(void) struio_ioctl(tcp->tcp_rq, mp);
			} else {
				/*
				 * No additional mblk(s) putnext()ed,
				 * so save the mblk for reuse (switch
				 * to synchronous  mode) and check for
				 * a defered strwakeq() needed.
				 */
				tcp->tcp_co_imp = mp;
				if (tcp->tcp_co_wakeq_need) {
					tcp->tcp_co_wakeq_need = 0;
					tcp->tcp_co_wakeq_done = 1;
					strwakeq(tcp->tcp_rq, QWANTR);
				}
			}
			TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
				"tcp_wput_slow end:  q %X", q);
			return;
		}
		if (q->q_next) {
			putnext(q, mp);
			SYNC_CHK_OUT(otcp, "tcp_wput_slow");
			TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
				"tcp_wput_slow end:  q %X", q);
			return;
		}
		mi_strlog(q, 1, SL_ERROR|SL_TRACE, "tcp_wput_slow, dropping one...");
		freemsg(mp);
		TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
			"tcp_wput_slow end:  q %X", q);
		return;
	}
	tcp_state = tcp->tcp_state;
	/*
	 * Don't allow data after data after T_ORDREL_REQ or T_DISCON_REQ,
	 * or before a connection attempt has begun.
	 */
	if (tcp_state < TCPS_SYN_SENT || tcp_state > TCPS_CLOSE_WAIT) {
		freemsg(mp);
		TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
			"tcp_wput_slow end:  q %X", q);
		return;
	}

	/* Strip empties */
	for (;;) {
		len = mp->b_wptr - mp->b_rptr;
		if (len > 0)
			break;
		mp1 = mp;
		mp = mp->b_cont;
		freeb(mp1);
		if (!mp) {
			TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT,
				"tcp_wput_slow end:  q %X", q);
			return;
		}
	}

	/* If we are the first on the list ... */
	if (!tcp->tcp_xmit_head) {
		tcp->tcp_xmit_head = mp;
		tcp->tcp_xmit_tail = mp;
		tcp->tcp_xmit_tail_unsent = len;
	} else {
		tcp->tcp_xmit_last->b_cont = mp;
		len += tcp->tcp_unsent;
	}

	/* Tack on however many more positive length mblks we have */
	if ((mp1 = mp->b_cont) != NULL) {
		do {
			int tlen = mp1->b_wptr - mp1->b_rptr;
			if (tlen <= 0) {
				mp->b_cont = mp1->b_cont;
				freeb(mp1);
			} else {
				len += tlen;
				mp = mp1;
			}
		} while ((mp1 = mp->b_cont) != NULL);
	}
	tcp->tcp_xmit_last = mp;
	tcp->tcp_unsent = len;
	}

	snxt = tcp->tcp_snxt;

	if (tcp_state == TCPS_SYN_RCVD) {
		/*
		 * The three-way connection establishment handshake
		 * is not complete yet. We want to queue the data
		 * for transmission after entering ESTABLISHED
		 * state (RFC793). Setting usable to zero cause
		 * a jump to "done" label effectively leaving
		 * data on the queue.
		 */

		usable = 0;

	} else {
		reg int usable_r = tcp->tcp_swnd;
		/* usable = MIN(swnd, cwnd) - unacked_bytes */
		if (usable_r > tcp->tcp_cwnd)
			usable_r = tcp->tcp_cwnd;

		/* NOTE: trouble if xmitting while SYN not acked? */
		usable_r -= snxt;
		usable_r += tcp->tcp_suna;

		/* usable = MIN(usable, unsent) */
		if (usable_r > len)
			usable_r = len;
		usable = usable_r;
	}

	xmit_tail = tcp->tcp_xmit_tail;
	tail_unsent = tcp->tcp_xmit_tail_unsent;

	local_time = (mblk_t *)lbolt;

	/* Check nagle limit */
	len = tcp->tcp_mss;
	if (len > usable) {
		if (usable < (int)tcp->tcp_naglim &&
		    tcp->tcp_naglim > tcp->tcp_last_sent_len &&
		    snxt != tcp->tcp_suna) {
			/*
			 * Send urgent data ignoring the Nagle algorithm.
			 * This reduces the probability that urgent
			 * bytes get "merged" together.
			 */
			if (!(tcp->tcp_valid_bits & TCP_URG_VALID))
				goto done;
		}
	}
	for (;;) {
		struct datab	* db;
		tcph_t	* tcph;
		u32	sum;

		len = tcp->tcp_mss;
		if (len > usable) {
			len = usable;
			if (len <= 0)
				/* Terminate the loop */
				goto done;
			/* Sender silly-window avoidance */
			/* TODO: force data into microscopic window ?? */
			/* TODO: ==> (!pushed  || (unsent > usable)) */
			if (len < (tcp->tcp_max_swnd >> 1)
			&& (tcp->tcp_unsent - (snxt - tcp->tcp_snxt)) > len) {
				/*
				 * If the retransmit timer is not running
				 * we start it so that we will retransmit
				 * in the case when the the receiver has
				 * decremented the window.
				 */
				/* TODO what should the timer value be? */
				if (snxt == tcp->tcp_snxt &&
				    snxt == tcp->tcp_suna)
					TCP_TIMER_RESTART(tcp, tcp->tcp_rto);
				goto done;
			}
		}

		tcph = tcp->tcp_tcph;

		usable -= len;	/* Approximate - can be adjusted later */
		if (usable)
			tcph->th_flags[0] = TH_ACK;
		else
			tcph->th_flags[0] = (TH_ACK | TH_PSH);

		/*
		 * Prime pump for IP's checksumming on our behalf
		 * Include the adjustment for a source route if any.
		 */
		sum = len + tcp->tcp_tcp_hdr_len + tcp->tcp_sum;
		sum = (sum >> 16) + (sum & 0xFFFF);
		U16_TO_ABE16(sum, ALIGN16(tcph->th_sum));

		U32_TO_ABE32(snxt, ALIGN32(tcph->th_seq));

		if (tcp->tcp_valid_bits) {
			u_char * prev_rptr = xmit_tail->b_rptr;
			u32	prev_snxt = tcp->tcp_snxt;
			
			xmit_tail->b_rptr = xmit_tail->b_wptr - tail_unsent;
			mp = tcp_xmit_mp(tcp, xmit_tail, len, snxt, 0);
			/* Restore tcp_snxt so we get amount sent right. */
			tcp->tcp_snxt = prev_snxt;
			if (prev_rptr == xmit_tail->b_rptr)
				xmit_tail->b_prev = local_time;
			else
				xmit_tail->b_rptr = prev_rptr;
			if (!mp)
				break;

			mp1 = mp->b_cont;
			/* 
			 * tcp_xmit_mp might not give us all of len
			 * in order to preserve mblk boundaries.
			 */
			len = msgdsize(mp1);
			snxt += len;	
			tcp->tcp_last_sent_len = (u_short)len;
			while (mp1->b_cont) {
				xmit_tail = xmit_tail->b_cont;
				xmit_tail->b_prev = local_time;
				mp1 = mp1->b_cont;
			}
			tail_unsent = xmit_tail->b_wptr - mp1->b_wptr;
			BUMP_LOCAL(tcp->tcp_obsegs);
			BUMP_MIB(tcp_mib.tcpOutDataSegs);
			UPDATE_MIB(tcp_mib.tcpOutDataBytes, len);
			putnext(tcp->tcp_wq, mp);
			continue;
		}

		snxt += len;	/* Adjust later if we don't send all of len */
		BUMP_MIB(tcp_mib.tcpOutDataSegs);
		UPDATE_MIB(tcp_mib.tcpOutDataBytes, len);

		if (tail_unsent) {
			/* Are the bytes above us in flight? */
			rptr = xmit_tail->b_wptr - tail_unsent;
			if (rptr != xmit_tail->b_rptr) {
				tail_unsent -= len;
				len += tcp->tcp_hdr_len;
				U16_TO_ABE16(len,
					     ALIGN16(tcp->tcp_iph.iph_length));
				mp = dupb(xmit_tail);
				if (!mp)
					break;
				mp->b_rptr = rptr;
#ifdef TCP_PERF
				tcp_must_alloc_tail++;
#endif
				goto must_alloc;
			}
		} else {
			xmit_tail = xmit_tail->b_cont;
			tail_unsent = xmit_tail->b_wptr - xmit_tail->b_rptr;
		}

		tail_unsent -= len;
		tcp->tcp_last_sent_len = (u_short)len;

		len += tcp->tcp_hdr_len;
		U16_TO_ABE16(len, ALIGN16(tcp->tcp_iph.iph_length));

		xmit_tail->b_prev = local_time;

		mp = dupb(xmit_tail);
		if (!mp)
			break;

		len = tcp->tcp_hdr_len;
		/*
		 * There are four reasons to allocate a new hdr mblk:
		 *  1) The bytes above us are in use by another packet
		 *  2) We don't have good alignment
		 *  3) The mblk is being shared
		 *  4) We don't have enough room for a header
		 */
		rptr = mp->b_rptr - len;
		if (!OK_32PTR(rptr)
		|| ((db = mp->b_datap), db->db_ref != 2)
		|| (rptr - db->db_base) < 0) {
			/* NOTE: we assume allocb returns an OK_32PTR */
#ifdef TCP_PERF
			if (!OK_32PTR(rptr))
				tcp_must_alloc_allign++;
			else if (db->db_ref != 2)
				tcp_must_alloc_ref++;
			else if ((rptr - db->db_base) < 0)
				tcp_must_alloc_space++;
#endif

		must_alloc:;
			
			mp1=allocb(TCP_MAX_COMBINED_HEADER_LENGTH +
				   tcp_wroff_xtra, BPRI_MED);
			if (!mp1)
				break;
			mp1->b_cont = mp;
			mp = mp1;
			/* Leave room for Link Level header */
			len = tcp->tcp_hdr_len;
			rptr = &mp->b_rptr[tcp_wroff_xtra];
			mp->b_wptr = &rptr[len];
#ifdef TCP_PERF
		} else {
			tcp_inline++;
#endif
		}
		mp->b_rptr = rptr;
		{
		u32	* dst = (u32 *)ALIGN32(rptr);
		u32	* src = (u32 *)ALIGN32(tcp->tcp_iphc);
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
		dst[3] = src[3];
		dst[4] = src[4];
		dst[5] = src[5];
		dst[6] = src[6];
		dst[7] = src[7];
		dst[8] = src[8];
		dst[9] = src[9];
		if (len -= 40) {
			len >>= 2;
			dst += 10;
			src += 10;
			do {
				*dst++ = *src++;
			} while (--len);
		}
		}

		if (tail_unsent) {
			mp1 = mp->b_cont;
			if (!mp1)
				mp1 = mp;
			/*
			 * If we're a little short, tack on more mblks
			 * as long as we don't need to split an mblk.
			 */
			if (tail_unsent < 0 &&
			    tail_unsent + xmit_tail->b_cont->b_wptr -
			    xmit_tail->b_cont->b_rptr <= 0) {
				do {
					xmit_tail = xmit_tail->b_cont;
					/* Stash for rtt use later */
					xmit_tail->b_prev = local_time;
					mp1->b_cont = dupb(xmit_tail);
					mp1 = mp1->b_cont;
					tail_unsent += xmit_tail->b_wptr -
						xmit_tail->b_rptr;
					if (!mp1)
						goto out_of_mem;
				} while (tail_unsent < 0 &&
					 tail_unsent + 
					 xmit_tail->b_cont->b_wptr -
					 xmit_tail->b_cont->b_rptr <= 0);
			}
			/* Trim back any surplus on the last mblk */
			if (tail_unsent > 0)
				mp1->b_wptr -= tail_unsent;
			if (tail_unsent < 0) {
				/* We did not send everything we could in
				 * order to preserve mblk boundaries. 
				 */
				usable -= tail_unsent;
				snxt += tail_unsent;
				tcp->tcp_last_sent_len += tail_unsent;
				UPDATE_MIB(tcp_mib.tcpOutDataBytes, 
					   tail_unsent);
				/* 
				 * Adjust the checksum
				 */
				tcph = (tcph_t *)(rptr + tcp->tcp_ip_hdr_len);
				sum += tail_unsent;
				sum = (sum >> 16) + (sum & 0xFFFF);
				U16_TO_ABE16(sum, ALIGN16(tcph->th_sum));
#ifdef _BIG_ENDIAN
				((ipha_t *)ALIGN32(rptr))->ipha_length +=
					tail_unsent;
#else
				/* for little endian systems need to swap */
				sum = BE16_TO_U16(((iph_t *)rptr)->iph_length) +
				  tail_unsent;
				U16_TO_BE16(sum, ((iph_t *)rptr)->iph_length);
#endif
				tail_unsent = 0;
			}
		}

		BUMP_LOCAL(tcp->tcp_obsegs);
#ifdef TCP_PERF
		tcp_wput_cnt_1++;
		if (mp1 = mp->b_cont) {
			tcp_wput_cnt_2++;
			if (mp1 = mp1->b_cont) {
				tcp_wput_cnt_3++;
				if (mp1 = mp1->b_cont) {
					tcp_wput_cnt_4++;
				}
			}
		}
#ifdef TCP_PERF_LEN
		tcp_count_len(mp);
#endif
#endif
		putnext(tcp->tcp_wq, mp);
	}
out_of_mem:;
	if (mp)
		freemsg(mp);
	/* Pretend that all we were trying to send really got sent */
	if (tail_unsent < 0) {
		do {
			xmit_tail = xmit_tail->b_cont;
			xmit_tail->b_prev = local_time;
			tail_unsent += xmit_tail->b_wptr - xmit_tail->b_rptr;
		} while (tail_unsent < 0);
	}
done:;
	tcp->tcp_xmit_tail = xmit_tail;
	tcp->tcp_xmit_tail_unsent = tail_unsent;
	len = tcp->tcp_snxt - snxt;
	if (len) {
		tcp->tcp_snxt = snxt + tcp->tcp_fin_sent;
		tcp->tcp_rack = tcp->tcp_rnxt;
		tcp->tcp_rack_cnt = 0;
		if ((snxt + len) == tcp->tcp_suna)
			TCP_TIMER_RESTART(tcp, tcp->tcp_rto);
	} else if (snxt == tcp->tcp_suna && tcp->tcp_swnd == 0) {
		/*
		 * Didn't send anything. Make sure the timer is running
		 * so that we will probe a zero window.
		 */
		TCP_TIMER_RESTART(tcp, tcp->tcp_rto);
	}
	/* Note that len is the amount we just sent but with a negative sign */
	len += tcp->tcp_unsent;
	tcp->tcp_unsent = len;
	if (tcp->tcp_flow_stopped) {
		if (len <= tcp->tcp_xmit_lowater) {
#ifdef TCP_PERF
			tcp_snd_flow_on++;
#endif
			tcp->tcp_flow_stopped = false;
			enableok(tcp->tcp_wq);
			rmvq(tcp->tcp_wq, tcp->tcp_flow_mp);
		}
	} else {
		/* The tcp_flow_mp of detached folks is nil */
		if (len >= tcp->tcp_xmit_hiwater
		&&  tcp->tcp_flow_mp) {
#ifdef TCP_PERF
			tcp_snd_flow_off++;
#endif
			tcp->tcp_flow_stopped = true;
			noenable(tcp->tcp_wq);
			(void) putq(tcp->tcp_wq, tcp->tcp_flow_mp);
		}
	}
	TRACE_1(TR_FAC_TCP, TR_TCP_WPUT_SLOW_OUT, "tcp_wput_slow end:  q %X", q);
}

/* tcp_wput_flush is called by tcp_wput_slow to handle M_FLUSH messages. */
staticf void
tcp_wput_flush (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	u_char	fval = *mp->b_rptr;
	mblk_t	* tail;
	tcp_t	* tcp = (tcp_t *)q->q_ptr;

	/* TODO: How should flush interact with urgent data? */
	if ((fval & FLUSHW)
	&&  tcp->tcp_xmit_head
	&&  !(tcp->tcp_valid_bits & TCP_URG_VALID)) {
		/*
		 * Flush only data that has not yet been put
		 * on the wire.  If we flush data that we have
		 * already transmitted, life, as we know it,
		 * may come to an end.
		 */
		tail = tcp->tcp_xmit_tail;
		tail->b_wptr -= tcp->tcp_xmit_tail_unsent;
		tcp->tcp_xmit_tail_unsent = 0;
		tcp->tcp_unsent = 0;
		if (tail->b_wptr != tail->b_rptr)
			tail = tail->b_cont;
		if (tail) {
			mblk_t ** excess = &tcp->tcp_xmit_head;
			for (;;) {
				mblk_t * mp1 = *excess;
				if (mp1 == tail)
					break;
				tcp->tcp_xmit_tail = mp1;
				tcp->tcp_xmit_last = mp1;
				excess = &mp1->b_cont;
			}
			*excess = nilp(mblk_t);
			freemsg(tail);
		}
	}
	/*
	 * TODO: you can't just flush these, you have to increase
	 * rwnd for one thing.  For another, how should urgent data
	 * interact?
	 */
	if (fval & FLUSHR) {
		*mp->b_rptr = fval & ~FLUSHW;
		qreply(q, mp);
		return;
	}
	freemsg(mp);
}

/* tcp_wput_iocdata is called by tcp_wput_slow to handle all M_IOCDATA messages. */
staticf void
tcp_wput_iocdata (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	char	* addr_cp;
	ipa_t	* ipaddr;
	mblk_t	* mp1;
	struct netbuf * nb;
	char	* port_cp;
	tcp_t	* tcp;

	/* Make sure it is one of ours. */
	switch ( ((struct iocblk *)ALIGN32(mp->b_rptr))->ioc_cmd ) {
	case TI_GETMYNAME:
	case TI_GETPEERNAME:
		break;
	default:
		putnext(q, mp);
		return;
	}
	switch ( mi_copy_state(q, mp, &mp1) ) {
	case -1:
		return;
	case MI_COPY_CASE(MI_COPY_IN, 1):
		break;
	case MI_COPY_CASE(MI_COPY_OUT, 1):
		/* Copy out the netbuf. */
		mi_copyout(q, mp);
		return;
	case MI_COPY_CASE(MI_COPY_OUT, 2):
		/* All done. */
		mi_copy_done(q, mp, 0);
		return;
	default:
		mi_copy_done(q, mp, EPROTO);
		return;
	}
	nb = (struct netbuf *)ALIGN32(mp1->b_rptr);
	if ( nb->maxlen < sizeof(ipa_t) ) {
		mi_copy_done(q, mp, EINVAL);
		return;
	}
	tcp = (tcp_t *)q->q_ptr;
	switch ( ((struct iocblk *)ALIGN32(mp->b_rptr))->ioc_cmd ) {
	case TI_GETMYNAME:
		addr_cp = (char *)tcp->tcp_iph.iph_src;
		port_cp = (char *)tcp->tcp_tcph->th_lport;
		break;
	case TI_GETPEERNAME:
		addr_cp = (char *)&tcp->tcp_remote;
		port_cp = (char *)tcp->tcp_tcph->th_fport;
		break;
	default:
		mi_copy_done(q, mp, EPROTO);
		return;
	}
	mp1 = mi_copyout_alloc(q, mp, nb->buf, sizeof(ipa_t));
	if ( !mp1 )
		return;
	nb->len = sizeof(ipa_t);
	ipaddr = (ipa_t *)ALIGN32(mp1->b_rptr);
	mp1->b_wptr = (u_char *)&ipaddr[1];
	bzero((char *)ipaddr, sizeof(ipa_t));
	ipaddr->ip_family = AF_INET;
	bcopy(addr_cp, (char *)&ipaddr->ip_addr, IP_ADDR_LEN);
	bcopy(port_cp, (char *)&ipaddr->ip_port, 2);
	/* Copy out the address */
	mi_copyout(q, mp);
}

/* tcp_wput_ioctl is called by tcp_wput_slow to handle all M_IOCTL messages. */
staticf void
tcp_wput_ioctl (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	tcp_t	* tcp = (tcp_t *)q->q_ptr;
	struct iocblk	* iocp;

	iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
	switch (iocp->ioc_cmd) {
	case TCP_IOC_DEFAULT_Q:
		/* Wants to be the default wq. */
		if ( !tcp->tcp_priv_stream ) {
			iocp->ioc_error = EPERM;
			goto err_ret;
		}
		tcp_def_q_set(q, mp);
		return;
	case TI_GETPEERNAME:
		if ( tcp->tcp_state < TCPS_SYN_RCVD ) {
			iocp->ioc_error = ENOTCONN;
err_ret:;
			iocp->ioc_count = 0;
			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			return;
		}
		/* FALLTHRU */
	case TI_GETMYNAME:
		mi_copyin(q, mp, nilp(char), sizeof(struct netbuf));
		return;
	case ND_SET:
		if ( !tcp->tcp_priv_stream ) {
			iocp->ioc_error = EPERM;
			goto err_ret;
		}
		TCP_LOCK_WRITE();
		if (!nd_getset(q, tcp_g_nd, mp)) {
			TCP_UNLOCK_WRITE();
			break;
		}
		TCP_UNLOCK_WRITE();

		qreply(q, mp);
		return;

	case ND_GET:
		TCP_LOCK_READ();
		if (!nd_getset(q, tcp_g_nd, mp)) {
			TCP_UNLOCK_READ();
			break;
		}
		TCP_UNLOCK_READ();

		qreply(q, mp);
		return;
	}
	putnext(q, mp);
}

/*
 * This routine is called by tcp_wput_slow to handle all TPI requests other than
 * T_DATA_IND.
 */
staticf void
tcp_wput_proto (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	tcp_t	* tcp = (tcp_t *)q->q_ptr;
	union T_primitives * tprim = (union T_primitives *)ALIGN32(mp->b_rptr);

	switch ((int)tprim->type) {
	case T_BIND_REQ:	/* bind request */
		tcp_bind(q, mp);
		return;
	case T_UNBIND_REQ:	/* unbind request */
		tcp_unbind(q, mp);
		return;
	case T_CONN_RES:	/* connection response */
		tcp_accept(q, mp);
		return;
	case T_CONN_REQ:	/* connection request */
		tcp_connect(q, mp);
		return;
	case T_DISCON_REQ:	/* disconnect request */
		tcp_disconnect(q, mp);
		return;
	case T_INFO_REQ:	/* information request */
		tcp_info_req(tcp, mp);
		return;
	case T_OPTMGMT_REQ:	/* manage options req */
		if (!snmpcom_req(q, mp, tcp_snmp_set, tcp_snmp_get
				 , tcp->tcp_priv_stream))
			optcom_req(tcp->tcp_wq, mp, tcp_opt_set, tcp_opt_get
				 , tcp_opt_chk, tcp->tcp_priv_stream);
		return;
	case T_UNITDATA_REQ:	/* unitdata request */
		tcp_err_ack(tcp->tcp_wq, mp, TNOTSUPPORT, 0);
		return;
	case T_ORDREL_REQ:	/* orderly release req */
		freemsg(mp);
		if (tcp_xmit_end(tcp) != 0) {
			mi_strlog(q, 1, SL_ERROR|SL_TRACE
			 , "tcp_wput_slow, T_ORDREL_REQ out of state %s"
			 , tcp_display(tcp));
			putctl1(tcp->tcp_rq, M_ERROR, EPROTO);
		}
		return;
	default:
		mi_strlog(q, 1, SL_ERROR|SL_TRACE
		 , "tcp_wput_slow, bogus TPI msg, type %d", tprim->type);
		freemsg(mp);
		putctl1(tcp->tcp_rq, M_ERROR, EPROTO);
		return;
	}
}

/*
 * The TCP write service routine.  The only thing we expect to see here is
 * timer messages.
 */
staticf void
tcp_wsrv (q)
	queue_t	* q;
{
	mblk_t	* mp;
#ifdef	SYNC_CHK
	tcp_t	* otcp = (tcp_t *)q->q_ptr;
#endif
	TRACE_1(TR_FAC_TCP, TR_TCP_WSRV_IN, "tcp_wsrv start:  q %X", q);
	SYNC_CHK_IN(otcp, "tcp_wsrv");

	/*
	 * Write side has is always noenable()'d so that putting tcp_flow_mp
	 * on the queue will not cause the service procedure to run.
	 */
	while ((mp = getq(q)) != NULL) {
		if (mp->b_datap->db_type != M_PCSIG) {
			(void) putbq(q, mp);
			break;
		}
		if (mi_timer_valid(mp)) {
			tcpt_t	* tcpt = (tcpt_t *)ALIGN32(mp->b_rptr);

			ASSERT(tcpt->tcpt_tcp->tcp_wq == q);
			(*tcpt->tcpt_pfv)(tcpt->tcpt_tcp);
		}
	}
	SYNC_CHK_OUT(otcp, "tcp_wsrv");
	TRACE_1(TR_FAC_TCP, TR_TCP_WSRV_OUT, "tcp_wsrv end:  q %X", q);
}

/* Non overlapping byte exchanger */
staticf void
tcp_xchg (a, b, len)
	u_char	* a;
	u_char	* b;
	int	len;
{
	u_char	uch;

	while (len-- > 0) {
		uch = a[len];
		a[len] = b[len];
		b[len] = uch;
	}
}

/*
 * Send out a control packet on the tcp connection specified.  This routine
 * is typically called where we need a simple ACK or RST generated.
 */
staticf void
tcp_xmit_ctl (str, tcp, mp, seq, ack, ctl)
	char	* str;
	tcp_t	* tcp;
	mblk_t	* mp;
	u_long	seq;
	u_long	ack;
	int	ctl;
{
	u_char	* rptr;
	tcph_t	* tcph;
	iph_t	* iph;
	u32	sum;

	if (mp) {
		iph = (iph_t *)mp->b_rptr;
		ASSERT(((iph->iph_version_and_hdr_length) & 0xf0) == 0x40);
		tcph = (tcph_t *)(mp->b_rptr + IPH_HDR_LENGTH(iph));
		if ( tcph->th_flags[0] & TH_RST ) {
			freemsg(mp);
			return;
		}
		freemsg(mp);
	}
	/* If a text string is passed in with the request, pass it to strlog. */
	if ( str ) {
		mi_strlog(tcp->tcp_rq, 1, SL_TRACE
		 , "tcp_xmit_ctl: '%s', seq 0x%x, ack 0x%x, ctl 0x%x"
		 , str, seq, ack, ctl);
	}
	mp = allocb(tcp->tcp_hdr_len + tcp_wroff_xtra, BPRI_MED);
	if (!mp)
		return;
	rptr = &mp->b_rptr[tcp_wroff_xtra];
	mp->b_rptr = rptr;
	mp->b_wptr = &rptr[tcp->tcp_hdr_len];
	bcopy(tcp->tcp_iphc, (char *)rptr, tcp->tcp_hdr_len);
	iph = (iph_t *)rptr;
	U16_TO_BE16(tcp->tcp_hdr_len, iph->iph_length);
	tcph = (tcph_t *)&rptr[tcp->tcp_ip_hdr_len];
	tcph->th_flags[0] = (u8)ctl;
	if (ctl & TH_RST) {
		BUMP_MIB(tcp_mib.tcpOutRsts);
		BUMP_MIB(tcp_mib.tcpOutControl);
	}
	if (ctl & TH_ACK) {
		tcp->tcp_rack = ack;
		tcp->tcp_rack_cnt = 0;
		BUMP_MIB(tcp_mib.tcpOutAck);
	}
	BUMP_LOCAL(tcp->tcp_obsegs);
	U32_TO_BE32(seq, tcph->th_seq);
	U32_TO_BE32(ack, tcph->th_ack);
	/*
	 * Include the adjustment for a source route if any.
	 */
	sum = sizeof(tcph_t) + tcp->tcp_sum;
	sum = (sum >> 16) + (sum & 0xFFFF);
	U16_TO_BE16(sum, tcph->th_sum);
	putnext(tcp->tcp_wq, mp);
}

/*
 * Generate a reset based on an inbound packet for which there is no active
 * tcp state that we can find.
 */
staticf void
tcp_xmit_early_reset (str, q, mp, seq, ack, ctl)
	char	* str;
	queue_t	* q;
	mblk_t	* mp;
	u_long	seq;
	u_long	ack;
	int	ctl;
{
	iph_t	* iph;
	u_short	len;
	tcph_t	* tcph;
	int	i;

	if ( str  &&  q ) {
		mi_strlog(q, 1, SL_TRACE
		 , "tcp_xmit_early_reset: '%s', seq 0x%x ack 0x%x, flags 0x%x"
		 , str, seq, ack, ctl);
	}
	if (mp->b_datap->db_ref != 1) {
		mblk_t * mp1 = copyb(mp);
		freemsg(mp);
		mp = mp1;
		if (!mp)
			return;
	} else if (mp->b_cont) {
		freemsg(mp->b_cont);
		mp->b_cont = nilp(mblk_t);
	}
	/*
	 * We skip reversing source route here.
	 * (for now we replace all IP options with EOL)
	 */
	iph = (iph_t *)mp->b_rptr;
	len = IPH_HDR_LENGTH(iph);
	for (i = IP_SIMPLE_HDR_LENGTH; i < (int)len; i++)
		mp->b_rptr[i] = IPOPT_EOL;
	tcph = (tcph_t *)&mp->b_rptr[len];
	if ( tcph->th_flags[0] & TH_RST ) {
		freemsg(mp);
		return;
	}
	tcph->th_offset_and_rsrvd[0] = (5 << 4);
	len += sizeof(tcph_t);
	mp->b_wptr = &mp->b_rptr[len];
	U16_TO_BE16(len, iph->iph_length);
	tcp_xchg(iph->iph_src, iph->iph_dst, 4);
	tcp_xchg(tcph->th_fport, tcph->th_lport, 2);
	U32_TO_BE32(ack, tcph->th_ack);
	U32_TO_BE32(seq, tcph->th_seq);
	U16_TO_BE16(0, tcph->th_win);
	U16_TO_BE16(sizeof(tcph_t), tcph->th_sum);
	tcph->th_flags[0] = (u8)ctl;
	if (ctl & TH_RST) {
		BUMP_MIB(tcp_mib.tcpOutRsts);
		BUMP_MIB(tcp_mib.tcpOutControl);
	}
	qreply(q, mp);
}

/*
 * Initiate closedown sequence on an active connection.  (May be called as
 * writer.)
 * Return value zero for OK return, non-zero for error return.
 */
staticf int
tcp_xmit_end (tcp)
	tcp_t	* tcp;
{
	struct iocblk	* ioc;
	ipic_t	* ipic;
	mblk_t	* mp;
	mblk_t	* mp1;

	if (tcp->tcp_state < TCPS_SYN_RCVD ||
	    tcp->tcp_state > TCPS_CLOSE_WAIT) {
		/*
		 * Invalid state, only states TCPS_SYN_RCVD,
		 * TCPS_ESTABLISHED and TCPS_CLOSE_WAIT are valid
		 */
		return (-1);
	}

	tcp->tcp_fss = tcp->tcp_snxt + tcp->tcp_unsent;
	tcp->tcp_valid_bits |= TCP_FSS_VALID;
	/*
	 * If there is nothing more unsent, send the FIN now.
	 * Otherwise, it will go out with the last segment.
	 */
	if (tcp->tcp_unsent == 0) {
		mp = tcp_xmit_mp(tcp, nilp(mblk_t), 0, tcp->tcp_fss, 0);
		if (mp) {
			putnext(tcp->tcp_wq, mp);
		} else {
			/*
			 * Couldn't allocate msg.  Pretend we got it out.
			 * Wait for rexmit timeout.
			 */
			tcp->tcp_snxt = tcp->tcp_fss + 1;
			TCP_TIMER_RESTART(tcp, tcp->tcp_rto);
		}
	}

	/* Don't allow folks to set the rtt unless they have some experience */
	/* NOTE: do we need to exclude old data? (remember when rtt was set) */
	if ((tcp->tcp_snxt - tcp->tcp_iss) < 1024)
		return 0;

	/* NOTE: should not update if source routes i.e. if tcp_remote if
	 * different from iph_dst.
	 */
	if (tcp->tcp_remote !=  tcp->tcp_ipha.ipha_dst) {
		return 0;
	}
	/*
	 * Record the final round trip estimate in the IRE for use by
	 * future connections.
	 */
	mp = allocb(sizeof(ipic_t) + IP_ADDR_LEN, BPRI_HI);
	if (!mp)
		return 0;
	bzero((char *)mp->b_rptr, sizeof(ipic_t) + IP_ADDR_LEN);
	ipic = (ipic_t *)ALIGN32(mp->b_rptr);
	ipic->ipic_cmd = IP_IOC_IRE_ADVISE_NO_REPLY;
	ipic->ipic_addr_offset = sizeof(ipic_t);
	ipic->ipic_addr_length = IP_ADDR_LEN;
	ipic->ipic_rtt = tcp->tcp_rto;
	/* ipic->ipic_max_frag = tcp->tcp_mss; */
	bcopy((char *)&tcp->tcp_iph.iph_dst, (char *)&ipic[1], IP_ADDR_LEN);
	mp->b_wptr = &mp->b_rptr[sizeof(ipic_t) + IP_ADDR_LEN];
	mp1 = allocb(sizeof(struct iocblk), BPRI_HI);
	if (!mp1) {
		freemsg(mp);
		return 0;
	}
	mp1->b_cont = mp;
	mp1->b_datap->db_type = M_IOCTL;
	bzero((char *)mp1->b_rptr, sizeof(struct iocblk));
	ioc = (struct iocblk *)ALIGN32(mp1->b_rptr);
	ioc->ioc_cmd = IP_IOCTL;
	mp1->b_wptr = (u_char *)&ioc[1];
	ioc->ioc_count = sizeof(ipic_t) + IP_ADDR_LEN;
	putnext(tcp->tcp_wq, mp1);
	return 0;
}

/*
 * Generate a "no listener here" reset in response to the
 * connection request contained within 'mp'
 */
staticf void
tcp_xmit_listeners_reset (rq, mp)
	queue_t	* rq;
	mblk_t	* mp;
{
	u_char	* rptr	= mp->b_rptr;
	u_long	seg_len = IPH_HDR_LENGTH(rptr);
	tcph_t	* tcph	= (tcph_t *)&rptr[seg_len];
	u_long	seg_seq = BE32_TO_U32(tcph->th_seq);
	u_long	seg_ack = BE32_TO_U32(tcph->th_ack);
	u_long	flags = (unsigned int)tcph->th_flags[0] & 0xFF;

	seg_len = msgdsize(mp) - (TCP_HDR_LENGTH(tcph) + seg_len);
	flags = (unsigned int)tcph->th_flags[0] & 0xFF;
	if (flags & TH_RST)
		freemsg(mp);
	else if (flags & TH_ACK) {
		tcp_xmit_early_reset("no tcp, reset",
				     rq, mp, seg_ack, 0, TH_RST);
	} else {
		if ( flags & TH_SYN )
			seg_len++;
		tcp_xmit_early_reset("no tcp, reset/ack", rq,
				     mp, 0, seg_seq + seg_len,
				     TH_RST | TH_ACK);
	}
}

/*
 * tcp_xmit_mp is called to return a pointer to an mblk chain complete with
 * ip and tcp header ready to pass down to IP.  If the mp passed in is
 * non-nil, then up to max_to_send bytes of data will be dupped off that
 * mblk. (If sendall is not set the dup'ing will stop at an mblk boundary
 * otherwise it will dup partial mblks.)
 * Otherwise, an appropriate ACK packet will be generated.  This
 * routine is not usually called to send new data for the first time.  It
 * is mostly called out of the timer for retransmits, and to generate ACKs.
 */
staticf mblk_t *
tcp_xmit_mp (tcp, mp, max_to_send, seq, sendall)
	tcp_t	* tcp;
	mblk_t	* mp;
	u_long	max_to_send;
	u_long	seq;
	int	sendall;
{
	int	data_length;
	u_int	flags;
	mblk_t	* mp1;
	mblk_t	* mp2;
	u_char	* rptr;
	tcph_t	* tcph;

	/* Allocate for our header + link-level + mss option */
	mp1 = allocb(tcp->tcp_hdr_len + tcp_wroff_xtra + 4, BPRI_MED);
	if (!mp1)
		return nilp(mblk_t);
	data_length = 0;
	for (mp2 = mp1; mp  &&  data_length != max_to_send; mp = mp->b_cont) {
		/* This could be faster with cooperation from downstream */
		if (mp2 != mp1 && sendall == 0 &&
		    data_length + mp->b_wptr - mp->b_rptr > max_to_send)
			/*
			 * Don't send the next mblk since the whole mblk
			 * does not fit.
			 */
			break;
		mp2->b_cont = dupb(mp);
		mp2 = mp2->b_cont;
		if (!mp2) {
			freemsg(mp1);
			return nilp(mblk_t);
		}
		data_length += mp2->b_wptr - mp2->b_rptr;
		if (data_length > max_to_send) {
			mp2->b_wptr -= data_length - max_to_send;
			data_length = max_to_send;
			break;
		}
	}
	rptr = mp1->b_rptr + tcp_wroff_xtra;
	mp1->b_rptr = rptr;
	mp1->b_wptr = rptr + tcp->tcp_hdr_len;
	bcopy(tcp->tcp_iphc, (char *)rptr, tcp->tcp_hdr_len);
	tcph = (tcph_t *)&rptr[tcp->tcp_ip_hdr_len];
	U32_TO_ABE32(seq, ALIGN32(tcph->th_seq));
	flags = TH_ACK;
	if (data_length != 0  &&  tcp->tcp_unsent == 0)
		flags = TH_ACK | TH_PSH;
	if (tcp->tcp_valid_bits) {
		u_long	u1;

		if ((tcp->tcp_valid_bits & TCP_ISS_VALID)
		&&  seq == tcp->tcp_iss) {
			u_char	* wptr;
			switch (tcp->tcp_state) {
			case TCPS_SYN_SENT:
				flags = TH_SYN;
				break;
			case TCPS_SYN_RCVD:
				flags |= TH_SYN;
				break; 
			default:
				break;
			}

			/* Tack on the mss option */
			wptr = mp1->b_wptr;
			wptr[0] = 2;
			wptr[1] = 4;
			wptr += 2;
			u1 = tcp->tcp_mss;
			U16_TO_BE16(u1, wptr);
			mp1->b_wptr = wptr + 2;

			/* Update the offset to cover the additional word */
			tcph->th_offset_and_rsrvd[0] += (1 << 4);

			/* allocb() of adequate mblk assures space */
			u1 = mp1->b_wptr - mp1->b_rptr;
			/* 
			 * Get IP set to checksum on our behalf
			 * Include the adjustment for a source route if any.
			 */
			u1 += tcp->tcp_sum;
			u1 = (u1 >> 16) + (u1 & 0xFFFF);
			U16_TO_BE16(u1, tcph->th_sum);
			if (tcp->tcp_state < TCPS_ESTABLISHED)
				flags |= TH_SYN;
			if (flags & TH_SYN)
				BUMP_MIB(tcp_mib.tcpOutControl);
		}
		if ((tcp->tcp_valid_bits & TCP_FSS_VALID)
		&&  (seq + data_length) == tcp->tcp_fss) {
			if (!tcp->tcp_fin_acked) {
				flags |= TH_FIN;
				BUMP_MIB(tcp_mib.tcpOutControl);
			}
			if (!tcp->tcp_fin_sent) {
				tcp->tcp_fin_sent = true;
				switch (tcp->tcp_state) {
				case TCPS_SYN_RCVD:
				case TCPS_ESTABLISHED:
					tcp->tcp_state = TCPS_FIN_WAIT_1;
					break;
				case TCPS_CLOSE_WAIT:
					tcp->tcp_state = TCPS_LAST_ACK;
					break;
				}
				if (tcp->tcp_suna == tcp->tcp_snxt)
					TCP_TIMER_RESTART(tcp, tcp->tcp_rto);
				tcp->tcp_snxt = tcp->tcp_fss + 1;
			}
		}
		u1 = tcp->tcp_urg - seq;
		if ((tcp->tcp_valid_bits & TCP_URG_VALID)
		&&  u1 < (u_long)(64 * 1024)) {
			flags |= TH_URG;
			BUMP_MIB(tcp_mib.tcpOutUrg);
			u1 += tcp->tcp_urp_old;
			U32_TO_ABE16(u1, ALIGN16(tcph->th_urp));
		}
	}
	tcph->th_flags[0] = (u_char)flags;
	tcp->tcp_rack = tcp->tcp_rnxt;
	tcp->tcp_rack_cnt = 0;
	rptr = mp1->b_rptr;
	data_length += mp1->b_wptr - rptr;
	U16_TO_ABE16(data_length, ALIGN16(((iph_t *)rptr)->iph_length));

	/* 
	 * Prime pump for IP 
	 * Include the adjustment for a source route if any.
	 */
	data_length -= tcp->tcp_ip_hdr_len;
	data_length += tcp->tcp_sum;
	data_length = (data_length >> 16) + (data_length & 0xFFFF);
	U16_TO_ABE16(data_length, ALIGN16(tcph->th_sum));
#ifdef TCP_PERF
	tcp_xmit_cnt_1++;
	if (mp2 = mp1->b_cont) {
		tcp_xmit_cnt_2++;
		if (mp2 = mp2->b_cont) {
			tcp_xmit_cnt_3++;
			if (mp2 = mp2->b_cont) {
				tcp_xmit_cnt_4++;
			}
		}
	}
#ifdef TCP_PERF_LEN
	tcp_count_len(mp);
#endif
#endif
	return mp1;

}

/* Generate an ACK-only (no data) segment for a TCP endpoint */
staticf mblk_t *
tcp_ack_mp(tcp)
	tcp_t *tcp;
{

	if (tcp->tcp_valid_bits)
		/* 
		 * For the complex case where we have to send some
		 * controls (FIN or SYN), let tcp_xmit_mp do it.
		 * When sending an ACK-only segment (no data)
		 * into a zero window, always set the seq number to
		 * suna, since snxt will be extended past the window.
		 * If we used snxt, the receiver might consider the ACK
		 * unacceptable.
		 */
		return (tcp_xmit_mp(tcp, nilp(mblk_t), 0,
				  (tcp->tcp_swnd == 0) ?
				  tcp->tcp_suna :
				  tcp->tcp_snxt, 0));
	else {
		/* Generate a simple ACK */
		int	data_length;
		u_char	*rptr;
		tcph_t	*tcph;
		mblk_t	*mp1;
		
		/*
		 * Allocate space for TCP + IP headers 
		 * and link-level header
		 */
		mp1 = allocb(tcp->tcp_hdr_len + 
			     tcp_wroff_xtra, BPRI_MED);
		if (!mp1)
			return((mblk_t *) 0);
		
		/* copy in prototype TCP + IP header */
		rptr = mp1->b_rptr + tcp_wroff_xtra;
		mp1->b_rptr = rptr;
		mp1->b_wptr = rptr + tcp->tcp_hdr_len;
		bcopy(tcp->tcp_iphc, (char *)rptr,
		      tcp->tcp_hdr_len);
		
		tcph = (tcph_t *)&rptr[tcp->tcp_ip_hdr_len];

		/*
		 * Set the TCP sequence number.
		 * When sending an ACK-only segment (no data)
		 * into a zero window, always set the seq number to
		 * suna, since snxt will be extended past the window.
		 * If we used snxt, the receiver might consider the ACK
		 * unacceptable.
		 */
		U32_TO_ABE32((tcp->tcp_swnd == 0) ?
			     tcp->tcp_suna : tcp->tcp_snxt,
			     ALIGN32(tcph->th_seq));
		
		/* set the TCP ACK flag */
		tcph->th_flags[0] = (u_char) TH_ACK;
		tcp->tcp_rack = tcp->tcp_rnxt;
		tcp->tcp_rack_cnt = 0;
		
		/* 
		 * set IP total length field equal to
		 * size of TCP + IP headers.
		 */
		U16_TO_ABE16(tcp->tcp_hdr_len,
			     ALIGN16(((iph_t *)rptr)->iph_length));
		
		/* 
		 * Prime pump for checksum calculation in IP.
		 * Include the adjustment for a source
		 * route if any.  Note: no TCP options
		 * will be present if tcp_valid_bits == 0,
		 * so we can use min TCP hdr len.
		 */
		data_length = TCP_MIN_HEADER_LENGTH +
			tcp->tcp_sum;
		data_length = (data_length >> 16) +
			(data_length & 0xFFFF);
		U16_TO_ABE16(data_length,
			     ALIGN16(tcph->th_sum));


		return(mp1);
	}
}


/*
 * Hash list insertion routine for tcp_t structures.  (Always called as
 * writer.)
 * Inserts entries with the ones bound to a specific IP address first
 * followed by those bound to INADDR_ANY.
 */
staticf void
tcp_bind_hash_insert (tcpp, tcp)
reg	tcp_t	** tcpp;
reg	tcp_t	* tcp;
{
reg	tcp_t	* tcpnext;

	ASSERT(TCP_WRITE_HELD());

	tcp_bind_hash_remove(tcp);
	tcpnext = tcpp[0];
	if ( tcpnext ) {
		/*
		 * If the new tcp bound to the INADDR_ANY address
		 * and the first one in the list is not bound to
		 * INADDR_ANY we skip all entries until we find the
		 * first one bound to INADDR_ANY.
		 * This makes sure that applications binding to a
		 * specific address get preference over those binding to
		 * INADDR_ANY.
		 */
		if (tcp->tcp_bound_source == INADDR_ANY &&
		    tcpnext->tcp_bound_source != INADDR_ANY) {
			while ((tcpnext = tcpp[0]) != NULL &&
			       tcpnext->tcp_bound_source != INADDR_ANY)
				tcpp = &(tcpnext->tcp_bind_hash);
			if (tcpnext)
				tcpnext->tcp_ptpbhn = &tcp->tcp_bind_hash;
		} else
			tcpnext->tcp_ptpbhn = &tcp->tcp_bind_hash;
	}
	tcp->tcp_bind_hash = tcpnext;
	tcp->tcp_ptpbhn = tcpp;
	tcpp[0] = tcp;
}

/*
 * Hash list removal routine for tcp_t structures.  (Always called as
 * writer.) 
 */
staticf void
tcp_bind_hash_remove (tcp)
reg	tcp_t	* tcp;
{
reg	tcp_t	* tcpnext;

	ASSERT(TCP_WRITE_HELD());

	if (tcp->tcp_ptpbhn) {
		tcpnext = tcp->tcp_bind_hash;
		if ( tcpnext ) {
			tcpnext->tcp_ptpbhn = tcp->tcp_ptpbhn;
			tcp->tcp_bind_hash = nilp(tcp_t);
		}
		*tcp->tcp_ptpbhn = tcpnext;
		tcp->tcp_ptpbhn = nilp(tcp_t *);
	}
}

/*
 * Hash list insertion routine for tcp_t structures.  (Always called as
 * writer.)
 * Inserts entries with the ones bound to a specific IP address first
 * followed by those bound to INADDR_ANY.
 */
staticf void
tcp_listen_hash_insert (tcpp, tcp)
reg	tcp_t	** tcpp;
reg	tcp_t	* tcp;
{
reg	tcp_t	* tcpnext;

	ASSERT(TCP_WRITE_HELD());

	tcp_listen_hash_remove(tcp);
	tcpnext = tcpp[0];
	if ( tcpnext ) {
		/*
		 * If the new tcp bound to the INADDR_ANY address
		 * and the first one in the list is not bound to
		 * INADDR_ANY we skip all entries until we find the
		 * first one bound to INADDR_ANY.
		 * This makes sure that applications binding to a
		 * specific address get preference over those binding to
		 * INADDR_ANY.
		 */
		if (tcp->tcp_bound_source == INADDR_ANY &&
		    tcpnext->tcp_bound_source != INADDR_ANY) {
			while ((tcpnext = tcpp[0]) != NULL &&
			       tcpnext->tcp_bound_source != INADDR_ANY)
				tcpp = &(tcpnext->tcp_listen_hash);
			if (tcpnext)
				tcpnext->tcp_ptplhn = &tcp->tcp_listen_hash;
		} else
			tcpnext->tcp_ptplhn = &tcp->tcp_listen_hash;
	}
	tcp->tcp_listen_hash = tcpnext;
	tcp->tcp_ptplhn = tcpp;
	tcpp[0] = tcp;
}

/*
 * Hash list removal routine for tcp_t structures.  (Always called as
 * writer.) 
 */
staticf void
tcp_listen_hash_remove (tcp)
reg	tcp_t	* tcp;
{
reg	tcp_t	* tcpnext;

	ASSERT(TCP_WRITE_HELD());

	if (tcp->tcp_ptplhn) {
		tcpnext = tcp->tcp_listen_hash;
		if ( tcpnext ) {
			tcpnext->tcp_ptplhn = tcp->tcp_ptplhn;
			tcp->tcp_listen_hash = nilp(tcp_t);
		}
		*tcp->tcp_ptplhn = tcpnext;
		tcp->tcp_ptplhn = nilp(tcp_t *);
	}
}

/*
 * Hash list insertion routine for tcp_t structures.  (Always called as
 * writer.)
 */
staticf void
tcp_conn_hash_insert (tcpp, tcp)
reg	tcp_t	** tcpp;
reg	tcp_t	* tcp;
{
reg	tcp_t	* tcpnext;

	ASSERT(TCP_WRITE_HELD());

	tcp_conn_hash_remove(tcp);
	tcpnext = tcpp[0];
	if ( tcpnext )
		tcpnext->tcp_ptpchn = &tcp->tcp_conn_hash;
	tcp->tcp_conn_hash = tcpnext;
	tcp->tcp_ptpchn = tcpp;
	tcpp[0] = tcp;
}

/*
 * Hash list removal routine for tcp_t structures.  (Always called as
 * writer.) 
 */
staticf void
tcp_conn_hash_remove (tcp)
reg	tcp_t	* tcp;
{
reg	tcp_t	* tcpnext;

	ASSERT(TCP_WRITE_HELD());

	if (tcp->tcp_ptpchn) {
		tcpnext = tcp->tcp_conn_hash;
		if ( tcpnext ) {
			tcpnext->tcp_ptpchn = tcp->tcp_ptpchn;
			tcp->tcp_conn_hash = nilp(tcp_t);
		}
		*tcp->tcp_ptpchn = tcpnext;
		tcp->tcp_ptpchn = nilp(tcp_t *);
	}
}

/*
 * Hash list lookup routine for tcp_t structures.
 */
staticf	tcp_t *
tcp_queue_hash_lookup(driverq)
	queue_t	* driverq;
{
	tcp_t	* tcp;

	ASSERT(TCP_READ_HELD() || TCP_WRITE_HELD());

	tcp = tcp_queue_fanout[TCP_QUEUE_HASH(driverq)];
	for (; tcp != nilp(tcp_t); tcp = tcp->tcp_queue_hash) {
		if (backq(tcp->tcp_rq) == driverq)
			break;
	}
	return tcp;
}

	
/*
 * Hash list insertion routine for tcp_t structures.  (Always called as
 * writer.)
 */
staticf void
tcp_queue_hash_insert (tcpp, tcp)
reg	tcp_t	** tcpp;
reg	tcp_t	* tcp;
{
reg	tcp_t	* tcpnext;

	ASSERT(TCP_WRITE_HELD());

	tcp_queue_hash_remove(tcp);
	tcpnext = tcpp[0];
	if ( tcpnext )
		tcpnext->tcp_ptpqhn = &tcp->tcp_queue_hash;
	tcp->tcp_queue_hash = tcpnext;
	tcp->tcp_ptpqhn = tcpp;
	tcpp[0] = tcp;
}

/*
 * Hash list removal routine for tcp_t structures.  (Always called as
 * writer.) 
 */
staticf void
tcp_queue_hash_remove (tcp)
reg	tcp_t	* tcp;
{
reg	tcp_t	* tcpnext;

	ASSERT(TCP_WRITE_HELD());

	if (tcp->tcp_ptpqhn) {
		tcpnext = tcp->tcp_queue_hash;
		if ( tcpnext ) {
			tcpnext->tcp_ptpqhn = tcp->tcp_ptpqhn;
			tcp->tcp_queue_hash = nilp(tcp_t);
		}
		*tcp->tcp_ptpqhn = tcpnext;
		tcp->tcp_ptpqhn = nilp(tcp_t *);
	}
}

/*
 * KLUDGE ALERT: the following code needs to disappear in the future, its
 *		 functionality needs to be moved into the appropriate STREAMS
 *		 frame work file. This code makes assumtions based on the
 *		 current implementation of Synchronous STREAMS.
 */

/*
 * Send an M_IOCTL of I_SYNCSTR up the read-side, when it comes back down
 * the write-side wput() will clear the co_norm bit and free the mblk.
 */
staticf int
struio_ioctl (rq, mp)
	queue_t	*rq;
	mblk_t	* mp;
{
	if (!mp)
		mp = allocb(sizeof(struct iocblk), BPRI_HI);
	if (!mp)
		return (0);

	mp->b_datap->db_type = M_IOCTL;
	((struct iocblk *)mp->b_rptr)->ioc_cmd = I_SYNCSTR;
	mp->b_wptr = mp->b_rptr + sizeof(struct iocblk);

	putnext(rq, mp);
	return (1);
}

#include <sys/strsubr.h>

/*
 * Function clears the appropriate bit in sd_wakeq set in strwakeq().
 */
staticf void
strwakeqclr(queue_t *q, int flag)
{
	register stdata_t *stp = STREAM(q);

	mutex_enter(&stp->sd_lock);
	if (flag & QWANTWSYNC)
		stp->sd_wakeq &= ~WSLEEP;
	else if (flag & QWANTR)
		stp->sd_wakeq &= ~RSLEEP;
	else if (flag & QWANTW)
		stp->sd_wakeq &= ~WSLEEP;
	mutex_exit(&stp->sd_lock);
}


/*
 * Type three generator adapted from the random() function in 4.4 BSD:
 */

/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Type 3 -- x**31 + x**3 + 1 */
#define	DEG_3		31
#define	SEP_3		3


static long tcp_randtbl[DEG_3 + 1];

static long *tcp_random_fptr = &tcp_randtbl[SEP_3 + 1];
static long *tcp_random_rptr = &tcp_randtbl[1];

static long *tcp_random_state = &tcp_randtbl[1];
static long *tcp_random_end_ptr = &tcp_randtbl[DEG_3 + 1];

void
tcp_random_init()
{
	register int i;
	hrtime_t hrt;

	/*
	 * Use high-res timer for seed.  Gethrtime() returns a longlong, 
	 * which may contain resolution down to nanoseconds.  Convert
	 * this to a 32-bit value by multiplying the high-order 32-bits by
	 * the low-order 32-bits.
	 */
	hrt = gethrtime();
	TCP_LOCK_WRITE();
	tcp_random_state[0] = ((hrt >> 32) & 0xffffffff) * (hrt & 0xffffffff);

	for (i = 1; i < DEG_3; i++)
		tcp_random_state[i] = 1103515245 * tcp_random_state[i - 1]
			+ 12345;
	tcp_random_fptr = &tcp_random_state[SEP_3];
	tcp_random_rptr = &tcp_random_state[0];
	for (i = 0; i < 10 * DEG_3; i++)
		(void) tcp_random();
	TCP_UNLOCK_WRITE();
}

/*
 * tcp_random: Return a random number in the range [1 - (128K + 1)].
 * This range is selcted to be approximately centered on TCP_ISS / 2,
 * and easy to compute. We get this value by generating a 32-bit random
 * number, selecting out the high-order 17 bits, and then adding one so
 * that we never return zero.
 */

staticf long
tcp_random()
{
	long i;

	ASSERT(TCP_WRITE_HELD());

	*tcp_random_fptr += *tcp_random_rptr;

	/*
	 * The high-order bits are more random than the low-order bits,
	 * so we select out the high-order 17 bits and add one so that
	 * we never return zero.
	 */
	i = ((*tcp_random_fptr >> 15) & 0x1ffff) + 1;
	if (++tcp_random_fptr >= tcp_random_end_ptr) {
		tcp_random_fptr = tcp_random_state;
		++tcp_random_rptr;
	} else if (++tcp_random_rptr >= tcp_random_end_ptr)
		tcp_random_rptr = tcp_random_state;

	return (i);
}
/*
 * XXX This will go away when TPI is extended to send
 * info reqs to sockmod/timod ..... 
 * Given a queue, set the max packet size for the write 
 * side of the queue below stream head.  This value is
 * cached on the stream head.
 * Returns 1 on success, 0 otherwise.
 */
staticf int
setmaxps(queue_t *q, int maxpsz)
{
	struct stdata 	*stp;
	queue_t 	*wq;
	stp = STREAM(q);

	/*
	 * At this point change of a queue parameter is not allowed 
	 * when a mulitplexer is sitting on top.
	 */
	if (stp->sd_flag & STPLEX)
		return(0);

	wq = stp->sd_wrq->q_next;
	ASSERT(wq != NULL);
	strqset(wq, QMAXPSZ, 0, maxpsz);
	return(1);
}
