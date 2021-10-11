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

#pragma	ident	"@(#)init.c	1.11	95/07/04 SMI"	/* SVr4.0 1.7.3.1 */

/*
 * This file contains code for the crash initialization.
 */

#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/var.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include "crash.h"

#define	VSIZE 160		/* version string length */

char	version[VSIZE];		/* version strings */

kvm_t *kd;

unsigned nproc, procv;
unsigned bufv;

/*
 * The following is to allow for a platform independent crash program.
 * Any platform dependent parameters that are deemed necessary for this
 * program to know about, much be obtained via the mechanism below.
 * (avoid using platform dependencies whenever possible)
 */
struct nlist c_nl[] = {
	{ "_mmu_pagesize" },
	{ "_kernelbase" },
	{ "_userlimit" },
	{ "hz" },
	{ (char *)0 },
};
/*
 * The following values obviously correspond to those above.
 * These are used by the rest of the crash program to access
 * the needed data.  Their naming corresponds to what is assigned
 * for a _KMEMUSER, without _MACHDEP, which is how we are compiled.
 */
uint_t _mmu_pagesize;
uint_t _kernelbase;
uint_t _userlimit;
uint_t hz;

/*
 * Structure to convert kernel symbols names to the crash usable
 * equivalent.
 */
typedef struct ktoc {
	char *kname;
	uint_t *cname;
} ktoc_t;

ktoc_t ktoc_tab[] = {
	{ "_mmu_pagesize", &_mmu_pagesize },
	{ "_kernelbase", &_kernelbase },
	{ "_userlimit", &_userlimit },
	{ "hz", &hz },
	{ (char *)0, (uint_t *)0 }
};

/*
 * Read the platform dependent constant values from the designated "kvm"
 * Return -1 on error, 0 on success.
 */
static int
read_kvm(kvm_t *kd)
{
	int ret;
	ktoc_t *p;
	struct nlist *n;

	/*
	 * kvm_nlist() should return 0 (zero) if all symbols were found
	 */
	if ((ret = kvm_nlist(kd, c_nl)) != 0)
		return (ret);

	for (n = c_nl; n->n_name != (char *)0; n++) {
		for (p = ktoc_tab; p->kname != (char *)0; p++) {
			if (strcmp(n->n_name, p->kname) == 0)
				break;
		}
		if (p->cname == (uint_t *)0) 	/* didnt find a match */
			return (-1);
		if ((ret = kvm_read(kd, n->n_value,
		    (char *)p->cname, sizeof (uint_t))) == -1)
			return (ret);
	}
	return (ret);
}

/* initialize buffers, symbols, and global variables for crash session */
int
init(void)
{
	int offset;
	Elf32_Sym *ts_symb = NULL;
	extern void sigint();
	int magic;
	struct stat mem_buf, file_buf;

#if i386
	void get_dump_info(const int, const char *const);
#endif /* i386 */

	if ((mem = open(dumpfile, 0)) < 0)	/* open dump file */
		fatal("cannot open dump file %s\n", dumpfile);
	/*
	 * Set a flag if the dumpfile is of an active system.
	 */
	if (stat("/dev/mem", &mem_buf) == 0 && stat(dumpfile, &file_buf) == 0 &&
	    S_ISCHR(mem_buf.st_mode) && S_ISCHR(file_buf.st_mode) &&
	    mem_buf.st_rdev == file_buf.st_rdev) {
		active = 1;
		dumpfile = NULL;
	}
	if ((kd = kvm_open(namelist, dumpfile, NULL, O_RDONLY, "crash"))
	    == NULL)
		fatal("cannot open kvm - dump file %s\n", dumpfile);

	if ((read_kvm(kd)) == -1)
		fatal("cannot read symbols from dumpfile %s\n", dumpfile);

	rdsymtab(); /* open and read the symbol table */

	/* check version */
	ts_symb = symsrch("version");
	if (ts_symb && (kvm_read(kd, ts_symb->st_value, version, VSIZE))
	    != VSIZE)
		fatal("could not process dumpfile with supplied namelist %s\n",
							namelist);

	if (!(V = symsrch("v")))
		fatal("var structure not found in symbol table\n");
	if (!(Start = symsrch("_start")))
		fatal("start not found in symbol table\n");
	if (!(symsrch("proc_init")))
		fatal("proc not found in symbol table\n");
	readsym("proc_init", &procv, sizeof (int));
	readsym("nproc", &nproc, sizeof (int));
#ifdef sombody_fixes_this
	readsym("bufchain", &bufv, sizeof (int));
#endif
#if i386
	if (!active)
		get_dump_info(mem, dumpfile);
#else /* !i386 */
	if (!(Panic = symsrch("panicstr")))
		fatal("panicstr not found in symbol table\n");
#endif /* !i386 */

	readmem((long)V->st_value, 1, -1, (char *)&vbuf,
		sizeof (vbuf), "var structure");
#if i386
	if (active)	/* Curthread is set in get_dump_info() otherwise */
#endif /* !i386 */
	Curthread = getcurthread();
	Procslot = getcurproc();

	/* setup break signal handling */
	if (signal(SIGINT, sigint) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
}
