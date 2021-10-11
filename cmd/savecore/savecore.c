/*
 * Copyright (c) 1980,1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 * The code that creates the namelist file with the module symbol tables
 * added (those modules present at the time of the crash) was written
 * at Sun Microsystems, Inc. All rights reserved.
 *
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)savecore.c	1.50	94/06/23 SMI"	/* UCB 5.8 5/26/86 */

/*
 * savecore - save kernel crash dump image.
 */

#include <stdlib.h>
#include <stdio.h>
#include <nlist.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/dumphdr.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <kvm.h>
#include <sys/kobj.h>
#include <sys/modctl.h>
#include <sys/systeminfo.h>
#include <libelf.h>

#define	COREFSIZE	100
#define	DEFPATHSIZE	200
#define	FD_MAX		200
#define	DAY	(60L*60L*24L)
#define	LEEWAY	(3*DAY)

#define	eq(a, b) (strcmp(a, b) == 0)
#define	ALIGN(x, a)	((a) == 0 ? (int)(x) : \
			(((int)(x) + (a) - 1) & ~((a) - 1)))

struct nlist current_nl[] = {	/* namelist for currently running system */
#define	X_VERSION	0
	{ "utsname" },
	{ "" },
};

int	cursyms[] =
	{ X_VERSION, -1 };

/*
 * data specific to a module, obtained from the core file.
 */
struct core_data {
	int	nglbs;		/* number of global symbols */
	int	nlcls;		/* number of local symbols */
	int	sym_size;	/* size of symbol table */
	int	str_size;	/* size of string table */
};

/*
 * list of modules loaded at the time of the crash.
 */
struct loaded {
	struct loaded *next;
	char		*path;		/* absolute path of located module */
	struct module	*mp;		/* module struct */
	char		*modname;	/* module name */
	struct core_data cd;		/* per module info from core file */
};

int 	read_dumphdr(void);
int	good_dumphdr(struct dumphdr *);
void	read_kmem(void);
void	nullterm(char *, int);
void	check_kmem(void);
int	get_crashtime(void);
char	*path(char *);
int	check_space(void);
int	read_number(char *);
int	save_core(void);
int	save_a_page(int, int, char *);
#ifdef	SAVESWAP
void	save_swap(int);
#endif	/* SAVESWAP */
void	clear_dump(void);
void	usage(int);
static void print_modname(const char *const);
int	build_namelist(int, char *);
void	get_mod_list(kvm_t *, u_long, char *, struct loaded **);
int	find_module(struct loaded *, char *);
kvm_t	*Kvm_open(char *, char *, char *, int);
void	Kvm_nlist(kvm_t *, struct nlist nl[]);
void	Kvm_read(kvm_t *, u_long, char *, u_int);
int	Open(char *, int);
int	Read(int, char *, int);
int	Create(char *, int);
void	Write(int, char *, int);
void	Fsync(int);
off_t	Lseek(int, off_t, int);
void 	*Malloc(size_t);

char	*sysname;			/* alternate name of kernel obj */
char	*dirname;			/* directory to save dumps in */
char	*ddname = "/dev/dump";		/* name of dump device */
int	dumpsize;			/* amount of memory dumped */
time_t	now;				/* current date */
char 	*progname;			/* argv[0] */
int	Verbose;
extern	int errno;
int	dfd;				/* dump file descriptor */
struct dumphdr	*dhp;			/* pointer to dump header */
int	pagesize;			/* dump pagesize */
int	fflag;
struct utsname utsname;
int	damnit;

main(int argc, char *argv[])
{
	register int c, opt;
	register int error = 0;

	progname = argv[0];
	while ((c = getopt(argc, argv, "vdf:")) != EOF) {
		switch (c) {
		case 'v':
			Verbose++;
			break;
		case 'd':	/* ignore dump time and valid flag - private */
			damnit = 1;
			break;
		case 'f':	/* Undocumented! */
			ddname = optarg;
			break;
		case '?':
			error++;
			break;
		}
		if (error != 0) {
			usage(c);
			return (1);
		}
	}
	opt = argc - optind;
	if (opt < 1 || opt > 2) {
		usage(0);
		return (1);
	} else {
		dirname = argv[optind];
		if (opt == 2)
			sysname = argv[optind + 1];
		else {
			static char kernname[MAXPATHLEN];
			char platform[MAXNAMELEN];

			if (sysinfo(SI_PLATFORM, platform,
			    sizeof (platform)) == -1) {
				(void) fprintf(stderr, "%s: could not get "
				    "platform name.\n", progname);
			}
			(void) sprintf(kernname, "/platform/%s/kernel/unix",
			    platform);
			sysname = kernname;
		}
	}
	openlog(progname, LOG_ODELAY, LOG_AUTH);
	if (access(dirname, W_OK) < 0) {
		int oerrno = errno;

		perror(dirname);
		errno = oerrno;
		syslog(LOG_ERR, "%s: %m", dirname);
		return (1);
	}
	if (read_dumphdr() == 0) {
		if (Verbose)
			(void) fprintf(stderr, "%s: No dump exists.\n",
			    progname);
		return (0);
	}
	read_kmem();
	(void) time(&now);
	check_kmem();
	if (*dump_panicstring(dhp) != 0)
		syslog(LOG_CRIT, "reboot after panic: %s",
			dump_panicstring(dhp));
	else
		syslog(LOG_CRIT, "reboot");
	if (get_crashtime() == 0 || check_space() == 0)
		return (1);
	if (save_core() != 0)
		clear_dump();
	(void) close(dfd);
	return (0);
}

int
read_dumphdr(void)
{
	struct dumphdr	dumphdr1;
	struct dumphdr	*dhp1 = &dumphdr1;

	pagesize = sysconf(_SC_PAGESIZE);		/* first guess */

	dfd = Open(ddname, fflag ? O_RDONLY : O_RDWR);	/* dump */
	(void) Lseek(dfd, -pagesize, SEEK_END);
	(void) Read(dfd, (char *)dhp1, sizeof (*dhp1)); /* read the header */
	dhp = dhp1;				/* temporarily */

	/* Check that this is a valid header */
	if (!good_dumphdr(dhp1))
		return (0);

	/*
	 * So far, so good.  Reposition file, read real header, and
	 * compare.
	 */
	pagesize = dhp1->dump_pagesize;		/* get dump pagesize */
	dhp = malloc(dhp1->dump_headersize);
	if (dhp == (struct dumphdr *)0) {
		(void) fprintf(stderr, "%s: Can't allocate dumphdr buffer.\n",
				progname);
		return (0);
	}
	(void) Lseek(dfd, -(dhp1->dump_dumpsize), SEEK_END);
	(void) Read(dfd, (char *)dhp, (int)dhp1->dump_headersize);

	if (memcmp(dhp, dhp1, sizeof (*dhp)) && !damnit)
		printf("memcmp failed\n");

	if (!good_dumphdr(dhp))
		return (0);

	return (1);
}

