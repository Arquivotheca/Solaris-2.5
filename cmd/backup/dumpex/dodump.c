#ident "@(#)dodump.c 1.72 93/10/15"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include "dumpex.h"
#include "tapelib.h"
#include <config.h>
#include <lfile.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>
#include <errno.h>

#ifdef __STDC__
static void onedump(char *, char *, int *, int, int);
static void setupdevs(char *);
static int setupvers(char *);
static int setupdump(char *, char *);
static void host_down(char *, char *, char *);
static char *get_dumpdates(void);
#else
static void onedump();
static void setupdevs();
static int setupvers();
static int setupdump();
static void host_down();
static char *get_dumpdates();
#endif

extern int diesoon;
extern int outofband;
extern struct oob_mail *outofband_mailp;

static struct string_f *devstring;

static int	indexoflasttapeused;	/* for knowing which to rewind */
static char	*filesystodump;		/* which filesys we're dumping */

static int	tapeposofsecur;		/* security tape pos start */
static int	gooddumps;		/* number of successful dumps */

void
dodump(void)
{
	int	i, remote_seq;
	struct devcycle_f *d;
	struct string_f *dbdumplog;
	char	*p, *q;
	char	scratch[MAXLINELEN];
	char	tempfilename[MAXLINELEN];
	char	line[MAXLINELEN];
	char	line2[MAXLINELEN];
	int	zero;
	int	newtape;

	zero = 0;
#ifdef lint
	zero = zero;
#endif
	indexoflasttapeused = -1;
	/* setup name of the local and remote L files */
	(void) sprintf(lfilename, "/tmp/lfile.%s.%05.5lu",
		hostname, (u_long)getpid());

	/* find the first filesystem to dump: */
	for (;;) {
		/* first, find which SET we'll be dumping in: */
		for (i = 1; i < MAXDUMPSETS; i++) {
			if (cf_tapeset[i] == NULL)
				continue;
			for (d = cf_tapeset[i]->ts_devlist; d; d = d->dc_next) {
				if (d->dc_filesys[0] == '-' ||
				    (fswitch && d->dc_filesys[0] == '*')) {
					if (d != cf_tapeset[i]->ts_devlist) {
						/* starting in middle */
						(void) sprintf(scratch, gettext(
			"%s: Re-starting interrupted %s execution.\n"),
							progname, progname);
						(void) printf(scratch);
						(void) fflush(stdout);
						log(scratch);
						display(scratch);
					}
					goto foundstart;
				}
			}
		}

		/*
		 * if we get here, then all dumpsets are complete and the
		 * mastercycle is done  - we have already called
		 * incrmastercycle() in the pre-processing checks so we would
		 * know the correct mastercycle for tape reservations
		 */

		/* therefore, we mark them all incomplete, and try again */
		for (i = 1; i < MAXDUMPSETS; i++) {
			if (cf_tapeset[i] == NULL)
				continue;
			for (d = cf_tapeset[i]->ts_devlist; d; d = d->dc_next)
				markundone(d);
		}
		/* this is done for atomic update: */
		if (nswitch == 0) {
			int	savelockfid = lockfid;

			outputfile(newfilename);
			(void) fclose(infid);
			lockfid = exlock(newfilename, gettext(
			    "Cannot lock modified configuration file\n"));
			if (rename(newfilename, filename) == -1)
				die(gettext("Cannot rename `%s' to `%s'\n"),
					newfilename, filename);
			(void) close(savelockfid);
			openconfig(filename);
		}
	}

foundstart:			/* start at set i filesystem d->dc_filesys */

	thisdumpset = i;
	sectapes = newstring();
	remote_seq = 0;
	for (; d && !diesoon; d = d->dc_next) {
		if (d->dc_filesys[0] == '+')	/* fswitch can make this */
			continue;		/* happen */
		/*
		 * XXX - tmpfs seems buggy on Solaris 2.0.  I have seen files
		 * disappear soon after being created.  To work around this, we
		 * add a unique field to the remote lfile name.
		 */
		(void) sprintf(rlfilename, "/tmp/rlfile.%s.%d.%05.5lu",
			hostname, remote_seq++, (u_long)getpid());
		logmail(d->dc_log, NULL); /* register the mail log */
		onedump(d->dc_filesys, d->dc_dumplevels, &d->dc_fullcycle,
			0, d->dc_linenumber);
	}
	dbdumplog = newstring();
	logmail(dbdumplog, NULL);
	if (!diesoon && nswitch == 0) {
		if (gooddumps > 0 && cf_dumplib && cf_dumplib[0]) {
			FILE   *listfid;

			(void) sprintf(tempfilename, "%s/tlist%05.5lu",
				tmpdir, (u_long)getpid());
			(void) sprintf(scratch,
		"(%s/dumpdm -s %s dbinfo; %s/dumpdm -s %s -V tapelist)> %s\n",
				gethsmpath(sbindir), cf_dumplib,
				gethsmpath(sbindir), cf_dumplib, tempfilename);
			listfid = NULL;
			p = gettext("database server not running");
			q = gettext("Database server unavailable");
			(void) fprintf(stderr, "%s: %s", progname, gettext(
			    "Creating database listing file...\n"));
			log("%s: exec %s", progname, scratch);
			for (;;) {
				if (System(scratch) != 0 ||
				    !(listfid = fopen(tempfilename, "r")) ||
				    !fgets(line, MAXLINELEN, listfid) ||
				/* info line */
				    !fgets(line2, MAXLINELEN, listfid) ||
				/* first line of tapelist */
				    strncmp(line, p, strlen(p)) == 0 ||
				    strncmp(line2, p, strlen(p)) == 0) {
					(void) sprintf(scratch, gettext(
	"Cannot obtain disaster recovery tape listing.  Be sure the command:\n\
	\t%s/dumpdm -s %s -V tapelist\nexecutes correctly.\n"),
					    gethsmpath(sbindir), cf_dumplib);
					stringapp(sectapes, scratch);
					if (listfid != NULL)
						(void) fclose(listfid);
					listfid = NULL;
					goto nomoredumps;
				}
				if (strncmp(line, q, strlen(q)) == 0 ||
				    strncmp(line2, q, strlen(q)) == 0) {
					/* busy */
					(void) fclose(listfid);
					/* Wait for server to be avail */
					(void) sleep(20);
					continue;
				}
				break;
			}
			tapeposofsecur = tapeposofnextfile; /* save this one! */
			(void) sprintf(scratch, "-%s", tempfilename);
			(void) fprintf(stderr, "%s: %s", progname, gettext(
			    "Dumping database listing file...\n"));
			onedump(scratch, ">0", &zero, 1, 0);
			stringapp(sectapes, "\n");
		}
nomoredumps:
		(void) unlink(tempfilename);

		/* Wind it up and offline tape if required: */
		newtape = 0;
		if (cf_longplay && cf_cron.c_enable && !isatty(0)) {
			/*
			 * See if the next cron dump will ask for a new tape
			 * Set "newtape" only if:
			 *	1) We are in long-play mode, and
			 *	2) "cron" exucution is enabled, and
			 *	3) We were invoked from "cron" (!isatty), and
			 *	4) Next scheduled dumpex run specifies -s.
			 */
			time_t now;
			struct tm *tmp;

			(void) time(&now);
			tmp = localtime(&now);
			if (--tmp->tm_wday < 0)
				tmp->tm_wday = Sun;
			/*
			 * Loop through the days to find the next enabled
			 * day and see if it has the new tape option on.
			 * Be careful with today!
			 */
			for (i = 0; i < 7; i++) {
				if ((!cf_cron.c_ena[tmp->tm_wday]) ||
				    (i == 0 && cf_cron.c_dtime <=
					(tmp->tm_hour * 100 + tmp->tm_min))) {
					if (++tmp->tm_wday > Sun)
						tmp->tm_wday = Mon;
					continue;
				}
				newtape = cf_cron.c_new[tmp->tm_wday];
				break;
			}
		}
		if (cf_longplay == 0 || newtape) {
			if (debug) {
				(void) fprintf(stderr, gettext(
				    "index of last tape = %d"),
					indexoflasttapeused);
				if (indexoflasttapeused != -1)
					(void) fprintf(stderr, " -> %s",
					    cf_dumpdevs[indexoflasttapeused %
						ncf_dumpdevs]);
				(void) fprintf(stderr, "\n");
			}
			if (indexoflasttapeused != -1 && dontoffline == 0)
				fixtape(cf_dumpdevs[indexoflasttapeused %
				    ncf_dumpdevs], "offline");
		} else {
			/*
			 * We are in long-play mode and we aren't switching
			 * to a fresh tape, so just rewind the tape drive
			 */
			if (indexoflasttapeused != -1)
				fixtape(cf_dumpdevs[indexoflasttapeused %
				    ncf_dumpdevs], "rewind");
		}
	}
}

