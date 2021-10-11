/*
 *	autod_nfs.c
 *
 *	Copyright (c) 1988-1993 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident   "@(#)autod_nfs.c 1.39     95/09/25 SMI"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <syslog.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/signal.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/fstyp.h>
#include <sys/fsid.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netconfig.h>
#include <netdir.h>
#include <errno.h>
#define	NFSCLIENT
#include <nfs/nfs.h>
#include <nfs/mount.h>
#include <rpcsvc/mount.h>
#include <rpc/nettype.h>
#include <locale.h>
#include <setjmp.h>
#include <rpcsvc/nlm_prot.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <thread.h>
#include <limits.h>
#include "automount.h"

#define	NOT_MT_SAFE	/* This file is not certified MT safe. */

#define	MAXHOSTS 	512
#define	MAXSUBNETS 	20

/* length of list of transports to try */
#define	MNT_PREF_LISTLEN	2

#define	MNTTYPE_CACHEFS "cachefs"

/*
 * The following definitions must be kept in sync
 * with those in lib/libnsl/rpc/clnt_dg.c
 */
#define	RPC_MAX_BACKOFF	30
#define	CLCR_GET_RPCB_TIMEOUT	1
#define	CLCR_SET_RPCB_TIMEOUT	2
#define	CLCR_SET_RPCB_RMTTIME	5
#define	CLCR_GET_RPCB_RMTTIME	6

struct tss {
	CLIENT	*cl;
	time_t	time_valid;
	char	prevhost[MXHOSTNAMELEN+1];
	u_long	prev_vers;
	int	lockflag;
};

extern	int	__rpc_control(int, char *);
extern	int	getnetmask_byaddr(char *, char **);
extern	void	get_opts(char *, char *, char *);
extern	void	free_mapent(struct mapent *);
extern	int	getnetmask_files(char *, char **);
int		loopbackmount(char *, char *, char *, int);
extern	int	__clnt_bindresvport(CLIENT *);
extern	int	self_check(char *);
extern	void	getword(char *, char *, char **, char **, char, int);
extern	int	mount_generic(char *, char *, char *, char *, int);
extern	int	get_retry(char *);
extern	int	str_opt(struct mnttab *, char *, char **);

extern enum clnt_stat pingnfs(char *, int, u_long *);
extern enum nfsstat nfsmount(char *, char *, char *, char *, int, int);

static void netbuf_free(struct netbuf *);
static struct knetconfig *get_knconf(struct netconfig *);
static void free_knconf(struct knetconfig *);
static struct pathcnf *get_pathconf(CLIENT *, char *, char *);
static struct tss *get_tss();
static struct mapfs *find_server(struct mapent *, char *);
static int remote_lock(char *, caddr_t);
static int trymany(struct host_names *, int, int *);
static int get_mynet_servers(struct mapfs *, struct host_names *);
static int get_mysubnet_servers(struct mapfs *, struct host_names *);
static int subnet_matches(u_int *, struct hostent *);
static u_int *get_myhosts_subnets();
static int getsubnet_byaddr(struct in_addr *, u_int *);
static void freeex_ent(struct exportnode *);
static void freeex(struct exportnode *);
static int nopt(struct mnttab *, char *);
static struct netbuf *get_addr(char *, int, int, struct netconfig **, char *,
	int);
static struct netbuf *get_the_addr(char *, int, int, struct netconfig *, int);

extern char self[];

int rpc_timeout = 20;

mount_nfs(me, mntpnt, prevhost, overlay)
	struct mapent *me;
	char *mntpnt;
	char *prevhost;
	int overlay;
{
	struct mapfs *mfs;
	int err;
	int cached;

	do {
		mfs = find_server(me, prevhost);
		if (mfs == NULL)
			return (ENOENT);

		if (self_check(mfs->mfs_host)) {
			err = loopbackmount(mfs->mfs_dir,
				mntpnt, me->map_mntopts, overlay);
		} else {
			cached = strcmp(me->map_mounter, MNTTYPE_CACHEFS) == 0;
			err = nfsmount(mfs->mfs_host, mfs->mfs_dir,
				mntpnt, me->map_mntopts, cached, overlay);
			if (err && trace > 1) {
				trace_prt(1, "  Couldn't mount %s:%s\n",
					mfs->mfs_host,
					mfs->mfs_dir);
			}
		}

		if (err) {
			mfs->mfs_ignore = 1;
		}

	} while (err);

	return (0);
}

struct mapent *
do_mapent_hosts(mapopts, host)
	char *mapopts, *host;
{
	CLIENT *cl;
	struct mapent *me, *ms, *mp;
	struct mapfs *mfs;
	struct exportnode *ex = NULL;
	struct exportnode *exlist, *texlist, **texp, *exnext;
	struct timeval timeout;
	enum clnt_stat clnt_stat;
	char name[MAXPATHLEN];
	char entryopts[1024];
	char fstype[32], mounter[32];
	int exlen, duplicate;

	/* check for special case: host is me */

	if (self_check(host)) {
		ms = (struct mapent *)malloc(sizeof (*ms));
		if (ms == NULL)
			goto alloc_failed;
		(void) memset((char *) ms, 0, sizeof (*ms));
		(void) strcpy(fstype, MNTTYPE_NFS);
		get_opts(mapopts, entryopts, fstype);
		ms->map_mntopts = strdup(entryopts);
		if (ms->map_mntopts == NULL)
			goto alloc_failed;
		ms->map_mounter = strdup(fstype);
		if (ms->map_mounter == NULL)
			goto alloc_failed;
		ms->map_fstype = strdup(MNTTYPE_NFS);
		if (ms->map_fstype == NULL)
			goto alloc_failed;

		(void) strcpy(name, "/");
		(void) strcat(name, host);
		ms->map_root = strdup(name);
		if (ms->map_root == NULL)
			goto alloc_failed;
		ms->map_mntpnt = strdup("");
		if (ms->map_mntpnt == NULL)
			goto alloc_failed;
		mfs = (struct mapfs *)malloc(sizeof (*mfs));
		if (mfs == NULL)
			goto alloc_failed;
		(void) memset((char *) mfs, 0, sizeof (*mfs));
		ms->map_fs = mfs;
		mfs->mfs_host = strdup(host);
		if (mfs->mfs_host == NULL)
			goto alloc_failed;
		mfs->mfs_dir  = strdup("/");
		if (mfs->mfs_dir == NULL)
			goto alloc_failed;
		return (ms);
	}

	if (pingnfs(host, get_retry(mapopts) + 1, NULL) != RPC_SUCCESS)
		return ((struct mapent *) NULL);

	/* get export list of host */
	cl = clnt_create(host, MOUNTPROG, MOUNTVERS, "circuit_v");
	if (cl == NULL) {
		cl = clnt_create(host, MOUNTPROG, MOUNTVERS, "datagram_v");
		if (cl == NULL) {
			syslog(LOG_ERR, "%s %s", host, clnt_spcreateerror(""));
			return ((struct mapent *) NULL);
		}

	}

	timeout.tv_usec = 0;
	timeout.tv_sec  = 25;
	if (clnt_stat = clnt_call(cl, MOUNTPROC_EXPORT, xdr_void, 0,
				xdr_exports, (caddr_t)&ex, timeout)) {
		syslog(LOG_ERR, "%s: export list: %s",
			host, clnt_sperrno(clnt_stat));
		clnt_destroy(cl);
		return ((struct mapent *) NULL);

	}

	clnt_destroy(cl);

	if (ex == NULL) {
		if (trace > 1)
			trace_prt(1,
			    gettext("  getmapent_hosts: null export list\n"));
		return ((struct mapent *) NULL);
	}

	/* now sort by length of names - to get mount order right */
	exlist = ex;
	texlist = NULL;
#ifdef lint
	exnext = NULL;
#endif
	for (; ex; ex = exnext) {
		exnext = ex->ex_next;
		exlen = strlen(ex->ex_dir);
		duplicate = 0;
		for (texp = &texlist; *texp; texp = &((*texp)->ex_next)) {
			if (exlen < (int) strlen((*texp)->ex_dir))
				break;
			duplicate = (strcmp(ex->ex_dir, (*texp)->ex_dir) == 0);
			if (duplicate) {
				/* disregard duplicate entry */
				freeex_ent(ex);
				break;
			}
		}
		if (!duplicate) {
			ex->ex_next = *texp;
			*texp = ex;
		}
	}
	exlist = texlist;

	/*
	 * The following ugly chunk of code crept in as
	 * a result of cachefs.  If it's a cachefs mount
	 * of an nfs filesystem, then have it handled as
	 * an nfs mount but have cachefs do the mount.
	 */
	(void) strcpy(fstype, MNTTYPE_NFS);
	get_opts(mapopts, entryopts, fstype);
	(void) strcpy(mounter, fstype);
	if (strcmp(fstype, MNTTYPE_CACHEFS) == 0) {
		struct mnttab m;
		char *p;

		m.mnt_mntopts = entryopts;
		if ((p = hasmntopt(&m, "backfstype")) != NULL) {
			int len = strlen(MNTTYPE_NFS);

			p += 11;
			if (strncmp(p, MNTTYPE_NFS, len) == 0 &&
			    (p[len] == '\0' || p[len] == ',')) {
				/*
				 * Cached nfs mount
				 */
				(void) strcpy(fstype, MNTTYPE_NFS);
				(void) strcpy(mounter, MNTTYPE_CACHEFS);
			}
		}
	}

	/* Now create a mapent from the export list */
	ms = NULL;
	me = NULL;

	for (ex = exlist; ex; ex = ex->ex_next) {
		mp = me;
		me = (struct mapent *)malloc(sizeof (*me));
		if (me == NULL)
			goto alloc_failed;
		(void) memset((char *) me, 0, sizeof (*me));

		if (ms == NULL)
			ms = me;
		else
			mp->map_next = me;

		(void) strcpy(name, "/");
		(void) strcat(name, host);
		me->map_root = strdup(name);
		if (me->map_root == NULL)
			goto alloc_failed;

		*name = '\0';
		if (strcmp(ex->ex_dir, "/") != 0) {
			if (*(ex->ex_dir) != '/')
				(void) strcpy(name, "/");
			(void) strcat(name, ex->ex_dir);
		}
		me->map_mntpnt = strdup(name);
		if (me->map_mntpnt == NULL)
			goto alloc_failed;

		me->map_fstype = strdup(fstype);
		if (me->map_fstype == NULL)
			goto alloc_failed;
		me->map_mounter = strdup(mounter);
		if (me->map_mounter == NULL)
			goto alloc_failed;
		me->map_mntopts = strdup(entryopts);
		if (me->map_mntopts == NULL)
			goto alloc_failed;

		mfs = (struct mapfs *)malloc(sizeof (*mfs));
		if (mfs == NULL)
			goto alloc_failed;
		(void) memset((char *) mfs, 0, sizeof (*mfs));
		me->map_fs = mfs;
		mfs->mfs_host = strdup(host);
		if (mfs->mfs_host == NULL)
			goto alloc_failed;
		mfs->mfs_dir = strdup(ex->ex_dir);
		if (mfs->mfs_dir == NULL)
			goto alloc_failed;
	}
	freeex(exlist);
	return (ms);

alloc_failed:
	syslog(LOG_ERR, "Memory allocation failed: %m");
	free_mapent(ms);
	freeex(exlist);
	return ((struct mapent *) NULL);
}

