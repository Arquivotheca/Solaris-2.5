/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_DKLABEL_H
#define	_SYS_DKLABEL_H

#pragma ident	"@(#)dklabel.h	1.4	94/08/08 SMI"

#include <sys/isa_defs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Miscellaneous defines
 */
#define	DKL_MAGIC	0xDABE		/* magic number */
#define	FKL_MAGIC	0xff		/* magic number for DOS floppies */

#if defined(_SUNOS_VTOC_16)
#define	NDKMAP		16		/* # of logical partitions */
#elif defined(_SUNOS_VTOC_8)
#define	NDKMAP		8		/* # of logical partitions */
#else
#error No Platform defined.
#endif

#define	LEN_DKL_ASCII	128		/* length of dkl_asciilabel */
#define	LEN_DKL_VVOL	8		/* length of v_volume */
#define	DK_LABEL_SIZE	512		/* size of disk label */

#define	DK_LABEL_LOC	1		/* location of disk label */

/*
 * Format of a Sun disk label.
 * Resides in cylinder 0, head 0, sector 0.
 *
 * sizeof (struct dk_label) should be 512 (the current sector size),
 * but should the sector size increase, this structure should remain
 * at the beginning of the sector.
 */

/* partition headers:  section 1 */

struct dk_map {
	daddr_t	dkl_cylno;		/*	starting cylinder */
	daddr_t	dkl_nblk;		/*	number of blocks;  if == 0, */
					/*	partition is undefined */
};

/*
 * partition headers:  section 2,
 * brought over from AT&T SVr4 vtoc structure.
 */

struct dk_map2 {
	unsigned short	p_tag;		/*	ID tag of partition */
	unsigned short	p_flag;		/*	permission flag */
};

struct dkl_partition    {
	ushort	p_tag;			/* ID tag of partition */
	ushort	p_flag;			/* permision flags */
	daddr_t	p_start;		/* start sector no of partition */
	long	p_size;			/* # of blocks in partition */
};


/* vtoc inclusions from AT&T SVr4 */

struct dk_vtoc {
#if defined(_SUNOS_VTOC_16)
	unsigned long v_bootinfo[3];	/* info needed by mboot (unsupported) */
	unsigned long v_sanity;		/* to verify vtoc sanity */
	unsigned long v_version;	/* layout version */
	char    v_volume[LEN_DKL_VVOL];	/* volume name */
	ushort  v_sectorsz;		/* sector size in bytes */
	ushort  v_nparts;		/* number of partitions */
	unsigned long v_reserved[10];	/* free space */
	struct dkl_partition v_part[NDKMAP];	/* partition headers */
	time_t  timestamp[NDKMAP];	/* partition timestamp (unsupported) */
	char    v_asciilabel[LEN_DKL_ASCII];	/* for compatibility    */
#elif defined(_SUNOS_VTOC_8)
	unsigned long	v_version;		/*	layout version */
	char		v_volume[LEN_DKL_VVOL];	/*	volume name */
	unsigned short	v_nparts;		/*	number of partitions  */
	struct dk_map2	v_part[NDKMAP];		/*	partition hdrs, sec 2 */
	u_long		v_bootinfo[3];		/*	info needed by mboot */
	u_long		v_sanity;		/*	to verify vtoc sanity */
	u_long		v_reserved[10];		/*	free space */
	time_t		v_timestamp[NDKMAP];	/*	partition timestamp */
#else
#error No Platform defined.
#endif
};

/*
 * define the amount of disk label padding needed to make
 * the entire structure occupy 512 bytes.
 */
#if defined(_SUNOS_VTOC_16)
#define	LEN_DKL_PAD	(DK_LABEL_SIZE - \
			    ((sizeof (struct dk_vtoc) + \
			    (4 * sizeof (unsigned long)) + \
			    (12 * sizeof (unsigned short)) + \
			    (2 * (sizeof (unsigned short))))))
