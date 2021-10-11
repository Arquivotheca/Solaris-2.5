/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)libld.h	1.42	95/06/20 SMI"

/*
 * Global include file for ld library.
 */
#ifndef	LIBLD_DOT_H
#define	LIBLD_DOT_H

#include	<stdlib.h>
#include	<libelf.h>
#include	"sgs.h"
#include	"machdep.h"


/*
 * Symbol defines.  The maximum no. of hash buckets that can be created in a
 * .hash section, and a hash value that can never be returned from elf_hash().
 */
#define	SYM_MAXNBKTS	10007
#define	SYM_NOHASH	(~(unsigned long)0)

/*
 * Define symbol reference types for use in symbol resolution.
 */
typedef enum {
	REF_DYN_SEEN,			/* a .so symbol has been seen */
	REF_DYN_NEED,			/* a .so symbol satisfies a .o symbol */
	REF_REL_NEED,			/* a .o symbol */
	REF_NUM				/* the number of symbol references */
} Symref;


/*
 * Output file processing structure
 */
struct ofl_desc {
	const char *	ofl_name;	/* full file name */
	Elf *		ofl_elf;	/* elf structure describing this file */
	Ehdr *		ofl_ehdr;	/* elf header describing this file */
	Phdr *		ofl_phdr;	/* program header descriptor */
	int		ofl_fd;		/* file descriptor */
	char *		ofl_image;	/* output files image in memory and */
	size_t		ofl_size;	/* 	its associated size. */
	List		ofl_maps;	/* list of input mapfiles */
	List		ofl_segs;	/* list of segments */
	List		ofl_ents;	/* list of entrance descriptors */
	List		ofl_objs;	/* relocatable object file list */
	List		ofl_ars;	/* archive library list */
	List		ofl_sos;	/* shared object list */
	List		ofl_soneed;	/* list of implicitly required .so's */
	List		ofl_socntl;	/* list of .so control definitions */
	List		ofl_outrels;	/* list of output relocations */
	List		ofl_actrels;	/* list of relocations to perform */
	Sym_names *	ofl_symdict;	/* global symbol dictionary (names) */
	Sym_cache **	ofl_symbkt;	/* global symbol hash buckets */
	Word		ofl_symbktcnt;	/* no. of symbol hash buckets */
	Elf32_Half	ofl_e_machine;	/* elf header e_machine field setting */
	Elf32_Word	ofl_e_flags;	/* elf header e_flags field setting */
	Word		ofl_libver;	/* max version that libelf can handle */
	Word		ofl_flags;	/* various state bits, args etc. */
	Word		ofl_segalign;	/* segment alignment */
	Word		ofl_segorigin;	/* segment origin (start) */
	void *		ofl_entry;	/* entry point (-e and Sym_desc *) */
	const char *	ofl_filter;	/* shared object we are a filter for */
	const char *	ofl_soname;	/* (-h option) output file name for */
					/*	dynamic structure */
	const char *	ofl_interp;	/* interpreter name used by exec() */
	const char *	ofl_rpath;	/* run path to store in .dynamic */
	List		ofl_ulibdirs;	/* user supplied library search list */
	List		ofl_dlibdirs;	/* default library search list */
	Word		ofl_vercnt;	/* number of versions to generate */
	List		ofl_verdesc;	/* list of version descriptors */
	Word		ofl_verdefsz;	/* size of version definition section */
	Word		ofl_verneedsz;	/* size of version needed section */
	Word		ofl_globcnt;	/* no. of global symbols to output */
	Word		ofl_globstrsz;	/*	and associated string size. */
	Word		ofl_scopecnt;	/* no. of scoped symbols to output */
	Word		ofl_locscnt;	/* no. of local symbols to output */
	Word		ofl_locsstrsz;	/*	and associated string size. */
	Word		ofl_dynstrsz;	/* strings applicable only to the */
					/*	.dynamic section. */
	Word		ofl_shdrcnt;	/* no. of output sections */
	Word		ofl_shdrstrsz;	/*	and associated string size. */
	Word		ofl_relocsz;	/* size of output relocations */
	Word		ofl_relocgotsz;	/* size of .got relocations */
	Word		ofl_relocpltsz;	/* size of .plt relocations */
	Word		ofl_relocbsssz;	/* size of .bss (copy) relocations */
	Word		ofl_reloccnt;	/* tot number of output relocations */
	Word		ofl_gotcnt;	/* no. of .got entries */
	Word		ofl_pltcnt;	/* no. of .plt entries */
	Word		ofl_hashbkts;	/* no. of hash buckets required */
	Is_desc	*	ofl_isbss;	/* .bss input section (globals) */
	Os_desc	*	ofl_osdynamic;	/* .dynamic output section */
	Os_desc	*	ofl_osdynsym;	/* .dynsym output section */
	Os_desc	*	ofl_osdynstr;	/* .dynstr output section */
	Os_desc	*	ofl_osgot;	/* .got output section */
	Os_desc	*	ofl_oshash;	/* .hash output section */
	Os_desc	*	ofl_osinterp;	/* .interp output section */
	Os_desc	*	ofl_osplt;	/* .plt output section */
	Os_desc	*	ofl_osreloc;	/* .rel[a] output section (first) */
	Os_desc	*	ofl_osshstrtab;	/* .shstrtab output section */
	Os_desc	*	ofl_osstrtab;	/* .strtab output section */
	Os_desc	*	ofl_ossymtab;	/* .symtab output section */
	Os_desc	*	ofl_osverdef;	/* .version definition output section */
	Os_desc	*	ofl_osverneed;	/* .version needed output section */
	Os_desc	*	ofl_osversym;	/* .version symbol ndx output section */
};

