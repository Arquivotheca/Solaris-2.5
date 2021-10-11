/*
 *		  (c) 1994-1995  Sun Microsystems, Inc.
 *			   All Rights Reserved
 */

#ident	"@(#)main.c	1.6	95/03/01 SMI"

/*
 * COPYRIGHT NOTICE
 * 
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 * 
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED 
 */
/*
 * OSF/1 1.2
 */
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: pax.c,v $ $Revision: 1.2.2.3 $
 * (OSF) $Date: 1992/02/26 23:01:12 $"; 
 * #endif
 */
/* 
 * main.c (renamed from pax.c for simplicity in Makefile messaging structure)
 *
 * DESCRIPTION
 *
 *	Pax is the archiver described in IEEE P1003.2.  It is an archiver
 *	which understands both tar and cpio archives and has a new interface.
 * 
 * 	Currently supports Draft 11 functionality.
 *	The charmap option was new in Draft 11 and removed in 11.1
 * 	The code for this is present but deactivated.
 *
 * SYNOPSIS
 *
 *	pax -[cdnv] [-f archive] [-s replstr] [pattern...]
 *	pax -r [-cdiknuv] [-f archive] [-p string] [-s replstr] 
 *	       [pattern...]
 *	pax -w [-dituvX] [-b blocking] [[-a] -f archive] 
 *	       [-s replstr]...] [-x format] [pathname...]
 *	pax -r -w [-diklntuvX] [-p string] [-s replstr][pathname...] directory
 *
 * DESCRIPTION
 *
 * 	PAX - POSIX conforming tar and cpio archive handler.  This
 *	program implements POSIX conformant versions of tar, cpio and pax
 *	archive handlers for UNIX.  These handlers have defined befined
 *	by the IEEE P1003.2 commitee.
 *
 * COMPILATION
 *
 *	A number of different compile time configuration options are
 *	available, please see the Makefile and config.h for more details.
 *
 * AUTHOR
 *
 *     Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 *
 * Sponsored by The USENIX Association for public distribution. 
 *
 * Copyright (c) 1989 Mark H. Colburn.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice is duplicated in all such 
 * forms and that any documentation, advertising materials, and other 
 * materials related to such distribution and use acknowledge that the 
 * software was developed * by Mark H. Colburn and sponsored by The 
 * USENIX Association. 
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Revision 1.2  89/02/12  10:05:17  mark
 * 1.2 release fixes
 * 
 * Revision 1.1  88/12/23  18:02:23  mark
 * Initial revision
 * 
 */

/* Headers */

#include <unistd.h>
#include <locale.h>
#include "pax.h"

/* Fakeouts */

#define MFPAX "Fake-Pax-MF_PAX-value"

/* Messages */

#define		PAX_BSIZE	"An invalid block size was specified with the -b option."
#define		PAX_PRIV	"An invalid file characteristic was specified with the -p option."
#define		PAX_CHARMAP	"An error occurred trying to read the charmap file."
#define		PAX_FORMAT	"The archive format specified is different from the existing archive."
#define		PAX_TYPE	"Unable to determine the archive format."
#define		PAX_CPIO	"ASCII CPIO format archive\n"
#define		PAX_BINCPIO	"Binary CPIO format archive\n"
#define		PAX_TAR		"USTAR format archive\n"
#define		PAX_BADVAL	"An invalid block size was specified with the -b option."
#define		PAX_U1 		"Usage: %s -[cdnv] [-f archive] [-s replstr] [pattern...]\n"
#define		PAX_U2 		"       %s -r [-cdiknuvy] [-f archive] [-p string] [-s replstr]\n              [pattern...]\n"
#define		PAX_U3 		"       %s -w [-dituvyX] [-b blocking] [[-a] -f archive] [-s replstr]\n               [-x format] [pathname...]\n"
#define		PAX_U4 		"       %s -r -w [-diklntuvyX] [-p string] [-s replstr] [pathname...] directory\n"
#define		PAX_NOTDIR	"The destination directory is not a directory."

#define NO_EXTERN


/* Globally Available Identifiers */

char           *ar_file;		/* File containing name of archive */
char           *mapfile;		/* File containing charmap */
char           *bufend;			/* End of data within archive buffer */
char           *bufstart;		/* Archive buffer */
char           *bufidx;			/* Archive buffer index */

