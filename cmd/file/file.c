/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)file.c	1.26	95/07/13 SMI"	/* SVr4.0 1.17.1.15	*/

#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <libelf.h>
#include <stdlib.h>
#include <limits.h>
#include <locale.h>
#include <wctype.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/mkdev.h>
#include <sys/stat.h>
#include <sys/elf.h>
#include <sys/elf_M32.h>
#include <sys/elf_SPARC.h>
#include <sys/procfs.h>
#include <sys/core.h>

/*
 *	Misc
 */

#define	FBSZ	512

/* Assembly language comment char */
#ifdef pdp11
#define	ASCOMCHAR '/'
#else
#define	ASCOMCHAR '#'
#endif

static char	fbuf[FBSZ];
static char	*mfile = NULL;
static char	*troff[] = {	/* new troff intermediate lang */
		"x", "T", "res", "init", "font", "202", "V0", "p1", 0};

static char	*fort[] = {			/* FORTRAN */
		"function", "subroutine", "common", "dimension", "block",
		"integer", "real", "data", "double",
		"FUNCTION", "SUBROUTINE", "COMMON", "DIMENSION", "BLOCK",
		"INTEGER", "REAL", "DATA", "DOUBLE", 0};

static char	*asc[] = {
		"sys", "mov", "tst", "clr", "jmp", 0};

static char	*c[] = {			/* C Language */
		"int", "char", "float", "double", "short", "long", "unsigned",
		"register", "static", "struct", "extern", 0};

static char	*as[] = {			/* Assembly Language */
		"globl", "byte", "even", "text", "data", "bss", "comm", 0};

/* start for MB env */
static wchar_t wchar;
static int	length;
static int	IS_ascii;
static int	Max;
/* end for MB env */
static int	i = 0;
static int	fbsz;
static int	ifd = -1;
static int	elffd = -1;
static int	tret;
static int	hflg = 0;
extern int errno;

static void is_stripped(Elf *elf);
static Elf *is_elf_file(int elffd);
static void ar_coff_or_aout(int ifd);
static int type(char *file);
static int troffint(char *bp, int n);
static int lookup(char **tab);
static int ccom(void);
static int ascom(void);
static int sccs(void);
static int english(char *bp, int n);
static int core(Elf *elf, Elf32_Ehdr *ehdr, int format);
static int shellscript(char buf[], struct stat *sb);
static int elf_check(Elf *elf);

extern int mkmtab(char *magfile, int cflg);
extern int ckmtab(char *buf, int bufsize, int silent);
extern void prtmtab(void);

#define	prf(x)	printf("%s:%s", x, (int)strlen(x) > 6 ? "\t" : "\t\t");

int
main(int argc, char **argv)
{
	register char	*p;
	register int	ch;
	register FILE	*fl;
	register int	cflg = 0, eflg = 0, fflg = 0;
	char	*ap = NULL;
	int	pathlen;
	extern	int	optind;
	extern	char	*optarg;
	struct stat	statbuf;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	while ((ch = getopt(argc, argv, "chf:m:")) != EOF) {
		switch (ch) {
		case 'c':
			cflg++;
			break;

		case 'f':
			fflg++;
			if ((fl = fopen(optarg, "r")) == NULL) {
				fprintf(stderr,
					gettext("cannot open %s\n"), optarg);
				usage();
			}
			pathlen = pathconf("/", _PC_PATH_MAX);
			if (pathlen == -1) {
				fprintf(stderr,
				    gettext("pathconf: cannot determine "
					"maximum path length\n"));
				exit(1);
			}
			pathlen += 2; /* for null and newline in fgets */
			ap = malloc(pathlen * sizeof (char));
			if (ap == NULL) {
				perror("malloc");
				exit(1);
			}
			break;

		case 'm':
			mfile = optarg;
			break;

		case 'h':
			hflg++;
			break;

		case '?':
			eflg++;
			break;
		}
	}
	if (!cflg && !fflg && (eflg || optind == argc))
		usage();

	if (mfile == NULL) { /* -m is not used */
		const char *s1 = "/usr/lib/locale/";
		const char *msg_locale = setlocale(LC_MESSAGES, NULL);
		const char *s3 = "/LC_MESSAGES/magic";

		mfile = malloc(strlen(s1)+strlen(msg_locale)+strlen(s3)+1);
		(void) strcpy(mfile, s1);
		(void) strcat(mfile, msg_locale);
		(void) strcat(mfile, s3);
		if (stat(mfile, &statbuf) != 0)
			(void) strcpy(mfile, "/etc/magic"); /* use /etc/magic */
	}

	if (mkmtab(mfile, cflg) == -1)
		exit(2);
	if (cflg) {
		prtmtab();
		exit(0);
	}
	for (; fflg || optind < argc; optind += !fflg) {
		register int	l;

		if (fflg) {
			if ((p = fgets(ap, pathlen, fl)) == NULL) {
				fflg = 0;
				optind--;
				continue;
			}
			l = strlen(p);
			if (l > 0)
				p[l - 1] = '\0';
		} else
			p = argv[optind];
		prf(p);				/* print "file_name:<tab>" */

		if (type(p))
			tret = 1;
	}
	if (ap != NULL)
		free(ap);
	if (tret != 0) {
		exit(tret);
	}
	return (0);
}

