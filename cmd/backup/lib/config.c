/*LINTLIBRARY*/
/*PROTOLIB1*/
#ident	"@(#)config.c 1.0 90/12/07 SMI"

#ident	"@(#)config.c 1.37 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include <metamucil.h>
#include <locale.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <malloc.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mman.h>
#ifdef USG
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#endif

/*
 * This should be passed in from the Makefile.
 * This is just CYA in case it isn't.
 */
#ifndef HSMROOT
#define	HSMROOT		"/opt/SUNWhsm"
#endif

struct fs_params {
	char	*fs_name;		/* file system name */
	char	*fs_lock;		/* file system lock type */
	struct active {
		int	a_retries;	/* number of retries */
		u_long	a_size;		/* size threshhold */
		int	a_report;	/* =1 if message should be produced */
		struct active *a_next;	/* next on list */
	} *fs_active;			/* activity commands */
	int	fs_online;		/* =1 if online dumps should be done */
	int	fs_reset;		/* =1 if hide user access changes */
	struct fs_params *fs_next;	/* next on list */
};
typedef struct fs_params fsparm_t;

struct logical_device {
	char	*ld_name;		/* device name */
	dtype_t	ld_type;		/* logical device type */
	char	**ld_drives;		/* list of physical devices */
	char	**ld_current;		/* current physical device name */
	int	ld_wrapped;		/* number of times device used */
	struct logical_device *ld_next;	/* next on list */
};
typedef struct logical_device ldev_t;

struct cmd {
	char	*c_name;		/* command name */
#ifdef __STDC__
	int	(*c_func)(char *);	/* function */
#else
	int	(*c_func)()		/* function */
#endif
};

#ifdef __STDC__
void	createdefaultfs(int);
static int	set_active(char *);
static int	set_database(char *);
static int	set_filesystem(char *);
static int	set_lock(char *);
static int	set_reset(char *);
static int	set_mail(char *);
static int	set_operd(char *);
static int	set_sequence(char *);
static int	set_tmpdir(char *);
static int	set_online(char *);
#else
void	createdefaultfs();
static int	set_active();
static int	set_database();
static int	set_filesystem();
static int	set_lock();
static int	set_reset();
static int	set_mail();
static int	set_operd();
static int	set_sequence();
static int	set_tmpdir();
static int	set_online();
#endif

static struct cmd cmdtab[] = {
	{
		"active",
		set_active
	},
	{
		"database",
		set_database
	},
	{
		"filesystem",
		set_filesystem
	},
	{
		"lock",
		set_lock
	},
	{
		"reset",
		set_reset
	},
	{
		"mail",
		set_mail
	},
	{
		"operd",
		set_operd
	},
	{
		"sequence",
		set_sequence
	},
	{
		"tmpdir",
		set_tmpdir
	},
	{
		"online",
		set_online
	}
};
static int ncmds = sizeof (cmdtab)/sizeof (struct cmd);

static struct active default_active[] = {
	0,	/* don't recopy */
	0,	/* regardless of size */
	1,	/* instead, notify */
	NULL	/* no other commands */
};
static fsparm_t default_fs = {
	"default",
	"scan",
	default_active,
	1,
	1,
	NULL
};
static fsparm_t *fslist;		/* file system parameter list */

static char *default_dirs[] = {		/* list of temporary directories */
	TMPDIR,
	0
};
static char **tmpdirlist = default_dirs;

static ldev_t	*devlist;		/* list of logical devices */
static ldev_t	*current_dev;		/* logical device currently in use */
static fsparm_t	*current_fs;		/* current file system */
static char	**maillist;		/* mail notification list */
static char	opserver[BCHOSTNAMELEN] = "localhost"; /* operator daemon */
static char	dbserver[BCHOSTNAMELEN] = "localhost"; /* database daemon */
static char	hostname[BCHOSTNAMELEN]; /* our name */
static int	lineno = 0;		/* current line number in dump.config */
static char	*errstr;
static int	commentsok;		/* interpret '#' as comment char */

/*
 * For dgettext()
 */
static char	*domainname = "hsm_libdump";

#ifdef __STDC__
static void	printdevice(ldev_t *);
static int	nstrings(char *);
static char	*skip(char *);
static char	**makelist(char *);
static void	msg(const char *, ...);
/*
 * XXX
 */
extern char *strdup(const char *);
extern int munmap(caddr_t, size_t);
extern caddr_t mmap(caddr_t, size_t, int, int, int, off_t);
extern int msync(caddr_t, size_t, int);
#else
static void	printdevice();
static int	nstrings();
static char	*skip();
static char	**makelist();
static void	msg();
/*
 * XXX
 */
extern char *strdup();
extern int munmap();
#endif

#ifdef __STDC__
readconfig(
	char *filename,			 /* HSMROOT/etc/CONFIGFILE if 0 */
	void (*func)(const char *, ...)) /* fprintf(stderr) if 0 */
#else
readconfig(filename, func)
	char *filename;			/* defaults to HSMROOT/etc/CONFIGFILE */
	void (*func)();			/* defaults to fprintf(stderr) */
