/*
 * Copyright (c) 1990-1993, by Sun Microsystems, Inc.
 */

#ident	"@(#)subr_crash.c	1.34	94/11/14 SMI"

#include <sys/types.h>
#include <sys/stack.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <sys/vnode.h>
#include <sys/errno.h>
#include <sys/dumphdr.h>
#include <sys/bootconf.h>
#include <sys/utsname.h>
#include <sys/cmn_err.h>
#include <sys/archsystm.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>
#include <sys/fs/ufs_fs.h>
#include <sys/kvtopdata.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/seg_kp.h>
#include <vm/seg_kmem.h>
#include <vm/anon.h>

struct dumphdr	*dumphdr;
char	*dump_bitmap;		/* bitmap */
int	dump_max = 0;		/* maximum dumpsize (pages) */
int	dump_size;		/* current dumpsize (pages) */
int	dump_allpages = 0;	/* if 1, dump everything */
int	dump_dochecksetbit = 1;	/* XXX debugging */
int	sync_timeout = 0;	/* timeout counter when syncing fs'es at */
				/* panic time */
int	dump_timeout = 0;	/* timeout counter for dumping a page at */
				/* panic time */

#define	DUMP_TIMEOUT	(20 * (HZ))	/* 20 seconds to dump a page */
#define	MAX_PANIC	100		/* Maximum panic string we'll copy */

static void dump_checksetbit(int);
static void dumpsys_doit(void);

extern	int dump_final(struct vnode *vp);	/* Flush remaining buffers */
int	dump_printp = 512;	/* Print remaining pages after every N */

void
dump_addpage(u_int i)
{
	int	chunksize = dumphdr->dump_chunksize;

	if (isclr(dump_bitmap, (i) / (chunksize))) {
		if (dump_dochecksetbit)
			dump_checksetbit((int)(i));
		dump_size += (chunksize);
		setbit(dump_bitmap, (i) / (chunksize));
	}
}

static void
dump_checksetbit(int i)
{
	static int	dump_checksetbit_error = 0;
	u_longlong_t	addr;

	addr = (u_longlong_t)i << PAGESHIFT;
	if (dump_checksetbit_machdep(addr))
		goto okay;
	dump_checksetbit_error++;
	printf("dump_checksetbit: bad pfn=%d (0x%x); addr=0x%llx\n", i, i,
	    addr);
	if (dump_checksetbit_error == 1)	/* only trace on first error */
		tracedump();
okay:
	/* void */;
}

/*
 * Initialize the dump header; called from main() via init_tbl.
 */
void
dumphdr_init(void)
{
	struct dumphdr	*dp;
	int		size;

	size = sizeof (struct dumphdr) + strlen(utsname.version) + 1 +
		MAX_PANIC + 1;

	dumphdr = dp = (struct dumphdr *)kmem_zalloc((u_int) size,
	    KM_NOSLEEP);
	if (dp == NULL)
		cmn_err(CE_PANIC, "dumphdr_init: no space for dumphdr");
	dp->dump_magic = DUMP_MAGIC;
	dp->dump_version = DUMP_VERSION;
	dp->dump_pagesize = PAGESIZE;
	size = DUMP_CHUNKSIZE;
	dp->dump_chunksize = size;
	dp->dump_bitmapsize = physmax / (size * NBBY) + 1;
	dp->dump_kvtop_paddr =
	    (hat_getkpfnum((caddr_t)&kvtopdata) << MMU_PAGESHIFT) |
	    ((u_int)(&kvtopdata) & MMU_PAGEOFFSET);

	dp->dump_versionoff = sizeof (*dp);

	(void) strcpy(dump_versionstr(dp), utsname.version);
	dp->dump_panicstringoff = dp->dump_versionoff +
	    strlen(utsname.version) + 1;
	dp->dump_headersize = dp->dump_panicstringoff + MAX_PANIC + 1;

	/* Now allocate space for bitmap */
	dump_bitmap = kmem_zalloc((u_int)dp->dump_bitmapsize, KM_NOSLEEP);
	if (dump_bitmap == NULL)
		panic("dumphdr_init: no space for bitmap");

	if (dump_max == 0 || dump_max > btop(dbtob(dumpfile.bo_size)))
		dump_max = btop(dbtob(dumpfile.bo_size));
	dump_max = (dump_max / dp->dump_chunksize) * dp->dump_chunksize;
	dump_max -= 2 + howmany(dp->dump_bitmapsize, dp->dump_pagesize);
	if (dump_max < 0) {
		printf("Warning: dump_max value inhibits crash dumps.\n");
		dump_max = 0;
	}
	dump_size = 0;
}

/*
 * Dump the system.
 * Only dump "interesting" chunks of memory, preceeded by a header and a
 * bitmap.
 * The header describes the dump, and the bitmap inidicates which chunks
 * have been dumped.
 */
