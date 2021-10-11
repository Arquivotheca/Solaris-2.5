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

#ident	"@(#)page.c	1.10	94/08/17 SMI"		/* SVr4.0 1.10.7.1 */

/*
 * This file contains code for the crash functions: page and as.
 */

#include <stdio.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/var.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/elf.h>
#include <vm/as.h>
#include <vm/page.h>
#include "crash.h"

/* symbol pointers */
extern Elf32_Sym *V;		/* ptr to var structure */
static Elf32_Sym *Pages;	/* ptr to start of page structure array */
static Elf32_Sym *Epages;	/* ptr to end of page structure array   */

static long pages = 0;
static long epages = 0;

static void prpage();
static void pras();
static void prsegs();

/* get arguments for page function */
int
getpage()
{
	int slot = -1;
	int all = 0;
	int lock = 0;
	int phys = 0;
	int fsdata = 0;
	long addr = -1;
	long arg1 = -1;
	long arg2 = -1;
	int c;
	unsigned  size;

	if (!Pages) {
		if (!(Pages = symsrch("pages")))
			error("pages not found in symbol table\n");
	}
	readmem(Pages->st_value, 1, -1, (char *)&pages, sizeof (pages),
				"pages: ptr to page structures");

	if (!Epages) {
		if (!(Epages = symsrch("epages")))
			error("epages not found in symbol table\n");
	}
	readmem(Epages->st_value, 1, -1, (char *)&epages, sizeof (epages),
			"epages: ptr to end of page structures");

	optind = 1;
	while ((c = getopt(argcnt, args, "elpw:f")) != EOF) {
		switch (c) {
			case 'e' :	all = 1;
					break;
			case 'l' :	lock = 1;
					break;
			case 'w' :	redirect();
					break;
			case 'p' :	phys = 1;
					break;
			case 'f' :	fsdata = 1;
					break;
			default  :	longjmp(syn, 0);
		}
	}

	size = ((epages - pages) / sizeof (struct page)) + 1;

	fprintf(fp, "PAGE STRUCTURE TABLE SIZE: %d\n\n", size);
	fprintf(fp, "SLOT  VNODE        HASH         PREV   VPPREV  FLAGS\n");
	fprintf(fp, "          OFFSET      HMENT     NEXT      VPNEXT\n");
	fprintf(fp, "          SELOCK      LCKCNT    COWCNT    FSDATA\n");
	if (args[optind]) {
		all = 1;
		do {
			getargs((int)size, &arg1, &arg2, phys);
			if (arg1 == -1)
				continue;
			if (arg2 != -1)
				for (slot = arg1; slot <= arg2; slot++)
					prpage(all, slot, phys, addr,
						lock, fsdata);
			else {
				if ((unsigned long)arg1 < size)
					slot = arg1;
				else
					addr = arg1;
				prpage(all, slot, phys, addr, lock, fsdata);
			}
			slot = addr = arg1 = arg2 = -1;
		} while (args[++optind]);
	} else
		for (slot = 0; slot < size; slot++)
			prpage(all, slot, phys, addr, lock, fsdata);
	return (0);
}