static int
type(char *file)
{
	int	j, nl;
	int	cc;
	char	ch;
	char	buf[BUFSIZ];
	struct	stat	mbuf;
	int	(*statf)() = hflg ? lstat : stat;
	int	i = 0;
	Elf	*elf;

	ifd = -1;
	if ((*statf)(file, &mbuf) < 0) {
		if (statf == lstat || lstat(file, &mbuf) < 0) {
			printf(gettext("cannot open: %s\n"), strerror(errno));
			return (0);		/* POSIX.2 */
		}
	}
	switch (mbuf.st_mode & S_IFMT) {
	case S_IFCHR:
		printf(gettext("character"));
		goto spcl;

	case S_IFDIR:
		printf(gettext("directory\n"));
		return (0);

	case S_IFIFO:
		printf(gettext("fifo\n"));
		return (0);

	case S_IFNAM:
		switch (mbuf.st_rdev) {
		case S_INSEM:
			printf(gettext("Xenix semaphore\n"));
			return (0);
		case S_INSHD:
			printf(gettext("Xenix shared memory handle\n"));
			return (0);
		default:
			printf(gettext("unknown Xenix name "
			    "special file\n"));
			return (0);
		}

	case S_IFLNK:
		if ((cc = readlink(file, buf, BUFSIZ)) < 0) {
			printf(gettext("readlink error: %s\n"),
				strerror(errno));
			return (1);
		}
		buf[cc] = '\0';
		printf(gettext("symbolic link to %s\n"), buf);
		return (0);

	case S_IFBLK:
		printf(gettext("block"));
					/* major and minor, see sys/mkdev.h */
spcl:
		printf(gettext(" special (%d/%d)\n"),
		    major(mbuf.st_rdev), minor(mbuf.st_rdev));
		return (0);
	}

	if (elf_version(EV_CURRENT) == EV_NONE) {
		printf(gettext("libelf is out of date\n"));
		return (1);
	}

	ifd = open(file, O_RDONLY);
	if (ifd < 0) {
		printf(gettext("cannot open: %s\n"), strerror(errno));
		return (0);			/* POSIX.2 */
	}
	/* need another fd for elf, since we might want to read the file too */
	elffd = open(file, O_RDONLY);
	if (elffd < 0) {
		printf(gettext("cannot open: %s\n"), strerror(errno));
		(void) close(ifd);
		ifd = -1;
		return (0);			/* POSIX.2 */
	}

	if ((fbsz = read(ifd, fbuf, FBSZ)) == -1) {
		printf(gettext("cannot read: %s\n"), strerror(errno));
		(void) close(ifd);
		ifd = -1;
		(void) close(elffd);
		elffd = -1;
		return (0);			/* POSIX.2 */
	}
	if (fbsz == 0) {
		printf(gettext("empty file\n"));
		goto out;
	}
	if (sccs()) {	/* look for "1hddddd" where d is a digit */
		printf("sccs \n");
		goto out;
	}
	if (fbuf[0] == '#' && fbuf[1] == '!' && shellscript(fbuf+2, &mbuf))
		goto out;

	if ((elf = is_elf_file(elffd)) != NULL) {
		(void) elf_check(elf);
		(void) elf_end(elf);
		(void) putchar('\n');
		goto out;
	} else if (*(int *)fbuf == CORE_MAGIC) {
		printf("a.out core file");
		if (*((struct core *)fbuf)->c_cmdname != '\0')
			printf(" from '%s'", ((struct core *)fbuf)->c_cmdname);
		(void) putchar('\n');
		goto out;
	}

	switch (ckmtab(fbuf, fbsz, 0)) { /* ChecK against Magic Table entries */
		case -1:	/* Error */
			exit(2);
			break;
		case 0:		/* Not magic */
			break;
		default:	/* Switch is magic index */

			/*
			 * ckmtab recognizes file type,
			 * check if it is PostScript.
			 * if not, check if elf or a.out
			 */
			if (fbuf[0] == '%' && fbuf[1] == '!') {
				(void) putchar('\n');
				goto out;
			} else {

				/*
				 * Check that the file is executable (dynamic
				 * objects must be executable to be exec'ed,
				 * shared objects need not be, but by convention
				 * should be executable).
				 *
				 * Note that we should already have processed
				 * the file if it was an ELF file.
				 */
				ar_coff_or_aout(elffd);
				(void) putchar('\n');
				goto out;
			}
	}
	if (ccom() == 0)
		goto notc;
	while (fbuf[i] == '#') {
		j = i;
		while (fbuf[i++] != '\n') {
			if (i - j > 255) {
				printf(gettext("data\n"));
				goto out;
			}
			if (i >= fbsz)
				goto notc;
		}
		if (ccom() == 0)
			goto notc;
	}
check:
	if (lookup(c) == 1) {
		while ((ch = fbuf[i++]) != ';' && ch != '{')
			if (i >= fbsz)
				goto notc;
		printf(gettext("c program text"));
		goto outa;
	}
	nl = 0;
	while (fbuf[i] != '(') {
		if (fbuf[i] <= 0)
			goto notas;
		if (fbuf[i] == ';') {
			i++;
			goto check;
		}
		if (fbuf[i++] == '\n')
			if (nl++ > 6)
				goto notc;
		if (i >= fbsz)
			goto notc;
	}
	while (fbuf[i] != ')') {
		if (fbuf[i++] == '\n')
			if (nl++ > 6)
				goto notc;
		if (i >= fbsz)
			goto notc;
	}
	while (fbuf[i] != '{') {
		if (fbuf[i++] == '\n')
			if (nl++ > 6)
				goto notc;
		if (i >= fbsz)
			goto notc;
	}
	printf(gettext("c program text"));
	goto outa;
notc:
	i = 0;
	while (fbuf[i] == 'c' || fbuf[i] == '#') {
		while (fbuf[i++] != '\n')
			if (i >= fbsz)
				goto notfort;
	}
	if (lookup(fort) == 1) {
		printf(gettext("fortran program text"));
		goto outa;
	}
notfort:
	i = 0;
	if (ascom() == 0)
		goto notas;
	j = i - 1;
	if (fbuf[i] == '.') {
		i++;
		if (lookup(as) == 1) {
			printf(gettext("assembler program text"));
			goto outa;
		} else if (j != -1 && fbuf[j] == '\n' && isalpha(fbuf[j + 2])) {
			printf(gettext("[nt]roff, tbl, or eqn input text"));
			goto outa;
		}
	}
	while (lookup(asc) == 0) {
		if (ascom() == 0)
			goto notas;
		while (fbuf[i] != '\n' && fbuf[i++] != ':')
			if (i >= fbsz)
				goto notas;
		while (fbuf[i] == '\n' || fbuf[i] == ' ' || fbuf[i] == '\t')
			if (i++ >= fbsz)
				goto notas;
		j = i - 1;
		if (fbuf[i] == '.') {
			i++;
			if (lookup(as) == 1) {
				printf(gettext("assembler program text"));
				goto outa;
			} else if (fbuf[j] == '\n' && isalpha(fbuf[j+2])) {
				printf(gettext("[nt]roff, tbl, or eqn input "
				    "text"));
				goto outa;
			}
		}
	}
	printf(gettext("assembler program text"));
	goto outa;
notas:
	/* start modification for multibyte env */
	IS_ascii = 1;
	if (fbsz < FBSZ)
		Max = fbsz;
	else
		Max = FBSZ - MB_LEN_MAX; /* prevent cut of wchar read */
	/* end modification for multibyte env */

	for (i = 0; i < Max; /* null */)
		if (fbuf[i] & 0200) {
			IS_ascii = 0;
			if (fbuf[0] == '\100' && fbuf[1] == '\357') {
				printf(gettext("troff output\n"));
				goto out;
			}
		/* start modification for multibyte env */
			if ((length = mbtowc(&wchar, &fbuf[i], MB_LEN_MAX))
			    <= 0 || !iswprint(wchar)) {
				printf(gettext("data\n"));
				goto out;
			}
			i += length;
		}
		else
			i++;
	i = fbsz;
		/* end modification for multibyte env */
	if (mbuf.st_mode&(S_IXUSR|S_IXGRP|S_IXOTH))
		printf(gettext("commands text"));
	else if (troffint(fbuf, fbsz))
		printf(gettext("troff intermediate output text"));
	else if (english(fbuf, fbsz))
		printf(gettext("English text"));
	else if (IS_ascii)
		printf(gettext("ascii text"));
	else
		printf(gettext("text")); /* for multibyte env */
outa:
	/*
	 * This code is to make sure that no MB char is cut in half
	 * while still being used.
	 */
	fbsz = (fbsz < FBSZ ? fbsz : fbsz - MB_CUR_MAX + 1);
	while (i < fbsz) {
		if (isascii(fbuf[i])) {
			i++;
			continue;
		} else {
			if ((length = mbtowc(&wchar, &fbuf[i], MB_LEN_MAX))
			    <= 0 || !iswprint(wchar)) {
				printf(gettext(" with garbage\n"));
				goto out;
			}
			i = i + length;
		}
	}
	printf("\n");
out:
	if (ifd != -1) {
		(void) close(ifd);
		ifd = -1;
	}
	if (elffd != -1) {
		(void) close(elffd);
		elffd = -1;
	}
	return (0);
}

