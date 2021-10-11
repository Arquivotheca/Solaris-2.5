/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)vold_main.c	1.60	95/07/10 SMI"

#include	<stdio.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<ctype.h>
#include	<syslog.h>
#include	<errno.h>
#include	<string.h>
#include	<rpc/rpc.h>
#include	<sys/param.h>
#include	<sys/types.h>
#include	<sys/wait.h>
#include	<sys/time.h>
#include	<sys/stat.h>
#include	<signal.h>
#include	<sys/signal.h>
#include	<rpcsvc/nfs_prot.h>
#include	<netinet/in.h>
#include	<sys/mnttab.h>
#include	<sys/mntent.h>
#include	<sys/mount.h>
#include	<sys/resource.h>
#include	<netdb.h>
#include	<sys/signal.h>
#include	<sys/file.h>
#include	<setjmp.h>
#include	<nfs/nfs_clnt.h>
#include	<netconfig.h>
#include	<netdir.h>
#include	<locale.h>
#include	<ulimit.h>
#include	<ucontext.h>
#include	<pwd.h>
#include	<grp.h>
#include	<setjmp.h>
#include	<stropts.h>
#include	<poll.h>
#include	<sys/systeminfo.h>

#include	"vold.h"
#include	"multithread.h"


/* extern vars */
extern int 	trace;		/* nfs server trace enable */


/* local prototypes */
static struct netconfig *trans_loopback(void);
static void		trans_netbuf(struct netconfig *, struct netbuf *);
static void		catch(void);
static void		catch_n_exit(void);
static void		reread_config(void);
static void		catch_n_return(int, siginfo_t *, ucontext_t *);
static void		usage(void);
static void		vold_run(void);


/* global vars */
int 		verbose 	= DEFAULT_VERBOSE;
int 		debug_level 	= DEFAULT_DEBUG;
char		*vold_root 	= DEFAULT_VOLD_ROOT;
char		*vold_config 	= DEFAULT_VOLD_CONFIG;
char		*vold_devdir	= DEFAULT_VOLD_DEVDIR;
char		*volume_group	= DEFAULT_VOLUME_GROUP;
char		*nisplus_group	= DEFAULT_NISPLUS_GROUP;
int		never_writeback = 0;
uid_t		default_uid;
gid_t		default_gid;
char		self[MAXHOSTNAMELEN];
struct timeval	current_time;
rlim_t		original_nofile;
#ifdef MT
int		vold_running = 0;
cond_t		running_cv;
mutex_t		running_mutex;
#endif /* MT */


/* local vars */
static int	vold_polltime = DEFAULT_POLLTIME;
static char	*prog_name;
#ifdef	DEBUG_MALLOC
static int	malloc_level;
#endif
static pid_t	mount_pid;
static int	mount_timeout 	= 30;
static int	reread_config_file = 0;
static int	do_main_poll = 1;

#define	MAXPOLLFD	5

mutex_t		polling_mutex;

bool_t		mount_complete = FALSE;