#define	FLG_OF_DYNAMIC	0x00000001	/* generate dynamic output module */
#define	FLG_OF_STATIC	0x00000002	/* generate static output module */
#define	FLG_OF_EXEC	0x00000004	/* generate an executable */
#define	FLG_OF_RELOBJ	0x00000008	/* generate a relocatable object */
#define	FLG_OF_SHAROBJ	0x00000010	/* generate a shared object */
#define	FLG_OF_BFLAG	0x00000020	/* do no special plt building: -b */
#define	FLG_OF_IGNENV	0x00000040	/* ignore LD_LIBRARY_PATH: -i */
#define	FLG_OF_STRIP	0x00000080	/* strip output: -s */
#define	FLG_OF_NOWARN	0x00000100	/* disable symbol warnings: -t */
#define	FLG_OF_NOUNDEF	0x00000200	/* allow no undefined symbols: -zdefs */
#define	FLG_OF_PURETXT	0x00000400	/* allow no text relocations: -ztext  */
#define	FLG_OF_GENMAP	0x00000800	/* generate a memory map: -m */
#define	FLG_OF_DYNLIBS	0x00001000	/* dynamic input allowed: -Bdynamic */
#define	FLG_OF_SYMBOLIC	0x00002000	/* bind global symbols: -Bsymbolic */
#define	FLG_OF_ADDVERS	0x00004000	/* add version stamp: -Qy */
#define	FLG_OF_MEMORY	0x00008000	/* produce a memory model */
#define	FLG_OF_SEGORDER	0x00010000	/* segment ordering is required */
#define	FLG_OF_SEGSORT	0x00020000	/* segment sorting is required */
#define	FLG_OF_TEXTREL	0x00040000	/* text relocations have been found */
#define	FLG_OF_MULDEFS	0x00080000	/* multiple symbols are allowed */
#define	FLG_OF_OUTMMAP	0x00100000	/* output image is mmaped to file */
#define	FLG_OF_BLDGOT	0x00200000	/* build GOT table */
#define	FLG_OF_VERDEF	0x00400000	/* record version definitions */
#define	FLG_OF_VERNEED	0x00800000	/* record version dependencies */
#define	FLG_OF_NOVERSEC 0x01000000	/* don't record version sections */
#define	FLG_OF_AUTOLCL	0x02000000	/* automatically reduce unspecified */
					/*	global symbols to locals */
#define	FLG_OF_PROCRED	0x04000000	/* process any symbol reductions by */
					/*	effecting the symbol table */
					/*	output and relocations */
#define	FLG_OF_BINDSYMB	0x08000000	/* individual symbols are being bound */
					/*	symbolically (mapfile entry) */
#define	FLG_OF_AUX	0x10000000	/* ofl_filter is an auxiliary filter */
#define	FLG_OF_FATAL	0x20000000	/* fatal or warning error during */
#define	FLG_OF_WARN	0x40000000	/* 	input file processing. */

/*
 * Relocation (active & output) processing structure - transparent to common
 * code.
 */
