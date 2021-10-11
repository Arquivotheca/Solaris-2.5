/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)udp.c	1.35	95/02/17 SMI"

#ifndef	MI_HDRS

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strlog.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <sys/tiuser.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>

#include <sys/socket.h>
#include <sys/vtrace.h>
#include <sys/debug.h>
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
#include <strlog.h>
#include <tihdr.h>
#include <timod.h>
#include <tiuser.h>

#include <socket.h>
#include <vtrace.h>
#include <debug.h>
#include <isa_defs.h>
#include <in.h>

#include <common.h>
#include <ip.h>
#include <mi.h>
#include <mib2.h>
#include <nd.h>
#include <optcom.h>
#include <snmpcom.h>

#endif

/* TPI message type used to obtain the current socket options. */
#ifndef	T_CURRENT
#define	T_CURRENT	MI_T_CURRENT
#endif

/*
 * Synchronization notes:
 *
 * At all points in this code
 * where exclusive, writer, access is required, we pass a message to a
 * subroutine by invoking "become_writer" which will arrange to call the
 * routine only after all reader threads have exited the shared resource, and
 * the writer lock has been acquired.  For uniprocessor, single-thread,
 * nonpreemptive environments, become_writer can simply be a macro which
 * invokes the routine immediately.
 */
#undef become_writer
#define	become_writer(q, mp, func) (*func)(q, mp)

/** UDP Protocol header */
typedef	struct udphdr_s {		/**/
	u8	uh_src_port[2];		/* Source port */
	u8	uh_dst_port[2];		/* Destination port */
	u8	uh_length[2];		/* UDP length */
	u8	uh_checksum[2];		/* UDP checksum */
} udph_t;
#define	UDPH_SIZE	8

/* Internal udp control structure, one per open stream */
typedef	struct ud_s {
	uint	udp_state;		/* TPI state */
	u8	udp_pad[2];
	u8	udp_port[2];		/* Port number bound to this stream */
	u8	udp_src[4];		/* Source address of this stream */
	uint	udp_hdr_length;		/* number of bytes used in udp_iphc */
	uint	udp_family;		/* Addr family used in bind, if any */
	uint	udp_ip_snd_options_len;	/* Length of IP options supplied. */
	u8	* udp_ip_snd_options;	/* Pointer to IP options supplied */
	uint	udp_ip_rcv_options_len;	/* Length of IP options supplied. */
	u8	* udp_ip_rcv_options;	/* Pointer to IP options supplied */
	union {
		u_char	udpu1_multicast_ttl;	/* IP_MULTICAST_TTL option */
		u_long	udpu1_pad;
	} udp_u1;
#define	udp_multicast_ttl	udp_u1.udpu1_multicast_ttl
	u32	udp_multicast_if_addr;	/* IP_MULTICAST_IF option */
	udph_t	* udp_udph;
	uint	udp_priv_stream : 1,	/* Stream opened by privileged user */
		udp_wants_header : 1,	/* Place the ip header in options */
		udp_debug : 1,		/* SO_DEBUG "socket" option. */
		udp_dontroute : 1,	/* SO_DONTROUTE "socket" option. */

		udp_broadcast : 1,	/* SO_BROADCAST "socket" option. */
		udp_useloopback : 1,	/* SO_USELOOPBACK "socket" option. */
		udp_reuseaddr : 1,	/* SO_REUSEADDR "socket" option. */
		udp_multicast_loop : 1,	/* IP_MULTICAST_LOOP option */

		udp_imasocket : 1,	/* set if sockmod is upstream */
		udp_socketchecked : 1,	/* check for socket has been done */

		udp_pad_to_bit_31 : 22;
	union {
		char	udpu2_iphc[IP_MAX_HDR_LENGTH + UDPH_SIZE];
		iph_t	udpu2_iph;
		u32	udpu2_ipharr[6];
		double	udpu2_aligner;
	} udp_u2;
#define	udp_iphc	udp_u2.udpu2_iphc
#define	udp_iph		udp_u2.udpu2_iph
#define	udp_ipharr	udp_u2.udpu2_ipharr
	u8	udp_pad2[2];
	u8	udp_type_of_service;
	u8	udp_ttl;
} udp_t;

/** UDP Protocol header aligned */
typedef	struct udpahdr_s {		/**/
	u16	uha_src_port;		/* Source port */
	u16	uha_dst_port;		/* Destination port */
	u16	uha_length;		/* UDP length */
	u16	uha_checksum;		/* UDP checksum */
} udpha_t;

/* Named Dispatch Parameter Management Structure */
typedef struct udpparam_s {
	u_long	udp_param_min;
	u_long	udp_param_max;
	u_long	udp_param_value;
	char	* udp_param_name;
} udpparam_t;

staticf	void	udp_bind(   queue_t * q, MBLKP mp   );
staticf	int	udp_close(   queue_t * q   );
staticf	void	udp_connect(   queue_t * q, MBLKP mp   );
staticf void	udp_err_ack(   queue_t * q, MBLKP mp, int t_error,
			       int sys_error   );
staticf	void	udp_info_req(   queue_t * q, MBLKP mp   );
staticf int	udp_isasocket(   queue_t * rq   );
staticf	int	udp_open(   queue_t * q, dev_t * devp, int flag, int sflag,
			    cred_t * credp   );
staticf	boolean_t	udp_opt_chk(   int level, int name   );
staticf	int	udp_opt_get(   queue_t * q, int level, int name,
			       u_char * ptr   );
staticf	int	udp_opt_set(   queue_t * q, int level, int name,
			       u_char * ptr,
			       int len   );