#ifdef OSF_MESSAGES
nl_catd		catd;			/* message catalog pointer */
#endif /* ifdef OSF_MESSAGES */

char	       *lastheader;		/* pointer to header in buffer */
char           *myname;			/* name of executable (argv[0]) */
char          **argv;		        /* global for access by name_* */
int             argc;			/* global for access by name_* */
int             archivefd;		/* Archive file descriptor */
int             blocking;		/* Size of each block, in records */
int             gid;			/* Group ID */
int             head_standard;		/* true if archive is POSIX format */
int             ar_interface;		/* defines interface we are using */
int             ar_format;		/* defines current archve format */
int             mask;			/* File creation mask */
int             ttyf;			/* For interactive queries */
int             uid;			/* User ID */
int		names_from_stdin;	/* names for files are from stdin */
int		exit_status;		/* Exit status of pax */
OFFSET          total;			/* Total number of bytes transferred */
short           f_access_time;		/* Reset access times of input files */
short		f_blocking;		/* blocking option was specified */
short           f_extract_access_time;	/* Reset access times of output files */
short           areof;			/* End of input volume reached */
short           f_dir_create;		/* Create missing directories */
short           f_append;		/* Add named files to end of archive */
short           f_create;		/* create a new archive */
short           f_extract;		/* Extract named files from archive */
short           f_follow_links;		/* follow symbolic links */
short           f_interactive;		/* Interactivly extract files */
short           f_linksleft;		/* Report on unresolved links */
short           f_list;			/* List files on the archive */
short           f_modified;		/* Don't restore modification times */
short           f_verbose;		/* Turn on verbose mode */
short		f_link;			/* link files where possible */
short		f_owner;		/* extract files as the user */
short		f_pass;			/* pass files between directories */
short           f_newer;		/* append files to archive if newer */
short		f_disposition;		/* ask for file disposition */
short           f_reverse_match;	/* Reverse sense of pattern match */
short           f_mtime;		/* Retain file modification time */
short           f_unconditional;	/* Copy unconditionally */
short		f_device;		/* stay on the same device */
short		f_mode;			/* Preserve the file mode */
short		f_no_overwrite;		/* Don't overwrite existing files */
short		f_no_depth;		/* Don't go into directories */
short		f_single_match;		/* Match only once for each pattern */
short		f_charmap;		/* use the charmap file */
time_t          now = 0;		/* Current time */
uint            arvolume;		/* Volume number */
uint            blocksize = BLOCKSIZE;	/* Archive block size */
FILE	       *msgfile;		/* message outpu file stdout/stderr */
Replstr        *rplhead = (Replstr *)NULL;	/*  head of replstr list */
Replstr        *rpltail;		/* pointer to tail of replstr list */
Dirlist        *dirhead = (Dirlist *)NULL;	/* head of directory list */
Dirlist        *dirtail;		/* tail of directory list */
short		bad_last_match = 0;	/* dont count last match as valid */
					/* */
#ifdef FNMATCH_OLD
    int f_fnmatch_old = 1;
#else /* ifdef FNMATCH_OLD */
    int f_fnmatch_old = 0;
#endif /* ifdef FNMATCH_OLD */

#ifdef DEBUG
    /* debug_mode controls the level of support available for debugging.
     * debug_pipe allows us to attach to the pax process from dbx at a
     * sleepy spin-wait location.
     *
     * The usage for the debug option is:
     *
     *    pax -Z [on|pipe] <other syntax>
     *
     * The debug option must be the first option after the verb.  It will
     * be skipped by subsequent getopt() processing.
     */

    const int debug_off  = 0;
    const int debug_on   = 1;
    const int debug_pipe = 2;

    int debug_mode = 0;		/* Initialized to off. */
#endif /* ifdef DEBUG */

/*
 *  Define the offsets and lengths into the tar header in a form that is
 *  useful for debugging.
 */

/*
 *  Offsets:
 */

