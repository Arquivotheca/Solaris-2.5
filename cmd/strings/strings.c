/*
 *	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
 *	All Rights Reserved
 */

/*
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 */

#ident	"@(#)strings.c	1.9	95/02/24 SMI"	/* SVr4.0 1.3	*/
/*
 *	Copyright (c) 1987, 1988 Microsoft Corporation
 *	All Rights Reserved
 */

/*
 *	This Module contains Proprietary Information of Microsoft
 *	Corporation and should be treated as Confidential.
 */


/*
 *	@(#) strings.c 1.3 88/05/09 strings:strings.c
 */

/*
 *	Copyright (c) 1979 Regents of the University of California
 */


#include <stdio.h>
#include "x.out.h"
#include <ctype.h>
#include <libelf.h>
#include <sys/elf.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>


#define	NOTOUT		0
#define	AOUT		1
#define	BOUT		2
#define	XOUT		3
#define	ELF		4

#define	dirt(c) ((!isascii(c) || !isprint(c)) && (isClocale || isnssx(c)))


/* struct	exec header */
static union uexec {
	struct xexec	u_xhdr;	/* x.out */
	struct aexec	u_ahdr;	/* a.out */
	struct bexec	u_bhdr;	/* b.out */
} header;


/*
 * function prototypes
 */
static void	Usage();
static void	find(long);
static int	ismagic(int, union uexec *, FILE *);
static int	tryelf(FILE *);
static int	isnssx(int);


/*
 * Strings - extract strings from an object file for whatever
 *
 * The algorithm is to look for sequences of "non-junk" characters
 * The variable "minlen" is the minimum length string printed.
 * This helps get rid of garbage.
 * Default minimum string length is 4 characters.
 *
 */

static	struct xexec	*xhdrp	= &(header.u_xhdr);
static	struct aexec	*ahdrp	= &(header.u_ahdr);
static	struct bexec	*bhdrp	= &(header.u_bhdr);

#define	DEF_MIN_STRING	4

static	int	tflg;
static	char	t_format;
static	int	aflg;
static	int	minlength = 0;
static	int	isClocale;


main(argc, argv)
	int argc;
	char *argv[];
{
	int		hsize;
	int		htype;
	int		fd;
	Elf		*elf;
	Elf32_Ehdr	*ehdr;
	Elf_Scn		*scn;
	Elf32_Shdr	*shdr;
	char		*scn_name;
	char		*locale;
	int		opt;
	int		i;


	(void) setlocale(LC_ALL, "");

#if	!defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	locale = setlocale(LC_CTYPE, NULL);
	isClocale = (strcmp(locale, "C") == 0) ||
		(strcmp(locale, "POSIX") == 0) ||
		(strcmp(locale, "en_US") == 0);

	/* check for non-standard "-" option */
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-") == 0) {
			aflg++;
			while (i < argc) {
				argv[i] = argv[i+1];
				i++;
			}
			argc--;
		}
	}

	/* get options */
	while ((opt = getopt(argc, argv, "1234567890an:ot:")) != -1) {
		switch (opt) {
			case 'a':
				aflg++;
				break;

			case 'n':
				minlength = (int) strtol(optarg, (char **)NULL,
				    10);
				break;

			case 'o':
				tflg++;
				t_format = 'd';
				break;

			case 't':
				tflg++;
				t_format = *optarg;
				if (t_format != 'd' && t_format != 'o' &&
				    t_format != 'x')
				{
					(void) fprintf(stderr,
					gettext("Invalid format\n"));
					Usage();
				}
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				minlength *= 10;
				minlength += opt - '0';
				break;

			default:
				Usage();
		}
	}

	/* if min string not specified, use default */
	if (!minlength)
		minlength = DEF_MIN_STRING;

	/* for each file operand */
	do {
		if (argv[optind] != NULL) {
			if (freopen(argv[optind], "r", stdin) == NULL) {
				perror(argv[optind]);
				exit(1);
			}
			optind++;
		} else
			aflg++;

		if (aflg)
			htype =  NOTOUT;
		else {
			hsize = fread((char *) &header, sizeof (char),
					sizeof (header), stdin);
			htype = ismagic(hsize, &header, stdin);
		}
		switch (htype) {
			case AOUT:
				(void) fseek(stdin, (long) ADATAPOS(ahdrp), 0);
				find((long) ahdrp->xa_data);
				continue;

			case BOUT:
				(void) fseek(stdin, (long) BDATAPOS(bhdrp), 0);
				find((long) bhdrp->xb_data);
				continue;

			case XOUT:
				(void) fseek(stdin, (long) XDATAPOS(xhdrp), 0);
				find((long) xhdrp->x_data);
				continue;

			case ELF:
			/*
			 * Will take care of COFF M32 and i386 also
			 * As well as ELF M32, i386 and Sparc
			 */

				fd = fileno(stdin);
				(void) lseek(fd, 0L, 0);
				elf = elf_begin(fd, ELF_C_READ, NULL);
				ehdr = elf32_getehdr(elf);
				scn = 0;
				while ((scn = elf_nextscn(elf, scn)) != 0)
				{
					if ((shdr = elf32_getshdr(scn)) != 0)
						scn_name = elf_strptr(elf,
						    ehdr->e_shstrndx,
						    (size_t)shdr->sh_name);
					/*
					 * There is more than one .data section
					 */

					if ((strcmp(scn_name, ".rodata")
					    == 0) ||
					    (strcmp(scn_name, ".rodata1")
					    == 0) ||
					    (strcmp(scn_name, ".data")
					    == 0) ||
					    (strcmp(scn_name, ".data1")
					    == 0))
					{
						(void) fseek(stdin,
						    (long) shdr->sh_offset, 0);
						find((long) shdr->sh_size);
					}
				}
				continue;

			case NOTOUT:
			default:
				(void) fseek(stdin, (long) 0, 0);
				find((long) 100000000L);
				continue;
		}
	} while (argv[optind] != NULL);

	return (0);
}


