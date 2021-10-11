/* Copyright (c) 1993 Sun Microsystems Inc */

/* Taken from 4.1.3 ypserv resolver code. */

#ifndef _NRES_H
#define	_NRES_H

#pragma ident	"@(#)nres.h	1.3	93/04/26 SMI"

#include "rpc_as.h"

#ifdef __cplusplus
extern "C" {
#endif

#if PACKETSZ > 1024
#define	MAXPACKET	PACKETSZ
#else
#define	MAXPACKET	1024
#endif
#define	REVERSE_PTR 1
#define	REVERSE_A	2
struct nres {
	rpc_as		nres_rpc_as;
	char		*userinfo;
	void		(*done) ();
	int		h_errno;
	int		reverse;	/* used for gethostbyaddr */
	struct in_addr	theaddr;	/* gethostbyaddr */
	char		name[MAXDNAME + 1];	/* gethostbyame name */
	char		search_name[2 * MAXDNAME + 2];
	int		search_index;	/* 0 up as we chase path */
	char		question[MAXPACKET];
	char		answer[MAXPACKET];
	int		using_tcp;	/* 0 ->udp in use */
	int		udp_socket;
	int		tcp_socket;
	int		got_nodata;	/* no_data rather than name_not_found */
	int		question_len;
	int		answer_len;
	int		current_ns;
	int		retries;
};

#ifdef __cplusplus
}
#endif

#endif	/* _NRES_H */
