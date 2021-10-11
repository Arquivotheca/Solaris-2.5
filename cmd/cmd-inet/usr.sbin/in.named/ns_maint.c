/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ns_maint.c	1.8	95/04/21 SMI"	/* SVr4.0 1.4 */

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *			All rights reserved.
 *
 */


#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#if defined(SYSV)
#include <unistd.h>
#include <utime.h>
#endif SYSV
#include <netinet/in.h>
#include <stdio.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <arpa/nameser.h>
#include <sys/wait.h>
#include "ns.h"
#include "db.h"
#include "pathnames.h"

extern int errno;
extern int maint_interval;
extern int needzoneload;
extern u_short ns_port;
extern char *ctime();
extern void gettime();

int xfers_running;	/* number of xfers running */
int xfers_deferred;	/* number of needed xfers not run yet */
static int alarm_pending;

void sched_maint();
static void startxfer(), abortxfer();
#ifdef DEBUG
void printzoneinfo();
#endif

/*
 * Invoked at regular intervals by signal interrupt; refresh all secondary
 * zones from primary name server and remove old cache entries.  Also,
 * ifdef'd ALLOW_UPDATES, dump database if it has changed since last
 * dump/bootup.
 */
void
ns_maint()
{
	register struct zoneinfo *zp;
	int zonenum;
	extern void doachkpt();

	gettime(&tt);

#ifdef DEBUG
	if (debug)
		fprintf(ddt, "\nns_maint(); now %s", ctime(&tt.tv_sec));
#endif
	xfers_deferred = 0;
	alarm_pending = 0;
	for (zp = zones, zonenum = 0; zp < &zones[nzones]; zp++, zonenum++) {
#ifdef DEBUG
		if (debug >= 2)
			printzoneinfo(zonenum);
#endif
		if (tt.tv_sec >= zp->z_time && zp->z_refresh > 0) {
			/*
			 * Set default time for next action first,
			 * so that it can be changed later if necessary.
			 */
			zp->z_time = tt.tv_sec + zp->z_refresh;

			switch (zp->z_type) {
			case Z_CACHE:
				doachkpt();
				break;

			case Z_SECONDARY:
				if ((zp->z_state & Z_NEED_RELOAD) == 0) {
				    if (zp->z_state & Z_XFER_RUNNING)
					abortxfer(zp);
				    else if (xfers_running < MAX_XFERS_RUNNING)
					startxfer(zp);
				    else {
					zp->z_state |= Z_NEED_XFER;
					++xfers_deferred;
#ifdef DEBUG
					if (debug > 1)
					    fprintf(ddt,
						"xfer deferred for %s\n",
						zp->z_origin);
#endif
				    }
				}
				break;
#ifdef ALLOW_UPDATES
			case Z_PRIMARY:
				/*
				 * Checkpoint the zone if it has changed
				 * since we last checkpointed
				 */
				if (zp->hasChanged)
					zonedump(zp);
				break;
#endif ALLOW_UPDATES
			}
			gettime(&tt);
		}
	}
	sched_maint();
#ifdef DEBUG
	if (debug)
		fprintf(ddt, "exit ns_maint()\n");
#endif
}

/*
 * Find when the next refresh needs to be and set
 * interrupt time accordingly.
 */
void
sched_maint()
{
	register struct zoneinfo *zp;
	struct itimerval ival;
	time_t next_refresh = 0;
	static time_t next_alarm;

	for (zp = zones; zp < &zones[nzones]; zp++)
		if (zp->z_time != 0 &&
		    (next_refresh == 0 || next_refresh > zp->z_time))
			next_refresh = zp->z_time;
	/*
	 *  Schedule the next call to ns_maint.
	 *  Don't visit any sooner than maint_interval.
	 */
#ifdef SYSV
	memset((void *)&ival, 0, sizeof (ival));
#else
	bzero((char *)&ival, sizeof (ival));
#endif
	if (next_refresh != 0) {
		if (next_refresh == next_alarm && alarm_pending) {
#ifdef DEBUG
			if (debug)
			    fprintf(ddt, "sched_maint: no schedule change\n");
#endif
			return;
		}
		ival.it_value.tv_sec = next_refresh - tt.tv_sec;
		if (ival.it_value.tv_sec < maint_interval)
			ival.it_value.tv_sec = maint_interval;
		next_alarm = next_refresh;
		alarm_pending = 1;
	}
	(void) setitimer(ITIMER_REAL, &ival, (struct itimerval *)NULL);
#ifdef DEBUG
	if (debug)
		fprintf(ddt, "sched_maint: Next interrupt in %d sec\n",
			ival.it_value.tv_sec);
#endif
}

