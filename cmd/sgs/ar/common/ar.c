/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)ar.c	6.26	95/05/10 SMI"
/* ar: UNIX Archive Maintainer */


#include <stdio.h>
#include <sys/param.h>
#include <ar.h>
#include <errno.h>
#include <ctype.h>
#include "sgs.h"

#ifndef	UID_NOBODY
#define	UID_NOBODY	60001
#endif

#ifndef GID_NOBODY
#define	GID_NOBODY	60001
#endif

#include <stdlib.h>

#include "libelf.h"

#include <signal.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#ifdef BROWSER
#include "sbfocus_enter.h"
#endif

#include <time.h>
#include <locale.h>

#define	SUID	04000
#define	SGID	02000
#define	ROWN	0400
#define	WOWN	0200
#define	XOWN	0100
#define	RGRP	040
#define	WGRP	020
#define	XGRP	010
#define	ROTH	04
#define	WOTH	02
#define	XOTH	01
#define	STXT	01000

#define	FLAG(ch)	(flg[ch - 'a'])
#define	CHUNK		500
#define	SYMCHUNK	1000
#define	SNAME		16
#define	ROUNDUP(x)	(((x) + 1) & ~1)

#define	LONGDIRNAME	"//              "
#define	SYMDIRNAME	"/               "	/* symbol directory filename */
#define	FORMAT		"%-16s%-12ld%-6u%-6u%-8o%-10ld%-2s"
#define	DATESIZE	60	 /*  sizeof (struct ar_hdr)  */

#define	C_FLAG		0x01
#define	T_FLAG		0x02

static	struct stat	stbuf;

static	char	ptr_index[SNAME];	/* holds the string that corresponds */
					/* to the filename's index in table  */

#ifdef BROWSER
extern  Sbld_rec   sb_data = {0, 0};	/* holds information needed */
				/* by sbfocus_symbol and sbfocus_close */
#endif

typedef struct arfile ARFILE;

struct arfile
{
	char	ar_name[SNAME];		/* info from archive member header */
	long	ar_date;
	int	ar_uid;
	int	ar_gid;
	unsigned long	ar_mode;
	long	ar_size;
	char    *longname;
	char    *rawname;
	long	offset;
	char	*pathname;
	char	*contents;
	ARFILE	*next;
};

static long	nsyms, *symlist;
static long	sym_tab_size, long_tab_size;
static long	*sym_ptr;
static long	*nextsym = NULL;
static int	syms_left = 0;


static ARFILE	*listhead, *listend;

static FILE	*outfile;
static int	fd;		/* opened archive file descriptor */

static Elf	*elf, *arf;

static char	flg[26];	/* a-z option flags */
static int	other_flgs;	/* For additional flags */
static char	**namv;		/* start of file names in argv arguments */
static char	*arnam;		/* archive file name from command line */
static char	*ponam;		/* positioning file name for a,b,i options */
static char	*gfile;		/* file name returned by match() */
static char	*str_base,	/* start of string table for names */
		*str_top;	/* pointer to next available location */

static char	*str_base1,
		*str_top1;
static	int	longnames = 0;	/* of of member files with names > 16 chars */

static int	signum[] = {SIGHUP, SIGINT, SIGQUIT, 0};
static int	namc;		/* count of file names in namv */
static int	modified;
static int	Vflag = 0;

static	int	m1[] = { 1, ROWN, 'r', '-' };
static	int	m2[] = { 1, WOWN, 'w', '-' };
static	int	m3[] = { 2, SUID, 's', XOWN, 'x', '-' };
static	int	m4[] = { 1, RGRP, 'r', '-' };
static	int	m5[] = { 1, WGRP, 'w', '-' };
static	int	m6[] = { 2, SGID, 's', XGRP, 'x', '-' };
static	int	m7[] = { 1, ROTH, 'r', '-' };
static	int	m8[] = { 1, WOTH, 'w', '-' };
static	int	m9[] = { 2, STXT, 't', XOTH, 'x', '-' };

static	int	*m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9};

static	int	notfound(),	qcmd(),
		rcmd(),		dcmd(),		xcmd(),
		pcmd(),		mcmd(),		tcmd(),
		create_extract(),
		search_sym_tab();

static	void	setcom(),	usage(),	sigexit(),
		cleanup(),	movefil(),	 mesg(),
		ar_select(),	mksymtab(),	getaf(),
		savename(),	writefile(),
		sputl(),	writesymtab(),	mklong_tab();

static  void	setup();

static	char	*trim(),	*match(),	*trimslash();

static	ARFILE	*getfile(),	*newfile();

static	FILE	*stats();
static  int	(*comfun)();	/* ptr to key operation: [rdxtpmq]cmd */

extern	char	*tempnam(),	*ctime();
extern	long	time(), lseek();
extern	void	exit(),		free();
extern  int	creat(),	write(),	close(),
		access(),	unlink(),	stat(),
		read();

#ifdef BROWSER
extern  void    sbfocus_symbol();
extern  void    sbfocus_close();
static  void    sbrowser_search_stab();
#endif
static	int	num_errs = 0;	/* Number of erros */

#define	OPTSTR	":a:b:i:vucsrdxtplmqVCT"

main(argc, argv)
	int argc;
	char **argv;
{
	register int i;
	int c;
	int usage_err = 0;

	setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	for (i = 0; signum[i]; i++)
		if (signal(signum[i], SIG_IGN) != SIG_IGN)
			(void) signal(signum[i], sigexit);

	if (argc < 2)
		usage();

	/*
	 * Option handling.
	 */
	if (argv[1][0] != '-') {
		int len;
		char *new;

		new = (char *)malloc(strlen(argv[1]) + 2);
		if (new == NULL) {
			fprintf(stderr, gettext(
			"ar: could not allocate memory.\n"));
			exit(1);
		}
		strcpy(new, "-");
		strcat(new, argv[1]);
		argv[1] = new;
	}
	setup(argc, argv);

	if (comfun == 0) {
		if (!(FLAG('d') || FLAG('r') || FLAG('q') ||
			FLAG('t') || FLAG('p') || FLAG('m') || FLAG('x'))) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	ar is a name of command. Do not translate.
 */
			(void) fprintf(stderr, gettext(
				"ar: one of [drqtpmx] must be specified\n"));
			exit(1);
		}
	}

	modified = FLAG('s');
	getaf();

	if ((fd == -1) &&
		(FLAG('d') || FLAG('t') || FLAG('p') || FLAG('m') ||
		FLAG('x') || (FLAG('r') && (FLAG('a') || FLAG('i') ||
		FLAG('b'))))) {
		(void) fprintf(stderr, gettext(
		"ar: archive, %s, not found\n"), arnam);
		exit(1);
	}

	(*comfun)();
#ifdef BROWSER
	sb_data.fd = NULL;
	sb_data.failed = 0;
#endif
	if (modified)	/* make archive symbol table */
		writefile();
	(void) close(fd);
#ifdef BROWSER
	sbfocus_close(&sb_data);
#endif
	return (notfound());
}

