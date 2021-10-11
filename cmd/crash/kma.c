#ident	"@(#)kma.c	1.6	94/12/19 SMI"		/* SVr4.0 1.1.1.1 */

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
 * This file contains code for the crash function kmastat.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/kmem_impl.h>
#include <sys/kstat.h>
#include <sys/elf.h>
#include "crash.h"

static void	kmainit(), prkmastat();

static void kmause(void *kaddr, void *buf,
		u_int size, kmem_bufctl_audit_t *bcp);
static struct nlist Kmem_flags;

Elf32_Sym *kmem_null_cache_sym, *kmem_misc_kstat_sym;

struct {
	kstat_named_t	arena_size;
	kstat_named_t	huge_alloc;
	kstat_named_t	huge_alloc_fail;
	kstat_named_t	perm_size;
	kstat_named_t	perm_alloc;
	kstat_named_t	perm_alloc_fail;
} kmem_misc_kstat;

/* get arguments for kmastat function */
int
getkmastat()
{
	int c;

	kmainit();
	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	(void) redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}
	prkmastat();
	return (0);
}

/* print kernel memory allocator statistics */
static void
prkmastat()
{
	kmem_cache_t c, *cp;
	kmem_cache_stat_t kcs;
	u_int meminuse, total_meminuse = 0;
	u_int total_alloc = 0, total_alloc_fail = 0;
	u_int kmem_null_cache_addr;

	(void) fprintf(fp,
	"                       buf   buf   buf   memory    #allocations\n");
	(void) fprintf(fp,
	"cache name            size avail total   in use    succeed fail\n");
	(void) fprintf(fp,
	"----------           ----- ----- ----- --------    ------- ----\n");

	kmem_null_cache_addr = kmem_null_cache_sym->st_value;
	kvm_read(kd, kmem_null_cache_addr, (char *)&c, sizeof (c));

	for (cp = c.cache_next; cp != (kmem_cache_t *)kmem_null_cache_addr;
	    cp = c.cache_next) {

		kvm_read(kd, (u_int)cp, (char *)&c, sizeof (c));
		if (kmem_cache_getstats(cp, &kcs) == -1) {
			printf("error reading stats for %s\n", c.cache_name);
			continue;
		}

		meminuse = kcs.kcs_slab_size *
			(kcs.kcs_slab_create - kcs.kcs_slab_destroy);

		total_meminuse += meminuse;
		total_alloc += kcs.kcs_alloc;
		total_alloc_fail += kcs.kcs_alloc_fail;

		(void) fprintf(fp, "%-20s %5u %5u %5u %8u %10u %4u\n",
			c.cache_name,
			(u_int)kcs.kcs_buf_size,
			(u_int)kcs.kcs_buf_avail,
			(u_int)kcs.kcs_buf_total,
			meminuse,
			(u_int)kcs.kcs_alloc,
			(u_int)kcs.kcs_alloc_fail);
	}

	kvm_read(kd, kmem_misc_kstat_sym->st_value,
		(char *)&kmem_misc_kstat, sizeof (kmem_misc_kstat));

	total_alloc += kmem_misc_kstat.huge_alloc.value.l;
	total_alloc_fail += kmem_misc_kstat.huge_alloc_fail.value.l;

	(void) fprintf(fp,
	"----------           ----- ----- ----- --------    ------- ----\n");

	(void) fprintf(fp, "%-20s %5s %5s %5s %8u %10u %4u\n",
		"permanent",
		"-",
		"-",
		"-",
		kmem_misc_kstat.perm_size.value.l,
		kmem_misc_kstat.perm_alloc.value.l,
		kmem_misc_kstat.perm_alloc_fail.value.l);

	(void) fprintf(fp, "%-20s %5s %5s %5s %8u %10u %4u\n",
		"oversize",
		"-",
		"-",
		"-",
		kmem_misc_kstat.arena_size.value.l -
			kmem_misc_kstat.perm_size.value.l -
			total_meminuse,
		kmem_misc_kstat.huge_alloc.value.l,
		kmem_misc_kstat.huge_alloc_fail.value.l);

	(void) fprintf(fp,
	"----------           ----- ----- ----- --------    ------- ----\n");

	(void) fprintf(fp, "%-20s %5s %5s %5s %8u %10u %4u\n",
		"Total",
		"-",
		"-",
		"-",
		kmem_misc_kstat.arena_size.value.l,
		total_alloc,
		total_alloc_fail);
}

