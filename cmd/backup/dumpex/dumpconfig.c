#ident	"@(#)dumpconfig.c 1.55 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include <config.h>
#include <lfile.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/vnode.h>
#include <sys/mtio.h>
#ifdef USG
#include <sys/fs/ufs_inode.h>
#else
#include <ufs/inode.h>
#endif
#include <protocols/dumprestore.h>

#undef getfs

#ifdef USG
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#endif

#define	DUMPCONF	"dump.conf"
#define	DUMPCONFCLIENT	"dump.conf.client"
#define	MAXTAPES	100

int	nfilesystems;

static char	*intromesg;
static char	*intromesgprompt;
static char	*filenamemesg;
static char	*filenameprompt;
static char	*onedumpdevsmesg;
static char	*manydumpdevsmesg;
static char	*dumpdevsmesg;
static char	*dumpdevsprompt;
static char	*labelmesg;
static char	*labelmesgprompt;
static char	*editmesg;
static char	*editmesgprompt;
static char	*wrapupmesg;

#ifdef __STDC__
static void inittext(void);
static void dotapes(void);
static void addkeep(int, int, int, int);
static void maketapelib(char *);
static void makedump_conf(void);
static void usage(void);
static char *getconfigname(const char *, const char *);
static char *getline(const char *, const char *);
static int etccheck(const char *);
static void putconfig(const char *, const char *, const char *, const char *,
	int, int, int);
#else
static void inittext();
static void dotapes();
static void addkeep();
static void maketapelib();
static void makedump_conf();
static void usage();
static char *getconfigname();
static char *getline();
static int etccheck();
static void putconfig();
#endif

static void
#ifdef __STDC__
inittext(void)
#else
inittext()
#endif
{
	intromesg = gettext(
"\nDumpconfig is used to create an initial dump configuration for your site.\n\
Dumpconfig may also:\n\
     o  Ask for a list of tape devices to be used.\n\
     o  Ask for the location of the online dump database.\n\
     o  Create the /etc/init.d/hsm startup file.\n\
     o  Startup the daemons required by this package.\n\
     o  Create a simple %s/%s configuration file.\n\
     o  Allow you to pre-label your tapes for this configuration.\n\
     o  Invoked dumped(1M) to allow you to edit this configuration file.\n");

	intromesgprompt = gettext(
"Do you wish to proceed? (yes/no) [y]: ");

	filenamemesg = gettext(
"\n\nChoose a name for this configuration file (e.g., bedrock).  This program\n\
will append `_t' for the tape library file (e.g., bedrock_t).  You will\n\
specify this configuration name when you invoke dumpex(1M) to backup\n\
the file systems listed in the configuration.\n");

	filenameprompt = gettext(
"Please enter a new name for this configuration file [%s]: ");

	onedumpdevsmesg = gettext(
"\n\nUsing %s as the default tape device.  You can change this\n\
default by invoking the editor program, dumped(1M), later on in this session.\n");

	manydumpdevsmesg = gettext(
"\n\nThis system currently has %d tape devices attached:\n");

	dumpdevsmesg = gettext(
"\n\nYou must enter at least one tape device to be used by this configuration.\n\
The BSD NON-REWINDING form of the device must be specified (i.e., use\n\
/dev/rmt/0bn instead of /dev/rmt/0).\n");

	dumpdevsprompt = gettext(
"Please enter a tape device (enter only `return' when\n\
no devices remain): ");

	labelmesg = gettext(
"\n\nYou are strongly encouraged to write electronic labels on your tapes.\n\
You can do this easily by answering \"yes\" to the following prompts,\n\
loading a new tape between prompts.  It is best to label at least 5-10\n\
tapes before your first invocation of dumpex(1M).\n");

	labelmesgprompt = gettext(
"Label the tape in `%s' as `%s' now? (yes/no) [y]: ");

	editmesg = gettext(
"\n\nA default dump configuration file has been created for you.  It is called\n\
`%s' and is ready to backup all the local file systems on this\n\
host (`%s').  You may edit this configuration at any time\n\
with the dumped(1M) command.\n");

	editmesgprompt = gettext(
"Would you like to edit this dump configuration now? (yes/no) [n]: ");

	wrapupmesg = gettext(
"\n\nConfiguration for `%s' is complete.\n\
\n\
To execute backups:                 # dumpex %s\n\
To execute backups on a new tape:   # dumpex -s %s\n\
To see which tapes to mount:        # dumpex -n %s\n\
To label new tapes:                 # dumptm -C %s -w <number>\n\
To edit this configuration:         # dumped %s\n\
To schedule dumpex execution:       # dumped %s  (choose option 'd')\n");
}