#endif
{
	struct stat statb;
	int fd;
	caddr_t data;
	char *cmd = NULL;
	int errors = 0;
	int cont = 0;
	int comment = 0;
	register int i;
	register char *cp, *next;

#ifdef __STDC__
	if (func == (void (*)(const char *, ...))0)
#else
	if (func == (void (*)())0)
#endif
		func = msg;

	if (filename == (char *)0) {
		char *etc = gethsmpath(etcdir);
		filename = malloc(strlen(etc) + strlen(CONFIGFILE) + 2);
		if (filename == (char *)0) {
			func("out of memory");
			return (0);
		}
		(void) sprintf(filename, "%s/%s", etc, CONFIGFILE);
	}

#ifdef USG
	if (sysinfo(SI_HOSTNAME, hostname, BCHOSTNAMELEN) < 0) {
#else
	if (gethostname(hostname, BCHOSTNAMELEN) < 0) {
#endif
		func(dgettext(domainname,
			"cannot determine local host name\n"));
		return (-1);
	}
	if ((fd = open(filename, O_RDONLY, 0)) < 0) {
		if (errno != ENOENT)
			return (-1);
		else
			return (0);
	}
	if (fstat(fd, &statb) < 0) {
		(void) close(fd);
		return (-1);
	}
	if (statb.st_size == 0) {
		func(dgettext(domainname,
		    "Warning: zero-length configuration file %s ignored\n"),
			filename);
		(void) close(fd);
		return (0);
	}
	data = mmap((caddr_t)0, (size_t)statb.st_size,
	    PROT_READ, MAP_SHARED, fd, 0L);
	if (data == (caddr_t) -1) {
		(void) close(fd);
		return (-1);
	}
	commentsok++;
	for (cp = next = (char *)data;
	    cp < &data[statb.st_size] && *cp; cp = next) {
		if (cmd) {
			free(cmd);
			cmd = NULL;
		}
		while (*next) {
			if (*next == '\n') {
				comment = 0;
				lineno++;
				if (!cont) {
					next++;
					break;
				}
				cont = 0;
			} else if (*next == '\\')
				cont = 1;
			else if (*next == '#')
				comment = 1;
			else if (cont && !isspace((u_char)*next) && !comment)
				cont = 0;
			next++;
		}
		cp = getstring(cp, &cmd);
		if (cp == NULL)
			goto error;
		if (cmd == NULL)
			continue;
		for (i = 0; i < ncmds; i++)
			if (strcmp(cmd, cmdtab[i].c_name) == 0) {
				if ((*cmdtab[i].c_func)(cp) < 0)
					goto error;
				break;
			}
		if (i == ncmds)
			errstr = dgettext(domainname, "unknown command");
		else
			continue;
error:
		errors++;
		func(dgettext(domainname, "error in %s, line %d: %s\n"),
			filename, lineno,
			errstr ? errstr : dgettext(domainname, "syntax error"));
	}
	commentsok = 0;
	createdefaultfs(METAMUCIL);
	(void) munmap(data, statb.st_size);
	(void) close(fd);
	return (errors);
}

void
#ifdef __STDC__
printconfig(void)
#else
printconfig()
#endif
{
	register ldev_t *dev;
	register fsparm_t *fsp;
	register struct active *ap;
	register char **cpp;
	char mail[1024];

	(void) fprintf(stderr, dgettext(domainname, "DATABASE SERVER: %s\n"),
		dbserver);
	(void) fprintf(stderr, dgettext(domainname, "OPERD SERVER: %s\n"),
		opserver);
	(void) fprintf(stderr, dgettext(domainname, "TEMPORARY DIRECTORIES:"));
	if (tmpdirlist) {
		(void) fprintf(stderr, "\n");
		for (cpp = tmpdirlist; *cpp; cpp++)
			(void) fprintf(stderr, "    %s\n", *cpp);
	} else
		(void) fprintf(stderr, dgettext(domainname, " (none)\n"));
	(void) getmail(mail, sizeof (mail));
	(void) fprintf(stderr, dgettext(domainname, "MAIL: %s\n"),
		mail[0] ? mail : dgettext(domainname, "none"));
	for (dev = devlist; dev; dev = dev->ld_next)
		printdevice(dev);
	(void) fprintf(stderr, dgettext(domainname, "FILE SYSTEMS:\n"));
	for (fsp = fslist; fsp; fsp = fsp->fs_next) {
		(void) fprintf(stderr,
		    dgettext(domainname, "  %s -- lock type %s\n"),
			fsp->fs_name,
			fsp->fs_lock ? fsp->fs_lock : default_fs.fs_lock);
		(void) fprintf(stderr, dgettext(domainname,
		    "  do online dumps? %s\n"),
			fsp->fs_online ? "yes" : "no");
		(void) fprintf(stderr, dgettext(domainname,
		    "  hide user access time changes? %s\n"),
			fsp->fs_reset ? "yes" : "no");
		if (fsp->fs_active == NULL) {
			(void) fprintf(stderr, dgettext(domainname,
			    "    uses default active commands\n"));
			continue;
		}
		(void) fprintf(stderr, dgettext(domainname,
			"    active commands:\n"));
		for (ap = fsp->fs_active; ap; ap = ap->a_next)
			(void) fprintf(stderr, dgettext(domainname,
			    "\ttries=%d, size=%d, report %s\n"),
				ap->a_retries,
				ap->a_size,
				ap->a_report ? dgettext(domainname, "yes") :
				    dgettext(domainname, "no"));
	}
}

char *
getstring(cp, bufp)
	char	*cp;
	char	**bufp;
{
	char *p;
	register char *op;
	char	*contp = (char *)0;

	/*
	 * Find beginning of token or delimiter
	 */
	cp = skip(cp);
	if (*cp == ',')
		return (NULL);
	if (*cp == '\0' || *cp == '\n') {	/* end of string */
		*bufp = NULL;
		return (cp);
	}
	/*
	 * Find the end of the token
	 */
	for (p = cp; *p && !isspace((u_char)*p) && *p != ','; p++) {
		if (*p == '\\')
			contp = p;
		else if (commentsok && *p == '#')
			break;
		else
			contp = (char *)0;
	}
	if (contp)
		p = contp;
	*bufp = malloc((unsigned)(p + 1 - cp));
	if (*bufp == NULL) {
		errstr = dgettext(domainname, "out of memory");
		return (NULL);
	}
	for (op = *bufp; cp < p; op++, cp++) {
		if (*cp == '\\' && cp == contp) {
			cp++;
			while (cp < p && *cp != '\n')
				cp++;
			break;
		}
		*op = *cp;
	}
	*op = '\0';
	/*
	 * Find end of white-space or delimiter
	 */
	p = skip(p);
	if (*p == ',') {
		p = skip(++p);
		if (*p == ',' || *p == '\0' || *p == '\n')
			return (NULL);
	}
	return (p);	/* return current position */
}

char *
#ifdef __STDC__
gettmpdir(void)
#else
gettmpdir()
#endif
{
	static char **tmpdir;
	char *retval;

	if (tmpdir == (char **)0)
		tmpdir = tmpdirlist;
	retval = *tmpdir;
	if (*tmpdir != (char *)0)
		tmpdir++;
	return (retval);
}

#ifdef __STDC__
int
setdbserver(const char *host)
#else
int
setdbserver(host)
	char *host;
#endif
{
	struct hostent	*h;

	if (host == (char *)0) {
		errno = EINVAL;
		return (-1);
	}
	if (strcmp(host, "+default") == 0 && dbserver == (char *)0)
		return (-1);
	h = gethostbyname((char *)host);
	if (h == (struct hostent *)0)
		return (-1);
	(void) strncpy(dbserver, host, sizeof (dbserver));
	return (0);
}

int
getdbserver(buf, namelen)
	char *buf;
	int namelen;
{
	if (buf == (char *)0 || namelen <= 0) {
		errno = EINVAL;
		return (-1);
	}
	(void) strncpy(buf, dbserver, namelen);
	return (0);
}

#ifdef __STDC__
int
setopserver(const char *host)
#else
int
setopserver(host)
	char *host;
#endif
{
	struct hostent	*h;

	if (host == (char *)0) {
		errno = EINVAL;
		return (-1);
	}
	while (isspace(*host))
		host++;
	h = gethostbyname((char *)host);
	if (h == (struct hostent *)0)
		return (-1);
	(void) strncpy(opserver, host, sizeof (opserver));
	return (0);
}

int
getopserver(buf, namelen)
	char *buf;
	int namelen;
{
	if (buf == (char *)0 || namelen <= 0) {
		errno = EINVAL;
		return (-1);
	}
	(void) strncpy(buf, opserver, namelen);
	return (0);
}

makedevice(name, list, type)
	char	*name;
	char	*list;
	dtype_t	type;
{
	ldev_t *dev;

	errno = 0;
	dev = (ldev_t *) malloc((unsigned)sizeof (ldev_t));
	if (dev == NULL) {
		errstr = dgettext(domainname, "out of memory");
		return (-1);
	}
	dev->ld_name = malloc((unsigned)strlen(name)+1);
	if (dev->ld_name == NULL) {
		errstr = dgettext(domainname, "out of memory");
		return (-1);
	}
	dev->ld_drives = makelist(list);
	if (dev->ld_drives == NULL || dev->ld_drives[0] == NULL) {
		errstr = dgettext(domainname,
			"missing device list in `sequence' command");
		if (errno != ENOMEM)
			errno = EINVAL;
		return (-1);
	}
	(void) strcpy(dev->ld_name, name);
	dev->ld_wrapped = -1;
	dev->ld_current = dev->ld_drives;
	dev->ld_next = devlist;
	dev->ld_type = type;
	devlist = dev;
	return (0);
}

static void
printdevice(dev)
	ldev_t *dev;
{
	register char **cpp;

	if (dev == (ldev_t *)0)
		return;
	(void) fprintf(stderr, dgettext(domainname, "DEVICE NAME = %s\n"),
		dev->ld_name);
	(void) fprintf(stderr, dgettext(domainname, "    COMPONENTS:\n"));
	for (cpp = dev->ld_drives; *cpp; cpp++)
		(void) fprintf(stderr, "\t%s\n", *cpp);
}

int
setdevice(name)
	char	*name;
{
	ldev_t *dev;

	for (dev = devlist; dev; dev = dev->ld_next)
		if (strcmp(dev->ld_name, name) == 0)
			break;
	if (dev) {
		current_dev = dev;
		return (0);
	}
	current_dev = (ldev_t *)0;
	return (-1);
}

char *
#ifdef __STDC__
getdevice(void)
#else
getdevice()
#endif
{
	char *drive = *current_dev->ld_current;

	if (current_dev->ld_current == current_dev->ld_drives)
		current_dev->ld_wrapped++;
	if (*++current_dev->ld_current == NULL)
		current_dev->ld_current = current_dev->ld_drives;
	return (drive);
}

/*
 * Return information about the selected logical device.
 * The arguments point to variables filled in with the
 * device's type, number of constituent physical devices,
 * and number of times the logical device has wrapped.
 */
void
getdevinfo(ptype, pdevices, pwrapped)
	dtype_t	*ptype;
	int	*pdevices;
	int	*pwrapped;
{
	if (current_dev == (ldev_t *)0) {
		if (ptype != (dtype_t *)0)
			*ptype = none;
		if (pdevices != (int *)0)
			*pdevices = 0;
		if (pwrapped != (int *)0)
			*pwrapped = 0;
		return;
	}
	if (ptype != (dtype_t *)0)
		*ptype = current_dev->ld_type;
	if (pdevices != (int *)0) {
		register char **cpp;
		int n = 0;

		if (current_dev->ld_drives != (char **)0)
			for (cpp = current_dev->ld_drives; *cpp; cpp++)
				n++;
		*pdevices = n;
	}
	if (pwrapped != (int *)0)
		*pwrapped =
		    current_dev->ld_wrapped < 0 ? 0 : current_dev->ld_wrapped;
}

#ifdef __STDC__
void
setfsname(const char *name)
#else
void
setfsname(name)
	char *name;
#endif
{
	register struct active *ap, *last;
	register fsparm_t *fsp = (fsparm_t *)0;
	fsparm_t *dfs = (fsparm_t *)0;

	createdefaultfs(METAMUCIL);
	if (name != 0)
		for (fsp = fslist; fsp; fsp = fsp->fs_next) {
			if (strcmp(fsp->fs_name, name) == 0)
				break;
			if (dfs == (fsparm_t *)0 &&
			    strcmp(fsp->fs_name, "default") == 0)
				dfs = fsp;	/* defaults are specified */
		}
	if (dfs == (fsparm_t *)0)
		dfs = &default_fs;		/* defaults are internal */
	if (fsp) {
		/*
		 * We matched a file system by name.
		 * Merge in the defaults.
		 */
		if (fsp->fs_lock == (char *)0)
			fsp->fs_lock = dfs->fs_lock;
		if (fsp->fs_online == -1)
			fsp->fs_online = dfs->fs_online;
		if (fsp->fs_active == (struct active *)0)
			fsp->fs_active = dfs->fs_active;
		else {
			/*
			 * If any active commands were specified,
			 * we have to determine whether we need
			 * to augment them with defaults.  We'll
			 * need some defaults for file sizes between
			 * 0 and the lowest threshhold specified
			 * in the active commands.
			 */
			for (ap = fsp->fs_active; ap; ap = ap->a_next) {
				last = ap;
				if (ap->a_size <= default_active[0].a_size)
					break;
			}
			if (ap == (struct active *)0)
				last->a_next = default_active;	/* augment */
		}
		current_fs = fsp;
	} else
		/*
		 * No match, use defaults
		 */
		current_fs = dfs;
}

char *
#ifdef __STDC__
getfslocktype(void)
#else
getfslocktype()
#endif
{
	if (current_fs == (fsparm_t *)0) {
		errno = ENOENT;
		return ((char *)0);
	}
	return (current_fs->fs_lock);
}

int
#ifdef __STDC__
getfsreset(void)
#else
getfsreset()
#endif
{
	if (current_fs == (fsparm_t *)0) {
		errno = ENOENT;
		return (-1);
	}
	return (current_fs->fs_reset);
}

/*
 * For an active file of given size, return
 * 1 if it should be recopied, 0 if it should
 * be reported, -1 if no action is necessary.
 */
action_t
getfsaction(size, pass)
	size_t size;
	int pass;
{
	register struct active *a, *thisa;

	if (current_fs == (fsparm_t *)0) {
		errno = ENOENT;
		return (error);
	}
	/*
	 * Find the appropriate structure
	 * to determine what to do.
	 */
	for (a = current_fs->fs_active, thisa = NULL; a; a = a->a_next)
		if (thisa == NULL ||
		    (size >= a->a_size && a->a_size > thisa->a_size))
			thisa = a;
	if (thisa == NULL)
		return (error);    /* this should never happen */
	if (thisa->a_retries > pass)
		if (thisa->a_report)
			return (reportandretry);
		else
			return (retry);
	else if (thisa->a_report)
		return (report);
	return (donothing);
}

void
#ifdef __STDC__
createdefaultfs(int mode)
#else
createdefaultfs(mode)
int mode;
#endif
{
	static int done = 0;
	register fsparm_t *fsp;
	register struct active *ap, *dap;

	if (done)
		return;
	else
		done = 1;

	if (mode == NOT_METAMUCIL) {
		default_fs.fs_lock = "none";
		default_fs.fs_online = 0;
	}

	if (fslist == (fsparm_t *)0) {
		fslist = &default_fs;
		return;
	}
	for (fsp = fslist; fsp; fsp = fsp->fs_next) {
		if (strcmp("default", fsp->fs_name) == 0) {
			if (fsp->fs_lock == NULL)
				fsp->fs_lock = default_fs.fs_lock;
			if (fsp->fs_online == -1)
				fsp->fs_online = default_fs.fs_online;
			if (fsp->fs_reset == -1)
				fsp->fs_reset = default_fs.fs_reset;
			if (fsp->fs_active == NULL) {
				fsp->fs_active = default_fs.fs_active;
				continue;
			}
			for (dap = default_fs.fs_active;
			    dap; dap = dap->a_next) {
				for (ap = fsp->fs_active; ap; ap = ap->a_next) {
					if (ap->a_size == dap->a_size)
						break;
				}
				if (ap == NULL) {
					dap->a_next = fsp->fs_active;
					fsp->fs_active = dap;
					break;
				}
			}
		}
	}
}

#define	PTRINC	10	/* build pointer array in multiples of PTRINC */
/*
 * Turn a white-space or comma-separated list of mail
 * recipients into a canonicalized list with tokens
 * separated by a single space.
 */
#ifdef __STDC__
int
setmail(const char *recipients)
#else
int
setmail(recipients)
	char *recipients;
#endif
{
	static unsigned	nptr = 0;
	static unsigned	nrecip = 0;
	register char	**cpp;
#ifdef __STDC__
	const char	*cp, *token;
#else
	char		*cp, *token;
#endif
	char		*newstr;
	unsigned	rlen;

	if (recipients == (char *)0) {	/* null arg resets list */
		if (maillist)
			for (cpp = maillist; *cpp; cpp++) {
				free(*cpp);
				*cpp = (char *)0;
			}
		nrecip = 0;
		return (0);
	}

	if (maillist == (char **)0) {
		nptr += PTRINC;
		maillist = (char **)malloc(nptr * sizeof (char *));
		if (maillist == (char **)0)
			return (-1);
		(void) memset((char *)maillist, 0, nptr * sizeof (char *));
	}

	cp = recipients;
	/*
	 * find beginning of first token
	 */
	while (*cp && (isspace((u_char)*cp) || *cp == ','))
		cp++;
	while (*cp) {
		token = cp;
		/*
		 * find end of token
		 */
		while (*cp && !isspace((u_char)*cp) && *cp != ',')
			cp++;
		rlen = cp - token;
		if ((nrecip + 2) > nptr) {
			nptr += PTRINC;
			maillist =
			    (char **)realloc(maillist, nptr * sizeof (char *));
			if (maillist == (char **)0)
				return (-1);
		}
		newstr = malloc(rlen + 1);
		if (newstr == (char *)0)
			return (-1);
		(void) strncpy(newstr, token, rlen);
		/*
		 * check for potential duplication
		 */
		for (cpp = maillist; *cpp; cpp++) {
			if (strcmp(*cpp, newstr) == 0) {
				free(newstr);
				return (0);
			}
		}
		maillist[nrecip++] = newstr;
		maillist[nrecip] = (char *)0;
		/*
		 * find next token
		 */
		while (*cp && (isspace((u_char)*cp) || *cp == ','))
			cp++;
	}
	return (0);
}

/*
 * Return the contents of the mail recipient
 * pointer array as a space-separated list of
 * addresses, suitable for use as a mailer
 * command-line argument.
 */
int
getmail(buf, buflen)
	char	*buf;
	int buflen;
{
	register char *bp = buf;
	register char *cp, **cpp;
	register int i;

	if (buf == (char *)0 || buflen <= 0) {
		errno = EINVAL;
		return (-1);
	}

	if (maillist != (char **)0) {
		i = 1;	/* need one extra for trailing '\0' */
		for (cpp = maillist; *cpp; cpp++) {
			cp = *cpp;
			while (*cp && i < buflen) {
				*bp++ = *cp++;
				i++;
			}
			*bp = ' ';	/* separate tokens */
			i++;
		}
	}
	*bp = '\0';
	return (0);
}

static int
set_active(cp)
	register char *cp;
{
	struct active *a, *ap;
	char *cmd = NULL;
	char *arg = NULL;
	char units[256];
	int ncmds = 0;

	if (current_fs == NULL) {
		if (set_filesystem("default") < 0) {
			errstr = dgettext(domainname,
				"missing default file system");
			return (-1);
		}
	}
	a = (struct active *) malloc((unsigned)sizeof (struct active));
	if (a == NULL) {
		errstr = dgettext(domainname, "out of memory");
		return (-1);
	}
	(void) memset((void *)a, 0, sizeof (struct active));
	for (cp = getstring(cp, &cmd); cp && cmd; cp = getstring(cp, &cmd)) {
		cp = getstring(cp, &arg);
		if (cp == NULL)
			break;
		if (arg == NULL) {
			free(cmd);
			errstr = dgettext(domainname,
				"missing argument in `active' command");
			return (-1);
		}
		if (strcmp(cmd, "retries") == 0) {
			if (sscanf(arg, "%d", &a->a_retries) != 1 ||
			    a->a_retries < 0) {
				errstr = dgettext(domainname,
			"`retries' option requires non-negative integer");
				return (-1);
			}
		} else if (strcmp(cmd, "size") == 0) {
			units[0] = '\0';
			if (sscanf(arg, "%lu%255s", &a->a_size, units) < 1) {
				errstr = dgettext(domainname,
			"`size' option requires non-negative integer");
				return (-1);
			}
			if (units[0] != '\0' && units[1] != '\0') {
				errstr = dgettext(domainname,
				    "bad unit specifier in `size' option");
				return (-1);
			}
			switch (*units) {
			case 'b':
			case 'B':
				a->a_size *= 512;
				break;
			case 'k':
			case 'K':
				a->a_size *= 1024;
				break;
			case 'm':
			case 'M':
				a->a_size *= 1024*1024;
				break;
			case 'g':
			case 'G':
				a->a_size *= 1024*1024*1024;
				break;
			case '\0':
				break;
			default:
				errstr = dgettext(domainname,
				    "bad unit specifier in `size' option");
				return (-1);
			}
		} else if (strcmp(cmd, "report") == 0) {
			if (strcmp(arg, "yes") == 0)
				a->a_report = 1;
			else if (strcmp(arg, "no") == 0)
				a->a_report = 0;
			else {
				errstr = dgettext(domainname,
				    "`report' option requires `yes' or `no'");
				return (-1);
			}
		} else {
			errstr = dgettext(domainname,
				"unrecogned option to `active' command");
			return (-1);
		}
		free(cmd);
		cmd = NULL;
		free(arg);
		arg = NULL;
		ncmds++;
	}
	if (cp == NULL) {
		errstr = dgettext(domainname,
			"syntax error in `active' command");
		return (-1);
	}
	if (ncmds == 0) {
		errstr = dgettext(domainname,
			"missing options in `active' command");
		return (-1);
	}
	for (ap = current_fs->fs_active; ap; ap = ap->a_next)
		if (ap->a_size == a->a_size) {
			errstr = dgettext(domainname,
			    "duplicate/conflicting `active' comands in file");
			return (-1);
		}
	a->a_next = current_fs->fs_active;
	current_fs->fs_active = a;
	return (0);
}

static int
set_database(cp)
	register char *cp;
{
	static int	set;
	char	*newserver, *garbage;

	if (set++) {
		errstr = dgettext(domainname,
			"multiple `database' commands in file");
		return (-1);
	}

	cp = getstring(cp, &newserver);
	if (cp == NULL || newserver == NULL) {
		errstr = dgettext(domainname,
			"missing host in `database' command");
		return (-1);
	}

	if (strcmp(newserver, "localhost") == 0)
		(void) strcpy(dbserver, hostname);
	else if ((size_t)strlen(newserver) > sizeof (dbserver)) {
		static char buf[256];
		(void) sprintf(buf, dgettext(domainname,
		    "hostname `%s' too large in `database' command"),
			newserver);
		errstr = buf;
		return (-1);
	} else if (gethostbyname(newserver) == NULL) {
		static char buf[256];
		(void) sprintf(buf, dgettext(domainname,
		    "unknown host `%s' in `database' command"), newserver);
		errstr = buf;
		return (-1);
	}

	cp = getstring(cp, &garbage);	/* check for multiple hosts */
	if (cp == NULL || garbage != NULL) {
		errstr = dgettext(domainname,
			"multiple hosts in `database' command");
		return (-1);
	}

	(void) strncpy(dbserver, newserver, sizeof (dbserver));
	return (0);
}

static int
set_filesystem(cp)
	register char *cp;
{
	char	*fsname;
	register fsparm_t *fsp;

	for (cp = getstring(cp, &fsname);
	    cp && fsname;
	    cp = getstring(cp, &fsname)) {
		for (fsp = fslist; fsp; fsp = fsp->fs_next) {
			if (strcmp(fsname, fsp->fs_name) == 0) {
				static char buf[256];
				(void) sprintf(buf, dgettext(domainname,
				    "multiple entries for file system %s"),
				    fsname);
				errstr = buf;
				return (-1);
			}
		}
		fsp = (fsparm_t *)
		    malloc((unsigned)sizeof (fsparm_t));
		if (fsp == NULL)
			return (-1);
		fsp->fs_name = fsname;
		fsp->fs_active = NULL;	 /* use defaults */
		fsp->fs_lock = NULL;
		fsp->fs_online = -1;
		fsp->fs_reset = -1;
		fsp->fs_lock = NULL;
		fsp->fs_next = fslist;
		fslist = current_fs = fsp;
	}
	return (cp == NULL ? -1 : 0);
}

static int
set_lock(cp)
	register char *cp;
{
	char	*locktype;

	if (current_fs == NULL) {
		if (set_filesystem("default") < 0) {
			errstr = dgettext(domainname,
				"missing default file system");
			return (-1);
		}
	}
	cp = getstring(cp, &locktype);
	if (cp == NULL || locktype == NULL) {
		errstr = dgettext(domainname, "missing lock type");
		return (-1);
	}
	if (strcmp(locktype, "delete") &&
	    strcmp(locktype, "rename") &&
	    strcmp(locktype, "name") &&
	    strcmp(locktype, "all") &&
	    strcmp(locktype, "write") &&
	    strcmp(locktype, "none") &&
	    strcmp(locktype, "scan")) {
		errstr = dgettext(domainname, "unknown lock type");
		return (-1);
	}
	current_fs->fs_lock = locktype;
	cp = getstring(cp, &locktype);
	if (cp && locktype) {
		errstr = dgettext(domainname, "multiple lock types");
		return (-1);
	}
	return (0);
}

static int
set_online(cp)
	register char *cp;
{
	char *yn;

#ifdef DEBUG
	printf("SET_ONLINE: setting %s to online\n", (cp ? cp : "NULL"));
#endif
	if (current_fs == NULL) {
		if (set_filesystem("default") < 0) {
			errstr = "missing default file system";
			return (-1);
		}
	}
	cp = getstring(cp, &yn);
	if (cp == NULL || yn == NULL)
		return (-1);
	if (strcmp(yn, "yes") == 0)
		current_fs->fs_online = 1;
	else if (strcmp(yn, "no") == 0)
		current_fs->fs_online = 0;
	else
		return (-1);
	return (0);
}

int
#ifdef __STDC__
getfsonline(void)
#else
getfsonline()
#endif
{
	if (current_fs == (fsparm_t *)0) {
		errno = ENOENT;
		return (-1);
	}
	return (current_fs->fs_online);
}

static int
set_reset(cp)
	register char *cp;
{
	char	*yn;

	if (current_fs == NULL) {
		if (set_filesystem("default") < 0) {
			errstr = dgettext(domainname,
				"missing default file system");
			return (-1);
		}
	}
	cp = getstring(cp, &yn);
	if (cp == NULL || yn == NULL)
		return (-1);
	if (strcmp(yn, "yes") == 0)
		current_fs->fs_reset = 1;
	else if (strcmp(yn, "no") == 0)
		current_fs->fs_reset = 0;
	else
		return (-1);
	return (0);
}

static int
set_mail(cp)
	register char *cp;
{
	char *recip;
	int nadded = 0;

	do {
		cp = getstring(cp, &recip);
		if (cp == NULL)
			return (-1);
		if (recip) {
			if (setmail(recip) < 0)
				return (-1);
			nadded++;
		}
	} while (recip);

	if (nadded == 0) {
		errstr = dgettext(domainname,
			"missing recipient list in `mail' command");
		return (-1);
	}

	return (0);
}

static int
set_operd(cp)
	register char *cp;
{
	static int	set;
	char	*newserver, *garbage;

	if (set++) {
		errstr = dgettext(domainname,
			"multiple `operd' commands in file");
		return (-1);
	}

	cp = getstring(cp, &newserver);
	if (cp == NULL || newserver == NULL) {
		errstr = dgettext(domainname,
			"missing host in `operd' command");
		return (-1);
	}

	if (gethostbyname(newserver) == NULL) {
		static char buf[256];
		(void) sprintf(buf, dgettext(domainname,
		    "unknown host `%s' in `operd' command"), newserver);
		errstr = buf;
		return (-1);
	}

	cp = getstring(cp, &garbage);	/* check for multiple hosts */
	if (cp == NULL || garbage != NULL) {
		errstr = dgettext(domainname,
			"multiple hosts in `operd' command");
		return (-1);
	}

	(void) strncpy(opserver, newserver, sizeof (opserver));
	return (0);
}

static int
set_sequence(cp)
	register char *cp;
{
	char	*name;

	cp = getstring(cp, &name);
	if (cp == NULL || name == NULL) {
		errstr = dgettext(domainname, "missing sequence name");
		return (-1);
	}
	return (makedevice(name, cp, sequence));
}

static int
set_tmpdir(cp)
	register char *cp;
{
	tmpdirlist = makelist(cp);
	if (tmpdirlist == NULL || tmpdirlist[0] == NULL) {
		errstr = dgettext(domainname,
			"missing directory list in `tmpdirs' command");
		return (-1);
	}
	return (0);
}

static int
nstrings(cp)
	char *cp;
{
	int n;			/* number of strings */
	int delim = 1;		/* in delimiter string */
	int comment = 0;	/* in a comment */
	int cont = 0;		/* continuation char on this line */

	for (n = 0; *cp; cp++) {
		if (*cp == '#' && commentsok) {
			comment = 1;
			delim = 1;
		} else if (*cp == '\\')
			cont = 1;
		else if (*cp == '\n') {
			comment = 0;
			if (!delim) {
				delim = 1;
				n++;
			}
			if (!cont)
				break;
			cont = 0;
		} else if (isspace((u_char)*cp) || *cp == ',') {
			if (!comment && !delim) {
				delim = 1;
				n++;
			}
		} else if (!comment) {
			delim = 0;
			cont = 0;
		}
	}
	if (!delim)
		n++;
	return (n);
}

/*
 * Skip to next delimiter or token
 */
static char *
skip(cp)
	char *cp;
{
	int cont = 0;
	int keepgoing = 1;

	while (keepgoing && cp) {
		switch (*cp) {
		case '\n':
			/*
			 * A newline terminates a command, unless
			 * preceeded by a continuation character.
			 */
			if (cont) {
				cont = 0;	/* reset flag */
				cp++;
				break;
			}
			/*FALLTHRU*/
		case '\0':
			keepgoing = 0;
			break;
		case '\\':
			cont = 1;	/* set continuation flag */
			cp++;
			break;
		case '#':
			if (commentsok) {
				/*
				 * A comment extends to the end of the
				 * current line and thus terminates a
				 * command, unless we've previously seen
				 * a continuation character.  Move forward
				 * to the end of the line.
				 */
				cp = strchr(cp, '\n');
			} else
				keepgoing = 0;
			break;
		default:
			if (!isspace((u_char)*cp))
				/*
				 * Found end of white-space
				 */
				keepgoing = 0;
			else
				cp++;
			break;
		}
	}
	return (cp);
}

/*
 * [non-destructively] turn a comma-separated
 * list of tokens into an array of pointers
 */
static char **
makelist(list)
	char *list;
{
	char	*cp;
	char	**cpp;
	char	**lpp;

	lpp = (char **)
	    malloc((unsigned)((nstrings(list)+1) * sizeof (char *)));
	if (lpp == NULL)
		return ((char **)0);
	cp = list;
	cpp = lpp;
	do {
		cp = getstring(cp, cpp);
		if (cp == NULL)
			return ((char **)0);
	} while (*cpp++);
	return (lpp);
}

/*
 * Set up the paths used by this package.  The root
 * of the package is determined in the following order:
 *	1) "root" argument
 *	2) HSMROOT environment variable
 *	3) HSMROOT pre-processor symbol
 *	4) /opt/SUNWhsm
 * The configured paths are available through the
 * gethsmpath() routine that follows.
 */
static char	rootpath[MAXPATHLEN];	/* root of package hierarchy */
static char	binpath[MAXPATHLEN];	/* [user] command directory */
static char	sbinpath[MAXPATHLEN];	/* [system] command directory */
static char	libpath[MAXPATHLEN];	/* library directory */
static char	localepath[MAXPATHLEN];	/* locale directory */
static char	etcpath[MAXPATHLEN];	/* configuration directory */
static char	admpath[MAXPATHLEN];	/* administrative database directory */

void
sethsmpath(root)
	char	*root;
{
	if (root == (char *)0) {
		root = getenv("HSMROOT");
		if (root == (char *)0)
			root = HSMROOT;
	}
	(void) sprintf(rootpath, "%.*s", MAXPATHLEN-1, root);
	(void) sprintf(binpath, "%.*s/bin", MAXPATHLEN-5, root);
	(void) sprintf(sbinpath, "%.*s/sbin", MAXPATHLEN-6, root);
	(void) sprintf(libpath, "%.*s/lib", MAXPATHLEN-5, root);
	(void) sprintf(localepath, "%.*s/locale", MAXPATHLEN-8, libpath);
	(void) sprintf(etcpath, "%.*s/etc", MAXPATHLEN-5, root);
	(void) sprintf(admpath, "%.*s/adm", MAXPATHLEN-5, root);
}

char *
gethsmpath(which)
	dirpath_t	which;
{
	static int init;

	if (init == 0) {
		sethsmpath((char *)0);
		++init;
	}
	switch ((int)(which)) {
		case rootdir:
			return (rootpath);
		case bindir:
			return (binpath);
		case sbindir:
			return (sbinpath);
		case libdir:
			return (libpath);
		case localedir:
			return (localepath);
		case etcdir:
			return (etcpath);
		case admdir:
			return (admpath);
		default:
			return ((char *)0);
	}
}

#ifdef __STDC__
#include <stdarg.h>

/* VARARGS */
static void
msg(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	(void) vfprintf(stderr, fmt, args);
	(void) fflush(stderr);
	va_end(args);
}
#else
#include <varargs.h>

static void
msg(va_alist)
	va_dcl
{
	va_list args;
	char	*fmt;

	va_start(args);
	fmt = va_arg(args, char *);
	(void) vfprintf(stderr, fmt, args);
	(void) fflush(stderr);
	va_end(args);
}
#endif
