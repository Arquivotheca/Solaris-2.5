#ident	"@(#)globs.c 1.19 92/09/22"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include "structs.h"
#include <netdb.h>

char	remote[MAXPATHLEN];	/* remote machine name for this dump */
int	usehsmroot;		/* uses /opt/SUNWhsm rather than /usr/etc */
int	keepdays;		/* how long to keep this tape */
int	keepminavail;		/* how many to keep of this tape type */
int	keeptil;		/* keep this tape through this cycle */
struct string_f *dumpcommand;	/* dump command */
int	thisdumpset;		/* the parallel set we're doing */

struct tapes_f tapes_head = {0, "", '\0', &tapes_head, &tapes_head};

int	debug;			/* debug level */
int	fswitch;		/* force: retry bad dumps */
int	nswitch;		/* not do -- just report */
int	Nswitch;		/* not do -- just report briefly */
FILE	*lfilefid;		/* for I/O on lfile */
FILE	*logfile;		/* logging */

char	filename[MAXPATHLEN];
char	newfilename[MAXPATHLEN];
char	hostname[BCHOSTNAMELEN];
char	opserver[BCHOSTNAMELEN];

FILE	*infid;			/* config file */
char	*configdir;		/* configuration directory */
struct inline_f *inlines = 0;
int	ninlines = 0;		/* how many lines total */
int	maxinlines = 0;		/* how many lines max */

char	*cf_filename;

char	*cf_tapelib;
char	*cf_dumplib;

char	**cf_dumpdevs;
int	ncf_dumpdevs;
int	maxcf_dumpdevs;

int	cf_tapesup;

int	cf_maxset;		/* maximum set number */

struct keep_f *cf_keep;
int	ncf_keep;
int	maxcf_keep;

/*
 * cron - default setup calls for a disabled cron entry, configured to
 * run Tue-Sat morning at 2am, with tape reminder sent at 4pm
 * the day before.
 */
struct cron_f cf_cron = {
	0, 200, 1600,
	0, 1, 1, 1, 1, 1, 0,
	0, 1, 0, 0, 0, 0, 0
};

int	cf_mastercycle;		/* how many mastercycles complete */
int	cf_mastercycleline;

char	**cf_notifypeople;
int	ncf_notifypeople;
int	maxcf_notifypeople;
char	*cf_rdevuser;		/* for rmt protocol */

int	cf_longplay;		/* leave tape in drive overnight */

struct tapeset_f *cf_tapeset[MAXDUMPSETS];
char	**splitfields;
int	maxsplitfields;
int	nsplitfields;

char	lfilename[300];		/* name of the temporary lfile */
char	rlfilename[300];	/* name of the (remote) temporary lfile */
int	tapelibfid;		/* open fid for tape library */
int	tapeliblen = -1;	/* number of tapes in tape lib (numbered as */
				/* [0..tapeliblen-1]) */
int	reposition;		/* 1 -> must physically reposition tape */
char	auxstatusfile[MAXPATHLEN];
int	tapeposofnextfile;	/* tape position of this file */

char	confdir[MAXPATHLEN];	/* where config files live */

char	*configfilesecurity = "### dumpconfigfile ###\n";
char	*tapelibfilesecurity = "### tapelibfile ###\n";
int	lentapelibfilesecurity;
int	cf_blockfac;		/* blocking factor for dump tapes */
char	*progname;		/* die() prints this upon termination */
int	dontoffline;		/* flag for easier debugging */
int	cachestart = -1;	/* -1 -> not being used */

struct tapedesc_f tapecache[NCACHEDTAPES];	/* cache for searching tapes */
int	thisisedit;		/* -> dumped is being run; warnings easier */
int	curseson;		/* curses is in use */
struct string_f *sectapes;	/* which tapes have security info */

int	lockfid;		/* fd of current lock on config file */