struct rel_desc {
	Os_desc *	rel_osdesc;	/* output section reloc is against */
	Is_desc *	rel_isdesc;	/* input section reloc is against */
	const char *	rel_fname;	/* filename from which relocation */
					/*	was generated. */
	const char *	rel_sname;	/* symbol name (may be "unknown") */
	Sym_desc *	rel_sym;	/* sym relocation is against */
	Sym_desc *	rel_usym;	/* strong sym if this is a weak pair */
	Word		rel_rtype;	/* relocation type */
	Word		rel_roffset;	/* relocation offset */
	Sword		rel_raddend;	/* addend from input relocation */
	Half		rel_flags;	/* misc. flags for relocations */
};

/*
 * common flags used on the Rel_desc structure (defined in machrel.h).
 */
#define	FLG_REL_GOT	0x001		/* relocation against GOT */
#define	FLG_REL_PLT	0x002		/* relocation against PLT */
#define	FLG_REL_BSS	0x004		/* relocation against BSS */
#define	FLG_REL_LOAD	0x008		/* section loadable */
#define	FLG_REL_SCNNDX	0x010		/* use section index for symbol ndx */
#define	FLG_REL_CLVAL	0x020		/* clear VALUE for active relocation */
#define	FLG_REL_ADVAL	0x040		/* add VALUE for output relocation, */
					/*	only relevent to SPARC and */
					/*	R_SPARC_RELATIVE */
#define	FLG_REL_GOTCL	0x080		/* clear the GOT entry.  This is */
					/* relevant to RELA relocations, */
					/* not REL (i386) relocations */

/*
 * Structure to hold a cache of Relocations.
 */
struct rel_cache {
	Rel_desc *	rc_end;
	Rel_desc *	rc_free;
};

/*
 * Input file processing structures.
 */
struct ifl_desc {			/* input file descriptor */
	const char *	ifl_name;	/* full file name */
	const char *	ifl_soname;	/* shared object name */
	dev_t		ifl_stdev;	/* device id and inode number for .so */
	ino_t		ifl_stino;	/*	multiple inclusion checks */
	Ehdr *		ifl_ehdr;	/* elf header describing this file */
	Half		ifl_flags;	/* Explicit/implicit reference */
	Sym_desc **	ifl_oldndx;	/* original symbol table indices */
	Sym_desc *	ifl_locs;	/* symbol desc version of locals */
	Word		ifl_locscnt;	/* no. of local symbols to process */
	Word		ifl_symscnt;	/* total no. of symbols to process */
	Is_desc **	ifl_isdesc;	/* isdesc[scn ndx] = Is_desc ptr */
	Sdf_desc *	ifl_sdfdesc;	/* control definition */
	Word		ifl_vercnt;	/* number of versions in file */
	Versym *	ifl_versym;	/* version symbol table array */
	Ver_index *	ifl_verndx;	/* verndx[ver ndx] = Ver_index */
	List		ifl_verdesc;	/* version descriptor list */
};

#define	FLG_IF_CMDLINE	0x001		/* full filename specified from the */
					/*	command line (no -l) */
#define	FLG_IF_USERDEF	0x002		/* pseudo file created to maintain */
					/*	symbols from the command line */
					/*	or a mapfile */
#define	FLG_IF_NEEDED	0x004		/* shared object should be recorded */
#define	FLG_IF_USED	0x008		/* record shared object as DT_USED */
#define	FLG_IF_EXTRACT	0x010		/* file extracted from an archive */
#define	FLG_IF_VERNEED	0x020		/* version dependency information is */
					/*	required */
#define	FLG_IF_DEPREQD	0x040		/* dependency is required to satisfy */
					/*	symbol references */

struct is_desc {			/* input section descriptor */
	const char *	is_name;	/* the section name */
	Shdr *		is_shdr;	/* The elf section header */
	Ifl_desc *	is_file;	/* infile desc for this section */
	Os_desc *	is_osdesc;	/* new output section for this */
					/*	input section */
	Elf_Data *	is_indata;	/* input sections raw data */
	Word		is_txtndx;	/* Index for section.  Used to decide */
					/*	where to insert section when */
					/* 	reordering sections */
};

/*
 * Map file and output file processing structures
 */