/*
 * Start an asynchronous zone transfer for a zone.
 * Depends on current time being in tt.
 * The caller must call sched_maint after startxfer.
 */
static void
startxfer(zp)
	struct zoneinfo *zp;
{
	static char *argv[NSMAX + 20], argv_ns[NSMAX][MAXDNAME];
#ifdef SYSV
	int cnt, argc = 0, argc_ns = 0, pid;
#else
	int cnt, argc = 0, argc_ns = 0, pid, omask;
#endif
	char debug_str[10];
	char serial_str[10];
	char port_str[10];

#ifdef DEBUG
	if (debug)
		fprintf(ddt, "startxfer() %s\n", zp->z_origin);
#endif

	argv[argc++] = "named-xfer";
	argv[argc++] = "-z";
	argv[argc++] = zp->z_origin;
	argv[argc++] = "-f";
	argv[argc++] = zp->z_source;
	argv[argc++] = "-s";
	sprintf(serial_str, "%d", zp->z_serial);
	argv[argc++] = serial_str;
	if (zp->z_state & Z_SYSLOGGED)
		argv[argc++] = "-q";
	argv[argc++] = "-P";
	sprintf(port_str, "%d", ns_port);
	argv[argc++] = port_str;
#ifdef DEBUG
	if (debug) {
		argv[argc++] = "-d";
		sprintf(debug_str, "%d", debug);
		argv[argc++] = debug_str;
		argv[argc++] = "-l";
		argv[argc++] = "/usr/tmp/xfer.ddt";
		if (debug > 5) {
			argv[argc++] = "-t";
			argv[argc++] = "/usr/tmp/xfer.trace";
		}
	}
#endif

	/*
	 * Copy the server ip addresses into argv, after converting
	 * to ascii and saving the static inet_ntoa result
	 */
	for (cnt = 0; cnt < zp->z_addrcnt; cnt++)
		argv[argc++] = strcpy(argv_ns[argc_ns++],
		    inet_ntoa(zp->z_addr[cnt]));

	argv[argc] = 0;

#ifdef DEBUG
#ifdef ECHOARGS
	if (debug) {
		int i;
		for (i = 0; i < argc; i++)
			fprintf(ddt, "Arg %d=%s\n", i, argv[i]);
	}
#endif /* ECHOARGS */
#endif /* DEBUG */

#ifdef SYSV
#define	vfork fork
#else
	gettime(&tt);
	omask = sigblock(sigmask(SIGCHLD));
#endif
	if ((pid = vfork()) == -1) {
#ifdef DEBUG
		if (debug)
			fprintf(ddt, "xfer [v]fork: %d\n", errno);
#endif
		syslog(LOG_ERR, "xfer [v]fork: %m");
#ifndef SYSV
		(void) sigsetmask(omask);
#endif
		zp->z_time = tt.tv_sec + 10;
		return;
	}

	if (pid) {
#ifdef DEBUG
		if (debug)
			fprintf(ddt, "started xfer child %d\n", pid);
#endif
		zp->z_state &= ~Z_NEED_XFER;
		zp->z_state |= Z_XFER_RUNNING;
		zp->z_xferpid = pid;
		xfers_running++;
		zp->z_time = tt.tv_sec + MAX_XFER_TIME;
#ifndef SYSV
		(void) sigsetmask(omask);
#endif
	} else {
		execve(_PATH_XFER, argv, NULL);
		syslog(LOG_ERR, "can't exec %s: %m", _PATH_XFER);
		_exit(XFER_FAIL);	/* avoid duplicate buffer flushes */
	}
}

