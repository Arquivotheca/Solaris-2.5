/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef _LQM_H
#define	_LQM_H

#pragma ident	"@(#)ppp_lqm.h	1.7	94/01/19 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	DEFAULT_LQM_REP		3000
#define	MIN_LQM_REP		100
#define	MAX_LQM_REP		60000


typedef struct {
	u_int	last_peerOutLQRs;
	u_int	last_peerOutPackets;
	u_int	last_peerOutOctets;
	u_int	saveInLQRs;
	u_int	saveInPackets;
	u_int	saveInDiscards;
	u_int	saveInErrors;
	u_int	saveInOctets;
	u_int	outLQRs;
	u_int	inLQRs;
	u_int	inGoodOctets;
} lqm_info_t;

typedef struct {
	u_int	magic_num;
	u_int	lastOutLQRs;
	u_int	lastOutPackets;
	u_int	lastOutOctets;
	u_int	peerInLQRs;
	u_int	peerInPackets;
	u_int	peerInDiscards;
	u_int	peerInErrors;
	u_int	peerInOctets;
	u_int	peerOutLQRs;
	u_int	peerOutPackets;
	u_int	peerOutOctets;
} LQM_pack_t;

struct lqmMachine {
	pppLink_t	*linkp;
	queue_t		*readq;

	int		send_on_rec;

	LQM_pack_t	last_lqm_in;
	lqm_info_t	lqm_info;

	int		lqm_send;
	int		timedoutid;
};

/*
 * typedef struct lqmMachine lqmMachine_t;
 */

#ifdef __cplusplus
}
#endif

#endif	/* _LQM_H */