staticf	void	udp_param_cleanup(   void   );
staticf int	udp_param_get(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf boolean_t	udp_param_register(   udpparam_t * udppa, int cnt   );
staticf int	udp_param_set(   queue_t * q, mblk_t * mp, char * value,
				 caddr_t cp   );
staticf	void	udp_rput(   queue_t * q, MBLKP mp   );
staticf	void	udp_rput_other(   queue_t * q, MBLKP mp   );
staticf	int	udp_snmp_get(   queue_t * q, mblk_t * mpctl   );
staticf	int	udp_snmp_set(   queue_t * q, int level, int name,
			       u_char * ptr, int len   );
staticf	int	udp_status_report(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf void	udp_ud_err(   queue_t * q, MBLKP mp, int err   );
staticf	void	udp_unbind(   queue_t * q, MBLKP mp   );
staticf	void	udp_wput(   queue_t * q, MBLKP mp   );
staticf	void	udp_wput_other(   queue_t * q, MBLKP mp   );
staticf	void	udp_wput_excl(   queue_t * q, MBLKP mp   );

static struct module_info info =  {
	5607, "udp", 1, INFPSZ, 512, 128
};

static struct qinit rinit = {
	(pfi_t)udp_rput, nil(pfi_t), udp_open, udp_close, nil(pfi_t), &info
};

static struct qinit winit = {
	(pfi_t)udp_wput, nil(pfi_t), nil(pfi_t), nil(pfi_t), nil(pfi_t), &info
};

struct streamtab udpinfo = {
	&rinit, &winit
};

	int	udpdevflag = 0;

static	void	* udp_g_head;	/* Head for list of open udp streams. */
static	IDP	udp_g_nd;	/* Points to table of UDP ND variables. */ 
static	u16	udp_g_next_port_to_try;
kmutex_t	udp_g_lock;	/* Protects the above three variables */

/* MIB-2 stuff for SNMP */
static	mib2_udp_t	udp_mib;	/* SNMP fixed size info */


	/* Default structure copied into T_INFO_ACK messages */
static	struct T_info_ack udp_g_t_info_ack = {
	T_INFO_ACK,
	(64 * 1024) - (UDPH_SIZE + 20),	/* TSDU_size.  max ip less headers */
	-2,		/* ETSU_size.  udp does not support expedited data. */
	-2,		/* CDATA_size. udp does not support connect data. */
	-2,		/* DDATA_size. udp does not support disconnect data. */
	sizeof(ipa_t),	/* ADDR_size. */
	64,		/* OPT_size.  udp takes an IP header for options. */
	(64 * 1024) - (UDPH_SIZE + 20),	/* TIDU_size.  max ip less headers */
	T_CLTS,		/* SERV_type.  udp supports connection-less. */
	TS_UNBND	/* CURRENT_state.  This is set from udp_state. */
};

/* largest UDP port number */
#define	UDP_MAX_PORT	65535

/*
 * Table of ND variables supported by udp.  These are loaded into udp_g_nd
 * in udp_open.
 * All of these are alterable, within the min/max values given, at run time.
 */
static	udpparam_t	udp_param_arr[] = {
	/*min	max		value		name */
	{ 0L,	256,		32,		"udp_wroff_extra" },
	{ 1L,	255,		255,		"udp_def_ttl" },
	{ 1024,	(32 * 1024),	1024,		"udp_smallest_nonpriv_port" },
	{ 0,	1,		0,		"udp_trust_optlen" },
	{ 0,	1,		1,		"udp_do_checksum" },
	{ 1024,	UDP_MAX_PORT,	(32 * 1024),	"udp_smallest_anon_port" },
	{ 1024,	UDP_MAX_PORT,	UDP_MAX_PORT,	"udp_largest_anon_port" },
	{ 4096,	65536,		8192,		"udp_xmit_hiwat"},
	{ 0,	65536,		1024,		"udp_xmit_lowat"},
	{ 4096,	65536,		8192,		"udp_recv_hiwat"},
	{ 65536, 1024*1024*1024, 256*1024,	"udp_max_buf"},
};
#define	udp_wroff_extra			udp_param_arr[0].udp_param_value
#define	udp_g_def_ttl			udp_param_arr[1].udp_param_value
#define	udp_smallest_nonpriv_port	udp_param_arr[2].udp_param_value
#define	udp_trust_optlen		udp_param_arr[3].udp_param_value
#define	udp_g_do_checksum		udp_param_arr[4].udp_param_value
#define	udp_smallest_anon_port		udp_param_arr[5].udp_param_value
#define	udp_largest_anon_port		udp_param_arr[6].udp_param_value
#define	udp_xmit_hiwat			udp_param_arr[7].udp_param_value
#define	udp_xmit_lowat			udp_param_arr[8].udp_param_value
#define	udp_recv_hiwat			udp_param_arr[9].udp_param_value
#define	udp_max_buf			udp_param_arr[10].udp_param_value

/*
 * This routine is called to handle each T_BIND_REQ message passed to
 * udp_wput.  It associates a port number and local address with the stream.
 * The T_BIND_REQ is passed downstream to ip with the UDP protocol type
 * (IPPROTO_UDP) placed in the message following the address.   A T_BIND_ACK
 * message is passed upstream when ip acknowledges the request.
 * (Called as writer.)
 */
staticf void
udp_bind (q, mp)
	queue_t	* q;
reg	mblk_t	* mp;
{
reg	ipa_t	* ipa;
	mblk_t	* mp1;
	u16	port;
	u16	requested_port;
reg	struct T_bind_req	* tbr;
	udp_t	* udp;
	int	count;

	udp = (udp_t *)q->q_ptr;
	if ((mp->b_wptr - mp->b_rptr) < sizeof(*tbr)) {
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			  "udp_bind: bad req, len %d", mp->b_wptr - mp->b_rptr);
		udp_err_ack(q, mp, TBADADDR, 0);
		return;
	}
	if ( udp->udp_state != TS_UNBND ) {
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			  "udp_bind: bad state, %d", udp->udp_state);
		udp_err_ack(q, mp, TOUTSTATE, 0);
		return;
	}
	/*
	 * Reallocate the message to make sure we have enough room for an
	 * address and the protocol type.
	 */
	mp1 = mi_reallocb(mp, sizeof(struct T_bind_ack) + sizeof(ipa_t) + 1);
	if (!mp1) {
		udp_err_ack(q, mp, TSYSERR, ENOMEM);
		return;
	}

	mp = mp1;
	tbr = (struct T_bind_req *)ALIGN32(mp->b_rptr);
	switch (tbr->ADDR_length) {
	case 0:			/* Request for a generic port */
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
			udp_err_ack(q, mp, TSYSERR, EINVAL);
			return;
		}
		port = BE16_TO_U16(ipa->ip_port);
		break;
	default:		/* Invalid request */
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			  "udp_bind: bad ADDR_length %d", tbr->ADDR_length);
		udp_err_ack(q, mp, TBADADDR, 0);
		return;
	}

	mutex_enter(&udp_g_lock);
	/* don't let next-port-to-try fall into the privileged range */
	if (udp_g_next_port_to_try < udp_smallest_nonpriv_port)
		udp_g_next_port_to_try = udp_smallest_anon_port;

	requested_port = port;

	/*
	 * If the application pased in zero for the port number, it
	 * doesn't care which port number we bind to.
	 */
	if (port == 0)
		port = udp_g_next_port_to_try;
	/*
	 * If the port is in the well-known privileged range,
	 * make sure the stream was opened by superuser.
	 */
	if (port < udp_smallest_nonpriv_port
	&& !udp->udp_priv_stream) {
		mutex_exit(&udp_g_lock);
		udp_err_ack(q, mp, TACCES, 0);
		return;
	}
	/*
	 * Copy the source address into our udp structure.  This address
	 * may still be zero; if so, ip will fill in the correct address
	 * each time an outbound packet is passed to it.
	 */
	bcopy((char *)ipa->ip_addr, (char *)udp->udp_src,sizeof(ipa->ip_addr));
	bcopy((char *)ipa->ip_addr, (char *)udp->udp_iph.iph_src,
	      sizeof(ipa->ip_addr));

	/*
	 * If udp_reuseaddr is not set, then we have to make sure that
	 * the IP address and port number the application requested
	 * (or we selected for the application) is not being used by
	 * another stream.  If another stream is already using the
	 * requested IP address and port, then we search for any an
	 * unused port to bind to the the stream.
	 * 
	 * If udp_reuseaddr is set, then we use the requested port
	 * whether or not another stream is already bound to it.
	 * XXX - This may not exacly match BSD semantics for the
	 * SO_REUSEADDR options.
	 */
	if (!udp->udp_reuseaddr) {
		u32	src = BE32_TO_U32(udp->udp_src);
		
		count = 0;
		for (;;) {
			reg	udp_t	* udp1;
			u8	a0;
			u8	a1;
			u32	src1;

			/*
			 * Walk through the list of open udp streams looking
			 * for another stream bound to this IP address
			 * and port number.
			 */
			a0 = (u8)(port >> 8);
			a1 = (u8)port;
			udp1 = (udp_t *)ALIGN32(mi_first_ptr(&udp_g_head));
			for (; udp1; udp1 = 
			     (udp_t *)ALIGN32(mi_next_ptr(&udp_g_head, (IDP)udp1))) {
				if (udp1->udp_port[1] == a1
				&&  udp1->udp_port[0] == a0) {
					if (src) {
						/* 
						 * If this stream is bound to
						 * distinct different IP
						 * addresses, keep going.
						 */
						src1 = 
						  BE32_TO_U32(udp1->udp_src);
						if (src1  &&  src1 != src)
							continue;
					}
					break;
				}
			}

			if (!udp1) {
				/* 
				 * No other stream has this IP address
				 * and port number. We can use it.
				 */
				break;
			}

			/* 
			 * Our search for an unused port number is bouded
			 * on the bottom by udp_smallest_anon_port and
			 * on the top by udp_largest_anon_port.
			 */
			if ((count == 0) && (port != udp_g_next_port_to_try))
				port = udp_g_next_port_to_try;
			else
				port++;

			if ((port > udp_largest_anon_port) ||
			    (port < udp_smallest_nonpriv_port))
		        	port = udp_smallest_anon_port;
			
			if (++count >= (udp_largest_anon_port -
					udp_smallest_anon_port + 1)) {
				/*
				 * We've tried every possible port number and
				 * there are none available, so send an error
				 * to the user.
				 */
				mutex_exit(&udp_g_lock);
				udp_err_ack(q, mp, TNOADDR, 0);
				return;
			}
			
		}
	}

	/*
	 * Now reset the the next anonymous port if the application requested
	 * an anonymous port, or we just handed out the next anonymous port.
	 */
	if ((requested_port == 0) ||
	    (port == udp_g_next_port_to_try)) {
		udp_g_next_port_to_try = port + 1;
		if (udp_g_next_port_to_try >=
		    udp_largest_anon_port)
			udp_g_next_port_to_try = udp_smallest_anon_port;
	}

	/* Initialize the T_BIND_REQ for ip. */
	U16_TO_BE16(port, ipa->ip_port);
	U16_TO_BE16(port, udp->udp_port);
	udp->udp_family = ipa->ip_family;
	udp->udp_state = TS_IDLE;

	mutex_exit(&udp_g_lock);
	/* Pass the protocol number in the message following the address. */
	*mp->b_wptr++ = IPPROTO_UDP;
	if (udp->udp_src) {
		/*
		 * Append a request for an IRE if udp_src not 0 (INADDR_ANY)
		 */
		mp->b_cont = allocb(sizeof (ire_t), BPRI_HI);
		if (!mp->b_cont) {
			udp_err_ack(q, mp, TSYSERR, ENOMEM);
			return;
		}
		mp->b_cont->b_datap->db_type = IRE_DB_REQ_TYPE;
	}
	putnext(q, mp);
}

/*
 * This routine handles each T_CONN_REQ message passed to udp.  It
 * associates a default destination address with the stream.
 * A default IP header is created and layed into udp_iphc.
 * This header is prepended to subsequent M_DATA messages.
 */
staticf void
udp_connect (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	ipa_t	* ipa;
	iph_t	* iph;
	struct T_conn_req	* tcr;
	udp_t	* udp;
	udph_t	* udph;

	udp = (udp_t *)q->q_ptr;
	tcr = (struct T_conn_req *)ALIGN32(mp->b_rptr);

	/* Make sure the request contains an IP address. */
	if (tcr->DEST_length != sizeof(ipa_t)
	|| (mp->b_wptr-mp->b_rptr < sizeof(struct T_conn_req)+sizeof(ipa_t))) {
		udp_err_ack(q, mp, TBADADDR, 0);
		return;
	}

	if (tcr->OPT_length > 0) {
		/*
		 * Options contain an IP header with any number of IP options 
		 * included.  udp does not verify that the header is valid.
		 */
		int	size_needed;

		size_needed = sizeof(struct T_conn_req) + sizeof(ipa_t);
		size_needed += IP_SIMPLE_HDR_LENGTH;
		if (tcr->OPT_length < IP_SIMPLE_HDR_LENGTH
		||  tcr->OPT_length > IP_MAX_HDR_LENGTH
		|| (mp->b_wptr - mp->b_rptr < size_needed)) {
			udp_err_ack(q, mp, TBADOPT, 0);
			return;
		}
		/* Create a message block containing the IP header. */
		udp->udp_hdr_length = tcr->OPT_length + UDPH_SIZE;
		bcopy((char *)&mp->b_rptr[tcr->OPT_offset],
			udp->udp_iphc, tcr->OPT_length);
		udp->udp_udph = (udph_t *)&udp->udp_iphc[tcr->OPT_length];
	} else {
		/*
		 * Since the user did not pass in an IP header, we create
		 * a default one with no IP options.
		 */
		udp->udp_hdr_length = IP_SIMPLE_HDR_LENGTH + UDPH_SIZE;
		udp->udp_udph = (udph_t *)&udp->udp_iphc[IP_SIMPLE_HDR_LENGTH];
	}

	/* Now, finish initializing the IP and UDP headers. */
	iph = &udp->udp_iph;
	iph->iph_protocol = IPPROTO_UDP;
	ipa = (ipa_t *)ALIGN32(&mp->b_rptr[tcr->DEST_offset]);
	/*
	 * Copy the source address already bound to the stream.
	 * This may still be zero in which case ip will fill it in.
	 */
	iph->iph_src[0] = udp->udp_src[0];
	iph->iph_src[1] = udp->udp_src[1];
	iph->iph_src[2] = udp->udp_src[2];
	iph->iph_src[3] = udp->udp_src[3];
	/* Copy the destination address from the T_CONN_REQ message. */
	iph->iph_dst[0] = ipa->ip_addr[0];
	iph->iph_dst[1] = ipa->ip_addr[1];
	iph->iph_dst[2] = ipa->ip_addr[2];
	iph->iph_dst[3] = ipa->ip_addr[3];
	iph->iph_ttl = udp->udp_ttl;
	iph->iph_type_of_service = udp->udp_type_of_service;

	udph = udp->udp_udph;
	udph->uh_src_port[0] = udp->udp_port[0];
	udph->uh_src_port[1] = udp->udp_port[1];
	udph->uh_dst_port[0] = ipa->ip_port[0];
	udph->uh_dst_port[1] = ipa->ip_port[1];
	udph->uh_checksum[0] = 0;
	udph->uh_checksum[1] = 0;

	/* Acknowledge the request. */
	if ((mp = mi_tpi_ok_ack_alloc(mp)) != NULL)
		qreply(q, mp);
	/* We also have to send a connection confirmation to keep TLI happy */
	if ((mp = mi_tpi_conn_con(nil(MBLKP), (char *)&iph->iph_dst[0],
				 sizeof(iph->iph_dst), nilp(char), 0)) != NULL)
		qreply(q, mp);
}

