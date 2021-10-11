/*
 *	nis_rpc.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_rpc.c	1.35	95/09/19 SMI"

/*
 *	nis_rpc.c
 *
 * This module contains some grungy RPC binding functions that are
 * used by the client library.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <rpcsvc/nis.h>
#include <string.h>
#include "nis_clnt.h"
#include "nis_local.h"

#if defined(sparc)
#define _FSTAT _fstat
#else  /* !sparc */
#define _FSTAT fstat
#endif /* sparc */

/*
 * h_mask is used to record various information about host replicas.
 * It is a byte string, with the i'th byte representing the i'th host.
 * The last byte of the string is used to record the number of bytes
 * in the string that are non-zero.
 */
#define	h_mask_clear(x)  memset((char *)(x), 0, NIS_MAXREPLICAS+1)
#define	h_mask_on_p(x, i)  ((i) < NIS_MAXREPLICAS && ((x)[i] == 1))
#define	h_mask_off_p(x, i) ((i) < NIS_MAXREPLICAS && ((x)[i] == 0))
#define	h_mask_on(x, i) if ((i) < NIS_MAXREPLICAS && ((x)[i] == 0)) \
				{(x)[i] = 1; ++((x)[NIS_MAXREPLICAS]); }
#define	h_mask_off(x, i) if ((i) < NIS_MAXREPLICAS && ((x)[i] == 1)) \
				{(x)[i] = 0; --((x)[NIS_MAXREPLICAS]); }
#define	h_mask_clear_p(x) ((x)[NIS_MAXREPLICAS] == 0)
#define	h_mask_count(x) ((x)[NIS_MAXREPLICAS])

/*
 * This internal structure is used to track the bindings. NOTE
 * the client structure comes first because it is cast to a CLIENT *
 * by other functions. This keeps the private stuff out of our hair
 * in those other functions.
 */
struct server {
	CLIENT		*clnt;		/* RPC Client handle connection  */
	nis_name	mach_name;	/* server's name */
	unsigned int	flags;		/* flags for this */
	int		bpc;		/* Binding policy control */
	int		key_type;	/* Type of authentication to use */
	pid_t		pid;		/* Process creating the handle */
	uid_t		uid;		/* uid of the process */
	unsigned long	ref_cnt;	/* reference count */
	struct server	*next;		/* linked list of these. */
	struct server	*prev;		/* linked list of these. */
	int		fd;		/* fd from clnt handle */
	dev_t		rdev;		/* device of clnt fd */
};
typedef struct server server;

extern CLIENT	*__nis_get_server(directory_obj *, u_long);
extern void	 __nis_release_server(CLIENT *, int);
/* NOTE: Every __nis_get_server() MUST have	*/
/*	 a matching __nis_release_server()	*/
extern enum clnt_stat __nis_cast(nis_server *, int, h_mask, int *, int);

extern int __nis_debuglevel;

/*
 * Prototypes for static functions.
 */
static void 		free_srv(server *);
static void 		remove_server(server *);
static int		__bind_rpc(directory_obj *, int, server **, u_long);
static void		set_rdev(server *);
static int		check_rdev(server *);

#define	SRV_LRU_CACHE	8

/* Note: We could have used seperate mutex to protect the srv_listhead  */
/*	 and srv_isfrees, but we found that there no real gain in doing */
/*	 that; because whenever we need to access the free list we need */
/*	 access the active list anyway.					*/
mutex_t	srv_cache_lock = DEFAULTMUTEX;  /* lock level 1 */
static server	*srv_listhead = NULL; 	/* protected by srv_cache_lock */
static server	*srv_listtail = NULL;   /* protected by srv_cache_lock */
static server   *srv_tobefree =  NULL;    /* protected by srv_cache_lock */
static server   *srv_isfree =  NULL;    /* protected by srv_cache_lock */
static int 	srv_hi_water_mark = SRV_LRU_CACHE;
					/* protected by srv_cache_lock */

static int	srv_count = 0;
mutex_t	__nis_preferred_lock = DEFAULTMUTEX;  /* lock level ? */
static char *__nis_preferred = NULL;	/* protected by _nis_preferred_lock */
static char __nis_test_server[NIS_MAXNAMELEN+1];
					/* protected by _nis_preferred_lock */

/*
 * our own version of rand() so we don't perturb the one in libc
 */