/*
 * Option hadning function.
 *	Using getopt(), following xcu4 convention.
 */
void
setup(int argc, char *argv[])
{
	int c;
	int usage_err = 0;
	extern char *optarg;
	extern int optind;
	extern int optopt;

	while ((c = getopt(argc, argv, OPTSTR)) != -1) {
		switch (c) {
		case 'a': /* position after named archive member file */
			flg[c - 'a']++;
			ponam = trim(optarg);
			break;
		case 'b': /* position before named archive member file */
		case 'i': /* position before named archive member: same as b */
			flg['b' - 'a']++;
			ponam = trim(optarg);
			break;
		case 'v': /* verbose */
		case 'l': /* temporary directory */
		case 'u': /* update: change archive dependent on file dates */
		case 'c': /* supress messages */
		case 's': /* force symbol table regeneration */
			flg[c - 'a']++;
			break;
		case 'r':
			/*
			 * key operation:
			 * replace or add files to the archive
			 */
			setcom(rcmd);
			flg[c - 'a']++;
			break;
		case 'd':
			/*
			 * key operation:
			 * delete files from the archive
			 */
			setcom(dcmd);
			flg[c - 'a']++;
			break;
		case 'x':
			/*
			 * key operation:
			 * extract files from the archive
			 */
			setcom(xcmd);
			flg[c - 'a']++;
			break;
		case 't':
			/*
			 * key operation:
			 * print table of contents
			 */
			setcom(tcmd);
			flg[c - 'a']++;
			break;
		case 'p':
			/*
			 * key operation:
			 * print files in the archive
			 */
			setcom(pcmd);
			flg[c - 'a']++;
			break;
		case 'm':
			/*
			 * key operation:
			 * move files to end of the archive
			 * or as indicated by position flag
			 */
			setcom(mcmd);
			flg[c - 'a']++;
			break;
		case 'q':
			/*
			 * key operation:
			 * quickly append files to end of the archive
			 */
			setcom(qcmd);
			flg[c - 'a']++;
			break;
		case 'V':
			/*
			 * print version information.
			 * adjust command line access accounting
			 */
			if (Vflag == 0) {
				(void) fprintf(stdout, "ar: %s %s\n",
				    (const char *)SGU_PKG,
				    (const char *)SGU_REL);
					Vflag++;
			}
			break;
		case 'C':
			other_flgs |= C_FLAG;
			break;
		case 'T':
			other_flgs |= T_FLAG;
			break;
		case ':':
			(void) fprintf(stderr, gettext(
				"ar: -%c requires an operand\n"),
				optopt);
			usage_err++;
			break;
		case '?':
			(void) fprintf(stderr, gettext(
				"ar: bad option `%c'\n"), optopt);
			usage_err++;
			break;
		}
	}

	if (usage_err || argc - optind < 1)
		usage();

	arnam = argv[optind];
	namv = &argv[optind+1];
	namc = argc - optind - 1;
}


/*
 * Set the function to be called to do the key operation.
 * Check that only one key is indicated.
 */
static void
setcom(fun)
	int (*fun)();
{
	if (comfun != 0) {
		(void) fprintf(stderr,
		"ar: only one of [drqtpmx] allowed\n");
		exit(1);
	}
	comfun = fun;
}

static	int
rcmd()
{
	register FILE *f;
	register ARFILE *fileptr;
	register ARFILE	*abifile = NULL;
	register ARFILE	*backptr = NULL;
	ARFILE	*endptr;
	ARFILE	*moved_files;
	ARFILE  *prev_entry, *new_listhead, *new_listend;
	int	deleted;

	new_listhead  = NULL;
	new_listend   = NULL;
	prev_entry    = NULL;

	for (fileptr = getfile(); fileptr; fileptr = getfile()) {
		deleted = 0;
		if (!abifile && ponam && strcmp(fileptr->longname, ponam) == 0)
			abifile = fileptr;
		else if (!abifile)
			backptr = fileptr;

		if (namc == 0 || match(fileptr->longname) != NULL) {
			f = stats(gfile);	/* gfile is set by match */
			if (f == NULL) {
				if (namc)
					(void) fprintf(stderr, gettext(
					"ar: cannot open %s\n"), gfile);
				mesg('c', gfile);
			} else {
			    if (FLAG('u') &&
				stbuf.st_mtime <= fileptr->ar_date) {
				(void) fclose(f);
				continue;
			    }
			    mesg('r', fileptr->longname);
			    movefil(fileptr);
			    free(fileptr->contents);
			    if ((fileptr->contents
				= (char *)malloc(ROUNDUP(stbuf.st_size)))
				== NULL) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	malloc is a name of a function. Do not translate.
 */
				(void) fprintf(stderr, gettext(
				"ar: cannot malloc space\n"));
				exit(1);
			    }
			    if (fread(fileptr->contents, sizeof (char),
						stbuf.st_size, f)
				!= stbuf.st_size) {
				(void) fprintf(stderr, gettext(
				"ar: cannot read %s\n"), fileptr->longname);
				exit(1);
			    }
			    if (fileptr->pathname != NULL)
				free(fileptr->pathname);
			    if ((fileptr->pathname = (char *)
					malloc(strlen(gfile) * sizeof (char *)))
					== NULL) {
				(void) fprintf(stderr, gettext(
					"ar: cannot malloc space\n"));
				exit(1);
			    }

			    (void) strcpy(fileptr->pathname, gfile);
			    fileptr->offset = 0;
			    (void) fclose(f);

			    if (ponam && (abifile != fileptr)) {
				deleted = 1;
				/* remove from archive list */
				if (prev_entry != NULL)
				    prev_entry->next = NULL;
				else
				    listhead = NULL;
				listend = prev_entry;

				/* add to moved list */
				if (new_listhead == NULL)
				    new_listhead = fileptr;
				else
				    new_listend->next = fileptr;
				new_listend = fileptr;
			    }
			    modified++;
			}
		}
		else
			mesg('c', fileptr->longname);

		if (deleted)
			deleted = 0;
		else
			prev_entry = fileptr;
	}

	endptr = listend;
	cleanup();
	if (ponam && endptr &&
		((moved_files = endptr->next) || new_listhead)) {
		if (!abifile) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 * 	posname. Look at man page. Be consistent with
 *	man page translation.
 */
			(void) fprintf(stderr, gettext(
			"ar: posname, %s, not found\n"), ponam);
			exit(2);
		}
		endptr->next = NULL;

		/*
		 * link new/moved files into archive entry list...
		 * 1: prepend newlist to moved/appended list
		 */
		if (new_listhead) {
		    if (!moved_files)
			listend = new_listend;
		    new_listend->next = moved_files;
		    moved_files = new_listhead;
		}
		/* 2: insert at appropriate position... */
		if (FLAG('b'))
			abifile = backptr;
		if (abifile) {
			listend->next = abifile->next;
			abifile->next = moved_files;
		} else {
			listend->next = listhead;
			listhead = moved_files;
		}
		listend = endptr;
	} else if (ponam && !abifile)
		(void) fprintf(stderr, gettext(
		"ar: posname, %s, not found\n"), ponam);
	return (0);
}