#elif defined(_SUNOS_VTOC_8)
#define	LEN_DKL_PAD	(DK_LABEL_SIZE \
			    - ((LEN_DKL_ASCII) + \
			    (sizeof (struct dk_vtoc)) + \
			    (sizeof (struct dk_map)  * NDKMAP) + \
			    (14 * (sizeof (unsigned short))) + \
			    (2 * (sizeof (unsigned short)))))
#else
#error No Platform defined.
#endif


struct dk_label {
#if defined(_SUNOS_VTOC_16)
	struct  dk_vtoc dkl_vtoc;	/* vtoc inclusions from AT&T SVr4 */
	unsigned long   dkl_pcyl;	/* # of physical cylinders */
	unsigned long   dkl_ncyl;	/* # of data cylinders */
	unsigned short  dkl_acyl;	/* # of alternate cylinders */
	unsigned short  dkl_bcyl;	/* cyl offset (for fixed head area) */
	unsigned long   dkl_nhead;	/* # of heads */
	unsigned long   dkl_nsect;	/* # of data sectors per track */
	unsigned short  dkl_intrlv;	/* interleave factor */
	unsigned short  dkl_skew;	/* skew factor */
	unsigned short  dkl_apc;	/* alternates per cyl (SCSI only)   */
	unsigned short  dkl_rpm;	/* revolutions per minute */
	unsigned short  dkl_write_reinstruct;	/* # sectors to skip, writes */
	unsigned short  dkl_read_reinstruct;	/* # sectors to skip, reads  */
	unsigned short  dkl_extra[4];	/* for compatible expansion */
	char		dkl_pad[LEN_DKL_PAD];	/* unused part of 512 bytes */
#elif defined(_SUNOS_VTOC_8)
	char		dkl_asciilabel[LEN_DKL_ASCII]; /* for compatibility */
	struct dk_vtoc	dkl_vtoc;	/* vtoc inclusions from AT&T SVr4 */
	unsigned short	dkl_write_reinstruct;	/* # sectors to skip, writes */
	unsigned short	dkl_read_reinstruct;	/* # sectors to skip, reads */
	char		dkl_pad[LEN_DKL_PAD]; /* unused part of 512 bytes */
	unsigned short	dkl_rpm;	/* rotations per minute */
	unsigned short	dkl_pcyl;	/* # physical cylinders */
	unsigned short	dkl_apc;	/* alternates per cylinder */
	unsigned short	dkl_obs1;	/* obsolete */
	unsigned short	dkl_obs2;	/* obsolete */
	unsigned short	dkl_intrlv;	/* interleave factor */
	unsigned short	dkl_ncyl;	/* # of data cylinders */
	unsigned short	dkl_acyl;	/* # of alternate cylinders */
	unsigned short	dkl_nhead;	/* # of heads in this partition */
	unsigned short	dkl_nsect;	/* # of 512 byte sectors per track */
	unsigned short	dkl_obs3;	/* obsolete */
	unsigned short	dkl_obs4;	/* obsolete */
	/* */
	struct dk_map 	dkl_map[NDKMAP]; /* logical partition headers */
#else
#error No Platform defined.
#endif
	unsigned short	dkl_magic;	/* identifies this label format */
	unsigned short	dkl_cksum;	/* xor checksum of sector */
};

#if defined(_SUNOS_VTOC_16)
#define	dkl_asciilabel	dkl_vtoc.v_asciilabel
#define	v_timestamp	timestamp
#endif

#if defined(_SUNOS_VTOC_8)
/*
 * These defines are for historic compatibility with old drivers.
 */
#define	dkl_gap1	dkl_obs1	/* used to be gap1 */
#define	dkl_gap2	dkl_obs2	/* used to be gap2 */
#define	dkl_bhead	dkl_obs3	/* used to be label head offset */
#define	dkl_ppart	dkl_obs4	/* used to by physical partition */
#endif

struct fk_label {			/* DOS floppy label */
	u_char  fkl_type;
	u_char  fkl_magich;
	u_char  fkl_magicl;
	u_char  filler;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DKLABEL_H */
