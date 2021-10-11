#ident	"@(#)lck.c	1.8	95/07/10 SMI"		/* SVr4.0 1.9.4.1 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 *		Copyright (C) 1991  Sun Microsystems, Inc
 *			All rights reserved.
 *		Notice of copyright on this source code
 *		product does not indicate publication.
 *
 *		RESTRICTED RIGHTS LEGEND:
 *   Use, duplication, or disclosure by the Government is subject
 *   to restrictions as set forth in subparagraph (c)(1)(ii) of
 *   the Rights in Technical Data and Computer Software clause at
 *   DFARS 52.227-7013 and in similar clauses in the FAR and NASA
 *   FAR Supplement.
 */

/*
 * This file contains code for the crash function lck.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <nlist.h>
#include <sys/types.h>
#include <sys/var.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/flock.h>
#include <sys/elf.h>
#include <sys/fs/ufs_inode.h>
#include <sys/flock_impl.h>
#include "crash.h"

static void prilcks();
static void prlcks();

extern long u_ninode, u_inohsz;		/* "ufs_inode.c"  */

struct procid {				/* effective ids */
	pid_t epid;
	int valid;
};

struct procid *procptr;		/* pointer to procid structure */

/* get effective and sys ids into table */
int
getprocid()
{
	struct proc *prp, prbuf;
	struct pid pid;
	static int lckinit = 0;
	register i;
	proc_t *slot_to_proc();

	if (lckinit == 0) {
		procptr = (struct procid *)malloc((unsigned)
			(sizeof (struct procid) * vbuf.v_proc));
		lckinit = 1;
	}

	for (i = 0; i < vbuf.v_proc; i++) {
		prp = slot_to_proc(i);
		if (prp == NULL)
			procptr[i].valid = 0;
		else {
			readmem((long)prp, 1, -1, (char *)&prbuf,
				sizeof (proc_t), "proc table");
			readmem((long)prbuf.p_pidp, 1, -1,
				(char *)&pid, sizeof (struct pid), "pid table");
			procptr[i].epid = pid.pid_id;
			procptr[i].valid = 1;
		}
	}
	return (0);
}

/* find process with same id and sys id */
int
findproc(pid)
pid_t pid;
{
	int slot;

	for (slot = 0; slot < vbuf.v_proc; slot++)
		if ((procptr[slot].valid) &&
		    (procptr[slot].epid == pid))
			return (slot);
	return (-1);
}

/* get arguments for lck function */
int
getlcks()
{
	int phys = 0;
	long addr = -1;
	int c;
	struct flckinfo infobuf;

	optind = 1;
	while ((c = getopt(argcnt, args, "epw:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			case 'p' :	phys = 1;
					break;
			case 'e' :
					break;
			default  :	longjmp(syn, 0);
		}
	}
	getprocid();

	if (args[optind]) {
		do {
			if ((addr = strcon(args[optind], 'h')) == -1)
				error("\n");
			fprintf(fp,
"TYP WHENCE  START    LEN    PID  STAT     PREV     NEXT\n");
			prlcks(phys, addr);
		} while (args[++optind]);
	} else
		prilcks();
	return (0);
}

/* print lock information relative to ufs_inodes (default) */
static void
prilcks()
{
	lock_descriptor_t *head, *lptr;
	lock_descriptor_t fibuf;
	char hexbuf[20], *str;
	char *vnodeptr;
	struct inode ibuf;
	int active = 0;
	int sleep = 0;
	int slot;

	get_ufsinfo();

	fprintf(fp, "\nActive and Sleep Locks:\n");
	fprintf(fp,
"INO  TYP   START    END      PROC   PID    FLAGS  PREV      NEXT      LOCK\n");

	for (slot = 0; slot < u_ninode; slot++) {
		if (slot_to_inode(slot, -1, 0, &ibuf) == 0)
			error("Could not find inode\n");

		if (ibuf.i_mode == 0)
			continue;
		if (ibuf.i_vnode.v_filocks == NULL)
			continue;

		head = (lock_descriptor_t *)ibuf.i_vnode.v_filocks;

		lptr = head;
		readmem((long) lptr, 1, -1, (char *) &fibuf,
		    sizeof (fibuf), "filock information");
		vnodeptr = (char *)fibuf.l_vnode;
		do {

			fprintf(fp, "%4d ", slot);

			if (fibuf.l_state & ACTIVE_LOCK)
				active++;
			else
				sleep++;

			if (fibuf.l_type == F_RDLCK)
				str = "  r  ";
			else if (fibuf.l_type == F_WRLCK)
				str = "  w  ";
			else
				str = "  ?  ";

			if (fibuf.l_end == MAXEND)
				strcpy(hexbuf, "MAXEND");
			else
				sprintf(hexbuf, "%x", fibuf.l_end);
			fprintf(fp, "%s  %8-x %8-s %4-d   %5-d  %03x",
				str,
				fibuf.l_start,
				hexbuf,
				findproc(fibuf.l_flock.l_pid),
				fibuf.l_flock.l_pid,
				fibuf.l_state);
			fprintf(fp, "    %8-x  %8-x",
				fibuf.l_prev,
				fibuf.l_next);
			fprintf(fp, "	 %8-x\n", lptr);
			lptr = fibuf.l_next;
			readmem((long) lptr, 1, -1, (char *) &fibuf,
			    sizeof (fibuf), "filock information");
		} while (((char *)fibuf.l_vnode == vnodeptr));
	}

	fprintf(fp, "\nSummary From List:\n");
	fprintf(fp, " TOTAL    ACTIVE  SLEEP\n");
	fprintf(fp, " %4d    %4d    %4d\n",
		active+sleep,
		active,
		sleep);
}

/* print linked list of locks */
static void
prlcks(phys, addr)
int phys;
long addr;
{
	struct lock_descriptor fibuf;

	readbuf(addr, 0, phys, -1, (char *)&fibuf, sizeof (fibuf), "frlock");
	if (fibuf.l_flock.l_type == F_RDLCK)
		fprintf(fp, " r ");
	else if (fibuf.l_flock.l_type == F_WRLCK)
		fprintf(fp, " w ");
	else if (fibuf.l_flock.l_type == F_UNLCK)
		fprintf(fp, " u ");
	else
		fprintf(fp, " - ");
	fprintf(fp, " %1d       %8-x %6-d %6-d %03x %8-x %8-x\n",
		fibuf.l_flock.l_whence,
		fibuf.l_flock.l_start,
		fibuf.l_flock.l_len,
		fibuf.l_flock.l_pid,
		fibuf.l_state,
		fibuf.l_prev,
		fibuf.l_next);
}