static void
onedump(filesystem, dumplevels, fullcycleptr, securitydump, linenumber)
	char	*filesystem;
	char	*dumplevels;
	int    *fullcycleptr;
	int	securitydump;
	int	linenumber;
{
	char	scratch[MAXLINELEN];
	int	scratchrc;
	struct tapes_f *t;
	int	tapenum, gotused;
	int	c, looksgood;
	char	line[MAXLINELEN];	/* temp for checking lfile results */
	char	*dumpdatefilename;
	char	*p;
	char	*q;
	int	returncode;		/* result of executing the dump */
	int	donelongplayfile;	/* flag to remember if lpfile fixed */
	int	savelastposition;
	char	level;
	extern char dumplevel;
	char	*filedumparg;
	FILE	*cmd;			/* for reading remote results */
	struct passwd *pwd;		/* for use with rcmd's */

	returncode = NODUMPDONE;	/* in case of no lines */

	/* XXX filesystem[0] == status */
	if (setupvers(&filesystem[1]) != 0) {
		if (nswitch == 0 && securitydump == 0)
			markfail(linenumber, filesystem);
		return;
	}

	/* SET UP ARGUMENTS: */
	initdumpargs();

	adddumpflag("b");	/* blocking factor */
	(void) sprintf(scratch, "%d", cf_blockfac);
	adddumparg(scratch);

	if (dontoffline == 0) {
		adddumpflag("o");	/* offline when hit EOT */
		adddumpflag("l");	/* autoload, as with a stacker */
	}

	adddumpflag("V");	/* verify labels */

	adddumpflag(reposition ? "P" : "p");	/* tape position */
	(void) sprintf(scratch, "%d", tapeposofnextfile);
	adddumparg(scratch);

	if (securitydump == 0) {
		if (!outofband)
			adddumpflag("u");	/* update dumpdates */
		if (cf_dumplib && cf_dumplib[0]) {
			adddumpflag("U");	/* update dumpindex */
			adddumparg(cf_dumplib);
		}
		adddumpflag("I");	/* dumpdates file */
		adddumparg(get_dumpdates());
	}
	/* dump level */
	for (p = dumplevels; *p && *p != '>'; p++)
		/* empty */;
	if (*p == NULL)
		die(gettext("find dump level: Internal consistency error\n"));
	if (dumplevel != '\0')
		level = dumplevel;
	else
		level = *(p + 1);
	if (level == 'x' || level == 'X')
		adddumpflag("x");	/* true incremental flag */
	else
		adddumpflagc(level);

	/* mail errors and problems in dump to: */
	adddumpflagc('M');
	if (ncf_notifypeople == 0) {	/* default to root */
		adddumparg("root");
	} else {
		struct string_f *notifylist = newstring();
		int	i;
		for (i = 0; i < ncf_notifypeople; i++) {
			if (i != 0)	/* continue list with comma */
				stringapp(notifylist, ",");
			stringapp(notifylist, cf_notifypeople[i]);
		}
		adddumparg(notifylist->s_string);
	}


	/* determine how long to keep things around: */
	if (securitydump == 0)
		figurekeep(level, *fullcycleptr + 1, dumplevels);

	/* determine filesys to dump and remote machine (if any): */
	remote[0] = 0;
	if ((p = index(filesystem, ':')) != NULL) {
		filesystodump = filesystem + 1;
		filedumparg = p + 1;
		p = filesystem + 1;
		q = remote;
		while (*p != ':')
			*q++ = *p++;
		*q++ = '\0';
		if (strcmp(remote, hostname) == 0)
			remote[0] = 0;
	} else {
		char	*localfilesys =
		checkalloc(strlen(hostname) + strlen(filesystem + 1) + 1 + 1);
		/* 1 for colon, 1 for \0 */
		filedumparg = filesystem + 1;
		(void) sprintf(localfilesys, "%s:%s", hostname, filesystem + 1);
		filesystodump = strdup(localfilesys);
		free(localfilesys);
	}

	adddumpflag("L");	/* L file */
	if (remote[0] == 0)
		adddumparg(lfilename);		/* local L-file */
	else
		adddumparg(rlfilename);		/* remote L-file */

	setupdevs(remote);
	adddumparg(filedumparg);

	makedumpcommand();

	if (debug)
		(void) printf(gettext("command: %s\n"), dumpcommand->s_string);
	if (nswitch) {
		if (Nswitch)
			(void) printf("%s: %s\n",
				progname, dumpcommand->s_string);
		else
			(void) printf("%s: hsmdump %c %s\n",
				progname, level, filesystodump);
		tapeposofnextfile++;	/* pretend like it was one file */
		return;
	}

	if (writelfile() != 0)		/* write l file */
		goto out;

	/* PERFORM A DUMP */

	printlfile(gettext(
		"\nVolume label file as it stands before running hsmdump:\n"));
	log("Dump: lev %c %s mcycle %d fcycle %d\n",
	    level, filesystodump, cf_mastercycle, *fullcycleptr);
	if (remote[0]) {	/* remote dump -> copy lfile away */
		char	command[MAXLINELEN];
		int	lfile_fd, n;
		rhp_t	rhp;

lfile_put_again:
		if ((lfile_fd = open(lfilename, O_RDONLY)) < 0) {
			fprintf(stderr, "Cannot open lfile: ");
			perror("open");
			goto out;
		}
		(void) sprintf(command,
			"sh -c '( /bin/cat > %s ) 2>&1; exec echo ==$?'",
			rlfilename);
		rhp = remote_setup(remote, cf_rdevuser, command, 0);
		if (rhp == 0) {
			host_down(gettext("Cannot create remote lfile"),
				command, filesystodump);
			(void) close(lfile_fd);
			goto out;
		}
		while ((n = read(lfile_fd, scratch, sizeof (scratch))) > 0) {
			if (remote_write(rhp, scratch, n) < 0) {
				/*
				 * XXX - sometimes this connection will time
				 * out before we get exit status data.
				 */
				(void) close(lfile_fd);
				remote_shutdown(rhp);
				goto lfile_put_again;
			}
		}
		(void) close(lfile_fd);
		/*
		 * XXX Close the writing side, so that we get our exit status.
		 */
		(void) shutdown(rhp->rh_fd, 1);

		scratchrc = NORETURNCODE;
		(void) gatherline(0, 0, (int *) NULL);
		do {
			int i, n;

			n = remote_read(rhp, scratch, sizeof (scratch));
			if (n <= 0)
				break;
			for (i = 0; i < n; i++) {
				(void) gatherline((int) scratch[i], 1,
					&scratchrc);
				if (scratchrc != NORETURNCODE)
					break;
			}
		} while (scratchrc == NORETURNCODE);
		remote_shutdown(rhp);
		if (scratchrc != 0) {
			host_down(gettext("Cannot write remote lfile"),
				command, filesystodump);
			goto out;
		}

		(void) printf(gettext(
		    "----> Reply to remote dump prompts via %s <----\n"),
			"opermon");
		(void) fflush(stdout);
	}

	returncode = NORETURNCODE;		/* in case of nolines */
	(void) gatherline(0, 0, (int *) NULL);	/* initialize */

	/*
	 * do the dump & gather its output & status
	 */
	if (remote[0]) {
		int fd, n, i;
		char *host;
		char buf[MAXLINELEN];
		rhp_t rhp;

		/*
		 * A sticky problem that could happen at this point, is
		 * that the remote dump will turn around and use rmt to
		 * a tape on this machine (or elsewhere). If the dumping
		 * machine crashes, the rmt process lives on and locks out
		 * further opens of the tape device. This dumpex has no
		 * handle on the rmt processes even if it correctly figures
		 * out the dump machine is in the toilet. But if dumpex
		 * continues, at least the log file and status mail will
		 * give a clue as to what happened. Hard to build a robust
		 * application on top of flakey system services. sigh.
		 */
		host = remote;
		rhp = remote_setup(remote, cf_rdevuser,
		    dumpcommand->s_string, 1);
		if (rhp == 0) {
			log(gettext("%s: Cannot execute `rsh %s %s'\n"),
				progname, host, dumpcommand->s_string);
			(void) fprintf(stderr, gettext(
				"%s: Cannot execute `rsh %s %s'\n"),
				progname, host, dumpcommand->s_string);
			goto out;
		}

		looksgood = 0;
		do {
			char *cp;

			if ((n = remote_read(rhp, buf, MAXLINELEN)) < 0) {
				host_down(gettext("Remote read error"),
					dumpcommand->s_string, filesystodump);
				(void) remote_shutdown(rhp);
				goto out;
			}
			if (n == 0)
				break;
			for (i = 0; i < n; i++) {
				cp = gatherline((int)buf[i], 1, &returncode);
				if (returncode != NORETURNCODE)
					break;
				/* XXX - see below... */
				if (cp && strstr(cp, "DUMP: ") &&
				    strstr(cp, " blocks (") &&
				    strstr(cp, ") on "))
					looksgood = 1;
			}
		} while (returncode == NORETURNCODE);
		remote_shutdown(rhp);

		/*
		 * XXX - Really gross hack.  Due to a TCP bug in Solaris 2.0,
		 * the exit status is sometimes lost by the TCP system before
		 * we get a chance to see it.  So, we peek at the remote
		 * dump data to see if it looks like it completed.
		 */
		if (returncode == NORETURNCODE && looksgood)
			returncode = 0;

		/*
		 * At this point, we really should know if the dump session
		 * succeeded. If not, we still try and retrieve the L-file,
		 * as it may tell us what tape is now in the drive.
		 */
		if (returncode == NORETURNCODE)
			host_down(gettext("Remote read early EOF from dump"),
				dumpcommand->s_string, filesystodump);
	} else {
		/*
		 * Use popen to execute local commands and capture output.
		 */
		cmd = popen(dumpcommand->s_string, "r");
		if (cmd == NULL) {
			log(gettext("%s: Cannot execute `%s'\n"),
				progname, dumpcommand->s_string);
			(void) fprintf(stderr, gettext(
				"%s: Cannot execute `%s'\n"),
				progname, dumpcommand->s_string);
			goto out;
		}
		setbuf(cmd, (char *) NULL);

		while ((c = getc(cmd)) != EOF) {
			(void) gatherline(c, 1, &returncode);
			if (returncode != NORETURNCODE)
				break;
		}
		(void) pclose(cmd);
	}

	if (logfile)
		(void) fflush(logfile);
	/* copy lfile back if remote: */
	if (remote[0]) {
		char	command[MAXLINELEN];
		FILE	*lfile_fp;
		int	fd;
		rhp_t	rhp;

lfile_get_again:
		if ((lfile_fp = fopen(lfilename, "w+")) == NULL) {
			fprintf(stderr, "Cannot open lfile: ");
			perror("creat");
			returncode = NORETURNCODE;
			goto out;
		}
		(void) sprintf(command,
			"sh -c '( /bin/cat %s ); exec echo ==$?'",
			rlfilename);
		rhp = remote_setup(remote, cf_rdevuser, command, 1);
		if (rhp == 0) {
			log(gettext(
		"%s: Cannot copy volume label file from machine `%s'\n"),
			    progname, remote);
			(void) fprintf(stderr, gettext(
		"%s: Cannot copy volume label file from machine `%s'\n"),
			    progname, remote);
			(void) fclose(lfile_fp);
			(void) unlink(lfilename);
			returncode = NORETURNCODE;
			goto out;
		}

		scratchrc = NORETURNCODE;
		(void) gatherline(0, 0, (int *) NULL);
		looksgood = 0;
		do {
			int i, n;
			char *cp;

			n = remote_read(rhp, scratch, sizeof (scratch));
			if (n < 0) {
				int oerrno = errno;

				(void) fclose(lfile_fp);
				(void) unlink(lfilename);
				remote_shutdown(rhp);
				/*
				 * XXX - sometimes this connection will time
				 * out before we get any data.  See if this
				 * is the case, then re-try.
				 */
				if (oerrno == ETIMEDOUT)
					goto lfile_get_again;
				returncode = NORETURNCODE;
				goto out;
			}
			if (n == 0)
				break;
			if (n > 0) {
				looksgood = 1;
				for (i = 0; i < n; i++) {
					cp = gatherline((int) scratch[i], 0,
						&scratchrc);
					if (cp && fputs(cp, lfile_fp) == EOF) {
						(void) fclose(lfile_fp);
						(void) unlink(lfilename);
						remote_shutdown(rhp);
						returncode = NORETURNCODE;
						goto out;
					}
					if (scratchrc != NORETURNCODE)
						break;
				}
			}
		} while (scratchrc == NORETURNCODE);
		remote_shutdown(rhp);

		/*
		 * XXX - For some reason, this remote command frequently
		 * loses its exit status.  If we got some data, we'll
		 * just assume that the exit status made it here OK.
		 */
		if (scratchrc == NORETURNCODE && looksgood)
			scratchrc = 0;

		if (fflush(lfile_fp) == EOF || scratchrc == NORETURNCODE) {
			(void) sprintf(scratch, gettext(
			    "%s: Cannot %s volume label file `%s'\n"),
			    progname, scratchrc == NORETURNCODE ?
			    gettext("get") : gettext("write"), lfilename);
			log("%s", scratch);
			logmail(NULL, scratch);
			(void) fprintf(stderr, "%s", scratch);
			(void) fclose(lfile_fp);
			(void) unlink(lfilename);
			returncode = NORETURNCODE;
			goto out;
		}
		(void) fclose(lfile_fp);
	}

	printlfile(gettext(
		"Volume label file as it stands AFTER running hsmdump:\n"));

	/* update the tape library: */
	lfilefid = fopen(lfilename, "r");
	if (lfilefid == NULL) {
		log(gettext("%s: Cannot re-open volume label file `%s'\n"),
			progname, lfilename);
		(void) fprintf(stderr, gettext(
			"%s: Cannot re-open volume label file `%s'\n"),
			progname, lfilename);
		goto out;
	}

	if (fgets(line, MAXLINELEN, lfilefid) == NULL) {
		(void) sprintf(scratch, gettext(
		    "%s: Volume label file `%s' is empty or corrupted (%d)\n"),
			progname, lfilename, 1);
		log("%s", scratch);
		logmail(NULL, scratch);
		fprintf(stderr, "%s", scratch);
		(void) fclose(lfilefid);
		goto out;
	}

	if (strcmp(line, LF_HEADER) != 0) {	/* security line: */
		log(gettext(
	    "%s: Volume label file `%s' contains invalid security string: %s"),
			progname, lfilename, line);
		(void) fprintf(stderr, gettext(
	    "%s: Volume label file `%s' contains invalid security string: %s"),
			progname, lfilename, line);
		(void) fclose(lfilefid);
		goto out;
	}

	if (fgets(line, MAXLINELEN, lfilefid) == NULL) { /* ignore library */
		log(gettext(
		    "%s: Volume label file `%s' is empty or corrupted (%d)\n"),
			progname, lfilename, 2);
		(void) fprintf(stderr, gettext(
		    "%s: Volume label file `%s' is empty or corrupted (%d)\n"),
			progname, lfilename, 2);
		(void) fclose(lfilefid);
		goto out;
	}

	/*
	 * At this point, the L-file looks OK.  Go ahead and remove the
	 * remote copy.
	 */
	if (remote[0]) {
		char	command[MAXLINELEN];
		char	*host;
		rhp_t	rhp;

		host = remote;
		(void) sprintf(command,
			"sh -c '( /bin/rm -f %s ) 2>&1; exec echo ==$?'",
			rlfilename);
		rhp = remote_setup(remote, cf_rdevuser, command, 1);
		if (rhp) {
			/*
			 * It's OK if this fails...  We currently do not
			 * even bother looking for the exit status.
			 */
			remote_shutdown(rhp);
		}
	}

	gotused = 0;
	donelongplayfile = 0;
	indexoflasttapeused = -1;
	tapenum = 0;
	savelastposition = tapeposofnextfile; /* where current dump started */
	while (fgets(line, MAXLINELEN, lfilefid)) {
		int	tapeid = atoi(&line[IDCOLUMN]);
		char	*p;
		struct oob_mail	*omp = NULL;

		if (debug)
			(void) fprintf(stderr, gettext(
			    "tapenum is: %d   status is %c\n"),
				tapenum, line[STATCOLUMN]);
		tapenum++;

		if (line[USEDCOLUMN] == LF_NOTUSED)	/* unused, no change */
			continue;
		if (line[USEDCOLUMN] == LF_USED && gotused == 0) {
			gotused = 1;
			tapeposofnextfile = 1;
			reposition = 0;
		}
		if (outofband) {
			struct oob_mail *tomp;

			tomp = (struct oob_mail *)checkalloc(sizeof (*tomp));
			tomp->om_fs = newstring();
			if (remote[0] != 0) {
				stringapp(tomp->om_fs, filesystem+1);
			} else {
				stringapp(tomp->om_fs, hostname);
				stringapp(tomp->om_fs, ":");
				stringapp(tomp->om_fs, filesystem+1);
			}
			tomp->om_tapeid = tapeid;
			tomp->om_file = savelastposition;
			tomp->om_continue = NULL;
			tomp->om_next = NULL;
			if (omp == NULL) {
				if (outofband_mailp == NULL) {
					outofband_mailp = tomp;
				} else {
					omp = outofband_mailp;
					for (; omp->om_next;
					    omp = omp->om_next)
						;
					omp->om_next = tomp;
				}
			} else {
				omp->om_continue = tomp;
			}
			omp = tomp;
		}


		if (securitydump) {
			if (sectapes->s_string[0] == '\0') {
				(void) sprintf(scratch, gettext(
			"Database recovery: tape %s%c%05.5d, file %d"),
					cf_tapelib,
					LF_LIBSEP,
					tapeid,
					tapeposofsecur);
				stringapp(sectapes, scratch);
			} else {
				(void) sprintf(scratch, gettext(
					" and tape %d"), tapeid);
				stringapp(sectapes, scratch);
			}
		}
		if (line[STATCOLUMN] == LF_PARTIAL ||
		    line[STATCOLUMN] == LF_FULL)
			indexoflasttapeused = tapenum - 1;

		/* update status in data base: */
		tl_markstatus(tapeid, TL_USED);	/* used -> labeled */
		log("Tape: %s used %s%c%05.5d exp %d %d\n",
			filesystodump,
			cf_tapelib, LF_LIBSEP, tapeid,
			time((time_t *) 0) + DAYTOSEC(keepdays),
			keeptil);
		if (line[STATCOLUMN] == LF_ERRORED) {
			tl_error(tapeid);
			log("TapeErr: %s%c%05.5d\n",
				cf_tapelib, LF_LIBSEP, tapeid);
		}
		/* find this one's extant tape record: */
		for (t = tapes_head.ta_next; t != &tapes_head; t = t->ta_next)
			if (tapeid == t->ta_number)
				goto updateit;

		/*
		 * oops, got a new tape (dump ran over list conf file req'd);
		 * update that tape:
		 */
		/*LINTED [alignment ok]*/
		t = (struct tapes_f *) checkalloc(sizeof (struct tapes_f));
		t->ta_number = tapeid;
		t->ta_mount = "";	/* used for initial mounting */
		/* doubly linked insertion: */
		t->ta_next = &tapes_head;
		t->ta_prev = tapes_head.ta_prev;
		tapes_head.ta_prev->ta_next = t;
		tapes_head.ta_prev = t;

		/* update our idea of the tape's status */
updateit:
		t->ta_status = line[STATCOLUMN];
		tl_update(t->ta_number, (int) (keepdays == -1 ? -1 :
			time((time_t *) 0) + DAYTOSEC(keepdays)), keeptil);

		/*
		 * note where next dump is to go (since re-dumps mess up tape
		 * pos):
		 */
		if ((p = index(line, ' ')) != NULL) {
			tapeposofnextfile = atoi(p + 1);
			if (cf_longplay) {	/* remember our status */
				if (line[STATCOLUMN] == LF_PARTIAL) {
					FILE   *auxfid =
						fopen(auxstatusfile, "w");
					if (auxfid == NULL ||
					    fputs(line, auxfid) == EOF ||
					    fflush(auxfid) == EOF ||
					    fsync(fileno(auxfid)) == -1) {
						log(gettext(
				    "%s: error writing longplay file `%s'\n"),
							progname,
							auxstatusfile);
						(void) fprintf(stderr, gettext(
				    "%s: error writing longplay file `%s'\n"),
							progname,
							auxstatusfile);
						if (auxfid != NULL)
							(void) fclose(auxfid);
						break;
					}
					(void) fclose(auxfid);
					donelongplayfile = 1;
				}
			}
		}
	}
	(void) fclose(lfilefid);
	/*
	 * If we are in longplay mode, we didn't muck with the
	 * longplay file (i.e., there was not a 'P' line that included
	 * new file positioning information), but we did see at least
	 * one '+' status in the file -> then remove the existing longplay
	 * file, if any.
	 */
	if (cf_longplay && donelongplayfile == 0 && gotused)
		unlinklpfile();

out:
	if (returncode > 0) {	/* life is bad */
		log(gettext(
		    "%s: Returned exit status %d; marking dump as bad\n"),
			"DumpBAD", returncode);
		reposition = 1;	/* re-try old position */
		if (securitydump == 0)
			markfail(linenumber, filesystem);	/* mark '*' */
		return;
	}
	if (returncode < 0) {	/* life is bad... */
		switch (returncode) {
		case -1:	/* output, but return code not last */
			log(gettext(
		    "%s: Status returned incorrectly; marking dump as bad\n"),
				"DumpBAD");
			reposition = 1;	/* re-try old position */
			if (securitydump == 0)
				markfail(linenumber, filesystem); /* mark '*' */
			return;
		case NORETURNCODE:	/* no output sent back */
			log(gettext(
			    "%s: No output; machine %s probably down\n"),
				"DumpBAD", remote);
			reposition = 1;	/* re-try old position */
			if (securitydump == 0)
				markfail(linenumber, filesystem); /* mark '*' */
			return;
		case NODUMPDONE:	/* no dump invoked; already logged */
			if (securitydump == 0)
				markfail(linenumber, filesystem); /* mark '*' */
			return;
		default:
			log(gettext(
		    "%s: Received unexpected return code (%d) from hsmdump\n"),
				progname, returncode);
			(void) fprintf(stderr, gettext(
		    "%s: Received unexpected return code (%d) from hsmdump\n"),
				progname, returncode);
			if (securitydump == 0)
				markfail(linenumber, filesystem); /* mark '*' */
			return;
		}
	}
	gooddumps++;
	if (securitydump == 0) {
		markdone(filesystem, linenumber, fullcycleptr);
		logmail(NULL, NULL); /* free the mail log */
	}
	log(gettext("%s: lev %c %s\n"), "DumpOK", level, filesystodump);
}