/*
 * This function parses the map entry for a nfs type file system
 * The input is the string lp (and lq) which can be one of the
 * following forms:
 *
 * a) host[(penalty)][,host[(penalty)]]... :/directory
 * b) host[(penalty)]:/directory[,host[(penalty)]:/directory]...
 * This routine constructs a mapfs link-list for each of
 * the hosts and the corresponding file system. The list
 * is then attatched to the mapent struct passed in.
 */
parse_nfs(mapname, me, w, wq, lp, lq, wsize)
	struct mapent *me;
	char *mapname, *w, *wq, **lp, **lq;
	int wsize;
{
	struct mapfs *mfs, **mfsp;
	char *wlp, *wlq;
	char *hl, hostlist[1024], *hlq, hostlistq[1024];
	char hostname_and_penalty[MXHOSTNAMELEN+5];
	char *hn, *hnq, hostname[MXHOSTNAMELEN+1];
	char dirname[MAXPATHLEN+1], subdir[MAXPATHLEN+1];
	char qbuff[MAXPATHLEN+1], qbuff1[MAXPATHLEN+1];
	char pbuff[10], pbuffq[10];
	int penalty;

	mfsp = &me->map_fs;
	*mfsp = NULL;

	while (*w && *w != '/') {
		wlp = w; wlq = wq;
		getword(hostlist, hostlistq, &wlp, &wlq, ':',
			sizeof (hostlist));
		if (!*hostlist)
			goto bad_entry;
		getword(dirname, qbuff, &wlp, &wlq, ':', sizeof (dirname));
		if (*dirname == '\0')
			goto bad_entry;
		*subdir = '/'; *qbuff = ' ';
		getword(subdir+1, qbuff+1, &wlp, &wlq, ':', sizeof (subdir));
		if (*(subdir+1))
			(void) strcat(dirname, subdir);

		hl = hostlist; hlq = hostlistq;
		for (;;) {
			getword(hostname_and_penalty, qbuff, &hl, &hlq, ',',
				sizeof (hostname_and_penalty));
			if (!*hostname_and_penalty)
				break;
			hn = hostname_and_penalty;
			hnq = qbuff;
			getword(hostname, qbuff1, &hn, &hnq, '(',
				sizeof (hostname));

			if (strcmp(hostname, hostname_and_penalty) == 0) {
				penalty = 0;
			} else {
				hn++; hnq++;
				getword(pbuff, pbuffq, &hn, &hnq, ')',
					sizeof (pbuff));
				if (!*pbuff)
					penalty = 0;
				else
					penalty = atoi(pbuff);
			}
			mfs = (struct mapfs *)malloc(sizeof (*mfs));
			if (mfs == NULL)
				return (-1);
			(void) memset(mfs, 0, sizeof (*mfs));
			*mfsp = mfs;
			mfsp = &mfs->mfs_next;

			mfs->mfs_host = strdup(hostname);
			mfs->mfs_penalty = penalty;
			if (mfs->mfs_host == NULL)
				return (-1);
			mfs->mfs_dir = strdup(dirname);
			if (mfs->mfs_dir == NULL)
				return (-1);
		}
		getword(w, wq, lp, lq, ' ', wsize);
	}
	return (0);

bad_entry:
	syslog(LOG_ERR, "bad entry in map %s \"%s\"", mapname, w);
	return (1);
}

static int
getsubnet_byaddr(ptr, subnet)
struct in_addr *ptr;
u_int *subnet;
{
	int  j;
	u_long i, netmask;
	u_char *bytes;
	u_int u[4];
	struct in_addr net;
	char key[128], *mask;

	i = ntohl(ptr->s_addr);
	bytes = (u_char *)(&net);
	if (IN_CLASSA(i)) {
		net.s_addr = htonl(i & IN_CLASSA_NET);
		(void) sprintf(key, "%d.0.0.0", bytes[0]);
	} else 	if (IN_CLASSB(i)) {
		net.s_addr = htonl(i & IN_CLASSB_NET);
		(void) sprintf(key, "%d.%d.0.0", bytes[0], bytes[1]);
	} else 	if (IN_CLASSC(i)) {
		net.s_addr = htonl(i & IN_CLASSC_NET);
		(void) sprintf(key, "%d.%d.%d.0", bytes[0], bytes[1], bytes[2]);
	}
	if (getnetmask_byaddr(key, &mask) != 0)
		return (-1);

	bytes = (u_char *) (&netmask);
	(void) sscanf(mask, "%d.%d.%d.%d", &u[0], &u[1], &u[2], &u[3]);
	free(mask);
	for (j = 0; j < 4; j++)
		bytes[j] = u[j];
	netmask = ntohl(netmask);
	if (IN_CLASSA(i))
		*subnet = IN_CLASSA_HOST & netmask & i;
	else if (IN_CLASSB(i)) {
		*subnet = IN_CLASSB_HOST & netmask & i;
	} else if (IN_CLASSC(i))
		*subnet = IN_CLASSC_HOST & netmask & i;

	return (0);
}