static void
usage(void)
{
	(void) fprintf(stderr, gettext("Usage: %s\n"), progname);
}

static int	dumptype;
static int	dumplen;
static int	dumpsublen;
static char	*dumpstagger;
static char	*longplay;

static char	*config1;
static char	*tapelib1;

main(argc, argv)
	char	*argv[];
{
	char	filepath[MAXPATHLEN];
	struct stat statbuf;
	struct passwd *pw;
	char	*p;
	char	*basename, *name;
	char	ans[MAXLINELEN];
	char	anscratch[2];
	char	scratch[MAXLINELEN];
	int	dscratch;	/* scratch tapes in daily library */
	char	*yes = gettext("yes");
	char	*no = gettext("no");
	char	*yorn = gettext("nNyY");
	int	i, exitstat;

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

	for (argc--, argv++; argc; argc--, argv++) {
		if (argv[0][0] == '-') {
			if (argv[0][2] != '\0') {
				(void) printf(gettext(
			"All switches to %s are single characters\n"),
					progname);
				usage();
				exit(1);
			}
			switch (argv[0][1]) {
			case 'd':	/* debug */
				debug = 1;
				continue;
			default:
				(void) printf(gettext("Switch %c invalid\n"),
					argv[0][1]);
				usage();
				exit(1);
			}
		}
		break;
	}
	if (argc != 0) {
		usage();
		exit(1);
	}
	checkroot(0);
	(void) sprintf(confdir, "%s/dumps", gethsmpath(etcdir));
	if (stat(confdir, &statbuf) < 0 && mkdir(confdir, 0700) < 0) {
		char dumpsetup[MAXPATHLEN];

		(void) sprintf(dumpsetup, "%s/dumpsetup", gethsmpath(libdir));
		if (System(dumpsetup) != 0)
			die(gettext(
			    "Cannot create configuration directory `%s'\n"),
			    confdir);
	}
	if (chdir(confdir) == -1)
		die(gettext("Cannot chdir to %s; run this program as root.\n"),
			confdir);
	(void) umask(0006);

	if (sysinfo(SI_HOSTNAME, hostname, BCHOSTNAMELEN) == -1) {
		(void) printf(gettext("Cannot determine this host's name\n"));
		exit(1);
	}

	inittext();

	/*
	 * intro
	 */
	(void) printf(intromesg, gethsmpath(etcdir), DUMPCONF);
	p = getline(intromesg, intromesgprompt);
	if (strncasecmp(p, no, 1) == 0) {
		(void) printf(gettext("\n%s: exiting.\n"), progname);
		exit(0);
	}

	/*
	 * config filename
	 */
	basename = getconfigname(filenamemesg, filenameprompt);

	/*
	 * tapelibrary -- mechanical
	 */
	(void) sprintf(scratch, "%s_t", basename);
	tapelib1 = cf_tapelib = strdup(scratch);

	/*
	 * dump library -- mechanical
	 */
	cf_dumplib = strdup(hostname);

	/*
	 * config1 is the config filename (?)
	 */
	config1 = strdup(cf_filename);

	/*
	 * dump devices
	 */
	maxcf_dumpdevs = GROW;
	/*LINTED [alignment ok]*/
	cf_dumpdevs = (char **) checkalloc(GROW * sizeof (char *));
	ncf_dumpdevs = 0;
	dotapes();

	/*
	 * blocking factor -- mechanical
	 */
	cf_blockfac = 112;

	/*
	 * tapes up -- mechanical
	 */
	cf_tapesup = 0;

	/*
	 * notify people
	 */
	/*LINTED [alignment ok]*/
	cf_notifypeople = (char **) checkalloc(GROW * sizeof (char *));
	maxcf_notifypeople = GROW;
	ncf_notifypeople = 0;
	if (((name = getenv("LOGNAME")) || (name = getenv("USER")) ||
	    (name = getlogin())) && *name) {
		cf_notifypeople[ncf_notifypeople++] = strdup(name);
	} else if ((pw = (struct passwd *) getpwuid(getuid())) != NULL) {
		cf_notifypeople[ncf_notifypeople++] = strdup(pw->pw_name);
	} else {
		cf_notifypeople[ncf_notifypeople++] = strdup("root");
	}

	/*
	 * remote device user -- none
	 */
	cf_rdevuser = 0;

	/*
	 * longplay -- mechanical
	 */
	longplay = strdup(yes);

	/*
	 * n dumpsets -- mechanical
	 */
	cf_maxset = 1;

	/*
	 * level pattern
	 */
	dumptype = LEV_FULL_TRUEINCRx2;

	/*
	 * sequence length
	 */
	dumplen = 10;
	dumpsublen = 5;

	/*
	 * dump stagger
	 */
	dumpstagger = strdup(no);

	/*
	 * full dump keep lens
	 */
	/*LINTED [alignment ok]*/
	cf_keep = (struct keep_f *) checkalloc(GROW * sizeof (struct keep_f));
	maxcf_keep = GROW;
	addkeep('0', 1, 60, 0);
	addkeep('5', 1, 30, 0);
	addkeep('x', 1, 14, 0);

	/*
	 * file systems to dump
	 */
	probefs(hostname, hostname, addfs);

	/*
	 * rc.local / init.d/hsm:
	 */
	if (stat("/etc/init.d/hsm", &statbuf) == -1) {
		(void) printf("\n\n");	/* some whitespace, please */
		(void) sprintf(scratch, "%s/dumpconfrc", gethsmpath(libdir));
		(void) system(scratch);
	}

	/*
	 * Scratch tapes -- mechanical
	 */
	dscratch = 20;

	/*
	 * Generate config file
	 */
	putconfig(config1, tapelib1, longplay, dumpstagger,
		dumptype, dumplen, dumpsublen);

	/*
	 * make the dump tape database:
	 */
	(void) sprintf(filepath, "%s/%s", confdir, tapelib1);
	maketapelib(tapelib1);

	/*
	 * make /etc/dump.conf
	 */
	makedump_conf();

	/*
	 * scratch the tapes
	 */
	if (dscratch >= 1) {
		int	status;

		(void) sprintf(ans,
			"%s/dumptm -C %s -s -r 0-%d >/dev/null",
			gethsmpath(sbindir), config1, dscratch - 1);
		status = System(ans);
		if (status) {
			(void) printf(gettext(
				"\nUnsuccessful exit status: %d\n"),
				status >> 8);
			(void) printf(gettext(
				"Try the command by hand: %s\n"), ans);
		}
	}

	/*
	 * Label tapes?
	 */
	(void) fputs(labelmesg, stdout);
	for (i = 0; /* ever */; i++) {
		char temp[5000];
		char tape[LBLSIZE+1];

		(void) sprintf(tape, "%s%c%05d", tapelib1, LF_LIBSEP, i);
		(void) sprintf(temp, labelmesgprompt, cf_dumpdevs[0], tape);
		p = getline(labelmesg, temp);
		if (strncasecmp(p, no, 1) == 0)
			break;
		(void) sprintf(temp, "%s/dumptm -C %s -w %d -D %s",
			gethsmpath(sbindir), config1, i, cf_dumpdevs[0]);
		if (system(temp) == 0) {
			(void) sprintf(temp,
				"/usr/bin/mt -f %s offline >/dev/null 2>&1",
				cf_dumpdevs[0]);
			(void) system(temp);
		} else {
			(void) printf(gettext(
			    "Failed to label tape with the command:\n\t%s\n"),
			    temp);
			i--;
		}
	}

	/*
	 * dumped
	 */
	(void) printf(editmesg, config1, hostname);
	do {
		p = getline(editmesg, editmesgprompt);
		if (strncasecmp(p, yes, 1) == 0) {
			char dumped_prog[MAXPATHLEN];

			(void) sprintf(dumped_prog, "%s/dumped %s",
				gethsmpath(sbindir), config1);
			exitstat = system(dumped_prog);
		}
	} while (strncasecmp(p, yes, 1) == 0 && exitstat != 0);

	/*
	 * DONE!
	 */
	(void) printf(wrapupmesg, config1, config1, config1, config1,
		config1, config1, config1);
	(void) printf("\n");
	exit(0);
#ifdef lint
	return (0);
#endif
}

