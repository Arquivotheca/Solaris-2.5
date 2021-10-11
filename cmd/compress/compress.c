/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 * Copyright (c) 1986, 1987, 1988, Sun Microsystems, Inc.
 * All Rights Reserved.
 */

#ident	"@(#)compress.c	1.18	95/08/30 SMI"	/* SVr4.0 1.4	*/
/*
 * Compress - data compression program
 */
#define	min(a, b)	((a > b) ? b : a)

/*
 * machine variants which require cc -Dmachine:  pdp11, z8000, pcxt
 */

/*
 * Set USERMEM to the maximum amount of physical user memory available
 * in bytes.  USERMEM is used to determine the maximum BITS that can be used
 * for compression.
 *
 * SACREDMEM is the amount of physical memory saved for others; compress
 * will hog the rest.
 */
#ifndef SACREDMEM
#define	SACREDMEM	0
#endif

#ifndef USERMEM
#define	USERMEM 	450000	/* default user memory */
#endif

#ifdef USERMEM
#if USERMEM >= (433484+SACREDMEM)
#define	PBITS	16
#else
#if USERMEM >= (229600+SACREDMEM)
#define	PBITS	15
#else
#if USERMEM >= (127536+SACREDMEM)
#define	PBITS	14
#else
#if USERMEM >= (73464+SACREDMEM)
#define	PBITS	13
#else
#define	PBITS	12
#endif
#endif
#endif
#endif
#undef USERMEM
#endif /* USERMEM */

#ifdef PBITS		/* Preferred BITS for this memory size */
#ifndef BITS
#define	BITS PBITS
#endif /* BITS */
#endif /* PBITS */

#if BITS == 16
#define	HSIZE	69001		/* 95% occupancy */
#endif
#if BITS == 15
#define	HSIZE	35023		/* 94% occupancy */
#endif
#if BITS == 14
#define	HSIZE	18013		/* 91% occupancy */
#endif
#if BITS == 13
#define	HSIZE	9001		/* 91% occupancy */
#endif
#if BITS <= 12
#define	HSIZE	5003		/* 80% occupancy */
#endif

#define OUTSTACKSIZE	(2<<BITS)

/*
 * a code_int must be able to hold 2**BITS values of type int, and also -1
 */
#if BITS > 15
typedef long int	code_int;
#else
typedef int		code_int;
#endif

typedef long int	  count_int;

typedef	unsigned char	char_type;

static char_type magic_header[] = { "\037\235" }; /* 1F 9D */

/* Defines for third byte of header */
#define	BIT_MASK	0x1f
#define	BLOCK_MASK	0x80
/*
 * Masks 0x40 and 0x20 are free.  I think 0x20 should mean that there is
 * a fourth header byte(for expansion).
*/
#define	INIT_BITS 9			/* initial number of bits/code */

/*
 * compress.c - File compression ala IEEE Computer, June 1984.
 */
static char rcs_ident[] =
	"$Header: compress.c,v 4.0 85/07/30 12:50:00 joe Release $";

#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdlib.h>			/* XCU4 */
#include <limits.h>
#include <libintl.h>
#include <locale.h>
#include <langinfo.h>
#include <string.h>
#include <utime.h>

/*
 * Multi-byte handling for 'y' or 'n'
 */
static char	*yesstr;		/* string contains int'l for "yes" */
static char	*nostr;			/* string contains int'l for "yes" */
static int	ynsize = 0;		/* # of (multi)bytes for "y" */
static char	*yesorno;		/* int'l input for 'y' */

static int n_bits;			/* number of bits/code */
static int maxbits = BITS;	/* user settable max # bits/code */
static code_int maxcode;	/* maximum code, given n_bits */
			/* should NEVER generate this code */
static code_int maxmaxcode = 1 << BITS;
#define	MAXCODE(n_bits)	((1 << (n_bits)) - 1)

static count_int htab [OUTSTACKSIZE];
static unsigned short codetab [OUTSTACKSIZE];

#define	htabof(i)	htab[i]
#define	codetabof(i)	codetab[i]
static code_int hsize = HSIZE; /* for dynamic table sizing */
static count_int fsize;

/*
 * To save much memory, we overlay the table used by compress() with those
 * used by decompress().  The tab_prefix table is the same size and type
 * as the codetab.  The tab_suffix table needs 2**BITS characters.  We
 * get this from the beginning of htab.  The output stack uses the rest
 * of htab, and contains characters.  There is plenty of room for any
 * possible stack (stack used to be 8000 characters).
 */

#define	tab_prefixof(i)		codetabof(i)
#define	tab_suffixof(i)		((char_type *)(htab))[i]
#define	de_stack		((char_type *)&tab_suffixof(1<<BITS))
#define	stack_max		((char_type *)&tab_suffixof(OUTSTACKSIZE))

static code_int free_ent = 0; /* first unused entry */
static int exit_stat = 0;	/* per-file status */
static int perm_stat = 0;	/* permanent status */

static code_int getcode();

	/* Use a 3-byte magic number header, unless old file */
static int nomagic = 0;
	/* Write output on stdout, suppress messages */
static int zcat_flg = 0;
static int zcat_cmd = 0;	/* zcat cmd */
	/* Don't unlink output file on interrupt */
static int precious = 1;
static int quiet = 1;	/* don't tell me about compression */

/*
 * block compression parameters -- after all codes are used up,
 * and compression rate changes, start over.
 */