/*
 * This function is called to get the subnets to which the
 * host is connected to. Since the subnets to which the hosts
 * is attached is not likely to change while the automounter
 * is running, this computation is done only once. So only
 * my_subnet_cnt is protected by a mutex.
 */
static u_int *
get_myhosts_subnets()
{
	static mutex_t mysubnet_hosts = DEFAULTMUTEX;
	static int my_subnet_cnt = 0;
	static u_int my_subnets[MAXSUBNETS + 1];
	struct hostent *myhost_ptr;
	struct in_addr *ptr;

	mutex_lock(&mysubnet_hosts);
	if (my_subnet_cnt)  {
		mutex_unlock(&mysubnet_hosts);
		return (my_subnets);
	}
	myhost_ptr = (struct hostent *) gethostbyname(self);
	/* LINTED pointer alignment */
	while ((ptr = (struct in_addr *) *myhost_ptr->h_addr_list++) != NULL) {
		if (my_subnet_cnt < MAXSUBNETS) {
			if (getsubnet_byaddr(ptr,
				&my_subnets[my_subnet_cnt]) == 0)
				my_subnet_cnt++;
		}
	}
	my_subnets[my_subnet_cnt] = (u_int) NULL;
	mutex_unlock(&mysubnet_hosts);
	return (my_subnets);

}

/*
 * Given a host-entry, check if it matches any of the subnets
 * to which the localhost is connected to
 */
static int
subnet_matches(mysubnets, hs)
u_int *mysubnets;
struct hostent *hs;
{
	struct in_addr *ptr;
	u_int subnet;

	/* LINTED pointer alignment */
	while ((ptr = (struct in_addr *) *hs->h_addr_list++) != NULL) {
		if (getsubnet_byaddr(ptr, &subnet) == 0)
			while (*mysubnets != (u_int) NULL)
				if (*mysubnets++ == subnet)
					return (1);
	}

	return (0);
}

/*
 * Given a list of servers find all the servers who are
 * on the same subnet(s)  as the local host
 */
static int
get_mysubnet_servers(mfs_head, hosts_ptr)
struct mapfs *mfs_head;
struct host_names *hosts_ptr;
{
	NCONF_HANDLE *nc;
	struct netconfig *nconf;
	u_int *mysubnets = (u_int *) NULL;
	struct mapfs *mfs;
	struct hostent *hs;
	int cnt;

	cnt = 0;
	nc = setnetconfig();
	if (nc == NULL)
		return (0);
	while (nconf = getnetconfig(nc)) {
		if ((nconf->nc_semantics == NC_TPI_CLTS) &&
		    (strcmp(nconf->nc_protofmly, NC_INET) == 0) &&
		    (strcmp(nconf->nc_proto, NC_UDP) == 0)) {
			mysubnets = get_myhosts_subnets();
			for (mfs = mfs_head; mfs && (cnt < MAXHOSTS);
				mfs = mfs->mfs_next) {
				if (mfs->mfs_ignore)
					continue;
				hs = (struct hostent *)
					gethostbyname(mfs->mfs_host);
				if (hs == NULL)
					continue;
				if (subnet_matches(mysubnets, hs)) {
					hosts_ptr->host = mfs->mfs_host;
					hosts_ptr->penalty = mfs->mfs_penalty;
					hosts_ptr++;
					cnt++;
				}
			}
			hosts_ptr->host = (char *) NULL; /* terminate list */
		}
	}

	if (nc)
		endnetconfig(nc);
	return (cnt);

}


static int
get_mynet_servers(mfs_head, hosts_ptr)
struct mapfs *mfs_head;
struct host_names *hosts_ptr;
{
	NCONF_HANDLE *nc;
	struct netconfig *nconf;
	struct mapfs *mfs;
	struct hostent *hs;
	int mynet = 0;
	int cnt;

	cnt = 0;
	nc = setnetconfig();
	if (nc == NULL)
		return (0);
	while (nconf = getnetconfig(nc)) {
		if ((nconf->nc_semantics == NC_TPI_CLTS) &&
		    (strcmp(nconf->nc_protofmly, NC_INET) == 0) &&
		    (strcmp(nconf->nc_proto, NC_UDP) == 0)) {
			hs = gethostbyname(self);
			mynet = inet_netof(*((struct in_addr *)
						/* LINTED pointer alignment */
						*hs->h_addr_list));
			for (mfs = mfs_head; mfs && (cnt < MAXHOSTS);
				mfs = mfs->mfs_next) {
				if (mfs->mfs_ignore)
					continue;
				hs = (struct hostent *)
					gethostbyname(mfs->mfs_host);
				if (hs == NULL)
					continue;
				if (mynet == inet_netof(*((struct in_addr *)
						/* LINTED pointer alignment */
						*hs->h_addr_list))) {
					hosts_ptr->host = mfs->mfs_host;
					hosts_ptr->penalty = mfs->mfs_penalty;
					hosts_ptr++;
					cnt++;
				}
			}
			hosts_ptr->host = (char *) NULL; /* terminate lilst */
		}
	}

	if (nc)
		endnetconfig(nc);
	return (cnt);
}

/*
 * ping a bunch of hosts at once and find out who
 * responds first
 */
static int
trymany(host_array, timeout, best_host)
	struct host_names *host_array;
	int timeout;
	int *best_host;
{
	enum clnt_stat nfs_cast();
	enum clnt_stat clnt_stat;

	if (trace > 1) {
		struct host_names *h;

		trace_prt(1, "  nfs_cast: ");
		for (h = host_array; h->host; h++)
			trace_prt(0, "%s ", h->host);
		trace_prt(0, "\n");
	}

	clnt_stat = nfs_cast(host_array, timeout, best_host);
	if (trace > 1) {
		trace_prt(1, "  nfs_cast: got %s\n",
			(int) clnt_stat ? "no response" :
					host_array[*best_host].host);
	}
	if (clnt_stat != RPC_SUCCESS) {
		char buff[2048];
		struct host_names *h;

		for (h = host_array; h->host; h++) {
			(void) strcat(buff, h->host);
			if ((h + 1)->host)
				(void) strcat(buff, ",");
		}

		syslog(LOG_ERR, "servers %s not responding: %s",
			buff, clnt_sperrno(clnt_stat));
	}

	return ((int) clnt_stat);
}

/*
 * This function is added to detect compatibility problem with SunOS4.x.
 * The compatibility problem exists when fshost cannot decode the request
 * arguments for NLM_GRANTED procedure.
 * Only in this case  we use local locking.
 * In any other case we use fshost's lockd for remote file locking.
 */
static int
remote_lock(fshost, fh)
	char *fshost;
	caddr_t fh;
{
	nlm_testargs rlm_args;
	nlm_res rlm_res;
	struct timeval timeout = { 5, 0};
	CLIENT *cl;
	enum clnt_stat rpc_stat;
	struct utsname myid;

	(void) memset((char *) &rlm_args, 0, sizeof (nlm_testargs));
	(void) memset((char *) &rlm_res, 0, sizeof (nlm_res));
	/*
	 * Assign the hostname and the file handle for the
	 * NLM_GRANTED request below.  If for some reason the uname call fails,
	 * list the server as the caller so that caller_name has some
	 * reasonable value.
	 */
	if (uname(&myid) == -1)  {
		rlm_args.alock.caller_name = fshost;
	} else {
		rlm_args.alock.caller_name = myid.nodename;
	}
	rlm_args.alock.fh.n_len = sizeof (fhandle_t);
	rlm_args.alock.fh.n_bytes = fh;

	cl = clnt_create(fshost, NLM_PROG, NLM_VERS, "datagram_v");
	if (cl == NULL)
		return (1);

	rpc_stat = clnt_call(cl, NLM_GRANTED,
			xdr_nlm_testargs, (caddr_t)&rlm_args,
			xdr_nlm_res, (caddr_t)&rlm_res, timeout);
	clnt_destroy(cl);

	return (rpc_stat == RPC_CANTDECODEARGS);
}

