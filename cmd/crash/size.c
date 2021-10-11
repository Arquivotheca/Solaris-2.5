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

#ident	"@(#)size.c	1.9	94/06/10 SMI"		/* SVr4.0 1.14.2.1 */

/*
 * This file contains code for the crash functions:  size, findslot, and
 * findaddr.  The size table for Streams structures is located in
 * sizenet.c
 */

#include <sys/param.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/flock.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/thread.h>
#include <sys/lwp.h>
#include <sys/callo.h>
#include <sys/stream.h>
#include <vm/as.h>
#include <vm/page.h>
#include <sys/strtty.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/fs/snode.h>
#include <sys/fs/fifonode.h>
#include <sys/fs/ufs_inode.h>
#include <sys/elf.h>
#include <sys/proc/prdata.h>
#include "crash.h"

struct sizetable {
	char *name;
	char *symbol;
	unsigned size;
};

struct sizetable siztab[] = {
	"buf", "buf", sizeof (struct buf),
	"callout", "callout", sizeof (struct callout),
	"flckinfo", "flckinfo", sizeof (struct flckinfo),
	"fifonode", "fifonode", sizeof (struct fifonode),
	"filock", "flox", sizeof (struct filock),
	"flox", "flox", sizeof (struct filock),
	"lwp", "lwp", sizeof (struct _klwp),
	"pp", "pp", sizeof (struct page),
	"prnode", "prnode", sizeof (struct prnode),
	"proc", "proc", sizeof (struct proc),
	"snode", "snode", sizeof (struct snode),
	"thread", "thread", sizeof (struct _kthread),
	"tty", "tty", sizeof (struct strtty),
	"ufs_inode", "ufs_inode", sizeof (struct inode),
	"user", "user", sizeof (struct user),
	"vfs", "vfs", sizeof (struct vfs),
	"vfssw", "vfssw", sizeof (struct vfssw),
	"vnode", "vnode", sizeof (struct vnode),
	NULL, NULL, NULL
};


/* get size from size tables */
unsigned
getsizetab(name)
char *name;
{
	unsigned size = 0;
	struct sizetable *st;
	extern unsigned getsizenetab();

	for (st = siztab; st->name; st++) {
		if (!(strcmp(st->name, name))) {
			size = st->size;
			break;
		}
	if (!size)
		size = getsizenetab(name);
	}
	return (size);
}

/* get arguments for size function */
int
getsize()
{
	int c;
	char *all = "";
	int hex = 0;

	optind = 1;
	while ((c = getopt(argcnt, args, "xw:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			case 'x' : 	hex = 1;
					break;
			default  :	longjmp(syn, 0);
		}
	}
	if (args[optind]) {
		do {
			prtsize(args[optind++], hex);
		} while (args[optind]);
	} else prtsize(all, hex);
}

/* print size */
int
prtsize(name, hex)
char *name;
int hex;
{
	unsigned size;
	struct sizetable *st;
	int i;

	if (strcmp("", name) == 0) {
		for (st = siztab, i = 0; st->name; st++, i++) {
			if (!(i & 3))
				fprintf(fp, "\n");
			fprintf(fp, "%-15s", st->name);
		}
		prsizenet(name);
	} else {
		size = getsizetab(name);
		if (size) {
			if (hex)
				fprintf(fp, "0x%x\n", size);
			else fprintf(fp, "%d\n", size);
		} else error("%s does not match in sizetable\n", name);
	}
}


/* get arguments for findaddr function */
int
getfindaddr()
{
	int c;
	int slot;
	char *name;

	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}
	if (args[optind]) {
		name = args[optind++];
		if (args[optind]) {
			if ((slot = (int)strcon(args[optind], 'd')) == -1)
				error("\n");
			prfindaddr(name, slot);
		} else longjmp(syn, 0);
	} else longjmp(syn, 0);
}

/* print address */
int
prfindaddr(name, slot)
char *name;
int slot;
{
	unsigned size = 0;
	struct nlist nl;
	struct sizetable *st;
	char symbol[10];

	symbol[0] = '\0';
	for (st = siztab; st->name; st++)
		if (!(strcmp(st->name, name))) {
			strcpy(symbol, st->symbol);
			size = st->size;
			break;
		}
	if (symbol[0] == '\0')
		getnetsym(name, symbol, &size);
	if (symbol[0] == '\0')
		error("no match for %s in sizetable\n", name);
	if (nl_getsym(symbol, &nl) == -1)
		error("no match for %s in symbol table\n", name);
	fprintf(fp, "%8x\n", nl.n_value + size * slot);
}

/* get arguments for findslot function */
int
getfindslot()
{
	int c;
	long addr;

	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}
	if (args[optind]) {
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				continue;
			prfindslot(addr);
		} while (args[optind]);
	} else longjmp(syn, 0);
}

/* print table and slot */
int
prfindslot(addr)
long addr;
{
	Elf32_Sym *sp;
	int slot, offset;
	unsigned size;
	extern char *strtbl;
	extern Elf32_Sym *findsym();
	char *name;

	if (!(sp = findsym((unsigned long)addr)))
		error("no symbol match for %8x\n", addr);
	name = strtbl + sp->st_name;
	size = getsizetab(name);
	if (!size)
		error("%s does not match in sizetable\n", name);
	slot = (addr - sp->st_value) / size;
	offset = (addr - sp->st_value) % size;
	fprintf(fp, "%s", name);
	fprintf(fp, ", slot %d, offset %d\n", slot, offset);
}