static void
dotapes(void)
{
	char tape[MAXPATHLEN];
	char *local_tapes[MAXTAPES];
	int i, fd, nlocal_tapes;
	struct stat sb;

	/* see if there are any local tape devices */
	for (nlocal_tapes = i = 0; i < MAXTAPES; i++) {
		(void) sprintf(tape, "/dev/rmt/%dbn", i);
		if (stat(tape, &sb) != -1 && S_ISCHR(sb.st_mode)) {
			fd = open(tape, O_RDONLY);
			if (fd == -1) {
				if (errno != EBUSY && errno != EIO &&
				    errno != EPERM)
					continue;
			} else
				(void) close(fd);
			local_tapes[nlocal_tapes++] = strdup(tape);
		}
	}
	if (nlocal_tapes == 1) {
		(void) printf(onedumpdevsmesg, local_tapes[0]);
		cf_dumpdevs[0] = local_tapes[0];
		ncf_dumpdevs = 1;
		return;
	}

	/* found more than one tape; show the list */
	if (nlocal_tapes > 1) {
		(void) printf(manydumpdevsmesg, nlocal_tapes);
		for (i = 0; i < nlocal_tapes; i++)
			(void) printf("\t%s\n", local_tapes[i]);
	}

	/* either found 0 or more than one tape device; just prompt */
	(void) fputs(dumpdevsmesg, stdout);
	for (;;) {
		char	*yyy;

		yyy = getline(dumpdevsmesg, dumpdevsprompt);
		if (yyy[0] == '\0' && ncf_dumpdevs == 0) {
			(void) printf(gettext(
			    "\nYou must enter at least one tape device.\n"));
			(void) printf(gettext(
	"Remote devices may be specified as `remote-host:/dev/rmt/0bn'.\n"));
			continue;
		}
		if (yyy[0] == '\0')
			break;
		/* do consistency checks on local tape devices */
		if (index(yyy, ':') == NULL) {
			if (stat(yyy, &sb) == -1) {
				(void) printf(gettext(
				    "\nTape device %s not added: %s\n"), yyy,
				    strerror(errno));
				continue;
			}
			if (!S_ISCHR(sb.st_mode)) {
				(void) printf(gettext(
		    "\nTape device %s is not character device; not added\n"),
				    yyy);
				continue;
			}
			fd = open(yyy, O_RDONLY);
			if (fd == -1) {
				if (errno != EBUSY && errno != EIO &&
				    errno != EPERM) {
					(void) printf(gettext(
				    "\nCannot open %s; not added: %s\n"),
						yyy, strerror(errno));
					continue;
				}
			} else {
				/* make sure it is a tape device */
				struct mtget mt_status;

				if (ioctl(fd, MTIOCGET, (char *) &mt_status)
				    < 0) {
					printf(gettext(
		"\nDevice %s does not appear to be a tape device; not added\n"),
						yyy);
					(void) close(fd);
					continue;
				}
				(void) close(fd);
			}
		}
		/* XXX - do some basic verification of the device here */
		if (ncf_dumpdevs >= maxcf_dumpdevs) {
			maxcf_dumpdevs += GROW;
			cf_dumpdevs =
				(char **) checkrealloc((char *) cf_dumpdevs,
				/*LINTED [alignment ok]*/
				maxcf_dumpdevs * sizeof (char *));
		}
		cf_dumpdevs[ncf_dumpdevs++] = yyy;
	}
}