#ifdef DEBUG
void
printzoneinfo(zonenum)
int zonenum;
{
	struct timeval  tt;
	struct zoneinfo *zp = &zones[zonenum];
	char *ZoneType;

	if (!debug)
		return; /* Else fprintf to ddt will bomb */
	fprintf(ddt, "printzoneinfo(%d):\n", zonenum);

	gettime(&tt);
	switch (zp->z_type) {
		case Z_PRIMARY: ZoneType = "Primary"; break;
		case Z_SECONDARY: ZoneType = "Secondary"; break;
		case Z_CACHE: ZoneType = "Cache"; break;
		default: ZoneType = "Unknown";
	}
	if (zp->z_origin[0] == '\0')
		fprintf(ddt, "origin ='.'");
	else
		fprintf(ddt, "origin ='%s'", zp->z_origin);
	fprintf(ddt, ", type = %s", ZoneType);
	fprintf(ddt, ", source = %s\n", zp->z_source);
	fprintf(ddt, "z_refresh = %ld", zp->z_refresh);
	fprintf(ddt, ", retry = %ld", zp->z_retry);
	fprintf(ddt, ", expire = %ld", zp->z_expire);
	fprintf(ddt, ", minimum = %ld", zp->z_minimum);
	fprintf(ddt, ", serial = %ld\n", zp->z_serial);
	fprintf(ddt, "z_time = %d", zp->z_time);
	if (zp->z_time) {
		fprintf(ddt, ", now time : %d sec", tt.tv_sec);
		fprintf(ddt, ", time left: %d sec", zp->z_time - tt.tv_sec);
	}
	fprintf(ddt, "; state %x\n", zp->z_state);
}
#endif DEBUG

/*
 * remove_zone (htp, zone) --
 *	Delete all RR's in the zone "zone" under specified hash table.
 */
void
remove_zone(htp, zone)
	register struct hashbuf *htp;
	register int zone;
{
	register struct databuf *dp, *pdp;
	register struct namebuf *np;
	struct namebuf **npp, **nppend;

	nppend = htp->h_tab + htp->h_size;
	for (npp = htp->h_tab; npp < nppend; npp++)
	    for (np = *npp; np != NULL; np = np->n_next) {
		for (pdp = NULL, dp = np->n_data; dp != NULL; /*EMPTY*/) {
			if (dp->d_zone == zone)
				dp = rm_datum(dp, np, pdp);
			else {
				pdp = dp;
				dp = dp->d_next;
			}
		}
		/* Call recursively to remove subdomains. */
		if (np->n_hash)
			remove_zone(np->n_hash, zone);
	    }
}

/*
 * Abort an xfer that has taken too long.
 */
static void
abortxfer(zp)
	register struct zoneinfo *zp;
{

	kill(zp->z_xferpid, SIGKILL); /* don't trust it at all */
#ifdef DEBUG
	if (debug)
		fprintf(ddt, "Killed child %d (zone %s) due to timeout\n",
						zp->z_xferpid, zp->z_origin);
#endif /* DEBUG */
	zp->z_time = tt.tv_sec + zp->z_retry;
}


/*
 * SIGCHLD signal handler: process exit of xfer's.
 * (Note: also called when outgoing transfer completes.)
 */