struct os_desc {			/* Output section descriptor */
	const char *	os_name;	/* the section name */
	Elf_Scn *	os_scn;		/* the elf section descriptor */
	Shdr *		os_shdr;	/* the elf section header */
	Os_desc *	os_relosdesc;	/* the output relocation section */
	List		os_relisdescs;	/* reloc input section descriptors */
					/*	for this output section */
	Word		os_szoutrels;	/* size of output relocation section */
	Word		os_szcopyrels;	/* size of output copy relocations */
	Word		os_scnsymndx;	/* index in output symtab of section */
					/*	symbol for this section */
	List		os_isdescs;	/* list of input sections in output */
	Sg_desc *	os_sgdesc;	/* segment os_desc is placed on */
	Elf_Data *	os_outdata;	/* output sections raw data */
	Word		os_txtndx;	/* Index for section.  Used to decide */
					/*	where to insert section when */
					/* 	reordering sections */
};

struct sg_desc {			/* output segment descriptor */
	Phdr		sg_phdr;	/* segment header for output file */
	const char *	sg_name;	/* segment name */
	Word		sg_round;	/* data rounding required (mapfile) */
	Word		sg_length;	/* maximum segment length; if 0 */
					/*	segment is not specified */
	List		sg_osdescs;	/* list of output section descriptors */
	List		sg_secorder;	/* List of section ordering */
					/*	which specify section */
					/*	ordering for the segment */
	Half		sg_flags;
	Sym_desc *	sg_sizesym;	/* size symbol for this segment */
};


#define	FLG_SG_VADDR	0x0001		/* vaddr segment attribute set */
#define	FLG_SG_PADDR	0x0002		/* paddr segment attribute set */
#define	FLG_SG_LENGTH	0x0004		/* length segment attribute set */
#define	FLG_SG_ALIGN	0x0008		/* align segment attribute set */
#define	FLG_SG_ROUND	0x0010		/* round segment attribute set */
#define	FLG_SG_FLAGS	0x0020		/* flags segment attribute set */
#define	FLG_SG_TYPE	0x0040		/* type segment attribute set */
#define	FLG_SG_ORDER	0x0080		/* has ordering been turned on for */
					/* 	this segment. */
					/*	i.e. ?[O] option in mapfile */
#define	FLG_SG_NOHDR	0x0100		/* don't map ELF or phdrs into */
					/*	this segment */

struct sec_order {
	const char *	sco_secname;	/* section name to be ordered */
	Word		sco_index;	/* ordering index for section */
	Half		sco_flags;
};

#define	FLG_SGO_USED	0x0001		/* was ordering used? */

struct ent_desc {			/* input section entrance criteria */
	List		ec_files;	/* files from which to accept */
					/*	sections */
	const char *	ec_name;	/* name to match (NULL if none) */
	Word		ec_type;	/* section type */
	Half		ec_attrmask;	/* section attribute mask (AWX) */
	Half		ec_attrbits;	/* sections attribute bits */
	Sg_desc *	ec_segment;	/* output segment to enter if matched */
	Word		ec_ndx;		/* index to determine where section */
					/*	meeting this criteria should */
					/*	inserted. Used for reordering */
					/*	of sections. */
	Half		ec_flags;
};

#define	FLG_EC_USED	0x0001		/* entrance criteria met? */


/*
 * To improve symbol lookup and processing performance, a number of structures
 * are maintained to localize references.  Symbol names are first hashed to
 * locate the appropriate bucket.  From here a linked list of Sym_cache
 * structures may be present, normally one Sym_cache node is sufficient.
 *
 *	ofl_symbucket -> -------
 *			|	|	 ---------------
 *			|    -------->	|    sc_next	|
 *			|	|	|    sc_end	|
 *			|	|	|    sc_free    |
 *					 ---------------
 *					|  sym_desc {}	|
 *					|  sym_desc {}	|
 *					|  sym_desc {}	|
 *					|    "    "	|
 *					 ---------------
 *
 * The symbol descriptor is sufficient to record the information necessary to
 * process local symbols as well as the `common' information for global symbols.
 */
struct sym_desc {
	Sym *		sd_sym;		/* pointer to symbol table entry */
	const char *	sd_name;	/* symbols name */
	Half		sd_ref;		/* reference definition of symbol */
	Half		sd_flags;	/* state flags */
	Ifl_desc *	sd_file;	/* file where symbol is taken */
	Is_desc *	sd_isc;		/* input section of symbol definition */
	Word		sd_symndx;	/* index in output symbol table */
	Word		sd_GOTndx;	/* index into GOT for symbol */
	Sym_aux *	sd_aux;		/* auxiliary global symbol info. */
};