/* This is the close routine for udp.  It frees the per-stream data. */
staticf int
udp_close (q)
	queue_t	* q;
{
	int	i1;
	udp_t	* udp = (udp_t *)q->q_ptr;

	TRACE_1(TR_FAC_UDP, TR_UDP_CLOSE,
		"udp_close: q %X", q);

	qprocsoff(q);

	/* If there are any options associated with the stream, free them. */
	if ( udp->udp_ip_snd_options )
		mi_free((char *)udp->udp_ip_snd_options);
	if ( udp->udp_ip_rcv_options )
		mi_free((char *)udp->udp_ip_rcv_options);

	mutex_enter(&udp_g_lock);
	/* Free the udp structure and release the minor device number. */
	i1 = mi_close_comm(&udp_g_head, q);

	/* Free the ND table if this was the last udp stream open. */
	udp_param_cleanup();
	mutex_exit(&udp_g_lock);

	return i1;
}

/* This routine creates a T_ERROR_ACK message and passes it upstream. */
staticf void
udp_err_ack (q, mp, t_error, sys_error)
	queue_t	* q;
	mblk_t	* mp;
	int	t_error;
	int	sys_error;
{
	if ((mp = mi_tpi_err_ack_alloc(mp, t_error, sys_error)) != NULL)
		qreply(q, mp);
}

/*
 * udp_icmp_error is called by udp_rput to process ICMP
 * messages passed up by IP.
 * Generates the appropriate T_UDERROR_IND.
 */
staticf void
udp_icmp_error (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	icmph_t * icmph;
	ipha_t	* ipha;
	int	iph_hdr_length;
	udph_t	* udph;
	ipa_t	ipaddr;
	mblk_t	* mp1;
	int	error = 0;

	ipha = (ipha_t *)mp->b_rptr;
	iph_hdr_length = IPH_HDR_LENGTH(ipha);
	icmph = (icmph_t *)ALIGN32(&mp->b_rptr[iph_hdr_length]);
	ipha = (ipha_t *)&icmph[1];
	iph_hdr_length = IPH_HDR_LENGTH(ipha);
	udph = (udph_t *)((char *)ipha + iph_hdr_length);

	switch (icmph->icmph_type ) {
	case ICMP_DEST_UNREACHABLE:
		switch ( icmph->icmph_code ) {
		case ICMP_FRAGMENTATION_NEEDED:
			/*
			 * XXX do something with MTU in UDP?
			 */
			break;
		case ICMP_PORT_UNREACHABLE:
		case ICMP_PROTOCOL_UNREACHABLE:
			error = ECONNREFUSED;
			break;
		default:
			break;
		}
		break;
	}
	if (error == 0) {
		freemsg(mp);
		return;
	}
	/*
	 * Can not deliver T_UDERROR_IND except for sockets.
	 * TODO: Need a mechanism (option?) so that TLI applications can
	 * enable delivery of T_UDERROR_IND messages.
	 */
	if (!udp_isasocket(q)) {
		freemsg(mp);
		return;
	}

	bzero((char *)&ipaddr, sizeof (ipaddr));

	ipaddr.ip_family = AF_INET;
	bcopy((char *)&ipha->ipha_dst,
	      (char *)ipaddr.ip_addr,
	      sizeof(ipaddr.ip_addr));
	bcopy((char *)udph->uh_dst_port,
	      (char *)ipaddr.ip_port,
	      sizeof(ipaddr.ip_port));
	mp1 = mi_tpi_uderror_ind((char *)&ipaddr, sizeof(ipaddr), NULL, 0,
				 error);
	if (mp1)
		putnext(q, mp1);
	freemsg(mp);
}

/*
 * This routine responds to T_INFO_REQ messages.  It is called by udp_wput.
 * Most of the T_INFO_ACK information is copied from udp_g_t_info_ack.
 * The current state of the stream is copied from udp_state.
 */
staticf void
udp_info_req (q, mp)
	queue_t	* q;
reg	mblk_t	* mp;
{
	udp_t	* udp = (udp_t *)q->q_ptr;

	/* Create a T_INFO_ACK message. */
	mp = mi_tpi_ack_alloc(mp, sizeof(udp_g_t_info_ack), T_INFO_ACK);
	if (!mp)
		return;
	bcopy((char *)&udp_g_t_info_ack, (char *)mp->b_rptr,
		sizeof(udp_g_t_info_ack));
	((struct T_info_ack *)ALIGN32(mp->b_rptr))->CURRENT_state = 
		udp->udp_state;
	qreply(q, mp);
}

/*
 * Check if this is a socket i.e. if sockmod is pushed on top.
 */
static int
udp_isasocket(rq)
	queue_t *rq;
{
	udp_t *udp = (udp_t *)rq->q_ptr;

	if (!udp->udp_socketchecked) {
		struct qinit *qi = rq->q_next->q_qinfo;
		struct module_info *mi = qi->qi_minfo;

		if (mi && mi->mi_idname &&
		    (strcmp(mi->mi_idname, "sockmod") == 0)) {
			/* its a socket */
			udp->udp_imasocket = 1;
			mi_strlog(rq, 1, SL_TRACE,
				  "udp_isasocket: udp 0x%x is a socket",
				  udp);
		} else {
			udp->udp_imasocket = 0;
			mi_strlog(rq, 1, SL_TRACE,
				  "udp_isasocket: udp 0x%x is not a socket",
				  udp);
		}
		udp->udp_socketchecked = 1;
	}
	return(udp->udp_imasocket);
}

/*
 * This is the open routine for udp.  It allocates a udp_t structure for
 * the stream and, on the first open of the module, creates an ND table.
 */
staticf int
udp_open (q, devp, flag, sflag, credp)
	queue_t	* q;
	dev_t	* devp;
	int	flag;
	int	sflag;
	cred_t	* credp;
{
	int	err;
	boolean_t	privy = drv_priv(credp) == 0;
reg	udp_t	* udp;

	TRACE_1(TR_FAC_UDP, TR_UDP_OPEN,
		"udp_open: q %X", q);

	/*
	 * Defer the qprocson until everything is initialized since
	 * we are D_MTPERQ and after qprocson the rput routine can
	 * run.
	 */

	/* If the stream is already open, return immediately.*/
	if ((udp = (udp_t *)q->q_ptr) != 0) {
		if (udp->udp_priv_stream  &&  !privy)
			return EPERM;
		return 0;
	}

	/* If this is not a push of udp as a module, fail. */
	if (sflag != MODOPEN)
		return EINVAL;

	mutex_enter(&udp_g_lock);
	/* If this is the first open of udp, create the ND table. */
	if (!udp_g_nd
	&&  !udp_param_register(udp_param_arr, A_CNT(udp_param_arr))) {
		mutex_exit(&udp_g_lock);
		return ENOMEM;
	}
	/*
	 * Create a udp_t structure for this stream and link into the
	 * list of open streams.
	 */
	err = mi_open_comm(&udp_g_head, sizeof(udp_t), q, devp,
			   flag, sflag, credp);
	if (err) {
		/*
		 * If mi_open_comm failed and this is the first stream,
		 * release the ND table.
		 */
		udp_param_cleanup();
		mutex_exit(&udp_g_lock);
		return err;
	}
	mutex_exit(&udp_g_lock);

	/* Set the initial state of the stream and the privilege status. */
	udp = (udp_t *)q->q_ptr;
	udp->udp_state = TS_UNBND;
	udp->udp_hdr_length = IP_SIMPLE_HDR_LENGTH + UDPH_SIZE;

	/*
	 * The receive hiwat is only looked at on the stream head queue.
	 * Store in q_hiwat in order to return on SO_RCVBUF getsockopts.
	 */
	q->q_hiwat = udp_recv_hiwat;

	udp->udp_multicast_ttl = IP_DEFAULT_MULTICAST_TTL;
	udp->udp_multicast_loop = IP_DEFAULT_MULTICAST_LOOP;
	udp->udp_ttl = udp_g_def_ttl;
	udp->udp_type_of_service= 0;	/* XXX should have a global default */
	if (privy)
		udp->udp_priv_stream = 1;

	qprocson(q);

	/*
	 * The transmit hiwat/lowat is only looked at on IP's queue.
	 * Store in q_hiwat/q_lowat in order to return on SO_SNDBUF/SO_SNDLOWAT
	 * getsockopts.
	 */
	WR(q)->q_hiwat = udp_xmit_hiwat;
	WR(q)->q_next->q_hiwat = WR(q)->q_hiwat;
	WR(q)->q_lowat = udp_xmit_lowat;
	WR(q)->q_next->q_lowat = WR(q)->q_lowat;

	mi_set_sth_wroff(q, udp->udp_hdr_length + udp->udp_ip_snd_options_len +
			 udp_wroff_extra);
	mi_set_sth_hiwat(q, q->q_hiwat);
	return 0;
}

