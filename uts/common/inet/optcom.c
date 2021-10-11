/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)optcom.c	1.11	93/11/11 SMI"

/*
 * This file contains common code for handling Options Management requests.
 */

#ifndef	MI_HDRS

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strlog.h>
#include <sys/errno.h>
#include <sys/tihdr.h>
#include <sys/tiuser.h>
#include <sys/timod.h>
#include <sys/socket.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>

#include <inet/common.h>
#include <inet/mi.h>
#include <inet/nd.h>
#include <inet/ip.h>
#include <inet/mib2.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/ip_mroute.h>

#ifdef	staticf
#undef	staticf
#endif
#define	staticf	static

#else

#include <types.h>
#include <stream.h>
#include <stropts.h>
#include <strlog.h>
#include <errno.h>
#include <tihdr.h>
#include <tiuser.h>
#include <timod.h>

#include <socket.h>
#include <in.h>

#include <common.h>
#include <mi.h>
#include <nd.h>
#include <tcp.h>
#include <ip.h>
#include <ip_mroute.h>

#endif

/*
 * Current upper bound on the amount of space needed to return all options.
 * Additional options with data size of sizeof(long) are handled automatically.
 * Others need hand job.
 */
#define	MAX_OPT_BUF_LEN							\
		( (A_CNT(opt_arr)<<2) +					\
		(A_CNT(opt_arr)*sizeof(struct opthdr)) +		\
		sizeof(struct linger) + 64 + sizeof(struct T_optmgmt_ack) )

/*
 * Some day there will be a TPI options management flag called T_CURRENT
 * which is used to request current option values.  Until then, we supply
 * our own.  The functionality is already built in below.
 */
#ifndef	T_CURRENT
#define	T_CURRENT	MI_T_CURRENT
#endif

/*
 * XXX have to fix socket library and TLI applications to pad the options.
 */
#undef	OPTLEN
#define	OPTLEN(len)	len

/* Options Description Structure */
typedef struct opdes_s {
	int	opdes_name;
	int	opdes_level;
	int	opdes_size;
	int	opdes_default;
	int	opdes_readonly;
	int	opdes_writeonly;
	int	opdes_priv_write;
} opdes_t;

	void	optcom_err_ack(   queue_t * q, mblk_t * mp, int t_error,
				  int sys_error   );
	void	optcom_req(   queue_t * q, mblk_t * mp, pfi_t setfn,
			      pfi_t getfn, pfb_t chkfn, int priv   );