static int block_compress = BLOCK_MASK;
static int clear_flg = 0;
static long int ratio = 0;
#define	CHECK_GAP 10000	/* ratio check interval */
static count_int checkpoint = CHECK_GAP;
/*
 * the next two codes should not be changed lightly, as they must not
 * lie within the contiguous general code space.
 */
#define	FIRST	257	/* first free entry */
#define	CLEAR	256	/* table clear output code */

static int force = 0;
static char ofname [MAXPATHLEN];
static int max_name = _POSIX_NAME_MAX;
static int max_path = MAXPATHLEN;

static int Vflg = 0;
static int vflg = 0;
static int qflg = 0;
static int bflg = 0;
static int Fflg = 0;
static int dflg = 0;
static int cflg = 0;
static int Cflg = 0;

#ifdef DEBUG
int verbose = 0;
int debug = 0;
#endif /* DEBUG */
static void (*oldint)();
static int bgnd_flag;

static int do_decomp = 0;

static char *progname;
/*
 * Fix lint errors
 */
extern int utime(const char *, const struct utimbuf *);
void exit();
static void output();
static char *rindex();
static void decompress();
static void version();
static void prratio();
static void cl_hash();
static void cl_block();
static void copystat();
static void writeerr();
static void compress();
static void Usage();
static void onintr();
static void oops();
static char *basename();

/*
 * *************************************************************
 * TAG( main )
 *
 * Algorithm from "A Technique for High Performance Data Compression",
 * Terry A. Welch, IEEE Computer Vol 17, No 6 (June 1984), pp 8-19.
 *
 * Usage: compress [-dfvc] [-b bits] [file ...]
 * Inputs:
 *	-d:	    If given, decompression is done instead.
 *
 *	-c:	    Write output on stdout, don't remove original.
 *
 *	-b:	    Parameter limits the max number of bits/code.
 *
 *	-f:	    Forces output file to be generated, even if one already
 *		    exists, and even if no space is saved by compressing.
 *		    If -f is not used, the user will be prompted if stdin is
 *		    a tty, otherwise, the output file will not be overwritten.
 *
 *  -v:	    Write compression statistics
 *
 * 	file ...:   Files to be compressed.  If none specified, stdin
 *		    is used.
 * Outputs:
 *	file.Z:	    Compressed form of file with same mode, owner, and utimes
 * 	or stdout   (if stdin used as input)
 *
 * Assumptions:
 * When filenames are given, replaces with the compressed version
 * (.Z suffix) only if the file decreases in size.
 * Algorithm:
 * Modified Lempel-Ziv method (LZW).  Basically finds common
 * substrings and replaces them with a variable size code.  This is
 * deterministic, and can be done on the fly.  Thus, the decompression
 * procedure needs no input table, but tracks the way the table was built.
 */

void
main(argc, argv)
register int argc; char **argv;
{
	int overwrite = 0;	/* Do not overwrite unless given -f flag */
	char tempname[MAXPATHLEN];
	char line[LINE_MAX];
	char **filelist, **fileptr;
	char *cp;
	struct stat statbuf;
	int ch;				/* XCU4 */
	char	*p, *yptr, *nptr;
	extern int optind, optopt;
	extern char *optarg;

	/* XCU4 changes */
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);
	/* Build multi-byte char for 'y' char */
	if ((yptr = nl_langinfo(YESSTR)) == NULL)
		yptr = "y";

	yesstr = (char *) malloc(strlen(yptr) + 1);
	(void) strcpy(yesstr, yptr);
	/* Build multi-byte char for 'n' char */
	if ((nptr = nl_langinfo(NOSTR)) == NULL)
		nptr = "n";

	nostr = (char *) malloc(strlen(nptr) + 1);
	(void) strcpy(nostr, nptr);

	/* Build multi-byte char for input char */
	yesorno = (char *) malloc((size_t) ynsize + 1);
	ynsize = mblen(yesstr, strlen(yesstr));

	/* This bg check only works for sh. */
	if ((oldint = signal(SIGINT, SIG_IGN)) != SIG_IGN) {
		(void) signal(SIGINT, onintr);
		(void) signal(SIGSEGV, oops);
	}
	bgnd_flag = oldint != SIG_DFL;

	filelist = fileptr = (char **) (malloc(argc * sizeof (*argv)));
	*filelist = NULL;

	if ((cp = rindex(argv[0], '/')) != 0) {
		cp++;
	} else {
		cp = argv[0];
	}

	if (strcmp(cp, "uncompress") == 0) {
		do_decomp = 1;
	} else if (strcmp(cp, "zcat") == 0) {
		do_decomp = 1;
		zcat_cmd = zcat_flg = 1;
	}

	progname = basename(argv[0]);

	/*
	 * Argument Processing
	 * All flags are optional.
	 * -D = > debug
	 * -V = > print Version; debug verbose
	 * -d = > do_decomp
	 * -v = > unquiet
	 * -f = > force overwrite of output file
	 * -n = > no header: useful to uncompress old files
	 * -b     maxbits => maxbits.  If -b is specified,
	 *        then maxbits MUST be given also.
	 * -c = > cat all output to stdout
	 * -C = > generate output compatible with compress 2.0.
	 * if a string is left, must be an input filename.
	 */
