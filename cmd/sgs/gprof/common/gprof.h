/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)gprof.h	1.11	94/06/03 SMI"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "gcrt0.h"
#include <elf.h>

#include "sparc.h"


/*
 * who am i, for error messages.
 */
extern char	*whoami;

/*
 * booleans
 */
typedef int	bool;
#define	FALSE	0
#define	TRUE	1

/*
 *	ticks per second
 */
long	hz;

typedef	short UNIT;		/* unit of profiling */
typedef unsigned short	unsigned_UNIT; /* to remove warnings from gprof.c */
char	*a_outname;
char	*prog_name;	/* keep the program name for error messages */
#define	A_OUTNAME		"a.out"

char	*gmonname;
#define	GMONNAME		"gmon.out"
#define	GMONSUM			"gmon.sum"

/*
 * Special symbols used for profiling of shared libraries through
 * the run-time linker.
 */
#define	PRF_ETEXT		"_etext"
#define	PRF_EXTSYM		"<external>"
#define	PRF_MEMTERM		"_END_OF_VIRTUAL_MEMORY"
#define	PRF_SYMCNT		3

/*
 *	blurbs on the flat and graph profiles.
 */
#define	FLAT_BLURB	"/gprof.flat.blurb"
#define	CALLG_BLURB	"/gprof.callg.blurb"
/*
 *	a constructed arc,
 *	    with pointers to the namelist entry of the parent and the child,
 *	    a count of how many times this arc was traversed,
 *	    and pointers to the next parent of this child and
 *		the next child of this parent.
 */
struct arcstruct {
    struct nl		*arc_parentp;	/* pointer to parent's nl entry */
    struct nl		*arc_childp;	/* pointer to child's nl entry */
    long		arc_count;	/* how calls from parent to child */
    double		arc_time;	/* time inherited along arc */
    double		arc_childtime;	/* childtime inherited along arc */
    struct arcstruct	*arc_parentlist; /* parents-of-this-child list */
    struct arcstruct	*arc_childlist;	/* children-of-this-parent list */
};
typedef struct arcstruct	arctype;

/*
 * The symbol table;
 * for each external in the specified file we gather
 * its address, the number of calls and compute its share of cpu time.
 */
struct nl {
    char		*name;		/* the name */
    unsigned long	value;		/* the pc entry point */
    unsigned long	svalue;		/* entry point aligned to histograms */
    unsigned char	syminfo;	/* sym info */
    double		time;		/* ticks in this routine */
    double		childtime;	/* cumulative ticks in children */
    long		ncall;		/* how many times called */
    long		selfcalls;	/* how many calls to self */
    double		propfraction;	/* what % of time propagates */
    double		propself;	/* how much self time propagates */
    double		propchild;	/* how much child time propagates */
    bool		printflag;	/* should this be printed? */
    int			index;		/* index in the graph list */
    int			toporder;	/* graph call chain top-sort order */
    int			cycleno;	/* internal number of cycle on */
    struct nl		*cyclehead;	/* pointer to head of cycle */
    struct nl		*cnext;		/* pointer to next member of cycle */
    arctype		*parents;	/* list of caller arcs */
    arctype		*children;	/* list of callee arcs */
};
typedef struct nl	nltype;

nltype	*nl;			/* the whole namelist */
nltype	*npe;			/* the virtual end of the namelist */
int	nname;			/* the number of function names */

/*
 *	flag which marks a nl entry as topologically ``busy''
 *	flag which marks a nl entry as topologically ``not_numbered''
 */
#define	DFN_BUSY	-1
#define	DFN_NAN		0

/*
 *	namelist entries for cycle headers.
 *	the number of discovered cycles.
 */
nltype	*cyclenl;		/* cycle header namelist */
int	ncycle;			/* number of cycles discovered */

/*
 * The header on the gmon.out file.
 * gmon.out consists of one of these headers,
 * and then an array of ncnt samples
 * representing the discretized program counter values.
 *	this should be a struct phdr, but since everything is done
 *	as UNITs, this is in UNITs too.
 */
struct hdr {
    UNIT	*lowpc;
    UNIT	*highpc;
    int	ncnt;
};

struct hdr	h;		/* header of profiled data */

int	debug;
int 	number_funcs_toprint;

/*
 * Each discretized pc sample has
 * a count of the number of samples in its range
 */
unsigned short	*samples;

unsigned long	s_lowpc;	/* lowpc from the profile file */
unsigned long	s_highpc;	/* highpc from the profile file */
unsigned sampbytes;		/* number of bytes of samples */
int	nsamples;		/* number of samples */
double	actime;			/* accumulated time thus far for putprofline */
double	totime;			/* total time for all routines */
double	printtime;		/* total of time being printed */
double	scale;			/* scale factor converting samples to pc */
				/* values: each sample covers scale bytes */
off_t	ssiz;			/* size of the string table */

unsigned char	*textspace;		/* text space of a.out in core */
bool	first_file;			/* for difference option */

/*
 *	option flags, from a to z.
 */
bool	aflag;				/* suppress static functions */
bool	bflag;				/* blurbs, too */
bool	cflag;				/* discovered call graph, too */
bool	Cflag;				/* gprofing c++ -- need demangling */
bool	dflag;				/* debugging options */
bool	Dflag;				/* difference option */
bool	eflag;				/* specific functions excluded */
bool	Eflag;				/* functions excluded with time */
bool	fflag;				/* specific functions requested */
bool	Fflag;				/* functions requested with time */
bool	lflag;				/* exclude LOCAL syms in output */
bool	sflag;				/* sum multiple gmon.out files */
bool	zflag;				/* zero time/called functions, too */
bool 	nflag;				/* print only n functions in report */
bool	rflag;				/* profiling input generated by */
					/* run-time linker */

/*
 *	structure for various string lists
 */
struct stringlist {
    struct stringlist	*next;
    char		*string;
};
extern struct stringlist	*elist;
extern struct stringlist	*Elist;
extern struct stringlist	*flist;
extern struct stringlist	*Flist;

/*
 *	function declarations
 */
int	addlist(struct stringlist *, char *);
void	addarc();
int	arccmp();
arctype	*arclookup();
int	printblurb();
int	dfn();
bool	dfn_busy();
int	dfn_findcycle();
bool	dfn_numbered();
int	dfn_post_visit();
int	dfn_pre_visit();
int	dfn_self_cycle();
nltype	**doarcs();
void	done();
int	findcalls();
int	flatprofheader();
int	flatprofline();
void	getnfile(char *);
int	gprofheader();
int	gprofline();
int	hertz();
int	main();
int	membercmp();
nltype	*nllookup();
bool	onlist();
int	printchildren();
int	printcycle();
int	printgprof();
int	printindex();
int	printmembers();
int	printname();
int	printparents();
int	printprof();
int	sortchildren();
int	sortmembers();
int	sortparents();
int	timecmp();
int	totalcmp();
int	valcmp();

#define	LESSTHAN	-1
#define	EQUALTO		0
#define	GREATERTHAN	1

#define	DFNDEBUG	1
#define	CYCLEDEBUG	2
#define	ARCDEBUG	4
#define	TALLYDEBUG	8
#define	TIMEDEBUG	16
#define	SAMPLEDEBUG	32
#define	ELFDEBUG	64
#define	CALLSDEBUG	128
#define	LOOKUPDEBUG	256
#define	PROPDEBUG	512
#define	ANYDEBUG	1024
