/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)boot_plat.c	1.9	94/12/31 SMI"

#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/obpdefs.h>
#include <sys/reboot.h>
#include <sys/promif.h>
#include <sys/stat.h>
#include <sys/bootvfs.h>
#include <sys/platnames.h>

#define	SUCCESS		0
#define	FAILURE		-1

#ifdef DEBUG
static int	debug = 1;
#define	HALTBOOT
#else DEBUG
static	int	debug = 0;
#endif DEBUG

#define	dprintf		if (debug) printf

extern	char *makepath(char *mname, char *fname);
extern	struct memlist 	*fill_memlists(caddr_t name, caddr_t prop);
extern	int (*readfile(int fd, int print))();
extern	void kmem_init(void);
extern	void v2_getargs(char *defname, char *buf);
extern	void sunmon_getargs(char *defname, char *buf);
extern	void setup_bootops(void);
extern	struct	bootops bootops;
extern	int read_redirect(char *redirect);

int openfile(char *filename);

extern	char *kernname;
extern	char *impl_arch_name;
extern	struct memlist *pfreelistp, *vfreelistp, *pinstalledp;
extern	char *mfg_name;
extern	char *my_own_name;
extern	int boothowto;

/*
 *  We enable the cache by default
 *  but boot -n will leave it alone...
 *  that is, we use whatever state the PROM left it in.
 */
char	*mfg_name;
int	cache_state = 1;
union sunromvec *romp = (union sunromvec *)0xffe81000;
char	filename2[MAXPATHLEN];

/*ARGSUSED*/
void
setup_bootargs(char *bargs)
{
	/*
	 * dummy
	 */
}

void
translate_tov2(char **v2path, char *bpath)
{	extern char *translate_v0tov2(char *s);

	if (prom_getversion() <= 0)
		*v2path = translate_v0tov2(bpath);
	else
		*v2path = bpath;
}

void
post_mountroot(char *bootfile, char *redirect)
{
	int (*go2)();
	int fd;
#ifdef MPSAS
	extern void sas_bpts(void);
#endif

	/* Save the bootfile, just in case we need it again */
	(void) strcpy(filename2, bootfile);

	for (;;) {
		if (boothowto & RB_ASKNAME) {
			char tmpname[MAXPATHLEN];

			printf("Enter filename [%s]: ", bootfile);
			(void) gets(tmpname);
			if (tmpname[0] != '\0')
				strcpy(bootfile, tmpname);
		}

		if ((fd = openfile(bootfile)) == FAILURE) {

			/*
			 * There are many reasons why this might've
			 * happened .. but one of them is that we're
			 * on the installation CD, and we need to
			 * revector ourselves off to a different partition
			 * of the CD.  Check for the redirection file.
			 */
			if (redirect != NULL &&
			    read_redirect(redirect)) {
				/* restore bootfile */
				(void) strcpy(bootfile, filename2);
				return;
				/*NOTREACHED*/
			}

			printf("%s: cannot open %s\n", my_own_name, bootfile);
			boothowto |= RB_ASKNAME;

			/* restore bootfile */
			(void) strcpy(bootfile, filename2);
			continue;
		}

		if ((go2 = readfile(fd, boothowto & RB_VERBOSE)) !=
		    (int(*)()) -1) {
#ifdef MPSAS
			sas_bpts();
#endif
			close(fd);
		} else {
			printf("boot failed\n");
			boothowto |= RB_ASKNAME;
			continue;
		}

		if (boothowto & RB_HALT) {
			printf("Boot halted.\n");
			prom_enter_mon();
		}

		/* update (or create) some bootprops */
		my_own_name = bootfile;
		dprintf("Calling exitto(%x)\n", go2);

		exitto(go2);
	}
}

static int
boot_open(char *pathname, void *arg)
{
	dprintf("trying '%s'\n", pathname);
	return (open(pathname, O_RDONLY));
}

/*
 * Open the given filename, expanding to it's
 * platform-dependent location if necessary.
 *
 * Boot supports OBP and IEEE1275.
 */