#ifdef DEBUG
	while ((ch = getopt(argc, argv, "b:cCdDfFnqvV")) != EOF) {
#else
	while ((ch = getopt(argc, argv, "b:cCdfFnqvV")) != EOF) {
#endif

		/* Process all flags in this arg */
		switch (ch) {
#ifdef DEBUG
		case 'D':
			debug = 1;
			break;
		case 'V':
			verbose = 1;
			version();
			break;
#else
		case 'V':
			version();
			Vflg++;
			break;
#endif /* DEBUG */
		case 'v':
			quiet = 0;
			vflg++;
			break;
		case 'd':
			do_decomp = 1;
			dflg++;
			break;
		case 'f':
		case 'F':
			Fflg++;
			overwrite = 1;
			force = 1;
			break;
		case 'n':
			nomagic = 1;
			break;
		case 'C':
			Cflg++;
			block_compress = 0;
			break;
		case 'b':
			bflg++;
			p = optarg;
			if (!p) {
				(void) fprintf(stderr, gettext(
					"Missing maxbits\n"));
				Usage();
				exit(1);
			}
			maxbits = strtoul(optarg, &p, 10);
			if (*p) {
				(void) fprintf(stderr, gettext(
					"Missing maxbits\n"));
				Usage();
				exit(1);
			}
			break;

		case 'c':
			cflg++;
			zcat_flg = 1;
			break;
		case 'q':
			qflg++;
			quiet = 1;
			break;
		default:
			(void) fprintf(stderr, gettext(
			    "Unknown flag: '%c'\n"), optopt);
			Usage();
			exit(1);
		}
	} /* while */

	for (; optind < argc; optind++) {
		*fileptr++ = argv[optind];	/* Build input file list */
		*fileptr = NULL;
	}

	if (maxbits < INIT_BITS)
		maxbits = INIT_BITS;
	if (maxbits > BITS)
		maxbits = BITS;
	maxmaxcode = 1 << maxbits;

	if ((*filelist != NULL) && strcmp(*filelist, "-")) {
		for (fileptr = filelist; *fileptr; fileptr++) {
			exit_stat = 0;
			if (do_decomp) {	/* DECOMPRESSION */
				/* Check for .Z suffix */
				if (strcmp(*fileptr +
					strlen(*fileptr) - 2, ".Z") != 0) {
					/* No .Z: tack one on */
					(void) strcpy(tempname, *fileptr);
					(void) strcat(tempname, ".Z");
					*fileptr = tempname;
				}
				/* Open input file */
				if ((freopen(*fileptr, "r", stdin)) == NULL) {
					perror(*fileptr);
					perm_stat = 1;
					continue;
				}
				/* Check the magic number */
				if (nomagic == 0) {
					if ((getchar() !=
					    (magic_header[0] & 0xFF)) ||
					    (getchar() !=
					    (magic_header[1] & 0xFF))) {
						(void) fprintf(stderr,
						    gettext(
			    "%s: not in compressed format\n"),
						*fileptr);
						perm_stat = 1;
						continue;
					}
					/* set -b from file */
					maxbits = getchar();
					block_compress = maxbits & BLOCK_MASK;
					maxbits &= BIT_MASK;
					maxmaxcode = 1 << maxbits;
					if (maxbits > BITS) {
						(void) fprintf(stderr,
	gettext("%s: compressed with %d bits, can only handle %d bits\n"),
						*fileptr, maxbits, BITS);
						continue;
					}
				}
				/* Generate output filename */
				(void) strcpy(ofname, *fileptr);
				/* Strip off .Z */
				ofname[strlen(*fileptr) - 2] = '\0';
			} else {		/* COMPRESSION */
				if (strcmp(*fileptr +
					strlen(*fileptr) - 2, ".Z") == 0) {
					(void) fprintf(stderr,
	gettext("%s: already has .Z suffix -- no change\n"),
						*fileptr);
					continue;
				}
				/* Open input file */
				if ((freopen(*fileptr, "r", stdin)) == NULL) {
					perror(*fileptr);
					perm_stat = 1;
					continue;
				}
				(void) stat(*fileptr, &statbuf);
				fsize = (long) statbuf.st_size;
				/*
				 * tune hash table size for small
				 * files -- ad hoc,
				 * but the sizes match earlier #defines, which
				 * serve as upper bounds on the number of
				 * output codes.
				 */
				hsize = HSIZE;
				if (fsize < (1 << 12))
					hsize = min(5003, HSIZE);
				else if (fsize < (1 << 13))
					hsize = min(9001, HSIZE);
				else if (fsize < (1 << 14))
					hsize = min(18013, HSIZE);
				else if (fsize < (1 << 15))
					hsize = min(35023, HSIZE);
				else if (fsize < 47000)
					hsize = min(50021, HSIZE);

				/* Generate output filename */
				(void) strcpy(ofname, *fileptr);
				max_name = pathconf(ofname, _PC_NAME_MAX);

				if ((strlen(basename(ofname)) + 2) >
					(size_t) max_name) {
					(void) fprintf(stderr,
		gettext("%s: filename too long to tack on .Z\n"), cp);
					exit_stat = 1;
					continue;
				}
				if ((strlen(ofname) + 2) >
					(size_t) max_path - 1) {
					(void) fprintf(stderr,
		gettext("%s: Pathname too long to tack on .Z\n"), cp);
					exit_stat = 1;
					continue;
				}
				(void) strcat(ofname, ".Z");

			}	/* if (do_decomp) */

			/* Check for overwrite of existing file */
			if (overwrite == 0 && zcat_flg == 0) {
				if (stat(ofname, &statbuf) == 0) {

					yesorno[ynsize] = (char) NULL;
					(void) fprintf(stderr, gettext(
					    "%s already exists;"), ofname);
					if (bgnd_flag == 0 && isatty(2)) {
						int cin;

						(void) fprintf(stderr, gettext(
			    " do you wish to overwrite %s (%s or %s)? "),
							ofname, yesstr, nostr);
						(void) fflush(stderr);
						for (cin = 0; line[cin] = 0;
								cin++);
						(void) read(2, line, LINE_MAX);
						(void) strncpy(yesorno, line,
							ynsize);

						if (!((strncmp(yesstr, yesorno,
						    ynsize) == 0) ||
						    (yesorno[0] == 'y') ||
							(yesorno[0] == 'Y'))) {
							(void) fprintf(stderr,
							    gettext(
							"\tnot overwritten\n"));
							continue;
						}
					} else {
						/*
						 * XPG4: Assertion 1009
						 * Standard input is not
						 * terminal, and no '-f',
						 * and file exists.
						 */
						exit(1);
					}
				}
			}
			if (zcat_flg == 0) { /* Open output file */
				if (freopen(ofname, "w", stdout) == NULL) {
					perror(ofname);
					perm_stat = 1;
					continue;
				}
				precious = 0;
				if (!quiet)
					(void) fprintf(stderr, "%s: ",
						*fileptr);
			}

			/* Actually do the compression/decompression */
			if (do_decomp == 0)
				compress();
#ifndef DEBUG
			else
				decompress();
#else
			else if (debug == 0)
				decompress();
			else
				printcodes();
			if (verbose)
				dump_tab();
#endif /* DEBUG */
			if (zcat_flg == 0) {
				copystat(*fileptr, ofname);	/* Copy stats */
				precious = 1;
				if ((exit_stat == 1) || (!quiet))
					(void) putc('\n', stderr);
				/*
				 * Print the info. for unchanged file
				 * when no -v
				 */
				if ((exit_stat == 2) && quiet)
					(void) fprintf(stderr, gettext(
					    "%s: -- file unchanged\n"),
						*fileptr);
			}
		}	/* for */
	} else {		/* Standard input */
		if (do_decomp == 0) {
			compress();
#ifdef DEBUG
			if (verbose)
				dump_tab();
#endif /* DEBUG */
			if (!quiet)
				(void) putc('\n', stderr);
		} else {
			/* Check the magic number */
			if (nomagic == 0) {
				if ((getchar() != (magic_header[0] & 0xFF)) ||
				    (getchar() != (magic_header[1] & 0xFF))) {
					(void) fprintf(stderr, gettext(
			    "stdin: not in compressed format\n"));
					exit(1);
				}
				maxbits = getchar();	/* set -b from file */
				block_compress = maxbits & BLOCK_MASK;
				maxbits &= BIT_MASK;
				maxmaxcode = 1 << maxbits;
				/* assume stdin large for USERMEM */
				fsize = 100000;
				if (maxbits > BITS) {
					(void) fprintf(stderr, gettext(
	    "stdin: compressed with %d bits, can only handle %d bits\n"),
							maxbits, BITS);
					exit(1);
				}
			}
#ifndef DEBUG
			decompress();
#else
			if (debug == 0)
				decompress();
			else
				printcodes();
			if (verbose)
				dump_tab();
#endif /* DEBUG */
		}	/* if (do_decomp == 0) */
	}
	(void) exit(perm_stat ? perm_stat : exit_stat);
}

