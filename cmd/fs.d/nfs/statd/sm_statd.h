/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sm_statd.h	1.11	95/03/08 SMI"	/* SVr4.0 1.2	*/
/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *  		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986-1989,1994  Sun Microsystems, Inc.
 *  	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 */

#define	MAXHOSTNAMELEN 64

#define	MAXIPADDRS 64
#define	MAXPATHS 8
#define	SM_DIRECTORY_MODE 00755

extern char hostname[MAXHOSTNAMELEN];

extern int	create_file(char *name);
extern int	delete_file(char *name);
extern void	record_name(char *name, int op);
extern void	send_notice(char *mon_name, int state);
extern void	sm_crash(void);
extern void	sm_notify(stat_chge *ntfp);
extern void	statd_init(void);
