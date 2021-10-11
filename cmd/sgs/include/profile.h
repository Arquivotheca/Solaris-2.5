/*
 *	Copyright (c) 1991 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)profile.h	1.9	94/07/28 SMI"

#ifndef	PROFILE_DOT_H
#define	PROFILE_DOT_H

#define	PROF

#include	"_profile.h"

#ifndef	_ASM

#include	<sys/types.h>
#include	<synch.h>

/*
 * The profile buffer created by ld.so.1 consists of 3 sections; the header,
 * the profil(2) buffer, and an array of call graph arc structures.
 */
typedef struct l_hdr {			/* Linker profile buffer header */
	unsigned long	hd_magic;	/* identifier for file */
	unsigned long	hd_version;	/* version for rtld prof file */
	mutex_t		hd_mutex;	/* Provides for process locking */
	caddr_t		hd_hpc;		/* Relative high pc address */
	unsigned long	hd_psize;	/* Size of profil(2) buffer */
	unsigned long	hd_fsize;	/* Size of file */
	unsigned long	hd_ncndx;	/* Next (and last) index into */
	unsigned long	hd_lcndx;	/*	call graph arc structure */
} L_hdr;

typedef struct l_cgarc {		/* Linker call graph arc entry */
	caddr_t		cg_from;	/* Source of call */
	caddr_t		cg_to;		/* Destination of call */
	unsigned long	cg_count;	/* Instance count */
	unsigned long	cg_next;	/* Link index for multiple sources */
} L_cgarc;


/*
 * Snapshots of this profile buffer are taken by `ldmon' and packaged into
 * a gmon.out file appropriate for analysis by gprof(1).  This gmon file
 * consists of three sections (taken from gmon.h); a header, a profil(2)
 * buffer, and an array of call graph arc structures.
 */
typedef struct m_hdr {			/* Monitor profile buffer header */
	char *		hd_lpc;		/* Low pc value */
	char *		hd_hpc;		/* High pc value */
	int		hd_off;		/* Offset into call graph array */
} M_hdr;

typedef struct m_cgarc {		/* Monitor call graph arc entry */
	unsigned long	cg_from;	/* Source of call */
	unsigned long	cg_to;		/* Destination of call */
	long		cg_count;	/* Instance count */
} M_cgarc;

typedef struct m_cnt {			/* Prof(1) function count structure */
	char *		fc_fnpc;	/* Called functions address */
	long		fc_mcnt;	/* Instance count */
} M_cnt;


/*
 * Generic defines for creating profiled output buffer.
 */
#define	PRF_BARSIZE	2		/* No. of program bytes that */
					/* correspond to each histogram */
					/* bar in the profil(2) buffer */
#define	PRF_SCALE	0x8000		/* Scale to provide above */
					/* histogram correspondence */
#define	PRF_CGNUMB	256		/* Size of call graph extension */
#define	PRF_CGINIT	2		/* Initial symbol blocks to allocate */
					/*	for the call graph structure */
#define	PRF_OUTADDR	(caddr_t)-1	/* Function addresses outside of */
					/*	the range being monitored */
#define	PRF_UNKNOWN	(caddr_t)-2	/* Unknown function address */

#define	PRF_DEFDIR	"/var/tmp"
#define	PRF_FILEFORM	"%s/%s.profile"
#define	PRF_FILESIZE	9

#define	PRF_ROUNDUP(x, a)	(((unsigned int)(x) + ((int)(a) - 1)) & \
					~((int)(a) - 1))
#define	PRF_ROUNDWN(x, a)	((unsigned int)(x) & ~((int)(a) - 1))
#define	PRF_MAGIC	0xffffffff	/* unique number to differentiate */
					/* profiled file from gmon.out for */
					/* gprof */
#define	PRF_VERSION	0x1		/* current PROF file version */


/*
 * Related data and function definitions.
 */
extern	char *		profile_name;	/* file to be profiled */
extern	int		profile_rtld;	/* Rtld is being profiled */
extern	char *		profile_dir;	/* profile buffer directory */

extern	int		profile(const char *, void *);
extern	void		profile_close(void *);
extern	caddr_t		plt_cg_interp(int, caddr_t, caddr_t);

#endif
#endif