void
main(int argc, char **argv)
{
	extern void		nfs_program_2();
	extern bool_t		vol_init(void);
	extern int		config_read(void);
	extern int		mnt_add(char *, char *, char *, char *);
	extern int		vol_fd;
	SVCXPRT			*xprt;
	struct netconfig	*nconf;
	struct nfs_args		args;
	struct knetconfig	knconf;
	struct stat		sb;
	int			c;
	int			set_my_log = 0;
	struct passwd		*pw;
	struct group		*gr;
	struct sigaction	act;
	int			rpc_fd;
	struct t_bind		*tbind;
	struct rlimit		rlim;
	char			buf[BUFSIZ];


	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	prog_name = argv[0];



	/* argument processing */
	while ((c = getopt(argc, argv, "vtf:d:pl:L:m:g:no:G:P:")) != -1) {
		switch (c) {
		case 'v':
			verbose++;
			break;
		case 't':
			trace++;
			break;
		case 'f':
			vold_config = (char *)optarg;
			break;
		case 'd':
			vold_root = (char *)optarg;
			break;
		case 'o':
			vold_devdir = (char *)optarg;
			break;
		case 'g':
			volume_group = (char *)optarg;
			break;
		case 'G':
			nisplus_group = (char *)optarg;
			break;
		case 'l':
			set_my_log = 1;
			setlog((char *)optarg);
			break;
		case 'L':
			debug_level = atoi((char *)optarg);
			break;
#ifdef	DEBUG_MALLOC
		case 'm':
			malloc_level = atoi((char *)optarg);
			break;
#endif
		case 'n':
			never_writeback = 1;
			break;
		case 'P':
			vold_polltime = atoi((char *)optarg) * 1000;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}
	}

	if (set_my_log == 0)
		setlog(DEFAULT_VOLD_LOG);

#ifdef	FULL_DEBUG
	if (verbose == 0) {
		verbose++;
	}
	if (debug_level < 11) {
		debug_level = 11;
	}
	debug(5, "main: debug level %d (verbose = %d)\n",
	    debug_level, verbose);
#endif	/* FULL_DEBUG */

	/* for core dumps... not that we'd have any of those... */
	(void) chdir(vold_devdir);

	/* keep track of what time it is "now" (approx.) */
	(void) gettimeofday(&current_time, NULL);

	openlog(prog_name, LOG_PID | LOG_NOWAIT | LOG_CONS, LOG_DAEMON);
	(void) umask(0);
	(void) setbuf(stdout, (char *)NULL);
	(void) sysinfo(SI_HOSTNAME, self, sizeof (self));

	if (geteuid() != 0) {
		fatal(gettext("Must be root to execute vold\n"));
	}

	/*
	 * Increase file descriptor limit to the most it can possibly
	 * be.
	 */
	if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) {
		fatal("getrlimit; %m\n");
	}

	original_nofile = rlim.rlim_cur;
	rlim.rlim_cur = rlim.rlim_max;

	if (setrlimit(RLIMIT_NOFILE, &rlim) < 0) {
		fatal("setrlimit (fd's); %m\n");
	}

	gr = getgrnam(DEFAULT_GROUP);
	if (gr == NULL) {
		fatal(gettext("Must have the %s group defined\n"),
		    DEFAULT_GROUP);
	}
	default_gid = gr->gr_gid;

	pw = getpwnam(DEFAULT_USER);
	if (pw == NULL) {
		fatal(gettext("Must have the %s user defined\n"),
			DEFAULT_USER);
	}
	default_uid = pw->pw_uid;

#ifdef	DEBUG_MALLOC
	if (malloc_level > 0) {
		debug(5, "main: setting malloc debug level to %d\n",
		    malloc_level);
		malloc_debug(malloc_level);
	}
