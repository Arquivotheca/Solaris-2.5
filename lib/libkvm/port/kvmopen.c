/*
 * Copyright (c) 1987-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)kvmopen.c	2.45	95/10/30 SMI"

#include "kvm_impl.h"
#include <stdio.h>
#include <nlist.h>
#include <varargs.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/swap.h>
#include <sys/sysmacros.h>	/* for roundup() */
#include <sys/proc.h>
#include <sys/thread.h>
#include <assert.h>
#include <dlfcn.h>
#include <sys/systeminfo.h>
#include <sys/dumphdr.h>

/*
 * This used to be in sys/param.h in 4.1.
 * It now lives in sys/fs/ufs_fs.h, which is a strange place for
 * non-fs specific macros.  Anyhow, we just copy it to avoid hauling
 * in a whole load of fs-specific include files.
 */
#define	isset(a, i)	((a)[(i)/NBBY] & (1<<((i)%NBBY)))

static struct nlist knl[] = {
	{ "practive" },
#define	X_PROC		0
	{ "nproc" },
#define	X_NPROC		1
	{ "segvn_ops" },
#define	X_SEGVN		2
	{ "segmap_ops" },
#define	X_SEGMAP	3
	{ "segdev_ops" },
#define	X_SEGDEV	4
	{ "swapinfo" },
#define	X_SWAPINFO	5
	{ "memsegs" },
#define	X_MEMSEGS	6
	{ "page_hash" },
#define	X_PAGE_HASH	7
	{ "page_hashsz" },
#define	X_PAGE_HASHSZ	8
	{ "econtig" },
#define	X_ECONTIG	9
	{ "kas" },
#define	X_KAS		10
	{ "segkmem_ops" },
#define	X_SEGKMEM	11
	{ "segkp_ops" },
#define	X_SEGKP		12
	{ "kvp" },
#define	X_KVP		13
	{ "_pagesize" },
#define	X_PAGESIZE	14
	{ "_pageshift" },
#define	X_PAGESHIFT	15
	{ "_pagemask" },
#define	X_PAGEMASK	16
	{ "_pageoffset" },
#define	X_PAGEOFFSET	17
	{ "_kernelbase" },
#define	X_KERNELBASE	18
	{ "_usrstack" },
#define	X_USRSTACK	19
	{ "kvseg" },
#define	X_KVSEG		20
	{ "end" },
#define	X_END		21
	{ "" },
};

#define	knlsize	(sizeof (knl) / sizeof (struct nlist))

static int readkvar(kvm_t *, u_longlong_t, caddr_t, int, char *, char *);
static int getkvtopdata(kvm_t *, char *, char *);

/*
 * Want to share these routines with lkvm_pd.so, so
 * cant declare them "static".
 */

static int condensed_setup(int, struct condensed **, char *, char *);
static void condensed_takedown(struct condensed *);
static int init_platdep(kvm_t *, char *, char *);

/*
 * XXX-As currently the <stdarg.h> in our build environment is not done
 *     properly, the full function prototype for the following cannot
 *     be presented.  This applies to __kvm_openerror() as well.
 */
static void openperror();
void __kvm_openerror();

/*
 * The known entry points into the platform dependent .so
 */
static const char pd_open[] = "__kvm_open_pd";
static const char pd_param[] = "__kvm_get_pdparams";
static const char pd_func[] = "__kvm_get_pdfuncs";

/*
 * Open kernel namelist and core image files and initialize libkvm
 */