/*
 * Check for a valid header:
 * 1. Valid magic number
 * 2. A version number we understand.
 * 3. "dump valid flag" on
 */
int
good_dumphdr(struct dumphdr *dhp)
{
	if (dhp->dump_magic != DUMP_MAGIC) {
		if (Verbose)
			(void) fprintf(stderr,
			    "magic number mismatch: 0x%lx != 0x%lx\n",
			    dhp->dump_magic, DUMP_MAGIC);
		return (0);
	}

	if (dhp->dump_version > DUMP_VERSION) {
		(void) fprintf(stderr,
		    "Warning: %s version (%d) is older than dumpsys "
		    "version (%ld)\n",
		    progname, DUMP_VERSION, dhp->dump_version);
	}

	if (dhp->dump_pagesize != pagesize) {
		(void) fprintf(stderr,
		    "Warning: dump pagesize (%ld) != system pagesize (%d)\n",
		    dhp->dump_pagesize, pagesize);
	}

	if (((dhp->dump_flags & DF_VALID) == 0) && !damnit) {
		if (Verbose)
			(void) fprintf(stderr,
			    "dump already processed by %s.\n", progname);
		return (0);
	}

	/* no fatal errors */
	return (1);
}

void
read_kmem(void)
{
	int i;
	kvm_t	*lkd;

	lkd = Kvm_open(sysname, NULL, NULL, O_RDONLY);
	Kvm_nlist(lkd, current_nl);

	/*
	 * Check that all the names we need are there
	 */
	for (i = 0; cursyms[i] != -1; i++)
		if (current_nl[cursyms[i]].n_value == 0) {
			(void) fprintf(stderr, "%s: %s not in namelist",
			    sysname, current_nl[cursyms[i]].n_name);
			syslog(LOG_ERR, "%s: %s not in namelist",
			    sysname, current_nl[cursyms[i]].n_name);
			exit(1);
		}
	if (sysname == NULL) {
		Kvm_read(lkd, current_nl[X_VERSION].n_value,
		    (char *)&utsname, sizeof (utsname));
		nullterm(utsname.version, sizeof (utsname.version));
	}
	(void) kvm_close(lkd);
}

void
nullterm(char *s, int len)
{
	s[len - 1] = '\0';
	while (*s++ != '\n') {
		if (*s == '\0')
			return;
	}
	*s = '\0';
}

void
check_kmem(void)
{
	nullterm(dump_versionstr(dhp), strlen(dump_versionstr(dhp)) + 1);
	if (!eq(utsname.version, dump_versionstr(dhp)) && sysname == 0) {
		(void) fprintf(stderr,
		    "Warning: kernel version mismatch:\n\t%sand\n\t%s",
		    utsname.version, dump_versionstr(dhp));
		syslog(LOG_WARNING,
		    "Warning: kernel version mismatch: %s and %s",
		    utsname.version, dump_versionstr(dhp));
	}
}

int
get_crashtime(void)
{
	if (dhp->dump_crashtime.tv_sec == 0) {
		if (Verbose)
			(void) fprintf(stderr, "Dump time not found.\n");
		return (0);
	}
	(void) fprintf(stdout, "System went down at %s",
	    ctime(&dhp->dump_crashtime.tv_sec));

	if (!damnit && (dhp->dump_crashtime.tv_sec < now - LEEWAY ||
	    dhp->dump_crashtime.tv_sec > now + LEEWAY)) {

		(void) fprintf(stderr, "dump time is unreasonable\n");
		return (0);
	}
	return (1);
}

char *
path(char *file)
{
	register char *cp;

	cp = Malloc((u_int)(strlen(file) + strlen(dirname) + 2));
	(void) strcpy(cp, dirname);
	(void) strcat(cp, "/");
	(void) strcat(cp, file);
	return (cp);
}

int
check_space(void)
{
	struct statvfs fsb;
	longlong_t spacefree;
	longlong_t dumpsize;
	longlong_t minfree;

	if (statvfs(dirname, &fsb) < 0) {
		int oerrno = errno;

		perror(dirname);
		errno = oerrno;
		syslog(LOG_ERR, "%s: %m", dirname);
		exit(1);
	}

	/*
	 * From savecore(1M):
	 *
	 * "Before savecore writes out a core image, it reads a number
	 * from the file directory/minfree.  This is the minimum number
	 * of kilobytes that must remain free on the file system con-
	 * taining directory.  If there is less free space on the file
	 * system containing directory than the number of kilobytes
	 * specified in minfree, the core dump is not saved.  If the
	 * minfree file does not exist, savecore always writes out the
	 * core file (assuming that a core dump was taken)."
	 *
	 * Historical note:
	 *
	 * All 4.x and early 5.x releases ignored the second sentence
	 * above, and implemented only the third sentence i.e. 'minfree'
	 * was treated as the number of Kbytes that should be available
	 * on the filesystem without including the size of the dump,
	 * rather than the minimum left -after- taking the dump.
	 *
	 * See 2001305, 1099788 and 1094160.
	 */

	spacefree = fsb.f_bavail * fsb.f_frsize;
	minfree = 1024LL * read_number("minfree");
	dumpsize = dhp->dump_nchunks * dhp->dump_chunksize
	    * dhp->dump_pagesize;

	if (minfree > 0 && (spacefree - dumpsize) < minfree) {
		(void) fprintf(stderr,
		    "%s: Dump omitted, not enough space on device\n",
		    progname);
		syslog(LOG_WARNING,
		    "Dump omitted, not enough space on device");
		return (0);
	}

	if (spacefree < dumpsize) {
		(void) fprintf(stderr,
		    "%s: Dump will be saved, but free space threshold "
		    "will be crossed\n", progname);
		syslog(LOG_WARNING,
		    "Dump will be saved, but free space threshold will "
		    "be crossed");
	}
	return (1);
}