void
dumpsys(void)
{
	register int	 i, n;
	struct dumphdr	*dp = dumphdr;
	int	chunksize;
	int	size;
	struct as	*as;
	struct seg	*seg;
	struct memseg	*msp;
	struct page	*pp;
	u_int	pfn;
	int	error = 0;
	label_t ljb;

	/*
	 * dumphdr not initialized yet...
	 */
	if (!dp)
		return;

	dump_timeout = DUMP_TIMEOUT;	/* set dump_timeout counter */

	/*
	 * While we still have room (dump_max is positive), mark pages
	 * that belong to the following:
	 * 1. Kernel (vnode == kvp and free == 0)
	 * 2. Grovel through segkmap looking for pages
	 * 3. Grovel through proc array, getting users' stack
	 *    segments.
	 * We don't want to panic again if data structures are invalid,
	 * so we use on_fault().
	 */
	dp->dump_flags = DF_VALID;
	chunksize = dp->dump_chunksize;

	size = dump_size;

	/* Dump kernel segkmem pages */
	if (!on_fault(&ljb)) {
		extern struct seg_ops	segkmem_ops;
		extern void dump_kvtopdata(void);

		/*
		 * The msgbuf contains the physical address of
		 * the kvtopdata which is the table we need for
		 * translating virtual to physical addresses.
		 * Make sure this gets into the dump too!
		 */
		dump_kvtopdata();

		for (seg = AS_SEGP(&kas, kas.a_segs); seg != NULL;
		    seg = AS_SEGP(&kas, seg->s_next)) {
			if (seg->s_ops != &segkmem_ops)
				continue;
			SEGOP_DUMP(seg);
		}

		printf("%5d static and sysmap kernel pages\n",
			dump_size - size);
		size = dump_size;
	} else {
		printf("failure dumping kernel static/sysmap pages: seg=0x%x\n",
		    (int)seg);
		error = 1;
	}

	/*
	 * Find and mark all pages in use by kernel.
	 */
	if (!on_fault(&ljb)) {
		for (msp = memsegs; msp; msp = msp->next) {
			for (pp = msp->pages; pp < msp->epages; pp++) {
				if (pp->p_vnode != &kvp || pp->p_free)
					continue;
				pfn = page_pptonum(pp);
				dump_addpage(pfn);
			}
		}

		printf("%5d dynamic kernel data pages\n", dump_size - size);
		size = dump_size;
	} else {
		printf(
		    "failure dumping kernel heap: "
		    "memseg=0x%x pp=0x%x pfn=0x%x\n", (int)msp, (int)pp, pfn);
		error = 1;
	}

	/* Dump all the thread stacks and lwp structures */
	if (!on_fault(&ljb)) {
		SEGOP_DUMP(segkp);
		printf("%5d kernel-pageable pages\n", dump_size - size);
		size = dump_size;
		if (error == 0 && dump_size < dump_max)
			dp->dump_flags |= DF_COMPLETE;
	} else {
		printf("failure dumping segkp\n");
	}

	/* Dump pages in use by segkmap */
	if (!on_fault(&ljb)) {
		SEGOP_DUMP(segkmap);
		printf("%5d segkmap kernel pages\n", dump_size - size);
		size = dump_size;
	} else {
		printf("failure dumping segkmap\n");
	}

	/* Dump kernel segvn pages */
	if (!on_fault(&ljb)) {
		for (seg = AS_SEGP(&kas, kas.a_segs); seg != NULL;
		    seg = AS_SEGP(&kas, seg->s_next)) {
			if (seg->s_ops != &segvn_ops)
				continue;
			SEGOP_DUMP(seg);
		}

		printf("%5d segvn kernel pages\n", dump_size - size);
		size = dump_size;
	} else {
		printf("failure dumping kernel segvn 0x%x\n", (int)seg);
	}

	/* Dump current user process */
	if (!on_fault(&ljb)) {
		as = curproc->p_as;
		if (as != &kas)
			for (seg = AS_SEGP(as, as->a_segs); seg != NULL;
			    seg = AS_SEGP(as, seg->s_next)) {
				if (seg->s_ops != &segvn_ops)
					continue;
				SEGOP_DUMP(seg);
			}

		printf("%5d current user process pages\n", dump_size - size);
		size = dump_size;
	} else {
		printf(
		    "failure dumping current user process: as=0x%x seg=0x%x\n",
		    (int)as, (int)seg);
	}

#if 0
	/* Dump other user stacks */
	if (!on_fault(&ljb)) {
		for (p = practive; p; p = p->p_next)
			if ((as = p->p_as) != &kas &&
			    ((seg = as->a_tail) != NULL) &&
			    ((seg->s_base + seg->s_size) == (addr_t)USRSTACK) &&
			    (seg->s_ops == &segvn_ops))
				SEGOP_DUMP(seg);
		printf("%5d user stack pages\n", dump_size - size);
		size = dump_size;
	} else {
		printf(
		    "failure dumping user stacks: proc=0x%x as=0x%x seg=0x%x\n",
		    p, as, seg);
	}
#endif
	/* If requested, dump everything */
	if (!on_fault(&ljb)) {
		if (dump_allpages) {
			dump_allpages_machdep();
			printf("%5d \"everything else\" pages\n",
				dump_size - size);
			size = dump_size;
		}
	} else {
		printf("failure dumping \"everything else\" pages\n");
	}
	no_fault();
	printf("%5d total pages", dump_size);

	/* Now count the bits */
	n = 0;
	for (i = 0; i < dp->dump_bitmapsize * NBBY; i++)
		if (isset(dump_bitmap, i))
			n++;
	dp->dump_nchunks = n;
	printf(" (%d chunks)\n", n);

	if (n * chunksize != dump_size)
		printf(
	"WARNING: nchunks (%d) * pages/chunk (%d) != total pages (%d) !!!\n",
		n, chunksize, dump_size);

	dp->dump_dumpsize = (1 /* for the first header */ +
			howmany(dp->dump_bitmapsize, PAGESIZE) +
			n * chunksize +
			1 /* for the last header */) *
			PAGESIZE;

	dp->dump_crashtime.tv_sec = hrestime.tv_sec;

	if (panicstr)
		vsprintf_len(MAX_PANIC, dump_panicstring(dp),
			panicstr, panicargs);
	else
		*dump_panicstring(dp) = '\0';

	/* Now dump everything */
	dumpsys_doit();
	dump_timeout = 0;	/* clear dump_timeout counter */
}