static	int
dcmd()
{
	register ARFILE	*fptr;
	register ARFILE *backptr = NULL;

	for (fptr = getfile(); fptr; fptr = getfile()) {
		if (match(fptr->longname) != NULL) {
			mesg('d', fptr->longname);
			if (backptr == NULL)
				listhead = NULL;
			else {
				backptr->next = NULL;
				listend = backptr;
			}
			modified = 1;
		} else {
			mesg('c', fptr->longname);
			backptr = fptr;
		}
	}
	return (0);
}

static	int
xcmd()
{
	register int f;
	register ARFILE *next;
	int	rawname = 0;
	int f_len;

	/*
	 * If -T is specified, get the maximum file name length.
	 */
	if (other_flgs & T_FLAG) {
		f_len = pathconf(".", _PC_NAME_MAX);
		if (f_len == -1) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	-T is an option specified.
 */
			fprintf(stderr, gettext(
			"ar: -T failed to calculate file name length.\n"));
			exit(1);
		}
	}
	for (next = getfile(); next; next = getfile()) {
	    if ((strcmp(next->longname, "") == 0) &&
		(strcmp(next->rawname, "") != 0))
		rawname = 1;
	    if (namc == 0 || match(next->longname) != NULL ||
		match(next->rawname) != NULL) {
		f = create_extract(next, rawname, f_len);
		if (f >= 0) {
			if (rawname) {
				mesg('x', next->rawname);
				if (write(f, next->contents, (unsigned)
					next->ar_size) != next->ar_size) {
					(void) fprintf(stderr, gettext(
					"ar: %s: cannot write\n"),
					next->rawname);
					exit(1);
				}
			} else {
				mesg('x', next->longname);
				if (write(f, next->contents, (unsigned)
					next->ar_size) != next->ar_size) {
					(void) fprintf(stderr, gettext(
					"ar: %s: cannot write\n"),
					next->longname);
					exit(1);
				}
			}
			(void) close(f);
		} else
			exit(1);
	    }
	    rawname = 0;
	} /* for */
	return (0);
}

static	int
pcmd()
{
	register ARFILE	*next;

	for (next = getfile(); next; next = getfile()) {
		if (namc == 0 || match(next->longname) != NULL) {
		    if (FLAG('v')) {
			(void) fprintf(stdout,
			"\n<%s>\n\n", next->longname);
			(void) fflush(stdout);
		    }
		    (void) fwrite(next->contents, sizeof (char),
			next->ar_size, stdout);
		}
	}
	return (0);
}

static	int
mcmd()
{
	register ARFILE	*fileptr;
	register ARFILE	*abifile = NULL;
	register ARFILE	*tmphead = NULL;
	register ARFILE	*tmpend = NULL;
	ARFILE	*backptr1 = NULL;
	ARFILE	*backptr2 = NULL;

	for (fileptr = getfile(); fileptr; fileptr = getfile()) {
		if (match(fileptr->longname) != NULL) {
			mesg('m', fileptr->longname);
			if (tmphead)
				tmpend->next = fileptr;
			else
				tmphead = fileptr;
			tmpend = fileptr;
			if (backptr1) {
				listend = backptr1;
				listend->next = NULL;
			}
			else
				listhead = NULL;
			continue;
		}
		mesg('c', fileptr->longname);
		backptr1 = fileptr;
		if (ponam && !abifile) {
			if (strcmp(fileptr->longname, ponam) == 0)
				abifile = fileptr;
			else
				backptr2 = fileptr;
		}
	}

	if (!tmphead)
		return (1);

	if (!ponam)
		listend->next = tmphead;
	else {
		if (!abifile) {
			(void) fprintf(stderr, gettext(
			"ar: posname, %s, not found\n"), ponam);
			exit(2);
		}
		if (FLAG('b'))
			abifile = backptr2;
		if (abifile) {
			tmpend->next = abifile->next;
			abifile->next = tmphead;
		} else {
			tmphead->next = listhead;
			listhead = tmphead;
		}
	}
	modified++;
	return (0);
}

static	int
tcmd()
{
	register ARFILE	*next;
	register int	**mp;
	char   buf[DATESIZE];

	for (next = getfile(); next; next = getfile()) {
		if (namc == 0 ||
			match(next->longname) != NULL ||
			match(next->rawname) != NULL) {
			if (FLAG('v')) {
				for (mp = &m[0]; mp < &m[9]; )
					ar_select(*mp++, next->ar_mode);

				(void) fprintf(stdout, "%6d/%6d", next->ar_uid,
				next->ar_gid);
				(void) fprintf(stdout, "%7ld", next->ar_size);
				if ((strftime(buf,
					DATESIZE,
					"%b %d %H:%M %Y",
					localtime(&(next->ar_date)))) == 0) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	You may translate this as "extracting date/time information failed."
 */
				    (void) fprintf(stderr, gettext(
			"ar: don't have enough space to store the date\n"));
				    exit(1);
				}
				(void) fprintf(stdout, " %s ", buf);
			}
			if ((strcmp(next->longname, "") == 0) &&
			    (strcmp(next->rawname, "") != 0))
				(void) fprintf(stdout,
					"%s\n", trim(next->rawname));
			else
				(void) fprintf(stdout,
					"%s\n", trim(next->longname));
		}
	} /* for */
	return (0);
}

static	int
qcmd()
{
	register ARFILE *fptr;

	if (FLAG('a') || FLAG('b')) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	abi means option a or b or i.
 */
		(void) fprintf(stderr, gettext(
		"ar: abi not allowed with q\n"));
		exit(1);
	}
	for (fptr = getfile(); fptr; fptr = getfile())
		;
	cleanup();
	return (0);
}

static void
getaf()
{
	Elf_Cmd cmd;

	if (elf_version(EV_CURRENT) == EV_NONE) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	Do not translate libelf.a. It is a name of a library.
 */
		(void) fprintf(stderr, gettext(
		"ar: libelf.a out of date\n"));
		exit(1);
	}

	if ((fd  = open(arnam, O_RDONLY)) == -1) {
		if (errno == ENOENT) {
			/* archive does not exist yet, may have to create one */
			return;
		} else {
			/* problem other than "does not exist" */
			(void) fprintf(stderr, gettext(
			"ar: cannot open %s: "), arnam);
			perror("");
			exit(1);
		}
	}

	cmd = ELF_C_READ;
	arf = elf_begin(fd, cmd, (Elf *)0);

	if (elf_kind(arf) != ELF_K_AR) {
		(void) fprintf(stderr, gettext(
		"ar: %s not in archive format\n"), arnam);
		if (FLAG('a') || FLAG('b'))
		    (void) fprintf(stderr, gettext(
		    "ar: %s taken as mandatory 'posname' with keys 'abi'\n"),
		    ponam);
		exit(1);
	}
}

