/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */
 
#pragma ident   "@(#)kvmplatdep.c	1.3	93/06/23 SMI"

#include "kvm.h"
#include <stdio.h>
#include <varargs.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/vmparam.h>
#include <sys/pte.h>
#include <sys/mmu.h>
#include <vm/seg_vn.h>
#include <vm/seg_map.h>
#include <vm/anon.h>
#include <sys/swap.h>

static struct nlist knl[] = {
        { "Sysmap" },
#define X_SYSMAP        0
        { "Syslimit" },
#define X_SYSLIMIT      1
        { "kvseg" },
#define X_KSEG          2
        { "E_Sysmap" },
#define X_E_SYSMAP      3
        { "E_Syslimit" },
#define X_E_SYSLIMIT    4
        { "E_kvseg" },
#define X_E_KSEG        5
        { "" },
};

#define knlsize (sizeof (knl) / sizeof (struct nlist))

#define	ptob64(p)	((u_longlong_t)(p) << PAGESHIFT)

extern char *malloc();
extern void __kvm_openerror();
extern int __kvm_getkvar();
extern u_longlong_t __kvm_kvtop();
extern int kvm_read();

u_int   Syslimit;       /* max kernel virtual addr mapped by Sysmap */
u_int   E_Syslimit;     /* max kernel virtual addr mapped by E_Sysmap*/
struct pte *Sysmap;     /* kernel pte cache */
struct pte *E_Sysmap;   /* (optional) kernel 'ethernet' pte cache */
struct seg Kseg;        /* segment structure that covers Sysmap */
struct seg E_Kseg;      /* segment structure that covers E_Sysmap */

int
__kvm_open_pd(kd, err)
	kvm_t *kd;
	char *err;
{
        register int msize;
        struct nlist kn[knlsize];

        /* copy static array to dynamic storage for reentrancy purposes */
        memcpy(kn, knl, sizeof (knl));

        /*
         * read kernel data for internal library use
         */
        if (nlist(kd->name, kn) == -1 ) {
		__kvm_openerror(err, "%s: not a kernel namelist", kd->name);
                return(-1);
        }
 
        if (__kvm_getkvar(kd, kn[X_KSEG].n_value,(caddr_t)&Kseg,
            sizeof (struct seg), err, "kvseg") != 0)
                return(-1);
 
        if (kn[X_E_KSEG].n_value) {
                /*
                 * Not all kernels have or need this map
                 */
                if (__kvm_getkvar(kd, kn[X_E_KSEG].n_value, (caddr_t)&E_Kseg,
                    sizeof (struct seg), err, "E_kvseg") != 0)
                        return(-1);
        }
 
        /*
         * XXX  Why do we nlist() for Syslimit and E_Syslimit if we're
         *      going to compute them anyway?
         */
 
        Syslimit = (u_int)Kseg.s_base + Kseg.s_size;
        msize = (Kseg.s_size >> MMU_PAGESHIFT) * sizeof (struct pte);

        Sysmap = (struct pte *)malloc((u_int)msize);

        if (Sysmap == NULL) {
		__kvm_openerror(err, "cannot allocate space for Sysmap");
                return(-1);
	}

        if (__kvm_getkvar(kd, kn[X_SYSMAP].n_value, (caddr_t)Sysmap,
            msize, err, "Sysmap") != 0)
                return(-1);
 
	if (kn[X_E_KSEG].n_value) {
                /*
                 * Not all kernels have or need this map
                 */
                E_Syslimit = (u_int)E_Kseg.s_base + E_Kseg.s_size;
                msize = (E_Kseg.s_size >> MMU_PAGESHIFT)
                    * sizeof (struct pte);
                E_Sysmap = (struct pte *)malloc((u_int)msize);
                if (E_Sysmap == NULL) {
			__kvm_openerror(err, "cannot allocate space for E_Sysmap");
                        return(-1);
                        }
                if (__kvm_getkvar(kd, kn[X_E_SYSMAP].n_value, (caddr_t)E_Sysmap,
                    msize, err, "E_Sysmap") != 0)
                        return(-1);
        }
 
	return(0);	/* success */
}

/* 
 * Called if we are in _kvm_physaddr() and we find
 * we are looking at an addr in segkmem.
 */
int
_kvm_segkmem_paddr(kd, vaddr, fdp, offp)
        kvm_t *kd;
        u_int vaddr;
        int *fdp;
        u_longlong_t *offp;
{
	/* Segkmem segment */
	register int poff;
	register u_longlong_t paddr;
	register u_int p;
	register u_int vbase;

        poff = vaddr & PAGEOFFSET;   /* save offset into page */
        vaddr &= PAGEMASK;           /* round down to start of page */

        if ((vaddr >= (vbase = (u_int)Kseg.s_base)) &&
            (vaddr < Syslimit)) {
                p = (vaddr - vbase) >> PAGESHIFT;
                if (!pte_valid(&Sysmap[p])) {
                        return (-1);
                }

                if (!pte_memory(&Sysmap[p])) {
                        return (-1);
                }

                paddr = poff + ptob64(MAKE_PFNUM(&Sysmap[p]));
        } else if (E_Sysmap &&
            (vaddr >= (vbase = (u_int)E_Kseg.s_base)) &&
            (vaddr < E_Syslimit)) {
                /*
                 * This map is only needed on older machines
                 */
                p = (vaddr - vbase) >> PAGESHIFT;
                if (!pte_valid(&E_Sysmap[p])) {
                        return (-1);
                }

                if (!pte_memory(&E_Sysmap[p])) {
                        return (-1);
                }

                paddr = poff + ptob64(MAKE_PFNUM(&E_Sysmap[p]));
        } else if (vaddr >= (KERNELBASE + NBPG) &&
                        vaddr < kd->econtig) {
                paddr = __kvm_kvtop(kd, vaddr) + poff;
        } else {
                return (-1);
        }
        *fdp = kd->corefd;
        *offp = paddr;

        return (0);
}

int
__kvm_get_pdparams(p, err)
	struct kvm_pdparams_t *p;
	char *err;
{

	p->pagesize = PAGESIZE;
	p->pagemask = PAGEMASK;
	p->pageshift = PAGESHIFT;
	p->pageoffset = PAGEOFFSET;
	p->kernelbase = KERNELBASE;
	p->usrstack = USRSTACK;
	p->kseg	= &Kseg;

	return(0);
}

int
__kvm_get_pdfuncs(f, err)
	struct kvm_pdfuncs_t *f;
	char *err;
{

	f->kvm_segkmem_paddr = &_kvm_segkmem_paddr;

	return(0);
}