/* Table of all known socket options. */
static	opdes_t	opt_arr[] = {
	{ SO_LINGER,	SOL_SOCKET, sizeof(struct linger), 0, 0, 0, 0 },
	{ SO_DEBUG,	SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
	{ SO_KEEPALIVE,	SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
	{ SO_DONTROUTE,	SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
	{ SO_USELOOPBACK, SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
	{ SO_BROADCAST,	SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
	{ SO_REUSEADDR, SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
	{ SO_OOBINLINE, SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
#ifdef	SO_PROTOTYPE
	/* icmp will only allow IPPROTO_ICMP for non-priviledged streams */
	{ SO_PROTOTYPE, SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
#endif
	{ SO_TYPE,	SOL_SOCKET, sizeof(long), 0, 1, 0, 0 },
	{ SO_SNDBUF,	SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
	{ SO_RCVBUF,	SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
	{ SO_SNDLOWAT,	SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
	{ SO_RCVLOWAT,	SOL_SOCKET, sizeof(long), 0, 0, 0, 0 },
	{ TCP_NODELAY,	IPPROTO_TCP, sizeof(long), 0, 0, 0, 0 },
	{ TCP_MAXSEG,	IPPROTO_TCP, sizeof(long), 536, 1, 0, 0 },
	{ TCP_NOTIFY_THRESHOLD, IPPROTO_TCP, sizeof(long), 0, 0, 0, 0 },
	{ TCP_ABORT_THRESHOLD, IPPROTO_TCP, sizeof(long), 0, 0, 0, 0 },
	{ TCP_CONN_NOTIFY_THRESHOLD, IPPROTO_TCP, sizeof(long), 0, 0, 0, 0 },
	{ TCP_CONN_ABORT_THRESHOLD, IPPROTO_TCP, sizeof(long), 0, 0, 0, 0 },
	{ IP_OPTIONS,	IPPROTO_IP, 0, 0, 0, 0, 0 },
	{ IP_HDRINCL,	IPPROTO_IP, sizeof(long), 0, 0, 0, 0 },
	{ IP_TOS,	IPPROTO_IP, sizeof(long), 0, 0, 0, 0 },
	{ IP_TTL,	IPPROTO_IP, sizeof(long), 0, 0, 0, 0 },
	{ IP_MULTICAST_IF, 	IPPROTO_IP, sizeof(long), 0, 0, 0, 0 },
	{ IP_MULTICAST_LOOP, 	IPPROTO_IP, sizeof(char), 0, 0, 0, 0 },
	{ IP_MULTICAST_TTL, 	IPPROTO_IP, sizeof(char), 1, 0, 0, 0 },
	{ IP_ADD_MEMBERSHIP, 	IPPROTO_IP, sizeof(struct ip_mreq), 0, 0, 1, 0},
	{ IP_DROP_MEMBERSHIP, 	IPPROTO_IP, sizeof(struct ip_mreq), 0, 0, 1, 0},
	{ DVMRP_INIT, 		IPPROTO_IP, 0, 0, 0, 1, 1 },
	{ DVMRP_DONE, 		IPPROTO_IP, 0, 0, 0, 1, 1 },
	{ DVMRP_ADD_VIF, 	IPPROTO_IP, sizeof(struct vifctl), 0, 0, 1, 1 },
	{ DVMRP_DEL_VIF, 	IPPROTO_IP, sizeof(vifi_t), 0, 0, 1, 1 },
	{ DVMRP_ADD_LGRP, 	IPPROTO_IP, sizeof(struct lgrplctl), 0, 0, 1,1},
	{ DVMRP_DEL_LGRP, 	IPPROTO_IP, sizeof(struct lgrplctl), 0, 0, 1,1},
	{ DVMRP_ADD_MRT, 	IPPROTO_IP, sizeof(struct mrtctl), 0, 0, 1, 1 },
	{ DVMRP_DEL_MRT, 	IPPROTO_IP, sizeof(long), 0, 0, 1, 1 },
};

/* Common code for sending back a T_ERROR_ACK. */
void
optcom_err_ack (q, mp, t_error, sys_error)
	queue_t	* q;
	mblk_t	* mp;
	int	t_error;
	int	sys_error;
{
	if ((mp = mi_tpi_err_ack_alloc(mp, t_error, sys_error)) != NULL)
		qreply(q, mp);
}

/*
 * Upper Level Protocols call this routine when they receive
 * a T_OPTMGMNT_REQ message.  They supply callback functions
 * for setting a new value for a single options, getting the
 * current value for a single option, and checking for support
 * of a single option.  optcom_req validates the option management
 * buffer passed in, and calls the appropriate routines to do the
 * job requested.
 */
void
optcom_req (q, mp, setfn, getfn, chkfn, priv)
	queue_t	* q;
	mblk_t	* mp;
	pfi_t	setfn;
	pfi_t	getfn;
	pfb_t	chkfn;
	int	priv;
{
	int	len = mp->b_wptr - mp->b_rptr;
	mblk_t	* mp1 = nilp(mblk_t);
	struct opthdr * next_opt;
	struct opthdr * opt;
	struct opthdr * opt1;
	struct opthdr * opt_end;
	struct opthdr * opt_start;
	opdes_t	* optd;
	boolean_t	pass_to_ip = false;
	struct T_optmgmt_ack * toa;
	struct T_optmgmt_req * tor =
		(struct T_optmgmt_req *)ALIGN32(mp->b_rptr);

	/* Verify message integrity. */
	if (len < sizeof(struct T_optmgmt_req)) {
bad_opt:;
		optcom_err_ack(q, mp, TBADOPT, 0);
		return;
	}
	
	switch (tor->MGMT_flags) {
		/* Is it a request for default option settings? */
	case T_DEFAULT:
		/*
		 * As we understand it, the input buffer is meaningless
		 * so we ditch the message.  A T_DEFAULT request is a
		 * request to obtain a buffer containing defaults for
		 * all supported options, so we allocate a maximum length
		 * reply.
		 */
		freemsg(mp);
		mp = allocb(MAX_OPT_BUF_LEN, BPRI_MED);
		if ( !mp ) {
no_mem:;
			optcom_err_ack(q, mp, TSYSERR, ENOMEM);
			return;
		}
		
		/* Initialize the T_optmgmt_ack header. */
		toa = (struct T_optmgmt_ack *)ALIGN32(mp->b_rptr);
		bzero((char *)toa, MAX_OPT_BUF_LEN);
		toa->PRIM_type = T_OPTMGMT_ACK;
		toa->OPT_offset = sizeof(struct T_optmgmt_ack);
		/* TODO: Is T_DEFAULT the right thing to put in MGMT_flags? */
		toa->MGMT_flags = T_DEFAULT;
		
		/* Now walk the table of all known options. */
		opt = (struct opthdr *)ALIGN32(&toa[1]);
		for ( optd = opt_arr; optd < A_END(opt_arr); optd++ ) {
			/*
			 * If the option is not supported by the protocol
			 * or if it doesn't have a readable value, skip it.
			 */
			if ( !((*chkfn)(optd->opdes_level, optd->opdes_name)) )
				continue;
			if ( optd->opdes_writeonly )
				continue;
			opt->level = optd->opdes_level;
			opt->name = optd->opdes_name;
			opt->len = optd->opdes_size;
			/*
			 * The following may have to change if more options
			 * are added with complicated defaults.
			 */
			switch (opt->len) {
			case sizeof(long):
				*(long *)&opt[1] = optd->opdes_default;
				break;
			case sizeof(short):
				*(short *)&opt[1] = optd->opdes_default;
				break;
			case sizeof(char):
				*(char *)&opt[1] = optd->opdes_default;
				break;
			}
			opt = (struct opthdr *)ALIGN32(((char *)&opt[1] +
							OPTLEN(opt->len)));
		}

		/* Now record the final length. */
		toa->OPT_length = (char *)opt - (char *)&toa[1];
		mp->b_wptr = (u_char *)opt;
		mp->b_datap->db_type = M_PCPROTO;
		/* Ship it back. */
		qreply(q, mp);
		return;

	case T_NEGOTIATE:
	case T_CURRENT:
	case T_CHECK:

		/*
		 * For T_NEGOTIATE, T_CURRENT, and T_CHECK requests, we make a
		 * pass through the input buffer validating the details and
		 * making sure each option is supported by the protocol.
		 */
		if ((opt_start = (struct opthdr *)ALIGN32(mi_offset_param(mp,
				   tor->OPT_offset, tor->OPT_length))) == NULL)
			goto bad_opt;

		opt_end = (struct opthdr *)ALIGN32(((u_char *)opt_start
					    + tor->OPT_length));
		/* LINTED lint confusion: next_opt used before set */
		for ( opt = opt_start; opt < opt_end; opt = next_opt ) {
			next_opt = (struct opthdr *)ALIGN32(((u_char *)&opt[1]
						     + OPTLEN(opt->len)));
			if ( next_opt > opt_end )
				goto bad_opt;

			/* Find the option in the opt_arr. */
			for ( optd = opt_arr; optd < A_END(opt_arr); optd++ ) {
				if ( opt->level == optd->opdes_level
				    &&  opt->name == optd->opdes_name )
					break;
			}
			if ( optd == A_END(opt_arr) )
				goto bad_opt;

			/* Additional checks dependent on operation. */
			switch ( tor->MGMT_flags ) {
			case T_NEGOTIATE:
				/* Note: opdes_size=0 implies variable length */
				if ( (optd->opdes_size && opt->len
				      != optd->opdes_size)
				    ||  optd->opdes_readonly
				    ||  !((*chkfn)(opt->level, opt->name)) )
					goto bad_opt;
				if ( optd->opdes_priv_write  &&  !priv ) {
					optcom_err_ack(q, mp, TACCES, 0);
					return;
				}
				break;
#ifdef	T_CURRENT
			case T_CURRENT:
				if (optd->opdes_writeonly)
					goto bad_opt;
				/* FALLTHRU */
#endif
			case T_CHECK:
				if ( opt->level != optd->opdes_level
				    ||  !((*chkfn)(opt->level, opt->name)) )
					goto bad_opt;
				break;
			default:
				optcom_err_ack(q, mp, TBADFLAG, 0);
				return;
			}
			/* We liked it.  Keep going. */
		}
		break;

	      default:
		optcom_err_ack(q, mp, TBADFLAG, 0);
		return;
	}			/* switch MGMT_flags */

	/* Now complete the operation as required. */
	switch ( tor->MGMT_flags ) {
	case T_CHECK:
#if 0
		/* The T_CHECK case is complete. */
		toa = (struct T_optmgmt_ack *)tor;
		toa->OPT_offset = 0;
		toa->OPT_length = 0;
		mp->b_wptr = (u_char *)&toa[1];
		break;
#endif
#ifdef	T_CURRENT
	case T_CURRENT:
		/*
		 * Allocate a maximum size reply.  Perhaps we are supposed to
		 * assume that the input buffer includes space for the answers
		 * as well as the opthdrs, but we don't know that for sure.
		 * So, instead, we create a new output buffer, using the
		 * input buffer only as a list of options.
		 */
		mp1 = allocb(MAX_OPT_BUF_LEN, BPRI_MED);
		if ( !mp1 )
			goto no_mem;
		/* Initialize the header. */
		mp1->b_datap->db_type = M_PCPROTO;
		mp1->b_wptr = &mp1->b_rptr[sizeof(struct T_optmgmt_ack)];
		toa = (struct T_optmgmt_ack *)ALIGN32(mp1->b_rptr);
		toa->OPT_offset = sizeof(struct T_optmgmt_ack);

		/*
		 * Walk through the input buffer again, this time adding
		 * entries to the output buffer for each option requested.
		 */
		opt1 = (struct opthdr *)ALIGN32(&toa[1]);
		for ( opt = opt_start; opt < opt_end
		; opt = (struct opthdr *)ALIGN32(((u_char *)&opt[1] +
						  OPTLEN(opt->len))) ) {
			opt1->name = opt->name;
			opt1->level = opt->level;
			opt1->len = (*getfn)(q, opt->level, opt->name,
					     (u_char *)&opt1[1]);
			opt1 = (struct opthdr *)ALIGN32(((u_char *)&opt1[1] +
							 OPTLEN(opt1->len)));
		}

		/* Record the final length. */
		toa->OPT_length = (u_char *)opt1 - (u_char *)&toa[1];
		mp1->b_wptr = (u_char *)opt1;
		/* Ditch the input buffer. */
		freemsg(mp);
		mp = mp1;
		break;
#endif
	case T_NEGOTIATE:
		/*
		 * Here we are expecting that the response buffer is exactly
		 * the same size as the input buffer.  We pass each opthdr
		 * to the protocol's set function.  If the protocol doesn't
		 * like what it sees, it replaces the values in the buffer
		 * with the one it is going to use, otherwise the protocol
		 * adopts the new value and returns the option untouched.
		 */
		/*
		 * Pass each negotiated option through the protocol set
		 * function.
		 */
		toa = (struct T_optmgmt_ack *)tor;
		for ( opt = opt_start; opt < opt_end;
		     opt = (struct opthdr *)ALIGN32(((u_char *)&opt[1] +
						     OPTLEN(opt->len))) ) {
			int error;

			error = (*setfn)(q, opt->level, opt->name, 
					 (u_char *)&opt[1], opt->len);
			if (error) {
				optcom_err_ack(q, mp, TSYSERR, error);
				return;
			}
		}
		pass_to_ip = true;
		break;
	default:
		optcom_err_ack(q, mp, TBADFLAG, 0);
		return;
	}
	/* Set common fields in the header. */
	toa->MGMT_flags = T_SUCCESS;
	mp->b_datap->db_type = M_PCPROTO;
	if ( pass_to_ip ) {
		/* Send it down to IP and let IP reply */
		toa->PRIM_type = T_OPTMGMT_REQ;	/* Changed by IP to ACK */
		putnext(q, mp);
	} else {
		toa->PRIM_type = T_OPTMGMT_ACK;
		qreply(q, mp);
	}
	return;
}