static int
troffint(char *bp, int n)
{
	int k;

	i = 0;
	for (k = 0; k < 6; k++) {
		if (lookup(troff) == 0)
			return (0);
		if (lookup(troff) == 0)
			return (0);
		while (i < n && bp[i] != '\n')
			i++;
		if (i++ >= n)
			return (0);
	}
	return (1);
}

/*
 * Determine if the passed descriptor describes an ELF file.
 * If so, return the Elf handle.
 */
static Elf *
is_elf_file(int elffd)
{
	Elf *elf;

	elf = elf_begin(elffd, ELF_C_READ, (Elf *)0);
	switch (elf_kind(elf)) {
	case ELF_K_ELF:
		break;
	default:
		(void) elf_end(elf);
		elf = NULL;
		break;
	}
	return (elf);
}

static void
ar_coff_or_aout(int elffd)
{
	Elf *elf;

	/*
	 * Get the files elf descriptor and process it as an elf or
	 * a.out (4.x) file.
	 */

	elf = elf_begin(elffd, ELF_C_READ, (Elf *)0);
	switch (elf_kind(elf)) {
		case ELF_K_AR :
			(void) printf(gettext(", not a dynamic executable "
			    "or shared object"));
			break;
		case ELF_K_COFF:
			(void) printf(gettext(", unsupported or unknown "
			    "file type"));
			break;
		default:
			/*
			 * This is either an unknown file or an aout format
			 * At this time, we don't print dynamic/stripped
			 * info. on a.out or non-Elf binaries.
			 */
			break;
	}
	(void) elf_end(elf);
}