/* set up devices to tell dump */
static void
setupdevs(remote)
	char	*remote;
{
	int	i;

	adddumpflag("f");	/* f devices */

	devstring = newstring();
	for (i = 0; i < ncf_dumpdevs; i++) {
		char	*tapespec = strdup(cf_dumpdevs[i]);
		char	*tapename;
		char	*colon;
		int	tapelocal = 0;

		if (i)
			stringapp(devstring, ",");
		tapename = tapespec;
		colon = index(tapespec, ':');
		if (colon) {	/* tapename had ':' */
			*colon = '\0';	/* isolate machine or name@machine */
			if (index(tapespec, '@'))
				tapename = index(tapespec, '@') + 1;
			if ((remote[0] && strcmp(tapename, remote) == 0) ||
			    (remote[0] == 0 &&
			    strcmp(tapename, hostname) == 0)) {
				tapelocal = 1;	/* tape located on machine to */
						/* be dumped */
			}
		} else {
			if (remote[0] == 0)	/* machine is local... */
				tapelocal = 1;
		}
		if (tapelocal) {
			if (colon)
				/* remote tape spec, use tape name */
				stringapp(devstring, colon + 1);
			else
				/* local tape name */
				stringapp(devstring, tapename);
		} else {
			/* tape is remote to machine being dumped */
			if (cf_rdevuser && index(cf_dumpdevs[i], '@') == NULL) {
				stringapp(devstring, cf_rdevuser);
				stringapp(devstring, "@");
			}
			if (index(cf_dumpdevs[i], ':') == 0) {
				/* local tape drive */
				stringapp(devstring, hostname);
				stringapp(devstring, ":");
				stringapp(devstring, tapename);
			} else {
				/* remote tape drive */
				stringapp(devstring, cf_dumpdevs[i]);
			}
			free(tapespec);
		}
	}
	adddumparg(devstring->s_string);
}