static	ARFILE *
getfile()
{
	Elf_Arhdr *mem_header;
	register ARFILE	*file;
	char	*tmp_rawname, *file_rawname;
	int	len;
	if (fd == -1)
		return (NULL); /* the archive doesn't exist */

	if ((elf = elf_begin(fd, ELF_C_READ, arf)) == 0)
		return (NULL);  /* the archive is empty or have hit the end */

	if ((mem_header = elf_getarhdr(elf)) == NULL) {
		(void) fprintf(stderr, gettext(
		"ar: %s: malformed archive (at %ld)\n"),
		arnam, elf_getbase(elf));
		exit(1);
	}

	/* zip past special members like the symbol and string table members */

	while (strncmp(mem_header->ar_name, "/", 1) == 0 ||
		strncmp(mem_header->ar_name, "//", 2) == 0) {
		(void) elf_next(elf);
		(void) elf_end(elf);
		if ((elf = elf_begin(fd, ELF_C_READ, arf)) == 0)
			return (NULL);
			/* the archive is empty or have hit the end */
		if ((mem_header = elf_getarhdr(elf)) == NULL) {
			(void) fprintf(stderr, gettext(
			"ar: %s: malformed archive (at %ld)\n"),
			arnam, elf_getbase(elf));
			exit(0);
		}
	}

	file = newfile();
	(void) strncpy(file->ar_name, mem_header->ar_name, SNAME);

	if ((file->longname
	    = (char *)malloc(strlen(mem_header->ar_name) * sizeof (char *)))
	    == NULL) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
	This means 'Allocation of memory space failed'.
 */
		(void) fprintf(stderr, gettext(
		"ar: cannot malloc space\n"));
		exit(1);
	}
	(void) strcpy(file->longname, mem_header->ar_name);
	if ((file->rawname
	    = (char *)malloc(strlen(mem_header->ar_rawname)*sizeof (char *)))
	    == NULL) {
		(void) fprintf(stderr, gettext(
		"ar: cannot malloc space\n"));
		exit(1);
	}
	tmp_rawname = mem_header->ar_rawname;
	file_rawname = file->rawname;
	while (!isspace(*tmp_rawname) &&
		((*file_rawname = *tmp_rawname) != '\0')) {
		file_rawname++;
		tmp_rawname++;
	}
	if (!(*tmp_rawname == '\0'))
		*file_rawname = '\0';
	file->ar_date = mem_header->ar_date;
	file->ar_uid  = mem_header->ar_uid;
	file->ar_gid  = mem_header->ar_gid;
	file->ar_mode = (unsigned long) mem_header->ar_mode;
	file->ar_size = mem_header->ar_size;
	file->offset = elf_getbase(elf);

	/* reverse logic */
	if (!(FLAG('t') && !FLAG('s'))) {
		if ((file->contents
		    = (char *)malloc(ROUNDUP(file->ar_size)))
		    == NULL) {
			(void) fprintf(stderr, gettext(
			"ar: cannot malloc space\n"));
			exit(1);
		}

		if (lseek(fd, file->offset, 0) != file->offset) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	This means 'Lseek() system call failed.'.
 */
			(void) fprintf(stderr, gettext(
			"ar: cannot lseek\n"));
			exit(1);
		}
		if (read(fd,
			file->contents,
			(unsigned) ROUNDUP(file->ar_size)) == -1) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	This means 'Read system call failed'.
 */
			(void) fprintf(stderr, gettext(
			"ar: %s: cannot read\n"), arnam);
			exit(1);
		}
	}
	(void) elf_next(elf);
	(void) elf_end(elf);
	return (file);
}

static	ARFILE *
newfile()
{
	static ARFILE	*buffer =  NULL;
	static int	count = 0;
	register ARFILE	*fileptr;

	if (count == 0) {
		if ((buffer = (ARFILE *) calloc(CHUNK, sizeof (ARFILE)))
		    == NULL) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	This means 'Allocating memory space failed."
 */
			(void) fprintf(stderr, gettext(
			"ar: cannot calloc space\n"));
			exit(1);
		}
		count = CHUNK;
	}
	count--;
	fileptr = buffer++;

	if (listhead)
		listend->next = fileptr;
	else
		listhead = fileptr;
	listend = fileptr;
	return (fileptr);
}

static void
usage()
{
	(void) fprintf(stderr, gettext(
"usage: ar -d[-vV] archive file ...\n"
"       ar -m[-abivV] [posname] archive file ...\n"
"       ar -p[-vV][-s] archive [file ...]\n"
"       ar -q[-cuvV] [-abi] [posname] [file ...]\n"
"       ar -t[-vV][-s] archive [file ...]\n"
"       ar -x[-vV][-sCT] archive [file ...]\n"));
	exit(1);
}

/*ARGSUSED0*/
static void
sigexit(i)
int i;
{
	if (outfile)
		(void) unlink(arnam);
	exit(100);
}

/* tells the user which of the listed files were not found in the archive */

static int
notfound()
{
	register int i, n;

	n = 0;
	for (i = 0; i < namc; i++)
		if (namv[i]) {
			(void) fprintf(stderr, gettext(
			"ar: %s not found\n"), namv[i]);
			n++;
		}
	return (n);
}


/* puts the file which was in the list in the linked list */

static void
cleanup()
{
	register int i;
	register FILE	*f;
	register ARFILE	*fileptr;

	for (i = 0; i < namc; i++) {
		if (namv[i] == 0)
			continue;
		mesg('a', namv[i]);
		f = stats(namv[i]);
		if (f == NULL)
			(void) fprintf(stderr, gettext(
			"ar: %s cannot open\n"), namv[i]);
		else {
			fileptr = newfile();
			/* if short name */
			(void) strncpy(fileptr->ar_name, trim(namv[i]), SNAME);

			if ((fileptr->longname
			    = (char *)malloc(strlen(trim(namv[i]))
						* sizeof (char *)))
			    == NULL) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 * 	This means 'Could not allocate memory space."
 */
				(void) fprintf(stderr, gettext(
				"cannot malloc space\n"));
				exit(1);
			}

			(void) strcpy(fileptr->longname,  trim(namv[i]));

			if ((fileptr->pathname
			    = (char *)malloc(strlen(namv[i]) * sizeof (char *)))
			    == NULL) {
				(void) fprintf(stderr, gettext(
				"cannot malloc space\n"));
				exit(1);
			}

			(void) strcpy(fileptr->pathname, namv[i]);

			movefil(fileptr);
			if ((fileptr->contents = (char *)
				malloc(ROUNDUP(stbuf.st_size))) == NULL) {
				(void) fprintf(stderr, gettext(
				"cannot malloc space\n"));
				exit(1);
			}
			if (fread(fileptr->contents,
				sizeof (char),
				stbuf.st_size, f) != stbuf.st_size) {
				(void) fprintf(stderr, gettext(
				"ar: %s: cannot read\n"),
				fileptr->longname);
				exit(1);
			}
			(void) fclose(f);
			modified++;
			namv[i] = 0;
		}
	}
}

/*
* insert the file 'file' into the temporary file
*/