static int offset;
static long int in_count = 1;	/* length of input */
static long int bytes_out;	/* length of compressed output */
	/* # of codes output (for debugging) */
static long int out_count = 0;

/*
 * compress stdin to stdout
 *
 * Algorithm:  use open addressing double hashing(no chaining) on the
 * prefix code / next character combination.  We do a variant of Knuth's
 * algorithm D (vol. 3, sec. 6.4) along with G. Knott's relatively-prime
 * secondary probe.  Here, the modular division first probe is gives way
 * to a faster exclusive-or manipulation.  Also do block compression with
 * an adaptive reset, whereby the code table is cleared when the compression
 * ratio decreases, but after the table fills.  The variable-length output
 * codes are re-sized at this point, and a special CLEAR code is generated
 * for the decompressor.  Late addition:  construct the table according to
 * file size for noticeable speed improvement on small files.  Please direct
 * questions about this implementation to ames!jaw.
 */

void
compress() {
	register long fcode;
	register code_int i = 0;
	register int c;
	register code_int ent;
	register int disp;
	register code_int hsize_reg;
	register int hshift;

	if (nomagic == 0) {
		(void) putchar(magic_header[0]);
		(void)  putchar(magic_header[1]);
		(void) putchar((char)(maxbits | block_compress));
		if (ferror(stdout))
			writeerr();
	}

	offset = 0;
	bytes_out = 3;		/* includes 3-byte header mojo */
	out_count = 0;
	clear_flg = 0;
	ratio = 0;
	in_count = 1;
	checkpoint = CHECK_GAP;
	maxcode = MAXCODE(n_bits = INIT_BITS);
	free_ent = ((block_compress) ? FIRST : 256);

	ent = getchar();

	hshift = 0;
	for (fcode = (long) hsize;  fcode < 65536L; fcode *= 2L)
		hshift++;
	hshift = 8 - hshift;		/* set hash code range bound */

	hsize_reg = hsize;
	cl_hash((count_int) hsize_reg);		/* clear hash table */

	while ((c = getchar()) != EOF) {
		in_count++;
		fcode = (long) (((long) c << maxbits) + ent);
		i = ((c << hshift) ^ ent);	/* xor hashing */

		if (htabof(i) == fcode) {
			ent = codetabof(i);
			continue;
		} else if ((long)htabof(i) < 0)	/* empty slot */
			goto nomatch;
		/* secondary hash (after G. Knott) */
		disp = hsize_reg - i;
		if (i == 0)
			disp = 1;
probe:
		if ((i -= disp) < 0)
			i += hsize_reg;

		if (htabof(i) == fcode) {
			ent = codetabof(i);
			continue;
		}
		if ((long)htabof(i) > 0)
			goto probe;
nomatch:
		output((code_int) ent);
		out_count++;
		ent = c;
		if (free_ent < maxmaxcode) {
			codetabof(i) = free_ent++;	/* code -> hashtable */
			htabof(i) = fcode;
		} else if ((count_int)in_count >= checkpoint && block_compress)
			cl_block();
		}

		if (ferror(stdin) != 0) {
			exit_stat = 1;
			return;
		}

		/*
		 * Put out the final code.
		 */
		output((code_int)ent);
		out_count++;
		output((code_int)-1);

		/*
		 * Print out stats on stderr
		 */
		if (zcat_flg == 0 && !quiet) {
#ifdef DEBUG
			(void) fprintf(stderr,
	"%ld chars in, %ld codes (%ld bytes) out, compression factor: ",
				in_count, out_count, bytes_out);
			prratio(stderr, in_count, bytes_out);
			(void) fprintf(stderr, "\n");
			(void) fprintf(stderr, "\tCompression as in compact: ");
			prratio(stderr, in_count-bytes_out, in_count);
			(void) fprintf(stderr, "\n");
			(void) fprintf(stderr,
		"\tLargest code (of last block) was %d (%d bits)\n",
				free_ent - 1, n_bits);
#else /* !DEBUG */
			(void) fprintf(stderr, gettext("Compression: "));
			prratio(stderr, in_count-bytes_out, in_count);
#endif /* DEBUG */
		}
		if (bytes_out > in_count)	/* exit(2) if no savings */
			exit_stat = 2;
		return;
	}