const int TO_NAME = 0;
const int TO_MODE = 100;
const int TO_UID = 108;
const int TO_GID = 116;
const int TO_SIZE = 124;
const int TO_MTIME = 136;
const int TO_CHKSUM = 148;
const int TO_TYPEFLG = 156;
const int TO_LINKNAME = 157;
const int TO_MAGIC = 257;
const int TO_VERSION = 263;
const int TO_UNAME = 265;
const int TO_GNAME = 297;
const int TO_DEVMAJOR = 329;
const int TO_DEVMINOR = 337;
const int TO_PREFIX = 345;

/*
 *  Offsets:
 */

const int TL_NAME = 100;
const int TL_MODE = 8;
const int TL_UID = 8;
const int TL_GID = 8;
const int TL_SIZE = 12;
const int TL_MTIME = 12;
const int TL_CHKSUM = 8;
const int TL_TYPEFLG = 1;
const int TL_LINKNAME = 100;
const int TL_MAGIC = 6;
const int TL_VERSION = 2;
const int TL_UNAME = 32;
const int TL_GNAME = 32;
const int TL_DEVMAJOR = 8;
const int TL_DEVMINOR = 8;
const int TL_PREFIX = 155;

/* Function Prototypes */

static void 	usage(void);
static OFFSET   pax_optsize(char *);

/* External linkages */

extern int	do_cpio(void);
extern int	do_tar(void);

/* Macros */

/*
 * Swap Bytes.
 */

#define SWAB(n) ((((ushort)(n) >> 8) & 0xff) | (((ushort)(n) << 8) & 0xff00))



/* main - main routine for handling all archive formats.
 *
 * DESCRIPTION
 *
 * 	Set up globals and call the proper interface as specified by the user.
 *
 * PARAMETERS
 *
 *	int argc	- count of user supplied arguments
 *	char **argv	- user supplied arguments 
 *
 * RETURNS
 *
 *	Exits with an appropriate exit code.
 */


void
main(int ac, char **av)
{
    setlocale(LC_ALL, "");

#ifdef OSF_MESSAGES
    catd = catopen(MF_PAX, 0);
#else /* ifdef OSF_MESSAGES */
#if	!defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif

    (void) textdomain(TEXT_DOMAIN);
#endif /* ifdef OSF_MESSAGES */

    argv = av;
    argc = ac;

#ifdef DEBUG
    if (argc > 1 && strcmp(argv[1], "-Z") == 0) {
      switch (argv[2][0]) {
	case 'p':
	debug_mode = debug_pipe;
	break;
	case 'o':
	if (argv[2][1] == 'n') {
	  debug_mode = debug_on;
	  break;
	}

	/* fall through */
	default:
	fprintf(stderr, "unknown debug option: %s\n", argv[2]);
	break;
      }
    }
#endif /* ifdef DEBUG */

    /* strip the pathname off of the name of the executable */
    if ((myname = strrchr(argv[0], '/')) != (char *)NULL) {
	myname++;
    } else {
	myname = argv[0];
    }

    /* get all our necessary information */
    mask = umask(0);
    (void) umask(mask);		/* Draft 11 - umask affects extracted files */
    uid = getuid();
    gid = getgid();
    now = time((time_t *) 0);

    /* open terminal for interactive queries */
    ttyf = open_tty();

    if (strcmp(myname, "tar") == 0) {
	do_tar();
    } else if (strcmp(myname, "cpio") == 0) {
	do_cpio();
    } else {
	do_pax();
    }
    exit(exit_status);
    /* NOTREACHED */
}



/* do_pax - provide a PAX conformant user interface for archive handling
 *
 * DESCRIPTION
 *
 *	Process the command line parameters given, doing some minimal sanity
 *	checking, and then launch the specified archiving functions.
 *
 * PARAMETERS
 *
 * RETURNS
 *
 *    Normally returns 0.  If an error occurs, -1 is returned 
 *    and state is set to reflect the error.
 *
 */