static void
movefil(fileptr)
	register ARFILE *fileptr;
{
	fileptr->ar_size = stbuf.st_size;
	fileptr->ar_date = stbuf.st_mtime;

	if ((unsigned short) stbuf.st_uid > 60000)
		fileptr->ar_uid = UID_NOBODY;
	else
		fileptr->ar_uid = stbuf.st_uid;
	if ((unsigned short) stbuf.st_gid > 60000)
		fileptr->ar_uid = GID_NOBODY;
	else
		fileptr->ar_gid = stbuf.st_gid;
	fileptr->ar_mode = stbuf.st_mode;
}

static FILE *
stats(file)
	register char *file;
{
	register FILE *f;

	f = fopen(file, "r");
	if (f == NULL)
		return (f);
	if (stat(file, &stbuf) < 0) {
		(void) fclose(f);
		return (NULL);
	}
	return (f);
}

static char *
match(file)
	register char	*file;
{
	register int i;

	for (i = 0; i < namc; i++) {
		if (namv[i] == 0)
			continue;
		if (strcmp(trim(namv[i]), file) == 0) {
			gfile = namv[i];
			file = namv[i];
			namv[i] = 0;
			return (file);
		}
	}
	return (NULL);
}

static void
mesg(c, file)
	int	c;
	char	*file;
{
	if (FLAG('v'))
		if (c != 'c' || FLAG('v') > 1)
			(void) fprintf(stdout, "%c - %s\n", c, file);
}

static char *
trimslash(s)
	char *s;
{
	static char buf[SNAME];

	(void) strncpy(buf, trim(s), SNAME - 2);
	buf[SNAME - 2] = '\0';
	return (strcat(buf, "/"));
}

static char *
trim(s)
	char *s;
{
	register char *p1, *p2;

	for (p1 = s; *p1; p1++)
		;
	while (p1 > s) {
		if (*--p1 != '/')
			break;
		*p1 = 0;
	}
	p2 = s;
	for (p1 = s; *p1; p1++)
		if (*p1 == '/')
			p2 = p1 + 1;
	return (p2);
}

static void
ar_select(pairp, mode)
	int	*pairp;
	unsigned long	mode;
{
	register int n, *ap;

	ap = pairp;
	n = *ap++;
	while (--n >= 0 && (mode & *ap++) == 0)
		ap++;
	(void) putchar(*ap);
}

static void
mksymtab()
{
	register ARFILE	*fptr;
	long	mem_offset = 0;

	Elf32_Ehdr *ehdr;
	Elf_Scn	*scn;
	Elf32_Shdr *shdr;
	int newfd;
	long  currentloc, computedloc, newloc;

	Elf_Data	*data;
	char	*sbshstr;
	char	*sbshstrtp;
	int	sbstabsect = -1;
	int	sbstabstrsect = -1;

	newfd = 0;
	for (fptr = listhead; fptr; fptr = fptr->next) {
		/* determine if file is coming from the archive or not */
		if ((fptr->offset > 0) && (fptr->pathname == NULL)) {
			currentloc = lseek(fd, 0, 1);
			if (currentloc < 0)
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	Next 9 messages are for internal errors or good only
 *	for programmers. You may leave them as un-translated.
 */
				fprintf(stderr, gettext(
				"ar: lseek(current) errno=%d\n"), errno);
			newloc = lseek(fd, fptr->offset - 60, 0);
			if (newloc < 0)
				fprintf(stderr, gettext(
				"ar: lseek(f->offset) errno=%d\n"), errno);
			if (newloc != fptr->offset-60) {
				fprintf(stderr, gettext(
				"ar: Problem seeking\n"));
				fprintf(stderr, gettext(
			"ar: currentloc %d intendedloc %d resultingloc %d\n"),
			currentloc, fptr->offset-60, newloc);
				fprintf(stderr,
				"ar: ar_name '%s' longname '%s'\n",
				(fptr->ar_name) ? fptr->ar_name : "",
				(fptr->longname) ? fptr->longname : "");
				exit(1);
			}
			if (elf_rand(arf, newloc) == 0) {
				(void) fprintf(stderr, gettext(
"ar: internal or system error; archive file has been scribbled\n"));
				exit(1);
			}
			if ((elf = elf_begin(fd, ELF_C_READ, arf)) == 0) {
				(void) fprintf(stderr, gettext(
"ar: archive is corrupted/possible end-of-archive\n"));
				(void) fprintf(stderr, gettext(
"ar: can not find member'%s' at offset 0x%x\n"),
				fptr->ar_name ?
				fptr->ar_name : "???", fptr->offset-60);
				break;
			}
		} else if ((fptr->offset == 0) && (fptr->pathname != NULL)) {
			if ((newfd  = open(fptr->pathname, O_RDONLY)) == -1) {
				(void) fprintf(stderr, gettext(
					"ar: cannot open %s\n"),
					fptr->pathname);
				num_errs++;
				continue;
			}

			if ((elf = elf_begin(newfd,
					ELF_C_READ,
					(Elf *)0)) == 0) {
				if (fptr->pathname != NULL)
					(void) fprintf(stderr,
gettext(
"ar: cannot elf_begin() %s.\n"), fptr->pathname);
				else
					(void) fprintf(stderr,
gettext(
"ar: cannot elf_begin().\n"));
				close(newfd);
				newfd = 0;
				num_errs++;
				continue;
			}
			if (elf_kind(elf) == ELF_K_AR) {
				if (fptr->pathname != NULL)
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	User's usage error.
 */
					(void) fprintf(stderr, gettext(
"ar: %s is in archive format - embedded archives are not allowed\n"),
					fptr->pathname);
				else
					(void) fprintf(stderr, gettext(
"ar: embedded archives are not allowed.\n"));
				if (newfd) {
					close(newfd);
					newfd = 0;
				}
				elf_end(elf);
				continue;
			}
		} else {
			(void) fprintf(stderr, gettext(
"ar: internal error - "
"cannot tell whether file is included in archive or not\n"));
			exit(1);
		}
		if ((ehdr = elf32_getehdr(elf)) != 0) {
			scn = elf_getscn(elf, ehdr->e_shstrndx);
			if (scn == NULL) {
				if (fptr->pathname != NULL)
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	Next 8 messages can be translated as:
 *		Elf format error.
 *	(Do not traslate Elf though.)
 */
					fprintf(stderr, gettext(
"ar: %s has no section header or bad elf format.\n"),
						fptr->pathname);
				else
					fprintf(stderr, gettext(
"ar: no section header or bad elf format.\n"));
				num_errs++;
				if (newfd) {
					close(newfd);
					newfd = 0;
				}
				elf_end(elf);
				continue;
			}
			data = 0;
			data = elf_getdata(scn, data);
			if (data == NULL) {
				if (fptr->pathname != NULL)
					fprintf(stderr, gettext(
"ar: %s has bad elf format.\n"),
						fptr->pathname);
				else
					fprintf(stderr, gettext(
"ar: bad elf format.\n"));
				num_errs++;
				if (newfd) {
					close(newfd);
					newfd = 0;
				}
				elf_end(elf);
				continue;
			}
			if (data->d_size == 0) {
				if (fptr->pathname != NULL)
					(void) fprintf(stderr, gettext(
"ar: %s has no data in section header table.\n"), fptr->pathname);
				else
					(void) fprintf(stderr, gettext(
"ar: No data in section header table.\n"));
				if (newfd) {
					close(newfd);
					newfd = 0;
				}
				elf_end(elf);
				num_errs++;
				continue;
			}
			sbshstr = (char *)data->d_buf;

			/* loop through sections to find symbol table */
			scn = 0;
			while ((scn = elf_nextscn(elf, scn)) != 0) {
				shdr = elf32_getshdr(scn);
				if (scn == NULL) {
					if (fptr->pathname != NULL)
						fprintf(stderr, gettext(
"ar: %s has bad elf format.\n"), fptr->pathname);
					else
						fprintf(stderr, gettext(
"ar: bad elf format.\n"));
					if (newfd) {
						close(newfd);
						newfd = 0;
					}
					num_errs++;
					elf_end(elf);
					continue;
				}
				if (shdr->sh_type == SHT_SYMTAB)
				    if (search_sym_tab(elf,
						shdr,
						scn,
						mem_offset,
						fptr->pathname) == -1) {
					if (newfd) {
						close(newfd);
						newfd = 0;
					}
					continue;
				    }
#ifdef BROWSER
				if (shdr->sh_name != 0) {
					sbshstrtp = (char *)
						((int)sbshstr + shdr->sh_name);
					if (strcmp(sbshstrtp, ".stab") == 0) {
						sbstabsect = elf_ndxscn(scn);
					} else if (strcmp(sbshstrtp,
							".stabstr") == 0) {
						sbstabstrsect = elf_ndxscn(scn);
					}
				}
#endif
			}
#ifdef BROWSER
			if (sbstabsect != -1 || sbstabstrsect != -1) {
				(void) sbrowser_search_stab(newfd,
					ehdr,
					sbstabsect,
					sbstabstrsect);
				sbstabsect = -1;
				sbstabstrsect = -1;
			}
#endif
		}
		mem_offset += sizeof (struct ar_hdr) + ROUNDUP(fptr->ar_size);
		(void) elf_end(elf);
		if (newfd) {
			(void) close(newfd);
			newfd = 0;
		}
	} /* for */
	if (num_errs)
		exit(1);
}