/*
 * This routine verifies that socket options are valid.  It returns
 * true if the option is supported by udp and false otherwise.
 */
staticf	boolean_t
udp_opt_chk (level, name)
	int	level;
	int	name;
{
	switch ( level ) {
	case SOL_SOCKET:
		switch ( name ) {
		case SO_DEBUG:
		case SO_DONTROUTE:
		case SO_USELOOPBACK:
		case SO_BROADCAST:
		case SO_REUSEADDR:
		case SO_TYPE:
		case SO_SNDBUF:
		case SO_RCVBUF:
		case SO_SNDLOWAT:
		case SO_RCVLOWAT:
			return true;
		case SO_LINGER:
		case SO_KEEPALIVE:
		case SO_OOBINLINE:
		default:
			/* These options are not meaningful for udp. */
			return false;
		}
		/*NOTREACHED*/
	case IPPROTO_IP:
		switch ( name ) {
		case IP_OPTIONS:
		case IP_TOS:
		case IP_TTL:
		case IP_MULTICAST_IF:
		case IP_MULTICAST_TTL:
		case IP_MULTICAST_LOOP:
		case IP_ADD_MEMBERSHIP:
		case IP_DROP_MEMBERSHIP:
			return true;
		default:
			return false;
		}
		/*NOTREACHED*/
	case IPPROTO_TCP:
	default:
		return false;
	}
	/*NOTREACHED*/
}

/*
 * This routine retrieves the current status of socket options.
 * It returns the size of the option retrieved.
 */
staticf	int
udp_opt_get (q, level, name, ptr)
	queue_t	* q;
	int	level;
	int	name;
	u_char	* ptr;
{
	int	* i1 = (int *)ALIGN32(ptr);
	udp_t	* udp = (udp_t *)q->q_ptr;
	
	switch ( level ) {
	case SOL_SOCKET:
		switch ( name ) {
		case SO_DEBUG:
			*i1 = udp->udp_debug;
			break;
		case SO_REUSEADDR:
			*i1 = udp->udp_reuseaddr;
			break;
		case SO_TYPE:
			*i1 = SOCK_DGRAM;
			break;

		/*
		 * The following three items are available here,
		 * but are only meaningful to IP.
		 */
		case SO_DONTROUTE:
			*i1 = udp->udp_dontroute;
			break;
		case SO_USELOOPBACK:
			*i1 = udp->udp_useloopback;
			break;
		case SO_BROADCAST:
			*i1 = udp->udp_broadcast;
			break;

		/*
		 * The following four items can be manipulated,
		 * but changing them should do nothing.
		 */
		case SO_SNDBUF:
			*i1 = q->q_hiwat;
			break;
		case SO_RCVBUF:
			*i1 = RD(q)->q_hiwat;
			break;
		case SO_SNDLOWAT:
			*i1 = q->q_lowat;
			break;
		case SO_RCVLOWAT:
			*i1 = RD(q)->q_lowat;
			break;
		}
		break;
	case IPPROTO_IP:
		switch ( name ) {
		case IP_OPTIONS:
			if ( udp->udp_ip_rcv_options_len )
				bcopy((char *)udp->udp_ip_rcv_options,
				      (char *)ptr, 
				      udp->udp_ip_rcv_options_len);
			return udp->udp_ip_rcv_options_len;
		case IP_TOS:
			*i1 = (int) udp->udp_type_of_service;
			break;
		case IP_TTL:
			*i1 = (int) udp->udp_ttl;
			break;
		case IP_MULTICAST_IF:
			/* 0 address if not set */
			bcopy((char *)&udp->udp_multicast_if_addr, (char *)ptr,
			      sizeof(udp->udp_multicast_if_addr));
			return sizeof(udp->udp_multicast_if_addr);
		case IP_MULTICAST_TTL:
			bcopy((char *)&udp->udp_multicast_ttl, (char *)ptr,
			      sizeof(udp->udp_multicast_ttl));
			return sizeof(udp->udp_multicast_ttl);
		case IP_MULTICAST_LOOP:
			*ptr = udp->udp_multicast_loop;
			return sizeof(u8);
		}
		break;
	}
	return sizeof(long);
}

/* This routine sets socket options. */
staticf	int
udp_opt_set (q, level, name, ptr, len)
	queue_t	* q;
	int	level;
	int	name;
	u_char	* ptr;
	int	len;
{
	int	* i1 = (int *)ALIGN32(ptr);
	udp_t	* udp = (udp_t *)q->q_ptr;

	switch ( level ) {
	case SOL_SOCKET:	
		switch ( name ) {
		case SO_REUSEADDR:
			udp->udp_reuseaddr = *i1;
			break;
		case SO_DEBUG:
			udp->udp_debug = *i1;
			break;
		/*
		 * The following three items are available here,
		 * but are only meaningful to IP.
		 */
		case SO_DONTROUTE:
			udp->udp_dontroute = *i1;
			break;
		case SO_USELOOPBACK:
			udp->udp_useloopback = *i1;
			break;
		case SO_BROADCAST:
			udp->udp_broadcast = *i1;
			break;
		/*
		 * The following four items can be manipulated,
		 * but changing them should do nothing.
		 */
		case SO_SNDBUF:
			if (*i1 > udp_max_buf)
				return ENOBUFS;
			q->q_hiwat = *i1;
			q->q_next->q_hiwat = *i1;
			break;
		case SO_RCVBUF:
			if (*i1 > udp_max_buf)
				return ENOBUFS;
			RD(q)->q_hiwat = *i1;
			mi_set_sth_hiwat(RD(q), *i1);
			break;
		case SO_SNDLOWAT:
			q->q_lowat = *i1;
			q->q_next->q_lowat = *i1;
			break;
		case SO_RCVLOWAT:
			RD(q)->q_lowat = *i1;
			break;
		}
		break;
	case IPPROTO_IP:
		switch ( name ) {
		case IP_OPTIONS:
			/* Save options for use by IP. */
			if ( udp->udp_ip_snd_options ) {
				mi_free((char *)udp->udp_ip_snd_options);
				udp->udp_ip_snd_options_len = 0;
			}
			if ( len ) {
				udp->udp_ip_snd_options =
					(u_char *)mi_alloc(len, BPRI_HI);
				if ( udp->udp_ip_snd_options ) {
					bcopy((char *)ptr,
						(char *)udp->udp_ip_snd_options,
						len);
					udp->udp_ip_snd_options_len = len;
				}
			}
			mi_set_sth_wroff(RD(q), udp->udp_hdr_length + 
					 udp->udp_ip_snd_options_len +
					 udp_wroff_extra);
			break;
		case IP_TTL:
			/* save ttl in udp state and connected ip header */
			udp->udp_ttl = (u8) *i1;
			udp->udp_iph.iph_ttl = (u8) *i1;
			break;
		case IP_TOS:
			/* save tos in udp state and connected ip header */
			udp->udp_type_of_service = (u8) *i1;
			udp->udp_iph.iph_type_of_service = (u8) *i1;
			break;
		case IP_MULTICAST_IF:
			/* TODO should check OPTMGMT reply and undo this if
			 * there is an error.
			 */
			udp->udp_multicast_if_addr = *i1;
			break;
		case IP_MULTICAST_TTL:
			udp->udp_multicast_ttl = *ptr;
			break;
		case IP_MULTICAST_LOOP:
			udp->udp_multicast_loop = *ptr;
			break;
		}
		break;
	}
	return 0;
}

/*
 * This routine frees the ND table if all streams have been closed.
 * It is called by udp_close and udp_open.
 */
staticf void
udp_param_cleanup ()
{
	ASSERT(MUTEX_HELD(&udp_g_lock));
	if (!udp_g_head)
		nd_free(&udp_g_nd);
}

/*
 * This routine retrieves the value of an ND variable in a udpparam_t
 * structure.  It is called through nd_getset when a user reads the
 * variable.
 */
/* ARGSUSED */
staticf int
udp_param_get (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	udpparam_t	* udppa = (udpparam_t *)ALIGN32(cp);

	mi_mpprintf(mp, "%ld", udppa->udp_param_value);
	return 0;
}

/*
 * Walk through the param array specified registering each element with the
 * named dispatch (ND) handler.
 */
staticf boolean_t
udp_param_register (udppa, cnt)
reg	udpparam_t	* udppa;
	int	cnt;
{
	ASSERT(MUTEX_HELD(&udp_g_lock));
	for ( ; cnt-- > 0; udppa++) {
		if (udppa->udp_param_name  &&  udppa->udp_param_name[0]) {
			if (!nd_load(&udp_g_nd, udppa->udp_param_name,
				     udp_param_get, udp_param_set,
				     (caddr_t)udppa)) {
				nd_free(&udp_g_nd);
				return false;
			}
		}
	}
	if ( !nd_load(&udp_g_nd, "udp_status", udp_status_report, nil(pfi_t),
		nil(caddr_t)) ) {
		nd_free(&udp_g_nd);
		return false;
	}
	return true;
}