static void
addkeep(level, mult, days, minavail)
	int	level;
	int	mult;
	int	days;
	int	minavail;
{
	while (ncf_keep >= maxcf_keep) {	/* dynamic growth */
		maxcf_keep += GROW;
		cf_keep = (struct keep_f *) checkrealloc((char *) cf_keep,
			/*LINTED [alignment ok]*/
			maxcf_keep * sizeof (struct keep_f));
	}
	cf_keep[ncf_keep].k_level = (char) level;
	cf_keep[ncf_keep].k_multiple = mult;
	cf_keep[ncf_keep].k_days = days;
	cf_keep[ncf_keep++].k_minavail = minavail;
}

static void
maketapelib(name)
	char	*name;
{
	int	fid;
	struct stat statbuf;
	int	seclen;

	if (stat(name, &statbuf) != -1)
		return;
	fid = creat(name, 0660);
	if (fid == -1) {
		(void) printf(gettext(
		    "\n    Error: Cannot create tape database file `%s' -- "),
			name);
		(void) printf(gettext(
			"check permissions and use dumptm to create.\n"));
		(void) printf(gettext("    Continuing...\n"));
		return;
	}
	seclen = strlen(tapelibfilesecurity);
	if (write(fid, tapelibfilesecurity, seclen) != seclen) {
		(void) printf(gettext(
		    "\n    Error: Cannot write tape database file `%s' -- "),
			name);
		(void) printf(gettext(
			"check permissions and use dumptm to create.\n"));
		(void) printf(gettext("    Continuing...\n"));
		return;
	}
	if (close(fid) == -1) {
		(void) printf(gettext(
		    "\n    Error: Cannot close tape database file `%s' -- "),
			name);
		(void) printf(gettext(
			"check permissions and use dumptm to create.\n"));
		(void) printf(gettext("    Continuing...\n"));
		return;
	}
}