static void
print_elf_type(int type)
{
	switch (type) {
	case ET_NONE:
		printf(" %s", gettext("unknown type"));
		break;
	case ET_REL:
		printf(" %s", gettext("relocatable"));
		break;
	case ET_EXEC:
		printf(" %s", gettext("executable"));
		break;
	case ET_DYN:
		printf(" %s", gettext("dynamic lib"));
		break;
	case ET_CORE:
		printf(" %s", gettext("core file"));
		break;
	default:
		break;
	}
}

static void
print_elf_machine(int machine)
{
	switch (machine) {
	case EM_NONE:
		printf(" %s", gettext("unknown machine"));
		break;
	case EM_M32:
		printf(" %s", gettext("WE32100"));
		break;
	case EM_SPARC:
		printf(" %s", gettext("SPARC"));
		break;
	case EM_386:
		printf(" %s", gettext("80386"));
		break;
	case EM_68K:
		printf(" %s", gettext("M68000"));
		break;
	case EM_88K:
		printf(" %s", gettext("M88000"));
		break;
	case EM_486:
		printf(" %s", gettext("80486"));
		break;
	case EM_860:
		printf(" %s", gettext("i860"));
		break;
	case EM_MIPS:
		printf(" %s", gettext("MIPS RS3000 Big-Endian"));
		break;
	case EM_MIPS_RS3_LE:
		printf(" %s", gettext("MIPS RS3000 Little-Endian"));
		break;
	case EM_RS6000:
		printf(" %s", gettext("MIPS RS6000"));
		break;
	case EM_PA_RISC:
		printf(" %s", gettext("PA-RISC"));
		break;
	case EM_nCUBE:
		printf(" %s", gettext("nCUBE"));
		break;
	case EM_VPP500:
		printf(" %s", gettext("VPP500"));
		break;
	case EM_SPARC32PLUS:
		printf(" %s", gettext("SPARC32PLUS"));
		break;
	default:
		break;
	}
}

