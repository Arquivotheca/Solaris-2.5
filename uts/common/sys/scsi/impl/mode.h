/*
 * Copyright (c) 1990 by Sun Microsystems Inc.
 */

#ifndef	_SYS_SCSI_IMPL_MODE_H
#define	_SYS_SCSI_IMPL_MODE_H

#pragma ident	"@(#)mode.h	1.9	94/02/01 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Defines and Structures for SCSI Mode Sense/Select data
 *
 * Implementation Specific variations
 */

/*
 * Variations to Sequential Access device mode header
 */
struct 	modeheader_seq {
	u_char	datalen;	/* sense data length */
	u_char	mediumtype;	/* medium type */
#if defined(_BIT_FIELDS_LTOH)
	u_char	speed	:4,	/* speed */
		bufm	:3,	/* buffered mode */
		wp	:1;	/* write protected */
#elif defined(_BIT_FIELDS_HTOL)
	u_char	wp	:1,	/* write protected */
		bufm	:3,	/* buffered mode */
		speed	:4;	/* speed */
#else
#error	One of _BIT_FIELDS_LTOH or _BIT_FIELDS_HTOL must be defined
#endif	/* _BIT_FIELDS_LTOH */
	u_char	bd_len;		/* block length in bytes */
	struct  block_descriptor blk_desc;
};

/*
 * Variations to Direct Access device pages
 */

/*
 * Page 1: CCS error recovery page is a little different than SCSI-2
 */

#define	PAGELENGTH_DAD_MODE_ERR_RECOV_CCS	0x06

struct mode_err_recov_ccs {
	struct	mode_page mode_page;	/* common mode page header */
#if defined(_BIT_FIELDS_LTOH)
	u_char 		dcr	: 1,	/* disable correction */
			dte	: 1,	/* disable transfer on error */
			per	: 1,	/* post error */
			eec	: 1,	/* enable early correction */
			rc	: 1,	/* read continuous */
			tb	: 1,	/* transfer block */
			arre	: 1,	/* auto read realloc enabled */
			awre	: 1;	/* auto write realloc enabled */
#elif defined(_BIT_FIELDS_HTOL)
	u_char		awre	: 1,	/* auto write realloc enabled */
			arre	: 1,	/* auto read realloc enabled */
			tb	: 1,	/* transfer block */
			rc	: 1,	/* read continuous */
			eec	: 1,	/* enable early correction */
			per	: 1,	/* post error */
			dte	: 1,	/* disable transfer on error */
			dcr	: 1;	/* disable correction */
#else
#error	One of _BIT_FIELDS_LTOH or _BIT_FIELDS_HTOL must be defined
#endif	/* _BIT_FIELDS_LTOH */
	u_char	retry_count;
	u_char	correction_span;
	u_char	head_offset_count;
	u_char	strobe_offset_count;
	u_char	recovery_time_limit;
};

/*
 * Page 3: CCS Direct Access Device Format Parameters
 *
 * The 0x8 bit in the Drive Type byte is used in CCS
 * as an INHIBIT SAVE bit. This bit is not in SCSI-2.
 */

#define	_reserved_ins	ins

/*
 * Page 0x38 - This is the CCS Cache Page
 */

struct mode_cache_ccs {
	struct	mode_page mode_page;	/* common mode page header */
	u_char	mode;			/* Cache control and size */
	u_char	threshold;		/* Prefetch threshold */
	u_char	max_prefetch;		/* Max. prefetch */
	u_char	max_multiplier;		/* Max. prefetch multiplier */
	u_char	min_prefetch;		/* Min. prefetch */
	u_char	min_multiplier;		/* Min. prefetch multiplier */
	u_char	rsvd2[8];
};




/*
 * Emulex MD21 Unique Mode Select/Sense structure.
 * This is apparently not used, although the MD21
 * documentation refers to it.
 *
 * The medium_type in the mode header must be 0x80
 * to indicate a vendor unique format. There is then
 * a standard block descriptor page, which must be
 * zeros (although the block descriptor length is set
 * appropriately in the mode header).
 *
 * After this stuff, comes the vendor unique ESDI
 * format parameters for the MD21.
 *
 * Notes:
 *
 *	1) The logical number of sectors/track should be the
 *	number of physical sectors/track less the number spare
 *	sectors/track.
 *
 *	2) The logical number of cylinders should be the
 *	number of physical cylinders less three (3) reserved
 *	for use by the drive, and less any alternate cylinders
 *	allocated.
 *
 *	3) head skew- see MD21 manual.
 */

struct emulex_format_params {
	u_char	alt_cyl;	/* number of alternate cylinders */
#if defined(_BIT_FIELDS_LTOH)
	u_char		: 1,
		sst	: 2,	/* spare sectors per track */
		ssz	: 1,	/* sector size. 1 == 256 bps, 0 == 512 bps */
		nheads	: 4;	/* number of heads */
#elif defined(_BIT_FIELDS_HTOL)
	u_char	nheads	: 4,	/* number of heads */
		ssz	: 1,	/* sector size. 1 == 256 bps, 0 == 512 bps */
		sst	: 2,	/* spare sectors per track */
			: 1;
#else
#error	One of _BIT_FIELDS_LTOH or _BIT_FIELDS_HTOL must be defined
#endif	/* _BIT_FIELDS_LTOH */
	u_char	nsect;		/* logical sectors/track */
	u_char	ncyl_hi;	/* logical number of cylinders, msb */
	u_char	ncyl_lo;	/* logical number of cylinders, lsb */
	u_char	head_skew;	/* head skew */
	u_char	reserved[3];
};

/*
 * Page 0x31: CD-ROM speed page
 */

#define	CDROM_MODE_SPEED	0x31

struct mode_speed {
	struct	mode_page mode_page;	/* common mode page header */
	u_char	speed;			/* drive speed */
	u_char	reserved;
};

/*
 * Definitions for drive speed supported are in cdio.h
 */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_IMPL_MODE_H */