int
read_number(char *fn)
{
	char lin[80];
	register FILE *fp;

	fp = fopen(path(fn), "r");
	if (fp == NULL)
		return (0);
	if (fgets(lin, 80, fp) == NULL) {
		(void) fclose(fp);
		return (0);
	}
	(void) fclose(fp);
	return (atoi(lin));
}

/*
 * First,  save the core image. Then build the namelist file, complete
 * with module symbol tables.
 */
int
save_core(void)
{
	char corefile[COREFSIZE];
	register char *cp;
	register int ifd, ofd, bounds;
	register FILE *fp;
	int saved = 0;
	int	nchunks;
	int	bitmapsize;

	cp = Malloc((u_int)pagesize);
	if (cp == 0) {
		(void) fprintf(stderr,
			"%s: Can't allocate i/o buffer.\n", progname);
		return (0);
	}
	bounds = read_number("bounds");
	/*
	 * Calculate some constants
	 */
	nchunks = dhp->dump_nchunks;
	dumpsize = nchunks * dhp->dump_chunksize;
	bitmapsize = howmany(dhp->dump_bitmapsize, pagesize);

	/*
	 * We'll save everything, including the bit map
	 */
	ifd = dfd;
	(void) Lseek(ifd, -(dhp->dump_dumpsize), SEEK_END);

	(void) sprintf(cp, "vmcore.%d", bounds);
	(void) strcpy(corefile, path(cp));
	ofd = Create(corefile, 0644);
	(void) fprintf(stdout, "Saving %d pages of image in vmcore.%d\n",
			dumpsize, bounds);
	syslog(LOG_NOTICE, "Saving %d pages of image in vmcore.%d\n",
		dumpsize, bounds);

	/* Save the first header */
	if (!save_a_page(ifd, ofd, cp))
		goto error;

	/* Save the bitmap */
	for (; bitmapsize > 0; bitmapsize--) {
		if (!save_a_page(ifd, ofd, cp))
			break;
	}

	/* Save the chunks */
	for (; dumpsize > 0; dumpsize--) {
		if (!save_a_page(ifd, ofd, cp))
			break;
		if ((++saved & 07) == 0) {	/* every 8 pages */
			(void) fprintf(stdout, "%6d pages saved\r",
					saved);
			(void) fflush(stdout);
		}
	}

	/* Save the last header */
	(void) save_a_page(ifd, ofd, cp);

error:
	(void) fprintf(stdout, "%6d pages saved.\n", saved);

	Fsync(ofd);
	(void) close(ofd);
	free(cp);

	/*
	 * We have the core file. Build a namelist file w/ all the modules
	 * symbol tables.
	 */
	if (build_namelist(bounds, corefile) != 0) {
		(void) fprintf(stderr,
		    "%s: error building namelist file.\n", progname);
		return (0);
	}

#ifdef SAVESWAP
	/*
	 * Now save the swapped out uareas into vmswap.N
	 */
	save_swap(bounds);
#endif SAVESWAP

	fp = fopen(path("bounds"), "w");
	if (fp == NULL) {
		(void) fprintf(stderr, "%s: ", progname);
		perror("cannot update \"bounds\" file");
	} else {
		(void) fprintf(fp, "%d\n", bounds+1);
		(void) fclose(fp);
	}

	/* success! */
	return (1);
}

int
save_a_page(int ifd, int ofd, char *cp)
{
	register int	n;

	n = Read(ifd, cp, pagesize);
	if (n == 0) {
		syslog(LOG_WARNING, "WARNING: vmcore may be incomplete\n");
		(void) fprintf(stderr,
		    "WARNING: vmcore may be incomplete\n");
		return (0);
	}
	if (n <= 0) {
		syslog(LOG_WARNING,
		    "WARNING: Dump area was too small - %d pages not saved",
			dumpsize);
		(void) fprintf(stderr,
		    "WARNING: Dump area was too small - %d pages not saved\n",
			dumpsize);
		return (0);
	}
	Write(ofd, cp, n);
	return (1);
}

#ifdef SAVESWAP
#include <sys/proc.h>
#include <sys/user.h>
#include <vm/anon.h>
#include <machine/seg_u.h>

void
save_swap(int bounds)
{
	int	ifd, ofd;
	char	*cp, *Malloc(u_int);
	char	*vmunix, *vmcore, *vmswap;
	kvm_t	*kd;
	struct proc	*proc;
	int	nuptes;		/* number of ptes to map uarea */

	nuptes = roundup(sizeof (struct user), pagesize) / pagesize;

	cp = Malloc(pagesize);		/* get a buffer to work with */
	(void) sprintf(cp, "unix.%d", bounds);
	vmunix = path(cp);
	(void) sprintf(cp, "vmcore.%d", bounds);
	vmcore = path(cp);
	(void) sprintf(cp, "vmswap.%d", bounds);
	vmswap = path(cp);

	ifd = Open("/dev/drum", O_RDONLY);
	ofd = Create(vmswap, 0644);
	kd = Kvm_open(vmunix, vmcore, NULL, O_RDONLY);

	/*
	 * For each proc that is swapped out,
	 * copy its user pages from /dev/drum to vmswap.N.
	 * Note that this code is liberally borrowed from kvmgetu.c in
	 * libkvm, and even uses an internal libkvm function!
	 */
	/* XXX This is all wrong for 4.1; needs to be changed XXX */
	for (kvm_setproc(kd); proc = kvm_nextproc(kd); /* void */) {
		struct segu_data sud;
		struct anon **app = &sud.su_swaddr[0];
		int cc;

		/* Skip loaded processes */
		if ((proc->p_flag & SLOAD) != 0)
			continue;

		/*
		 * u-area information lives in the segu data structure
		 * pointed to by proc->p_useg.  Obtain the contents of
		 * this structure before worrying about whether or not
		 * the u-area is swapped out.
		 */
		if (kvm_read(kd, (u_long)proc->p_useg, (char *)&sud,
		    sizeof (sud))
		    != sizeof (sud)) {
			(void) fprintf(stderr,
			    "%s: couldn't read seg_u for pid %d\n",
			    progname, proc->p_pid);
			syslog(LOG_WARNING,
			    "couldn't read seg_u for pid %d\n",
			    proc->p_pid);
			continue;
		}

		/*
		 * u-area is swapped out.  proc->p_useg->su_swaddr[i]
		 * contains an anon pointer for the swap space holding
		 * the corresponding u page.  Note that swap space for
		 * a given u-area is not guaranteed to be contiguous.
		 */

		for (cc = 0; cc < nuptes; cc++) {
			long swapoffset;
			addr_t /* really a (struct vnode *) */ vp;
			u_int off;

			/* XXX _anonoffset is an internal libkvm function! */
			swapoffset = _anonoffset(kd, app[cc], &vp, &off);
			if (swapoffset == -1) {
				(void) fprintf(stderr,
				    "%s: couldn't find uarea for pid %d\n",
				    progname, proc->p_pid);
				syslog(LOG_WARNING,
				    "couldn't find uarea for pid %d\n",
				    proc->p_pid);
				continue;
			}
			lseek(ifd, swapoffset, 0);
			lseek(ofd, swapoffset, 0);
			read(ifd, cp, pagesize);
			write(ofd, cp, pagesize);
		}
	}
	/* End of stolen code */

	(void) close(ifd);
	Fsync(ofd);
	(void) close(ofd);
	free(cp);
	free(vmunix);
	free(vmcore);
	free(vmswap);
	kvm_close(kd);
}
#endif SAVESWAP