static void
makedump_conf(void)
{
	FILE	*fid;
	struct stat statbuf;
	char	*dumpconf, *clientconf;
	char	*etc = gethsmpath(etcdir);

	dumpconf = checkalloc(strlen(etc) + strlen(DUMPCONF) + 2);
	clientconf = checkalloc(strlen(etc) + strlen(DUMPCONFCLIENT) + 2);

	(void) sprintf(dumpconf, "%s/%s", etc, DUMPCONF);
	(void) sprintf(clientconf, "%s/%s", etc, DUMPCONFCLIENT);

	/* first, do the real dump.conf file */
	if (stat(dumpconf, &statbuf) != -1)
		return;

	fid = fopen(dumpconf, "w");
	if (fid == NULL) {
		(void) printf(gettext("\nError: cannot create %s\n"), dumpconf);
		(void) printf(gettext(
		    "Error: You will have to create this file by hand\n"));
		return;
	}
	if (fprintf(fid,
	    "# Dump configuration file\n\noperd\t\t%s\ndatabase\t%s\n",
	    hostname, hostname) == EOF) {
		(void) printf(gettext(
			"\nError: write to %s failed\n"), dumpconf);
		(void) printf(gettext(
		    "Error: You will have to create this file by hand\n"));
		(void) fclose(fid);
		return;
	}
	(void) fclose(fid);
	if (chmod(dumpconf, 0664) < 0) {
		(void) printf(gettext(
			"\nError: chmod of %s failed\n"), dumpconf);
		(void) printf(gettext(
	    "Error: You may want to change its mode to 644 by hand\n"));
		return;
	}
	(void) printf(gettext("The %s file has been created.\n"), dumpconf);

	/* now do the client configuration file */
	if (stat(clientconf, &statbuf) != -1)
		return;

	fid = fopen(clientconf, "w");
	if (fid == NULL)
		return;
	if (fprintf(fid,
	    "# Dump configuration file\n\noperd\t\t%s\ndatabase\t%s\n",
	    hostname, hostname) == EOF) {
		(void) fclose(fid);
		return;
	}
	(void) fclose(fid);
	(void) chmod(clientconf, 0664);
}