/*
 * The auxiliary symbol descriptor contains the additional information (beyond
 * the symbol descriptor) required to process global symbols.  These symbols are
 * accessed via an internal symbol hash table where locality of reference is
 * important for performance.
 */
struct sym_aux {
	Word		sa_hash;	/* the pure hash value of symbol */
	const char * 	sa_rfile;	/* file with first symbol referenced */
	List 		sa_dfiles;	/* files where symbol is defined */
	Word		sa_PLTndx;	/* index into PLT for symbol */
#if	defined(i386)
	Word		sa_PLTGOTndx;	/* GOT entry indx for PLT indirection */
#endif
	Word		sa_linkndx;	/* index of associated symbol */
	Sym		sa_sym;		/* copy of symtab entry */
	Half		sa_symspec;	/* special symbol ids */
	Half		sa_verndx;	/* versioning index */
	const char *	sa_vfile;	/* first unavailable definition */
};

/*
 * These are the ids for processing of `Special symbols'.  They are used
 * to set the sym->sd_aux->sa_symspec field.
 */
#define	SDAUX_ID_ETEXT	1		/* etext && _etext symbol */
#define	SDAUX_ID_EDATA	2		/* edata && _edata symbol */
#define	SDAUX_ID_END	3		/* end && _end symbol */
#define	SDAUX_ID_DYN	4		/* DYNAMIC && _DYNAMIC symbol */
#define	SDAUX_ID_PLT	5		/* _PROCEDURE_LINKAGE_TABLE_ symbol */
#define	SDAUX_ID_GOT	6		/* _GLOBAL_OFFSET_TABLE_ symbol */

/*
 * Flags for sym_desc
 */
#define	FLG_SY_MVTOCOMM	0x0001		/* assign symbol to common (.bss) */
#define	FLG_SY_GLOBREF	0x0002		/* a global reference has been seen */
#define	FLG_SY_WEAKDEF	0x0004		/* a weak definition has been used */
#define	FLG_SY_CLEAN	0x0008		/* `Sym' entry points to original */
					/*	input file (read-only). */
#define	FLG_SY_UPREQD	0x0010		/* symbol value update is required, */
					/*	either it's used as an entry */
					/*	point or for relocation, but */
					/*	it must be updated even if */
					/*	the -s flag is in effect */
#define	FLG_SY_NOTAVAIL	0x0020		/* symbol is not available to the */
					/*	application either because it */
					/*	originates from an implicitly */
					/* 	referenced shared object, or */
					/*	because it is not part of a */
					/*	specified version. */
#define	FLG_SY_REDUCED	0x0040		/* a global is reduced to local */
#define	FLG_SY_VERSPROM	0x0080		/* version definition has been */
					/*	promoted to output file. */
#define	FLG_SY_SEGSIZE	0x0100		/* segment size symbol (map_atsign()) */

#define	FLG_SY_GLOBAL	0x1000		/* global symbol - remain global */
#define	FLG_SY_SYMBOLIC	0x2000		/* global symbol - reduce relocation */
#define	FLG_SY_LOCAL	0x4000		/* global symbol - reduce to local */
#define	FLG_SY_REFRSD	0x8000		/* symbols sd_ref has been raised */
					/* 	due to a copy-relocs */
					/*	weak-strong pairing */

#define	MSK_SY_DEFINED	(FLG_SY_GLOBAL | FLG_SY_SYMBOLIC | FLG_SY_LOCAL)

struct sym_cache {
	Sym_cache *	sc_next;	/* next available symbol cache */
	Sym_desc *	sc_end;		/* last available list entry */
	Sym_desc *	sc_free;	/* next available list entry */
};

struct sym_names {
	Sym_names *	sn_next;	/* next available dictionary */
	char *		sn_end;		/* last available character entry */
	char *		sn_free;	/* next available character entry */
};

/*
 * Structure to manage the shared object definition lists.  There are two lists
 * that use this structure:
 *
 *  o	ofl_soneed; maintain the list of implicitly required dependencies
 *	(ie. shared objects needed by other shared objects).  These definitions
 *	may include RPATH's required to locate the dependencies, and any
 *	version requirements.
 *
 *  o	ofl_socntl; maintains the shared object control definitions.  These are
 *	provided by the user (via a mapfile) and are used to indicate any USED
 *	dependency, or SONAME translations, and any verion control requirements.
 */