static struct mapfs *
find_server(me, preferred)
	struct mapent *me;
	char *preferred;
{
	int entrycount;
	struct mapfs *mfs, *mfs_one;
	struct host_names host_array[MAXHOSTS + 1];
	int best_host; /* index into the host array */
	/*
	 * last entry reserved for terminating list
	 * in case there are MAXHOST servers
	 */
	int subnet_cnt, net_cnt;

	/*
	 * get addresses & see if any are myself
	 * or were mounted from previously in a
	 * hierarchical mount.
	 */
	entrycount =  subnet_cnt = net_cnt = 0;
	mfs_one = NULL;
	for (mfs = me->map_fs; mfs; mfs = mfs->mfs_next) {
		if (mfs->mfs_ignore)
			continue;
		mfs_one = mfs;
		if (self_check(mfs->mfs_host) ||
		    strcmp(mfs->mfs_host, preferred) == 0)
			return (mfs);
		entrycount++;
	}
	if (entrycount == 0)
		return (NULL);

	if (entrycount == 1)
		return (mfs_one);

	subnet_cnt =  get_mysubnet_servers(me->map_fs, host_array);

	if (subnet_cnt > 0) {
		if (trymany(host_array, rpc_timeout / 2, &best_host) == 0)
			for (mfs = me->map_fs; mfs; mfs = mfs->mfs_next)
				if (host_array[best_host].host ==
				    mfs->mfs_host) {
					return (mfs);
				}
	}
	if (subnet_cnt == entrycount)
		return (NULL);

	net_cnt =  get_mynet_servers(me->map_fs, host_array);

	if (net_cnt > subnet_cnt) {
		if (trymany(host_array, rpc_timeout / 2, &best_host) == 0)
			for (mfs = me->map_fs; mfs; mfs = mfs->mfs_next)
				if (host_array[best_host].host ==
				    mfs->mfs_host) {
					return (mfs);
				}
	}
	if (entrycount > net_cnt) {
		int i = 0;
		for (mfs = me->map_fs; mfs && (i < MAXHOSTS);
			mfs = mfs->mfs_next) {
			if (mfs->mfs_ignore)
				continue;
			host_array[i].host = mfs->mfs_host;
			host_array[i++].penalty = mfs->mfs_penalty;
		}
		host_array[i].host = (char *) NULL; /* terminate the list */
		if (trymany(host_array, rpc_timeout / 2, &best_host) == 0)
			for (mfs = me->map_fs; mfs; mfs = mfs->mfs_next)
				if (host_array[best_host].host ==
				    mfs->mfs_host) {
					return (mfs);
				}
	}
	return (NULL);
}

#ifndef NOT_MT_SAFE

static thread_key_t nfsmount_key = NULL;
static mutex_t nfsmount_keylock = DEFAULTMUTEX;

void
nfsmount_key_destroy(d)
	void *d;
{
	if (d != NULL)
		free(d);
}

static struct tss *
get_tss()
{
	int i;
	struct tss *d;

	mutex_lock(&nfsmount_keylock);
	if (!nfsmount_key) {
		if (thr_keycreate(&nfsmount_key,
				nfsmount_key_destroy) != 0) {
			perror("get_tss");
			syslog(LOG_ERR, "Could not do thr_keycreate");
			mutex_unlock(&nfsmount_keylock);
			return ((struct tss *) NULL);
		}
	}
	mutex_unlock(&nfsmount_keylock);

	thr_getspecific(nfsmount_key, (void **) &d);
	if ((struct tss *)d == NULL) {

		if ((d = (struct tss *) malloc(sizeof (struct tss))) != NULL) {
			(void) memset((char *) d, 0, sizeof (struct tss));
			thr_setspecific(nfsmount_key, (void *) d);
		}
	}
	return (d);
}
#else

static struct tss *
get_tss()
{
	static struct tss *d = NULL;
	if (d)
		return (d);

	if ((d = (struct tss *) malloc(sizeof (struct tss))) != NULL)
		(void) memset((char *) d, 0, sizeof (struct tss));
	return (d);
}
#endif

