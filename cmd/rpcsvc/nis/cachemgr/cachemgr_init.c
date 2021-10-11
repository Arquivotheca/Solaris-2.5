/*
 *	cachemgr_init.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)cachemgr_init.c	1.11	95/02/21 SMI"

/*
 * This file defines the main() routine for the cachemgr.
 * it registers the rpc dispatch function with the rpc runtime library
 * and rpcbind.
 * The code is different for TD and TI rpc.
 * It initializes the cache and then goes into svc_run().
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <rpc/rpc.h>
#include <rpc/key_prot.h>
#include <sys/signal.h>
#include <sys/stat.h>

#ifdef TDRPC

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/ttycom.h> /* TIOCNOTTY */

#else

#include <netconfig.h>
#include "tiuser.h"
#include <netdir.h>
#include <sys/resource.h> /* rlimit */

#endif /* TDRPC */

#include <rpc/pmap_clnt.h>
#include <stropts.h>
#include <syslog.h>
#include <rpcsvc/nis.h>
#include <rpcsvc/nis_cache.h>


extern int __nis_getpagesize();
extern void cacheprog_1(...);
extern void initCacheMgrCache(bool_t initialize, int init_size, int max_size,
				    char *uaddr);
extern void MgrCache_statistics(void);
extern void cache_cleanup_onsig();
extern void cache_purge();
extern void mgr_cache_dumpstats();
extern void update_cold_start_entry();
extern void hold_signals();
extern void release_signals();

static void hangup_handler(int sig);
static void cache_purge_handler(int sig);
static void cache_cleanup_handler(int sig);
static void init_signals();
static void parseArgs();
static void usage();
static void check_securemode_setup();
static int check_is_keylogin();


bool_t mgrVerbose = FALSE;
/*
 * flag to indicate if the cachemgr should add directory objects which
 * are not signed properly.
 * If the '-n' (insecure) flag is set then it insecureMode = TRUE
 */
bool_t insecureMode = FALSE;


/*
 * shold the cachefile be initialized on start up? i.e. the cold start entry
 * would be the only one in the cachefile.
 * if this is FALSE then the previous cachefile persists across the restart.
 */
static int initCache = FALSE;
static int init_size;
static int max_size;


#ifdef TDRPC
static int orig_mask;
static char *create_td_server();
#else
static char *create_ti_server();
#endif

/*
 * Note: Have to make this work with a port monitor like inetd.
 * get code generated from rpcgen in 5.0
 */

main(argc, argv)
int argc;
char *argv[];
{
	char 	*uaddr;


	openlog("nis_cachemgr", LOG_CONS, LOG_DAEMON);

	parseArgs(argc, argv);


#ifdef TDRPC
	uaddr =  create_td_server();
#else
	uaddr =  create_ti_server();
#endif

	init_signals();
	hold_signals();
	initCacheMgrCache(initCache, init_size, max_size, uaddr);
	release_signals();
	check_securemode_setup();
	svc_run();
	syslog(LOG_ERR, "svc_run() returned");
	exit(1);
}



#ifndef TDRPC

/*
 * create transport endpoint when running under TI RPC in 5.0.
 * this code currently does not take work if this process is started by a
 * portmonitor like listen or inetd. That should probably be added later.
 */

static char *
create_ti_server()
{

	SVCXPRT *transp;
	struct netconfig *nconf;
	char *uaddr;
	int pid, i;
	void *nc_handle;	/* Net config handle */
	bool_t found_loopback = FALSE;
	char mname[FMNAMESZ + 1];
	int fd = 0;
	int size = 3;

	if (!__nis_debuglevel) {
		pid = fork();
		if (pid < 0) {
			perror("cannot fork");
			exit(1);
		}
		if (pid)
			exit(0);
		for (i = 0; i < size; i++)
			(void) close(i);
		i = open("/dev/console", 2);
		(void) dup2(i, 1);
		(void) dup2(i, 2);
		setsid();
	}

	/*
	 * we want to get a loopback transport to have clients talk to the
	 * cachemgr
	 * this provides security in the sense that only local folk can access
	 * the cachemgr. Also, it makes the messages inherently more reliable.
	 * we also want a CLTS transport so search in the netconfig database
	 * for this type of transport.
	 * This is an implicit protocol between the cachemgr and the clients
	 * and the clients make the same selection of trasport (in
	 * cache_getclnt.c) and use the uaddr that the cachemgr writes into
	 * the cache file. This uaddr would be valid only for this transport
	 * that both agree on implicitly.
	 * If the selection scheme is changed here it should also be changed
	 * in the client side (cache_getclnt.c)
	 */

	nc_handle = setnetconfig();
	if (nc_handle != (void *) NULL) {
		while (nconf = getnetconfig(nc_handle))
			if ((strcmp(nconf->nc_protofmly, NC_LOOPBACK) == 0) &&
		    (nconf->nc_semantics == NC_TPI_CLTS)) {
			found_loopback = TRUE;
			break;
		}
	}
	if (!found_loopback) {
		syslog(LOG_ERR,
		"Could not get loopback transport from netconfig database");
		exit(1);

	}
	/* create a new transport endpoint */
	transp = svc_tp_create(
			(void (*)(struct svc_req *, SVCXPRT *))cacheprog_1,
			CACHEPROG, CACHE_VER_1, nconf);
	if (!transp) {
		syslog(LOG_ERR,
	"create_ti_server: cannot create server handle on loopback transport");
		exit(1);
	}
	uaddr = taddr2uaddr(nconf, &transp->xp_ltaddr);
	endnetconfig(nc_handle);
	return (uaddr);
}