struct	sdf_desc {
	const char *	sdf_name;	/* the shared objects file name */
	const char *	sdf_soname;	/* the shared objects SONAME */
	char *		sdf_rpath;	/* library search path DT_RPATH */
	const char *	sdf_rfile;	/* referencing file for diagnostics */
	Ifl_desc *	sdf_file;	/* the final input file descriptor */
	List		sdf_vers;	/* list of versions that are required */
					/*	from this object */
	Word		sdf_flags;
};

#define	FLG_SDF_USED	0x01		/* DT_USED definition required */
#define	FLG_SDF_SONAME	0x02		/* An alternative SONAME is supplied */
#define	FLG_SDF_SELECT	0x04		/* version control selection required */
#define	FLG_SDF_VERIFY	0x08		/* version definition verification */
					/*	required */

/*
 * Structure to manage shared object version usage requirements.
 */
struct	sdv_desc {
	const char *	sdv_name;	/* version name */
	const char *	sdv_ref;	/* versions reference */
};

/*
 * Structures to manage versioning information.  Two versioning structures are
 * defined:
 *
 *   o	a version descriptor maintains a linked list of versions and their
 *	associated dependencies.  This is used to build the version definitions
 *	for an image being created (see map_symbol), and to determine the
 *	version dependency graph for any input files that are versioned.
 *
 *   o	a version index array contains each version of an input file that is
 *	being processed.  It informs us which versions are available for
 *	binding, and is used to generate any version dependency information.
 */
struct	ver_desc {
	const char *	vd_name;	/* version name */
	unsigned long	vd_hash;	/* hash value of name */
	Ifl_desc *	vd_file;	/* file that defined version */
	Half		vd_ndx;		/* coordinates with symbol index */
	Half		vd_flags;	/* version information */
	List		vd_deps;	/* version dependencies */
	Ver_desc *	vd_ref;		/* dependency's first reference */
};

struct	ver_index {
	const char *	vi_name;	/* dependency version name */
	Half		vi_flags;	/* communicates availability */
	Ver_desc *	vi_desc;	/* cross reference to descriptor */
};

/*
 * Define any internal version descriptor flags (for vi_flags).  Note that the
 * first byte is reserved for user visible flags (refer VER_FLG's in link.h).
 */
#define	MSK_VER_USER	0x0f		/* mask for user visible flags */

#define	FLG_VER_AVAIL	0x10		/* version is available for binding */
#define	FLG_VER_REFER	0x20		/* version has been referenced */
#define	FLG_VER_SELECT	0x40		/* version has been selected by user */

/*
 * Function Declarations
 */
extern int		create_outfile(Ofl_desc *);
extern int		ent_setup(Ofl_desc *);
extern void		eprintf(Error, const char *, ...);
extern int		finish_libs(Ofl_desc *);
extern int		ld_support_loadso(const char *);
extern Listnode *	list_append(List *, const void *);
extern Listnode *	list_insert(List *, const void *, Listnode *);
extern Listnode *	list_prepend(List *, const void *);
extern Listnode *	list_where(List *, Word);
extern int		make_sections(Ofl_desc *);
extern int		open_outfile(Ofl_desc *);
extern Ifl_desc *	process_open(const char *, int, int, Ofl_desc *, Half);
extern int		reloc_init(Ofl_desc *);
extern int		reloc_process(Ofl_desc *);
extern Sdf_desc *	sdf_find(const char *, List *);
extern Sdf_desc *	sdf_desc(const char *, List *);
extern Sym_desc *	sym_add_u(const char *, Ofl_desc *);
extern Sym_desc *	sym_enter(const char *, Sym *, Word, Ifl_desc *,
				Ofl_desc *, int);
extern Sym_desc *	sym_find(const char *, unsigned long, Ofl_desc *);
extern int		sym_validate(Ofl_desc *);
extern Addr		update_outfile(Ofl_desc *);
extern Ver_desc *	vers_base(Ofl_desc *);
extern int		vers_check_defs(Ofl_desc *);
extern Ver_desc *	vers_desc(const char *, unsigned long, List *);
extern Ver_desc *	vers_find(const char *, unsigned long, List *);

#endif
