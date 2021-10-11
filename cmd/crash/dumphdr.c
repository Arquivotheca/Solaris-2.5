/*
 * Copyright (c) 1992 Sun Microsystems, Inc.  All Rights Reserved.
 */

#ident	"@(#)dumphdr.c	1.5	94/06/10 SMI"

/*
 * This file contains routines to handle the dump file header info.
 */

#include <stdio.h>
#include <sys/elf.h>
#include <sys/time.h>
#include <sys/dumphdr.h>
#include <sys/reg.h>
#include "crash.h"

#define	MAX_PANIC	256	/* Maximum panic string we'll copy */

struct dumphdr *dhp = NULL;
char panic_str[MAX_PANIC + 1];	/* to be used by 'status' subcommand */
int panic_eip, panic_esp;	/* to be used by 'status' subcommand */
struct cpu *Curcpu;		/* current cpu */

/*
 * Sanity checks over the dump header.
 */
static void
chk_dumphdr(const struct dumphdr *const dumphdr, const char *dumpname)
{
	if (dumphdr->dump_magic != DUMP_MAGIC)
		fatal("%s: bad dump magic number", dumpname);
	if (dumphdr->dump_version < DUMP_VERSION)
		fatal("%s: bad dump version number", dumpname);
	if (!(dumphdr->dump_flags & DF_VALID))
		fatal("%s: invalid core dump", dumpname);
	if (!(dumphdr->dump_flags & DF_COMPLETE))
		fatal("%s: incomplete core dump", dumpname);
}

/*
 * Sets Curthread, Curcpu, panic_str, panic_eip and panic_esp
 * from the dump header.
 */
void
get_dump_info(const int mem, const char *const dumpname)
{
	caddr_t kaddr;
	struct dumphdr dumphdr;
	Elf32_Sym *panic_reg_sym;
	Elf32_Sym *symsrch(char *);
	static void chk_dumphdr(const struct dumphdr *const, const char *const);

	readmem(0, 0, -1, (char *)&dumphdr, sizeof (struct dumphdr),
	    (char *)dumpname);
	chk_dumphdr(&dumphdr, dumpname);

	dhp = (struct dumphdr *)malloc(dumphdr.dump_headersize);
	if (dhp == NULL)
		fatal("cannot allocate dump headr");

	readmem(0, 0, -1, (char *)dhp, dumphdr.dump_headersize,
	    (char *)dumpname);
	readmem(dhp->dump_panicstringoff, 0, -1, (char *) &panic_str,
	    MAX_PANIC + 1, (char *)dumpname);

	Curthread = (kthread_id_t)dhp->dump_thread;
	Curcpu = (struct cpu *)dhp->dump_cpu;
	free(dhp);

	panic_reg_sym = symsrch("panic_reg");
	/*
	 * trap() sets panic_reg to point to all the registers at the
	 * time of crash in kernel.  If it is NULL, trap() was not
	 * called.  Instead cmn_err() is called and the registers should
	 * be obtained from the t_pcb of the current thread.
	 */
	if (panic_reg_sym == NULL)
		error("Could not find symbol panic_reg\n");
	readbuf(panic_reg_sym->st_value, 0, 0, -1, (char *)&kaddr,
	    sizeof (kaddr), "pointer to current thread's registers");
	if (kaddr == NULL) {
		label_t *t_pcb, pcb;
		kthread_t *thread = (kthread_t *)NULL;

		/*
		 * A kernel assertion is failed and the dump is produced by
		 * a call to cmn_err() in the kernel which eventually
		 * calls dumpsys() - bypassing trap() - i.e. panic_reg is
		 * invalid.  The following registers are moved to the
		 * setjmp() buffer and saved in t_pcb of Curthread in
		 * this order: edi, esi, ebx, ebp, esp, eip.
		 */
		t_pcb = (label_t *)((unsigned long)Curthread +
		    (unsigned long)&thread->t_pcb);
		readbuf((unsigned)t_pcb, 0, 0, -1, (char *)&pcb, sizeof (pcb),
		    "pcb of current thread");
		panic_esp = pcb.val[4];
		panic_eip = pcb.val[5];
	} else {
		struct regs regs;

		/*
		 * trap() is called and panic_reg contain the registers
		 * of the crashing thread.
		 */
		readbuf((unsigned)kaddr, 0, 0, -1, (char *)&regs, sizeof (regs),
		    "Current thread registers");
		panic_eip = regs.r_eip;
		panic_esp = regs.r_esp;
	}
}