#else /* TDRPC */

static char *
create_td_server()
{
	SVCXPRT 	*transp;
	static 		char uaddr[256];
	u_short 	udpport;
	int 		_rpcfdtype;	/* Whether Stream or Datagram ? */
	int 		pid, i;
	static 		struct sockaddr_in addr;
	int 		sock;
	struct 		sockaddr_in saddr;
	int 		asize = sizeof (saddr);
	int len 	= sizeof (struct sockaddr_in);
	u_short		 mask = 0x00ff;


	sock = RPC_ANYSOCK;
	if (!__nis_debuglevel) {
		pid = fork();
		if (pid < 0) {
			perror("cannot fork");
			exit(1);
		}
		if (pid)
			exit(0);
		for (i = 0; i < 20; i++)
			(void) close(i);
		i = open("/dev/console", 2);
		(void) dup2(i, 1);
		(void) dup2(i, 2);
		i = open("/dev/tty", 2);
		if (i >= 0) {
			(void) ioctl(i, TIOCNOTTY, (char *)NULL);
			(void) close(i);
		}
	}

	(void) pmap_unset(CACHEPROG, CACHE_VER_1);
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		syslog(LOG_ERR,
			"create_td_server: could not create socket: %m");
		exit(1);
	}
	memset((void*)&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	(void) bind(sock, (struct sockaddr *)&addr, len);
	if (getsockname(sock, (struct sockaddr *)&addr, &len) != 0) {
		syslog(LOG_ERR, "create_td_server:cannot getsockname: %m");
		exit(1);
	}
	transp = svcudp_create(sock);
	if (transp == NULL) {
		syslog(LOG_ERR,
			"create_td_server:cannot create udp service: %m");
		exit(1);
	}
	if (!svc_register(transp, CACHEPROG, CACHE_VER_1, cacheprog_1,
							IPPROTO_UDP)) {
		syslog(LOG_ERR,
			"unable to register (CACHEPROG, CACHE_VER_1, udp)");
		exit(1);
	}
	udpport = transp->xp_port;
	get_myaddress(&addr);
	sprintf(uaddr, "%s.%d.%d",
		inet_ntoa(addr.sin_addr), ((udpport >> 8) & mask),
							(udpport & mask));
	return (uaddr);
}

#endif




static void
parseArgs(argc, argv)
	int argc;
	char *argv[];
{
	extern char 	*optarg;	/* option argument string */
	int 		c;
	int 		pgsize = __nis_getpagesize();

	__nis_debuglevel = 0;
	mgrVerbose = FALSE;
	/* default initial size of cache file */
	init_size = 2 * pgsize;
	/* default max size of cache file */
	max_size =  8 * pgsize;

	insecureMode = TRUE; /* XXX workaround for 1090541 */

	/* get command line options */
	while ((c = getopt(argc, argv, "ivd:a:ns:m:")) != EOF) {
		switch (c) {

		case 'd':	/* DBG level */
			__nis_debuglevel = atoi(optarg);
			break;

		case 'i':		/* initialize cache */
			initCache = TRUE;
			break;

		case 'm':
			max_size = pgsize * atoi(optarg);
			break;

		case 'n':
			insecureMode = TRUE;
			break;

		case 's':
			init_size = pgsize * atoi(optarg);
			/* don't allow very small cache files */
			if (init_size < pgsize)
				init_size = 2 * pgsize;
		case 'v':
			mgrVerbose = TRUE;
			break;

		default:
		case '?':	/* error */
			usage(argv[0]);
			break;

		}
	}
	if (init_size > max_size)
		syslog(LOG_WARNING,
	"initial cachefile size > max size, setting maxsize to initsize");

}




static void
init_signals()
{
#ifdef TDRPC

	signal(SIGHUP, hangup_handler);
	signal(SIGALRM, cache_purge_handler);

	signal(SIGPIPE, SIG_IGN);

	signal(SIGTERM, cache_cleanup_handler);
	signal(SIGINT, cache_cleanup_handler);
	signal(SIGKILL, cache_cleanup_handler);
	signal(SIGIOT, cache_cleanup_handler);
	signal(SIGILL, cache_cleanup_handler);
	signal(SIGQUIT, cache_cleanup_handler);
	signal(SIGSYS, cache_cleanup_handler);
	signal(SIGFPE, cache_cleanup_handler);
#else

	sigset(SIGHUP, hangup_handler);
	sigset(SIGALRM, cache_purge_handler);

	sigset(SIGPIPE, SIG_IGN);

	sigset(SIGTERM, cache_cleanup_handler);
	sigset(SIGINT, cache_cleanup_handler);
	sigset(SIGKILL, cache_cleanup_handler);
	sigset(SIGIOT, cache_cleanup_handler);
	sigset(SIGILL, cache_cleanup_handler);
	sigset(SIGQUIT, cache_cleanup_handler);
	sigset(SIGSYS, cache_cleanup_handler);
	sigset(SIGFPE, cache_cleanup_handler);

#endif

}