int
openfile(char *filename)
{
	static char fullpath[MAXPATHLEN];
	static char iarch[MAXPATHLEN];
	int fd;

	/*
	 * Exported as the 'mfg-name' boot property.
	 */
	mfg_name = get_mfg_name();

	/*
	 * If the -I flag has been used, impl_arch_name will
	 * be specified .. otherwise we want to supply a buffer
	 * to open_platform_file() so that it can tell us what
	 * impl_arch_name was actually chosen so we can export that
	 * name via the 'impl-arch-name' boot property.
	 */
	if (impl_arch_name == NULL)
		impl_arch_name = iarch;

	fd = open_platform_file(filename, boot_open, NULL, fullpath,
	    impl_arch_name);

	if (fd == -1 || *impl_arch_name == '\0')
		return (-1);
	/*
	 * Copy back the name we actually found
	 */
	(void) strcpy(filename, fullpath);
	return (fd);
}

void
setup_bootpath(char *bpath, char *bargs)
{	char	*cp;
	int 	n;

	if (prom_getversion() <= 0) {
		struct bootparam *bp = prom_bootparam();

		cp = (char *)strchr(bp->bp_argv[0], ')');
		n = cp - bp->bp_argv[0] + 1;
		strncpy(bpath, bp->bp_argv[0], n);
		*(bpath + n) = '\0';
		sunmon_getargs(kernname, bargs);
	} else {
		/*
		 * 1115931 - strip options from network device types
		 * (So standalone can handle boot net:IPADDRESS.)
		 * We don't want to do this for non-network devices,
		 * otherwise we may strip disk partition information.
		 */
		int fd;
		dnode_t node;

		/*
		 * Convert pathname to phandle, so we can get devicetype
		 */
		fd = prom_open(prom_bootpath());
		node = prom_getphandle(fd);
		(void) prom_close(fd);

		if (prom_devicetype(node, "network"))
			prom_strip_options(prom_bootpath(), bpath);
		else
			strcpy(bpath, prom_bootpath());
		v2_getargs(kernname, bargs);
	}
}

/*
 * Given the boot path in the native firmware format
 * (e.g. 'sd(0,3,2)' or '/sbus@.../.../sd@6,0:d', use
 * the redirection string to mutate the boot path to the new device.
 * Fix up the 'v2path' so that it matches the new firmware path.
 */
void
redirect_boot_path(char **v2path_p, char *bpath, char *redirect)
{
	char slicec = *redirect;
	char *p = bpath + strlen(bpath);

	/*
	 * If the redirection character doesn't fall in this
	 * range, something went horribly wrong.
	 */
	if (slicec < '0' || slicec > '7') {
		printf("boot: bad redirection slice '%c'\n", slicec);
		return;
	}

	if (prom_getversion() <= 0) {
		/*
		 * Horrible old SunMON names.
		 */
		while (--p >= bpath)
			if (*p == ',')
				break;
		if (*p++ == ',') {
			/*
			 * Slice letter is the same as the partition number.
			 */
			*p = slicec;

			/*
			 * Because we know that the v2path has been forged,
			 * we're quite confident to assert that we know its
			 * format exactly.
			 *
			 * XXX	Note that we can't just call translate_tov2()
			 *	here because that routine looks at the
			 *	'bootparam' array as part of the translation.
			 */	
			p = *v2path_p;
			*(p + strlen(p) - 1) = 'a' + slicec - '0';
			return;
		}
	} else {
		/*
		 * Handle fully qualified OpenBoot pathname.
		 */
		while (--p >= bpath && *p != '@' && *p != '/')
			if (*p == ':')
				break;
		if (*p++ == ':') {
			/*
			 * Convert slice number to partition 'letter'.
			 */
			*p++ = 'a' + slicec - '0';
			*p = '\0';
			translate_tov2(v2path_p, bpath);
			return;
		}
	}
	prom_panic("redirect_boot_path: mangled boot path!");
}
