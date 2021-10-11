/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)init.c 1.0 91/02/10 SMI"

#ident	"@(#)init.c 1.20 94/08/10"

#include "operd.h"
#include "config.h"
#include <netdb.h>
#include <syslog.h>
#include <signal.h>
#include <time.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#include <rpc/pmap_clnt.h>

#define	OPERD_CONFIGFILE	"operd.conf"

static char configfile[MAXPATHLEN];	/* configuration file */
static int nflag;			/* don't do gateway mode */

int	dogateway;			/* gateway mode - forward login calls */
int	dobroadcast;			/* forward login calls via broadcast */
int	maxcache;			/* upper bound on cached messages */
char	**namelist;			/* names we go by */
char	**domainlist;			/* domains considered equivalent */
char	*tmpdir = TMPDIR;		/* for core, cache dumps */
#ifdef DEBUG
int	xflag;				/* turn on debugging */
#endif

#define	INTERVAL	60		/* expiration interval */

#ifdef __STDC__
static void	onintr(int);
static void	onhup(int);
static void	onusr1(int);
static void	config(char *);
static void	usage(void);
static int	initifc(void);
#else
static void	onintr();
static void	onhup();
static void	onusr1();
static void	config();
static void	usage();
static int	initifc();
#endif

/*ARGSUSED*/
static void
onintr(sig)
	int	sig;
{
	finish(0);
}

/*ARGSUSED*/
static void
onhup(sig)
	int	sig;
{
	config(configfile);
}

/*ARGSUSED*/
static void
onusr1(sig)
	int	sig;
{
	dump_all();
}

static void
usage(void)
{
	(void) fprintf(stderr,
	    gettext("usage: %s [ -c msgs ] [ -n ] [ config_file ]\n"),
		thisprogram);
	finish(-1);
}

#define	MAXIF	100

void
init(ac, av)
	int	ac;
	char	**av;
{
	struct sigvec sv;
	int	nifr;
	char	*cp;

	cp = strrchr(av[0], '/');
	if (cp != (char *)0)
		(void) strncpy(thisprogram, &cp[1], sizeof (thisprogram));
	else
		(void) strncpy(thisprogram, av[0], sizeof (thisprogram));
#ifdef USG
	(void) sigemptyset(&sv.sv_mask);
	sv.sv_flags = SA_RESTART;
#else
	sv.sv_mask = 0;
	sv.sv_flags = 0;
#endif
	sv.sv_handler = onintr;			/* terminate cleanly */
	(void) sigvec(SIGINT, &sv, (struct sigvec *)0);

	(void) sysinfo(SI_HOSTNAME, thishost, sizeof (thishost));
	(void) sysinfo(SI_SRPC_DOMAIN, thisdomain, sizeof (thisdomain));

	while (--ac && **++av == '-') {
		switch (*++*av) {
		case 'c':	/* max cache size */
			if (*++*av)
				maxcache = atoi(*av);
			else if (*++av) {
				--ac;
				maxcache = atoi(*av);
			} else
				usage();
			if (maxcache < 0) {
				(void) fprintf(stderr,
				    gettext("cache size must be positive\n"));
				finish(-1);
			}
			break;
		case 'n':	/* non-gateway mode */
			nflag = 1;
			break;
#ifdef DEBUG
		case 't':	/* tmp dir */
			if (*++*av)
				tmpdir = *av;
			else if (*++av) {
				--ac;
				tmpdir = *av;
			} else
				usage();
			break;
		case 'x':	/* debug mode */
			xflag = 1;
			break;
#endif
		default:
			usage();
		}
	}
	if (ac)
		(void) strncpy(configfile, *av, sizeof (configfile));
	else
		(void) sprintf(configfile, "%s/%s",
				gethsmpath(etcdir), OPERD_CONFIGFILE);

	sv.sv_handler = onhup;			/* re-read conf file */
	(void) sigvec(SIGHUP, &sv, (struct sigvec *)0);

	nifr = initifc();
	if (nifr < 1)
		finish(-1);
#ifdef DEBUG
	debug("%d non-loopback interfaces found\n", nifr);
#endif
	/*
	 * If this host has more than one network interface,
	 * forward login calls via broadcast.  This may be
	 * disabled later via the configuration file.
	 */
	if (!nflag) {
		if (nifr > 1) {
#ifdef DEBUG
			debug("in re-broadcast mode\n");
#endif
			dogateway++;
			dobroadcast++;
		}
	}

	sv.sv_handler = expire_all;
	(void) sigvec(SIGALRM, &sv, (struct sigvec *)0);
	itv.it_interval.tv_sec = itv.it_value.tv_sec = INTERVAL;
	itv.it_interval.tv_usec = itv.it_value.tv_usec = 0;
	if (setitimer(ITIMER_REAL, &itv, NULL) < 0) {
		perror("setitimer");
		finish(-1);
	}

	(void) openlog(thisprogram, LOG_PID|LOG_CONS, LOG_DAEMON);

	if (oper_init(NULL, thisprogram, 1) != OPERMSG_SUCCESS) {
		(void) fprintf(stderr,
		    gettext("%s: initialization failed\n"), thisprogram);
		finish(-1);
	}

	config(configfile);
	(void) chdir(tmpdir);

	init_msg();

	sv.sv_handler = onusr1;			/* dump cache */
	(void) sigvec(SIGUSR1, &sv, (struct sigvec *)0);

	advertise(OPER_LOGIN);
}