int
do_pax(void)
{
    int		c;
    char	*dirname;
    Stat	st;
    char	*string;
    int		x_format = 0;	/* format as specified via the -x option */
    int		act_format; /* the actual format of existing archive */

    /* default input/output file for PAX is STDIN/STDOUT */

    ar_file = "-";

    /*
     * set up the flags to reflect the default pax interface.  Unfortunately
     * the pax interface has several options which are completely opposite
     * of the tar and/or cpio interfaces...
     */

    ar_format = TAR;	/* default interface if none given for -w */
    ar_interface = PAX;
    blocking = 0;
    blocksize = 0;
    f_append = 0;
    f_blocking = 0;
    f_create = 0;
    f_device = 0;
    f_dir_create = 1;
    f_disposition = 0;
    f_extract = 0;
    f_interactive = 0;
    f_link = 0;
    f_list = 1;
    f_no_depth = 0;
    f_no_overwrite = 0;
    f_pass = 0;
    f_reverse_match = 0;
    f_single_match = 0;
    f_unconditional = 1;
    f_verbose = 0;
    msgfile=stdout;

    f_access_time = 0;
    f_extract_access_time = 1;
    f_mode = 0;
    f_mtime = 1;

#ifdef DEBUG
    while ((c = getopt(argc, argv, "Z:ab:cdf:iklnMp:rs:tuvwx:Xy")) != EOF)
#else /* ifdef DEBUG */
    while ((c = getopt(argc, argv, "ab:cdf:iklnMp:rs:tuvwx:Xy")) != EOF)
#endif /* ifdef DEBUG */
    {
	switch (c) {
#ifdef DEBUG
	  case 'Z':
	      /* This special option must be the first on the command
	       * line to be effective.  It is processed in main().  This
	       * case is here to satisfy getopt.
	       */
	      break;
#endif /* ifdef DEBUG */

	case 'a':		/* append to archive */
	    f_append = 1;
	    f_list = 0;
	    break;
	case 'b':		/* b <blocking>: set blocking factor */
	    f_blocking = 1;

	    if ((blocksize = pax_optsize(optarg)) == 0) {
		fatal(MSGSTR(PAX_BSIZE, "Bad block size"));
	    }
	    break;
	case 'c':		/* match all files execpt those named */
	    f_reverse_match = 1;
	    break;
	case 'd':		/* do not recurse on directories */
	    f_no_depth = 1;
	    break;
	case 'f':		/* f <archive>: specify archive */
	    ar_file = optarg;
	    break;
	case 'i':		/* interactively rename files */
	    f_interactive = 1;
	    break;
	case 'k':		/* don't overwrite existing files */
	    f_no_overwrite = 1;
	    break;
	case 'l':		/* make hard-links when copying */
	    f_link = 1;
	    break;
	case 'n':		/* only first match for each pattern */
	    f_single_match = 1;
	    break;
        case 'M':		/* match with '/' having no special meaning */
	    f_fnmatch_old = (f_fnmatch_old) ? 0 : 1;
	    break;
	case 'p':		/* privilege options */
	    string = optarg;
	    while (*string != '\0')
		switch(*string++) {
		    case 'a':	/* do not preserve access time */
			f_extract_access_time = 0;
			break;
		    case 'e':	/* preserve everything */
			f_extract_access_time = 1;
			f_mtime = 1;	/* mod time */
			f_owner = 1;	/* owner and group */
			f_mode = 1;	/* file mode */
			break;
		    case 'm':	/* do not preserve modification time */
			f_mtime = 0;
			break;
		    case 'o':	/* preserve uid and gid */
			f_owner = 1;
			break;
		    case 'p':	/* preserve file mode bits */
			f_mode = 1;
			break;
		    default:
			fatal(MSGSTR(PAX_PRIV, "Invalid privileges"));
			break;
		}
	    break;
	case 'r':		/* read from archive */
	    if (f_create) {
		f_create = 0;
		f_pass = 1;
	    } else {
		f_list = 0;
		f_extract = 1;
	    } 
	    msgfile=stderr;
	    break;
	case 's':		/* ed-like substitute */
	    add_replstr(optarg);
	    break;
	case 't':		/* preserve access times on files read */
	    f_access_time = 1;
	    break;
	case 'u':		/* ignore older files */
	    f_unconditional = 0;
	    break;
	case 'v':		/* verbose */
	    f_verbose = 1;
	    break;
	case 'w':		/* write to archive */
	    if (f_extract) {
		f_extract = 0;
		f_pass = 1;
	    } else {
		f_list = 0;
		f_create = 1;
	    } 
	    msgfile=stderr;
	    break;
	case 'x':		/* x <format>: specify archive format */
	    if (strcmp(optarg, "ustar") == 0) {
		x_format = TAR;
		if (blocksize ==0)
			blocksize = DEFBLK_TAR * BLOCKSIZE;	/* Draft 11 */
	    } else if (strcmp(optarg, "cpio") == 0) {
		x_format = CPIO;
		if (blocksize == 0)
			blocksize = DEFBLK_CPIO * BLOCKSIZE;	/* Draft 11 */
	    } else {
		usage();
	    }
	    break;
	case 'X':		/* do not descend into dirs on other */
	    f_device = 1;	/* filesystems */
	    break;
	case 'y':		/* Not in std: interactively ask for */
				/* the disposition of all files (from */
				/* the net version) */
	    f_disposition = 1;
	    break;
	default:
	    usage();
	}
    }

#ifdef DEBUG
    if (debug_mode == debug_pipe) {
        /* use this delay point to attach to an instance of the program
	 * in a pipe from the debugger.
	 */

      int dummy;

      for (dummy = 1; dummy != 0;)
      {
	sleep(10);
	dummy = (dummy) ? dummy : 0;
      }
    }
#endif /* ifdef DEBUG */

    if (blocksize == 0) {
	blocking = DEFBLK_TAR;		/* default for ustar is 20 */
	blocksize = blocking * BLOCKSIZE;
    }
    buf_allocate((OFFSET) blocksize);

    if (!f_unconditional && f_create) {		/* -wu should be an append */
	f_create = 0;
	f_append = 1;
    }

    /* If the archive doesn't exist, we must create rather than */
    /* append. */

    if (f_append &&
	strcmp(ar_file, "-") != 0 &&
	access(ar_file, F_OK) < 0 && errno == ENOENT) {
	    f_create = 1;
	    f_append = 0;
    }

    if (f_extract || f_list) {	/* -r or nothing */
	open_archive(AR_READ);
	ar_format = get_archive_type();
	read_archive();
    } else if (f_create && !f_append) {	/* -w without -a */
	if (optind >= argc) {
	    names_from_stdin++;		/* args from stdin */
	}
	open_archive(AR_WRITE);
	if (x_format)
	    ar_format = x_format;
	create_archive();
    } else if (f_append) {	/* -w with -a */
	if (optind >= argc) {
	    names_from_stdin++;		/* args from stdin */
	}
	open_archive(AR_APPEND);
	act_format = get_archive_type();

	if (x_format && x_format != act_format) {
	    fatal(MSGSTR(PAX_FORMAT, 
			 "Archive format specified is different from existing archive"));
	} else if (ar_format != act_format) {
	  fatal(MSGSTR(PAX_FORMAT, 
		       "The default archive format is different from existing archive"));
	}

	ar_format = act_format;
	append_archive();
    } else if (f_pass && optind < argc) { /* -r and -w (ie, pass mode) */
	dirname = argv[--argc];
	if (LSTAT(dirname, &st) < 0) {
	    fatal(strerror(errno));
	}
	if ((st.sb_mode & S_IFMT) != S_IFDIR) {
	    fatal(MSGSTR(PAX_NOTDIR, "Not a directory"));
	}
	if (optind >= argc) {
	    names_from_stdin++;		/* args from stdin */
	}
	pass(dirname);
    } else {
	usage();
    }

    names_notfound();
    return (0);
}