static void
cache_purge_handler(int sig)
{
	if (mgrVerbose)
		syslog(LOG_INFO, "Purging CacheFile");
	cache_purge();
}




static void
hangup_handler(int sig)
{
	mgr_cache_dumpstats();
}






static void
cache_cleanup_handler(int sig)
{
	syslog(LOG_ERR, "terminating on signal (%d)", sig);
	/*
	 * function cache_cleanup_onsig is called on exit()
	 * as this is set up by atexit() in cachemgr.cc when intializing the
	 * cache
	 */
	exit(0);
}




/*
 * block some signals temporarily while we are updating the cache
 */

void
hold_signals()
{

#ifdef TDRPC
	/* 4.1 does not have sighold() and sigrelse() */
	int mask;

	mask = sigmask(SIGALRM)|sigmask(SIGTERM)|sigmask(SIGQUIT)
	mask |= sigmask(SIGINT)| sigmask(SIGHUP);
	orig_mask = sigblock(mask);
#else
	sighold(SIGALRM);
	sighold(SIGTERM);
	sighold(SIGQUIT);
	sighold(SIGINT);
	sighold(SIGHUP);
#endif

}



/*
 * release previously blocked signals and return to orignal signal mask.
 */

void
release_signals()
{

#ifdef TDRPC
	sigsetmask(orig_mask);
#else
	sigrelse(SIGALRM);
	sigrelse(SIGTERM);
	sigrelse(SIGQUIT);
	sigrelse(SIGINT);
	sigrelse(SIGHUP);
#endif

}


int
__nis_getpagesize()
{
#ifdef TDRPC
	extern int getpagesize();
	return (getpagesize());
#else
	return ((int)sysconf(_SC_PAGESIZE));

#endif
}



/*
 * Checks that the things recured to run in a secure mode are needed.
 * If some condition is not met it prints a warning message and defaults
 * to insecure mode.
 */

static void
check_securemode_setup()
{
	char 	netname[MAXNETNAMELEN + 1];


	if (insecureMode)
		return;

	/*
	 * running in secure mode
	 * check to see if 'root' on this machine has a publickey and has
	 * done a keylogin.
	 * if not, syslog a warning message and switch to insecure mode
	 * because if the  pubclickeys etc. are
	 * not set up and the cachemgr is running in secure mode
	 * then the signatures on the directory objects will not match
	 * and it will discard all directory objects. This would
	 * result in a decrease in performance because the local cache
	 * in individual processes will not be active.
	 *
	 * This is also affected by rpc.nisd having appropriate keys,
	 * and having logged in.
	 *
	 * Should hook into the ASET security levels stuff.
	 */

	if (check_is_keylogin() == 0) {
		syslog(LOG_ERR,
	"WARNING: 'root' has not done a 'keylogin', running in insecure mode");
		insecureMode = TRUE;
		return;
	}
}





/*
 * This is an attempt to figure out if root has done a keylogin.
 * This function will succeed if root has done an explicit keylogin
 * or if a "/etc/.rootkey" file exists.
 * returns 0 on Failure - i.e. root has not done a keylogin, 1 on success
 * Note:
 * The correct way of doing this would be to add a routine to keyserv which will
 * tell whether a particular principal has logged in or not.
 */

static int
check_is_keylogin()
{
	struct key_netstres 	kres;
	struct stat		buf;

	memset((void*) &kres, 0, sizeof (kres));

	/*
	 * If:
	 * Either the key_call fails - mean keyserv not running
	 * status != KEY_SUCESS - means key does not exist
	 * or key[0] = 0 - means that a keylogout has occurred
	 * we fail and return 0
	 * else return 1.
	 */
	if (key_call((u_long) KEY_NET_GET, xdr_void, (char *)NULL,
			xdr_key_netstres, (char *) &kres) &&
	    (kres.status == KEY_SUCCESS) &&
	    (kres.key_netstres_u.knet.st_priv_key[0] != 0)) {
		return (1);
	}
	/*
	 * check if /etc/.rootkey exists. Assume keylogin if
	 * it exists.
	 */
	if (stat("/etc/.rootkey", &buf) == 0)
		return (1);

	/* not done a keylogin, or publickey does not exist */
	return (0);
}




static void usage(name)
	char *name;
{
	printf("\tusage: %s\n", name);
	printf("\t\t-d  [debug_level]\n");
	printf("\t\t-i  <initialize cache when starting up>\n");
	printf("\t\t-m  [max size of cache file (in pages) ]\n");
	printf(
	"\t\t-n  <insecure mode - directory object signatures not checked>.\n");
	printf("\t\t-s  [initial size of cache file (in pages)]\n");
	printf("\t\t-v  <verbose - sends events to syslog>\n");
	exit(1);
}