/*
 * **************************************************************
 * TAG(output)
 *
 * Output the given code.
 * Inputs:
 * 	code:	A n_bits-bit integer.  If == -1, then EOF.  This assumes
 *		that n_bits = < (long)wordsize - 1.
 * Outputs:
 * 	Outputs code to the file.
 * Assumptions:
 *	Chars are 8 bits long.
 * Algorithm:
 * 	Maintain a BITS character long buffer(so that 8 codes will
 * fit in it exactly).  Use the VAX insv instruction to insert each
 * code in turn.  When the buffer fills up empty it and start over.
 */

static char buf[BITS];

static char_type lmask[9] =
	{0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0x00};
static char_type rmask[9] =
	{0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};


static
void
output(code)
code_int  code;
{
#ifdef DEBUG
	static int col = 0;
#endif /* DEBUG */

	/*
	 * On the VAX, it is important to have the register declarations
	 * in exactly the order given, or the asm will break.
	 */
	register int r_off = offset, bits = n_bits;
	register char * bp = buf;

#ifdef DEBUG
	if (verbose)
		(void) fprintf(stderr, "%5d%c", code,
			(col += 6) >= 74 ? (col = 0, '\n') : ' ');
#endif /* DEBUG */
	if (code >= 0) {
		/*
		 * byte/bit numbering on the VAX is simulated
		 * by the following code
		 */
		/*
		 * Get to the first byte.
		 */
		bp += (r_off >> 3);
		r_off &= 7;
		/*
		 * Since code is always >= 8 bits, only need to mask the first
		 * hunk on the left.
		 */
		*bp = (*bp & rmask[r_off]) | (code << r_off) & lmask[r_off];
		bp++;
		bits -= (8 - r_off);
		code >>= 8 - r_off;
		/* Get any 8 bit parts in the middle (<=1 for up to 16 bits). */
		if (bits >= 8) {
			*bp++ = code;
			code >>= 8;
			bits -= 8;
		}
		/* Last bits. */
		if (bits)
			*bp = code;
		offset += n_bits;
		if (offset == (n_bits << 3)) {
			bp = buf;
			bits = n_bits;
			bytes_out += bits;
			do
			(void) putchar(*bp++);
			while (--bits);
				offset = 0;
		}

		/*
		 * If the next entry is going to be too big for the code size,
		 * then increase it, if possible.
		 */
		if (free_ent > maxcode || (clear_flg > 0)) {
			/*
			 * Write the whole buffer, because the input
			 * side won't discover the size increase until
			 * after it has read it.
			 */
			if (offset > 0) {
				if (fwrite(buf, 1, n_bits, stdout) != n_bits)
					writeerr();
				bytes_out += n_bits;
			}
			offset = 0;

			if (clear_flg) {
				maxcode = MAXCODE(n_bits = INIT_BITS);
			    clear_flg = 0;
			} else {
				n_bits++;
				if (n_bits == maxbits)
					maxcode = maxmaxcode;
				else
					maxcode = MAXCODE(n_bits);
			}
#ifdef DEBUG
			if (debug) {
				(void) fprintf(stderr,
					"\nChange to %d bits\n", n_bits);
				col = 0;
			}
#endif /* DEBUG */
		}
	} else {
		/*
		 * At EOF, write the rest of the buffer.
		 */
		if (offset > 0)
			(void) fwrite(buf, 1, (offset + 7) / 8, stdout);
		bytes_out += (offset + 7) / 8;
		offset = 0;
		(void) fflush(stdout);
#ifdef DEBUG
		if (verbose)
			(void) fprintf(stderr, "\n");
#endif /* DEBUG */
		if (ferror(stdout))
			writeerr();
	}
}