static void
print_elf_datatype(int datatype)
{
	switch (datatype) {
	case ELFDATA2LSB:
		printf(" %s", gettext("LSB"));
		break;
	case ELFDATA2MSB:
		printf(" %s", gettext("MSB"));
		break;
	default:
		break;
	}
}

static void
print_elf_class(int class)
{
	switch (class) {
	case ELFCLASS32:
		printf(" %s", gettext("32-bit"));
		break;
	case ELFCLASS64:
		printf(" %s", gettext("64-bit"));
		break;
	default:
		break;
	}
}

static void
print_elf_flags(int machine, unsigned int flags)
{
	switch (machine) {
	case EM_M32:
		if (flags & EF_M32_MAU)
			printf("%s", gettext(", MAU Required"));
		break;
	case EM_SPARC32PLUS:
		if (flags & EF_SPARC_32PLUS)
			printf("%s", gettext(", V8+ Required"));
		if (flags & EF_SPARC_SUN_US1) {
			printf("%s",
				gettext(", UltraSPARC1 Extensions Required"));
		}
		if (flags & EF_SPARC_HAL_R1)
			printf("%s", gettext(", HaL R1 Extensions Required"));
		break;
	default:
		break;
	}
}

static int
elf_check(Elf *elf)
{
	Elf32_Ehdr	*ehdr;
	Elf32_Phdr	*phdr;
	int		dynamic, cnt;
	char	*ident;
	size_t	size;

	/*
	 * verify information in file header
	 */
	if ((ehdr = elf32_getehdr(elf)) == (Elf32_Ehdr *)0) {
		(void) fprintf(stderr, gettext("can't read ELF header\n"));
		return (1);
	}
	printf("%s", gettext("ELF"));
	ident = elf_getident(elf, &size);
	print_elf_class(ident[EI_CLASS]);
	print_elf_datatype(ident[EI_DATA]);
	print_elf_type(ehdr->e_type);
	print_elf_machine(ehdr->e_machine);
	if (ehdr->e_version == 1)
		printf(" %s %d", gettext("Version"), (int)ehdr->e_version);
	print_elf_flags(ehdr->e_machine, ehdr->e_flags);

	if (core(elf, ehdr, ident[EI_DATA]))	/* check for core file */
		return (0);

	/*
	 * check type
	 */
	if ((ehdr->e_type != ET_EXEC) && (ehdr->e_type != ET_DYN))
		return (1);

	/*
	 * read program header and check for dynamic section
	 */
	if ((phdr = elf32_getphdr(elf)) == (Elf32_Phdr *)0) {
		fprintf(stderr, gettext("can't read program header\n"));
		return (1);
	}

	for (dynamic = 0, cnt = 0; cnt < (int)ehdr->e_phnum; cnt++) {
		if (phdr->p_type == PT_DYNAMIC) {
			dynamic = 1;
			break;
		}
		phdr = (Elf32_Phdr *)((unsigned long)phdr + ehdr->e_phentsize);
	}
	if (dynamic)
		printf(gettext(", dynamically linked"));
	else
		printf(gettext(", statically linked"));

	is_stripped(elf);
	return (0);
}