void
clear_dump(void)
{
	if (fflag)		/* Don't clear if overridden */
		return;
	(void) Lseek(dfd, -pagesize, SEEK_END);	/* Seek to last header */
	dhp->dump_flags &= ~DF_VALID;	/* turn off valid bit */
	Write(dfd, (char *)dhp, sizeof (*dhp));	/* re-write the header */
}

void
usage(int c)
{
	if (c == 'f')
		(void) fprintf(stderr, "-f requires dumpfile name");
	(void) fprintf(stderr, "usage: %s [-v] dirname [ system ]\n",
	    progname);
}

/*
 * Prints the module names at the time of crash in an orderly way.
 * Called only in the verbose mode.
 */
static void
print_modname(const char *const modname)
{
	static int mod_count = 0;

	(void) fprintf(stdout, "\t%-20s%c", modname,
			(++mod_count % 3) ? NULL : '\n');
	fflush(stdout);
}

/*
 * Using information from our core file, create a namelist file (with the
 * help of /dev/ksyms) that contains all the symbol information for all
 * modules loaded in the system at the time of the crash.
 */
int
build_namelist(int bounds, char *corefile)
{
	/* The two symbols we're interested in */
	static struct nlist nd[] = {
		{ "default_path", 0, 0, 0, 0, 0 },
		{ "modules", 0, 0, 0, 0, 0 },
		{ "", 0, 0, 0, 0, 0 },
	};
	Elf32_Sym 	*symbols;	/* temporary symbol table */
	Elf32_Sym 	*locals;	/* start of locals in temporary */
	int		nlocals;	/* number of local symbols */
	Elf32_Sym 	*globals;	/* start of globals in temporary */
	int		nglobals;	/* number of nonlocal symbols */
	int		loc_size;	/* total size of combined locals */
	int		glb_size;	/* total size of combined globals */
	int		str_size;	/* total size of string table */
	int		i_nld, o_nld;	/* i/o namelist descriptors */
	int		mod_fd;		/* module file descriptor */
	Elf32_Shdr	*shp;		/* used for relocation */
	Elf32_Shdr 	*shdr;		/* used for libelf routines */
	register int	cnt;		/* temp counter */
	Elf 		*elf;		/* output file's elf desc */
	Elf		*mod_ed;	/* current module's elf desc */
	Elf32_Ehdr 	*hdr;		/* temp elf hdr */
	Elf_Scn 	*sec, *sym_sec = 0, *str_sec = 0;
	Elf_Scn		*mod_symsec, *mod_strsec;
	Elf_Scn		*dyn_symsec, *dyn_strsec;
	Elf_Data	*mod_symdata, *mod_strdata, *sym_data, *str_data;
	Elf32_Sym	*loc_p, *glb_p, *tp;	/* symbol processing */
	Elf32_Sym	*wlk_sym;	/* ditto */
	char		*strings;	/* temporary string table */
	char		*wlk_str;	/* string table processing */
	Elf32_Sym	*wlk_symbols;	/* coalesced symtab processing */
	char		*symname;	/* symbol name */
	char		*str_p;		/* working strings table ptr */
	u_int		str_ndx = 1;	/* index into string table */
	register char 	*sect_name;	/* Elf section name */
	kvm_t 		*kd;		/* kvm_t descriptor for namelist */
	char 		*ptr;		/* buffer for lvalue */
	char 		*tbuf;		/* scratch */
	char 		namelist_buf[10];	/* namelist name buffer */
	char 		*namelist = &namelist_buf[0];
	char 		default_path[DEFPATHSIZE];
	register struct loaded *wlk;	/* list walking ptr for loaded */
	struct loaded *head = (struct loaded *)0;
	u_int		bss;

	kd = Kvm_open(sysname, corefile, (char *)0, O_RDONLY);
	Kvm_nlist(kd, nd);
	if (nd[0].n_type == 0 || nd[1].n_type == 0) {
		(void) fprintf(stderr, "%s: nlist of symbols: "
		    "'default_path' and 'modules' in namelist "
		    "file '%s' failed.\n", progname, sysname);
		return (-1);
	}

	/*
	 * get default_path.
	 */
	Kvm_read(kd, nd[0].n_value, (char *)&ptr, sizeof (char *));
	if (kvm_read(kd, (u_long)ptr, &default_path[0], DEFPATHSIZE) < 0) {
		(void) fprintf(stderr, "%s: kernel read error\n",
		    progname);
		syslog(LOG_ERR, "kernel read error");
		exit(1);
	}

	/*
	 * get list of accessable modules.
	 */
	get_mod_list(kd, nd[1].n_value, default_path, &head);
	kvm_close(kd);

	/* get size totals for non-kernel modules */
	loc_size = glb_size = str_size = nlocals = nglobals = 0;
	for (wlk = head; wlk != (struct loaded *)0; wlk = wlk->next) {
		if (wlk->mp == (struct module *)0)
			continue;	/* failed to read module */
		nlocals += wlk->cd.nlcls;
		nglobals += wlk->cd.nglbs;
		loc_size += (wlk->cd.sym_size -
		    (wlk->cd.nglbs * sizeof (Elf32_Sym)));
		glb_size += (wlk->cd.sym_size -
		    (wlk->cd.nlcls * sizeof (Elf32_Sym)));
		str_size += wlk->cd.str_size;
	}
	if (((loc_size + glb_size) % sizeof (Elf32_Sym)) != 0) {
		(void) fprintf(stderr,
		    "%s: symbol table size is not a multiple of entry size.\n",
		    progname);
		return (-1);
	}

	locals = Malloc(loc_size);
	loc_p = locals;
	globals = Malloc(glb_size);
	glb_p = globals;
	strings = Malloc(str_size);
	str_p = strings;

	/* set up for undefined symbols */
	*str_p++ = '\0';

#ifdef	DEBUG
	(void) fprintf(stdout, "locals: 0x%x, globals: 0x%x, strings: 0x%x\n",
	    locals, globals, strings);
	(void) fprintf(stdout, "size of locals in symbol table: 0x%x\n",
	    loc_size);
	(void) fprintf(stdout, "size of globals in symbol table: 0x%x\n",
	    glb_size);
	(void) fprintf(stdout, "size of string table: 0x%x\n", str_size);
#endif	/* DEBUG */

	/*
	 * for each module in the list, open the module file, access
	 * it's symbol/string setions, transfer locals/globals to the
	 * image. relocate symbol values to load address. Toss any
	 * undefined symbols (We hope they're defined in the kernel
	 * anyway)
	 */
	if (Verbose)
		(void) fprintf(stdout,
		    "Modules loaded at the time of crash:\n");
	for (wlk = head; wlk != (struct loaded *)0; wlk = wlk->next) {
		if (wlk->mp == (struct module *)0)
			continue;	/* failed to find module */
		if (Verbose)
			print_modname(wlk->modname);
		if ((mod_fd = open(wlk->path, O_RDONLY)) < 0) {
			(void) fprintf(stderr,
			    "%s: Warning: cannot open module '%s': ",
			    progname, wlk->path);
			perror(wlk->path);
			syslog(LOG_WARNING, "cannot open module '%s': %m",
			    wlk->path);
			continue;	/* skip it */
		}
		elf_version(EV_CURRENT);
		if ((mod_ed = elf_begin(mod_fd, ELF_C_READ, (Elf *)0)) ==
		    0) {
			(void) fprintf(stderr,
			    "%s: Warning: can't get ELF descriptor for %s\n",
			    progname, wlk->path);
			syslog(LOG_WARNING,
			    "can't get ELF descriptor for %s", wlk->path);
			(void) close(mod_fd);
			continue;
		}
		if (elf_kind(mod_ed) != ELF_K_ELF) {
			(void) fprintf(stderr,
			    "%s: Warning: '%s'is not an ELF file.\n",
			    progname, wlk->path);
			syslog(LOG_WARNING, "'%s'is not an ELF file.",
			    wlk->path);
			elf_end(mod_ed);
			(void) close(mod_fd);
			continue;
		}
		hdr = elf32_getehdr(mod_ed);
		mod_symsec = mod_strsec = (Elf_Scn *)0;
		dyn_symsec = dyn_strsec = (Elf_Scn *)0;
		for (cnt = 1, sec = elf_getscn(mod_ed, cnt);
		    (shdr = elf32_getshdr(sec)) != 0;
		    sec = elf_nextscn(mod_ed, sec), cnt++) {
			if (shdr->sh_type != SHT_SYMTAB &&
			    shdr->sh_type != SHT_STRTAB &&
			    shdr->sh_type != SHT_DYNSYM)
				continue;
			sect_name = elf_strptr(mod_ed, hdr->e_shstrndx,
			    shdr->sh_name);

			if (eq(sect_name, ".dynsym"))
				dyn_symsec = sec;
			if (eq(sect_name, ".dynstr"))
				dyn_strsec = sec;
			if (eq(sect_name, ".symtab"))
				mod_symsec = sec;
			if (eq(sect_name, ".strtab"))
				mod_strsec = sec;
		}
		/*
		 * Use dynamic sections.
		 * XXX We make sure that shdrs is NULL.
		 * If the kernel linker used the dynamic symbol table
		 * that was mapped into the image, then it won't
		 * have section header information.
		 */
		if (wlk == head && wlk->mp->shdrs == NULL &&
		    dyn_symsec && dyn_strsec) {
			mod_symsec = dyn_symsec;
			mod_strsec = dyn_strsec;
		}
		if (mod_symsec == (Elf_Scn *)0 ||
		    mod_strsec == (Elf_Scn *)0) {
			(void) fprintf(stderr,
			    "%s: Warning: '%s' is missing symbol "
			    "or string table(s).\n", progname, wlk->path);
			syslog(LOG_WARNING,
			    "'%s' is missing symbol or string table(s).",
			    wlk->path);
			elf_end(mod_ed);
			(void) close(mod_fd);
			continue;
		}
		mod_symdata = elf_getdata(mod_symsec, (Elf_Data *)0);
		if (mod_symdata == 0 || mod_symdata->d_size == 0) {
			(void) fprintf(stderr, "%s: Warning: '%s' has an "
			    "empty symbol table section.\n",
			    progname, wlk->path);
			syslog(LOG_WARNING,
			    "'%s' has an empty symbol table section.",
			    wlk->path);
			elf_end(mod_ed);
			(void) close(mod_fd);
			continue;
		}
		mod_strdata = elf_getdata(mod_strsec, (Elf_Data *)0);
		if (mod_strdata == 0 || mod_strdata->d_size == 0) {
			(void) fprintf(stderr, "%s: Warning: '%s' has an "
			    "empty string table section.\n",
			    progname, wlk->path);
			syslog(LOG_WARNING,
			    "'%s' has an empty string table section.",
			    wlk->path);
			elf_end(mod_ed);
			(void) close(mod_fd);
			continue;
		}
		wlk_str = mod_strdata->d_buf;
		if (mod_strdata->d_size != wlk->cd.str_size) {
			(void) fprintf(stderr,
			    "%s: Warning: mismatched core/file string "
			    "table: %s\n", progname, wlk->modname);
			syslog(LOG_WARNING,
			    "mismatched core/file string table: '%s'",
			    wlk->modname);
			elf_end(mod_ed);
			(void) close(mod_fd);
			continue;
		}
		wlk_sym = mod_symdata->d_buf;
		if (mod_symdata->d_size != wlk->cd.sym_size) {
			(void) fprintf(stderr,
			    "%s: Warning: mismatched core/file symbol "
			    "table: %s\n", progname, wlk->modname);
			syslog(LOG_WARNING,
			    "mismatched core/file symbol table: '%s'",
			    wlk->modname);
			elf_end(mod_ed);
			(void) close(mod_fd);
			continue;
		}
		bss = ALIGN(wlk->mp->bss, wlk->mp->bss_align);
		/*
		 * transfer the symbols and strings...
		 */
		for (cnt = 0; cnt < wlk->mp->nsyms; ++cnt) {
			symname = (char *)((u_int)wlk_str +
			    (u_int)wlk_sym->st_name);
			if (ELF32_ST_BIND(wlk_sym->st_info) == STB_LOCAL) {
				/*
				 * locals first...
				 *
				 * We need the first "UNDEF" in the
				 * kernel file to satisfy the elf format.
				 */
				if ((wlk_sym->st_shndx != SHN_UNDEF) ||
				    (wlk == head && cnt == 0)) {
					/* struct copy */
					*loc_p = *wlk_sym++;
					tp = loc_p;
					loc_p++;
				} else {
					/* undefined. toss it */
					wlk_sym++;
					continue;
				}
			} else {
				/* then the globals.... */
				if (wlk_sym->st_shndx != SHN_UNDEF) {
					/* struct copy */
					*glb_p = *wlk_sym++;
					tp = glb_p;
					glb_p++;
				} else {
					/* undefined. toss it */
					wlk_sym++;
					continue;
				}
			}
			/*
			 * Relocate symbol value to previous load addr.
			 * But don't adjust values in kernel module.
			 */
			if (wlk != head) {
				if (tp->st_shndx < SHN_LORESERVE) {
					shp = (Elf32_Shdr *)
					    ((u_long)wlk->mp->shdrs +
					    (tp->st_shndx *
					    wlk->mp->hdr.e_shentsize));
					tp->st_value += shp->sh_addr;
				}
				if (tp->st_shndx == SHN_COMMON) {
					Elf32_Sym *ep, *sp;

					/*
					 * Scan for an allocated copy of
					 * this COMMON symbol.
					 * XXX This is *horribly* slow,
					 * should use a hash table.
					 */
					ep = glb_p - 1;
					for (sp = globals; sp < ep; sp++) {
						if (sp->st_name && symname &&
						    eq(strings + sp->st_name,
						    symname)) {
							tp->st_shndx = SHN_ABS;
							tp->st_value =
							    sp->st_value;
							break;
						}
					}
					/*
					 * Didn't find it, calculate it.
					 */
					if (sp >= ep) {
						bss = ALIGN(bss, tp->st_value);
						tp->st_value = bss;
						bss += tp->st_size;
						tp->st_shndx = SHN_ABS;
					}
				}
			}
			/* update string table */
			if (tp->st_name != 0) {
				tp->st_name = str_ndx;
				while (*symname != '\0') {
					*str_p = *symname++;
					str_p++;
					str_ndx++;
				}
				/* terminate the string with a null. */
				*str_p = '\0';
				str_p++;	/* step over the null char */
				str_ndx++;
			}
		}
		/* we're done with this module. Free it's data. */
		(void) elf_end(mod_ed);
		if (wlk != head) {
			/* then these are dynamically allocated. */
			free(wlk->path);
			if (*wlk->modname != '/')
				free(wlk->modname);
		}
		if (wlk->mp->shdrs)
			free(wlk->mp->shdrs);
		free(wlk->mp);
		(void) close(mod_fd);
	}

	/* free list */
	for (wlk = head; wlk != (struct loaded *)0; wlk = wlk->next)
		free(wlk);

	(void) fprintf(stdout, "\nProcessing modules:  Done.\n");

	/*
	 * We probably threw away some symbols and their associated strings.
	 * Coalesce the remaining locals and globals into one symbol
	 * table.
	 */

	/* Reset symbol / string counts to reflect syms/strs remaining */
	loc_size = ((u_long)loc_p - (u_long)locals);
	glb_size = ((u_long)glb_p - (u_long)globals);
	str_size = (u_long)str_p - (u_long)strings;

	symbols = Malloc(loc_size + glb_size);
	wlk_symbols = symbols;

	(void) memcpy(wlk_symbols, locals, loc_size);
	wlk_symbols = (Elf32_Sym *)((u_long)wlk_symbols + (u_long)loc_size);
	(void) memcpy(wlk_symbols, globals, glb_size);

	/* free up symbol buffers. We don't need them now */
	free(locals);
	free(globals);

	/*
	 * We have what we need. Create the namelist file, substitute our
	 * symbol and string tables for the kernel's, and update our
	 * namelist file.
	 */
	i_nld = Open(sysname, O_RDONLY);
	(void) sprintf(namelist, "unix.%d", bounds);
	namelist = path(namelist);
	(void) fprintf(stdout, "Constructing Namelist file: %s\n", namelist);
	syslog(LOG_NOTICE, "Constructing Namelist file: %s", namelist);
	o_nld = Create(namelist, 0644);
	tbuf = Malloc((unsigned)pagesize);
	while ((cnt = Read(i_nld, tbuf, pagesize)) > 0)
		Write(o_nld, tbuf, cnt);
	(void) close(i_nld);
	Fsync(o_nld);
	(void) close(o_nld);
	free(tbuf);

	/* reopen namelist file - RDWR */
	o_nld = Open(namelist, O_RDWR);

	elf_version(EV_CURRENT);
	(void) elf_errno();
	if ((elf = elf_begin(o_nld, ELF_C_RDWR, (Elf *)0)) == 0) {
		(void) fprintf(stderr,
		    "%s: cannot get ELF descriptor for %s (%s)\n",
		    progname, namelist, elf_errmsg(elf_errno()));
		return (-1);
	}
	if (elf_kind(elf) != ELF_K_ELF) {
		(void) fprintf(stderr, "%s: '%s' is not an ELF file.\n",
		    progname, sysname);
		return (-1);
	}
	hdr = elf32_getehdr(elf);

	for (cnt = 1, sec = elf_getscn(elf, cnt); /* continued line */
	    (shdr = elf32_getshdr(sec)) != 0; sec = elf_nextscn(elf, sec),
	    cnt++) {
		if (shdr->sh_type != SHT_SYMTAB &&
		    shdr->sh_type != SHT_STRTAB)
			continue;
		sect_name = elf_strptr(elf, hdr->e_shstrndx, shdr->sh_name);
		if (eq(sect_name, ".symtab"))
			sym_sec = sec;
		if (eq(sect_name, ".strtab"))
			str_sec = sec;
	}
	if (sym_sec == (Elf_Scn *)0 || str_sec == (Elf_Scn *)0) {
		(void) fprintf(stderr,
		    "%s: '%s' is missing symbol or string table(s).\n",
		    progname, sysname);
		return (-1);
	}
	sym_data = elf_getdata(sym_sec, (Elf_Data *)0);
	if (sym_data == 0 || sym_data->d_size == 0) {
		(void) fprintf(stderr,
		    "%s: '%s' has an empty symbol table section.\n",
		    progname, sysname);
		return (-1);
	}
	str_data = elf_getdata(str_sec, (Elf_Data *)0);
	if (str_data == 0 || str_data->d_size == 0) {
		(void) fprintf(stderr,
		    "%s: '%s' has an empty string table section.\n",
		    progname, sysname);
		return (-1);
	}
	sym_data->d_buf = symbols;
	sym_data->d_size = loc_size + glb_size;
	str_data->d_buf = strings;
	str_data->d_size = str_size;
	(void) elf_flagdata(sym_data, ELF_C_SET, ELF_F_DIRTY);
	(void) elf_flagdata(str_data, ELF_C_SET, ELF_F_DIRTY);
	(void) elf_update(elf, ELF_C_WRITE);
	(void) elf_end(elf);
	(void) close(o_nld);
	(void) fprintf(stdout, "Namelist file complete.\n");
	syslog(LOG_NOTICE, "Namelist file complete.");
	return (0);
}