/*
 * Decompress stdin to stdout.  This routine adapts to the codes in the
 * file building the "string" table on-the-fly; requiring no table to
 * be stored in the compressed file.  The tables used herein are shared
 * with those of the compress() routine.  See the definitions above.
 */

static
void
decompress() {
	register char_type *stackp, *stack_lim;
	register int finchar;
	register code_int code, oldcode, incode;

	/*
	 * Validate zcat syntax
	 */
	if (zcat_cmd && (Fflg | Cflg | cflg |
		bflg | qflg | dflg | nomagic)) {
		(void) fprintf(stderr, gettext(
		    "Invalid Option\n"));
		Usage();
		exit(1);
	}
	/*
	 * As above, initialize the first 256 entries in the table.
	 */
	maxcode = MAXCODE(n_bits = INIT_BITS);
	for (code = 255; code >= 0; code--) {
		tab_prefixof(code) = 0;
		tab_suffixof(code) = (char_type)code;
	}
	free_ent = ((block_compress) ? FIRST : 256);

	finchar = oldcode = getcode();
	if (oldcode == -1)	/* EOF already? */
		return;			/* Get out of here */
	/* first code must be 8 bits = char */
	(void) putchar((char) finchar);
	if (ferror(stdout))		/* Crash if can't write */
		writeerr();
	stackp = de_stack;
	stack_lim = stack_max;

	while ((code = getcode()) > -1) {

		if ((code == CLEAR) && block_compress) {
			for (code = 255; code >= 0; code--)
			tab_prefixof(code) = 0;
			clear_flg = 1;
			free_ent = FIRST - 1;
			if ((code = getcode()) == -1)	/* O, untimely death! */
				break;
		}
		incode = code;
		/*
		 * Special case for KwKwK string.
		 */
		if (code >= free_ent) {
			if (stackp < stack_lim) {
				*stackp++ = (char_type) finchar;
				code = oldcode;
			} else
				oops();
		}

		/*
		 * Generate output characters in reverse order
		 */
		while (code >= 256) {
			if (stackp < stack_lim) {
				*stackp++ = tab_suffixof(code);
				code = tab_prefixof(code);
			} else
				oops();
		}
		*stackp++ = finchar = tab_suffixof(code);

		/*
		 * And put them out in forward order
		 */
		do
			(void) putchar (*--stackp);
		while (stackp > de_stack);

		/*
		 * Generate the new entry.
		 */
		if ((code = free_ent) < maxmaxcode) {
			tab_prefixof(code) = (unsigned short) oldcode;
			tab_suffixof(code) = (char_type) finchar;
			free_ent = code+1;
		}
		/*
		 * Remember previous code.
		 */
		oldcode = incode;
	}
	(void) fflush(stdout);
	if (ferror(stdout))
		writeerr();
}

/*
 * **************************************************************
 * TAG( getcode )
 *
 * Read one code from the standard input.  If EOF, return -1.
 * Inputs:
 * 	stdin
 * Outputs:
 * 	code or -1 is returned.
 */

code_int
getcode() {
	/*
	 * On the VAX, it is important to have the register declarations
	 * in exactly the order given, or the asm will break.
	 */
	register code_int code;
	static int offset = 0, size = 0;
	static char_type buf[BITS];
	register int r_off, bits;
	register char_type *bp = buf;

	if (clear_flg > 0 || offset >= size || free_ent > maxcode) {
		/*
		 * If the next entry will be too big for the current code
		 * size, then we must increase the size.  This implies reading
		 * a new buffer full, too.
		 */
		if (free_ent > maxcode) {
			n_bits++;
			if (n_bits == maxbits)
				/* won't get any bigger now */
				maxcode = maxmaxcode;
			else
				maxcode = MAXCODE(n_bits);
		}
		if (clear_flg > 0) {
			maxcode = MAXCODE(n_bits = INIT_BITS);
			clear_flg = 0;
		}
		size = fread(buf, 1, n_bits, stdin);
		if (size <= 0)
			return (-1);			/* end of file */
		offset = 0;
		/* Round size down to integral number of codes */
		size = (size << 3) - (n_bits - 1);
	}
	r_off = offset;
	bits = n_bits;
	/*
	 * Get to the first byte.
	 */
	bp += (r_off >> 3);
	r_off &= 7;
	/* Get first part (low order bits) */
	code = (*bp++ >> r_off);
	bits -= (8 - r_off);
	r_off = 8 - r_off;		/* now, offset into code word */
	/* Get any 8 bit parts in the middle (<=1 for up to 16 bits). */
	if (bits >= 8) {
		code |= *bp++ << r_off;
		r_off += 8;
		bits -= 8;
	}
	/* high order bits. */
	code |= (*bp & rmask[bits]) << r_off;
	offset += n_bits;

	return (code);
}

char *
rindex(s, c)		/* For those who don't have it in libc.a */
register char *s, c;
{
	char *p;
	for (p = NULL; *s; s++)
		if (*s == c)
			p = s;
	return (p);
}