/*
 * is_stripped prints information on whether the executable has
 * been stripped.
 */
static void
is_stripped(Elf *elf)
{
	Elf32_Shdr	*shdr;
	int		flag;
	Elf_Scn		*scn, *nextscn;


	/*
		If the Symbol Table exists, the executable file has not
		been stripped.
	*/
	flag = 0;
	scn = NULL;
	while ((nextscn = elf_nextscn(elf, scn)) != NULL) {
		scn = nextscn;
		shdr = elf32_getshdr(scn);
		if (shdr->sh_type == SHT_SYMTAB) {
			flag = 1;
			break;
		}
	}

	if (flag)
		printf(gettext(", not stripped"));
	else
		printf(gettext(", stripped"));
}

static int
lookup(char **tab)
{
	register char	r;
	register int	k, j, l;

	while (fbuf[i] == ' ' || fbuf[i] == '\t' || fbuf[i] == '\n')
		i++;
	for (j = 0; tab[j] != 0; j++) {
		l = 0;
		for (k = i; ((r = tab[j][l++]) == fbuf[k] && r != '\0'); k++);
		if (r == '\0')
			if (fbuf[k] == ' ' || fbuf[k] == '\n' ||
			    fbuf[k] == '\t' || fbuf[k] == '{' ||
			    fbuf[k] == '/') {
				i = k;
				return (1);
			}
	}
	return (0);
}

static int
ccom(void)
{
	register char	cc;

	while ((cc = fbuf[i]) == ' ' || cc == '\t' || cc == '\n')
		if (i++ >= fbsz)
			return (0);
	if (fbuf[i] == '/' && fbuf[i+1] == '*') {
		i += 2;
		while (fbuf[i] != '*' || fbuf[i+1] != '/') {
			if (fbuf[i] == '\\')
				i += 2;
			else
				i++;
			if (i >= fbsz)
				return (0);
		}
		if ((i += 2) >= fbsz)
			return (0);
	}
	if (fbuf[i] == '\n')
		if (ccom() == 0)
			return (0);
	return (1);
}

static int
ascom(void)
{
	while (fbuf[i] == ASCOMCHAR) {
		i++;
		while (fbuf[i++] != '\n')
			if (i >= fbsz)
				return (0);
		while (fbuf[i] == '\n')
			if (i++ >= fbsz)
				return (0);
	}
	return (1);
}

static int
sccs(void)
{				/* look for "1hddddd" where d is a digit */
	register int j;

	if (fbuf[0] == 1 && fbuf[1] == 'h') {
		for (j = 2; j <= 6; j++) {
			if (isdigit(fbuf[j]))
				continue;
			else
				return (0);
		}
	} else {
		return (0);
	}
	return (1);
}

