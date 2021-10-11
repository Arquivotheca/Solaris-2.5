/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)main.c	1.21	94/07/18 SMI"

/*
 *	Modified to recursively extract all files within a subtree
 *	(supressed by the h option) and recreate the heirarchical
 *	structure of that subtree and move extracted files to their
 *	proper homes (supressed by the m option).
 *	Includes the s (skip files) option for use with multiple
 *	dumps on a single tape.
 *	8/29/80		by Mike Litzkow
 *
 *	Modified to work on the new file system and to recover from
 *	tape read errors.
 *	1/19/82		by Kirk McKusick
 *
 *	Full incremental restore running entirely in user code and
 *	interactive tape browser.
 *	1/19/83		by Kirk McKusick
 */

#include "restore.h"
#include <signal.h>
#include <byteorder.h>

#include <config.h>

#include <euc.h>
#include <getwidth.h>
#include <sys/mtio.h>
eucwidth_t wp;

int	bflag = 0, cvtflag = 0, dflag = 0, vflag = 0, yflag = 0;
int	hflag = 1, mflag = 1;
char	command = '\0';
long	dumpnum = 1;
long	volno = 0;
long	ntrec;
char	*progname;
char	*dumpmap;
char	*clrimap;
ino_t	maxino;
time_t	dumptime;
time_t	dumpdate;
FILE 	*terminal;
struct byteorder_ctx *byteorder;