/* This routine sets an ND variable in a udpparam_t structure. */
/* ARGSUSED */
staticf int
udp_param_set (q, mp, value, cp)
	queue_t	* q;
	mblk_t	* mp;
	char	* value;
	caddr_t	cp;
{
	char	* end;
	long	new_value;
	udpparam_t	* udppa = (udpparam_t *)ALIGN32(cp);

	ASSERT(MUTEX_HELD(&udp_g_lock));
	/* Convert the value from a string into a long integer. */
	new_value = mi_strtol(value, &end, 10);
	/*
	 * Fail the request if the new value does not lie within the
	 * required bounds.
	 */	
	if (end == value
	||  new_value < udppa->udp_param_min
	||  new_value > udppa->udp_param_max)
		return EINVAL;

	/* Set the new value */
	udppa->udp_param_value = new_value;
	return 0;
}

staticf void
udp_rput(q, mp_orig)
	queue_t	* q;
	mblk_t	* mp_orig;
{
reg	struct T_unitdata_ind	* tudi;
reg	mblk_t	* mp = mp_orig;
reg	u_char	* rptr;
reg	int	hdr_length;
	udp_t	* udp;

	TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_START,
		"udp_rput_start: q %X db_type 0%o",
		q, mp->b_datap->db_type);

	rptr = mp->b_rptr;
	switch (mp->b_datap->db_type) {
	case M_DATA:
		/*
		 * M_DATA messages contain IP datagrams.  They are handled
		 * after this switch.
		 */
		hdr_length = ((rptr[0] & 0xF) << 2) + UDPH_SIZE;
		udp = (udp_t *)q->q_ptr;
		if ((hdr_length > IP_SIMPLE_HDR_LENGTH + UDPH_SIZE) ||
		    (udp->udp_ip_rcv_options_len)) {
			become_exclusive(q, mp_orig, udp_rput_other);
			TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
				"udp_rput_end: q %X (%S)", q, "end");
			return;
		}
		break;
	case M_PROTO:
	case M_PCPROTO:
		/* M_PROTO messages contain some type of TPI message. */
		if ((mp->b_wptr - rptr) < sizeof (long)) {
			freemsg(mp);
			TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
				"udp_rput_end: q %X (%S)", q, "protoshort");
			return;
		}
		become_exclusive(q, mp_orig, udp_rput_other);
		TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
			"udp_rput_end: q %X (%S)", q, "proto");
		return;
	case M_FLUSH:
		if (*mp->b_rptr & FLUSHR)
			flushq(q, FLUSHDATA);
		putnext(q, mp);
		TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
			"udp_rput_end: q %X (%S)", q, "flush");
		return;
	case M_CTL:
		/*
		 * ICMP messages.
		 */
		udp_icmp_error(q, mp);
		TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
			"udp_rput_end: q %X (%S)", q, "m_ctl");
		return;
	default:
		putnext(q, mp);
		TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
			"udp_rput_end: q %X (%S)", q, "default");
		return;
	}
	/*
	 * This is the inbound data path.
	 * First, we make sure the data contains both IP and UDP headers.
	 */
	if ((mp->b_wptr - rptr) < hdr_length) {
		if (!pullupmsg(mp, hdr_length)) {
			freemsg(mp_orig);
			BUMP_MIB(udp_mib.udpInErrors);
			TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
				"udp_rput_end: q %X (%S)", q, "hdrshort");
			return;
		}
		rptr = mp->b_rptr;
	}
	/* Walk past the headers. */
	mp->b_rptr = rptr + hdr_length;

	/*
	 * If the user does not want to receive the headers in options,
	 * then we only need to pass the address up.
	 */
	if (!udp->udp_wants_header) {
		hdr_length = sizeof (ipa_t) + sizeof (struct T_unitdata_ind);
	} else {
		/*
		 * Calculate the size needed for a T_UNITDATA_IND block with
		 * the address and the headers.
		 */
		hdr_length += (sizeof(ipa_t) + sizeof (struct T_unitdata_ind)
				- UDPH_SIZE);
	}

	/* Allocate a message block for the T_UNITDATA_IND structure. */
	mp = allocb(hdr_length, BPRI_MED);
	if (!mp) {
		freemsg(mp_orig);
		TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
			"udp_rput_end: q %X (%S)", q, "allocbfail");
		return;
	}
	mp->b_cont = mp_orig;
	mp->b_datap->db_type = M_PROTO;
	tudi = (struct T_unitdata_ind *)ALIGN32(mp->b_rptr);
	mp->b_wptr = (u_char *)tudi + hdr_length;
	tudi->PRIM_type = T_UNITDATA_IND;
	tudi->SRC_length = sizeof (ipa_t);
	tudi->SRC_offset = sizeof (struct T_unitdata_ind);
	tudi->OPT_offset = sizeof (struct T_unitdata_ind) + sizeof (ipa_t);
	hdr_length -= (sizeof (struct T_unitdata_ind) + sizeof (ipa_t));
	tudi->OPT_length = hdr_length;
#define	ipa	((ipa_t *)&tudi[1])
	*(u32 *)ALIGN32(&ipa->ip_addr[0]) = (((u32 *)ALIGN32(rptr))[3]);
	*(u16 *)ALIGN16(ipa->ip_port) =		/* Source port */
		((u16 *)ALIGN16(mp->b_cont->b_rptr))[-UDPH_SIZE/sizeof (u16)];
	ipa->ip_family = ((udp_t *)q->q_ptr)->udp_family;
	*(u32 *)ALIGN32(&ipa->ip_pad[0]) = 0;
	*(u32 *)ALIGN32(&ipa->ip_pad[4]) = 0;

	/* Copy in the full IP header, if udp_wants_headers is true */
	if (hdr_length != 0)
		bcopy((char *)rptr, (char *)&ipa[1], hdr_length);
#undef	ipa
	BUMP_MIB(udp_mib.udpInDatagrams);
	TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
		"udp_rput_end: q %X (%S)", q, "end");
	putnext(q, mp);
}

staticf void
udp_rput_other(q, mp_orig)
	queue_t	* q;
	mblk_t	* mp_orig;
{
reg	struct T_unitdata_ind	* tudi;
reg	mblk_t	* mp = mp_orig;
reg	u_char	* rptr;
reg	int	hdr_length;
	struct T_error_ack	* tea;
	udp_t	* udp;
	mblk_t *mp1;
	ire_t *ire;

	TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_START,
		"udp_rput_other: q %X db_type 0%o",
		q, mp->b_datap->db_type);

	rptr = mp->b_rptr;

	switch (mp->b_datap->db_type) {
	case M_DATA:
                /*
                 * M_DATA messages contain IP datagrams.  They are handled
                 * after this switch.
		 */
		break;
	case M_PROTO:
	case M_PCPROTO:
		/* M_PROTO messages contain some type of TPI message. */
		if ((mp->b_wptr - rptr) < sizeof (long)) {
			freemsg(mp);
			TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
			    "udp_rput_other_end: q %X (%S)", q, "protoshort");
			return;
		}
		tea = (struct T_error_ack *)ALIGN32(rptr);
		switch (tea->PRIM_type) {
		case T_ERROR_ACK:
			switch (tea->ERROR_prim) {
			case T_BIND_REQ:
				/*
				 * If our T_BIND_REQ fails, clear out the
				 * associated port and source address before
				 * passing the message upstream.
				 */
				udp = (udp_t *)q->q_ptr;
				mutex_enter(&udp_g_lock);
				udp->udp_port[0] = 0;
				udp->udp_port[1] = 0;
				U32_TO_BE32(0, udp->udp_src);
				U32_TO_BE32(0, udp->udp_iph.iph_src);
				udp->udp_state = TS_UNBND;
				mutex_exit(&udp_g_lock);
				break;
			default:
				break;
			}
			break;
		case T_BIND_ACK:
			udp = (udp_t *)q->q_ptr;
			mi_set_sth_maxblk(q, ip_max_mtu - udp->udp_hdr_length);
			/*
			 * If src_addr is not 0 (INADDR_ANY) already, we
			 * set it to 0 if broadcast address was bound.
			 * This ensures no datagrams with broadcast address
			 * as source address are emitted (which would violate
			 * RFC1122 - Hosts requirements)
			 */
			if (udp->udp_src && mp->b_cont) {
				mp1 = mp->b_cont;
				mp->b_cont = NULL;
				if (mp1->b_datap->db_type == IRE_DB_TYPE) {
					ire = (ire_t *) mp1->b_rptr;
					if (ire->ire_type == IRE_BROADCAST)
						U32_TO_BE32(0, udp->udp_src);
				}
				freemsg(mp1);
			}
			/* FALLTHRU */
		case T_OPTMGMT_ACK:
		case T_OK_ACK:
			break;
		default:
			freemsg(mp);
			return;
		}
		putnext(q, mp);
		return;
	}

	/*
	 * This is the inbound data path.
	 * First, we make sure the data contains both IP and UDP headers.
	 */
	hdr_length = ((rptr[0] & 0xF) << 2) + UDPH_SIZE;
	udp = (udp_t *)q->q_ptr;
	if ((mp->b_wptr - rptr) < hdr_length) {
		if (!pullupmsg(mp, hdr_length)) {
			freemsg(mp_orig);
			BUMP_MIB(udp_mib.udpInErrors);
			TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
				"udp_rput_other_end: q %X (%S)", q, "hdrshort");
			return;
		}
		rptr = mp->b_rptr;
	}
	/* Walk past the headers. */
	mp->b_rptr = rptr + hdr_length;

	/* Save the options if any */
	if (hdr_length > IP_SIMPLE_HDR_LENGTH + UDPH_SIZE) {
		int opt_len = hdr_length - (IP_SIMPLE_HDR_LENGTH + UDPH_SIZE);

		if ( opt_len > udp->udp_ip_rcv_options_len) {
			if ( udp->udp_ip_rcv_options_len ) 
				mi_free((char *)udp->udp_ip_rcv_options);
			udp->udp_ip_rcv_options_len = 0;
			udp->udp_ip_rcv_options =
				(u_char *)mi_alloc(opt_len, BPRI_HI);
			if (udp->udp_ip_rcv_options)
				udp->udp_ip_rcv_options_len = opt_len;
		}
		if ( udp->udp_ip_rcv_options_len ) {
			bcopy((char *)rptr + IP_SIMPLE_HDR_LENGTH,
			      (char *)udp->udp_ip_rcv_options,
			      opt_len);
			/* Adjust length if we are resusing the space */
			udp->udp_ip_rcv_options_len = opt_len;
		}
	} else if (udp->udp_ip_rcv_options_len) {
		mi_free((char *)udp->udp_ip_rcv_options);
		udp->udp_ip_rcv_options = nilp(u8);
		udp->udp_ip_rcv_options_len = 0;
	}

	/*
	 * If the user does not want to receive the headers in options,
	 * then we only need to pass the address up.
	 */
	if ( !udp->udp_wants_header ) {
		hdr_length = sizeof(ipa_t) + sizeof(struct T_unitdata_ind);
	} else {
		/*
		 * Calculate the size needed for a T_UNITDATA_IND block with
		 * the address and the headers.
		 */
		hdr_length += (sizeof(ipa_t) + sizeof(struct T_unitdata_ind)
			       - UDPH_SIZE);
	}
	/* Allocate a message block for the T_UNITDATA_IND structure. */
	mp = allocb(hdr_length, BPRI_MED);
	if (!mp) {
		freemsg(mp_orig);
		TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
			"udp_rput_other_end: q %X (%S)", q, "allocbfail");
		return;
	}
	mp->b_cont = mp_orig;
	mp->b_datap->db_type = M_PROTO;
	tudi = (struct T_unitdata_ind *)ALIGN32(mp->b_rptr);
	mp->b_wptr = (u_char *)tudi + hdr_length;
	tudi->PRIM_type = T_UNITDATA_IND;
	tudi->SRC_length = sizeof(ipa_t);
	tudi->SRC_offset = sizeof(struct T_unitdata_ind);
	tudi->OPT_offset = sizeof(struct T_unitdata_ind) + sizeof(ipa_t);
	hdr_length -= (sizeof(struct T_unitdata_ind) + sizeof(ipa_t));
	tudi->OPT_length = hdr_length;
