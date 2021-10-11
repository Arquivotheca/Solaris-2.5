/*
 * Copyright (c) 1994-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)uname-i.c	1.9	95/07/14 SMI"

#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/idprom.h>
#include <sys/promif.h>

#include <sys/platnames.h>

#include <string.h>

/*
 * This source is (and should be ;-) shared between the boot blocks
 * and the boot programs.  So if you change it, be sure to test them all!
 */

#define	MAXNMLEN	1024		/* # of chars in a property */

short
_get_cputype(void)
{
	dnode_t n;
	struct idprom idprom;

	if ((n = prom_rootnode()) != OBP_NONODE &&
	    prom_getprop(n, OBP_IDPROM, (caddr_t)&idprom) == sizeof (idprom))
		return (idprom.id_machine);
	return (CPU_NONE);
}

/*
 * Use the cputype to determine the impl_arch name
 * Really only needed for sun4c clone machines.
 */
static char *
get_iarch_from_cputype(void)
{
	struct cputype2name *p;
	short cputype;

	if ((cputype = _get_cputype()) == 0x20)
		return ("sun4c");	/* SI bogosity! */
	for (p = _cputype2name_tbl; p->cputype != CPU_NONE; p++)
		if (p->cputype == cputype)
			return (p->iarch);
	if ((CPU_ARCH & cputype) == SUN4C_ARCH)
		return ("sun4c");	/* naughty clone */
	return (NULL);
}

enum ia_state_mach {
	STATE_NAME,
	STATE_COMPAT_INIT,
	STATE_COMPAT,
	STATE_CPUTYPE,
	STATE_FINI
};

/*
 * Return the implementation architecture name (uname -i) for this platform.
 *
 * Use the named rootnode property to determine the iarch; if the name is
 * an empty string, use the cputype.
 */
static char *
get_impl_arch_name(enum ia_state_mach *state)
{
	static char iarch[MAXNMLEN];
	static int len;
	static char *ia;

	dnode_t n;
	char *cp;
	struct cputype2name *p;
	char *namename;

newstate:
	switch (*state) {
	case STATE_NAME:
		*state = STATE_COMPAT_INIT;
		namename = "name";
		n = (dnode_t)prom_rootnode();
		len = prom_getproplen(n, namename);
		if (len <= 0 || len >= MAXNMLEN)
			goto newstate;
		(void) prom_getprop(n, namename, iarch);
		iarch[len] = '\0';	/* fix broken clones */
		ia = iarch;
		break;

	case STATE_COMPAT_INIT:
		*state = STATE_COMPAT;
		namename = "compatible";
		n = (dnode_t)prom_rootnode();
		len = prom_getproplen(n, namename);
		if (len <= 0 || len >= MAXNMLEN) {
			*state = STATE_CPUTYPE;
			goto newstate;
		}
		(void) prom_getprop(n, namename, iarch);
		iarch[len] = '\0';	/* ensure null termination */
		ia = iarch;
		break;

	case STATE_COMPAT:
		/*
		 * Advance 'ia' to point to next string in
		 * compatible property array (if any).
		 */
		while (*ia++)
			;
		if ((ia - iarch) >= len) {
			*state = STATE_CPUTYPE;
			goto newstate;
		}
		break;

	case STATE_CPUTYPE:
		*state = STATE_FINI;
		return (get_iarch_from_cputype());

	case STATE_FINI:
		return (NULL);
	}

	/*
	 * Crush filesystem-awkward characters.  See PSARC/1992/170.
	 * (Convert the property to a sane directory name in UFS)
	 */
	for (cp = ia; *cp; cp++)
		if (*cp == '/' || *cp == ' ' || *cp == '\t')
			*cp = '_';
	/*
	 * Convert old sun4c names to 'SUNW,' prefix form
	 */
	for (p = _cputype2name_tbl; p->iarch != (char *)0; p++)
		if (strcmp(p->iarch + 5, ia) == 0)
			return (p->iarch);
	return (ia);
}

static void
make_platform_path(char *fullpath, char *iarch, char *filename)
{
	(void) strcpy(fullpath, "/platform/");
	(void) strcat(fullpath, iarch);
	(void) strcat(fullpath, "/");
	(void) strcat(fullpath, filename);
}

/*
 * Given a filename, and a function to perform an 'open' on that file,
 * find the corresponding file in the /platform hierarchy, generating
 * the implementation architecture name on the fly.
 *
 * The routine will also set 'impl_arch_name' if non-null, and returns
 * the full pathname of the file opened.
 *
 * We allow the caller to specify the impl_arch_name.  We also allow
 * the caller to specify an absolute pathname, in which case we do
 * our best to generate an impl_arch_name.
 */
int
open_platform_file(
	char *filename,
	int (*openfn)(char *, void *),
	void *arg,
	char *fullpath,
	char *impl_arch_name)
{
	char *ia;
	int fd = -1;
	enum ia_state_mach state = STATE_NAME;

	/*
	 * If the caller -specifies- an absolute pathname, then we just
	 * open it after (optionally) determining the impl_arch_name.
	 *
	 * This is only here for booting non-kernel standalones (or pre-5.5
	 * kernels).  It's debateable that they would ever care what the
	 * impl_arch_name is.
	 */
	if (*filename == '/') {
		(void) strcpy(fullpath, filename);
		if (impl_arch_name &&
		    (ia = get_impl_arch_name(&state)) != NULL)
			(void) strcpy(impl_arch_name, ia);
		return ((*openfn)(fullpath, arg));
		/*NOTREACHED*/
	}

	/*
	 * If the caller -specifies- the impl_arch_name, then there's
	 * not much more to do than just open it.
	 *
	 * This is only here to support the '-I' flag to the boot program.
	 */
	if (impl_arch_name && *impl_arch_name != '\0') {
		make_platform_path(fullpath, impl_arch_name, filename);
		return ((*openfn)(fullpath, arg));
		/*NOTREACHED*/
	}

	/*
	 * Otherwise, we must hunt the filesystem for one that works ..
	 */
	while ((ia = get_impl_arch_name(&state)) != NULL) {
		make_platform_path(fullpath, ia, filename);
		if ((fd = (*openfn)(fullpath, arg)) == -1)
			continue;
		if (impl_arch_name)
			(void) strcpy(impl_arch_name, ia);
		break;
	}

	return (fd);
}
