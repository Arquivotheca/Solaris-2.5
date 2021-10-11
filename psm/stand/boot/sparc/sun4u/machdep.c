/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma ident "@(#)machdep.c	1.6	94/12/10 SMI"


#include <sys/types.h>
#include <sys/machparam.h>

#ifdef MPSAS

void sas_symtab(int start, int end);
extern int sas_command(char *cmdstr);

/*
 * SAS support - inform SAS of new symbols being dynamically added
 * during simulation via the first standalone.
 */

#ifndef	BUFSIZ
#define	BUFSIZ	1024		/* for cmd string buffer allocation */
#endif

int	sas_symdebug = 0;		/* SAS support */

void
sas_symtab(int start, int end)
{
	char *addstr = "symtab add $LD_KERNEL_PATH/%s%s 0x%x 0x%x\n";
	char *file, *prefix, cmdstr[BUFSIZ];
	extern char filename[];

	file = filename;
	prefix = *file == '/' ? "../../.." : "";

	sprintf(cmdstr, addstr, prefix, file, start, end);

	/* add the symbol table */
	if (sas_symdebug) printf("sas_symtab: %s", cmdstr);
	sas_command(cmdstr);
}

void
sas_bpts()
{
	sas_command("file $KERN_SCRIPT_FILE\n");
}
#endif	/* MPSAS */

/*
 * Stubs for sun4c/sunmmu routines.
 */

int fiximp_sun4c() {}
int v0_silence_nets() {}
void map_child(caddr_t v, caddr_t p) {}
void dump_mmu(void) {}
void sunm_cache_prog(u_long start, u_long end) {}
void sunm_turnon_cache(void) {}
void sunm_vac_flush(caddr_t v, u_int nbytes) {}
int l14enable() {}

short cputype;
int pagesize = PAGESIZE;
int vac = 1;

/*
 * XXX: This is a cheesy fix to a icache flush problem in krtld. This
 * should be fixed in the platform-dependent boot project.
 */
u_int icache_flush = 1;

#define	MAGIC_PHYS	(4*1024*1024)
caddr_t magic_phys = (caddr_t) MAGIC_PHYS;
caddr_t top_bootmem = (caddr_t) MAGIC_PHYS;
caddr_t	hole_start;
caddr_t	hole_end;