enum nfsstat
nfsmount(host, dir, mntpnt, opts, cached, overlay)
	char *host, *dir, *mntpnt, *opts;
	int cached, overlay;
{
	char netname[MAXNETNAMELEN+1];
	char remname[MAXPATHLEN];
	struct mnttab m;
	struct nfs_args args;
	int flags;
	struct fhstatus fhs;
	struct timeval timeout;
	enum clnt_stat rpc_stat;
	enum nfsstat status;
	struct stat stbuf;
	struct netconfig *nconf = NULL;
	int cache_time = 60;	/* sec */
	u_long vers;
	u_long outvers;
	u_long nfsvers;
	int posix;
	int newhost;
	struct tss *tsd;
	int err;
	struct nd_addrlist *retaddrs = NULL;
	struct mountres3 res3;
	nfs_fh3 fh3;
	char *fstype;
	int count, i;
	int *auths;
	int delay = 5;
	int retries;
	char *nfs_proto = NULL;
	u_int nfs_port = 0;

	if (trace > 1) {
		trace_prt(1, "  nfsmount: %s:%s %s %s\n",
			host, dir, mntpnt, opts);
	}

	if ((tsd = get_tss()) == NULL)
		return (NFSERR_NOSPC);

	/* Make sure mountpoint is safe to mount on */
	if (lstat(mntpnt, &stbuf) < 0) {
		syslog(LOG_ERR, "Couldn't stat %s: %m", mntpnt);
		return (NFSERR_NOENT);
	}

	(void) sprintf(remname, "%s:%s", host, dir);
	m.mnt_mntopts = opts;

	/*
	 * Attempt figure out which version of NFS to use for this mount.
	 * If the version number was specified, then use it.
	 * Otherwise, default to NFS Version 3 with a fallback
	 * to NFS Version 2.
	 */
	nfsvers = nopt(&m, MNTOPT_VERS);
	if (nfsvers != 0 && nfsvers != NFS_VERSION && nfsvers != NFS_V3) {
		syslog(LOG_ERR, "Incorrect NFS version specified for %s",
			remname);
		return (NFSERR_NOENT);
	}

	if (nfsvers == 0 || nfsvers == NFS_V3)
		vers = NFS_V3;
	else
		vers = NFS_VERSION;

	/*
	 * After the ping is completed, vers will contain the
	 * version of NFS that the server supports.
	 */
	if (pingnfs(host, 1, &vers) != RPC_SUCCESS) {
		syslog(LOG_ERR, "server %s not responding", host);
		return (NFSERR_NOENT);
	}

	if (nfsvers != 0 && nfsvers != vers) {
		syslog(LOG_ERR, "NFS version specified for %s not supported",
			remname);
		return (NFSERR_NOENT);
	}

	/*
	 * Map the NFS version number to a MOUNT version number.
	 */
	if (vers == NFS_V3)
		vers = MOUNTVERS3;
	else
		vers = MOUNTVERS_POSIX;

	/*
	 * If it's cached then we need to get
	 * cachefs to mount it.
	 */
	if (cached) {
		err = mount_generic(remname, MNTTYPE_CACHEFS, opts,
			mntpnt, overlay);
		return (err ? NFSERR_NOENT : 0);
	}

	newhost = strcmp(host, tsd->prevhost);

	timeout.tv_usec = 0;
	timeout.tv_sec = rpc_timeout;
	rpc_stat = RPC_TIMEDOUT;
	retries = get_retry(opts);
	/*
	 * Get a client handle if it's a new host or if
	 * the handle is too old.
	 */
retry:
	if (tsd->cl == NULL || newhost ||
	    vers != tsd->prev_vers || time_now > tsd->time_valid) {

		if (tsd->cl) {
			if ((tsd->cl)->cl_auth) {
				AUTH_DESTROY((tsd->cl)->cl_auth);
			}
			clnt_destroy(tsd->cl);
		}
		tsd->cl = clnt_create_vers(host, MOUNTPROG, &outvers, MOUNTVERS,
					    vers, "udp");
		if (tsd->cl == NULL) {
			syslog(LOG_ERR, "%s %s", remname,
				clnt_spcreateerror("server not responding"));
			return (NFSERR_NOENT);
		}

		tsd->prev_vers = outvers;

		if (__clnt_bindresvport(tsd->cl) < 0) {
			syslog(LOG_ERR, "mount %s: %s", remname,
				"Couldn't bind to reserved port");
			clnt_destroy(tsd->cl);
			tsd->cl = NULL;
			return (NFSERR_NOENT);
		}
		(tsd->cl)->cl_auth = authsys_create_default();

		(void) strcpy(tsd->prevhost, host);
		tsd->time_valid = time_now + cache_time;
	}

	/*
	 * set mount args
	 */
	(void) memset(&args, 0, sizeof (args));

	if (hasmntopt(&m, MNTOPT_KERB) != NULL) {
		args.flags |= NFSMNT_KERBEROS;
	}
	if (hasmntopt(&m, MNTOPT_SECURE) != NULL) {
		args.flags |= NFSMNT_SECURE;
	}
	if ((args.flags & NFSMNT_SECURE) && (args.flags & NFSMNT_KERBEROS)) {
		syslog(LOG_ERR,
		    "conflicting options for %s: secure and kerberos", remname);
		return (NFSERR_IO);
	}

	posix = (hasmntopt(&m, MNTOPT_POSIX) != NULL) ? 1 : 0;

	/*
	 * Get fhandle of remote path from server's mountd
	 */

	switch (tsd->prev_vers) {
	case MOUNTVERS:
		if (posix) {
			syslog(LOG_ERR, "can't get posix info for %s", remname);
			return (NFSERR_NOENT);
		}
	/* FALLTHRU */
	case MOUNTVERS_POSIX:
		if (nfsvers == NFS_V3) {
			syslog(LOG_ERR, "%s doesn't support NFS Version 3",
			    host);
			return (NFSERR_NOENT);
		}
		rpc_stat = clnt_call(tsd->cl, MOUNTPROC_MNT, xdr_dirpath,
			(caddr_t)&dir, xdr_fhstatus, (caddr_t)&fhs, timeout);
		if ((rpc_stat == RPC_TIMEDOUT) && (retries-- > 0)) {
			tsd->time_valid = 0;
			(void) sleep(delay);
			delay *= 2;
			if (delay > 20)
				delay = 20;
			goto retry;
		}
		if (rpc_stat != RPC_SUCCESS) {
			/*
			 * Given the way "clnt_sperror" works, the "%s"
			 * immediately following the "not responding" is
			 * correct.
			 */
			syslog(LOG_ERR, "%s server not responding%s", remname,
			    clnt_sperror(tsd->cl, ""));
			if ((tsd->cl)->cl_auth) {
				AUTH_DESTROY((tsd->cl)->cl_auth);
			}
			clnt_destroy(tsd->cl);
			tsd->cl = NULL;
			return (NFSERR_NOENT);
		}
		if ((errno = fhs.fhs_status) != MNT_OK)  {
			if (errno == EACCES) {
				status = NFSERR_ACCES;
			} else {
				syslog(LOG_ERR, "%s: %m", remname);
				status = NFSERR_IO;
			}
			return (status);
		}
		args.fh = (caddr_t)&fhs.fhstatus_u.fhs_fhandle;
		fstype = MNTTYPE_NFS;
		nfsvers = NFS_VERSION;
		break;
	case MOUNTVERS3:
		posix = 0;
		(void) memset((char *)&res3, '\0', sizeof (res3));
		rpc_stat = clnt_call(tsd->cl, MOUNTPROC_MNT, xdr_dirpath,
			(caddr_t)&dir, xdr_mountres3, (caddr_t)&res3, timeout);
		if ((rpc_stat == RPC_TIMEDOUT) && (retries-- > 0)) {
			tsd->time_valid = 0;
			(void) sleep(delay);
			delay *= 2;
			if (delay > 20)
				delay = 20;
			goto retry;
		}
		if (rpc_stat != RPC_SUCCESS) {
			/*
			 * Given the way "clnt_sperror" works, the "%s"
			 * immediately following the "not responding" is
			 * correct.
			 */
			syslog(LOG_ERR, "%s server not responding%s", remname,
			    clnt_sperror(tsd->cl, ""));
			if ((tsd->cl)->cl_auth) {
				AUTH_DESTROY((tsd->cl)->cl_auth);
			}
			clnt_destroy(tsd->cl);
			tsd->cl = NULL;
			return (NFSERR_NOENT);
		}
		if ((errno = res3.fhs_status) != MNT_OK)  {
			if (errno == EACCES) {
				status = NFSERR_ACCES;
			} else {
				syslog(LOG_ERR, "%s: %m", remname);
				status = NFSERR_IO;
			}
			return (status);
		}
		auths =
		    res3.mountres3_u.mountinfo.auth_flavors.auth_flavors_val;
		count =
		    res3.mountres3_u.mountinfo.auth_flavors.auth_flavors_len;
		if (args.flags & NFSMNT_KERBEROS) {
			for (i = 0; i < count; i++) {
				if (auths[i] == AUTH_KERB)
					break;
			}
			if (i >= count) {
				syslog(LOG_ERR, "%s not exported kerberos",
				    remname);
				return (NFSERR_NOENT);
			}
		} else if (args.flags & NFSMNT_SECURE) {
			for (i = 0; i < count; i++) {
				if (auths[i] == AUTH_DES)
					break;
			}
			if (i >= count) {
				syslog(LOG_ERR, "%s not exported secure",
				    remname);
				return (NFSERR_NOENT);
			}
		} else if (count > 0) {
			/*
			 * Find the first recognized auth flavor
			 * from the auth list returned by the server.
			 */
			for (i = 0; i < count; i++) {
			    switch (auths[i]) {
			    case AUTH_NONE:
			    case AUTH_UNIX:
			    case AUTH_SHORT:
				goto success;
			    case AUTH_DES:
				args.flags |= NFSMNT_SECURE;
				goto success;
			    case AUTH_KERB:
				args.flags |= NFSMNT_KERBEROS;
				goto success;
			    default:
				break;
			    }
			}
			/*
			 *  If none is found, print out the 1st unrecognized
			 *  flavor number information.
			 */
			syslog(LOG_ERR, "%s: unknown authentication flavor %d",
				remname, auths[0]);
			return (NFSERR_NOENT);
		}
success:
		fh3.fh3_length =
		    res3.mountres3_u.mountinfo.fhandle.fhandle3_len;
		(void) memcpy(fh3.fh3_u.data,
		    res3.mountres3_u.mountinfo.fhandle.fhandle3_val,
		    fh3.fh3_length);
		args.fh = (caddr_t)&fh3;
		fstype = MNTTYPE_NFS3;
		nfsvers = NFS_V3;
		break;
	default:
		syslog(LOG_ERR, "unknown MOUNT version %ld on %s", vers,
		    remname);
		return (NFSERR_NOENT);
	}
	args.flags |= NFSMNT_INT;	/* default is "intr" */
	args.hostname = host;
	args.flags |= NFSMNT_HOSTNAME;

	/*
	 * Get protocol specified in options list, if any.
	 */
	if ((str_opt(&m, "proto", &nfs_proto)) == -1)
		return (NFSERR_NOENT);

	/*
	 * Get port specified in options list, if any.
	 */
	nfs_port = nopt(&m, MNTOPT_PORT);
	if (nfs_port > USHRT_MAX) {
		syslog(LOG_ERR, "%s: invalid port number %d", host, nfs_port);
		return (NFSERR_NOENT);
	}

	args.addr = get_addr(host, NFS_PROGRAM, nfsvers, &nconf, nfs_proto,
			nfs_port);
	if (nfs_proto)
		free(nfs_proto);

	if (args.addr == NULL) {
		syslog(LOG_ERR, "%s: no NFS service", host);
		return (NFSERR_NOENT);
	}

	args.flags |= NFSMNT_KNCONF;
	args.knconf = get_knconf(nconf);
	if (args.knconf == NULL) {
		netbuf_free(args.addr);
		return (NFSERR_NOSPC);
	}

	if (hasmntopt(&m, MNTOPT_SOFT) != NULL) {
		args.flags |= NFSMNT_SOFT;
	}
	if (hasmntopt(&m, MNTOPT_NOINTR) != NULL) {
		args.flags &= ~(NFSMNT_INT);
	}
	if (hasmntopt(&m, MNTOPT_NOAC) != NULL) {
		args.flags |= NFSMNT_NOAC;
	}
	if (hasmntopt(&m, MNTOPT_NOCTO) != NULL) {
		args.flags |= NFSMNT_NOCTO;
	}
	if (hasmntopt(&m, MNTOPT_KERB) != NULL) {
		args.flags |= NFSMNT_KERBEROS;
	}
	if (hasmntopt(&m, MNTOPT_SECURE) != NULL) {
		args.flags |= NFSMNT_SECURE;
	}
	if ((args.flags & NFSMNT_SECURE) && (args.flags & NFSMNT_KERBEROS)) {
		syslog(LOG_ERR, "conflicting options: secure and kerberos\n");
		netbuf_free(args.addr);
		free_knconf(args.knconf);
		return (NFSERR_IO);
	}
	if ((args.flags & (NFSMNT_SECURE | NFSMNT_KERBEROS)) != 0) {
		if (args.flags & NFSMNT_KERBEROS) {
			(void) sprintf(netname, "nfs.%s", host);
		} else {
			/*
			 * NFSMNT_SECURE
			 * XXX: need to support other netnames
			 * outside domain and not always just use
			 * the default conversion.
			 */
			if (!host2netname(netname, host, NULL)) {
				netbuf_free(args.addr);
				free_knconf(args.knconf);
				/* really unknown host */
				return (NFSERR_NOENT);
			}
		}
		args.netname = netname;
		args.syncaddr = get_the_addr(host, RPCBPROG, RPCBVERS, nconf,
					nfs_port);
		if (args.syncaddr) {
			args.flags |= NFSMNT_RPCTIMESYNC;
		} else {
			/*
			 * If it's a UDP transport, then use the time service.
			 *
			 * The reason we only use UDP here is because in the
			 * kernel rtime() routine (rpc/authdesubr.c) will
			 * establish CLTS connection if the
			 * NFSMNT_RPCTIMESYNC flag is not set.
			 *
			 * This is not a total solution because
			 * mount will fail if the remote server does
			 * not support rpcbind-based time service
			 * over udp.
			 *
			 * This problem should be revisited in the
			 * future.
			 *
			 */
			if ((strcmp(nconf->nc_protofmly, NC_INET) == 0) &&
			    (strcmp(nconf->nc_proto, NC_UDP) == 0)) {
				struct nd_hostserv hs;

				hs.h_host = host;
				hs.h_serv = "rpcbind";
				if (netdir_getbyname(nconf, &hs, &retaddrs)
				    != ND_OK) {
					netbuf_free(args.addr);
					free_knconf(args.knconf);
					syslog(LOG_ERR,
					    "%s: no time service", host);
					return (NFSERR_IO);
				}
				args.syncaddr = retaddrs->n_addrs;
				((struct sockaddr_in *)
					/* LINTED pointer alignment */
					args.syncaddr->buf)->sin_port
						= IPPORT_TIMESERVER;
			} else {
				netbuf_free(args.addr);
				free_knconf(args.knconf);
				syslog(LOG_ERR,
					"%s: no time service", host);
				return (NFSERR_IO);
			}
		}
	} /* end of secure stuff */

	if (hasmntopt(&m, MNTOPT_GRPID) != NULL) {
		args.flags |= NFSMNT_GRPID;
	}
	if (args.rsize = nopt(&m, MNTOPT_RSIZE)) {
		args.flags |= NFSMNT_RSIZE;
	}
	if (args.wsize = nopt(&m, MNTOPT_WSIZE)) {
		args.flags |= NFSMNT_WSIZE;
	}
	if (args.timeo = nopt(&m, MNTOPT_TIMEO)) {
		args.flags |= NFSMNT_TIMEO;
	}
	if (args.retrans = nopt(&m, MNTOPT_RETRANS)) {
		args.flags |= NFSMNT_RETRANS;
	}
	if (args.acregmax = nopt(&m, MNTOPT_ACTIMEO)) {
		args.flags |= NFSMNT_ACREGMAX;
		args.flags |= NFSMNT_ACDIRMAX;
		args.flags |= NFSMNT_ACDIRMIN;
		args.flags |= NFSMNT_ACREGMIN;
		args.acdirmin = args.acregmin = args.acdirmax
			= args.acregmax;
	} else {
		if (args.acregmin = nopt(&m, MNTOPT_ACREGMIN)) {
			args.flags |= NFSMNT_ACREGMIN;
		}
		if (args.acregmax = nopt(&m, MNTOPT_ACREGMAX)) {
			args.flags |= NFSMNT_ACREGMAX;
		}
		if (args.acdirmin = nopt(&m, MNTOPT_ACDIRMIN)) {
			args.flags |= NFSMNT_ACDIRMIN;
		}
		if (args.acdirmax = nopt(&m, MNTOPT_ACDIRMAX)) {
			args.flags |= NFSMNT_ACDIRMAX;
		}
	}

	if (posix) {
		args.pathconf = get_pathconf(tsd->cl, dir, remname);
		if (args.pathconf == (struct pathcnf *) 0) {
			netbuf_free(args.addr);
			if (retaddrs)
				netdir_free(retaddrs, ND_ADDRLIST);
			else
				netbuf_free(args.syncaddr);
			free_knconf(args.knconf);
			return (NFSERR_IO);
		}
		args.flags |= NFSMNT_POSIX;
	}

	flags = 0;
	flags |= (hasmntopt(&m, MNTOPT_RO) == NULL) ? 0 : MS_RDONLY;
	flags |= (hasmntopt(&m, MNTOPT_NOSUID) == NULL) ? 0 : MS_NOSUID;
	flags |= overlay ? MS_OVERLAY : 0;
	if (mntpnt[strlen(mntpnt) - 1] != ' ')
		/* direct mount point without offsets */
		flags |= MS_OVERLAY;

	/* decide whether to use remote host's lockd or do local locking */
	if (newhost && nfsvers == NFS_VERSION) {
		if (remote_lock(host, args.fh)) {
			syslog(LOG_ERR, "No network locking on %s : "
				"contact admin to install server change",
				host);
			tsd->lockflag = NFSMNT_LLOCK;
		} else
			tsd->lockflag = 0;
	}
	args.flags |= tsd->lockflag;

	if (trace > 1) {
		trace_prt(1, "  mount %s %s (%s)\n", remname, mntpnt, opts);
	}
	if (mount("", mntpnt, flags | MS_DATA, fstype,
		    &args, sizeof (args)) < 0) {
		if (errno != EBUSY || verbose)
			syslog(LOG_ERR,
				"Mount of %s on %s: %m", remname, mntpnt);
		netbuf_free(args.addr);
		if (retaddrs)
			netdir_free(retaddrs, ND_ADDRLIST);
		else
			netbuf_free(args.syncaddr);
		free_knconf(args.knconf);
		return (NFSERR_IO);
	}

	if (trace > 1) {
		trace_prt(1, "  mount %s OK\n", remname);
	}

	m.mnt_special = remname;
	m.mnt_mountp = mntpnt;
	m.mnt_fstype = MNTTYPE_NFS;
	m.mnt_mntopts = opts;

	netbuf_free(args.addr);
	free_knconf(args.knconf);
	if (retaddrs)
		netdir_free(retaddrs, ND_ADDRLIST);
	else
		netbuf_free(args.syncaddr);
	if (add_mnttab(&m) == ENOENT)
		return (NFSERR_NOENT);

	return (NFS_OK);
}