static int
english(char *bp, int n)
{
#define	NASC 128		/* number of ascii char ?? */
	register int	j, vow, freq, rare;
	register int	badpun = 0, punct = 0;
	int	ct[NASC];

	if (n < 50)
		return (0); /* no point in statistics on squibs */
	for (j = 0; j < NASC; j++)
		ct[j] = 0;
	for (j = 0; j < n; j++) {
#ifdef __STDC__
		if (bp[j] < NASC)
			ct[bp[j]|040]++;
#else
		ct[bp[j]|040]++;
#endif
		switch (bp[j]) {
		case '.':
		case ',':
		case ')':
		case '%':
		case ';':
		case ':':
		case '?':
			punct++;
			if (j < n-1 && bp[j+1] != ' ' && bp[j+1] != '\n')
				badpun++;
		}
	}
	if (badpun*5 > punct)
		return (0);
	vow = ct['a'] + ct['e'] + ct['i'] + ct['o'] + ct['u'];
	freq = ct['e'] + ct['t'] + ct['a'] + ct['i'] + ct['o'] + ct['n'];
	rare = ct['v'] + ct['j'] + ct['k'] + ct['q'] + ct['x'] + ct['z'];
	if (2*ct[';'] > ct['e'])
		return (0);
	if ((ct['>'] + ct['<'] + ct['/']) > ct['e'])
		return (0);	/* shell file test */
	return (vow * 5 >= n - ct[' '] && freq >= 10 * rare);
}

/*
 * Convert a word from an elf file to native format.
 * This is needed because there's no elf routine to
 * get and decode a Note section header.
 */
static void
convert_elf32_word(Elf32_Word *data, int version, int format)
{
	Elf_Data src, dst;

	dst.d_buf = data;
	dst.d_version = version;
	dst.d_size = sizeof (Elf32_Word);
	dst.d_type = ELF_T_WORD;
	src.d_buf = data;
	src.d_version = version;
	src.d_size = sizeof (Elf32_Word);
	src.d_type = ELF_T_WORD;
	(void) elf32_xlatetom(&dst, &src, format);
}

/*
 * If it's a core file, print out the name of the file that dumped core.
 */
static int
core(Elf *elf, Elf32_Ehdr *ehdr, int format)
{
	register int inx;
	prpsinfo_t pr;
	Elf32_Phdr *phdr;
	Elf32_Nhdr nhdr;

	if (ehdr->e_type != ET_CORE)
		return (0);
	phdr = elf32_getphdr(elf);
	for (inx = 0; inx < (int)ehdr->e_phnum; inx++) {
		if (phdr->p_type == PT_NOTE) {
			(void) lseek(ifd, phdr->p_offset, SEEK_SET);
			(void) read(ifd, &nhdr, sizeof (Elf32_Nhdr));
			convert_elf32_word(&nhdr.n_namesz,
			    ehdr->e_version, format);
			/*
			 * Note: the ABI states that n_namesz
			 * must be rounded up to a 4 byte
			 * boundary.
			 */
			(void) lseek(ifd, ((off_t)phdr->p_offset +
			    sizeof (Elf32_Nhdr) +
			    ((nhdr.n_namesz + 0x03) & ~0x3)),
			    SEEK_SET);
			(void) read(ifd, &pr, sizeof (prpsinfo_t));
			printf(gettext(", from '%s'"), pr.pr_fname);
			break;
		} else {
			phdr += ehdr->e_phentsize;
		}
	}
	return (1);
}

static int
shellscript(char buf[], struct stat *sb)
{
	register char *tp;
	char *cp, *xp;

	cp = strchr(buf, '\n');
	if (cp == 0 || cp - fbuf > fbsz)
		return (0);
	for (tp = buf; tp != cp && isspace(*tp); tp++)
		if (!isascii(*tp))
			return (0);
	for (xp = tp; tp != cp && !isspace(*tp); tp++)
		if (!isascii(*tp))
			return (0);
	if (tp == xp)
		return (0);
	if (sb->st_mode & S_ISUID)
		printf("set-uid ");
	if (sb->st_mode & S_ISGID)
		printf("set-gid ");
	if (strncmp(xp, "/bin/sh", tp - xp) == 0)
		xp = "shell";
	else if (strncmp(xp, "/bin/csh", tp - xp) == 0)
		xp = "c-shell";
	else
		*tp = '\0';
	printf(gettext("executable %s script\n"), xp);
	return (1);
}

usage()
{
	fprintf(stderr, gettext(
		"usage: file [-h] [-m mfile] [-f ffile] file ...\n"
		"       file [-h] [-m mfile] -f ffile\n"
		"       file -c [-m mfile]\n"));
	exit(2);
}