/*
 * Get the modctl list from the core file. We need some information from
 * the core image about the module to handle relocation. We need the
 * virtual addresses/contents of each module's section headers.
 *
 * This information is contained in the module's struct module. We'll
 * need to get the "name" of the module - but the pointers contained in
 * the module struct will give us the values we need. Use the loaded
 * struct to store our list.
 *
 * We also need to locate our modules.
 */
void
get_mod_list(kvm_t *kd, u_long value, char *search, struct loaded **hd)
{
	struct modctl 		tmp, *modctl_head, *modctl_walk;
	struct loaded 		*wlk;
	Elf32_Shdr 		tmp_shdr;
	register char 		*cp;
	register char 		*module_name;
	register int		shdrs_size;

	/*
	 * get the head of the list.
	 */
	Kvm_read(kd, value, (char *)&tmp, sizeof (tmp));

	if (tmp.mod_id != 0) { /* sanity check */
		(void) fprintf(stderr,
		    "%s: FATAL: struct modctl list format not recognized. "
		    "Mismatched %s/%s?\n", progname, progname, sysname);
		syslog(LOG_CRIT, "struct modctl list format not "
		    "recognized. Mismatched %s/%s?", progname, sysname);
		exit(1);
	}

	modctl_head = &tmp;
	modctl_walk = modctl_head;

	*hd = Malloc(sizeof (struct loaded));
	(void) memset(*hd, 0, sizeof (struct loaded));
	wlk = *hd;
	for (;;) {
		if (modctl_walk->mod_mp == 0)
			goto skip;
		wlk->mp = Malloc(sizeof (struct module));

		/* get module struct */
		Kvm_read(kd, (u_long)modctl_walk->mod_mp, (char *)wlk->mp,
		    sizeof (struct module));

		module_name = Malloc(MOD_MAXPATH);
		if (kvm_read(kd, (u_long)modctl_walk->mod_filename,
		    module_name, MOD_MAXPATH) < 0) {
			(void) fprintf(stderr, "%s: kvm_read error\n",
			    progname);
			syslog(LOG_ERR, "kernel read error");
			exit(1);
		}
		if (modctl_walk->mod_id == 0) {
			/*
			 * We will use the info in core kernel module to
			 * obtain our path to "sysname".
			 * We have defaulted to running kernel for info til now.
			 */
			wlk->modname = (char *)0; /* kernel mod */
			sysname = module_name;
		} else {
			wlk->modname = module_name;
		}

		/* locate a module matching this name */
		if (find_module(wlk, search) != 0) {
			/*
			 * Non-fatal error occurred. (has been reported).
			 * We simply won't process this module.
			 */
			free(wlk->mp);
			wlk->path = (char *)0;
			wlk->mp = (struct module *)0;
		} else {
			/* update sym/str table size totals */
			Kvm_read(kd, (u_long)wlk->mp->symhdr,
			    (char *)&tmp_shdr, sizeof (tmp_shdr));
			if (tmp_shdr.sh_size == 0) {
				(void) fprintf(stderr, "%s: WARNING: '%s' "
				    "has a zero size symbol table.\n",
				    progname, module_name);
				syslog(LOG_WARNING,
				    "'%s' has a zero size symbol table.",
				    module_name);
				free(wlk->mp);
				wlk->path = (char *)0;
				wlk->mp = (struct module *)0;
			} else {
				wlk->cd.sym_size = tmp_shdr.sh_size;
				/*
				 * Index of first global == number of locals.
				 */
				wlk->cd.nlcls = tmp_shdr.sh_info;
				wlk->cd.nglbs = (tmp_shdr.sh_size /
				    tmp_shdr.sh_entsize) - tmp_shdr.sh_info;

				/* strings */
				Kvm_read(kd, (u_long)wlk->mp->strhdr,
				    (char *)&tmp_shdr, sizeof (tmp_shdr));
				wlk->cd.str_size += tmp_shdr.sh_size;

				/* get section hdrs */
				shdrs_size = wlk->mp->hdr.e_shentsize *
				    wlk->mp->hdr.e_shnum;
				cp = Malloc(shdrs_size);
				if (wlk->mp->shdrs) {
					Kvm_read(kd, (u_long)wlk->mp->shdrs, cp,
					    shdrs_size);
					wlk->mp->shdrs = cp;
				}
			}
		}
skip:
		/* next modctl list element */
		if (modctl_walk->mod_next == (struct modctl *)value) {
			/* circled back to the kernel module */
			wlk->next = (struct loaded *)0;
			break;
		} else {
			Kvm_read(kd, (u_long)modctl_walk->mod_next,
			    (char *)&tmp, sizeof (tmp));
			modctl_walk = &tmp;
			wlk->next = Malloc(sizeof (struct loaded));
			wlk = wlk->next;
			(void) memset(wlk, 0, sizeof (struct loaded));
		}
	}
}

