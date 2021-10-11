/*
 * Copyright (c) 1990-1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_DUMPHDR_H
#define	_SYS_DUMPHDR_H

#pragma ident	"@(#)dumphdr.h	1.8	94/06/21 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The dump header describes the contents of the crash dump.
 * Only "interesting" chunks are written to the dump area; they are
 * written in physical address order and the bitmap indicates which
 * chunks are present.
 * Savecore uses the bitmap to create a sparse file out of the chunks.
 *
 * Layout of crash dump at end of swap partition:
 * +------------------------------------+	end of swap partition
 * |	duplicate header		|
 * |------------------------------------|	page boundary
 * |	"interesting" chunks		|
 * |------------------------------------|	page boundary
 * |	bitmap of interesting chunks	|
 * |------------------------------------|	page boundary
 * |	header information		|
 * |------------------------------------|	page boundary
 * |	unused				|
 * +------------------------------------+	start of swap partition
 *
 * the chunksize is now always 1 page.  dumps can become inordinately huge
 * on large memory systems as the chunksize gets large.
 *
 * * The size of a "chunk" is determined by balancing the following factors:
 * *   a desire to keep the bitmap small
 * *   a desire to keep the dump small
 * * A small chunksize will have a large, sparse bitmap and many small
 * * chunks, but only a fraction of physical memory will be represented
 * * (the limit is chunksize == pagesize).
 * * A large chunksize will have a smaller, denser bitmap and a few large
 * * chunks, but much more of physical memory will be represented (the
 * * limit is chunksize = physical memory size).
 *
 * "Interesting" is defined as follows:
 * Most interesting:
 *	msgbuf
 *	interrupt stack
 *	user struct of current process (including kernel stack)
 *	kernel data (etext to econtig)
 *	Sysbase to Syslimit (kmem_alloc space)
 * Still interesting:
 *	other user structures
 *	segkmap
 * Least interesting (but may be useful):
 *	anonymous pages (user stacks and modified data)
 *	non-anonymous pages (user text and unmodified data)
 * Not interesting:
 *	"gone" pages
 *
 * A "complete" dump includes all the "interesting" items above.
 */

#define	DUMP_MAGIC	0x8FCA0102	/* magic number for savecore(8) */
#define	DUMP_VERSION	2		/* version of this dumphdr */
#define	DUMP_CHUNKSIZE	1		/* chunksize is always 1 page */
struct	dumphdr {
	long	dump_magic;		/* magic number */
	long	dump_version;		/* version number */
	long	dump_flags;		/* flags; see below */
	long	dump_pagesize;		/* size of page in bytes */
	long	dump_chunksize;		/* size of chunk in pages */
	long	dump_bitmapsize;	/* size of bitmap in bytes */
	long	dump_nchunks;		/* number of chunks */
	long	dump_dumpsize;		/* size of dump in bytes */
	struct timeval	dump_crashtime;	/* time of crash */
	long	dump_versionoff;	/* offset to version string */
	long	dump_panicstringoff;	/* offset to panic string */
	long	dump_headersize;	/* size of header, including strings */
	u_longlong_t dump_kvtop_paddr;	/* paddr of kvtopdata */
	/* char	dump_versionstr[]; */	/* copy of version string */
	/* char	dump_panicstring[]; */	/* copy of panicstring string */
};

#define	dump_versionstr(dhp)	((char *)(dhp) + (dhp)->dump_versionoff)
#define	dump_panicstring(dhp)	((char *)(dhp) + (dhp)->dump_panicstringoff)

/* Flags in dump header */
#define	DF_VALID	0x00000001	/* Dump is valid (savecore clears) */
#define	DF_COMPLETE	0x00000002	/* All "interesting" chunks present */

#ifdef _KERNEL

struct vnode;	/* (possible forward reference) */

/* platform-independent */

extern int dump_max;

extern void dumpinit(struct vnode *, char *);
extern void dumphdr_init(void);
extern void dump_addpage(u_int);
extern void dumpsys(void);

/* platform-specific */

extern int dump_checksetbit_machdep(u_longlong_t);
extern int dump_page(struct vnode *, int, int);
extern int dump_kaddr(struct vnode *, caddr_t, int, int);
extern void dump_kvtopdata(void);
extern void dump_allpages_machdep(void);

/*
 * Use the setbit, clrbit, isset, and isclr macros defined in param.h
 * for manipulating the bit map.
 */

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DUMPHDR_H */
