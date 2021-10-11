#ident	"@(#)dumpex.c 1.59 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include "dumpex.h"
#include "tapelib.h"
#include <config.h>
#include <lfile.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>
#include <pwd.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>
#include <rmt.h>
#include <sys/mtio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>

#ifdef USG
#define	sigvec		sigaction
#define	sv_mask		sa_mask
#define	sv_flags	sa_flags
#define	sv_handler	sa_handler
#else
#define	SA_RESTART	0
#endif

#ifdef __STDC__
static void clear_tapes(void);
#else
static void clear_tapes();
#endif

static void
usage(void)
{
	(void) fprintf(stderr, gettext("Usage: %s [-n] [-N] [-f] \
[-l level] [-o] [-t tapesup] [-r reservetime] [-s] configfile\n"),
		progname);
}

int	diesoon;		/* got a SIGINT */
char	dumplevel;		/* level for all the dumps, if set */
int	outofband;		/* out-of-band dump */
struct oob_mail	*outofband_mailp;	/* info to mail after dumpex -o run */
static int tapesreserved;	/* to know if we need to clean 'em up */

/* ARGSUSED */
static void
sighandler(sig, code)
{
	struct string_f *remotedelete;	/* command to remove remote file */

	(void) fprintf(stderr, gettext(
	    "\n%s: SIG%02.2d: terminating %s abnormally.\n"),
		progname, sig, progname);
	if (nswitch == 0) {
		if (sig == SIGINT) {
			(void) fprintf(stderr, gettext(
		"%s will terminate when next dump terminates.\n"), progname);
			(void) fprintf(stderr, gettext(
			    "Some output from that dump is already lost.\n"));
			diesoon = 1;
			return;
		}
		if (lfilename[0]) {
			(void) unlink(lfilename);
			if (remote[0] && rlfilename[0]) {
				rhp_t rhp;

				remotedelete = newstring();
				stringapp(remotedelete, "sh -c '( /bin/rm -f ");
				stringapp(remotedelete, rlfilename);
				stringapp(remotedelete, " ) 2>&1; echo ==$?'");
				rhp = remote_setup(remote, cf_rdevuser,
					remotedelete->s_string, 1);
				if (rhp) {
					/*
					 * It's OK if this fails...
					 * We currently do not even
					 * bother looking for the
					 * exit status.
					 */
					remote_shutdown(rhp);
				}
			}
		}
		if (tapesreserved)
			clear_tapes();
		log(gettext("Terminated: signal %d\n"), sig);
		if (logfile)
			(void) fclose(logfile);
	}
	if (sig == SIGSEGV)
		abort();
	exit(1);
}

#define	SCHEDTAPES_DISPLAY	10	/* when tapesup is 0, use this value */
#define	SCHEDTAPES_DEFAULT	20	/* when tapesup is 0, reserve 20 */

char *
fullname(host, fs)
char *host, *fs;
{
	static char rc[MAXNAMELEN];

	if (index(fs, ':'))
		return (fs);

	(void) sprintf(rc, "%s:%s", host, fs);
	return (rc);
}