/*
 * Find the module in module path. Returns 0 on success,
 * -1 on failure.
 *
 * If a module has a null name, it is assumed to be the kernel module. If
 * a module's name begins with "/", it is assumed to have been loaded
 * via a manual modload (no path search is done).
 */
int
find_module(struct loaded *mod, char *path)
{
	register char 		*el, *cp;
	char			*p_buf, *tmp_buf;
	register int		len;
	register char		c;

	if (mod->modname == (char *)0) {
		/* kernel module */
		mod->modname = sysname;
		mod->path = sysname;
		goto verify_mod;
	}
	if (*mod->modname == '/') {
		/* manually loaded module. */
		mod->path = mod->modname;
		goto verify_mod;
	}

	/*
	 * Look in the module path for the module..
	 */
	p_buf = Malloc(MOD_MAXPATH);
	len = strlen(path) + 1;
	tmp_buf = Malloc(len);
	(void) strcpy(tmp_buf, path);
	mod->path = (char *)0;
	for (el = tmp_buf, cp = tmp_buf; cp < (tmp_buf + len); cp++) {
		if ((c = *cp) == '\t' || c == ' ' || c == ':' || c == '\0') {
			*cp = '\0';
			/* element of path. */
			(void) sprintf(p_buf, "%s/%s", el,
			    mod->modname);
			if (access(p_buf, R_OK | F_OK) < 0) {
				el = (char *)((u_long)cp + 1);
				continue;
			}
			mod->path = p_buf;
			break;
		}
	}
	free(tmp_buf);
	if (mod->path == (char *)0) {
		(void) fprintf(stderr,
		    "%s: Warning: can't find '%s' in  module path: %s\n",
		    progname, mod->modname, path);
		syslog(LOG_WARNING, "can't find '%s' in module path: %s",
		    mod->modname, path);
		free(p_buf);
		return (-1);
	} else
		return (0);

verify_mod:
	if (access(mod->path, R_OK | F_OK) < 0) {
		(void) fprintf(stderr,
		    "%s: Warning: can't access '%s'.\n", progname,
		    mod->modname);
		syslog(LOG_WARNING, "can't access '%s'.",
		    mod->modname);
		return (-1);
	} else
		return (0);
}