/*
 * Dump the system:
 * Dump the header, the bitmap, the chunks, and the header again.
 */
static void
dumpsys_doit(void)
{
	register int bn;
	register int err = 0;
	struct dumphdr	*dp = dumphdr;
	register int i;
	int	totpages, npages = 0;
	register char	*bitmap = dump_bitmap;
	extern void mmu_setctx();
	extern int vac;

	if (dumpvp == NULL) {
		printf("\ndump device not initialized, no dump taken\n");
		return;
	}

	bn = dumpfile.bo_size - ctod(btoc(dp->dump_dumpsize));
	if (bn < 0) {
		printf("\nnot enough space on dump device. no dump taken.\n"
		    "need %d blocks, only %d blocks available.\n\n",
		    ctod(btoc(dp->dump_dumpsize)), dumpfile.bo_size);
		return;
	}

	printf("\ndumping to vp %x, offset %d\n", (int)dumpvp, bn);
#ifdef sparc
	/* Must get all the kernel's windows to memory for adb */
	flush_windows();

	mmu_setctx(kctx);
	if (vac)
		vac_flushall();
#endif

	/*
	 * Write the header, then the bitmap, then the chunks, then the
	 * header again.
	 * XXX Should we write the last header first, so that if
	 * XXX anything gets dumped we can use it?
	 */
	/* 1st copy of header */
	if (err = dump_kaddr(dumpvp, (caddr_t)dp, bn, ctod(1)))
		goto done;
	bn += ctod(1);

	/* bitmap */
	if (err = dump_kaddr(dumpvp, bitmap, bn,
	    (int)ctod(btoc(dp->dump_bitmapsize))))
		goto done;
	bn += ctod(btoc(dp->dump_bitmapsize));

	totpages = dp->dump_nchunks * dp->dump_chunksize;
	for (i = 0; i < dp->dump_bitmapsize * NBBY; i++) {
		if (isset(bitmap, i)) {
			register int n, pg;

			for (pg = i * dp->dump_chunksize,
			    n = dp->dump_chunksize;
			    n > 0 && !err; n--, pg++) {
				err = dump_page(dumpvp, pg, bn);
				/*
				 * While we are making progress,
				 * bump up dump_timeout so that we
				 * don't get an erroneous timeout in clock().
				 */
				if (err == 0)
					dump_timeout = DUMP_TIMEOUT;

				bn += ctod(1);
				npages++;
				/*
				 * Update the display every 2Mb dump.
				 */
				if ((npages % dump_printp) == 0) {
					register int t;
					extern int msgbufinit;

					t = msgbufinit;
					msgbufinit = 0;
					printf("\r%d pages left      ",
						totpages - npages);
					msgbufinit = t;
				}
			}
			if (err)
				goto done;
		}
	}

	/*
	 * Dump the final pages cached away by the dump_page
	 * routines.
	 */
	err = dump_final(dumpvp);
	if (err)
		goto done;

	/* Now dump the second copy of the header */
	if (err = dump_kaddr(dumpvp, (caddr_t)dp, bn, ctod(1)))
		goto done;
	bn += ctod(1);

done:
	printf("\r%d total pages, dump ", npages);

	switch (err) {

	case ENXIO:
		printf("device bad\n");
		break;

	case EFAULT:
		printf("device not ready\n");
		break;

	case EINVAL:
		printf("area improper\n");
		break;

	case EIO:
		printf("i/o error\n");
		break;

	case 0:
		printf("succeeded\n");
		break;

	default:
		printf("failed: error %d\n", err);
		break;
	}
}