static void
config(file)
	char	*file;
{
	char line[256];
	char cmd[21];
	FILE *fp;
	int nlines = 0;
	int ndomains;

#ifdef DEBUG
	debug("config(%s)\n", file);
#endif
	/*
	 * We re-read the config file on SIGHUP,
	 * in which case we must free the equivalent
	 * domain list and rebuild it.
	 */
	if (domainlist != (char **)0) {
		while (*domainlist)
			free(*domainlist++);
		free(domainlist);
	}

	domainlist = (char **)malloc(sizeof (char *) * 2);
	if (domainlist == (char **)0) {
		perror("malloc");
		finish(-1);
	}
	domainlist[0] = thisdomain;
	domainlist[1] = (char *)0;
	ndomains = 1;

	fp = fopen(file, "r");
	if (fp == NULL) {
		if (errno != ENOENT) {
			perror(gettext("cannot open config file"));
			finish(-1);
		} else
			return;
	}
	while (fgets(line, sizeof (line), fp)) {
		++nlines;
		if (strcmp(line, "nobroadcast\n") == 0) {
#ifdef DEBUG
			debug("disabling re-broadcast mode\n");
#endif
			dobroadcast = 0;
			continue;
		} else {
			(void) sscanf(line, "%20s", cmd);
			if (strcmp(cmd, "forward") == 0) {
				make_fwd(line+strlen("forward"));
				continue;
			} else if (strcmp(cmd, "domain") == 0) {
				char *cp = line + strlen("domain");

				/* (ndomains + 2) for new thing plus NULL */
				domainlist = (char **) realloc(domainlist,
					sizeof (char *) * (ndomains + 2));
				if (domainlist == (char **)0) {
					perror("realloc");
					finish(-1);
				}
				domainlist[ndomains] = (char *)
					malloc(strlen(cp) + 1);
				if (domainlist[ndomains] == (char *)0) {
					perror("malloc");
					finish(-1);
				}
				(void) sscanf(cp, "%s", domainlist[ndomains]);
				++ndomains;
				domainlist[ndomains] = (char *)0;
			}
		}
		(void) fprintf(stderr,
		    gettext("unrecognized line (%d) in config file ignored\n"),
		    nlines);
	}
	(void) fclose(fp);
}

void
finish(status)
	int	status;
{
	advertise(OPER_LOGOUT);
	oper_end();
	(void) pmap_unset(OPERMSG_PROG, OPERMSG_VERS);
	exit(status);
}