SIG_FN
endxfer(int sig)
{
	register struct zoneinfo *zp;
	int pid, xfers = 0;
	int exitstatus;
#if defined(SYSV)
	int status;
#else /* SYSV */
	union wait status;
#endif /* SYSV */

	gettime(&tt);
#if defined(SYSV)
	pid = wait(&status);
#else /* SYSV */
	while ((pid = wait3(&status, WNOHANG, (struct rusage *)NULL)) > 0) {
#endif /* SYSV */
		exitstatus = WIFEXITED(status) ? WEXITSTATUS(status) : 0;

		for (zp = zones; zp < &zones[nzones]; zp++)
		    if (zp->z_xferpid == pid) {
			xfers++;
			xfers_running--;
			zp->z_xferpid = 0;
			zp->z_state &= ~Z_XFER_RUNNING;
#ifdef DEBUG
			if (debug)
			    fprintf(ddt,
		"\nendxfer: child %d zone %s returned status=%d termsig=%d\n",
				pid, zp->z_origin, exitstatus,
				WIFSIGNALED(status) ? WTERMSIG(status) : -1);
#endif
			if (WIFSIGNALED(status)) {
				if (WTERMSIG(status) != SIGKILL) {
					syslog(LOG_ERR,
					"named-xfer exited with signal %d\n",
							WTERMSIG(status));
#ifdef DEBUG
					if (debug)
					    fprintf(ddt,
					"\tchild termination with signal %d\n",
						WTERMSIG(status));
#endif
				}
				zp->z_time = tt.tv_sec + zp->z_retry;
			} else switch (exitstatus) {
				case XFER_UPTODATE:
					zp->z_state &= ~Z_SYSLOGGED;
					zp->z_lastupdate = tt.tv_sec;
					zp->z_time = tt.tv_sec + zp->z_refresh;
					/*
					 * Restore z_auth in case expired,
					 * but only if there were no errors
					 * in the zone file.
					 */
					if ((zp->z_state & Z_DB_BAD) == 0)
						zp->z_auth = 1;
					if (zp->z_source) {
#if defined(SYSV)
						struct utimbuf t;

						t.actime = tt.tv_sec;
						t.modtime = tt.tv_sec;
						(void) utime(zp->z_source, &t);
#else
						struct timeval t[2];

						t[0] = tt;
						t[1] = tt;
						(void) utimes(zp->z_source, t);
#endif /* SYSV */
					}
					break;

				case XFER_SUCCESS:
					zp->z_state |= Z_NEED_RELOAD;
					zp->z_state &= ~Z_SYSLOGGED;
					needzoneload++;
					break;

				case XFER_TIMEOUT:
#ifdef DEBUG
					if (debug) fprintf(ddt,
		    "zoneref: Masters for secondary zone %s unreachable\n",
					    zp->z_origin);
#endif
					if ((zp->z_state & Z_SYSLOGGED) == 0) {
						zp->z_state |= Z_SYSLOGGED;
						syslog(LOG_WARNING,
			"zoneref: Masters for secondary zone %s unreachable",
						    zp->z_origin);
					}
					zp->z_time = tt.tv_sec + zp->z_retry;
					break;

				default:
					if ((zp->z_state & Z_SYSLOGGED) == 0) {
						zp->z_state |= Z_SYSLOGGED;
						syslog(LOG_ERR,
						    "named-xfer exit code %d",
						    exitstatus);
					}
					/* FALLTHROUGH */
				case XFER_FAIL:
					zp->z_state |= Z_SYSLOGGED;
					zp->z_time = tt.tv_sec + zp->z_retry;
					break;
			}
			break;
		}
#ifndef SYSV
	}
#endif /* SYSV */
	if (xfers) {
		for (zp = zones;
		    xfers_deferred != 0 && xfers_running < MAX_XFERS_RUNNING &&
		    zp < &zones[nzones]; zp++)
			if (zp->z_state & Z_NEED_XFER) {
				xfers_deferred--;
				startxfer(zp);
			}
		sched_maint();
	}
#if defined(SYSV)
	(void) signal(SIGCLD, endxfer);
#endif
}

/*
 * Reload zones whose transfers have completed.
 */
void
loadxfer()
{
	register struct zoneinfo *zp;

	gettime(&tt);
	for (zp = zones; zp < &zones[nzones]; zp++)
	    if (zp->z_state & Z_NEED_RELOAD) {
#ifdef DEBUG
		if (debug)
			fprintf(ddt, "loadxfer() '%s'\n",
			zp->z_origin[0] ? zp->z_origin : ".");
#endif
		zp->z_state &= ~Z_NEED_RELOAD;
		zp->z_auth = 0;
		remove_zone(hashtab, zp - zones);
		if (db_load(zp->z_source, zp->z_origin, zp, 0) == 0)
			zp->z_auth = 1;
		if (zp->z_state & Z_TMP_FILE)
			(void) unlink(zp->z_source);
	    }
	sched_maint();
}