kvm_t *
kvm_open(char *namelist, char *corefile, char *swapfile, int flag, char *err)
{
	register long msize;
	register kvm_t *kd;
	struct nlist kn[knlsize];
	u_long memseg;
	struct seg *seg;
	struct stat membuf; 	/* live mem */
	struct stat fbuf;	/* user-specified core */
	char psm_ptype[MAXNAMELEN];	/* platform name */

	kd = (kvm_t *)malloc((u_int)sizeof (struct _kvmd));
	if (kd == NULL) {
		openperror(err, "cannot allocate space for kvm_t");
		return (NULL);
	}
	kd->namefd = -1;
	kd->corefd = -1;
	kd->virtfd = -1;
	kd->swapfd = -1;
	kd->pnext = 0;
	kd->uarea = NULL;
	kd->pbuf = NULL;
	kd->sbuf = NULL;
	kd->econtig = NULL;
	kd->sip = NULL;
	kd->corecdp = NULL;
	kd->swapcdp = NULL;
	kd->proc = NULL;
	kd->kvm_param = NULL;
	kd->kvtopmap = NULL;

	/* copy static array to dynamic storage for reentrancy purposes */
	memcpy(kn, knl, sizeof (knl));

	switch (flag) {
	case O_RDWR:
		kd->wflag = 1;
		break;
	case O_RDONLY:
		kd->wflag = 0;
		break;
	default:
		__kvm_openerror(err, "illegal flag 0x%x to kvm_open()", flag);
		goto error;
	}

	if (namelist == NULL)
		namelist = LIVE_NAMELIST;
	kd->name = namelist;
	if ((kd->namefd = open(kd->name, O_RDONLY, 0)) < 0) {
		openperror(err, "cannot open %s", kd->name);
		goto error;
	}

	if (corefile == NULL)
		corefile = LIVE_COREFILE;
	/*
	 * Check if corefile is really /dev/mem.
	 */
	if (stat(LIVE_COREFILE, &membuf) == 0 &&
	    stat(corefile, &fbuf) == 0 &&
	    S_ISCHR(fbuf.st_mode) && fbuf.st_rdev == membuf.st_rdev) {
		kd->virt = LIVE_VIRTMEM;
		if ((kd->virtfd = open(kd->virt, flag, 0)) < 0) {
			openperror(err, "cannot open %s", kd->virt);
			goto error;
		}
	}
	/*
	 * We have a live kernel iff kd->virt > 0, and that can only
	 * happen if corefile is NULL or LIVE_COREFILE.
	 */
	kd->core = corefile;
	if ((kd->corefd = open(kd->core, flag, 0)) < 0) {
		openperror(err, "cannot open %s", kd->core);
		goto error;
	}
	if (condensed_setup(kd->corefd, &kd->corecdp, kd->core, err) == -1)
		goto error;	/* condensed_setup calls openperror */
	if (swapfile != NULL) {
		kd->swap = swapfile;
		if ((kd->swapfd = open(kd->swap, O_RDONLY, 0)) < 0) {
			openperror(err, "cannot open %s", kd->swap);
			goto error;
		}
		if (condensed_setup(kd->swapfd, &kd->swapcdp, kd->swap,
				    err) == -1)
			goto error;
	}

	/*
	 * read kernel data for internal library use
	 */
	if (nlist(kd->name, kn) == -1) {
		__kvm_openerror(err, "%s: not a kernel namelist", kd->name);
		goto error;
	}

	/*
	 * fetch the translation table from the memory image
	 * so that we can translate virtual addresses in the range
	 * KERNELBASE -> econtig into physical offsets.  And no,
	 * we can't just subtract KERNELBASE from the va any more ..
	 */
	if (getkvtopdata(kd, err, "kvtopdata") != 0) {
		__kvm_openerror(err, "%s: unable to read kvtopdata", kd->core);
		goto error;
	}
	/*
	 * Initialize system parameters.
	 *
	 * XXX There should be a better kernel/libkvm interface
	 * for passing information.  At the very least, this
	 * stuff should be packaged, so that it can be done with
	 * one read.  Perhaps when the KBI is finalized ...
	 */
	kd->kvm_param = (struct kvm_param *)malloc(sizeof (struct kvm_param));
	if (kd->kvm_param == NULL) {
		openperror(err, "cannot allocate space for kvm_param");
		goto error;
	}
	if (__kvm_getkvar(kd, kn[X_PAGESIZE].n_value, (caddr_t)&kd->pagesz,
	    sizeof (int), err, "pagesize") != 0)
		goto error;

	if (__kvm_getkvar(kd, kn[X_PAGESHIFT].n_value, (caddr_t)&kd->pageshift,
	    sizeof (int), err, "pageshift") != 0)
		goto error;

	if (__kvm_getkvar(kd, kn[X_PAGEMASK].n_value, (caddr_t)&kd->pagemask,
	    sizeof (int), err, "pagemask") != 0)
		goto error;

	if (__kvm_getkvar(kd, kn[X_PAGEOFFSET].n_value,
	    (caddr_t)&kd->pageoffset, sizeof (int), err, "pageoffset") != 0)
		goto error;

	if (__kvm_getkvar(kd, kn[X_KERNELBASE].n_value,
	    (caddr_t)&kd->kernelbase, sizeof (int), err, "kernelbase") != 0)
		goto error;

	if (__kvm_getkvar(kd, kn[X_USRSTACK].n_value, (caddr_t)&kd->usrstack,
	    sizeof (int), err, "usrstack") != 0)
		goto error;

	if (__kvm_getkvar(kd, kn[X_KAS].n_value, (caddr_t)&kd->Kas,
	    sizeof (struct as), err, "kas") != 0)
		goto error;

	if (kd->Kas.a_lrep == AS_LREP_LINKEDLIST)
		seg = kd->Kas.a_segs.list;
	else {
		seg_skiplist ssl;

		if (__kvm_getkvar(kd, (u_long)kd->Kas.a_segs.skiplist,
		    (caddr_t)&ssl, sizeof (seg_skiplist), err,
		    "ktextseg skiplist") != 0)
			goto error;

		seg = ssl.segs[0];
	}
	if (__kvm_getkvar(kd, (u_long)seg, (caddr_t)&kd->Ktextseg,
	    sizeof (struct seg), err, "ktextseg") != 0)
		goto error;

	if (__kvm_getkvar(kd, kn[X_KVSEG].n_value, (caddr_t)&kd->Kseg,
	    sizeof (struct seg), err, "kvseg") != 0)
		goto error;

	if (__kvm_getkvar(kd, kn[X_ECONTIG].n_value, (caddr_t)&kd->econtig,
	    sizeof (int), err, "econtig") != 0)
		goto error;

#define	getval(o, b)	\
	if (kvm_read(kd, kn[o].n_value, 				\
		(caddr_t)&kd->b, sizeof (kd->b)) != sizeof (kd->b)) {	\
		__kvm_openerror(err, "%s: can't find %s", kd->name, #b);\
		goto error;						\
	}

	/* save segment mapping and process table info */
	kd->segvn = (struct seg_ops *)kn[X_SEGVN].n_value;
	kd->segmap = (struct seg_ops *)kn[X_SEGMAP].n_value;
	kd->segkmem = (struct seg_ops *)kn[X_SEGKMEM].n_value;
	kd->segdev = (struct seg_ops *)kn[X_SEGDEV].n_value;
	kd->segkp = (struct seg_ops *)kn[X_SEGKP].n_value;
	kd->kvp = (struct vnode *)kn[X_KVP].n_value;

	/*
	 * Got to do this before calling the init_platdep() routine
	 * since we will be using this info there.
	 */
	if (sysinfo(SI_PLATFORM, psm_ptype, sizeof (psm_ptype)) == -1) {
		__kvm_openerror(err, "Unable to determine platform name");
		goto error;
	}


#if 0
	/*
	 * XXX These hooks have been left here for later KBI work;
	 * currently we aren't using any platform-dependent
	 * shared libraries.
	 *
	 * Call the platform-dependent initialization routine.
	 */
	if ((init_platdep(kd, psm_ptype, err)) != 0) {
		__kvm_openerror(err, "Unable to init platform dependent code");
		goto error;
	}
#endif
	kvm_read(kd, kn[X_MEMSEGS].n_value, (caddr_t)&memseg, sizeof (memseg));
	kvm_read(kd, memseg, (caddr_t)&kd->memseg, sizeof (struct memseg));
	getval(X_SWAPINFO, swapinfo);
	getval(X_PAGE_HASH, page_hash);
	getval(X_PAGE_HASHSZ, page_hashsz);

	/*
	 * In order to determine the current proc, we must first
	 * determine the current thread.
	 */
	{
		static proc_t *prc;

		kvm_read(kd, kn[X_PROC].n_value, (caddr_t)&prc,
		    sizeof (proc_t *));
		kd->proc = (u_long)prc;
	}
	kd->practp = kn[X_PROC].n_value;
	kd->pnext = kd->proc;
	getval(X_NPROC, nproc);

	return (kd);

error:
	(void) kvm_close(kd);
	return (NULL);
}

/*
 * Translate kernel virtual address in the range KERNELBASE
 * to econtig to a physical offset in the memory image
 */
u_longlong_t
__kvm_kvtop(kvm_t *kd, u_int va)
{
	register int	i;
	register u_int	pagesize = kd->kvtophdr.pagesize;

	for (i = 0; i < kd->kvtophdr.nentries; i++) {
		struct kvtopent *ke = kd->kvtopmap + i;

		if (ke->kvpm_vaddr <= va &&
		    va < ke->kvpm_vaddr + ke->kvpm_len * pagesize) {
			return ((u_longlong_t)ke->kvpm_pfnum * pagesize
			    + (va - ke->kvpm_vaddr));
		}
	}

	return ((u_longlong_t)-1);	/* an "impossible" address */
}

/*
 * On a kernel core dump, readkvar() can only be used to read the
 * chunks by remapping the physical addresses through _uncondense().
 * Use readumphdr() to read the dump header.
 */
static int
readkvar(kvm_t *kd, u_longlong_t paddr, caddr_t buf, int size,
    char *err, char *name)
{
	assert(kd->corefd > 0);
	if (_uncondense(kd, kd->corefd, &paddr)) {
		openperror(err, "%s: uncondense error on %s", kd->core, name);
		return (-1);
	}
	if (llseek(kd->corefd, (offset_t)paddr, L_SET) == -1) {
		openperror(err, "%s: seek error on %s", kd->core, name);
		return (-1);
	}
	if (read(kd->corefd, buf, size) != size) {
		openperror(err, "%s: read error on %s", kd->core, name);
		return (-1);
	}
	return (0);
}

static int
readkvirt(kvm_t *kd, u_long vaddr, char *buf, int size, char *err, char *name)
{
	assert(kd->virtfd > 0);
	if (lseek(kd->virtfd, (off_t)vaddr, L_SET) == -1) {
		openperror(err, "%s: seek error on %s", kd->virt, name);
		return (-1);
	}
	if (read(kd->virtfd, buf, size) != size) {
		openperror(err, "%s: read error on %s", kd->virt, name);
		return (-1);
	}
	return (0);
}

static int
getkvtopdata(kvm_t *kd, char *err, char *name)
{
	u_longlong_t kvtopaddr = 0;
	struct dumphdr *dp;
	static struct nlist nl[] = {
		{ "kvtopdata" },
		{ "" }
	};
	int maplen;

	/*
	 * If the core image is a dump, we retrieve kvtopdata
	 * by using the physical address stored in the dump header.
	 * Otherwise, we simply nlist to get the virtual address.
	 * First, read the header...
	 */
	if (kd->virtfd == -1) {
		if (llseek(kd->corefd, (offset_t)0, L_SET) == -1) {
			__kvm_openerror(err,
			    "%s: seek error on dumphdr", kd->core);
			return (-1);
		}
		dp = (struct dumphdr *)mmap(0, sizeof (struct dumphdr),
		    PROT_READ, MAP_PRIVATE, kd->corefd, 0);
		if (dp == (struct dumphdr *)-1) {
			__kvm_openerror(err,
			    "%s: unable to locate kvtopdata", kd->core);
			return (-1);
		}
		kvtopaddr = dp->dump_kvtop_paddr;
		(void) munmap((caddr_t)dp, sizeof (struct dumphdr));

		if (readkvar(kd, kvtopaddr, (char *)&kd->kvtophdr,
		    sizeof (struct kvtophdr), err, name) == -1) {
			__kvm_openerror(err, "%s: unable to read %s",
			    kd->core, name);
			return (-1);
		}
	} else {
		if (nlist(kd->name, nl) == -1) {
			__kvm_openerror(err,
			    "%s: unable to locate kvtopdata", kd->core);
			return (-1);
		}
		if (readkvirt(kd, nl[0].n_value, (char *)&kd->kvtophdr,
		    sizeof (struct kvtophdr), err, "kvtophdr") == -1) {
			__kvm_openerror(err,
			    "%s: unable to read %s", kd->core, name);
			return (-1);
		}
	}
	/*
	 * Sanity check.
	 */
	if (kd->kvtophdr.version != KVTOPD_VER) {
		__kvm_openerror(err, "%s: corrupt %s", kd->core, name);
		return (-1);
	}
	/*
	 * Allocate space for the maps.
	 */
	maplen = kd->kvtophdr.nentries * sizeof (struct kvtopent);
	kd->kvtopmap = (struct kvtopent *)malloc(maplen);
	if (kd->kvtopmap == NULL) {
		__kvm_openerror(err, "%s: unable to allocate kvtopmaps",
		    kd->core);
		return (-1);
	}
	/*
	 * ... and now read the map entries.
	 */
	if (kd->virtfd == -1) {
		if (readkvar(kd, kvtopaddr + sizeof (struct kvtophdr),
			(char *)kd->kvtopmap, maplen, err, name) == -1) {
			__kvm_openerror(err, "%s: unable to read kvtopmaps",
			    kd->core);
			return (-1);
		}
	} else {
		if (readkvirt(kd, nl[0].n_value + sizeof (struct kvtophdr),
		    (char *)kd->kvtopmap, maplen, err, "kvtopmap") == -1) {
			__kvm_openerror(err, "%s: unable to read kvtopmap",
			    kd->core, name);
			return (-1);
		}
	}
	return (0);
}

int
__kvm_getkvar(kvm_t *kd, u_long kaddr, caddr_t buf, int size, char *err,
    char *name)
{
	u_longlong_t paddr;

	paddr = __kvm_kvtop(kd, kaddr);
	return (readkvar(kd, paddr, buf, size, err, name));
}

static int
condensed_setup(int fd, struct condensed **cdpp, char *name, char *err)
{
	struct dumphdr *dp = (struct dumphdr *)-1;
	struct condensed *cdp;
	char *bitmap = (char *)-1;
	off_t *atoo = NULL;
	off_t offset;
	int bitmapsize;
	int pagesize;
	int chunksize;
	int nchunks;
	int i;
	int rc = 0;

	*cdpp = cdp = NULL;

	/* See if beginning of file is a dumphdr */
	dp = (struct dumphdr *)mmap(0, sizeof (struct dumphdr),
	    PROT_READ, MAP_PRIVATE, fd, 0);

	if ((int)dp == -1) {
		/* This is okay; assume not condensed */
		KVM_ERROR_2("cannot mmap %s; assuming non_condensed", name);
		goto not_condensed;
	}
	if (dp->dump_magic != DUMP_MAGIC ||
	    (dp->dump_flags & DF_VALID) == 0) {
		/* not condensed */
		goto not_condensed;
	}
	pagesize = dp->dump_pagesize;

	/* get the bitmap */
	bitmapsize = dp->dump_bitmapsize;
	bitmap = mmap(0, bitmapsize, PROT_READ, MAP_PRIVATE, fd, pagesize);
	if ((int)bitmap == -1) {
		openperror(err, "cannot mmap %s's bitmap", name);
		goto error;
	}

	/* allocate the address to offset table */
	if ((atoo = (off_t *)
		calloc(bitmapsize * NBBY, sizeof (*atoo))) == NULL) {
		openperror(err, "cannot allocate %s's uncondensing table",
		    name);
		goto error;
	}

	/* allocate the condensed structure */
	if ((cdp = (struct condensed *)
	    malloc(sizeof (struct condensed))) == NULL) {
		openperror(err, "cannot allocate %s's struct condensed",
		    name);
		goto error;
	}
	cdp->cd_atoo = atoo;
	cdp->cd_atoosize = bitmapsize * NBBY;
	cdp->cd_dp = dp;
	*cdpp = cdp;

	cdp->cd_chunksize = chunksize = pagesize * dp->dump_chunksize;
	offset = pagesize + roundup(bitmapsize, pagesize);
	nchunks = dp->dump_nchunks;

	for (i = 0; nchunks > 0; i++) {
		if (isset(bitmap, i)) {
			atoo[i] = offset;
			offset += chunksize;
			nchunks--;
		}
	}

	goto done;

error:
	rc = -1;
not_condensed:
	if (cdp) {
		free((char *)cdp);
		*cdpp = NULL;
	}
	if ((int)dp != -1)
		(void) munmap((caddr_t)dp, sizeof (*dp));
	if (atoo)
		free((char *)atoo);
done:
	if ((int)bitmap != -1)
		(void) munmap((caddr_t)bitmap, bitmapsize);

	return (rc);
}

static void
condensed_takedown(struct condensed *cdp)
{
	struct dumphdr *dp;
	off_t *atoo;

	if (cdp) {
		if ((int)(dp = cdp->cd_dp) != -1) {
			(void) munmap((caddr_t)dp, sizeof (*dp));
		}
		if ((atoo = cdp->cd_atoo) != NULL) {
			free((char *)atoo);
		}
		free((char *)cdp);
	}
}

/*
 * Close files associated with libkvm
 */
int
kvm_close(kvm_t *kd)
{
	register struct swapinfo *sn;

	if (kd->proc != NULL)
		(void) kvm_setproc(kd);	/* rewind process table state */

	if (kd->namefd != -1) {
		(void) close(kd->namefd);
	}
	if (kd->corefd != -1) {
		(void) close(kd->corefd);
	}
	if (kd->virtfd != -1) {
		(void) close(kd->virtfd);
	}
	if (kd->swapfd != -1) {
		(void) close(kd->swapfd);
	}
	if (kd->pbuf != NULL) {
		free((char *)kd->pbuf);
	}
	if (kd->sbuf != NULL) {
		free((char *)kd->sbuf);
	}
	if (kd->kvtopmap != NULL) {
		free((char *)kd->kvtopmap);
	}
	while ((sn = kd->sip) != NULL) {
		if (sn->si_hint != -1)
			(void) close(sn->si_hint);
		kd->sip = sn->si_next;
		free(sn->si_pname);
		free((char *)sn);
	}
	condensed_takedown(kd->corecdp);
	condensed_takedown(kd->swapcdp);
	if (kd->kvm_param != NULL)
		free((char *)kd->kvm_param);
	free((char *)kd);
	return (0);
}

/*
 * Reset the position pointers for kvm_nextproc().
 * This routine is here so that kvmnextproc.o isn't always included.
 */
int
kvm_setproc(kvm_t *kd)
{
	struct proc_t *pract;
	/*
	 * Need to reread practive because processes could
	 * have been added to the table since the kvm_open() call.
	 * This addresses bugid #1117308.
	 */
	kvm_read(kd, kd->practp, (caddr_t)&pract, sizeof (proc_t *));
	kd->proc = (u_long)pract;

	kd->pnext = kd->proc;		/* rewind the position ptr */
	return (0);
}

/*
 * __kvm_openerror - print error messages for kvm_open() if prefix is non-NULL
 */
/*VARARGS*/
void
__kvm_openerror(va_alist)
	va_dcl
{
	va_list args;
	char *prefix, *fmt;

	va_start(args);

	prefix = va_arg(args, char *);
	if (prefix == NULL)
		goto done;

	fprintf(stderr, "%s: ", prefix);
	fmt = va_arg(args, char *);
	vfprintf(stderr, fmt, args);
	putc('\n', stderr);

done:
	va_end(args);
}

/*VARARGS*/
static void
openperror(va_alist)
	va_dcl
{
	va_list args;
	char *prefix, *fmt;

	va_start(args);

	prefix = va_arg(args, char *);
	if (prefix == NULL)
		goto done;

	fprintf(stderr, "%s: ", prefix);
	fmt = va_arg(args, char *);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, ": ");
	perror("");

done:
	va_end(args);
}