static char *
#ifdef __STDC__
getconfigname(
	const char *mesg,
	const char *prompt)
#else
getconfigname(mesg, prompt)
	char *mesg;
	char *prompt;
#endif
{
	char	defaultname[MAXLINELEN];
	char	temp[10000];		/* for formatted messages */
	char	*basename;

#define	EXTENSIONLEN 2		/* strlen("_t") */
	(void) strcpy(defaultname, hostname);
	if ((int)strlen(defaultname) > MAXLIBLEN - EXTENSIONLEN)
		defaultname[MAXLIBLEN - EXTENSIONLEN] = '\0';

	(void) sprintf(temp, prompt, defaultname);

	(void) fputs(mesg, stdout);
	for (;;) {
		basename = getline(mesg, temp);
		if (basename[0] == '\0')
			basename = defaultname;
		cf_filename = strdup(basename);
		/* length is more restrictive but easier to use later: */
		if (filenamecheck(cf_filename, MAXLIBLEN - EXTENSIONLEN)) {
			(void) printf(gettext(
	"\nThe filename `%s' must contain only letters, numbers, and '_'.\n"),
				cf_filename, confdir);
			(void) printf(gettext(
		"It should be %d characters in length or shorter.\n"),
				MAXLIBLEN - EXTENSIONLEN);
			continue;
		}
		if (etccheck(cf_filename)) {
			(void) printf(gettext(
	"\nThe filename `%s' must not exist in the %s directory.\n"),
				cf_filename, confdir);
			continue;
		}
		break;
	}
	return (basename);
}

static char *
#ifdef __STDC__
getline(const char *mesg,
	const char *prompt)
#else
getline(mesg, prompt)
	char *mesg;
	char *prompt;
#endif
{
	static char in[200];
tryagain:
	if (fputs("\n", stdout) == EOF)
		die(gettext("%s failed (%d)\n"), "fputs", 2);
	if (fputs(prompt, stdout) == EOF)
		die(gettext("%s failed (%d)\n"), "fputs", 3);
	if (fgets(in, sizeof (in), stdin) == NULL) {
		(void) printf(gettext("Unexpected EOF -- terminating\n"));
		exit(1);
	}
	if (in[0] == '?') {
		if (mesg)
			(void) fputs(mesg, stdout);
		goto tryagain;
	}
	chop(in);
	return (strdup(in));
}

static int
#ifdef __STDC__
etccheck(const char *filename)
#else
etccheck(filename)
	char *filename;
#endif
{
	char	filepath[MAXPATHLEN];
	struct stat statbuf;

	if (index(filename, '/') != NULL)
		return (1);
	(void) sprintf(filepath, "%s/%s", confdir, filename);
	if (stat(filepath, &statbuf) != -1 || errno != ENOENT)
		return (1);
	return (0);
}

