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

#if !defined(lint) && !defined(_NOIDENT)
static char rcsid[] = "@(#)$RCSfile: option.c,v $ $Revision: 1.1.2.4 $ (OSF) $Date: 1992/11/17 20:19:02 $";
#endif

#pragma	ident	"@(#)option.c 1.3	95/06/15 SMI"

/*
 * Copyright (c) 1988 Mark Nudleman
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef lint
static char sccsid[] = "@(#)option.c	5.11 (Berkeley) 6/1/90";
#endif /* not lint */

#if defined(sun)
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "less.h"
#include "more_msg.h"

int sc_window_set = 0;
#endif


int top_scroll;			/* Repaint screen from top */
int bs_mode;			/* How to process backspaces */
int caseless;			/* Do "caseless" searches */
int cbufs = 10;			/* Current number of buffers */
int linenums = 1;		/* Use line numbers */
int quit_at_eof;
int squeeze;			/* Squeeze multiple blank lines into one */
int tabstop = 8;		/* Tab settings */
int tagoption;
int verbose;			/* Print long prompt */
int show_opt = 1;		/* Show control chars as ^c */
int show_all_opt;		/* Show all ctrl chars, even CR, BS, and TAB */


extern char *first_cmd;
extern char *every_first_cmd;
extern int sc_window;
extern int use_tite;

int
option(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
#if !defined(sun)
	static int sc_window_set = 0;
#endif
	int ch;
	char *p;

	/* backward compatible processing for "+/search" */
	{
		char 	**av = argv;
		int	ac = argc;

		/*
		 * search through for '+' syntax and convert to -p syntax
		 * i.e. "+/<pattern>" is now "-p/<pattern>"
		 * find '-<number>' syntax and convert to -n syntax.
		 */
		while (--ac > 0) {
			char *ap = *++av;
			if ((ch = ap[0]) == '+') {
				char *p = calloc(strlen(ap)+2, sizeof(char));
				p[0] = '-'; p[1] = 'p';
				strcpy(&p[2], ap+1);
				*av = p;
			}
			if (ch == '-' && isdigit(ap[1])) {
				char *p = calloc(strlen(ap)+2, sizeof(char));
				p[0] = '-'; p[1] = 'n';
				strcpy(&p[2], ap+1);
				*av = p;
			}
		}
	}

	optind = 1;		/* called twice, re-init getopt. */
#if defined(sun)
	while ((ch = getopt(argc, argv, "cdeifn:p:st:u")) != EOF)
#else
	while ((ch = getopt(argc, argv, "Ncdeifn:p:st:uvW:x:z")) != EOF)
#endif
		switch((char)ch) {
		case 'c':
			top_scroll = 1;
			break;
		case 'd':
			verbose = 1;
			break;
		case 'e':
			quit_at_eof = 1;
			break;
		case 'i':
			caseless = 1;
			break;
		case 'n':
			errno = 0;
#if defined(sun)
			sc_window = atoi(optarg) - 1;
#else
			sc_window = atoi(optarg);
#endif
			if (errno) {
#if defined(sun)
				error(gettext("Bad screen size.\n"));
#else
				error(MSGSTR(BADSCREEN, "Bad screen size.\n"));
#endif
			}
			sc_window_set = 1;
			break;
#if !defined(sun)
		case 'N':
			linenums = 0;
			break;
#endif
		case 'p':
			every_first_cmd = save(optarg);
			first_cmd = optarg;
			break;
		case 's':
			squeeze = 1;
			break;
		case 't':
			tagoption = 1;
			findtag(optarg);
			break;
		case 'u':
#if defined(sun)
			bs_mode = show_opt = 1;
#else
			bs_mode = 1;
#endif
			break;
#if !defined(sun)
		case 'v':
			show_opt = 0;
			break;
		case 'W':		/* POSIX says use for extentions */
			if (strcmp(optarg, "notite")==0)
				use_tite = 0;
			else if (strcmp(optarg, "tite") == 0)
				use_tite = 1;
			else {
				fprintf(stderr, MSGSTR(BADWARG,
						"Invalid -W option.\n"));
				exit(1);
			}
			break;
		case 'x':
			tabstop = atoi(optarg);
			if (tabstop <= 0)
				tabstop = 8;
			break;
#endif
		case 'f':	/* ignore -f, compatability with old more */
			break;
#if !defined(sun)
		case 'z':
			bs_mode = show_all_opt = show_opt = 1;
			break;
#endif
		case '?':
		default:
#if defined(sun)
			fprintf(stderr, gettext(
"usage: more [-cdeisu] [-n number] [-p command] [-t tagstring] [file ...]\n"
"       more [-cdeisu] [-n number] [+command] [-t tagstring] [file ...]\n"));
#else
			fprintf(stderr, MSGSTR(USAGE, 
"usage: more [-Ncdeisuvz] [-t tag] [-x tabs] [-p command] [-n number]\n\t    [-W option] [file ...]\n"));
#endif
			exit(1);
		}
	return(optind);
}