#endif
	/* initialize mutexes/cond-vars */
	(void) mutex_setup(&running_mutex);
	(void) cv_setup(&running_cv);

	/* initialize interface with vol driver */
	if (vol_init() == FALSE) {
		fatal(gettext("vol_init failed\n"));
		/*NOTREACHED*/
	}

	(void) config_read();			/* read in the config file */

	nconf = trans_loopback();
	if (nconf == (struct netconfig *) NULL) {
		fatal(gettext("no tpi_clts loopback transport available\n"));
		/*NOTREACHED*/
	}
	if ((rpc_fd = t_open(nconf->nc_device, O_RDWR,
	    (struct t_info *)NULL)) < 0) {
		fatal(gettext("unable to t_open %s\n"), nconf->nc_device);
		/*NOTREACHED*/
	}
	if ((tbind = (struct t_bind *)
	    /*LINTED: alignment ok*/
	    t_alloc(rpc_fd, T_BIND, T_ALL)) == NULL) {
		fatal(gettext("unable to t_alloc\n"));
		/*NOTREACHED*/
	}
	tbind->qlen = 1;
	trans_netbuf(nconf, &tbind->addr);
	xprt = svc_tli_create(rpc_fd, nconf, tbind, 0, 0);
	if (xprt == (SVCXPRT *) NULL) {
		fatal(
		    gettext("svc_tli_create: Cannot create server handle\n"));
		/*NOTREACHED*/
	}
	if (!svc_reg(xprt, NFS_PROGRAM, NFS_VERSION, nfs_program_2,
		(struct netconfig *)0)) {
		fatal(gettext("Could not register RPC service\n"));
		/*NOTREACHED*/
	}


	/*
	 *  Fork vold
	 *  For debugging, the sense of this is backwards -- here we fork
	 *  the mount half rather than the work half (so we can use dbx
	 *  easily).
	 */
	switch (mount_pid = fork()) {
	case -1:
		fatal(gettext("Cannot fork; %m\n"));
		/*NOTREACHED*/
	case 0:
		(void) memset(&args, 0, sizeof (args));
		(void) memset(&knconf, 0, sizeof (knconf));

		/* child */

		/*
		 * NFSMNT_NOAC flag needs to be turned off when NFS client
		 * side bugid 1110389 is fixed.
		 *
		 * NOTE: as of s494-ea, the NFSMNT_NOAC flag can NOT
		 *	be used, as it doesn't seem to be fully implemented.
		 *
		 * 10/14/94: symlinks seem to be hosed in 2.4 (NFS seems to
		 *	be caching READLINKs, so on goes NFSMNT_NOAC again
		 *	(see bug id# 1179769) -- also, 1110389 has long-since
		 *	been fixed.
		 */
		args.flags = NFSMNT_INT | NFSMNT_TIMEO | NFSMNT_RETRANS |
		    NFSMNT_HOSTNAME | NFSMNT_NOAC;
		args.addr = &xprt->xp_ltaddr;

		if (stat(nconf->nc_device, &sb) < 0) {
			fatal(gettext("Couldn't stat %s; %m\n"),
			    nconf->nc_device);
			/*NOTREACHED*/
		}
		knconf.knc_semantics = nconf->nc_semantics;
		knconf.knc_protofmly = nconf->nc_protofmly;
		knconf.knc_proto = nconf->nc_proto;
		knconf.knc_rdev = sb.st_rdev;
		args.flags |= NFSMNT_KNCONF;
		args.knconf = &knconf;

		args.timeo = (mount_timeout + 5) * 10;
		args.retrans = 5;
		args.hostname = strdup("for volume management (/vol)");
		args.netname = strdup("");
#ifdef notdef
		args.acregmin = 1;
		args.acregmax = 1;
		args.acdirmin = 1;
		args.acdirmax = 1;
#endif
		args.fh = (caddr_t)&root->vn_fh;

		/*
		 * Check to see mount point is there...
		 */
		if (stat(vold_root, &sb) < 0) {
			if (errno == ENOENT) {
				info(gettext("%s did not exist: creating\n"),
				    vold_root);
				if (makepath(vold_root, 0755) < 0) {
					fatal(gettext(
					    "can't make directory %s; %m\n"),
					    vold_root);
				}
			} else {
				fatal("%s; %m\n", vold_root);
				/*NOTREACHED*/
			}
		} else if (!(sb.st_mode & S_IFDIR)) {
			/* ...and that it's a directory. */
			fatal(gettext("%s is not a directory\n"), vold_root);
			/*NOTREACHED*/
		}

		/*
		 * Mount the daemon.
		 */
		if (mount("", vold_root, MS_DATA, MNTTYPE_NFS,
		    &args, sizeof (args)) < 0) {
			if (errno == EBUSY) {
				warning(gettext("vold restarted\n"));
			} else {
				warning(
				    gettext("Can't mount %s; %m\n"),
				    vold_root);
			}
		}
		/* it's not really mounted yet */
		(void) sprintf(buf, "%s:vold(pid%ld)", self, getppid());
		(void) mnt_add(buf, vold_root, MNTTYPE_NFS, MNTOPT_IGNORE);
		exit(0);
		/*NOTREACHED*/
	}

	/* parent */

	(void) setsid();

	/* set up our signal handlers */
	act.sa_handler = catch_n_return;
	(void) sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART|SA_SIGINFO;
	(void) sigaction(SIGCHLD, &act, NULL);

	act.sa_handler = catch;
	(void) sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART|SA_SIGINFO;
	(void) sigaction(SIGTERM, &act, NULL);

	act.sa_handler = reread_config;
	(void) sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART|SA_SIGINFO;
	(void) sigaction(SIGHUP, &act, NULL);

	act.sa_handler = catch;
	(void) sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART|SA_SIGINFO;
	(void) sigaction(SIGINT, &act, NULL);

	act.sa_handler = catch_n_exit;
	(void) sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART|SA_SIGINFO;
	(void) sigaction(SIGUSR1, &act, NULL);

#ifdef	DEBUG
	act.sa_handler = catch_n_exit;
	(void) sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART|SA_SIGINFO;
	(void) sigaction(SIGSEGV, &act, NULL);