static void
#ifdef __STDC__
putconfig(
	const char *dumplib,
	const char *tapelib,
	const char *longplay,
	const char *dumpstagger,
	int	dumptype,
	int	dumplen,
	int	dumpsublen)
#else
putconfig(dumplib, tapelib, longplay, dumpstagger,
    dumptype, dumplen, dumpsublen)
	char	*dumplib;
	char	*tapelib;
	char	*longplay;
	char	*dumpstagger;
	int	dumptype;
	int	dumplen;
	int	dumpsublen;
#endif
{
	int	i, j;
	FILE	*out;
	char	*yes = gettext("yes");
	char	*warn = "# NEVER EDIT THIS FILE BY HAND.  USE dumped(1M).\n\n";
	char	*lwarn = gettext(warn);		/* locale-specific version */
	int	dolocale = 0;			/* ...and flag */

	out = fopen(dumplib, "w");
	if (out == (FILE *)0) {
		(void) printf(gettext(
		    "Cannot open configuration file `%s' for writing.\n"),
			dumplib);
		(void) printf(gettext("I can't make one, sorry.\n\n"));
		return;
	}
	(void) fprintf(out, "%s\n", configfilesecurity);
	(void) fprintf(out, warn);
	if (strcmp(warn, lwarn)) {
		dolocale++;
		(void) fprintf(out, lwarn);
	}

	(void) fprintf(out, "tapelib   %s\n", tapelib);
	(void) fprintf(out, "dumpmach  %s\n", hostname);
	(void) fprintf(out, "dumpdevs ");
	for (i = 0; i < ncf_dumpdevs; i++)
		(void) fprintf(out, " %s", cf_dumpdevs[i]);
	(void) fprintf(out, "\n");
	(void) fprintf(out, "block     %d\n", cf_blockfac);
	(void) fprintf(out, "tapesup   %d\n", cf_tapesup);
	if (ncf_notifypeople) {
		(void) fprintf(out, "notify   ");
		for (i = 0; i < ncf_notifypeople; i++)
			(void) fprintf(out, " %s", cf_notifypeople[i]);
		(void) fprintf(out, "\n");
	}
	if (cf_rdevuser && cf_rdevuser[0])
		(void) fprintf(out, "rdevuser  %s\n", cf_rdevuser);
	if (strncasecmp(longplay, yes, 1) == 0)
		(void) fprintf(out, "longplay\n");
	(void) fprintf(out, "cron      %d %d %d",
		cf_cron.c_enable, cf_cron.c_dtime, cf_cron.c_ttime);
	for (i = 0; i < 7; i++)
		(void) fprintf(out, " %d %d", cf_cron.c_ena[i],
			cf_cron.c_new[i]);
	(void) fprintf(out, "\n\n");

	(void) fprintf(out, "#    level   multiple   days   min available\n");
	if (dolocale)
		(void) fprintf(out, gettext(
			"#    level   multiple   days   min available\n"));

	for (i = 0; i < ncf_keep; i++)
		(void) fprintf(out,
		    "keep    %c%9d%9d%10d\n",
			cf_keep[i].k_level,
			cf_keep[i].k_multiple,
			cf_keep[i].k_days,
			cf_keep[i].k_minavail);

	(void) fprintf(out, "\nmastercycle 00000\n");

	for (i = 1; i <= cf_maxset; i++) {
		(void) fprintf(out, "\nset %d\n", i);
		for (j = 0; j < nfilesystems; j++)
			(void) fprintf(out,
			    "fullcycle 00000 +%-25s >%s\n", getfs(j),
			    genlevelstring(dumptype,
				strncasecmp(dumpstagger, yes, 1) == 0 ?
				j % dumplen : 0, dumplen, dumpsublen));
	}
	if (fclose(out) == EOF)
		(void) printf(gettext(
	    "\nWarning: configuration file `%s' was not closed cleanly\n"),
			dumplib);
}