/* initialization for namelist symbols */
static void
kmainit()
{
	static int kmainit_done = 0;

	if (kmainit_done)
		return;
	if ((kmem_null_cache_sym = symsrch("kmem_null_cache")) == 0)
		(void) error("kmem_null_cache not in symbol table\n");
	if ((kmem_misc_kstat_sym = symsrch("kmem_misc_kstat")) == 0)
		(void) error("kmem_misc_kstat not in symbol table\n");
	kmainit_done = 1;
}

static int kmafull;

/*
 * Print "kmem_alloc*" usage with stack traces when KMF_AUDIT is enabled
 */
int
getkmausers()
{
	int c;
	u_long kmem_flags;
	kmem_cache_t *kma_cache[300];
	int ncaches, i;
	int mem_threshold = 1024;	/* Minimum # bytes for printing */
	int cnt_threshold = 10;		/* Minimum # blocks for printing */
	char *matchstr;
	char *excludelist[10];

	/*
	 * Include everything except those caches that
	 * never have KMF_AUDIT set.
	 */
	matchstr = "";
	excludelist[0] = "kmem_magazine";
	excludelist[1] = "kmem_slab";
	excludelist[2] = "kmem_bufctl";
	excludelist[3] = NULL;

	if (nl_getsym("kmem_flags", &Kmem_flags))
		error("kmem_flags not found in symbol table\n");
	readmem((unsigned)Kmem_flags.n_value, 1, -1, (char *)&kmem_flags,
		sizeof (kmem_flags), "kmem_flags");
	if (!(kmem_flags & KMF_AUDIT))
		error("kmausers requires that KMF_AUDIT be turned on"
			"in kmem_flags");

	kmafull = 0;
	optind = 1;
	while ((c = getopt(argcnt, args, "efw:")) != EOF) {
		switch (c) {
			case 'e':
				mem_threshold = 0;
				cnt_threshold = 0;
				break;
			case 'f':
				kmafull = 1;
				break;
			case 'w':
				redirect();
				break;
			default:
				longjmp(syn, 0);
		}
	}

	init_owner();

	if (args[optind]) {
		kmem_cache_t *cache;
		do {
			cache = kmem_cache_find(args[optind]);
			if (cache == NULL)
				error("Unknown cache: %s\n",
					args[optind]);
				
			kmem_cache_audit_apply(cache, kmause);
			optind++;
		} while (args[optind]);
	} else {
		ncaches = kmem_cache_find_all_excl(matchstr, excludelist,
				kma_cache, 300);
		for (i = 0; i < ncaches; i++) {
			if (kmafull) {
				kmem_cache_t c;
				kmem_cache_t *cp = kma_cache[i];

				if (kvm_read(kd, (u_long)cp, (void *)&c,
				    sizeof (c)) == -1) {
					perror("kvm_read kmem_cache");
					return (-1);
				}

				fprintf(fp, "Cache: %s\n", c.cache_name);
			}
			kmem_cache_audit_apply(kma_cache[i], kmause);
		}
	}
	print_owner("allocations", mem_threshold, cnt_threshold);
	return (0);
}

/* ARGSUSED */
static void
kmause(void *kaddr, void *buf, u_int size, kmem_bufctl_audit_t *bcp)
{
	int i;

	if (kmafull) {
		fprintf(fp, "Allocated size %d, stack depth %d\n",
			size, bcp->bc_depth);
		for (i = 0; i < bcp->bc_depth; i++) {
			fprintf(fp, "\t ");
			prsymbol(NULL, bcp->bc_stack[i]);
		}
	}
	add_owner(bcp, size, size);
}