#endif

	act.sa_handler = catch_n_return;
	(void) sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;	/* no restart!! */
	(void) sigaction(SIGUSR2, &act, NULL);

	/*
	 * tell vol driver about where our root is
	 */
	if (ioctl(vol_fd, VOLIOCDROOT, vold_root) != 0) {
		fatal(gettext("can't set vol root to \"%s\"; %m\n"),
		    vold_root);
		/*NOTREACHED*/
	}

	/* do the real work */
	vold_run();
	fatal(gettext("vold_run returned!\n"));
	/*NOTREACHED*/
}

/*
 * Get a netconfig entry for loopback transport
 */
static struct netconfig *
trans_loopback()
{
	struct netconfig	*nconf;

	NCONF_HANDLE		*nc;


	nc = setnetconfig();
	if (nc == NULL)
		return (NULL);

	while (nconf = getnetconfig(nc)) {
		if (nconf->nc_flag & NC_VISIBLE &&
		    nconf->nc_semantics == NC_TPI_CLTS &&
		    strcmp(nconf->nc_protofmly, NC_LOOPBACK) == 0) {
			nconf = getnetconfigent(nconf->nc_netid);
			break;
		}
	}

	endnetconfig(nc);
	return (nconf);
}

static void
trans_netbuf(struct netconfig *nconf, struct netbuf *np)
{
	struct nd_hostserv	nd_hostserv;
	struct nd_addrlist	*nas;

	nd_hostserv.h_host = self;
	nd_hostserv.h_serv = DEFAULT_SERVICE;

	if (!netdir_getbyname(nconf, &nd_hostserv, &nas)) {
		np->len = nas->n_addrs->len;
		(void) memcpy(np->buf, nas->n_addrs->buf,
		    (int)nas->n_addrs->len);
		netdir_free((char *)nas, ND_ADDRLIST);
	} else {
		fatal(gettext("No service found for %s on transport %s\n"),
			DEFAULT_SERVICE, nconf->nc_netid);
		/*NOTREACHED*/
	}

}


/*
 * main loop for the volume daemon.
 */

/*
 * egad... what a clever... well...
 * The problem is that it's impossible to write a fully MT program
 * at this time because several of the libraries that I depend on
 * (e.g. the rpc library) are not MT safe.  So, we suffer from a
 * very partial MT job.  The main significance here is that poll(2)
 * relies on SIGCHLD to kick us out of a system call.  It's much more
 * efficient (in terms of wall clock time) to do this than just
 * poll for some number of seconds.  The trouble is that the child can
 * end and it's signal be generated and caught before we make it back
 * to poll.  So, we have this flag that gets set to zero anytime
 * a sigcld is recieved.  It's set to one just before the reaping is
 * done.  The only window left :-( is between the if(do_main_poll) and
 * the entry into the system call.  It's a small number of instructions...
 * To cover this possibility, I have a 30 second timeout for the poll :-(.
 * Once in a blue moon, someone may run into a 30 second delay.
 * The trade-off is performance of the overall system (dumb daemon
 * wakes up all the time) vs waiting a bit every once in a rare
 * while.
 */