static void
writesymtab(tf)
	register FILE	*tf;
{
	long	offset;
	char	buf1[sizeof (struct ar_hdr) + 1];
	char	buf11[sizeof (struct ar_hdr) + 1];
	register char	*buf2, *bptr;
	int	i, j;
	long	*ptr;

	/*
	 * patch up archive pointers and write the symbol entries
	 */
	while ((str_top - str_base) & 03)	/* round up string table */
		*str_top++ = '\0';
	sym_tab_size = (nsyms +1) * 4 + sizeof (char) * (str_top - str_base);
	offset = (nsyms + 1) * 4 + sizeof (char) * (str_top - str_base)
		+ sizeof (struct ar_hdr) + SARMAG;

	(void) sprintf(buf1, FORMAT, SYMDIRNAME, time(0), (unsigned)0,
		(unsigned)0, (unsigned)0, (long)sym_tab_size, ARFMAG);

	if (longnames)
		offset += long_tab_size + sizeof (struct ar_hdr);

	if (strlen(buf1) != sizeof (struct ar_hdr)) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	archive file format error.
 */
		(void) fprintf(stderr, gettext(
		"ar: internal header generation error\n"));
		exit(1);
	}

	if ((buf2 = (char *)malloc(4 * (nsyms + 1))) == NULL) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	THis means 'Could not allocate memory space'.
 */
		(void) fprintf(stderr, gettext(
		"ar: cannot get space for number of symbols\n"));
		(void) fprintf(stderr, gettext(
		"ar: diagnosis: ERRNO=%d\n"), errno);
		exit(1);
	}
	sputl(nsyms, buf2);
	bptr = buf2 + 4;

	for (i = 0, j = SYMCHUNK, ptr = symlist; i < nsyms; i++, j--, ptr++) {
		if (!j) {
			j = SYMCHUNK;
			ptr = (long *) *ptr;
		}
		*ptr += offset;
		sputl(*ptr, bptr);
		bptr += 4;
	}
	(void) fwrite(buf1, 1, sizeof (struct ar_hdr), tf);
	(void) fwrite(buf2, 1, (nsyms  + 1) * 4, tf);
	(void) fwrite(str_base, 1, sizeof (char) * (str_top - str_base), tf);
}

static void
savename(symbol)
	char    *symbol;
{
	static int str_length = BUFSIZ * 5;
	register char *p, *s;
	register unsigned int i;
	int diff;

	diff = 0;
	if (str_base == (char *)0) {
		/* no space allocated yet */
		if ((str_base = (char *)malloc((unsigned)str_length))
		    == NULL) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	THis means 'Could not allocate memory space'.
 */
			(void) fprintf(stderr, gettext(
			"ar: %s cannot get string table space\n"),
				arnam);
			(void) fprintf(stderr, gettext(
			"ar: diagnosis: ERRNO=%d\n"), errno);
			exit(1);
		}
		str_top = str_base;
	}

	p = str_top;
	str_top += strlen(symbol) + 1;

	if (str_top > str_base + str_length) {
		char *old_base = str_base;

		str_length += BUFSIZ * 2;
		if ((str_base = (char *)realloc(str_base, str_length)) ==
			NULL) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	THis means 'Could not allocate memory space'.
 */
			(void) fprintf(stderr, gettext(
			"ar: %s cannot grow string table\n"), arnam);
			exit(1);
		}
		/*
		 * Re-adjust other pointers
		 */
		diff = str_base - old_base;
		p += diff;
	}
	for (i = 0, s = symbol;
		i < strlen(symbol) && *s != '\0'; i++) {
		*p++ = *s++;
	}
	*p++ = '\0';
	str_top = p;
}