/*
 * Versions of kvm routines that exit on error.
 */
kvm_t *
Kvm_open(char *namelist, char *corefile, char *swapfile, int flag)
{
	kvm_t *kd;

	kd = kvm_open(namelist, corefile, swapfile, flag,
		Verbose ? progname : NULL);
	if (kd == NULL) {
		if (Verbose)
			(void) fprintf(stderr, "%s: kvm_open failed\n",
			    progname);
		exit(1);
	}
	return (kd);
}

void
Kvm_nlist(kvm_t *kd, struct nlist nl[])
{
	if (kvm_nlist(kd, nl) < 0) {
		(void) fprintf(stderr, "%s: no namelist\n", progname);
		syslog(LOG_ERR, "no namelist");
		exit(1);
	}
}

void
Kvm_read(kvm_t *kd, u_long addr, char *buf, u_int nbytes)
{
	if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
		(void) fprintf(stderr, "%s: kernel read error\n",
		    progname);
		syslog(LOG_ERR, "kernel read error");
		exit(1);
	}
}

/*
 * Versions of std routines that exit on error.
 */
int
Open(char *name, int rw)
{
	int fd;

	fd = open(name, rw);
	if (fd < 0) {
		int oerrno = errno;

		(void) fprintf(stderr, "%s: ", progname);
		errno = oerrno;
		perror(name);
		errno = oerrno;
		syslog(LOG_ERR, "%s: %m", name);
		exit(1);
	}
	return (fd);
}