#ifdef DEBUG
printcodes()
{
	/*
	 * Just print out codes from input file.  For debugging.
	 */
	code_int code;
	int col = 0, bits;

	bits = n_bits = INIT_BITS;
	maxcode = MAXCODE(n_bits);
	free_ent = ((block_compress) ? FIRST : 256);
	while ((code = getcode()) >= 0) {
		if ((code == CLEAR) && block_compress) {
			free_ent = FIRST - 1;
			clear_flg = 1;
		} else if (free_ent < maxmaxcode)
			free_ent++;
		if (bits != n_bits) {
			(void) fprintf(stderr, "\nChange to %d bits\n", n_bits);
			bits = n_bits;
			col = 0;
		}
		(void) fprintf(stderr, "%5d%c",
			code, (col += 6) >= 74 ? (col = 0, '\n') : ' ');
	}
	(void) putc('\n', stderr);
	exit(0);
}

code_int sorttab[1<<BITS];	/* sorted pointers into htab */

dump_tab()	/* dump string table */
{
	register int i, first;
	register ent;
#define	STACK_SIZE	15000
	int stack_top = STACK_SIZE;
	register c;

	if (do_decomp == 0) {	/* compressing */
		register int flag = 1;

		for (i = 0; i < hsize; i++) {	/* build sort pointers */
			if ((long)htabof(i) >= 0) {
				sorttab[codetabof(i)] = i;
			}
		}
		first = block_compress ? FIRST : 256;
		for (i = first; i < free_ent; i++) {
			(void) fprintf(stderr, "%5d: \"", i);
			de_stack[--stack_top] = '\n';
			de_stack[--stack_top] = '"';
			stack_top =
				in_stack((htabof(sorttab[i]) >> maxbits) & 0xff,
					stack_top);
			for (ent = htabof(sorttab[i]) & ((1 << maxbits) -1);
				ent > 256;
				ent = htabof(sorttab[ent]) & ((1<<maxbits)-1)) {
				stack_top = in_stack(
					htabof(sorttab[ent]) >> maxbits,
					stack_top);
			}
			stack_top = in_stack(ent, stack_top);
			(void) fwrite(&de_stack[stack_top], 1,
					STACK_SIZE - stack_top, stderr);
			stack_top = STACK_SIZE;
		}
	} else if (!debug) {	/* decompressing */

		for (i = 0; i < free_ent; i++) {
			ent = i;
			c = tab_suffixof(ent);
			if (isascii(c) && isprint(c))
				(void) fprintf(stderr, "%5d: %5d/'%c'  \"",
					ent, tab_prefixof(ent), c);
			else
				(void) fprintf(stderr, "%5d: %5d/\\%03o \"",
					ent, tab_prefixof(ent), c);
			de_stack[--stack_top] = '\n';
			de_stack[--stack_top] = '"';
			for (; ent != NULL;
				ent = (ent >= FIRST ? tab_prefixof(ent) :
						NULL)) {
				stack_top = in_stack(tab_suffixof(ent),
								stack_top);
			}
			(void) fwrite(&de_stack[stack_top], 1,
				STACK_SIZE - stack_top, stderr);
			stack_top = STACK_SIZE;
		}
	}
}

int
in_stack(c, stack_top)
	register c, stack_top;
{
	if ((isascii(c) && isprint(c) && c != '\\') || c == ' ') {
		de_stack[--stack_top] = c;
	} else {
		switch (c) {
		case '\n': de_stack[--stack_top] = 'n'; break;
		case '\t': de_stack[--stack_top] = 't'; break;
		case '\b': de_stack[--stack_top] = 'b'; break;
		case '\f': de_stack[--stack_top] = 'f'; break;
		case '\r': de_stack[--stack_top] = 'r'; break;
		case '\\': de_stack[--stack_top] = '\\'; break;
		default:
			de_stack[--stack_top] = '0' + c % 8;
			de_stack[--stack_top] = '0' + (c / 8) % 8;
			de_stack[--stack_top] = '0' + c / 64;
			break;
		}
		de_stack[--stack_top] = '\\';
	}
	return (stack_top);
}
#endif /* DEBUG */

static
void
writeerr()
{
	perror(ofname);
	(void) unlink(ofname);
	exit(1);
}

static
void
copystat(ifname, ofname)
char *ifname, *ofname;
{
	struct stat statbuf;
	mode_t mode;
	struct utimbuf timep;

	if (fclose(stdout)) {
		perror(ofname);
		if (!quiet)
			(void) fprintf(stderr, gettext(" -- file unchanged"));
		exit_stat = 1;
		perm_stat = 1;
	} else if (stat(ifname, &statbuf)) {	/* Get stat on input file */
		perror(ifname);
		return;
	} else if ((statbuf.st_mode &
			S_IFMT /* 0170000 */) != S_IFREG /* 0100000 */) {
		if (quiet)
			(void) fprintf(stderr, "%s: ", ifname);
		(void) fprintf(stderr, gettext(
			" -- not a regular file: unchanged"));
		exit_stat = 1;
		perm_stat = 1;
	} else if (statbuf.st_nlink > 1) {
		if (quiet)
			(void) fprintf(stderr, "%s: ", ifname);
		(void) fprintf(stderr, gettext(
			" -- has %d other links: unchanged"),
			statbuf.st_nlink - 1);
		exit_stat = 1;
		perm_stat = 1;
	} else if (exit_stat == 2 && (!force)) {
		/* No compression: remove file.Z */
		if (!quiet)
			(void) fprintf(stderr, gettext(" -- file unchanged"));
		} else {	/* ***** Successful Compression ***** */
			exit_stat = 0;
			mode = statbuf.st_mode & 07777;
			if (chmod(ofname, mode))		/* Copy modes */
				perror(ofname);
			/* Copy ownership */
			(void) chown(ofname, statbuf.st_uid, statbuf.st_gid);
			timep.actime = statbuf.st_atime;
			timep.modtime = statbuf.st_mtime;
			/* Update last accessed and modified times */
			(void) utime(ofname, &timep);
			if (unlink(ifname))	/* Remove input file */
				perror(ifname);
			if (!quiet)
				(void) fprintf(stderr, gettext(
				    " -- replaced with %s"), ofname);
			return;		/* Successful return */
		}

		/* Unsuccessful return -- one of the tests failed */
		if (unlink(ofname))
			perror(ofname);
	}