main(argc, argv)
	int argc;
	char *argv[];
{
	register char *cp;
	ino_t ino;
	char *inputdev;
	char *archivefile = 0;
	char *symtbl = "./restoresymtable";
	char name[MAXPATHLEN];
	char *metamucilfile = NULL;
	int  fflag = 0;
#ifdef USG
	struct sigaction sa, osa;
#endif

	if (progname = strrchr(argv[0], '/'))
		progname++;
	else
		progname = argv[0];

	if (strcmp("hsmrestore", progname) == 0) {
		metamucil_mode = METAMUCIL;
		inputdev = TAPE;
	} else {
		metamucil_mode = NOT_METAMUCIL;
		inputdev = DEFTAPE;
	}

	/* This doesn't work because ufsrestore is statically linked */
	/* (void) setlocale(LC_ALL, ""); */
	/* The problem seems to be with LC_COLLATE, so set all the */
	/* others explicitly.  Bug 1157128 was created against the I18N */
	/* library.  When that bug is fixed this should go back to the way */
	/* it was. */
	(void) setlocale(LC_CTYPE, "");
	(void) setlocale(LC_NUMERIC, "");
	(void) setlocale(LC_TIME, "");
	(void) setlocale(LC_MONETARY, "");
	(void) setlocale(LC_MESSAGES, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);
	getwidth(&wp);
	if ((byteorder = byteorder_create()) == NULL) {
		(void) fprintf(stderr,
		    gettext("Cannot create byteorder context\n"));
		done(1);
	}

#ifdef USG
	sa.sa_handler = onintr;
	sa.sa_flags = SA_RESTART;
	(void) sigemptyset(&sa.sa_mask);

	(void) sigaction(SIGINT, &sa, &osa);
	if (osa.sa_handler == SIG_IGN)
		(void) sigaction(SIGINT, &osa, (struct sigaction *)0);

	(void) sigaction(SIGTERM, &sa, &osa);
	if (osa.sa_handler == SIG_IGN)
		(void) sigaction(SIGTERM, &osa, (struct sigaction *)0);
#else
	if (signal(SIGINT, onintr) == SIG_IGN)
		(void) signal(SIGINT, SIG_IGN);
	if (signal(SIGTERM, onintr) == SIG_IGN)
		(void) signal(SIGTERM, SIG_IGN);
#endif
	if (argc < 2) {
usage:
		(void) fprintf(stderr, gettext("Usage:\n\
\t%s tfhsvy [file file ...]\n\
\t%s xfhmsvy [file file ...]\n\
\t%s ifhmsvy\n\
\t%s rfsvy\n\
\t%s Rfsvy\n"), progname, progname, progname, progname, progname);
		done(1);
	}

	if (metamucil_mode == METAMUCIL) {
#ifdef __STDC__
		(void) readconfig((char *)0, (void (*)(const char *, ...))0);
#else
		(void) readconfig((char *)0, (void (*)())0);
#endif
	} else {
		createdefaultfs(NOT_METAMUCIL);
	}
	argv++;
	argc -= 2;
	command = '\0';
	for (cp = *argv++; *cp; cp++) {
		switch (*cp) {		/* BE CAUTIONS OF FALLTHROUGHS */
		case '-':
			break;
		case 'a':
			if (argc < 1) {
				(void) fprintf(stderr,
					gettext("missing device specifier\n"));
				done(1);
			}
			archivefile = *argv++;
			argc--;
			break;
		case 'c':
			cvtflag++;
			break;
		case 'd':
			dflag++;
			break;
		case 'D':
			/*
			 * support for diskette dumps made with other
			 * than the metamucil dump program - This used to be
			 * the Dflag, but it doesn't hurt to always check, so
			 * was removed.  This case is here for backward
			 * compatability.
			 */
			break;
		case 'h':
			hflag = 0;
			break;
		case 'm':
			mflag = 0;
			break;
		case 'v':
			vflag++;
			break;
		case 'y':
			yflag++;
			break;
		case 'f':
			if (argc < 1) {
				(void) fprintf(stderr,
					gettext("missing device specifier\n"));
				done(1);
			}
			inputdev = *argv++;
			fflag++;
			if (metamucil_mode == METAMUCIL) {
				if (*inputdev == '+') {
					if (setdevice(inputdev+1)) {
						(void) fprintf(stderr, gettext(
						  "bad device sequence `%s'\n"),
							inputdev);
						done(1);
					}
				} else {
					static char *defseq = "+cmd-line-devs";
					if (makedevice((defseq+1), inputdev,
					    sequence) < 0) {
						(void) fprintf(stderr, gettext(
					      "cannot make device sequence\n"));
						done(1);
					}
					(void) setdevice(defseq+1);
					inputdev = defseq;
				}
			}
			argc--;
			break;
		case 'b':
			/*
			 * change default tape blocksize
			 */
			bflag++;
			if (argc < 1) {
				(void) fprintf(stderr,
					gettext("missing block size\n"));
				done(1);
			}
			ntrec = atoi(*argv++);
			if (ntrec <= 0) {
				(void) fprintf(stderr, gettext(
				"Block size must be a positive integer\n"));
				done(1);
			}
			if (ntrec <= 0 || (ntrec&1)) {
				(void) fprintf(stderr, gettext(
			"Block size must be a positive, even integer\n"));
				done(1);
			}
			ntrec /= 2;
			argc--;
			break;
		case 's':
			/*
			 * dumpnum (skip to) for multifile dump tapes
			 */
			if (argc < 1) {
				(void) fprintf(stderr,
					gettext("missing dump number\n"));
				done(1);
			}
			dumpnum = atoi(*argv++);
			if (dumpnum <= 0) {
				(void) fprintf(stderr, gettext(
				"Dump number must be a positive integer\n"));
				done(1);
			}
			argc--;
			break;
		case 'M':
			if (metamucil_mode == NOT_METAMUCIL) {
				(void) fprintf(stderr,
					gettext("Bad key character M\n"));
				goto usage;
			}
			/* fall through if METAMUCIL */
		case 't':
		case 'R':
		case 'r':
		case 'x':
		case 'i':
			if (command != '\0') {
				(void) fprintf(stderr, gettext(
					"%c and %c are mutually exclusive\n"),
					(u_char)*cp, (u_char)command);
				goto usage;
			}
			command = *cp;
			if (command == 'M') {
				if (argc < 1) {
					(void) fprintf(stderr, gettext(
					    "missing recover command file\n"));
					done(1);
				}
				metamucilfile = *argv++;
				argc--;
			}
			break;
		default:
			(void) fprintf(stderr,
				gettext("Bad key character %c\n"), (u_char)*cp);
			goto usage;
		}
	}
	if (command == '\0') {
		(void) fprintf(stderr,
			gettext("must specify i, t, r, R, or x\n"));
		goto usage;
	}
	/*
	 * no default device when running the metamucil recover program.
	 */
	if (metamucilfile && !fflag)
		inputdev = NULL;
	setinput(inputdev, archivefile);
	if (argc == 0) {
		argc = 1;
		*--argv = mflag ? "." : "2";
	}
	switch (command) {

	case 'M':
		metamucil(metamucilfile);
		done(0);
		/* NOTREACHED */

	/*
	 * Interactive mode.
	 */
	case 'i':
		setup();
		extractdirs(1);
		initsymtable((char *)0);
		runcmdshell();
		done(0);
		/* NOTREACHED */
	/*
	 * Incremental restoration of a file system.
	 */
	case 'r':
		setup();
		if (dumptime > 0) {
			/*
			 * This is an incremental dump tape.
			 */
			vprintf(stdout, gettext("Begin incremental restore\n"));
			initsymtable(symtbl);
			extractdirs(1);
			removeoldleaves();
			vprintf(stdout, gettext("Calculate node updates.\n"));
			treescan(".", ROOTINO, nodeupdates);
			findunreflinks();
			removeoldnodes();
		} else {
			/*
			 * This is a level zero dump tape.
			 */
			vprintf(stdout, gettext("Begin level 0 restore\n"));
			initsymtable((char *)0);
			extractdirs(1);
			vprintf(stdout,
				gettext("Calculate extraction list.\n"));
			treescan(".", ROOTINO, nodeupdates);
		}
		createleaves(symtbl);
		createlinks();
		setdirmodes();
		checkrestore();
		if (dflag) {
			vprintf(stdout,
				gettext("Verify the directory structure\n"));
			treescan(".", ROOTINO, verifyfile);
		}
		dumpsymtable(symtbl, (long)1);
		done(0);
		/* NOTREACHED */
	/*
	 * Resume an incremental file system restoration.
	 */
	case 'R':
		setupR();
		initsymtable(symtbl);
		skipmaps();
		skipdirs();
		createleaves(symtbl);
		createlinks();
		setdirmodes();
		checkrestore();
		dumpsymtable(symtbl, (long)1);
		done(0);
		/* NOTREACHED */
	/*
	 * List contents of tape.
	 */
	case 't':
		setup();
		extractdirs(0);
		initsymtable((char *)0);
		if (vflag)
			printdumpinfo();
		while (argc--) {
			canon(*argv++, name);
			ino = dirlookup(name);
			if (ino == 0)
				continue;
			treescan(name, ino, listfile);
		}
		done(0);
		/* NOTREACHED */
	/*
	 * Batch extraction of tape contents.
	 */
	case 'x':
		setup();
		extractdirs(1);
		initsymtable((char *)0);
		while (argc--) {
			long tmp_ino;

			if (mflag) {
				canon(*argv++, name);
				ino = dirlookup(name);
				if (ino == 0)
					continue;
				pathcheck(name);
			} else {
				tmp_ino = atol(*argv++);
				if (tmp_ino < ROOTINO) {
					(void) fprintf(stderr, gettext(
						"bad inode number: %ld\n"),
						tmp_ino);
					done(1);
				}
				name[0] = '\0';
				ino = (ino_t)tmp_ino;
			}
			treescan(name, ino, addfile);
		}
		createfiles();
		createlinks();
		setdirmodes();
		if (dflag)
			checkrestore();
		done(0);
		/* NOTREACHED */
	}
#ifdef lint
	return (0);
#endif
}