static struct pathcnf *
get_pathconf(cl, path, fsname)
	CLIENT *cl;
	char *path, *fsname;
{
	static struct ppathcnf p;
	enum clnt_stat rpc_stat;
	struct timeval timeout;

	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	rpc_stat = clnt_call(cl, MOUNTPROC_PATHCONF,
	    xdr_dirpath, (caddr_t) &path, xdr_ppathcnf, (caddr_t) &p, timeout);
	if (rpc_stat != RPC_SUCCESS) {
			syslog(LOG_ERR,
				"pathconf: %s: server not responding: %s",
				fsname,
				clnt_sperror(cl, ""));
		return ((struct pathcnf *) 0);
	}
	if (_PC_ISSET(_PC_ERROR, p.pc_mask)) {
		syslog(LOG_ERR, "pathconf: no info for %s", fsname);
		return ((struct pathcnf *) 0);
	}
	return ((struct pathcnf *)&p);
}

static struct knetconfig *
get_knconf(nconf)
	struct netconfig *nconf;
{
	struct stat stbuf;
	struct knetconfig *k;

	if (stat(nconf->nc_device, &stbuf) < 0) {
		syslog(LOG_ERR, "get_knconf: stat %s: %m", nconf->nc_device);
		return (NULL);
	}
	k = (struct knetconfig *) malloc(sizeof (*k));
	if (k == NULL)
		goto nomem;
	k->knc_semantics = nconf->nc_semantics;
	k->knc_protofmly = strdup(nconf->nc_protofmly);
	if (k->knc_protofmly == NULL)
		goto nomem;
	k->knc_proto = strdup(nconf->nc_proto);
	if (k->knc_proto == NULL)
		goto nomem;
	k->knc_rdev = stbuf.st_rdev;