/*
 * Determine which version of the software the target
 * host is running and initialize "usehsmroot" appropriately.
 * We get the version by checking to see if /opt/SUNWhsm/lib
 * exists and is a directory.  If it does, we're running V2.0.
 *
 * Returns 0 if we could figure it out; non-zero if we should skip
 * this file system
 */
#ifdef __STDC__
static int
setupvers(char *filesystem)
#else
static int
setupvers(filesystem)
	char	*filesystem;
#endif
{
	struct string_f *verscommand;
	FILE	*cmd;
	int	result, i, n;
	rhp_t	rhp;
	char	scratch[MAXLINELEN];
	char	*cp, *colon, *remote, *xremote, *host;
	static char *lasthost;

	if (nswitch != 0 && Nswitch == 0)	/* just -n specified */
		return (0);

	remote = strdup(filesystem);
	if (remote == NULL)
		return (1);
	colon = index(remote, ':');
	if (colon) {			/* remote */
		char *atsign;

		*colon = '\0';		/* isolate [user@]hostname */
		atsign = index(remote, '@');
		if (atsign) {
			xremote = strdup(atsign + 1);
			free(remote);
			if (xremote == NULL)
				return (1);
			remote = xremote;
		}
	} else {
		xremote = strdup(hostname);
		free(remote);
		if (xremote == NULL)
			return (1);
		remote = xremote;
	}
	if (lasthost && strcmp(lasthost, remote) == 0) {
		free(remote);
		return (0);		/* same host as last time */
	}
	if (lasthost)
		free(lasthost);		/* free old memory */
	lasthost = (char *) NULL;
	if (colon == NULL || strcmp(remote, hostname) == 0) {
		/* local: just call setupdump and we're done */
		int savehsmroot = usehsmroot;

		usehsmroot = 1;
		if (setupdump((char *) NULL, filesystem) == 0) {
			lasthost = remote;
			return (0);
		}
		usehsmroot = savehsmroot;
		free(remote);
		return (1);
	}

	/*
	 * Construct a command to determine which version of the software
	 * the target machine is running. Try a Solaris 2.x system first
	 * and if that fails, see if the metamucil version of dump is
	 * running on a 4.x machine.
	 */
	verscommand = newstring();
	stringapp(verscommand, "sh -c '( test -f ");
	stringapp(verscommand, gethsmpath(sbindir));
	stringapp(verscommand, "/hsmdump && test -x ");
	stringapp(verscommand, gethsmpath(sbindir));
	stringapp(verscommand, "/hsmdump ) 2>&1; exec echo ==$?'");
	if (debug)
		(void) printf(gettext("command: %s\n"), verscommand->s_string);
	/*
	 * Run the first remote check. See if hsmdump is an executable
	 * file installed in an expected place.
	 */
	host = remote;
	rhp = remote_setup(host, cf_rdevuser, verscommand->s_string, 1);
	if (rhp == 0) {
		host_down(gettext("Cannot determine remote system type"),
			verscommand->s_string, filesystem);
		free(remote);
		freestring(verscommand);
		return (1);
	}

	result = NORETURNCODE;		/* in case of nolines */
	(void) gatherline(0, 0, (int *) NULL);
	do {
		n = remote_read(rhp, scratch, sizeof (scratch));
		if (n <= 0)
			break;
		for (i = 0; i < n; i++) {
			(void) gatherline((int) scratch[i], 1, &result);
			if (result != NORETURNCODE)
				break;
		}
	} while (result == NORETURNCODE);
	remote_shutdown(rhp);
	freestring(verscommand);

	if (result == NORETURNCODE) {
		host_down(gettext("Cannot determine remote system type"),
			verscommand->s_string, filesystem);
		free(remote);
		return (1);
	}

	if (result != 0) {

		/*
		 * Either the target is not Solaris 2.x or the product
		 * is not installed. See if this is a 4.x machine with
		 * metamucil 1.x installed.
		 */
		verscommand = newstring();
		stringapp(verscommand,
"sh -c '( test -f /usr/etc/dump && test -x /usr/etc/dump && /usr/etc/dump X )");
		stringapp(verscommand, " >/dev/null 2>&1; exec echo ==$?'");
		if (debug)
			(void) printf(gettext("command: %s\n"),
				verscommand->s_string);
		/*
		 * Run the command. The 'X' arg to dump is only valid for
		 * the metamucil version.
		 */
		host = remote;
		rhp = remote_setup(host, cf_rdevuser, verscommand->s_string, 1);
		if (rhp == 0) {
			host_down(gettext(
			    "Cannot determine remote system type"),
			    verscommand->s_string, filesystem);
			free(remote);
			freestring(verscommand);
			return (1);
		}

		result = NORETURNCODE;		/* in case of nolines */
		(void) gatherline(0, 0, (int *) NULL);
		do {
			n = remote_read(rhp, scratch, sizeof (scratch));
			if (n <= 0)
				break;
			for (i = 0; i < n; i++) {
				(void) gatherline((int) scratch[i], 1, &result);
				if (result != NORETURNCODE)
					break;
			}
		} while (result == NORETURNCODE);
		remote_shutdown(rhp);
		freestring(verscommand);

		if (result == NORETURNCODE) {
			host_down(gettext(
			    "Cannot determine remote system type"),
			    verscommand->s_string, filesystem);
			free(remote);
			return (1);
		} else if (result != 0) {
			sprintf(scratch, gettext(
 "Cannot read dump.conf file.  Is the product installed on host %s?\n"), host);
			log(scratch);
			logmail(NULL, scratch);
			fprintf(stderr, scratch);
			return (1);
		} else {
			usehsmroot = 0;
		}
	} else {
		usehsmroot = 1;
	}

	/* we only call setupdump if we are using hsm paths */
	if (usehsmroot) {
		if (setupdump(remote, filesystem) == 0) {
			lasthost = remote;
		} else {
			free(remote);
			return (1);
		}
	} else
		lasthost = remote;

	if (debug)
		(void) fprintf(stderr, gettext(
		    "Host `%s' appears to use %s-based paths\n"),
			remote, usehsmroot ? gethsmpath(rootdir) : "/usr/etc");

	return (0);
}