#define	ipa	((ipa_t *)&tudi[1])
					/* First half of source addr */
	*(u16 *)ALIGN16(&ipa->ip_addr[0]) = ((u16 *)ALIGN16(rptr))[6];
					/* Second half of source addr */
	*(u16 *)ALIGN16(&ipa->ip_addr[2]) = ((u16 *)ALIGN16(rptr))[7];
	*(u16 *)ALIGN16(ipa->ip_port) =		/* Source port */
		((u16 *)ALIGN16(mp->b_cont->b_rptr))[-UDPH_SIZE/sizeof(u16)];
	ipa->ip_family = ((udp_t *)q->q_ptr)->udp_family;
	*(u32 *)ALIGN32(&ipa->ip_pad[0]) = 0;
	*(u32 *)ALIGN32(&ipa->ip_pad[4]) = 0;

	/* Copy in the full IP header, if udp_wants_headers is true */
	if (hdr_length != 0)
		bcopy((char *)rptr, (char *)&ipa[1], hdr_length);
#undef	ipa
	BUMP_MIB(udp_mib.udpInDatagrams);
        TRACE_2(TR_FAC_UDP, TR_UDP_RPUT_END,
                "udp_rput_other_end: q %X (%S)", q, "end");
	putnext(q, mp);
}

/*
 * return SNMP stuff in buffer in mpdata
 */
staticf	int
udp_snmp_get (q, mpctl)
	queue_t	* q;
	mblk_t	* mpctl;
{
	mblk_t		* mpdata;
	mblk_t		* mp2ctl;
	struct opthdr	* optp;
	IDP		idp;
	udp_t		* udp;
	char		buf[sizeof(mib2_udpEntry_t)];
	mib2_udpEntry_t	* ude = (mib2_udpEntry_t *)ALIGN32(buf);

	if (!mpctl
	|| !(mpdata = mpctl->b_cont)
	|| !(mp2ctl = copymsg(mpctl)))
		return 0;

	optp = (struct opthdr *)ALIGN32(&mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	optp->level = MIB2_UDP;
	optp->name = 0;
	snmp_append_data(mpdata, (char *)&udp_mib, sizeof(udp_mib));
	optp->len = msgdsize(mpdata);
	qreply(q, mpctl);

	mpctl = mp2ctl;
	mpdata = mp2ctl->b_cont;
	SET_MIB(udp_mib.udpEntrySize, sizeof(mib2_udpEntry_t));
	mutex_enter(&udp_g_lock);
	for (idp = mi_first_ptr(&udp_g_head);
	     (udp = (udp_t *)ALIGN32(idp)) != 0; 
	     idp = mi_next_ptr(&udp_g_head, idp)) {
		bcopy((char *)udp->udp_iph.iph_src, (char *)&ude->udpLocalAddress, sizeof(IpAddress));
		ude->udpLocalPort = (uint)BE16_TO_U16(udp->udp_port);
		if (udp->udp_state == TS_UNBND)
			ude->udpEntryInfo.ue_state = MIB2_UDP_unbound;
		else if (udp->udp_state == TS_IDLE)
			ude->udpEntryInfo.ue_state = MIB2_UDP_idle;
		else
			ude->udpEntryInfo.ue_state = MIB2_UDP_unknown;
		snmp_append_data(mpdata, buf, sizeof(mib2_udpEntry_t));
	}
	mutex_exit(&udp_g_lock);
	optp = (struct opthdr *)ALIGN32(&mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	optp->level = MIB2_UDP;
	optp->name = MIB2_UDP_5;
	optp->len = msgdsize(mpdata);
	qreply(q, mpctl);
	return 1;
}

/* 
 * Return 0 if invalid set request, 1 otherwise, including non-udp requests.
 * NOTE: Per MIB-II, UDP has no writeable data.
 * TODO:  If this ever actually tries to set anything, it needs to be
 * called via become_writer.
 */
/* ARGSUSED */
staticf	int
udp_snmp_set (q, level, name, ptr, len)
	queue_t	* q;
	int	level;
	int	name;
	u_char	* ptr;
	int	len;
{
	switch ( level ) {
	case MIB2_UDP:
		return 0;
	default:
		return 1;
	}
}
 
/* Report for ndd "udp_status" -- used by netstat */
/* ARGSUSED */
staticf	int
udp_status_report (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	IDP	idp;
	udp_t	* udp;
	udph_t	* udph;
	char *	state;
	uint	rport;
	u8	addr[4];

	mi_mpprintf(mp, "UDP      lport src addr        dest addr       port  state");
       /*                01234567 12345 xxx.xxx.xxx.xxx xxx.xxx.xxx.xxx 12345 UNBOUND */

	ASSERT(MUTEX_HELD(&udp_g_lock));
	for (idp = mi_first_ptr(&udp_g_head);
	     (udp = (udp_t *)ALIGN32(idp)) != 0; 
	     idp = mi_next_ptr(&udp_g_head, idp)) {
		if (udp->udp_state == TS_UNBND)
			state = "UNBOUND";
		else if (udp->udp_state == TS_IDLE)
			state = "IDLE";
		else
			state = "UnkState";
		addr[0] = udp->udp_iph.iph_dst[0];
		addr[1] = udp->udp_iph.iph_dst[1];
		addr[2] = udp->udp_iph.iph_dst[2];
		addr[3] = udp->udp_iph.iph_dst[3];
		rport = 0;
		if ((udph = udp->udp_udph) != 0)
			rport = BE16_TO_U16(udph->uh_dst_port);
		mi_mpprintf(mp,
			    "%08x %05d %03d.%03d.%03d.%03d %03d.%03d.%03d.%03d %05d %s",
			    udp, (uint)BE16_TO_U16(udp->udp_port),
			    udp->udp_src[0] & 0xff,
			    udp->udp_src[1] & 0xff,
			    udp->udp_src[2] & 0xff,
			    udp->udp_src[3] & 0xff,
			    addr[0] & 0xff, addr[1] & 0xff,
			    addr[2] & 0xff, addr[3] & 0xff,
			    rport, state);
	}
	return 0;
}

/*
 * This routine creates a T_UDERROR_IND message and passes it upstream.
 * The address and options are copied from the T_UNITDATA_REQ message
 * passed in mp.  This message is freed.
 */
staticf void
udp_ud_err (q, mp, err)
	queue_t	* q;
	mblk_t	* mp;
	int	err;
{
	mblk_t	* mp1;
reg	char	* rptr = (char *)mp->b_rptr;
reg	struct T_unitdata_req	* tudr = (struct T_unitdata_req *)ALIGN32(rptr);

	mp1 = mi_tpi_uderror_ind(&rptr[tudr->DEST_offset],
			tudr->DEST_length, &rptr[tudr->OPT_offset],
			tudr->OPT_length, err);
	if (mp1)
		qreply(q, mp1);
	freemsg(mp);
}

/*
 * This routine removes a port number association from a stream.  It
 * is called by udp_wput to handle T_UNBIND_REQ messages.
 */
staticf void
udp_unbind (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	udp_t	* udp;

	udp = (udp_t *)q->q_ptr;
	/* If a bind has not been done, we can't unbind. */
	if (udp->udp_state != TS_IDLE) {
		udp_err_ack(q, mp, TOUTSTATE, 0);
		return;
	}
	mutex_enter(&udp_g_lock);
	udp->udp_port[0] = 0;
	udp->udp_port[1] = 0;
	udp->udp_state = TS_UNBND;
	mutex_exit(&udp_g_lock);

	/* Pass the unbind to IP */
	putnext(q, mp);
}

/*
 * This routine handles all messages passed downstream.  It either
 * consumes the message or passes it downstream; it never queues a
 * a message.
 */
staticf void
udp_wput(q, mp)
	queue_t	* q;
reg	mblk_t	* mp;
{
reg	u_char	* rptr = mp->b_rptr;
	struct datab * db;
reg	iph_t	* iph;
#define	ipha	((ipha_t *)ALIGN32(iph))
reg	mblk_t	* mp1;
	int	ip_hdr_length;
#define	tudr ((struct T_unitdata_req *)ALIGN32(rptr))
	u32	u1;
	udp_t	* udp;

	TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_START,
		"udp_wput_start: q %X db_type 0%o",
		q, mp->b_datap->db_type);

	db = mp->b_datap;
	switch (db->db_type) {
	case M_PROTO:
	case M_PCPROTO:
		u1 = mp->b_wptr - rptr;
		if (u1 >= sizeof(struct T_unitdata_req) + sizeof (ipa_t)) {
			/* Detect valid T_UNITDATA_REQ here */
			if (((union T_primitives *)ALIGN32(rptr))->type
			    == T_UNITDATA_REQ)
				break;
		}
		/* FALLTHRU */
	default:
		become_exclusive(q, mp, udp_wput_excl);
		return;
	}

	udp = (udp_t *)q->q_ptr;

	/* Handle UNITDATA_REQ messages here */
	if (udp->udp_state != TS_IDLE) {
		/* If a port has not been bound to the stream, fail. */
		udp_ud_err(q, mp, TOUTSTATE);
		TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_END,
			"udp_wput_end: q %X (%S)", q, "outstate");
		return;
	}
	if (tudr->DEST_length != sizeof (ipa_t) ||
	    !(mp1 = mp->b_cont)) {
		udp_ud_err(q, mp, TBADADDR);
		TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_END,
			"udp_wput_end: q %X (%S)", q, "badaddr");
		return;
	}

	/* Ignore options in the unitdata_req */
	/* If the user did not pass along an IP header, create one. */
	ip_hdr_length = udp->udp_hdr_length + udp->udp_ip_snd_options_len;
	iph = (iph_t *)&mp1->b_rptr[-ip_hdr_length];
	if ((mp1->b_datap->db_ref != 1) ||
	    ((u_char *)iph < mp1->b_datap->db_base)) {
		mp1 = allocb(ip_hdr_length + udp_wroff_extra, BPRI_LO);
		if (!mp1) {
			udp_ud_err(q, mp, TSYSERR);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_END,
				"udp_wput_end: q %X (%S)", q, "allocbfail2");
			return;
		}
		mp1->b_cont = mp->b_cont;
		/* Use iph as a temporary variable */
		iph = (iph_t *)mp1->b_datap->db_lim;
		mp1->b_wptr = (u_char *)iph;
		iph = (iph_t *)((u_char *)iph - ip_hdr_length);
	}
	ip_hdr_length -= UDPH_SIZE;