	return (k);

nomem:
	syslog(LOG_ERR, "get_knconf: no memory");
	free_knconf(k);
	return (NULL);
}

static void
free_knconf(k)
	struct knetconfig *k;
{
	if (k == NULL)
		return;
	if (k->knc_protofmly)
		free(k->knc_protofmly);
	if (k->knc_proto)
		free(k->knc_proto);
	free(k);
}

static void
netbuf_free(nb)
	struct netbuf *nb;
{
	if (nb == NULL)
		return;
	if (nb->buf)
		free(nb->buf);
	free(nb);
}

/*
 * Get the network address for the service identified by "prog"
 * and "vers" on "hostname" by using the nconf configuration data
 * passed in.
 */

static struct netbuf *
get_the_addr(hostname, prog, vers, nconf, port)
	char *hostname;
	int prog, vers, port;
	struct netconfig *nconf;
{
	struct netbuf *nb = NULL;
	struct t_bind *tbind = NULL;
	int fd = -1;
	enum clnt_stat cs;
	CLIENT *cl = NULL;
	struct timeval tv;

	if (nconf == NULL) {
		return (NULL);
	}

	if ((fd = t_open(nconf->nc_device, O_RDWR, NULL)) < 0) {
		goto done;
	}

	/* LINTED pointer alignment */
	if ((tbind = (struct t_bind *) t_alloc(fd, T_BIND, T_ADDR))
		== NULL) {
		goto done;
	}

	if (rpcb_getaddr(prog, vers, nconf, &tbind->addr, hostname) == 0) {
		goto done;
	}

	if (port) {
		((struct sockaddr_in *) tbind->addr.buf)->sin_port = port;
		cl = clnt_tli_create(fd, nconf, &tbind->addr, prog, vers, 0, 0);
		if (cl == NULL)
			goto done;

		tv.tv_sec = 10;
		tv.tv_usec = 0;
		cs = clnt_call(cl, NULLPROC, xdr_void, 0, xdr_void, 0, tv);
		clnt_destroy(cl);
		if (cs != RPC_SUCCESS)
			goto done;
	}

	/*
	 * Make a copy of the netbuf to return
	 */
	nb = (struct netbuf *) malloc(sizeof (struct netbuf));
	if (nb == NULL) {
		syslog(LOG_ERR, "no memory");
		goto done;
	}
	*nb = tbind->addr;
	nb->buf = (char *)malloc(nb->len);
	if (nb->buf == NULL) {
		syslog(LOG_ERR, "no memory");
		free(nb);
		nb = NULL;
		goto done;
	}
	(void) memcpy(nb->buf, tbind->addr.buf, tbind->addr.len);

done:
	if (tbind) {
		t_free((char *) tbind, T_BIND);
		tbind = NULL;
	}
	if (fd >= 0)
		(void) t_close(fd);
	return (nb);
}
/*
 * Get the network address for the service identified by "prog"
 * and "vers" on "hostname".  The netconfig address is returned
 * in the value of "nconfp".
 *
 * If the address of a netconfig pointer was passed and
 * if it's not null use it as the netconfig, otherwise
 * assign the address of the netconfig that was used to
 * establish contact with the service.
 */

static struct netbuf *
get_addr(hostname, prog, vers, nconfp, nfs_proto, port)
	char *hostname;
	int prog, vers, port;
	struct netconfig **nconfp;
	char *nfs_proto;
{
	struct netbuf *nb = NULL;
	struct netconfig *nconf = NULL;
	NCONF_HANDLE *nc = NULL;
	int i;

	i = 1;

	if (nconfp && *nconfp)
		nconf = *nconfp;
	else {

	/*
	 * First choice is COTS, second is CLTS.  When we retry,
	 * we reset the netconfig again, so that we search the
	 * whole list for our second choice, not just the part
	 * after the first choice.
	 */
startover:
		nc = setnetpath();
		if (nc == NULL)
			goto done;

retry:
		if (nfs_proto) {
		    while (nconf = getnetpath(nc)) {
			if (strcmp(nconf->nc_netid, nfs_proto) == 0) {
			    if ((strcmp(nconf->nc_protofmly, NC_INET) == 0) &&
				((strcmp(nconf->nc_proto, NC_TCP) == 0) ||
				(strcmp(nconf->nc_proto, NC_UDP) == 0)))
					break;
			    else {
				nconf = NULL;
				break;
			    }
			}
		    }
		} else {
		    while (nconf = getnetpath(nc)) {
			if (nconf->nc_flag & NC_VISIBLE) {
			    if (i == 1) {
				if ((nconf->nc_semantics == NC_TPI_COTS_ORD) ||
				(nconf->nc_semantics == NC_TPI_COTS)) {
				    if ((strcmp(nconf->nc_protofmly,
					NC_INET) == 0) &&
					(strcmp(nconf->nc_proto, NC_TCP) == 0))
					break;
				}
			    }
			    if (i == 2) {
				if (nconf->nc_semantics == NC_TPI_CLTS) {
				    if (port == 0)
					break;
				    if ((strcmp(nconf->nc_protofmly,
					NC_INET) == 0) &&
					(strcmp(nconf->nc_proto, NC_UDP)
					== 0))
					break;
				}
			    }
			}
		    } /* while */
		} /* else */
		if (nconf == NULL) {
			if (nfs_proto)
				goto done;
			if (++i <= MNT_PREF_LISTLEN) {
				endnetpath(nc);
				nc = NULL;
				goto startover;
			} else
				goto done;
		}
	} /* else */

	/*
	 * Get the network address based on the given prog/vers/port/nconf.
	 */
	if ((nb = get_the_addr(hostname, prog, vers, nconf, port)) == NULL) {
		if (nfs_proto)
		    goto done;
		else
		    goto retry;
	}

	/*
	 * Get the netconfig data
	 */
	if (nconfp && *nconfp == NULL) {
		*nconfp = nconf;
	}

done:
	if (nc)
		endnetpath(nc);

	if (nfs_proto && nb == NULL)
		syslog(LOG_ERR, "%s: %s protocol not available.\n",
		    hostname, nfs_proto);

	return (nb);
}

/*
 * Sends a null call to the remote host's (NFS program, versp). versp
 * may be "NULL" in which case NFS_V3 is used.
 * Upon return, versp contains the maximum version supported iff versp!= NULL.
 */
