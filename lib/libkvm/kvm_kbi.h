/*
 * Copyright (c) 1987-1993, by Sun Microsystems, Inc.
 */

#ifndef	_KVM_KBI_H
#define	_KVM_KBI_H

#pragma ident	"@(#)kvm_kbi.h	1.1	94/06/08 SMI"

#include <sys/types.h>
#include <vm/as.h>
#include <vm/page.h>
#include <sys/kvtopdata.h>
#include <nlist.h>
#include <sys/user.h>
#include <sys/proc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This structure is shared with implementors of platform
 * dependent libkvm modules (lkvm_pd.so).  It should still
 * be considered opaque by all except implementors of
 * lkvm_pd.so modules.
 *
 * This is an uncommitted interface, and thus any reliance on it
 * by programmers is apt to result in future incompatibility for
 * their programs. Only the documented libkvm routines [man (3k)]
 * should be used by applications.
 */

struct _kvmd {			/* libkvm dynamic variables */
	int	wflag;		/* true if kernel opened for write */
	int	namefd;		/* namelist file descriptor */
	int	corefd;		/* corefile file descriptor */
	int	virtfd;		/* virtual memory file descriptor */
	int	swapfd;		/* swap file descriptor */
	char	*name;		/* saved name of namelist file */
	char	*core;		/* saved name of corefile file */
	char	*virt;		/* saved name of virtual memory file */
	char	*swap;		/* saved name of swap file */
	u_int	nproc;		/* number of process structures */
	u_long	proc;		/* address of process table */
	u_long	practp;		/* address of pointer to active process */
	u_int	econtig;	/* end of contiguous kernel memory */
	struct as Kas;		/* kernel address space */
	struct seg Ktextseg;	/* segment that covers kernel text+data */
	struct seg Kseg;	/* segment that covers kmem_alloc space */
	struct seg_ops *segvn;	/* ops vector for segvn segments */
	struct seg_ops *segmap;	/* ops vector for segmap segments */
	struct seg_ops *segdev;	/* ops vector for segdev segments */
	struct seg_ops *segkmem; /* ops vector for segkmem segments */
	struct seg_ops *segkp;	/* ops vector for segkp segments */
	struct swapinfo *sip;	/* ptr to linked list of swapinfo structures */
	struct swapinfo *swapinfo; /* kernel virtual addr of 1st swapinfo */
	struct memseg memseg;	/* first memory segment */
	struct vnode *kvp;	/* vp used with segkp/no anon */
	u_int	page_hashsz;	/* number of buckets in page hash list */
	struct page **page_hash; /* address of page hash list */
	struct proc *pbuf;	/* pointer to process table cache */
	int	pnext;		/* kvmnextproc() pointer */
	char	*uarea;		/* pointer to u-area buffer */
	char	*sbuf;		/* pointer to process stack cache */
	int	stkpg;		/* page in stack cache */
	struct seg useg;	/* segment structure for user pages */
	struct as Uas;		/* default user address space */
	struct condensed	{
		struct dumphdr	*cd_dp;	/* dumphdr pointer */
		off_t	*cd_atoo;	/* pointer to offset array */
		int	cd_atoosize;	/* number of elements in array */
		int	cd_chunksize;	/* size of each chunk, in bytes */
		} *corecdp;	/* if not null, core file is condensed */
	struct condensed *swapcdp; /* if not null, swap file is condensed */
	struct kvtophdr kvtophdr; /* for __kvm_kvtop() */
	struct kvtopent *kvtopmap;
	struct kvm_param *kvm_param;
};

/*
 * Platform-dependendent constants.
 */
struct kvm_param {
	u_int	p_pagesize;
	u_int	p_pagemask;
	u_int	p_pageshift;
	u_int	p_pageoffset;
	u_int	p_kernelbase;
	u_int	p_usrstack;
};

/*
 * The following structure is used to hold platform dependent
 * info.  It is  only of interest to implementors of lkvm_pd.so
 * modules.
 */
struct kvm_pdfuncs {
	int	(*kvm_segkmem_paddr)();
};

u_longlong_t	__kvm_kvtop(kvm_t *, u_int);
int		__kvm_getkvar(kvm_t *, u_long, caddr_t, int, char *, char *);

#ifdef	__cplusplus
}
#endif

#endif	/* _KVM_KBI_H */