static void
host_down(reason, command, filesys)
	char *reason;
	char *command;
	char *filesys;
{
	char buffy[BUFSIZ];

	log(gettext("%s: %s: `%s'\n"), progname, reason, command);
	log(gettext("%s: Cannot connect to remote host; skipping `%s'\n"),
		progname, filesys);
	(void) fprintf(stderr, gettext(
		"%s: Cannot connect to remote host; skipping `%s'\n"),
		progname, filesys);
	(void) sprintf(buffy, gettext("%s: Host appears to be down.\n"),
		reason);
	logmail(NULL, buffy);
}

/*
 * Do whatever magic we have to do on the remote end to setup a remote
 * dump execution environment.  For remote UNIX hosts, this probably means
 * creating the /etc/opt/SUNWhsm and /var/opt/SUNWhsm directory hierarchy.
 */
static int
setupdump(host, filesys)
	char *host;
	char *filesys;
{
	struct string_f *verscommand;
	char scratch[MAXLINELEN];
	FILE *cmd;
	rhp_t rhp;
	int result, i, n, c;

	if (nswitch)
		return (0);

	/* build the command */
	verscommand = newstring();
	if (host != NULL)
		stringapp(verscommand, "sh -c '");
	stringapp(verscommand, "( ");
	stringapp(verscommand, gethsmpath(libdir));
	stringapp(verscommand, "/");
	stringapp(verscommand, "dumpsetup");
	if (cf_dumplib && cf_dumplib[0]) {
		stringapp(verscommand, " -U ");
		stringapp(verscommand, cf_dumplib);
	}
	if (opserver[0]) {
		/* XXX - check for localhost? */
		stringapp(verscommand, " -O ");
		stringapp(verscommand, opserver);
	}
	stringapp(verscommand, " -I ");
	stringapp(verscommand, get_dumpdates());
	if (host != NULL)
		stringapp(verscommand, " ) 2>&1; exec echo ==$?'");
	else
		stringapp(verscommand, " ) 2>&1; echo ==$?");
	if (debug)
		(void) fprintf(stderr, gettext("%s: exec %s\n"),
			progname, verscommand->s_string);

	/* ... and do it */
	result = NORETURNCODE;		/* in case of nolines */
	(void) gatherline(0, 0, (int *) NULL);
	if (host != NULL) {
		rhp = remote_setup(host, cf_rdevuser,
		    verscommand->s_string, 1);
		if (rhp == 0) {
			host_down(gettext("Cannot invoke dumpsetup"),
			    verscommand->s_string, filesys);
			freestring(verscommand);
			return (1);
		}
		do {
			n = remote_read(rhp, scratch, sizeof (scratch));
			if (n <= 0)
				break;
			for (i = 0; i < n; i++) {
				(void) gatherline((int) scratch[i], 1,
				    &result);
				if (result != NORETURNCODE)
					break;
			}
		} while (result == NORETURNCODE);
		remote_shutdown(rhp);
	} else {
		cmd = popen(verscommand->s_string, "r");
		if (cmd == NULL) {
			freestring(verscommand);
			return (1);
		}
		setbuf(cmd, (char *) NULL);

		while ((c = getc(cmd)) != EOF) {
			(void) gatherline(c, 1, &result);
			if (result != NORETURNCODE)
				break;
		}
		(void) pclose(cmd);
	}

	if (result == NORETURNCODE) {
		if (host != NULL)
			host_down(gettext("Cannot invoke dumpsetup"),
				verscommand->s_string, filesys);
		result = 1;
	}
	freestring(verscommand);
	return (result);
}

/*
 * Figure out what to call the dumpdates file, and return a static buffer.
 */
static char *
get_dumpdates()
{
	static char *dumpdatefilename;

	if (dumpdatefilename)
		free(dumpdatefilename);

	dumpdatefilename = checkalloc(
		strlen(usehsmroot ? gethsmpath(etcdir) : "/etc") +
		strlen("/dumps/dumpdates.") + strlen(filename) + 10 + 1);

	/* 2.0: dumpdates goes in etc/dumps/<config-file>.dumpdates */
	if (cf_maxset == 1)
		if (usehsmroot)
			(void) sprintf(dumpdatefilename,
				"%s/%s.dumpdates", confdir, filename);
		else
			(void) sprintf(dumpdatefilename,
				"/etc/dumpdates.%s", filename);
	else
		if (usehsmroot)
			(void) sprintf(dumpdatefilename,
				"%s/%s.dumpdates.%d", confdir,
				filename, thisdumpset);
		else
			(void) sprintf(dumpdatefilename,
				"/etc/dumpdates.%s.%d", filename,
				thisdumpset);
	return (dumpdatefilename);
}