/* print page structure table */
static void
prpage(all, slot, phys, addr, lock, fsdata)
int all, slot, phys;
long addr;
int lock;
int fsdata;
{
	struct page pagebuf;

	readbuf(addr, pages + slot * sizeof (pagebuf), phys, -1,
		(char *)&pagebuf, sizeof (pagebuf), "page structure table");

	/* check page flags */
	if ((*((ushort *)&pagebuf) == 0) && !all)
		return;

	if (fsdata && pagebuf.p_fsdata == 0)
		return;

	if (slot == -1)
		fprintf(fp, "   -  ");
	else
		fprintf(fp, "%4d  ", slot);

	fprintf(fp, "0x%08x  ", pagebuf.p_vnode);

	fprintf(fp, "0x%07x 0x%07x 0x%07x ",
			pagebuf.p_hash,
			pagebuf.p_prev,
			pagebuf.p_vpprev);

	fprintf(fp, "%s%s%s%s%s%s\n",
		pagebuf.p_free    ? "free "    : "",
		pagebuf.p_age     ? "age "     : "",
		pagebuf.p_nrm & P_MOD ? "mod " : "",
		pagebuf.p_nrm & P_REF ? "ref " : "",
		pagebuf.p_nrm & P_PNC ? "pnc " : "",
		pagebuf.p_nrm & P_TNC ? "tnc " : "");

	/* second line */

	fprintf(fp, "        %7d    %7x ",
		pagebuf.p_offset, pagebuf.p_mapping);

	fprintf(fp, "0x%07x  0x%07x  \n",
		pagebuf.p_next,
		pagebuf.p_vpnext);

	/* third line */

	fprintf(fp, "        %7x    %7x    %7x    %7x\n",
		pagebuf.p_selock, pagebuf.p_lckcnt, pagebuf.p_cowcnt,
		pagebuf.p_fsdata);
	if (lock) {
		fprintf(fp, "\nLock information\n");
		prcondvar(&pagebuf.p_cv, "p_cv");
		fprintf(fp, "selock p_selock: %d\n", pagebuf.p_selock);
		fprintf(fp, "p_iolock:");
		prsema(&(pagebuf.p_iolock));
	}
}

/* get arguments for as function */
int
getas()
{
	int slot = -1;
	int full = 0;
	int lock = 0;
	int phys = 0;
	long addr = -1;
	long arg1 = -1;
	long arg2 = -1;
	int c;
	char *heading =
	    "PROC        PAGLCK   CLGAP  VBITS HAT        HRM         RSS\n"
	    " SEGLST     LOCK        SEGS       SIZE     LREP TAIL     NSEGS\n";

	optind = 1;
	while ((c = getopt(argcnt, args, "eflpw:")) != EOF) {
		switch (c) {
			case 'e' :
					break;
			case 'f' :	full = 1;
					break;
			case 'l' :	lock = 1;
					break;
			case 'w' :	redirect();
					break;
			case 'p' :	phys = 1;
					break;
			default  :	longjmp(syn, 0);
		}
	}

	if (!full)
		fprintf(fp, "%s", heading);

	if (args[optind]) {
		do {
			getargs(vbuf.v_proc, &arg1, &arg2, phys);
			if (arg1 == -1)
				continue;
			if (arg2 != -1)
				for (slot = arg1; slot <= arg2; slot++)
					pras(full, slot, phys, addr,
						heading, lock);
			else {
				if ((unsigned long)arg1 < vbuf.v_proc)
					slot = arg1;
				else
					addr = arg1;
				pras(full, slot, phys, addr, heading, lock);
			}
			slot = arg1 = arg2 = -1;
		} while (args[++optind]);
	} else {
		readmem(V->st_value, 1, -1, (char *)&vbuf,
			sizeof (vbuf), "var structure");
		for (slot = 0; slot < vbuf.v_proc; slot++)
			pras(full, slot, phys, addr, heading, lock);
	}
	return (0);
}