main(argc, argv)
	int	argc;
	char	*argv[];
{
	extern int optind;
	extern char *optarg;
	struct sigvec sv;
	int	schedtapes;			/* how many tapes to put up */
	int	reservetime = RESERVETIME;	/* how long to reserve them */
	int	i, c;
	struct tapes_f *t;
	int	tapesfound;	/* how many tapes we came up short */
	time_t  timeval;
	struct tm *tm;
	char	logfilename[MAXPATHLEN];
	char	mailcommand[MAXCOMMANDLEN];
	int	nbad;		/* number of bad filesystems */
	struct devcycle_f *d;
	char	scratchline[MAXPATHLEN];
	FILE	*auxfile;
	char	*p;
	int	skiptonexttape;	/* 1 -> don't append to old tape */

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	progname = strrchr(argv[0], '/');
	if (progname == (char *)0)
		progname = argv[0];
	else
		progname++;

	setbuf(stderr, (char *) NULL);
	if (sysinfo(SI_HOSTNAME, hostname, BCHOSTNAMELEN) == -1)
		die(gettext("Cannot determine this host's name\n"));

	dumplevel = '\0';	/* dumplevel overrides dc_dumplevels */
	schedtapes = -1;	/* schedtapes overrides cf_tapesup */
	(void) umask(0006);

	skiptonexttape = 0;
	while ((c = getopt(argc, argv, "ol:t:nfNr:dzs")) != -1) {
		switch (c) {
		case 'l':		/* more tapes up */
			if (strcasecmp(optarg, "x") == 0) {
				dumplevel = 'x';
			} else {
				int level = atoi(optarg);
				if ((!isdigit(*optarg)) ||
				    level < 0 || level > 9)
					die(gettext(
			    "Need valid dump level [0-9x] for -l option\n"));
				dumplevel = *optarg;
			}
			break;
		case 'o':			/* out-of-band dump */
			outofband = 1;
			skiptonexttape = 1;	/* implies -s as well */
			break;
		case 't':		/* more tapes up */
			schedtapes = atoi(optarg);
			if (schedtapes < 0)
				die(gettext(
				    "Need non-negative number for -t tapes\n"));
			break;
		case 'n':		/* -n -> don't do it */
			nswitch = 1;
			break;
		case 'f':		/* -f -> force: retry all bad dumps */
			fswitch = 1;
			break;
		case 'N':		/* -N -> don't do it, but briefly */
			Nswitch = nswitch = 1;
			break;
		case 'r':		/* reserve time */
			reservetime = atoi(optarg);
			if (reservetime <= 0)
				die(gettext(
			    "Need positive number for -r reservetime\n"));
			break;
		case 'd':
			debug = 1;
			break;
		case 'z':
			dontoffline = 1;
			break;
		case 's':
			skiptonexttape = 1;
			break;
		default:
			usage();
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1) {
		(void) fprintf(stderr, gettext(
		    "%s: You must supply a configuration file name\n"),
			progname);
		usage();
		exit(1);
	}
	checkroot(0);
	(void) sprintf(confdir, "%s/dumps", gethsmpath(etcdir));
	if (chdir(confdir) == -1)
		die(gettext("Cannot chdir to %s; run this program as root\n"),
			confdir);

	/* set up output logfile: */
	if (time(&timeval) == -1)
		die(gettext("Cannot determine current time\n"));
	tm = localtime(&timeval);
	(void) sprintf(logfilename, "%s/%s/%02.2d.%02.2d.%02.2d",
		gethsmpath(admdir), LOGFILEDIR,
		tm->tm_year, tm->tm_mon + 1, tm->tm_mday);
	if (nswitch == 0)
		logfile = fopen(logfilename, "a+");
	if (logfile != NULL)
		(void) setvbuf(logfile, NULL, _IOLBF, 0);
	/* logfile might be NULL when "logfiledir" does not exist */

#ifdef USG
	(void) sigemptyset(&sv.sv_mask);
#else
	sv.sv_mask = 0;
#endif
	sv.sv_flags = SA_RESTART;
	sv.sv_handler = sighandler;

	(void) sigvec(SIGHUP, &sv, (struct sigvec *)0);
	(void) sigvec(SIGINT, &sv, (struct sigvec *)0);
	(void) sigvec(SIGQUIT, &sv, (struct sigvec *)0);
	(void) sigvec(SIGBUS, &sv, (struct sigvec *)0);
	(void) sigvec(SIGSEGV, &sv, (struct sigvec *)0);
	(void) sigvec(SIGTERM, &sv, (struct sigvec *)0);

	(void) strcpy(filename, argv[0]);

	if (index(filename, '/') != NULL)
		die(gettext(
	    "Names of configuration files must not contain slashes (`%s')\n"),
			filename);

	openconfig(filename);	/* stays open mostly */
	readit();
	if (outofband) {
		cf_longplay = 0;	/* force long-play off */
		cf_dumplib = "";	/* force database updates off */
	}

	(void) sprintf(scratchline, gettext(
	    "This configuration file (%s) is reserved by another executor\n\
or the dump editor.  Please try again when the file is released.\n"),
		filename);
	lockfid = exlock(filename, scratchline);
	if ((int)strlen(filename) > MAXPATHLEN - 5) /* 5 = strlen(".new\0") */
		die(gettext("Configuration file name too long\n"));
	(void) sprintf(newfilename, "%s.new", filename);

	display_init();		/* operator console mesgs */
	log(gettext("Start: By %s -------------------------------\n"),
	    p = getpwuid(getuid())->pw_name);
	(void) sprintf(scratchline, gettext("[%s/%s] Start %s by %s ------"),
		hostname, filename, progname, p);
	display(scratchline);

	(void) sprintf(auxstatusfile, "%s.lp", filename);
	if (nswitch == 0 && (skiptonexttape || cf_longplay == 0))
		unlinklpfile();

	tl_open(cf_tapelib, filename);

	if (schedtapes == -1)
		schedtapes = cf_tapesup;
	if (schedtapes == 0)			/* our default */
		schedtapes = SCHEDTAPES_DEFAULT;

	reposition = 0;
	tapeposofnextfile = 1;

	/*
	 * figure out if we will be incrementing mastercycle before we start
	 * for real, and if so, go ahead and do it so we don't screw up and
	 * reserve based on the wrong mastercycle:
	 */
	for (i = 1; i < MAXDUMPSETS; i++) {
		if (cf_tapeset[i] == NULL)
			continue;
		for (d = cf_tapeset[i]->ts_devlist; d; d = d->dc_next)
			if (d->dc_filesys[0] == '-' ||
			    (fswitch && d->dc_filesys[0] == '*'))
				goto dontincr;
	}
	incrmastercycle();
dontincr:

	/* Get `schedtapes' number of free tapes */
	for (tapesfound = i = 0; i < schedtapes; i++) {
		struct tapedesc_f td;
		/*LINTED [alignment ok]*/
		t = (struct tapes_f *) checkalloc(sizeof (struct tapes_f));
		t->ta_number = -1;

		if (cf_longplay && i == 0 && skiptonexttape == 0 &&
		    (auxfile = fopen(auxstatusfile, "r")) != NULL) {
			if (fgets(scratchline, MAXLINELEN, auxfile) == NULL) {
				log(gettext(
		"%s: Cannot read longplay file (%s) for volume information\n"),
					progname, auxstatusfile);
			} else {
				t->ta_number = atoi(&scratchline[IDCOLUMN]);
				t->ta_status = LF_PARTIAL; /* partially full */
				tapeposofnextfile =
					atoi(&scratchline[POSCOLUMN]);
				reposition = 1;
			}
			(void) fclose(auxfile);
		}
		if (t->ta_number == -1) {
			t->ta_number = tl_findfree(cf_mastercycle, reservetime);
			if (t->ta_number < 0)
				break;
			tapesreserved++;
			/* mark tape as new or new/unlabeled */
			tl_read(t->ta_number, &td);
			if (td.t_status & TL_LABELED)
				t->ta_status = LF_NEWLABELD;
			else
				t->ta_status = LF_UNLABELD;
		}
		t->ta_mount = i < ncf_dumpdevs ? cf_dumpdevs[i] : (char *) 0;

		/* doubly linked insertion: */
		t->ta_next = &tapes_head;
		t->ta_prev = tapes_head.ta_prev;
		tapes_head.ta_prev->ta_next = t;
		tapes_head.ta_prev = t;
		tapesfound++;
	}

	for (t = tapes_head.ta_next, i = 0; t != &tapes_head;
	    t = t->ta_next, i++) {
		if (cf_tapesup == 0 && i >= SCHEDTAPES_DISPLAY)
			break;
		if (t->ta_mount) {
			(void) sprintf(scratchline,
			    gettext("[%s/%s] MOUNT TAPE %s%c%05.5d ON %s"),
				hostname,
				filename,
				cf_tapelib,
				LF_LIBSEP,
				t->ta_number,
				t->ta_mount);
			if (!nswitch)
				display(scratchline);
			if (t->ta_status == LF_UNLABELD) {
				(void) strcat(scratchline, " ");
				(void) strcat(scratchline,
					gettext("[unlabeled]"));
			}
			(void) puts(scratchline);
			log(gettext("%s: %s%c%05.5d on %s\n"),
				"Mount",
				cf_tapelib, LF_LIBSEP, t->ta_number,
				t->ta_mount);
		} else {
			(void) sprintf(scratchline, gettext(
			    "[%s/%s] PREPARE TAPE %s%c%05.5d for mounting"),
				hostname,
				filename,
				cf_tapelib,
				LF_LIBSEP,
				t->ta_number);
			if (!nswitch)
				display(scratchline);
			if (t->ta_status == LF_UNLABELD) {
				(void) strcat(scratchline, " ");
				(void) strcat(scratchline,
					gettext("[unlabeled]"));
			}
			(void) puts(scratchline);
			log("%s: %s%c%05.5d\n",
				"Prepare",
				cf_tapelib, LF_LIBSEP, t->ta_number);
		}
	}

	if (cf_tapesup != 0 && tapesfound != schedtapes) {
		if (schedtapes - tapesfound == 1)
			(void) sprintf(scratchline,
			    gettext("[%s/%s] MIGHT NEED 1 MORE TAPE"),
				hostname, filename);
		else
			(void) sprintf(scratchline,
			    gettext("[%s/%s] MIGHT NEED %d MORE TAPES"),
				hostname, filename, schedtapes - tapesfound);
		if (!nswitch)
			display(scratchline);
		(void) puts(scratchline);
	}
	dodump();
	clear_tapes();
	(void) fclose(infid);
	if (nswitch == 0) {
		(void) unlink(lfilename);

		/* inform log and mail people of problem filesystems: */
		nbad = 0;
		for (d = cf_tapeset[thisdumpset]->ts_devlist; d; d = d->dc_next)
			if ((d->dc_filesys[0] == '*') ||
				((d->dc_filesys[0] == '-') &&
				d->dc_log->s_string[0] != '\0'))
					nbad++;
		if (nbad) {
			/* log them: */
			if (nbad == 1)
				log(gettext(
				    "%s: 1 undumped file system:\n"),
					"Notdumped#");
			else
				log(gettext(
				    "%s: %d undumped file systems:\n"),
					"Notdumped#", nbad);
			for (d = cf_tapeset[thisdumpset]->ts_devlist;
			    d; d = d->dc_next)
				if (d->dc_filesys[0] == '*')
					log("Notdumped: %s\n",
					    &d->dc_filesys[1]);
			/* mail them out: */
			if (ncf_notifypeople) {
				char	*p = mailcommand;
				FILE	*cmd;

				p[0] = '\0';
				p = strappend(p, "/usr/bin/mail");

				for (i = 0; i < ncf_notifypeople; i++) {
					p = strappend(p, " ");
					p = strappend(p, cf_notifypeople[i]);
				}
				cmd = popen(mailcommand, "w");
				if (cmd == NULL) {
					log(gettext(
					    "%s: Cannot execute `%s'\n"),
						progname, mailcommand);
				} else {
					(void) fprintf(cmd, gettext(
					    "%s: DUMP PROBLEMS FROM %s/%s\n\n"),
					    "Subject", hostname, filename);

					/* show the dumps that failed */
					for (d =
					    cf_tapeset[thisdumpset]->ts_devlist;
					    d; d = d->dc_next)
						if ((d->dc_filesys[0] == '*') ||
						    ((d->dc_filesys[0]
						    == '-') &&
						    (d->dc_log->s_string[0]))) {
							(void) fprintf(cmd,
							    gettext(
							"Dump of %s failed"),
							    fullname(hostname,
							    &d->dc_filesys[1]));
						    if (d->dc_log->s_string[0])
							(void) fprintf(cmd,
							"; Reason:\n%s\n",
							d->dc_log->s_string);
						    else
							fputs("\n", cmd);
						}

					/* show the dumps that succeeded */
					for (d =
					    cf_tapeset[thisdumpset]->ts_devlist;
					    !outofband && d; d = d->dc_next)
						if (d->dc_filesys[0] == '+') {
							(void) fprintf(cmd,
							    gettext(
						    "Dump of %s succeeded\n"),
							    fullname(hostname,
							    &d->dc_filesys[1]));
						}

					if ((int)strlen(sectapes->s_string) > 1)
						(void) fprintf(cmd, "%s",
							sectapes->s_string);
					if (outofband)
						mail_tape_table(cmd);
					(void) fprintf(cmd, ".\n");
					(void) pclose(cmd);
				}
			}
		} else {
			if (ncf_notifypeople) {
				char	*p = mailcommand;
				FILE	*cmd;
				p[0] = '\0';
				p = strappend(p, "/usr/bin/mail");
				for (i = 0; i < ncf_notifypeople; i++) {
					p = strappend(p, " ");
					p = strappend(p, cf_notifypeople[i]);
				}
				cmd = popen(mailcommand, "w");
				if (cmd == NULL) {
					log(gettext(
					    "%s: Cannot execute `%s'\n"),
						progname, mailcommand);
				} else {
					(void) fprintf(cmd, gettext(
					    "%s: DUMP OK: %s/%s\n\n"),
					    "Subject", hostname, filename);
					(void) fprintf(cmd, gettext(
					    "Successful completion %s/%s\n"),
					    hostname, filename);
					if ((int)strlen(sectapes->s_string) > 1)
						(void) fprintf(cmd, "%s",
							sectapes->s_string);
					if (outofband)
						mail_tape_table(cmd);
					(void) fprintf(cmd, ".\n");
					(void) pclose(cmd);
				}
			}
		}
		log(gettext("Finish\n"));
		if (logfile)
			(void) fclose(logfile);

		(void) sprintf(scratchline, gettext("[%s/%s] Finished"),
			hostname, filename);
		display(scratchline);
	}
	exit(0);
#ifdef lint
	return (0);
#endif
}

/*
 * 50 characters in the filesystem field.
 * 16 characters in the tape label field.
 * 6 characters in the file number field.
 */
static char *single_line =
"+--------------------------------------------------+----------------+------+";
static char *double_line =
"+==================================================+================+======+";
static char *fifty_spaces =
"                                                  ";
static char *sixteen_spaces =
"                ";
static char *six_spaces =
"      ";

mail_tape_table(FILE *m)
{
	struct oob_mail *tomp, *comp;
	char line[MAXLINELEN];

	fprintf(m, "\n%s\n|", single_line);
	sprintf(line, "%s", gettext("File system"));
	tape_table_field(m, line, 50);
	sprintf(line, "%s", gettext("Tape"));
	tape_table_field(m, line, 16);
	sprintf(line, "%s", gettext("File #"));
	tape_table_field(m, line, 6);
	fprintf(m, "\n%s\n", double_line);

	for (tomp = outofband_mailp; tomp; tomp = tomp->om_next) {
		fprintf(m, "|");
		tape_table_field(m, tomp->om_fs->s_string, 50);
		sprintf(line, "%s:%05.5d", cf_tapelib, tomp->om_tapeid);
		tape_table_field(m, line, 16);
		fprintf(m, "%6d|\n", tomp->om_file);
		fprintf(m, "%s\n", single_line);
		for (comp = tomp->om_continue; comp; comp = comp->om_next) {
			fprintf(m, "|");
			sprintf(line, "%s", gettext("     Continued on:"));
			tape_table_field(m, line, 50);
			sprintf(line, "%s:%05.5d", cf_tapelib, tomp->om_tapeid);
			tape_table_field(m, line, 16);
			fprintf(m, "%6d|\n", tomp->om_file);
		}
	}
}

tape_table_field(FILE *m, char *f, int width)
{
	int n;

	switch (width) {
	case 50:
		n = strlen(f);
		if (n >= 50) {
			fprintf(m, "%50.50s|", f);
		} else {
			fprintf(m, "%s", f);
			fprintf(m, "%s|", &fifty_spaces[n]);
		}
		break;
	case 16:
		n = strlen(f);
		if (n >= 16) {
			fprintf(m, "%16.16s|", f);
		} else {
			fprintf(m, "%s", f);
			fprintf(m, "%s|", &sixteen_spaces[n]);
		}
		break;
	case 6:
		n = strlen(f);
		if (n >= 6) {
			fprintf(m, "%6.6s|", f);
		} else {
			fprintf(m, "%s", f);
			fprintf(m, "%s|", &six_spaces[n]);
		}
		break;
	}
}

void
clear_tapes()
{
	struct tapes_f *t;

	/* re-scratch those tapes we did not use: */
	tl_lock();
	for (t = tapes_head.ta_next; t != &tapes_head; t = t->ta_next) {
		struct tapedesc_f tapedesc;
		if (t->ta_status == LF_UNLABELD ||
		    t->ta_status == LF_NEWLABELD) {
			tl_read(t->ta_number, &tapedesc);
			tapedesc.t_status =
				(tapedesc.t_status & ~TL_STATMASK) | TL_SCRATCH;
			tl_write(t->ta_number, &tapedesc);
		}
	}
	tl_unlock();
}

void
unlinklpfile(void)
{
	(void) unlink(auxstatusfile);
}

void
fixtape(dev, what)
	char	*dev;
	char	*what;		/* e.g., offline, rewind */
{
	struct mtget mt;
	struct mtop iocmd;
	char	*device;
	int	t, remote;

	if (strcmp(what, "rewind") == 0)
		iocmd.mt_op = MTREW;
	else if (strcmp(what, "offline") == 0)
		iocmd.mt_op = MTOFFL;
	else {
		log(gettext("%s: Unknown device command: %s\n"),
			progname, what);
		return;
	}
	iocmd.mt_count = (daddr_t) 1;
	remote = 0;
	split(strdup(dev), ":");
	switch (nsplitfields) {
	case 2:		/* name + drive name */
		device = splitfields[1];
		if (strcmp(hostname, splitfields[0]) != 0) { /* remote host */
			struct passwd *pwd;

			remote = 1;
			if (cf_rdevuser && cf_rdevuser[0] && (geteuid() == 0 ||
			    ((pwd = getpwnam(cf_rdevuser)) != 0 &&
			    pwd->pw_uid == geteuid()))) {
				char userathost[LINEWID+BCHOSTNAMELEN+2];
				/*
				 * Execute remote command as rdevuser
				 * if running as root or as that user
				 * by supplying rmthost() user@host
				 */
				(void) sprintf(userathost, "%s@%s",
					cf_rdevuser,
					splitfields[0]);
				t = rmthost(userathost, 1024);
			} else
				/* execute as root */
				t = rmthost(splitfields[0], 1024);
			if (t == 0) {
				log(gettext(
			    "%s: Cannot connect to remote tape host `%s'\n"),
					progname, splitfields[0]);
				return;
			}
			if (rmtopen(device, O_RDONLY) < 0) {
				log(gettext(
					"%s: Cannot open remote device `%s'\n"),
					progname, device);
				return;
			}
		} else {	/* local host */
			t = open(device, O_RDONLY);
			if (t < 0) {
				log(gettext(
				    "%s: Cannot open device `%s'\n"),
					progname, device);
				return;
			}
		}
		break;
	case 1:		/* local tape drive */
		device = splitfields[0];
		t = open(device, O_RDONLY);
		if (t < 0) {
			log(gettext("%s: Cannot open device `%s'\n"),
				progname, device);
			return;
		}
		break;
	default:
		log(gettext("%s: Cannot parse tape drive name `%s'\n"),
			progname, dev);
		return;
	}
	if ((remote ? rmtstatus(&mt) : ioctl(t, MTIOCGET, &mt)) >= 0) {
		/*
		 * Only do next operations if we are
		 * really talking to a tape device
		 */
		if ((remote ? rmtioctl((int)iocmd.mt_op, 1) :
		    ioctl(t, MTIOCTOP, &iocmd)) < 0) {
			log(gettext("%s: Cannot %s device `%s'\n"),
				progname, what, device);
			return;
		}
		if (remote)
			rmtclose();
		else
			(void) close(t);
		if (debug)
			(void) fprintf(stderr, gettext("%s: DONE: %s %s\n"),
				"fixtape", "mt", what);
	} else if (debug)
		(void) fprintf(stderr, gettext(
			"%s: `%s' is not a tape device\n"), "fixtape", device);
}
