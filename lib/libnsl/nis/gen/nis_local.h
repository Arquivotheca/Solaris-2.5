/*
 *	nis_local.h
 *
 * Manifest constants for the NIS+ client library.
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_local.h	1.8	94/11/21 SMI"

#ifndef _NIS_LOCAL_H
#define _NIS_LOCAL_H

#include "../../rpc/rpc_mt.h"
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>

#ifdef DEBUG
#define	ASSERT(cond)  \
	{ \
		if (!(cond)) { \
			printf("ASSERT ERROR:(%s),file %s,line %d\n", \
				#cond, __FILE__, __LINE__); \
			abort(); \
		} \
	}
#else
#define	ASSERT(cond)  /* no op */
#endif DEBUG

#ifdef __cplusplus
extern "C" {
#endif


#define	MAX_LINKS	16
/* maximum number of links to follow before exiting */
#define	NIS_MAXLINKS	16
#define	NIS_MAXSRCHLEN		2048
#define	NIS_MAXPATHDEPTH	128
#define	NIS_MAXPATHLEN		8192
#ifndef NIS_MAXREPLICAS
#define	NIS_MAXREPLICAS		128
#endif
typedef u_char h_mask[NIS_MAXREPLICAS+1];

/* clock definitions */
#define	MAXCLOCKS 16
#define	CLOCK_SERVER 		0
#define	CLOCK_DB 		1
#define	CLOCK_CLIENT 		2
#define	CLOCK_CACHE 		3
#define	CLOCK_CACHE_SEARCH 	4
#define	CLOCK_CACHE_FINDDIR 	5
#define	CLOCK_SCRATCH		6

#ifndef TRUE
#define	TRUE 1
#define	FALSE 0
#endif

struct nis_tick_data {
	u_long	aticks,
		dticks,
		zticks,
		cticks;
};

#define	UPD_TICKS(t, r) {t.aticks += r->aticks; \
			t.dticks += r->dticks; \
			t.zticks += r->zticks; \
			t.cticks += r->cticks; }
#define	CLR_TICKS(t) {t.aticks = 0; \
			t.dticks = 0; \
			t.zticks = 0; \
			t.cticks = 0; }
#define	RET_TICKS(t, r) {r->aticks = t.aticks; \
			r->dticks = t.dticks; \
			r->zticks = t.zticks; \
			r->cticks = t.cticks; }

/*
 * Manifest timeouts
 */
#define NIS_PING_TIMEOUT	5	/* timeout of ping operations */
#define NIS_DUMP_TIMEOUT	120	/* timeout for dump/dumplog operations */
#define NIS_FINDDIR_TIMEOUT	30	/* timeout for finddirectory operations */
#define NIS_TAG_TIMEOUT		30	/* timeout for statistics operations */
#define NIS_GEN_TIMEOUT		15	/* timeout for general NIS+ operations */
#define NIS_READ_TIMEOUT	5	/* timeout for read NIS+ operations */
#define NIS_HARDSLEEP		5 	/* interval to sleep during HARD_LOOKUP */
#define	NIS_CBACK_TIMEOUT	180	/* timeout for callback */

/*
 * use for the cached client handles
 */
#define	SRV_IS_FREE		0
#define	SRV_TO_BE_FREED		1
#define	SRV_IN_USE		2
#define	SRV_INVALID		3
#define SRV_AUTH_INVALID        4

#define	BAD_SERVER 1
#define	GOOD_SERVER 0

#define	NIS_SEND_SIZE 2048
#define	NIS_RECV_SIZE 2048
#define	NIS_TCP_TIMEOUT 3600
#define	NIS_UDP_TIMEOUT 120

/*
 * Internal functions
 */
#define	NIS_HARDSLEEP 5
extern nis_result *__nis_make_binding(nis_result **, char *, directory_obj *,
					int);
extern nis_result *__nis_core_lookup(ib_request *, u_long, int, void *,
				int (*)(nis_name, nis_object *, void *));
extern CLIENT	*__nis_get_server(directory_obj *, u_long);
extern void	 __nis_release_server(CLIENT *, int);
extern void	 __nis_bad_auth_server(CLIENT *);
extern enum clnt_stat __nis_cast(nis_server*, int, h_mask, int*, int);

extern void 	* thr_get_storage(thread_key_t *, int, void(*)(void *));
extern void 	thr_sigblock(sigset_t *);
extern void 	abort();
void nis_sort_directory_servers(directory_obj *);

#ifdef __cplusplus
}
#endif

#endif _NIS_LOCAL_H
