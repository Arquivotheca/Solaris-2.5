/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)boot.c	1.47	94/12/31 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/promif.h>
#include <sys/reboot.h>
#include <sys/bootconf.h>
#include <sys/stat.h>
#include <sys/bootvfs.h>
#include <sys/boot_redirect.h>
#include <sys/fcntl.h>

#ifdef DEBUG
static int	debug = 1;
#define	HALTBOOT
#else DEBUG
static int	debug = 0;
#endif DEBUG

#define	dprintf		if (debug) printf
#define	SUCCESS		0
#define	FAILURE		-1

/*
 * The file system is divided into blocks of usually 8K each.
 * Each block would then hold 16 sectors of 512 bytes each.
 * Sector zero is reserved for the label.
 * Sectors 1 through 15 are reserved for the boot block program.
 */

/*
 * Base of all platform-dependent kernel directories.
 */
#define	MACHINE_BASEDIR	"/platform"

/*
 *  These variables should be declared as pointers (and init'd
 *  as such, if you wish) so that the boot getprop code works right.
 */
char	*kernname = "kernel/unix";
char	*my_own_name = "boot";
char	*impl_arch_name;
char    *v2path, *v2args;
int	boothowto = 0;
int 	verbosemode = 0;
char	*systype;
char    filename[MAXPATHLEN];

/*  These are the various memory lists */
struct memlist 	*pfreelistp, /* physmem available */
		*vfreelistp, /* virtmem available */
		*pinstalledp;   /* physmem installed */

extern int	cache_state;

extern void	fiximp(void);
extern void	init_memlists(void);
extern void	setup_bootpath(char *bpath, char *bargs);
extern void	setup_bootargs(char *bargs);
extern int	mountroot(caddr_t devpath);
extern int	bootflags(char *s);
extern void	setup_bootops(void);
extern void	set_cache_state(int cache_state);
extern void	post_mountroot(char *bootfile, char *redirect);
extern void	translate_tov2(char **v2path, char *bpath);
extern caddr_t	kmem_alloc(int);
extern void	redirect_boot_path(char **, char *, char *);

#define	MAXARGS		8

/*
 * Reads in the standalone (client) program and jumps to it.  If this
 * attempt fails, prints "boot failed" and returns to its caller.
 *
 * It will try to determine if it is loading a Unix file by
 * looking at what should be the magic number.  If it makes
 * sense, it will use it; otherwise it jumps to the first
 * address of the blocks that it reads in.
 *
 * This new boot program will open a file, read the ELF header,
 * attempt to allocate and map memory at the location at which
 * the client desires to be linked, and load the program at
 * that point.  It will then jump there.
 */

main(void *cookie)
{
	char	*cp;
	static	char    bpath[256], bargs[256];
	int 	i;
	int	once = 0;

	prom_init("boot", cookie);
	fiximp();

	dprintf("\nboot: V%d /boot interface.\n", BO_VERSION);
#ifdef HALTBOOT
	prom_enter_mon();
#endif HALTBOOT

#ifdef DEBUG_MMU
	dump_mmu();
#endif DEBUG_MMU

	init_memlists();

#ifdef DEBUG_LISTS
	dprintf("Physmem avail:\n");
	if (debug) print_memlist(pfreelistp);
	dprintf("Virtmem avail:\n");
	if (debug) print_memlist(vfreelistp);
	dprintf("Phys installed:\n");
	if (debug) print_memlist(pinstalledp);
	prom_enter_mon();
#endif DEBUG_LISTS

	setup_bootpath(bpath, bargs);

	/*
	 * both of *_getargs() routines will place the filename as the
	 * first arg.  Any args that follow must begin with '-' and they
	 * will be separated from the filename by a space.  So we just
	 * copy the name of the file out to filename here.
	 */
	for (cp = bargs, i = 0; *cp && *cp != ' '; cp++, i++)
		filename[i] = *cp;

	dprintf("bootpath: 0x%x %s bootargs: 0x%x %s filename: 0x%x %s\n",
	    bpath, bpath, bargs, bargs, filename, filename);

	/* translate bpath to v2 format */
	translate_tov2(&v2path, bpath);
	v2args = bargs;

	/*
	 * Our memory lists should be "up" by this time
	 */

	setup_bootops();

	if (bargs && *bargs)
		boothowto = bootflags(bargs);

	setup_bootargs(bargs);

	/*
	 * (we might've disabled cacheing with boot -n)
	 */
	set_cache_state(cache_state);

	systype = set_fstype(v2path);

loop:
	if (verbosemode)
		printf("device path '%s'\n", v2path);

	/*
	 * Open our native device driver
	 */
	if (mountroot(bpath) != SUCCESS)
		prom_panic("Could not mount filesystem.\n");

	if (once == 0 &&
	    (strcmp(systype, "ufs") == 0 || strcmp(systype, "hsfs") == 0)) {
		char redirect[256];

		post_mountroot(filename, redirect);

		/*
		 * If we return at all, it's because we discovered
		 * a redirection file - the 'redirect' string now contains
		 * the name of the disk slice we should be looking at.
		 *
		 * Unmount the filesystem, tweak the boot path and retry
		 * the whole operation one more time.
		 */
		closeall(1);
		once++;
		redirect_boot_path(&v2path, bpath, redirect);
		if (verbosemode)
			printf("%sboot: using '%s'\n", systype, bpath);

		goto loop;
		/*NOTREACHED*/
	}
	post_mountroot(filename, NULL);
	/*NOTREACHED*/
}

/*
 * Construct a pathname (given the platform name and file name).
 */
char *
makepath(char *mname, char *fname)
{
	register char *p;
	register int len;

	/*
	 * Build the pathname.
	 */
	len = sizeof (MACHINE_BASEDIR) +
	    strlen(mname) + strlen(fname) + 2;
	p = (char *) kmem_alloc(len);

	(void) sprintf(p, "%s/%s/%s", MACHINE_BASEDIR, mname, fname);
	return (p);
}

/*
 * The slice redirection file is used on the install CD
 */
int
read_redirect(char *redirect)
{
	int fd;
	char slicec;
	int nread = 0;

	if ((fd = open(BOOT_REDIRECT, O_RDONLY)) != -1) {
		/*
		 * Read the character out of the file - this is the
		 * slice to use, in base 36.
		 */
		nread = read(fd, &slicec, 1);
		(void) close(fd);
		if (nread == 1)
			*redirect++ = slicec;
	}
	*redirect = '\0';

	return (nread == 1);
}