int
Read(int fd, char *buff, int size)
{
	int ret;

	ret = read(fd, buff, size);
	if (ret < 0) {
		int oerrno = errno;

		(void) fprintf(stderr, "%s: ", progname);
		perror("read");
		errno = oerrno;
		syslog(LOG_ERR, "read: %m");
		exit(1);
	}
	return (ret);
}

int
Create(char *file, int mode)
{
	register int fd;

	fd = creat(file, mode);
	if (fd < 0) {
		int oerrno = errno;

		(void) fprintf(stderr, "%s: ", progname);
		errno = oerrno;
		perror(file);
		errno = oerrno;
		syslog(LOG_ERR, "%s: %m", file);
		exit(1);
	}
	return (fd);
}

void
Write(int fd, char *buf, int size)
{
	if (write(fd, buf, size) < size) {
		int oerrno = errno;

		(void) fprintf(stderr, "%s: ", progname);
		perror("write");
		errno = oerrno;
		syslog(LOG_ERR, "write: %m");
		exit(1);
	}
}

void
Fsync(int fd)
{
	if (fsync(fd) < 0) {
		int oerrno = errno;

		(void) fprintf(stderr, "%s: ", progname);
		perror("fsync");
		errno = oerrno;
		syslog(LOG_ERR, "write: %m");
		exit(1);
	}
}

off_t
Lseek(int fd, off_t offset, int whence)
{
	off_t ret;

	ret = lseek(fd, offset, whence);
	if (ret == -1) {
		int oerrno = errno;

		(void) fprintf(stderr, "%s: ", progname);
		perror("lseek");
		errno = oerrno;
		syslog(LOG_ERR, "lseek: %m");
		exit(1);
	}
	return (ret);
}

void *
Malloc(size_t size)
{
	register void *tmp;

	if ((tmp = malloc(size)) == (void *)0) {
		int oerrno = errno;
		(void) fprintf(stderr, "%s: ", progname);
		perror("malloc");
		errno = oerrno;
		syslog(LOG_ERR, "malloc: %m");
		exit(1);
	}
	return (tmp);
}