/* debugging routines */
#ifdef _KVM_DEBUG

int _kvm_debug = _KVM_DEBUG;

/*VARARGS*/
void
_kvm_error(va_alist)
	va_dcl
{
	va_list args;
	char *fmt;

	if (!_kvm_debug)
		return;

	va_start(args);

	fprintf(stderr, "libkvm: ");
	fmt = va_arg(args, char *);
	vfprintf(stderr, fmt, args);
	putc('\n', stderr);

	va_end(args);
}

/*VARARGS*/
void
_kvm_perror(va_alist)
	va_dcl
{
	va_list args;
	char *fmt;

	if (!_kvm_debug)
		return;

	va_start(args);

	fprintf(stderr, "libkvm: ");
	fmt = va_arg(args, char *);
	vfprintf(stderr, fmt, args);
	perror("");

	va_end(args);
}

#endif _KVM_DEBUG

/*
 * Initialize the platform-dependent
 * piece of libkvm.
 */
static int
init_platdep(kvm_t *kd, char *platform, char *err)
{

	static struct kvm_pdfuncs fval;
	void *handle;
	int (*fptr)();
	int i;
	char so_path[MAXPATHLEN];

	/*
	 * XXX- If directory layout proposal changes, will need to modify.
	 */
	(void) sprintf(so_path, "/usr/platform/%s/lib/lkvm_pd.so%s",
	    platform, VERS10K);

	if ((handle = dlopen(so_path, RTLD_LAZY)) == NULL)
		return (-1);
	/*
	 * Locate and call platform dependent functions.
	 */
	if ((fptr = (int (*)(int))dlsym(handle, pd_open)) == NULL) {
		__kvm_openerror(err, "Failed dlsym of %s", pd_open);
		return (-1);
	}

	if ((*fptr)(kd, err) != 0) {
		__kvm_openerror(err, "Platform dependent init failed on open");
		return (-1);
	}

	if ((fptr = (int (*)(int))dlsym(handle, pd_param)) == NULL) {
		__kvm_openerror(err, "Failed dlsym of %s", pd_param);
		return (-1);
	}

	if ((*fptr)(kd->kvm_param, err) != 0) {
		__kvm_openerror(err,
		    "Platform dependent init failed reading params");
		return (-1);
	}

	if ((fptr = (int (*)(int))dlsym(handle, pd_func)) == NULL) {
		__kvm_openerror(err, "Failed dlsym of %s", pd_func);
		return (-1);
	}

	if ((*fptr)(&fval, err) != 0) {
		__kvm_openerror(err,
		    "Platform dependent init failed reading funcs");
		return (-1);
	}

	return (0);
}