static void
vold_run()
{
	extern void	svc_getreq_common(const int);
	extern void	vol_readevents(void);
	extern void	vol_async(void);
	extern int	config_read(void);
	extern	int	vol_fd;
	static int	n;
	static int	i;
	static int	maxfds;
	static int	rpc_fd;
	static size_t	npollfd = 0;
	static struct	pollfd	poll_fds[MAXPOLLFD];


	maxfds = ulimit(UL_GDESLIM, 0);
	for (i = 0; i < maxfds; i++) {
		if (FD_ISSET(i, &svc_fdset)) {
			rpc_fd = i;
			break;
		}
	}
	info(gettext("vold: running\n"));

	/* let the threads GO */
	if (vold_running == 0) {
		(void) mutex_enter(&running_mutex);
		vold_running = 1;
		(void) cv_broadcast(&running_cv);
		(void) mutex_exit(&running_mutex);
	}

	poll_fds[npollfd].fd = vol_fd;
	poll_fds[npollfd].events = POLLRDNORM;
	npollfd++;
	poll_fds[npollfd].fd = rpc_fd;
	poll_fds[npollfd].events = POLLIN|POLLRDNORM|POLLRDBAND;
	npollfd++;

	/* init our polling mutex */
	(void) mutex_setup(&polling_mutex);

	/* handle events forever */
	for (;;) {

		/* wait until something happens */
		if (do_main_poll) {
#ifdef	DEBUG
			debug(12, "vold_run: about to poll()\n");
#endif
			n = poll(poll_fds, npollfd, vold_polltime);
#ifdef	DEBUG
			debug(12, "vold_run: poll() returned %d (errno %d)\n",
			    n, errno);
#endif
		}

		(void) mutex_enter(&polling_mutex);

		/* update idea of the "now" */
		(void) gettimeofday(&current_time, NULL);

		/*
		 * Is there work to do?
		 */
		if (n > 0) {

			/* there is work to do -- look at each possible fd */
			for (i = 0; n && i < npollfd; i++) {

				/* does this fd have a read event ? */
				if (poll_fds[i].revents) {

					if (poll_fds[i].fd == rpc_fd) {

						/* this is an NFS event */
						svc_getreq_common(rpc_fd);

					} else if (poll_fds[i].fd == vol_fd) {

						/* this is a volctl event */
						vol_readevents();

					}

				}
			}

		} else if (n < 0) {

			/* poll() had an error */

			if (errno == EINTR) {
				debug(10, "vold_run: poll interrupted\n");
			} else {
				debug(10,
				    "vold_run: poll failed (errno %d)\n",
				    errno);
			}
		}

		if (reread_config_file) {
			(void) config_read();	/* check for changes */
			reread_config_file = 0;
		}

		if (!mount_complete) {
			int	stat;	/* set but not used */

			if (waitpid(mount_pid, &stat, WNOHANG) == mount_pid) {
				mount_complete = TRUE;
			}
		}

		do_main_poll = 1;

		if (mount_complete) {
			/*
			 * don't want to process async tasks (such as
			 * media insertion) until the NFS server is ready
			 * to handle request
			 */
			vol_async(); 	/* do any async tasks */
		}

		(void) mutex_exit(&polling_mutex);
	}
	/*NOTREACHED*/
}

static void
usage()
{
	(void) fprintf(stderr,
	    gettext("usage: %s\n"), prog_name);
	(void) fprintf(stderr,
	    gettext("\t[-v]\t\tverbose status information\n"));
	(void) fprintf(stderr,
	    gettext("\t[-t]\t\tunfs server trace information\n"));
	(void) fprintf(stderr,
	    gettext("\t[-f pathname]\talternate vold.conf file\n"));
	(void) fprintf(stderr,
	    gettext("\t[-d directory]\talternate /vol directory\n"));
	(void) fprintf(stderr,
	    gettext("\t[-l logfile]\tplace to put log messages\n"));
	(void) fprintf(stderr,
	    gettext("\t[-L loglevel]\tlevel of debug information\n"));
#ifdef	DEBUG_MALLOC
	(void) fprintf(stderr,
	    gettext("\t[-m mlevel]\tlevel of malloc debug info\n"));
#endif
	(void) fprintf(stderr, "\n");
	exit(1);
}


void
catch()
{
	extern int	umount_all(char *);
	pid_t		parentpid = getpid();
	pid_t		fork_ret;
	int		err;



#ifdef MT
	fork_ret = fork1();
#else
	fork_ret = fork();
		return;
	}
#endif
	if (fork_ret != 0) {
		if (fork_ret < 0) {
			warning(gettext("Can't fork; %m\n"));
		}
#ifdef	DEBUG
		else {
			debug(1,
			    "catch(): pid %d created pid %d\n", getpid(),
			    fork_ret);
		}
#endif
		return;
	}

	/* in child now */

	if ((err = umount_all(vold_root)) != 0) {
		syslog(LOG_ERR,
		    gettext("problem unmounting %s; %m\n"), vold_root);
	} else {
		/* nail vold with a -9 */
#ifdef DEBUG
		(void) fprintf(stderr, "Killing pid %d\n", parentpid);
#endif
		(void) kill(parentpid, SIGKILL);
	}
	exit(err);
}


/*
 * Exit from the thread.
 */
void
catch_n_exit()
{
	extern void	flushlog(void);


	flushlog();
	if (thread_self() > 1) {
		debug(1, "thread %d exiting\n", thread_self());
		thread_exit();
	}
	warning(gettext("volume management exiting\n"));
	exit(0);
}

/*
 * don't do anything but set this flag...
 */
/*ARGSUSED*/
void
catch_n_return(int sig, siginfo_t *si, ucontext_t *uc)
{
	do_main_poll = 0;
}

/*
 * Signal to reread the configuration file
 */
void
reread_config()
{
	reread_config_file = 1;
}