static void
find(cnt)
	long cnt;
{
	static	char buf[BUFSIZ];
	int	c;
	int	cc;

	cc = 0;
	for (c = ~EOF; (cnt > 0) && (c != EOF); cnt--) {
		c = getc(stdin);
		if (dirt(c)) {
			if (cc >= minlength) {
				if (tflg) {
					switch (t_format) {
					case 'd':
						(void) printf("%7ld ",
						    ftell(stdin) - cc - 1);
						break;

					case 'o':
						(void) printf("%7lo ",
						    ftell(stdin) - cc - 1);
						break;

					case 'x':
						(void) printf("%7lx ",
						    ftell(stdin) - cc - 1);
						break;
					}
				}

				buf[cc] = '\0';
				(void) puts(buf);
			}
			cc = 0;
		} else if (cc < (BUFSIZ - 2))
		    buf[cc++] = (char) c;
	}
}


static int
ismagic(hsize, hdr, fp)
	int hsize;
	union uexec *hdr;
	FILE *fp;
{
	switch ((int) (hdr->u_bhdr.xb_magic)) {
		case A_MAGIC1:
		case A_MAGIC2:
		case A_MAGIC3:
		case A_MAGIC4:
			if (hsize < sizeof (struct bexec))
				return (NOTOUT);
			else
				return (BOUT);
		default:
			break;
	}
	switch (hdr->u_xhdr.x_magic) {
		case X_MAGIC:
			if (hsize < sizeof (struct xexec))
				return (NOTOUT);
			else
				return (XOUT);
		default:
			break;
	}
	switch (hdr->u_ahdr.xa_magic) {
		case A_MAGIC1:
		case A_MAGIC2:
		case A_MAGIC3:
		case A_MAGIC4:
			if (hsize < sizeof (struct aexec))
				return (NOTOUT);
			else
				return (AOUT);
		default:
			break;
	}
	return (tryelf(fp));
}


static int
tryelf(fp)
FILE *fp;
{
	int fd;
	Elf *elf;
	Elf32_Ehdr *ehdr;

	fd = fileno(fp);

	if ((elf_version(EV_CURRENT)) == EV_NONE) {
		(void) fprintf(stderr, "%s\n", gettext(elf_errmsg(-1)));
		return (NOTOUT);
	}

	(void) lseek(fd, 0L, 0);

	if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		(void) fprintf(stderr, "%s\n", gettext(elf_errmsg(-1)));
		return (NOTOUT);
	}

	if ((elf_kind(elf)) == ELF_K_NONE) {
		(void) elf_end(elf);
		return (NOTOUT);
	}

	if ((ehdr = elf32_getehdr(elf)) == NULL) {
		(void) fprintf(stderr, "%s\n", gettext(elf_errmsg(-1)));
		(void) elf_end(elf);
		return (NOTOUT);
	}

	if ((ehdr->e_type == ET_CORE) || (ehdr->e_type == ET_NONE)) {
		(void) elf_end(elf);
		return (NOTOUT);
	}

	(void) elf_end(elf);

	return (ELF);

}


static int
isnssx(c)
	int c;
{
	switch (c) {
		case 0216:	/* SS2 */
		case 0217:	/* SS3 */
			return (0);
		case 0240:
		case 0377:
			return (1);
		default:
			return (c < ' ' || (c >= 0200 && c <= 0237));
	}
}


static void
Usage()
{
	(void) fprintf(stderr, gettext(
	    "Usage: strings [-|-a] [-t format] [-n #] [file ...]\n"
	    "       strings [-|-a] [-o] [-#] [file ...]\n"));
	exit(1);
}