static mutex_t __nis_randx_lock = DEFAULTMUTEX; /* lock level ? */
static long __nis_randx = 0;

long
__nis_librand()
{
	long		rx;
	sigset_t	oset;
	struct timeval	tp;

	thr_sigblock(&oset);
	mutex_lock(&__nis_randx_lock);
	if (__nis_randx == 0) {
		gettimeofday(&tp, 0);
		__nis_randx = tp.tv_usec;
	}

	rx = __nis_randx = ((__nis_randx * 1103515245L + 12345) >> 16) & 0x7fff;
	mutex_unlock(&__nis_randx_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	return (rx);
}



/*
 *	Select server from host_array that responses the fastest.
 *	'tried' is a (pointer to) a mask indication which hosts have
 *	already been tried.  'favored' is a pointer to a mask
 *	indicating which hosts should be tried first.
 *	Set the tried bit to 'on' for the selected host, so that we won't
 *	try to select it again the next time around.
 */

nis_server *
__nis_select_replica(host_array, num_hosts, favored, tried)
	nis_server *host_array;
	int num_hosts;
	h_mask favored, tried;
{
	enum clnt_stat stat;
	int answer = 0;
	int i, start, curr;

	if (num_hosts <= 1) {
		if (!h_mask_off_p(tried, 0))
			return (NULL);  /* none left */
	}

	if (num_hosts >= NIS_MAXREPLICAS)
		syslog(LOG_INFO,
"NIS+: __nis_select_replica: selection is based on first %d replicas only",
							NIS_MAXREPLICAS);

	if (__nis_debuglevel)
		syslog(LOG_INFO,
			"NIS+: __nis_select_replica: favored = %d; tried = %d",
				h_mask_count(favored), h_mask_count(tried));

	/* First, randomly try list of favored machines first */
	if (!h_mask_clear_p(favored)) {
		if (h_mask_count(favored) > 1)
			start = __nis_librand() % num_hosts;
		else
			start = 0;
		for (i = 0; i < num_hosts; i++) {
			curr = (start+i) % num_hosts;
			if (h_mask_on_p(favored, curr)) {
				h_mask_off(favored, curr);  /* remove */
				h_mask_on(tried, curr);   /* add to tried */
				if (__nis_debuglevel)
					syslog(LOG_INFO,
			"NIS+: __nis_select_replica: selected favored host %s",
						host_array[curr].name);
				return (&host_array[curr]);
			}
		}
	}

	/* We've exhausted those, so try pinging to see who responds */
	stat = __nis_cast(host_array, num_hosts, tried, &answer,
							NIS_PING_TIMEOUT);

	if (stat == RPC_SUCCESS) {
		h_mask_on(tried, answer);
		if (__nis_debuglevel)
			syslog(LOG_INFO,
			"NIS+: __nis_select_replica: selected nis_cast host %s",
						host_array[answer].name);
		return (&host_array[answer]);
	}
	if (__nis_debuglevel)
		syslog(LOG_INFO,
		"NIS+: __nis_select_replica: no servers responding");
	return (NULL);  /* we've tried everything */
}



void
__nis_set_preference(s)
	nis_name	s;
{
	sigset_t oset;

	thr_sigblock(&oset);
	mutex_lock(&__nis_preferred_lock);
	strcpy(__nis_test_server, s);
	__nis_preferred = &__nis_test_server[0];
	mutex_unlock(&__nis_preferred_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
}

void
__nis_no_preference()
{
	sigset_t oset;

	thr_sigblock(&oset);
	mutex_lock(&__nis_preferred_lock);
	__nis_preferred = NULL;
	mutex_unlock(&__nis_preferred_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
}

static void
remove_server(srv)
	server	*srv;
{
	if (srv->prev)
		srv->prev->next = srv->next;
	if (srv->next)
		srv->next->prev = srv->prev;
	if (srv_listhead == srv)
		srv_listhead = srv->next;
	if (srv_listtail == srv)
		srv_listtail = srv->prev;
	srv->next = NULL;
	srv->prev = NULL;
}

/*
 * This STUPID function is provided TEMPORARILY to get around a csh BUG.
 */
extern int __nis_destroy_callback();

int
__nis_reset_state()
{
	server	*cur;
	sigset_t	oset;

	/* WARNING: calling this function from a MT program	*/
	/* is dangerous, it should be avoid at all cost		*/
	thr_sigblock(&oset);
	mutex_lock(&srv_cache_lock);
	cur = srv_listhead;
	while (cur) {
		/* XXX: Force reference to zero, otherwise	*/
		/* 	entry will not be freed.		*/
		cur->ref_cnt = 0;
		srv_listhead = cur->next;
		free_srv(cur);
		cur = srv_listhead;
	}
	mutex_unlock(&srv_cache_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
}

/* this function try to make/free_up a entry into the "srv_is_free" list */
/* it also try to stay below the srv_high_water_mark..			 */
static void
make_free_srv_entry()
{
	struct server *cur;

	ASSERT(MUTEX_HELD(&srv_cache_lock)); /* make sure we got the lock */
	if (srv_count <  srv_hi_water_mark) {
		if (srv_isfree = (struct server *)
				    calloc(1, sizeof (struct server)))
			srv_count++;
	}

	if (!srv_isfree) {
		/*
		 * No free server struct, now try
		 * free up the one of the structure
		 * with ref_cnt == 0;
		 */
		for (cur = srv_listtail; cur; cur = cur->prev) {
			if (cur->ref_cnt == 0) {
				/*
				 * if invalid, it should be either on the
				 *  srv_tobefree list or freed
				 */
				ASSERT(cur->flags != SRV_INVALID &&
				    cur->flags != SRV_AUTH_INVALID);
				free_srv(cur);
				break;
			}
		}
	}

	/* if we get here, it means that we have no free entry	*/
	/* and we are above/at the hi_water_mark ....		*/
	/* make a new entry any way..				*/
	if (!srv_isfree) {
		if (srv_isfree = (struct server *)
				    calloc(1, sizeof (struct server)))
				srv_count++;
	}
}

void
__nis_modify_dirobj(s, dl)
	char		*s;
	directory_obj	*dl;
{
	int		i, ns, nsx, ndx = 0;
	nis_server	*nlist, *olist;

	ns = dl->do_servers.do_servers_len;
	if (ns == 1)
		return;

	olist = dl->do_servers.do_servers_val;
	nsx = ns - 1;
	nlist = (nis_server *) calloc(nsx, sizeof (nis_server));

	for (i = 0; i < ns; i++) {
		if (nis_dir_cmp(olist[i].name, s) != SAME_NAME) {
			if (ndx == nsx) {
				/* server s not found in olist */
				free(nlist);
				return;
			}
			nlist[ndx++] = olist[i];
		} else {
			xdr_free(xdr_nis_server, (char *) &(olist[i]));
		}
	}

	/* we found this machine name in the list */
	dl->do_servers.do_servers_val = nlist;
	dl->do_servers.do_servers_len--;
	free(olist);
}


/*
 * __bind_rpc()
 *
 * This function creates the binding handle that the client RPC's will use.
 *
 * This function maintains a short (8 entry) LRU cache of bindings to various
 * machines. When the client decides to talk to directory D, the list of
 * servers that serve that directory are compared to the entries in the cache
 * and if a match is found that binding is returned. If no match is found
 * then an attempt is made to bind to each machine in the list until
 * one responds.
 */
static int
__bind_rpc(dl, ns, srv, flags)
	directory_obj	*dl;
	int		ns;
	server		**srv;
	u_long		flags;
{
	register server	*cur, *nextsrv;	/* some pointers to play with    */
	register nis_server *s;
	nis_server	*m = dl->do_servers.do_servers_val;
	register int	i;		/* index into the server list    */
	u_char		bpc;		/* binding policy control	*/
	CLIENT		*clnt;		/* Our client handle		 */
	h_mask   	tried;		/* list of machines  tried */
	h_mask		favored;	/* list of favored machines */
	pid_t		mypid;		/* current process pid */
	uid_t		myuid;		/* caller's uid */
	int		fd;
	sigset_t	oset;

	h_mask_clear(favored);
	h_mask_clear(tried);

	/*
	 * Determine the binding policy :
	 *	0 - authenticated virtual circuit
	 * 	1 - authenticated datagram
	 *	2 - unauthenticated virtual circuit;
	 *	3 - unauthenticated datagram;
	 */
	bpc = ((flags & USE_DGRAM) != 0) ? 1 : 0;
	bpc += ((flags & NO_AUTHINFO) != 0) ? 2 : 0;

	if (__nis_debuglevel)
		syslog(LOG_INFO,
			"NIS+: __bind_rpc: wants handle (%s, %s())",
			(bpc == 2 || bpc == 0)? "VC" : "DG",
			(bpc == 0 || bpc == 1)? "auth" : "no auth");

	/* Look for the binding in our *short* 8 entry LRU cache */
	*srv = NULL;
	mypid = getpid();
	myuid = geteuid();
	thr_sigblock(&oset);
	mutex_lock(&srv_cache_lock);
	for (cur = srv_listhead; cur; cur = nextsrv) {
		/* First save the next pointer in case we delete this one */
		nextsrv = cur->next;
		ASSERT(cur->flags != SRV_IS_FREE);

		/*
		 * Four things can cause us to throw out a cached handle :
		 * 	a) someone has called "bad_server()" on it.
		 *	b) Some other process created it (we're a forked
		 *	   child process, or a vforked process.)
		 *      c) application closed or changed file descriptor in clnt
		 *	d) the process has done a setuid on it and it wants to
		 *	   do a authenticated operation.
		 */
		if ((cur->flags == SRV_INVALID) ||
		    (cur->flags == SRV_AUTH_INVALID) ||
		    (cur->pid != mypid) ||
		    (!check_rdev(cur)) ||
		    ((bpc <= 1) && (cur->uid != myuid))) {
			if ((cur->flags == SRV_INVALID) &&
					    ((flags & MASTER_ONLY) == 0)) {
				/* remove known bad server from dl */
				__nis_modify_dirobj(cur->mach_name, dl);
				m = dl->do_servers.do_servers_val;
				ns = dl->do_servers.do_servers_len;
			}
			remove_server(cur);
			cur->flags = SRV_TO_BE_FREED;
			cur->next = srv_tobefree;
			cur->bpc = 0;
			srv_tobefree = cur;
			continue;
		}

		/* see if our binding is to one of the machines on their list */
		for (i = 0; i < ns; i++) {
			if ((nis_dir_cmp(m[i].name, cur->mach_name) ==
								SAME_NAME)) {
				/* a bound machine is valued */
				h_mask_on(favored, i);

				if ((cur->bpc == bpc) &&
				    (cur->key_type == m[i].key_type))
					break;
				/* sometimes non-exact matches can be reused */

				/*
				 * DG+UNAUTH: do not worry about key_type.
				 * VC+UNAUTH and DG+UNAUTH can be reused.
				 */
				if ((bpc == 3) &&
					((cur->bpc == 3) || (cur->bpc == 2)))
					break;

				/*
				 * DG+AUTH: match key_types
				 * VC+AUTH can be reused.
				 */
				if ((cur->bpc == 0) &&
				    (bpc == 1) &&
				    (cur->key_type == m[i].key_type))
					break;

#ifdef NIS_NON_EXACT
				/*
				 * These may be too ambitious.
				 * authenticated handles may not work
				 * sometimes when unauthenticated ones would.
				 * (e.g. if caller cannot be authenticated).
				 */


				/* DG+unauth can reuse any existing binding */
				if (bpc == 3)
					break;

				/*
				 * VC+auth can be used for
				 * VC+unauth
				 * DG+auth if key_types are the same
				*/
				if (cur->bpc == 0 &&
				    (bpc == 2 ||
				    (bpc == 1 &&
					    (cur->key_type == m[i].key_type))))
					break;
#endif NIS_NON_EXACT
			}
		}
		if (i < ns) {
			/*
			 * It is, prepare to return it and move the
			 * binding to the head of the list.
			 */
			*srv = cur;
			if (srv_listhead != cur) {
				remove_server(cur);
				cur->next = srv_listhead;
				srv_listhead->prev = cur;
				srv_listhead = cur;
			}
			cur->ref_cnt++;
			ASSERT((*srv)->ref_cnt > 0);

			/* If nothing left to do then return */
			if (! srv_tobefree) {
				if (__nis_debuglevel) {
					syslog(LOG_INFO,
			"NIS+: __bind_rpc: reusing handle to %s (%s, %s(%d))",
							(*srv)->mach_name,
			((*srv)->bpc == 2 || (*srv)->bpc == 0)? "VC" : "DG",
		((*srv)->bpc == 0 || (*srv)->bpc == 1)? "auth" : "no auth",
							(*srv)->key_type);
				}
				mutex_unlock(&srv_cache_lock);
				thr_sigsetmask(SIG_SETMASK, &oset, NULL);
				return (TRUE);
			} else
				break;
		}
	}
	{
	    server	*prev_srv;
	    server	*next_srv;

	    /* Free any machines we found to be invalid */
	    prev_srv = 0;
	    next_srv = 0;
	    for (cur = srv_tobefree; cur; cur = next_srv) {
		next_srv = cur->next;
		if (cur->ref_cnt == 0) {
			if (prev_srv == 0)
				srv_tobefree = next_srv;
			else
				prev_srv->next = next_srv;
			auth_destroy(cur->clnt->cl_auth);
			clnt_destroy(cur->clnt);
			free(cur->mach_name);
			memset((char *)cur, 0, sizeof (server));
			cur->flags = SRV_IS_FREE;
			cur->next = srv_isfree;
			srv_isfree = cur;
		} else {
			prev_srv = cur;
		}
	    }
	}
	mutex_unlock(&srv_cache_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);

	/* If we have an answer, return. (just cleaning invalid servers) */
	if (*srv != NULL) {
		if (__nis_debuglevel)
			syslog(LOG_INFO,
			"NIS+: __bind_rpc: reusing handle to %s (%s, %s(%d))",
							(*srv)->mach_name,
			((*srv)->bpc == 2 || (*srv)->bpc == 0)? "VC" : "DG",
		((*srv)->bpc == 0 || (*srv)->bpc == 1)? "auth" : "no auth",
							(*srv)->key_type);

		return (TRUE);
	}

	/*
	 * POLICY : Where to start binding?
	 * ANSWER :
	 *   First, try hosts to which we already have bindings (albeit with
	 *   different connection requirements).
	 *   If that list has been exhausted, then
	 *   use a many-cast ping to select the most responsive server.
	 *   If that fails, return FALSE
	 */
	for (i = 0; i < ns; i++) {
		s = __nis_select_replica(m, ns, favored, tried);
		if (s == NULL)
			break;

		switch (bpc) {
			case 0 : /* auth + circuit */
				clnt = nis_make_rpchandle(s, 0,
							NIS_PROG, NIS_VERSION,
							ZMH_VC+ZMH_AUTH,
							NIS_SEND_SIZE,
							NIS_RECV_SIZE);
				break;
			case 1 : /* auth + datagram */
				clnt = nis_make_rpchandle(s, 0,
					    NIS_PROG, NIS_VERSION,
					    ZMH_DG+ZMH_AUTH, 0, 0);
				break;
			case 2 : /* circuit */
				clnt = nis_make_rpchandle(s, 0,
							NIS_PROG, NIS_VERSION,
							ZMH_VC,
							NIS_SEND_SIZE,
							NIS_RECV_SIZE);
				break;
			case 3 :  /* datagram */
				clnt = nis_make_rpchandle(s, 0,
					    NIS_PROG, NIS_VERSION,
					    ZMH_DG, 0, 0);
				break;
			default : /* error */
				clnt = NULL;
				break;
		}
		if (clnt == NULL) {
			if (__nis_debuglevel) {
				syslog(LOG_INFO,
		"NIS+: __bind_rpc: could not create handle to %s (%s, %s(%d))",
				s->name,
				(bpc == 2 || bpc == 0)? "VC" : "DG",
				(bpc == 0 || bpc == 1)? "auth" : "no auth",
				s->key_type);
			}
			continue;
		}

		if (__nis_debuglevel)
			syslog(LOG_INFO,
			"NIS+: __bind_rpc: created handle to %s (%s, %s(%d))",
								s->name,
					(bpc == 2 || bpc == 0)? "VC" : "DG",
				(bpc == 0 || bpc == 1)? "auth" : "no auth",
								s->key_type);

		/* Got one, get next available structure and cache it */
		thr_sigblock(&oset);
		mutex_lock(&srv_cache_lock);
		if (!srv_isfree) {
			make_free_srv_entry();
			if (!srv_isfree) {
				/*  no free entry, so give up... */
				if (__nis_debuglevel)
					printf(
			"can not create binding cache entry: out of memory\n");
				/* clean up */
				auth_destroy(clnt->cl_auth);
				clnt_destroy(clnt);
				mutex_unlock(&srv_cache_lock);
				thr_sigsetmask(SIG_SETMASK, &oset, NULL);
				return (FALSE);
			}
		}

		*srv = srv_isfree;
		ASSERT((*srv)->flags == SRV_IS_FREE);
		ASSERT((*srv)->mach_name == NULL);
		ASSERT((*srv)->clnt == NULL);
		srv_isfree = srv_isfree->next;
		(*srv)->mach_name = strdup(s->name);
		(*srv)->clnt	= clnt;
		ASSERT((*srv)->ref_cnt == 0);
		(*srv)->ref_cnt = 1;
		(*srv)->flags	= SRV_IN_USE;
		(*srv)->next	= srv_listhead;
		(*srv)->prev	= NULL;
		(*srv)->bpc	= bpc;
		(*srv)->pid	= mypid;
		(*srv)->uid	= myuid;
		(*srv)->key_type = s->key_type;
		set_rdev(*srv);
		if (srv_listhead)
			srv_listhead->prev = (*srv);
		if (! srv_listtail)
			srv_listtail = (*srv);
		srv_listhead = (*srv);
		mutex_unlock(&srv_cache_lock);
		if (clnt_control(clnt, CLGET_FD, (char *)&fd))
			fcntl(fd, F_SETFD, 1);	/* make it "close on exec" */
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);

		return (TRUE);
	}

	if (__nis_debuglevel) {
		syslog(LOG_INFO,
		"NIS+: __bind_rpc: could not bind to any server for (%s,%s)",
			(bpc == 2 || bpc == 0)? "VC" : "DG",
			(bpc == 0 || bpc == 1)? "auth" : "no auth");
	}
	/* Unable to bind to any server in the list (name is unreachable) */
	return (FALSE);
}

static void
free_srv(srv)
register server *srv;
{
	ASSERT(MUTEX_HELD(&srv_cache_lock)); /* make sure we got the lock */
	ASSERT(srv->ref_cnt == 0);
	remove_server(srv);		/* remove from active list */

	auth_destroy(srv->clnt->cl_auth);
	clnt_destroy(srv->clnt);
	free(srv->mach_name);
	if (srv_count > srv_hi_water_mark) {
		free(srv);
		srv_count--;
		return;
	}
#ifdef DEBUG
	srv->clnt = NULL;
	srv->mach_name = NULL;
#endif
	srv->bpc = 0;
	srv->flags = SRV_IS_FREE;
	srv->next = srv_isfree; /* put it but on free list */
	srv_isfree = srv;
}



/*
 * __nis_release_server()
 *
 * This function decreament the server reference count,
 * without destroying the client handle.
 *
 */
void
__nis_release_server(c, isbad)
	CLIENT	*c;
	int	isbad;
{
	register server *cur;	/* some pointers to play with    */
	sigset_t	oset;

	ASSERT(c != NULL); /* is someone freeing a bogus srv ??? */
	thr_sigblock(&oset);
	mutex_lock(&srv_cache_lock);
	for (cur = srv_listhead; cur; cur = cur->next) {
		if (cur->clnt == c) {
			ASSERT(cur->ref_cnt > 0);
			cur->ref_cnt--;

			if (srv_count > srv_hi_water_mark) {
				/* non-presist cacheing */
				if (cur->ref_cnt == 0) {
					free_srv(cur);
				}
				break;
			}
			if (isbad)
				cur->flags = SRV_INVALID;
			break;
		}
	}
	if (!cur) {	/* didn't find it on the server list */
		for (cur = srv_tobefree; cur; cur = cur->next) {
			if (cur->clnt == c) {
				ASSERT(cur->ref_cnt > 0);
				cur->ref_cnt--;
				break;
			}
		}
	}
	mutex_unlock(&srv_cache_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	ASSERT(cur != NULL); /* is someone freeing a bogus srv ??? */
}

/*
 * Same as nis_bad_server except
 * 1.  it marks the flag as SRV_AUTH_INVALID to prevent deletion __bind_rpc
 * 2.  it marks all other authenticated handles for same server as bad too.
 */
void
__nis_bad_auth_server(c)
	CLIENT	*c;
{
	int	i;
	register server *cur;	/* some pointers to play with    */
	sigset_t	oset;

	ASSERT(c != NULL); /* is someone freeing a bogus srv ??? */
	thr_sigblock(&oset);
	mutex_lock(&srv_cache_lock);
	for (cur = srv_listhead; cur; cur = cur->next) {
		if (cur->clnt == c) {
			ASSERT(cur->ref_cnt > 0);
			cur->ref_cnt--;
			if (srv_count > srv_hi_water_mark) {
				/* non-presist cacheing */
				if (cur->ref_cnt == 0) {
					free_srv(cur);
				}
				break;
			}
			cur->flags = SRV_AUTH_INVALID;
			break;
		}
	}
	if (!cur) {
		/* didn't find it; try the srv_tobefree list */
		for (cur = srv_tobefree; cur; cur = cur->next) {
			if (cur->clnt == c) {
				ASSERT(cur->ref_cnt > 0);
				cur->ref_cnt--;
				break;
			}
		}
	}
	if (cur) {
		/* found it, now mark other auth handles as invalid too. */
		nis_name bad_server = cur->mach_name;

		/* Mark all other authenticated handles as invalid */
		for (cur = srv_listhead; cur; cur = cur->next) {
			if (nis_dir_cmp(cur->mach_name,
			    bad_server) == SAME_NAME && cur->bpc <= 1) {
				cur->flags = SRV_AUTH_INVALID;
			}
		}
	}
	mutex_unlock(&srv_cache_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	ASSERT(cur != NULL); /* is someone freeing a bogus srv ??? */
}


/*
 * __nis_get_server()
 *
 * This function will attempt to contact one of the servers
 * in the list of servers provided. It calls __bind_rpc to intialize
 * a nis_server structure which includes within it a client handle
 * that can be passed to the RPC stubs. If this function returns NULL
 * none of the servers in the list of possible servers could be contacted.
 */
CLIENT *
__nis_get_server(slist, flags)
	directory_obj	*slist;
	u_long		flags;
{
	nis_server	*m;
	server 		*srv;
	int		i, ns;
	sigset_t	oset;
	char preferred_server[NIS_MAXNAMELEN+1];

	ns = slist->do_servers.do_servers_len;
	m = slist->do_servers.do_servers_val;
	/* For testing, we bind to the preferred server */
	thr_sigblock(&oset);
	mutex_lock(&__nis_preferred_lock);
	preferred_server[0] = '\0';
	if (__nis_preferred) {
		strcpy(preferred_server, __nis_preferred);
	}
	mutex_unlock(&__nis_preferred_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	if (preferred_server[0]) {
		for (i = 0; i < ns; i++)
			if (nis_dir_cmp(preferred_server, m[i].name)
					== SAME_NAME) {
				__bind_rpc(slist, 1, &srv, flags);
				if (srv) {
					return (srv->clnt);
				}
				return (NULL);
			}
		syslog(LOG_ERR,
			"NIS+: __nis_get_server: %s doesn't serve domain %s!",
			__nis_preferred, slist->do_name);
		return (NULL);
	}

	if ((flags & MASTER_ONLY) != 0)
		__bind_rpc(slist, 1, &srv, flags);
	else
		__bind_rpc(slist, ns, &srv, flags);

	/* If all of the machines were marked as BAD, we're hosed */
	if (!srv) {
		__nis_CacheRemoveEntry(slist);
		return (NULL);
	}

	/* return what ever the result was */
	return (srv->clnt);
}

static
void
set_rdev(srv)
	server *srv;
{
	int fd;
	int st;
	struct stat stbuf;

	if (clnt_control(srv->clnt, CLGET_FD, (char *)&fd) != TRUE ||
	    _FSTAT(fd, &stbuf) == -1) {
		syslog(LOG_DEBUG, "NIS+:  can't get rdev");
		srv->fd = -1;
		return;
	}
	srv->fd = fd;
	srv->rdev = stbuf.st_rdev;
}

static
int
check_rdev(srv)
	server *srv;
{
	struct stat stbuf;

	if (srv->fd == -1)
		return (1);    /* can't check it, assume it is okay */

	if (_FSTAT(srv->fd, &stbuf) == -1) {
		syslog(LOG_DEBUG, "NIS+:  can't stat %d", srv->fd);
		/* could be because file descriptor was closed */
		return (0);
	}
	if (srv->rdev != stbuf.st_rdev) {
		syslog(LOG_DEBUG,
		    "NIS+:  fd %d changed, old=0x%x, new=0x%x",
		    srv->fd, srv->rdev, stbuf.st_rdev);
		/* it's not our file descriptor, so don't try to close it */
		clnt_control(srv->clnt, CLSET_FD_NCLOSE, (char *)NULL);
		return (0);
	}
	return (1);    /* fd is okay */
}