/* get_archive_type - determine input archive type from archive header
 *
 * DESCRIPTION
 *
 * 	reads the first block of the archive and determines the archive
 *	type from the data.  Exits if the archive cannot be read.  If
 *	verbose mode is on, then the archive type will be printed on the
 *	standard error device as it is determined.
 *
 */


int
get_archive_type(void)
{
  int act_format;

    if (ar_read() != 0) {
	fatal(MSGSTR(PAX_TYPE, "Unable to determine archive type."));
    }
    if (strncmp(bufstart, M_ASCII, strlen(M_ASCII)) == 0) {
	act_format = CPIO;
	if (f_verbose) {
	    fputs(MSGSTR(PAX_CPIO, "ASCII CPIO format archive\n"), stderr);
	}
    } else if (strncmp(&bufstart[TO_MAGIC], TMAGIC, strlen(TMAGIC)) == 0) {
	act_format = TAR;
	if (f_verbose) {
	    fputs(MSGSTR(PAX_TAR, "USTAR format archive\n"), stderr);
	}
    } else if ( *((ushort *) bufstart) == M_BINARY || 
		*((ushort *) bufstart) == SWAB(M_BINARY)) {
	act_format = CPIO;
	if (f_verbose) {
	    fputs(MSGSTR(PAX_BINCPIO, "Binary CPIO format archive\n"), stderr);
	}
    } else {
      /* should we return 0 here? */
	act_format = TAR;
    }

  return (act_format);
}