static void
savelongname(fptr)
	ARFILE	*fptr;
{
	static int str_length = BUFSIZ * 5;
	register char *p, *s;
	register unsigned int i;
	int diff;
	static int bytes_used;
	int index;
	char	ptr_index1[SNAME-1];

	diff = 0;
	if (str_base1 == (char *)0) {
		/* no space allocated yet */
		if ((str_base1 = (char *)malloc((unsigned)str_length))
		    == NULL) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	THis means 'Could not allocate memory space'.
 */
			(void) fprintf(stderr, gettext(
			"ar: %s cannot get string table space\n"), arnam);
			(void) fprintf(stderr, gettext(
			"ar: diagnosis: ERRNO=%d\n"), errno);
			exit(1);
		}
		str_top1 = str_base1;
	}

	p = str_top1;
	str_top1 += strlen(fptr->longname) + 2;

	index = bytes_used;
	(void) sprintf(ptr_index1, "%d", index); /* holds digits */
	(void) sprintf(ptr_index, "%-16s", SYMDIRNAME);
	ptr_index[1] = '\0';
	(void) strcat(ptr_index, ptr_index1);
	(void) strcpy(fptr->ar_name, ptr_index);
	bytes_used += strlen(fptr->longname) + 2;

	if (str_top1 > str_base1 + str_length) {
		char *old_base = str_base1;

		str_length += BUFSIZ * 2;
		if ((str_base1 = (char *)realloc(str_base1, str_length))
		    == NULL) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	THis means 'Could not allocate memory space'.
 */
			(void) fprintf(stderr, gettext(
				"ar: %s cannot grow string table\n"), arnam);
			exit(1);
		}
		/*
		 * Re-adjust other pointers
		 */
		diff = str_base1 - old_base;
		p += diff;
	}
	for (i = 0, s = fptr->longname;
		i < strlen(fptr->longname) && *s != '\0';
		i++) {
		*p++ = *s++;
	}
	*p++ = '/';
	*p++ = '\n';
	str_top1 = p;
}

static void
writefile()
{
	register ARFILE	* fptr;
	char		buf[sizeof (struct ar_hdr) + 1];
	char		buf11[sizeof (struct ar_hdr) + 1];
	register int 	i;
	int		new_archive;

	mklong_tab();
	mksymtab();

	for (i = 0; signum[i]; i++)
		/* started writing, cannot interrupt */
		(void) signal(signum[i], SIG_IGN);

	/* Is this a new archive? */
	if ((access(arnam, 0) < 0) && (errno == ENOENT)) {
	    new_archive = 1;
	    if (!FLAG('c')) {
		(void) fprintf(stderr, gettext(
			"ar: creating %s\n"), arnam);
	    }
	}
	else
	    new_archive = 0;

	if ((outfile = fopen(arnam, "w")) == NULL) {
	    if (new_archive)
		(void) fprintf(stderr, gettext(
		"ar: cannot create %s\n"), arnam);
	    else
		(void) fprintf(stderr, gettext(
		"ar: cannot write %s\n"), arnam);
	    exit(1);
#ifdef XPG4
	}
#else
	} else if (FLAG('v'))
	    (void) fprintf(stderr, gettext(
	    "ar: writing %s\n"), arnam);
#endif

	(void) fwrite(ARMAG, sizeof (char), SARMAG, outfile);

	if (nsyms)
		writesymtab(outfile);

	if (longnames) {
		(void) sprintf(buf11, FORMAT, LONGDIRNAME, time(0),
			(unsigned)0, (unsigned)0, (unsigned)0,
			(long)long_tab_size, ARFMAG);
		(void) fwrite(buf11, 1, sizeof (struct ar_hdr), outfile);
		(void) fwrite(str_base1, 1,
			sizeof (char) * (str_top1 - str_base1), outfile);
	}
	for (fptr = listhead; fptr; fptr = fptr->next) {
			if (strlen(fptr->longname) <= (unsigned)SNAME-2)
			(void) sprintf(buf, FORMAT,
			    trimslash(fptr->longname), fptr->ar_date,
			    (unsigned)fptr->ar_uid, (unsigned)fptr->ar_gid,
			    (unsigned)fptr->ar_mode, fptr->ar_size, ARFMAG);
		else
			(void) sprintf(buf, FORMAT, fptr->ar_name,
			    fptr->ar_date, (unsigned)fptr->ar_uid,
			    (unsigned)fptr->ar_gid, (unsigned)fptr->ar_mode,
			    fptr->ar_size, ARFMAG);

		if (fptr->ar_size & 0x1)
			fptr->contents[ fptr->ar_size ] = '\n';

		(void) fwrite(buf, sizeof (struct ar_hdr), 1, outfile);
		(void) fwrite(fptr->contents, ROUNDUP(fptr->ar_size), 1,
			outfile);
	}
	if (ferror(outfile)) {
		(void) fprintf(stderr, gettext(
			"ar: cannot write archive\n"));
		(void) unlink(arnam);
		exit(2);
	}
	(void) fclose(outfile);
}

static void
mklong_tab()
{
	ARFILE  *fptr;
	for (fptr = listhead; fptr; fptr = fptr->next) {
		if (strlen(fptr->longname) >= (unsigned)SNAME-1) {
			longnames++;
			savelongname(fptr);
			(void) strcpy(fptr->ar_name, ptr_index);
		}
	}
	if (longnames) {
		/* round up table that keeps the long filenames */
		while ((str_top1 - str_base1) & 03)
			*str_top1++ = '\0';
		long_tab_size = sizeof (char) * (str_top1 - str_base1);
	}
}

/* Put bytes in archive header in machine independent order.  */
static
void
sputl(n, cp)
	long n;
	char *cp;
{
	*cp++ = n/(256*256*256);
	*cp++ = n/(256*256);
	*cp++ = n/(256);
	*cp++ = n & 255;
}

#ifdef BROWSER
static
void
sbrowser_search_stab(fd, ehdr, stabid, stabstrid)
	int	fd;
	Elf32_Ehdr	*ehdr;
	int	stabid;
	int	stabstrid;
{
	Elf_Scn		*stab_scn;
	Elf_Scn		*stabstr_scn;
	Elf32_Shdr	*stab_sh;
	Elf32_Shdr	*stabstr_sh;
	Elf_Data	*data;
	struct nlist	*np;
	struct nlist	*stabtab;
	char		*cp;
	char		*p;
	char		*stabstrtab;
	char		*stabstroff;
	char		*symname;
	int		prevstabstrsz;
	int		nstab;

	/* use the .stab and stabstr section index to find the data buffer */
	if (stabid == -1) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	Next 22 messages can be translated as
 *		Bad elf format.
 */
		(void) fprintf(stderr, gettext(
			"ar: No data in stab table.\n"));
		return;
	}
	stab_scn = elf_getscn(elf, (Elf32_Half)stabid);
	if (stab_scn == NULL) {
		(void) fprintf(stderr, gettext(
			"ar: Bad elf format.\n"));
		return;
	}
	stab_sh = elf32_getshdr(stab_scn);
	if (stab_sh == NULL) {
		(void) fprintf(stderr, gettext(
			"ar: Bad elf format.\n"));
		return;
	}
	nstab = stab_sh->sh_size / stab_sh->sh_entsize;
	if (stabstrid == -1) {
		(void) fprintf(stderr, gettext(
			"ar: No data in stab string table.\n"));
		return;
	}
	stabstr_scn = elf_getscn(elf, (Elf32_Half)stabstrid);
	if (stabstr_scn == NULL) {
		(void) fprintf(stderr, gettext(
			"ar: Bad elf format.\n"));
		return;
	}
	stabstr_sh = elf32_getshdr(stabstr_scn);
	if (stabstr_sh == NULL) {
		(void) fprintf(stderr, gettext(
			"ar: Bad elf format.\n"));
		return;
	}
	if (stabstr_sh->sh_size == 0) {
		(void) fprintf(stderr, gettext(
			"ar: No data in stab string table.\n"));
		return;
	}
	data = 0;
	data = elf_getdata(stabstr_scn, data);
	if (data == NULL) {
		(void) fprintf(stderr, gettext(
			"ar: Bad elf format.\n"));
		return;
	}
	if (data->d_size == 0) {
		(void) fprintf(stderr, gettext(
			"ar: No data in stab string table.\n"));
		return;
	}
	stabstrtab = (char *)data->d_buf;
	data = 0;
	data = elf_getdata(stab_scn, data);
	if (data == NULL) {
		(void) fprintf(stderr, gettext(
			"ar: Bad elf format.\n"));
		return;
	}
	if (data->d_size == 0) {
		(void) fprintf(stderr, gettext(
			"ar: No data in stab table - size is 0\n"));
		return;
	}
	stabtab = (struct nlist *)data->d_buf;
	stabstroff = stabstrtab;
	prevstabstrsz = 0;
	for (np = stabtab; np < &stabtab[nstab]; np++) {
		if (np->n_type == 0) {
			stabstroff += prevstabstrsz;
			prevstabstrsz = np->n_value;
		}
		symname = stabstroff + np->n_un.n_strx;
		if (np->n_type == 0x48) {
			sbfocus_symbol(&sb_data, arnam, "-a", symname);
		}
	}
}
#endif