/* print address space structure */
static void
pras(full, slot, phys, addr, heading, lock)
int full, slot, phys;
long addr;
char *heading;
int lock;
{
	struct proc prbuf, *procaddr;
	struct as asbuf;
	struct seg *seg, *seglast;
	proc_t *slot_to_proc();

	if (slot != -1)
		procaddr = slot_to_proc(slot);
	else
		procaddr = (struct proc *)addr;

	if (procaddr) {
		readbuf((unsigned)procaddr, 0, phys, -1, (char *)&prbuf,
				sizeof (prbuf), "proc table");
	} else {
		return;
	}

	if (full)
		fprintf(fp, "\n%s", heading);

	if (slot == -1)
		fprintf(fp, "%x  ", procaddr);
	else
		fprintf(fp, "%4d  ", slot);

	if (prbuf.p_as == NULL) {
		fprintf(fp, "- no address space.\n");
		return;
	}

	readbuf(-1, (long)(prbuf.p_as), phys, -1, (char *)&asbuf,
		sizeof (asbuf), "as structure");
	if (asbuf.a_lrep == AS_LREP_LINKEDLIST) {
		seg = asbuf.a_segs.list;
		seglast = asbuf.a_cache.seglast;
	} else {
		seg_skiplist ssl;
		ssl_spath spath;

		readbuf(addr, (unsigned)asbuf.a_segs.skiplist, phys, -1,
			(char *)&ssl, sizeof (ssl), "skiplist structure");
		seg = ssl.segs[0];
		readbuf(addr, (unsigned)asbuf.a_cache.spath, phys, -1,
			(char *)&spath, sizeof (spath), "spath structure");
		readbuf(addr, (unsigned)spath.ssls[0], phys, -1,
			(char *)&ssl, sizeof (ssl), "skiplist structure");
		seglast = ssl.segs[0];
	}

	fprintf(fp, "%7d  %7d      0x%x   0x%8-x   0x%8-x %4d\n",
		asbuf.a_paglck,
		asbuf.a_claimgap,
		asbuf.a_vbits,
		asbuf.a_hat,
		asbuf.a_hrm,
		asbuf.a_rss);
	fprintf(fp, "0x%7-x  0x%7-x  0x%7-x  %7d  %d     0x%7-x  %4d\n",
		seglast,
		&asbuf.a_lock,
		seg,
		asbuf.a_size,
		asbuf.a_lrep,
		asbuf.a_tail,
		asbuf.a_nsegs);

	if (full) {
		prsegs(prbuf.p_as, (struct as *)&asbuf, phys, addr);
	}
	if (lock) {
		fprintf(fp, "\na_contents: ");
		prmutex(&(asbuf.a_contents));
		prcondvar(&asbuf.a_cv, "a_cv");
		fprintf(fp, "a_lock: ");
		prrwlock(&(asbuf.a_lock));
	}
}


/* print list of seg structures */
static void
prsegs(as, asbuf, phys, addr)
	struct as *as, *asbuf;
	long phys, addr;
{
	struct seg *seg;
	struct seg  segbuf;
	seg_skiplist ssl;
	Elf32_Sym *sp;
	extern char * strtbl;

	if (asbuf->a_lrep == AS_LREP_LINKEDLIST) {
		seg = asbuf->a_segs.list;
	} else {
		readbuf(addr, (unsigned)asbuf->a_segs.skiplist, phys, -1,
			(char *)&ssl, sizeof (ssl), "skiplist structure");
		seg = ssl.segs[0];
	}

	if (seg == NULL)
		return;

	fprintf(fp,
"    BASE       SIZE     AS       NEXT        PREV         OPS        DATA\n");

	do {
		readbuf(addr, (unsigned)seg, phys, -1, (char *)&segbuf,
			sizeof (segbuf), "seg structure");
		if (asbuf->a_lrep == AS_LREP_LINKEDLIST) {
			seg = segbuf.s_next.list;
		} else {
			readbuf(addr, (unsigned)segbuf.s_next.skiplist,
				phys, -1, (char *)&ssl,
				sizeof (ssl), "skiplist structure");
			seg = ssl.segs[0];
		}
		fprintf(fp, "   0x%08x %6x 0x%08x 0x%08x 0x%08x ",
			segbuf.s_base,
			segbuf.s_size,
			segbuf.s_as,
			seg,
			segbuf.s_prev);

		/*
		 * Try to find a symbolic name for the sops vector. If
		 * can't find one print the hex address.
		 */
		sp = findsym((unsigned long)segbuf.s_ops);
		if ((!sp) || ((unsigned long)segbuf.s_ops != sp->st_value))
			fprintf(fp, "0x%08x  ", segbuf.s_ops);
		else
			fprintf(fp, "%10.10s  ", strtbl+sp->st_name);

		fprintf(fp, "0x%08x\n", segbuf.s_data);

		if (segbuf.s_as != as) {
			fprintf(fp,
"WARNING - seg was not pointing to the correct as struct: 0x%8x\n",
				segbuf.s_as);
			fprintf(fp, "          seg list traversal aborted.\n");
			return;
		}
	} while (seg != NULL);
}