/* pax_optsize - interpret a size argument
 *
 * DESCRIPTION
 *
 * 	Recognizes suffixes for blocks (512-bytes), k-bytes and megabytes.  
 * 	Also handles simple expressions containing '+' for addition.
 *
 * PARAMETERS
 *
 *    char 	*str	- A pointer to the string to interpret
 *
 * RETURNS
 *
 *    Normally returns the value represented by the expression in the 
 *    the string.
 *
 * ERRORS
 *
 *	If the string cannot be interpretted, the program will fail, since
 *	the buffering will be incorrect.
 *
 */


static OFFSET
pax_optsize(char *str)
{
    char           *idx;
    OFFSET          number;	/* temporary storage for current number */
    OFFSET          result;	/* cumulative total to be returned to caller */

    result = 0;
    idx = str;
    for (;;) {
	number = 0;
	while (*idx >= '0' && *idx <= '9')
	    number = number * 10 + *idx++ - '0';

	switch (*idx++) {
	case 'b':
	    result += number * 512L;
	    continue;
	case 'k':
	    result += number * 1024L;
	    continue;
	case 'm':
	    result += number * 1024L * 1024L;
	    continue;
	case '+':
	    result += number;
	    continue;
	case '\0':
	    result += number;
	    break;
	default:
	    break;
	}
	break;
    }
    if (*--idx) {
	fatal(MSGSTR(PAX_BADVAL, "Unrecognizable value"));
    }
    return (result);
}



/* usage - print a helpful message and exit
 *
 * DESCRIPTION
 *
 *	Usage prints out the usage message for the PAX interface and then
 *	exits with a non-zero termination status.  This is used when a user
 *	has provided non-existant or incompatible command line arguments.
 *
 * RETURNS
 *
 *	Returns an exit status of 1 to the parent process.
 *
 */


static void
usage(void)
{
#ifdef DEBUG
    fprintf(stderr, MSGSTR(PAX_U1, "Usage: %s [-Z option] -[cdnv] [-f archive] [-s replstr] [pattern...]\n"),
	myname);
    fprintf(stderr, MSGSTR(PAX_U2, "       %s [-Z option] -r [-cdiknuvy] [-f archive] [-p string] [-s replstr]\n              [pattern...]\n"),
	myname);
    fprintf(stderr, MSGSTR(PAX_U3, "       %s [-Z option] -w [-dituvyX] [-b blocking] [[-a] -f archive]\n              [-s replstr] [-x format] [pathname...]\n"),
	myname);
    fprintf(stderr, MSGSTR(PAX_U4, "       %s [-Z option] -r -w [-diklntuvyX] [-p string] [-s replstr] [pathname...] directory\n"),
	myname);
#else /* ifdef DEBUG */
    fprintf(stderr, MSGSTR(PAX_U1, "Usage: %s -[cdnv] [-f archive] [-s replstr] [pattern...]\n"),
	myname);
    fprintf(stderr, MSGSTR(PAX_U2, "       %s -r [-cdiknuvy] [-f archive] [-p string] [-s replstr]\n              [pattern...]\n"),
	myname);
    fprintf(stderr, MSGSTR(PAX_U3, "       %s -w [-dituvyX] [-b blocking] [[-a] -f archive]\n              [-s replstr] [-x format] [pathname...]\n"),
	myname);
    fprintf(stderr, MSGSTR(PAX_U4, "       %s -r -w [-diklntuvyX] [-p string] [-s replstr] [pathname...] directory\n"),
	myname);
#endif /* ifdef DEBUG */
    exit(1);
}