static int
search_sym_tab(elf, shdr, scn, mem_offset, fname)
	Elf *elf;
	Elf32_Shdr *shdr;
	Elf_Scn *scn;
	long mem_offset;
	char *fname;
{
	Elf_Data 	*str_data, *sym_data; /* string table, symbol table */
	Elf_Scn  	*str_scn;
	Elf32_Shdr	*str_shdr;
	int 		no_of_symbols, counter;
	char 		*symname;
	Elf32_Sym 	*p;
	int 		symbol_bind;
	unsigned int 	index;

	str_scn = elf_getscn(elf, shdr->sh_link); /* index for string table */
	if (str_scn == NULL) {
		if (fname != NULL)
			fprintf(stderr, gettext(
"ar: %s has bad elf format.\n"), fname);
		else
			fprintf(stderr, gettext(
"ar: Bad elf format.\n"));
		num_errs++;
		return (-1);
	}
	str_shdr = elf32_getshdr(str_scn); /* section header for string table */
	if (str_shdr == NULL) {
		if (fname != NULL)
			fprintf(stderr, gettext(
"ar: %s has bad elf format.\n"), fname);
		else
			fprintf(stderr, gettext(
"ar: Bad elf format.\n"));
		num_errs++;
		return (-1);
	}
	if (shdr->sh_entsize)
		no_of_symbols = shdr->sh_size/shdr->sh_entsize;
	else {
		(void) fprintf(stderr, gettext(
"ar: Symbol table entry size is 0!\n"));
		return (-1);
	}

	/* This test must happen before testing the string table. */
	if (no_of_symbols == 1)
		return (0);	/* no symbols; 0th symbol is the non-symbol */

	if (str_shdr->sh_type != SHT_STRTAB) {
		if (fname != NULL)
			(void) fprintf(stderr, gettext(
"ar: %s has no string table for symbol names\n"), fname);
		else
			(void) fprintf(stderr, gettext(
"ar: No string table for symbol names\n"));
		return (0);
	}
	str_data = 0;
	if ((str_data = elf_getdata(str_scn, str_data)) == 0) {
		if (fname != NULL)
			(void) fprintf(stderr, gettext(
"ar: %s has no data in string table\n"), fname);
		else
			(void) fprintf(stderr, gettext(
"ar: No data in string table\n"));
		return (0);
	}
	if (str_data->d_size == 0) {
		if (fname != NULL)
			(void) fprintf(stderr, gettext(
"ar: %s has no data in string table - size is 0\n"), fname);
		else
			(void) fprintf(stderr, gettext(
"ar: No data in string table - size is 0\n"));
		return (0);
	}
	sym_data = 0;
	if ((sym_data = elf_getdata(scn, sym_data)) == NULL) {
		if (fname != NULL)
			(void) fprintf(stderr, gettext(
"ar: %s caused libelf error: %s\n"), fname, elf_errmsg(-1));
		else
			(void) fprintf(stderr, gettext(
"ar: libelf error: %s\n"), elf_errmsg(-1));
		return (0);
	}

	p = (Elf32_Sym *)sym_data->d_buf;
	p++; /* the first symbol table entry must be skipped */

	for (counter = 1; counter < no_of_symbols; counter++, p++) {
		symbol_bind = ELF32_ST_BIND(p->st_info);
		index = p->st_name;
		symname = (char *)(str_data->d_buf) + index;

		if (((symbol_bind == STB_GLOBAL) ||
			(symbol_bind == STB_WEAK)) &&
			(p->st_shndx != SHN_UNDEF)) {
			if (!syms_left) {
				sym_ptr = (long *)malloc((SYMCHUNK+1)
							*sizeof (long));
				if (sym_ptr == NULL) {
/*
 * TRANSLATION_NOTE  -- This is a message from ar.
 *	End of ELF format error.
 */
					fprintf(stderr, gettext(
"ar: Could not allocate memory.\n"));
					exit(1);
				}
				syms_left = SYMCHUNK;
				if (nextsym)
					*nextsym = (long) sym_ptr;
				else
					symlist = sym_ptr;
				nextsym = sym_ptr;
			}
			sym_ptr = nextsym;
			nextsym++;
			syms_left--;
			nsyms++;
			*sym_ptr = mem_offset;
			savename(symname); /* put name in the archiver's */
					/* symbol table string table */
		}
	}
	return (0);
}

/*
 * Used by xcmd()
 */
static int
create_extract(ARFILE *a, int rawname, int f_len)
{

	int f;
	char *f_name;
	char *dup = NULL;
	if (rawname)
		f_name = a->rawname;
	else
		f_name = a->longname;

	/*
	 * If -T is specified, check the file length.
	 */
	if (other_flgs & T_FLAG) {
		int len;
		len = strlen(f_name);
		if (f_len <= len) {
			dup = malloc(f_len+1);
			if (dup == NULL) {
				fprintf(stderr, gettext(
				"ar: Could not allocate memory.\n"));
				exit(1);
			}
			strncpy(dup, f_name, f_len);
		}
		f_name = dup;
	}

	/*
	 * If -C is specified, check the existense of the file.
	 */
	if (other_flgs & C_FLAG) {
		if (access(f_name, F_OK) != -1) {
			if (dup != NULL)
				free(dup);
			fprintf(stderr, gettext(
			"ar: %s already exists. Will not be extracted\n"),
			f_name);
			return (-1);
		}
	}
	f = creat(f_name, (mode_t)a->ar_mode & 0777);
	if (f < 0) {
		(void) fprintf(stderr, gettext(
		"ar: %s: cannot create file\n"), f_name);
		mesg('c', f_name);
	}
	if (dup)
		free(dup);
	return (f);
}