static
void
onintr()
{
	if (!precious)
		(void) unlink(ofname);
	exit(1);
}

static
void
oops()	/* wild pointer -- assume bad input */
{
	if (do_decomp)
		(void) fprintf(stderr, gettext("uncompress: corrupt input\n"));
	(void) unlink(ofname);
	exit(1);
}

static
void
cl_block()		/* table clear for block compress */
{
	register long int rat;

	checkpoint = in_count + CHECK_GAP;
#ifdef DEBUG
	if (debug) {
		(void) fprintf(stderr, "count: %ld, ratio: ", in_count);
		prratio(stderr, in_count, bytes_out);
		(void) fprintf(stderr, "\n");
	}
#endif /* DEBUG */

	if (in_count > 0x007fffff) {	/* shift will overflow */
		rat = bytes_out >> 8;
		if (rat == 0) {		/* Don't divide by zero */
			rat = 0x7fffffff;
		} else {
			rat = in_count / rat;
		}
	} else {
		rat = (in_count << 8) / bytes_out;	/* 8 fractional bits */
	}
	if (rat > ratio) {
		ratio = rat;
	} else {
		ratio = 0;
#ifdef DEBUG
		if (verbose)
			dump_tab();	/* dump string table */
#endif
		cl_hash((count_int) hsize);
		free_ent = FIRST;
		clear_flg = 1;
		output((code_int) CLEAR);
#ifdef DEBUG
		if (debug)
			(void) fprintf(stderr, "clear\n");
#endif /* DEBUG */
	}
}

static
void
cl_hash(hsize)		/* reset code table */
	register count_int hsize;
{
	register count_int *htab_p = htab+hsize;
	register long i;
	register long m1 = -1;

	i = hsize - 16;
	do {				/* might use Sys V memset(3) here */
		*(htab_p-16) = m1;
		*(htab_p-15) = m1;
		*(htab_p-14) = m1;
		*(htab_p-13) = m1;
		*(htab_p-12) = m1;
		*(htab_p-11) = m1;
		*(htab_p-10) = m1;
		*(htab_p-9) = m1;
		*(htab_p-8) = m1;
		*(htab_p-7) = m1;
		*(htab_p-6) = m1;
		*(htab_p-5) = m1;
		*(htab_p-4) = m1;
		*(htab_p-3) = m1;
		*(htab_p-2) = m1;
		*(htab_p-1) = m1;
		htab_p -= 16;
	} while ((i -= 16) >= 0);
		for (i += 16; i > 0; i--)
			*--htab_p = m1;
}

static
void
prratio(stream, num, den)
FILE *stream;
long int num, den;
{
	register int q;			/* Doesn't need to be long */

	if ((num > 214748L) || (num < -214748L)) {	/* 2147483647/10000 */
		q = num / (den / 10000L);
	} else {
		q = 10000L * num / den;		/* Long calculations, though */
	}
	if (q < 0) {
		(void) putc('-', stream);
		q = -q;
	}
	(void) fprintf(stream, "%d%s%02d%%", q / 100,
			localeconv()->decimal_point, q % 100);
}

static
void
version()
{
	(void) fprintf(stderr, "%s, Berkeley 5.9 5/11/86\n", rcs_ident);
	(void) fprintf(stderr, "Options: ");
#ifdef DEBUG
	(void) fprintf(stderr, "DEBUG, ");
#endif
	(void) fprintf(stderr, "BITS = %d\n", BITS);
}

static
void
Usage()
{
#ifdef DEBUG
	(void) fprintf(stderr,
	"Usage: compress [-dDVfc] [-b maxbits] [file ...]\n");
#else
	if (strcmp(progname, "compress") == 0) {
		(void) fprintf(stderr,
		    gettext(
		    "Usage: compress [-fv] [-b maxbits] [file ...]\n"\
		    "       compress [-cfv] [-b maxbits] [file]\n"));
	} else if (strcmp(progname, "uncompress") == 0)
		(void) fprintf(stderr, gettext(
		    "Usage: uncompress [-cfv] [file ...]\n"));
	else if (strcmp(progname, "zcat") == 0)
		(void) fprintf(stderr, gettext("Usage: zcat [file ...]\n"));

#endif /* DEBUG */
}

static
char *
basename(path)
char *path;
{
	char *p;
	char *ret = (char *) path;

	while ((p = (char *) strpbrk(ret, "/")) != NULL)
		ret = p + 1;
	return (ret);
}