#ifdef	_BIG_ENDIAN
	*(u16 *)ALIGN16(&iph->iph_version_and_hdr_length) =
		((((IP_VERSION << 4) | (ip_hdr_length>>2)) << 8) |
		     udp->udp_type_of_service);
	*(u16 *)ALIGN16(&iph->iph_ttl) = (udp->udp_ttl << 8) | IPPROTO_UDP;
#else
	*(u16 *)ALIGN16(&iph->iph_version_and_hdr_length) =
		((udp->udp_type_of_service << 8) |
		    ((IP_VERSION << 4) | (ip_hdr_length>>2)));
	*(u16 *)ALIGN16(&iph->iph_ttl) = (IPPROTO_UDP << 8) | udp->udp_ttl;
#endif
	/*
	 * Copy our address into the packet.  If this is zero,
	 * ip will fill in the real source address.
	 */
	*(long *)(&iph->iph_src[0]) = *(long *)(&udp->udp_src[0]);
	*(u16 *)ALIGN16(iph->iph_fragment_offset_and_flags) = 0;

	mp1->b_rptr = (u_char *)iph;

	rptr = &rptr[tudr->DEST_offset];
	u1 = mp1->b_wptr - (u_char *)iph;
	{
	mblk_t	* mp2;
	if ((mp2 = mp1->b_cont) != NULL) {
		do {
			u1 += mp2->b_wptr - mp2->b_rptr;
		} while ((mp2 = mp2->b_cont) != NULL);
	}
	}
	U32_TO_ABE16(u1, ALIGN16(iph->iph_length));
	u1 -= ip_hdr_length;
#ifdef _LITTLE_ENDIAN
	u1 = ((u1 & 0xFF) << 8) | (u1 >> 8);
#endif
	/*
	 * Copy in the destination address from the T_UNITDATA 
	 * request */
	if (!OK_32PTR(rptr)) {
		/*
		 * Copy the long way if rptr is not aligned for long
		 * word access.
		 */
#define ipa	((ipa_t *)ALIGN32(rptr))
#define	udph	((udph_t *)iph)
		iph->iph_dst[0] = ipa->ip_addr[0];
		iph->iph_dst[1] = ipa->ip_addr[1];
		iph->iph_dst[2] = ipa->ip_addr[2];
		iph->iph_dst[3] = ipa->ip_addr[3];
		iph = (iph_t *)(((u_char *)iph) + ip_hdr_length);
		udph->uh_dst_port[0] = ipa->ip_port[0];
		udph->uh_dst_port[1] = ipa->ip_port[1];
#undef ipa
#undef	udph
	} else {
#define ipa	((ipa_t *)ALIGN32(rptr))
#define	udpha	((udpha_t *)ALIGN32(iph))
		ipha->ipha_dst = *(u32 *)ALIGN32(ipa->ip_addr);
		iph = (iph_t *)(((u_char *)iph) + ip_hdr_length);
		udpha->uha_dst_port = *(u16 *)ALIGN16(ipa->ip_port);
#undef ipa
#undef udpha
	}

#define	udpha	((udpha_t *)ALIGN32(iph))
	udpha->uha_src_port = *(u16 *)ALIGN16(udp->udp_port);
#undef udpha

	if (ip_hdr_length > IP_SIMPLE_HDR_LENGTH) {
		u32	cksum;

		iph = (iph_t *)(((u_char *)iph) - ip_hdr_length);
		bcopy((char *)udp->udp_ip_snd_options, 
		      (char *)&iph[1], udp->udp_ip_snd_options_len);
		/* 
		 * Massage source route putting first source route in iph_dst.
		 * Ignore the destination in dl_unitdata_req.
		 * Create an adjustment for a source route, if any.
		 */
		cksum = ip_massage_options((ipha_t *)ALIGN32(iph));
		cksum = (cksum & 0xFFFF) + (cksum >> 16);
		cksum -= (((u_short *)ALIGN16(iph->iph_dst))[0] +
			  ((u_short *)ALIGN16(iph->iph_dst))[1]);
		if ((int)cksum < 0)
			cksum--;
		cksum = (cksum & 0xFFFF) + (cksum >> 16);
		iph = (iph_t *)(((u_char *)iph) + ip_hdr_length);
		/*
		 * IP does the checksum if uh_checksum is non-zero,
		 * We make it easy for IP to include our pseudo header
		 * by putting our length in uh_checksum.
		 */
		cksum += u1;
		cksum = (cksum & 0xFFFF) + (cksum >> 16);
#ifdef _LITTLE_ENDIAN
		if (udp_g_do_checksum)
			u1 = (cksum << 16) | u1;
#else
		if (udp_g_do_checksum)
			u1 = (u1 << 16) | cksum;
		else
			u1 <<= 16;
#endif
	} else {
		/*
		 * IP does the checksum if uh_checksum is non-zero,
		 * We make it easy for IP to include our pseudo header
		 * by putting our length in uh_checksum.
		 */
		if (udp_g_do_checksum)
			u1 |= (u1 << 16);
#ifndef _LITTLE_ENDIAN
		else
			u1 <<= 16;
#endif
	}
	((u32 *)ALIGN32(iph))[1] = u1;

	freeb(mp);

	/* We're done.  Pass the packet to ip. */
	BUMP_MIB(udp_mib.udpOutDatagrams);
	TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_END,
		"udp_wput_end: q %X (%S)", q, "end");
	putnext(q, mp1);
#undef	ipha
#undef tudr
}

staticf void
udp_wput_excl (q, mp)
	queue_t	* q;
reg	mblk_t	* mp;
{
reg	u_char	* rptr = mp->b_rptr;
	struct datab * db;
reg	mblk_t	* mp1;
	int	ip_hdr_length;
#define	tudr ((struct T_unitdata_req *)ALIGN32(rptr))
	u32	u1;
	udp_t	* udp;

	TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_START,
		"udp_wput_start: q %X db_type 0%o",
		q, mp->b_datap->db_type);

	udp = (udp_t *)q->q_ptr;

	/* Ignore options in the unitdata_req */
	db = mp->b_datap;
	switch (db->db_type) {
	case M_DATA:
		/* Prepend the "connected" header */
		if (!udp->udp_udph) {
			/* Not connected */
			freemsg(mp);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_END,
				"udp_wput_end: q %X (%S)", q, "not-connected");
			return;
		}
		ip_hdr_length = udp->udp_hdr_length + 
			udp->udp_ip_snd_options_len;
		rptr -= ip_hdr_length;
		if (!OK_32PTR(rptr)
		|| db->db_ref != 1
		|| (rptr - db->db_base) < 0) {
			mp1 = allocb(ip_hdr_length + udp_wroff_extra, BPRI_LO);
			if (!mp1) {
				/* unitdata error indication? or M_ERROR? */
				freemsg(mp);
				return;
			}
			mp1->b_cont = mp;
			mp = mp1;
			rptr = &mp->b_rptr[udp_wroff_extra];
			mp->b_wptr = &rptr[ip_hdr_length];
		}
		mp->b_rptr = rptr;

		u1 = mp->b_wptr - rptr;
		if ((mp1 = mp->b_cont) != NULL) {
			do {
				u1 += mp1->b_wptr - mp1->b_rptr;
			} while ((mp1 = mp1->b_cont) != NULL);
		}
		U32_TO_ABE16(u1, ALIGN16(udp->udp_iph.iph_length));

		u1 += UDPH_SIZE;
		u1 -= ip_hdr_length;

		/*
		 * IP does the checksum if uh_checksum is non-zero,
		 * We make it easy for IP to include our pseudo header
		 * by putting our length in uh_checksum.
		 */
