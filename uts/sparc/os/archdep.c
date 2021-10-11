/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)archdep.c	1.47	95/10/19 SMI"	/* from SVr4.0 1.83 */

/* Was sparcdep.c - OSAC naming decision */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/vmparam.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/stack.h>
#include <sys/frame.h>
#include <sys/proc.h>
#include <sys/ucontext.h>
#include <sys/siginfo.h>
#include <sys/cpuvar.h>
#include <sys/asm_linkage.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <sys/callo.h>
#include <sys/bootconf.h>
#include <sys/archsystm.h>
#include <sys/fpu/fpusystm.h>
#include <sys/auxv.h>
#include <sys/debug.h>
#include <sys/elf.h>
#include <sys/elf_SPARC.h>
#include <sys/cmn_err.h>
#include <sys/spl.h>

extern struct bootops *bootops;

/*
 * Advertised via /etc/system
 */
int enable_mixed_bcp = 1;

/*
 * Get a pc-only stacktrace.  Used for kmem_alloc() buffer ownership tracking.
 * Returns MIN(current stack depth, pcstack_limit).
 */
int
getpcstack(u_int *pcstack, int pcstack_limit)
{
	struct frame *stacktop = (struct frame *)curthread->t_stk;
	struct frame *prevfp = (struct frame *)KERNELBASE;
	struct frame *fp = (struct frame *)getfp();
	int depth = 0;

	flush_windows();

	while (fp > prevfp && fp < stacktop && depth < pcstack_limit) {
		pcstack[depth++] = (u_int)fp->fr_savpc;
		prevfp = fp;
		fp = (struct frame *)fp->fr_savfp;
	}
	return (depth);
}

/*
 * Allocator for routines which need memory before kmem_init has
 * finished.   Storage returned must be freed via startup_free().
 *
 * Assumes single-threaded, which is right until kmem_init() (at least).
 *
 * This was put here for mutex statistics, and isn't very space efficient.
 * Do not overuse this.
 */
extern caddr_t startup_alloc_vaddr;
extern caddr_t startup_alloc_size;

extern u_int	startup_alloc_chunk_size;

void *
startup_alloc(size_t size, void ** header)
{
	caddr_t	p, q;
	extern u_int kernelmap;

	ASSERT(size < MMU_PAGESIZE);
	ASSERT(!kmem_ready);
	ASSERT(size != 0);

	/*
	 * Make sure size is at least big enough for a double.
	 * This also guarantees double-word alignment.
	 * PTR24_ALIGN is bigger than a double and a power of 2.
	 */
	size = (size + PTR24_ALIGN - 1) & ~(PTR24_ALIGN - 1);
	if (*header == NULL) {
		u_int	chunk_size = startup_alloc_chunk_size;

		ASSERT(kernelmap == 0);	/* don't alloc after kernelmap setup */
		/*
		 * nothing free, grab some pages from the boot and put pieces
		 * on the free list.  This works best when size is small.
		 */
		p = (caddr_t)BOP_ALLOC(bootops,
			startup_alloc_vaddr, chunk_size, BO_NO_ALIGN);
		startup_alloc_vaddr = p + chunk_size;
		startup_alloc_size += chunk_size;
		q = p + chunk_size;
		for (; p + size < q; p = p + size) {
			* (void **)p = *header; /* add p to free list */
			*header = (void *)p;
		}
	}

	p = (caddr_t)*header;
	*header = (void *)*(caddr_t *)p;
	bzero(p, size);
	return ((void *)p);
}

/* ARGSUSED */
void
startup_free(void *p, size_t size, void **header)
{
	* (void **)p = *header;
	*header = p;
}

/*
 * The following ELF header fields are defined as processor-specific
 * in the SPARC V8 ABI:
 *
 *	e_ident[EI_DATA]	encoding of the processor-specific
 *				data in the object file
 *	e_machine		processor identification
 *	e_flags			processor-specific flags associated
 *				with the file
 */

/*
 * The value of at_flags reflects a platform's cpu module support.
 * at_flags is used to check for allowing a binary to execute and
 * is passed as the value of the AT_FLAGS auxiliary vector.
 */
int at_flags = 0;

/*
 * Check the processor-specific fields of an ELF header.
 *
 * returns 1 if the fields are valid, 0 otherwise
 */
int
elfheadcheck(
	unsigned char e_data,
	Elf32_Half e_machine,
	Elf32_Word e_flags)
{
	if (e_data != ELFDATA2MSB)
		return (0);

	if (e_machine != EM_SPARC) {
		if (e_machine != EM_SPARC32PLUS)
			return (0);
		if ((e_flags & EF_SPARC_32PLUS) == 0)
			return (0);
		if ((e_flags & ~at_flags) & EF_SPARC_32PLUS_MASK)
			return (0);
	} else if (e_flags != 0) {
		return (0);
	}
	return (1);
}

/*
 * Set the processor-specific fields of an ELF header.
 */
void
elfheadset(
	unsigned char *e_data,
	Elf32_Half *e_machine,
	Elf32_Word *e_flags)
{
	*e_data = ELFDATA2MSB;
	*e_machine = EM_SPARC;
	*e_flags = 0;
}

int auxv_hwcap_mask = 0;	/* user: patch for broken cpus, debugging */
int kauxv_hwcap_mask = 0;	/* kernel: patch for broken cpus, debugging */

/*
 * Gather information about the processor
 * Determine if the hardware supports mul/div instructions
 * Determine whether we'll use 'em in the kernel or in userland.
 */
void
bind_hwcap(void)
{
	auxv_hwcap = get_hwcap_flags(0) & ~auxv_hwcap_mask;
	kauxv_hwcap = get_hwcap_flags(1) & ~kauxv_hwcap_mask;

#ifndef	__sparcv9
	/*
	 * Conditionally switch the kernels .umul, .div etc. to use
	 * the whizzy instructions.  The processor better be able
	 * to handle them!
	 */
	if (kauxv_hwcap)
		kern_use_hwinstr(kauxv_hwcap & AV_SPARC_HWMUL_32x32,
		    kauxv_hwcap & AV_SPARC_HWDIV_32x32);
#ifdef DEBUG
	/* XXX	Take this away! */
	cmn_err(CE_CONT, "?kernel: %sware multiply, %sware divide\n",
	    kauxv_hwcap & AV_SPARC_HWMUL_32x32 ? "hard" : "soft",
	    kauxv_hwcap & AV_SPARC_HWDIV_32x32 ? "hard" : "soft");
	cmn_err(CE_CONT, "?  user: %sware multiply, %sware divide\n",
	    auxv_hwcap & AV_SPARC_HWMUL_32x32 ? "hard" : "soft",
	    auxv_hwcap & AV_SPARC_HWDIV_32x32 ? "hard" : "soft");
#endif
#endif
}

void
sync_icache(caddr_t va, u_int len)
{
	u_int va1, end;
	extern void doflush();
	/* start with a double word aligned addr */
	va1 = (u_int) va >> 3;
	va1 = (va1 << 3);
	end = (u_int) (va + len);
	while (va1 < end) {
		doflush(va1);
		va1 = va1+8;
	}
}

int
__ipltospl(int ipl)
{
	return (ipltospl(ipl));
}