enum clnt_stat
pingnfs(hostname, attempts, versp)
	char *hostname;
	int attempts;
	u_long *versp;
{
	CLIENT *cl = NULL;
	struct timeval rpc_to_old, rpc_rtrans_old;
	struct timeval rpc_to_new = {15, 0};
	struct timeval rpc_rtrans_new = {15, 0};
	enum clnt_stat clnt_stat;
	int i, j;
	u_long versmax;
	static mutex_t pingnfs_lock = DEFAULTMUTEX;
	static char goodhost[MXHOSTNAMELEN+1];
	static char deadhost[MXHOSTNAMELEN+1];
	static time_t goodtime, deadtime;
	static u_long reqvers;		/* last version requested */
	static u_long outvers;		/* version supported by host */
					/* on last call */
	int cache_time = 30;  /* sec */

	mutex_lock(&pingnfs_lock);
	/*
	 * Check the "deadhost" cache if we don't care about the version,
	 * or if this call has the same version as either the previous
	 * request or its result.
	 */
	if (versp == NULL ||
	    (versp != NULL && *versp == reqvers) ||
	    (versp != NULL && *versp == outvers)) {
		if (strcmp(hostname, goodhost) == 0) {
			if (goodtime > time_now) {
				if (versp != NULL)
					*versp = outvers;
				mutex_unlock(&pingnfs_lock);
				return (RPC_SUCCESS);
			}
		} else if (strcmp(hostname, deadhost) == 0) {
			if (deadtime > time_now) {
				mutex_unlock(&pingnfs_lock);
				return (RPC_TIMEDOUT);
			}
		}
	}
	mutex_unlock(&pingnfs_lock);

	/*
	 * XXX Manipulate the total timeout to get the number of
	 * desired retransmissions. This code is heavily dependant on
	 * the RPC backoff mechanism in clnt_dg_call (clnt_dg.c).
	 */

	for (i = 0, j = rpc_rtrans_new.tv_sec; i < attempts-1; i++) {
		if (j < RPC_MAX_BACKOFF) {
			j *= 2;
		}
		rpc_to_new.tv_sec += (long)j;
	}

	if (trace > 1)
		trace_prt(1,
			"  ping: %s timeout=%ld ",
			hostname, rpc_to_new.tv_sec);

	if (versp != NULL)
		versmax = *versp;
	else
		versmax = NFS_V3;

	__rpc_control(CLCR_GET_RPCB_TIMEOUT, (char *) &rpc_to_old);
	__rpc_control(CLCR_SET_RPCB_TIMEOUT, (char *) &rpc_to_new);
	__rpc_control(CLCR_GET_RPCB_RMTTIME, (char *) &rpc_rtrans_old);
	__rpc_control(CLCR_SET_RPCB_RMTTIME, (char *) &rpc_rtrans_new);

	cl = clnt_create_vers(hostname, NFS_PROGRAM, &outvers,
			NFS_VERSMIN, versmax, "datagram_v");

	__rpc_control(CLCR_SET_RPCB_TIMEOUT, (char *) &rpc_to_old);
	__rpc_control(CLCR_SET_RPCB_RMTTIME, (char *) &rpc_rtrans_old);

	if (cl == NULL) {
		if (verbose)
			syslog(LOG_ERR, "pingnfs: %s%s",
				hostname, clnt_spcreateerror(""));
		clnt_stat = RPC_TIMEDOUT;
	} else {
		clnt_destroy(cl);
		clnt_stat = RPC_SUCCESS;
	}

	mutex_lock(&pingnfs_lock);
	if (clnt_stat == RPC_SUCCESS) {
		(void) strcpy(goodhost, hostname);
		goodtime = time((time_t *)NULL) + cache_time;
		if (versp != NULL)
			*versp = outvers;
	} else {
		(void) strcpy(deadhost, hostname);
		deadtime = time((time_t *)NULL) + cache_time;
		/*
		 * Since clnt_create_vers failed, we want to remember for
		 * which version the "deadhost" cache applies.
		 * When successful, outvers already contains the version.
		 */
		outvers = versmax;
	}
	/* remember requested vesion for next call's cache validation */
	reqvers = versmax;
	mutex_unlock(&pingnfs_lock);

	if (trace > 1)
		trace_prt(0, "vers=%d %s\n", outvers,
			clnt_stat == RPC_SUCCESS ?
			"OK" : "NO RESPONSE");

	return (clnt_stat);
}

#define	RET_ERR		33
#define	MNTTYPE_LOFS    "lofs"

int
loopbackmount(fsname, dir, mntopts, overlay)
	char *fsname; 		/* Directory being mounted */
	char *dir;		/* Directory being mounted on */
	char *mntopts;
	int overlay;
{
	struct mnttab mnt;
	int fs_ind;
	int flags = 0;
	char fstype[] = MNTTYPE_LOFS;
	int dirlen;

	dirlen = strlen(dir);
	if (dir[dirlen-1] == ' ')
		dirlen--;

	if (dirlen == strlen(fsname) &&
		strncmp(fsname, dir, dirlen) == 0) {
		syslog(LOG_ERR,
			"Mount of %s on %s would result in deadlock, aborted\n",
			fsname, dir);
		return (RET_ERR);
	}
	mnt.mnt_mntopts = mntopts;
	if (hasmntopt(&mnt, MNTOPT_RO) != NULL)
		flags |= MS_RDONLY;

	if (overlay)
		flags |= MS_OVERLAY;

	if ((fs_ind = sysfs(GETFSIND, MNTTYPE_LOFS)) < 0) {
		syslog(LOG_ERR, "Mount of %s on %s: %m", fsname, dir);
		return (RET_ERR);
	}
	if (trace > 1)
		trace_prt(1,
			"  loopbackmount: fsname=%s, dir=%s, flags=%d\n",
			fsname, dir, flags);

	if (mount(fsname, dir, flags | MS_FSS, fs_ind, 0, 0) < 0) {
		syslog(LOG_ERR, "Mount of %s on %s: %m", fsname, dir);
		return (RET_ERR);
	}
	if (trace > 1)
		trace_prt(1,
			"  loopbackmount of %s on %s OK\n",
			fsname, dir);

	mnt.mnt_special = fsname;
	mnt.mnt_mountp  = dir;
	mnt.mnt_fstype  = fstype;
	mnt.mnt_mntopts = (flags & MS_RDONLY) ? MNTOPT_RO : MNTOPT_RW;
	(void) add_mnttab(&mnt);

	return (0);
}

/*
 * Return the value of a numeric option of the form foo=x, if
 * option is not found or is malformed, return 0.
 */
static int
nopt(mnt, opt)
	struct mnttab *mnt;
	char *opt;
{
	int val = 0;
	char *equal;
	char *str;

	if (str = hasmntopt(mnt, opt)) {
		if (equal = strchr(str, '=')) {
			val = atoi(&equal[1]);
		} else {
			syslog(LOG_ERR, "Bad numeric option '%s'", str);
		}
	}
	return (val);
}

static void
freeex_ent(ex)
	struct exportnode *ex;
{
	struct groupnode *groups, *tmpgroups;

	free(ex->ex_dir);
	groups = ex->ex_groups;
	while (groups) {
		free(groups->gr_name);
		tmpgroups = groups->gr_next;
		free((char *)groups);
		groups = tmpgroups;
	}
	free((char *)ex);
}

static void
freeex(ex)
	struct exportnode *ex;
{
	struct exportnode *tmpex;

	while (ex) {
		tmpex = ex->ex_next;
		freeex_ent(ex);
		ex = tmpex;
	}
}

nfsunmount(mnt)
	struct mnttab *mnt;
{
	struct timeval timeout;
	CLIENT *cl;
	enum clnt_stat rpc_stat;
	char *server, *path, *p;


	if (umount(mnt->mnt_mountp) < 0) {
		/*
		 * EINVAL means that it is not a mount point
		 * ENOENT and EINVAL can happen if on a previous
		 * attempt the daemon successfully did its job
		 * but timed out. So this time around it should
		 * return all ok
		 */
		if (errno)
			return (errno);
	}

	/*
	 * The rest of this code is advisory to the server.
	 * If it fails return success anyway.
	 */

	server = mnt->mnt_special;
	p = strchr(server, ':');
	if (p == NULL)
		return (0);
	*p = '\0';
	path = p + 1;
	if (*path != '/')
		goto done;

	cl = clnt_create(server, MOUNTPROG, MOUNTVERS, "datagram_v");
	if (cl == NULL) {
		goto done;
	}
	if (__clnt_bindresvport(cl) < 0) {
		if (verbose)
			syslog(LOG_ERR, "umount %s:%s: %s",
				server, path,
				"Couldn't bind to reserved port");
		clnt_destroy(cl);
		goto done;
	}
	cl->cl_auth = authsys_create_default();
	timeout.tv_usec = 0;
	timeout.tv_sec = 5;
	rpc_stat = clnt_call(cl, MOUNTPROC_UMNT, xdr_dirpath, (caddr_t)&path,
	    xdr_void, (char *)NULL, timeout);
	AUTH_DESTROY(cl->cl_auth);
	clnt_destroy(cl);
	if (verbose && rpc_stat != RPC_SUCCESS)
		syslog(LOG_ERR, "%s: %s",
			server, clnt_sperror(cl, "unmount"));

done:
	*p = ':';
	return (0);
}