#include <stdarg.h>

/* VARARGS2 */
/* ARGSUSED */
void
errormsg(int level, const char *fmt, ...)
{
	va_list args;
	char buf[1024];

#ifdef USG
	va_start(args, fmt);
#else
	va_start(args);
#endif
	(void) vsprintf(buf, fmt, args);
#ifdef DEBUG
	(void) fprintf(stderr, buf);
	(void) fflush(stderr);
#else
	(void) syslog(level, buf);
#endif
	va_end(args);
}

#ifdef DEBUG
/* VARARGS1 */
void
debug(const char *fmt, ...)
{
	va_list args;
	char buf[1024];
	static int lastnl = 1;

	if (!xflag)
		return;
	if (thisprogram[0] == '\0')
		strcpy(thisprogram, gettext("un-named"));
#ifdef USG
	va_start(args, fmt);
#else
	va_start(args);
#endif
	(void) vsprintf(buf, fmt, args);
	if (lastnl)
		(void) fprintf(stdout, "%s[%lu]: ",
			thisprogram, (u_long)getpid());
	if (strchr(buf, '\n'))
		lastnl = 1;
	else
		lastnl = 0;
	(void) fprintf(stdout, buf);
	(void) fflush(stdout);
	va_end(args);
}
#endif

/*
 * Determine the number of non-loopback interfaces
 * configured into the system and initialize "namelist"
 * with all the various names we go by.  Return the
 * number of non-loopback interfaces.
 *
 * Solaris 1.0/2.0 version.
 */
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>

static int
initifc(void)
{
	struct sockaddr_in *sin;
	struct ifconf ifc;
	struct ifreq *ifr;
	struct hostent *h;
	int	s, i, nifr;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		return (-1);
	}

	ifc.ifc_len = sizeof (struct ifreq) * MAXIF;
	ifc.ifc_buf = (caddr_t)malloc(ifc.ifc_len);
	/*LINTED [alignment ok]*/
	ifr = (struct ifreq *)ifc.ifc_buf;
	if (ifr == NULL) {
		perror("malloc");
		return (-1);
	}

	if (ioctl(s, SIOCGIFCONF, &ifc) < 0) {
		perror("ioctl(SIOCGIFCONF)");
		return (-1);
	}

	nifr = ifc.ifc_len/sizeof (struct ifreq);
	nifr++;
	namelist = (char **)malloc(nifr * sizeof (char *));
	if (namelist == NULL) {
		perror("malloc");
		return (-1);
	}
	(void) bzero((char *)namelist, nifr * sizeof (char *));

	for (i = 0, nifr = 0; i < ifc.ifc_len/sizeof (struct ifreq); i++) {
		char *cp;

		if (ioctl(s, SIOCGIFADDR, &ifr[i]) < 0) {
			perror("ioctl");
			continue;
		}
		if (strncmp(ifr[i].ifr_name, "lo", 2) == 0)
			continue;
		/*LINTED [alignment ok]*/
		sin = (struct sockaddr_in *)&ifr[i].ifr_addr;
		/* skip non-AF_INET family */
		if (ifr[i].ifr_addr.sa_family != AF_INET)
			continue;

		h = gethostbyaddr((char *)&sin->sin_addr,
		    sizeof (struct in_addr), AF_INET);
		if (h == NULL) {
			perror(gettext("Cannot get host name from address"));
			continue;
		}
		cp = strchr(h->h_name, '.');
		if (cp)
			*cp = '\0';	/* strip domain */
		namelist[nifr] = malloc(strlen(h->h_name)+1);
		if (namelist[nifr] == NULL) {
			perror("malloc");
			return (-1);
		}
		(void) strcpy(namelist[nifr], h->h_name);
#ifdef DEBUG
		debug("%s: %s\n", ifr[i].ifr_name, namelist[nifr]);
#endif
		nifr++;
	}
	free(ifr);
	(void) close(s);
	return (nifr);
}