#ifdef _LITTLE_ENDIAN
		u1 = ((u1 & 0xFF) << 8) | (u1 >> 8);
		if (udp_g_do_checksum)
			u1 |= (u1 << 16);
#else
		if (udp_g_do_checksum)
			u1 |= (u1 << 16);
		else
			u1 <<= 16;
#endif
		((u32 *)ALIGN32(udp->udp_udph))[1] = u1;

		/* Lay in the header */
#define	dst	((u32 *)ALIGN32(rptr))
		dst[0] = udp->udp_ipharr[0];
		dst[1] = udp->udp_ipharr[1];
		dst[2] = udp->udp_ipharr[2];
		dst[3] = udp->udp_ipharr[3];
		dst[4] = udp->udp_ipharr[4];
		dst[5] = udp->udp_ipharr[5];
#undef	dst

		/* If we have options, copy them in here */
		ip_hdr_length -= sizeof(udp->udp_ipharr);
		if (ip_hdr_length) {
			bcopy((char *)udp->udp_ip_snd_options,
			      (char *)&rptr[sizeof(udp->udp_ipharr)],
			      ip_hdr_length);
		}
		BUMP_MIB(udp_mib.udpOutDatagrams);
		TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_END,
			"udp_wput_end: q %X (%S)", q, "mdata");
		putnext(q, mp);
		return;
        case M_PROTO:
        case M_PCPROTO:
	default:
		udp_wput_other(q, mp);
		return;
	}
}


staticf void
udp_wput_other (q, mp)
	queue_t	* q;
reg	mblk_t	* mp;
{
reg	u_char	* rptr = mp->b_rptr;
	struct datab * db;
	struct iocblk * iocp;
auto	mblk_t	* mp2;
#define	tudr ((struct T_unitdata_req *)ALIGN32(rptr))
	u32	u1;
	udp_t	* udp;

	TRACE_1(TR_FAC_UDP, TR_UDP_WPUT_OTHER_START,
		"udp_wput_other_start: q %X", q);

	udp = (udp_t *)q->q_ptr;
	db = mp->b_datap;
	switch (db->db_type) {
	case M_PROTO:
	case M_PCPROTO:
		u1 = mp->b_wptr - rptr;
		if (u1 < sizeof(long)) {
			freemsg(mp);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)",
				q, "protoshort");
			return;
		}
		switch (((union T_primitives *)ALIGN32(rptr))->type) {
		case T_BIND_REQ:
			become_writer(q, mp, (pfi_t)udp_bind);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)", q, "bindreq");
			return;
		case T_CONN_REQ:
			udp_connect(q, mp);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)", q, "connreq");
			return;
		case T_INFO_REQ:
			udp_info_req(q, mp);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)", q, "inforeq");
			return;
		case T_UNITDATA_REQ:
			/*
			 * If a T_UNITDATA_REQ gets here, the address must
			 * be bad.  Valid T_UNITDATA_REQs are handled
			 * in udp_wput.
			 */
			udp_ud_err(q, mp, TBADADDR);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)",
				q, "unitdatareq");
			return;
		case T_UNBIND_REQ:
			udp_unbind(q, mp);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)", q, "unbindreq");
			return;
		case T_OPTMGMT_REQ:
			if (!snmpcom_req(q, mp, udp_snmp_set, udp_snmp_get,
				 udp->udp_priv_stream))
				optcom_req(q, mp, udp_opt_set, udp_opt_get,
					   udp_opt_chk, udp->udp_priv_stream);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)",
				q, "optmgmtreq");
			return;

		/* The following 2 TPI messages are not supported by udp. */
		case T_CONN_RES:
		case T_DISCON_REQ:
			udp_err_ack(q, mp, TNOTSUPPORT, 0);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)",
				q, "connres/disconreq");
			return;

		/* The following 3 TPI messages are illegal for udp. */
		case T_DATA_REQ:
		case T_EXDATA_REQ:
		case T_ORDREL_REQ:
			freemsg(mp);
			putctl1(RD(q), M_ERROR, EPROTO);
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)",
				q, "data/exdata/ordrel");
			return;
		default:
			break;
		}
		break;
	case M_FLUSH:
		if (*rptr & FLUSHW)
			flushq(q, FLUSHDATA);
		break;
	case M_IOCTL:
		/* TODO: M_IOCTL access to udp_wants_header. */
		iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
		switch (iocp->ioc_cmd) {
		case TI_GETPEERNAME:
			if ( !udp->udp_udph ) {
				/*
				 * If a default destination address has not
				 * been associated with the stream, then we
				 * don't know the peer's name.
				 */
				iocp->ioc_error = ENOTCONN;
err_ret:;
				iocp->ioc_count = 0;
				mp->b_datap->db_type = M_IOCACK;
				qreply(q, mp);
				TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
					"udp_wput_other_end: q %X (%S)",
					q, "getpeername");
				return;
			}
			/* FALLTHRU */
		case TI_GETMYNAME:
			/*
			 * For TI_GETPEERNAME and TI_GETMYNAME, we first
			 * need to copyin the user's netbuf structure.
			 * Processing will continue in the M_IOCDATA case
			 * below.
			 */
			mi_copyin(q, mp, nilp(char), sizeof(struct netbuf));
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)",
				q, "getmyname");
			return;
		case ND_SET:
			if ( !udp->udp_priv_stream ) {
				iocp->ioc_error = EPERM;
				goto err_ret;
			}
			/* FALLTHRU */
		case ND_GET:
			mutex_enter(&udp_g_lock);
			if (nd_getset(q, udp_g_nd, mp)) {
				mutex_exit(&udp_g_lock);
				qreply(q, mp);
				TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
					"udp_wput_other_end: q %X (%S)",
					q, "get");
				return;
			}
			mutex_exit(&udp_g_lock);
			break;
		default:
			break;
		}
		break;
	case M_IOCDATA:
		/* Make sure it is one of ours. */
		switch ( ((struct iocblk *)ALIGN32(mp->b_rptr))->ioc_cmd ) {
		case TI_GETMYNAME:
		case TI_GETPEERNAME:
			break;
		default:
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)",
				q, "iocdatadef");
			putnext(q, mp);
			return;
		}
		switch ( mi_copy_state(q, mp, &mp2) ) {
		case -1:
			TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
				"udp_wput_other_end: q %X (%S)",
				q, "iocdataneg");
			return;
		case MI_COPY_CASE(MI_COPY_IN, 1): {
			/*
			 * Now we have the netbuf structure for TI_GETMYNAME
			 * and TI_GETPEERNAME.  Next we copyout the requested
			 * address and then we'll copyout the netbuf.
			 */
			ipa_t	* ipaddr;
			iph_t	* iph;
			udph_t	* udph1;
			struct netbuf * nb = (struct netbuf *)ALIGN32(mp2->b_rptr);
			
			if ( nb->maxlen < sizeof(ipa_t) ) {
				mi_copy_done(q, mp, EINVAL);
				TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
					"udp_wput_other_end: q %X (%S)",
					q, "iocdatashort");
				return;
			}
			/*
			 * Create a message block to hold the addresses for
			 * copying out.
			 */
			mp2 = mi_copyout_alloc(q, mp, nb->buf, sizeof(ipa_t));
			if ( !mp2 ) {
				TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
					"udp_wput_other_end: q %X (%S)",
					q, "allocbfail");
				return;
			}
			ipaddr = (ipa_t *)ALIGN32(mp2->b_rptr);
			bzero((char *)ipaddr, sizeof(ipa_t));
			ipaddr->ip_family = AF_INET;
			switch ( ((struct iocblk *)ALIGN32(mp->b_rptr))->ioc_cmd ) {
			case TI_GETMYNAME:
				bcopy((char *)udp->udp_src,
					(char *)ipaddr->ip_addr,
					sizeof(ipaddr->ip_addr));
				bcopy((char *)udp->udp_port,
					(char *)ipaddr->ip_port,
					sizeof(ipaddr->ip_port));
				break;
			case TI_GETPEERNAME:
				iph = &udp->udp_iph;
				bcopy((char *)iph->iph_dst,
					(char *)ipaddr->ip_addr,
					sizeof(ipaddr->ip_addr));
				udph1 = udp->udp_udph;
				bcopy((char *)udph1->uh_dst_port,
					(char *)ipaddr->ip_port,
					sizeof(ipaddr->ip_port));
				break;
			default:
				mi_copy_done(q, mp, EPROTO);
				TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
					"udp_wput_other_end: q %X (%S)",
					q, "default");
				return;
			}
			nb->len = sizeof(ipa_t);
			mp2->b_wptr = mp2->b_rptr + sizeof(ipa_t);
			/* Copy out the address */
			mi_copyout(q, mp);
			break;
			}
		case MI_COPY_CASE(MI_COPY_OUT, 1):
			/*
			 * The address has been copied out, so now
			 * copyout the netbuf.
			 */
			mi_copyout(q, mp);
			break;
		case MI_COPY_CASE(MI_COPY_OUT, 2):
			/*
			 * The address and netbuf have been copied out.
			 * We're done, so just acknowledge the original
			 * M_IOCTL.
			 */
			mi_copy_done(q, mp, 0);
			break;
		default:
			/*
			 * Something strange has happened, so acknowledge
			 * the original M_IOCTL with an EPROTO error.
			 */
			mi_copy_done(q, mp, EPROTO);	
			break;
		}
		TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
			"udp_wput_other_end: q %X (%S)", q, "iocdata");
		return;
	default:
		/* Unrecognized messages are passed through without change. */
		break;
	}
	TRACE_2(TR_FAC_UDP, TR_UDP_WPUT_OTHER_END,
		"udp_wput_other_end: q %X (%S)", q, "end");
	putnext(q, mp);
	return;
}
