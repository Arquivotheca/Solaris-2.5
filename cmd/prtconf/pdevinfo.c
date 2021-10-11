/*
 * Copyright (c) 1990, 1991, 1992 Sun Microsystems, Inc.  All Rights Reserved.
 */

#pragma ident "@(#)pdevinfo.c	1.17	95/06/27 SMI"

/*
 * For machines that support the openprom, fetch and print the list
 * of devices that the kernel has fetched from the prom or conjured up.
 *
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <kvm.h>
#include <nlist.h>
#include <fcntl.h>
#include <varargs.h>
#include <malloc.h>
#include <unistd.h>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/autoconf.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddipropdefs.h>
#include <sys/openpromio.h>

/*
 * Structure to map device names to major numbers.
 */
struct name_to_major {
	char	*ntm_name;
	major_t	ntm_major;
	struct name_to_major *ntm_next;
};
/*
 * function declarations
 */

static struct name_to_major *read_name_to_major(int maxdevs);
static void walk_devs(struct dev_info *dp);
static void build_devs(struct dev_info *pdev, struct dev_info *dp);
static void dump_devs(struct dev_info *dp, int ilev);
static char *getkname(char *kaddr);
static void dump_prop_list(char *name, ddi_prop_t **list_head, int ilev);
static void getpropdata(ddi_prop_t *propp);
static void getproplist(ddi_prop_t **list_head);
static void dump_node(int id, int level);
int _error();
static void bzero();

/*
 * local data
 */
kvm_t *kd;
char *mfail = "malloc";
static char *indent_string = "    ";
static struct name_to_major *ntm_head;

extern int verbose;
extern int pseudodevs;
extern char *progname;
extern char *promdev;
extern void getppdata();
extern void printppdata();

#define	DSIZE	(sizeof (struct dev_info))
#define	P_DSIZE	(sizeof (dev_info_t *))
#define	NO_PERROR	((char *)0)

#define	TRUE  1
#define	FALSE 0

/*
 * Define DPRINT for run-time debugging printf's...
 * #define	DPRINT	1
 */

/* #define	DPRINT	1 */
#ifdef	DPRINT
static	char    vdebug_flag = 1;
#define	dprintf	if (vdebug_flag) printf
static void dprint_dev_info(caddr_t, struct dev_info *);
#endif	DPRINT

extern struct utsname uts_buf;
static struct devnames *devnames_array;
static int	devcnt;
static void dump_global_list(struct dev_info *dp, int ilev);

void
prtconf_devinfo()
{
	struct dev_info root_node;
	dev_info_t *rnodep;
	struct devnames *devnamesp;
	struct devnames *dnp;
	int devnames_array_size;
	static struct nlist list[] = {
		{ "top_devinfo" },
		{ "devcnt" },
		{ "devnamesp" },
		""
	};
	struct nlist *top_devinfo_nlp = &list[0];
	struct nlist *devcnt_nlp = &list[1];
	struct nlist *devnamesp_nlp = &list[2];
	int	i;


#ifdef	DPRINT
	dprintf("verbosemode %s\n", verbose ? "on" : "off");
#endif	DPRINT

	if ((kd = kvm_open((char *)0, (char *)0, (char *)0, O_RDONLY, progname))
	    == NULL) {
		exit(_error("kvm_open failed"));
	} else if ((kvm_nlist(kd, &list[0])) != 0) {
		(void) _error(NO_PERROR,
		    "%s not available on kernel architecture %s (yet).",
		    progname, uts_buf.machine);
		exit(1);
	}
	/*
	 * First, read the devnamesp array (so we can get global
	 * properties.
	 */
	if (kvm_read(kd, devcnt_nlp->n_value, (char *)&devcnt, sizeof (int))
	    != sizeof (int)) {
		exit(_error("kvm_read of devcnt fails"));
	}

#ifdef	DPRINT
	dprintf("devcnt = %d.\n", devcnt);
#endif	DPRINT

	ntm_head = read_name_to_major(devcnt);

	/* sanity check the name_to_major_list */
	devnames_array_size = devcnt * sizeof (struct devnames);

#ifdef	DPRINT
	dprintf("size of devnames array = %d.\n", devnames_array_size);
#endif	DPRINT

	devnames_array = malloc(devnames_array_size);
	if (devnames_array == NULL) {
		exit(_error("malloc of devnames array fails"));
	}

	if (kvm_read(kd, devnamesp_nlp->n_value, (char *)&devnamesp,
	    sizeof (struct devnamesp *)) != sizeof (struct devnamesp *)) {
		exit(_error("kvm_read of devnamesp fails"));
	}

#ifdef	DPRINT
	dprintf("devnamesp = 0x%x.\n", devnamesp);
#endif	DPRINT

	if (kvm_read(kd, (u_long)devnamesp, (char *)devnames_array,
	    devnames_array_size) != devnames_array_size) {
		exit(_error("kvm_read of devnames array fails"));
	}

	if (verbose != 0)  {
		dnp = &devnames_array[0];
		for (i = 0; i < devcnt; i++, dnp++) {
			if (dnp->dn_name)
				dnp->dn_name = getkname(dnp->dn_name);
			getproplist(&(dnp->dn_global_prop_ptr));
		}
	}

	/*
	 * Next, build the root node...
	 */

#ifdef	DPRINT
	dprintf("Building root node DSIZE %d, P_DSIZE %d...",
	    DSIZE, P_DSIZE);
#endif	DPRINT

	if (kvm_read(kd, top_devinfo_nlp->n_value, (char *)&rnodep, P_DSIZE)
	    != P_DSIZE) {
		exit(_error("kvm_read of root node pointer fails"));
	}

	if (kvm_read(kd, (u_long)rnodep, (char *)&root_node, DSIZE) != DSIZE) {
		exit(_error("kvm_read of root node fails"));
	}

#ifdef	DPRINT
	dprintf("got root node.\n");
	dprint_dev_info((caddr_t)rnodep, &root_node);
#endif	DPRINT

	/*
	 * call build_devs to fetch and construct a user space copy
	 * of the dev_info tree.....
	 */

	build_devs(NULL, &root_node);
	(void) kvm_close(kd);

	/*
	 * ...and call walk_devs to report it out...
	 */
	walk_devs(&root_node);
}

/*
 * build_devs copies all appropriate information out of the kernel
 * and gets it into a user addrssable place. the argument is a
 * pointer to a just-copied out to user space dev_info structure.
 * all pointer quantities that we might de-reference now have to
 * be replaced with pointers to user addressable data.
 */

static void
build_devs(struct dev_info *pdev, struct dev_info *dp)
{
	char *tptr;

	DEVI(dp)->devi_parent = DEVI(pdev);

	if (DEVI(dp)->devi_name)
		DEVI(dp)->devi_name = getkname(DEVI(dp)->devi_name);

#ifdef	DPRINT
	dprintf("copied nodename is <%s>\n", DEVI(dp)->devi_name ?
		DEVI(dp)->devi_name : "<none>");
#endif	DPRINT

	if (verbose != 0)  {
		getproplist(&(DEVI(dp)->devi_drv_prop_ptr));
		getproplist(&(DEVI(dp)->devi_sys_prop_ptr));
		getppdata(dp);
	}

	/*
	 * Skip children of "pseudo" unless pseudodevs flag is set.
	 */
	if ((pseudodevs == 0) && (strcmp(DEVI(dp)->devi_name, "pseudo") == 0))
		DEVI(dp)->devi_child = 0;

	if (DEVI(dp)->devi_child) {
		if (!(tptr = malloc(DSIZE))) {
			exit(_error(mfail));
		}
		if (kvm_read(kd, (u_long)DEVI(dp)->devi_child, tptr, DSIZE)
		    != DSIZE) {
			exit(_error("kvm_read of devi_child"));
		}
		DEVI(dp)->devi_child = (struct dev_info *)tptr;
		build_devs(dp, DEVI(dp)->devi_child);
	}

	if (DEVI(dp)->devi_sibling) {
		if (!(tptr = malloc(DSIZE))) {
			exit(_error(mfail));
		}
		if (kvm_read(kd, (u_long)DEVI(dp)->devi_sibling, tptr,
		    DSIZE) != DSIZE) {
			exit(_error("kvm_read of devi_sibling"));
		}
		DEVI(dp)->devi_sibling = (struct dev_info *)tptr;
		build_devs(pdev, DEVI(dp)->devi_sibling);
	}
}

void
indent_to_level(int ilev)
{
	register i;

	for (i = 0; i < ilev; i++)
		(void) printf(indent_string);
}

/*
 * print out information about this node, descend to children, then
 * go to siblings
 */

static void
walk_devs(struct dev_info *dp)
{
	static int indent_level = 0;	/* we would start at 0, except ... */
					/* that we skip the root node */
	indent_to_level(indent_level);

	(void) printf("%s", DEVI(dp)->devi_name);
	if (DEVI(dp)->devi_instance >= 0)
		(void) printf(", instance #%d", DEVI(dp)->devi_instance);
	if (DEVI(dp)->devi_ops == 0)
		printf(" (driver not attached)");
	(void) printf("\n");
	dump_devs(DEVI(dp), indent_level+1);

	if (DEVI(dp)->devi_child) {
		indent_level++;
		walk_devs(DEVI(dp)->devi_child);
		indent_level--;
	}
	if (DEVI(dp)->devi_sibling) {
		walk_devs(DEVI(dp)->devi_sibling);
	}
}

/*
 * utility routines
 */

static void
dump_devs(struct dev_info *dp, int ilev)
{
	if (verbose)  {
		dump_prop_list("System", &(DEVI(dp)->devi_sys_prop_ptr), ilev);
		dump_global_list(dp, ilev);
		dump_prop_list("Driver", &(DEVI(dp)->devi_drv_prop_ptr), ilev);
		printppdata(dp, ilev);
	}
}

static void
dump_prop_list(char *name, ddi_prop_t **list_head, int ilev)
{
	int i;
	ddi_prop_t *propp;

	if (*list_head != 0)  {
		indent_to_level(ilev);
		printf("%s software properties:\n", name);
	}

	for (propp = *list_head; propp != 0; propp = propp->prop_next)  {

		indent_to_level(ilev +1);
		printf("name <%s> length <%d>",
			propp->prop_name, propp->prop_len);

		if (propp->prop_flags & DDI_PROP_UNDEF_IT)  {
			printf(" -- Undefined.\n");
			continue;
		}

		if (propp->prop_len == 0)  {
			printf(" -- <no value>.\n");
			continue;
		}

		putchar('\n');
		indent_to_level(ilev +1);
		printf("    value <0x");
		for (i = 0; i < propp->prop_len; ++i)  {
			unsigned byte;
			byte = (unsigned)((unsigned char)*(propp->prop_val+i));
			printf("%2.2x", byte);
		}
		printf(">.\n");
	}
}

static void
dump_global_list(struct dev_info *dp, int ilev)
{
	struct devnames *dnp;
	int	i;
	ddi_prop_t *propp;
	struct name_to_major *tmp;

	if (DEVI(dp)->devi_name == NULL)
		return;

	for (tmp = ntm_head; tmp != NULL; tmp = tmp->ntm_next) {
		if (strcmp(tmp->ntm_name, DEVI(dp)->devi_name) == 0)
			break;
	}

	if (tmp == NULL) {
		/* not a driver node, no global driver properties */
		return;
	}

	dnp = &devnames_array[tmp->ntm_major];
	/*
	 * At this point, we're going to print out a property
	 * as if it was a 'System' one.  However, if there were no
	 * per-device System properties no header was printed,
	 * so we do it here.
	 */
	if (DEVI(dp)->devi_sys_prop_ptr == NULL &&
	    dnp->dn_global_prop_ptr != NULL) {
		indent_to_level(ilev);
		printf("System software properties:\n");
	}

	for (propp = dnp->dn_global_prop_ptr; propp != NULL;
		propp = propp->prop_next)  {

		indent_to_level(ilev +1);
		printf("name <%s> length <%d>",
			propp->prop_name, propp->prop_len);

		if (propp->prop_flags & DDI_PROP_UNDEF_IT)  {
			printf(" -- Undefined.\n");
			continue;
		}

		if (propp->prop_len == 0)  {
			printf(" -- <no value>.\n");
			continue;
		}

		putchar('\n');
		indent_to_level(ilev +1);
		printf("    value <0x");
		for (i = 0; i < propp->prop_len; ++i)  {
			unsigned byte;
			byte = (unsigned)((unsigned char)*(propp->prop_val+i));
			printf("%2.2x", byte);
		}
		printf(">.\n");
	}
}

#define	KNAMEBUFSIZE 256

static char *
getkname(char *kaddr)
{
	auto char buf[KNAMEBUFSIZE], *rv;
	register i = 0;
	char c;

#ifdef	DPRINT
	dprintf("getkname: kaddr %x\n", (int)kaddr);
#endif	DPRINT

	if (kaddr == (char *)0) {
		(void) strcpy(buf, "<null>");
		i = 7;
	} else {
		while (i < KNAMEBUFSIZE) {
			if (kvm_read(kd, (u_long)kaddr++, (char *)&c, 1) != 1) {
				exit(_error("kvm_read of name string"));
			}
			if ((buf[i++] = c) == (char)0)
				break;
		}
		buf[i] = 0;
	}
	if ((rv = malloc((unsigned)i)) == 0) {
		exit(_error(mfail));
	}
	strncpy(rv, buf, i);

#ifdef	DPRINT
	dprintf("getkname: got string <%s>\n", rv);
#endif	DPRINT

	return (rv);
}

static void
getproplist(ddi_prop_t **list_head)
{
	ddi_prop_t *kpropp, *npropp, *prevpropp;


#ifdef	DPRINT
	dprintf("getproplist: Reading property list at kaddr <0x%x>\n",
	    *list_head);
#endif	DPRINT

	prevpropp = 0;
	npropp = 0;
	for (kpropp = *list_head; kpropp != 0; kpropp = npropp->prop_next)  {

#ifdef	DPRINT
		dprintf("getproplist: Reading property data size <%d>",
		    sizeof (ddi_prop_t));
		dprintf(" at kadr <0x%x>\n", kpropp);
#endif	DPRINT

		npropp = (ddi_prop_t *)malloc(sizeof (ddi_prop_t));
		if (npropp == 0)
			exit(_error(mfail));

		if (kvm_read(kd, (u_long)kpropp, (char *)npropp,
		    sizeof (ddi_prop_t)) != sizeof (ddi_prop_t))
			exit(_error("kvm_read of property data"));

		if (prevpropp == 0)
			*list_head = npropp;
		else
			prevpropp->prop_next = npropp;

		prevpropp = npropp;
		getpropdata(npropp);
	}
}

static void
getpropdata(ddi_prop_t *propp)
{
	char *p;

	propp->prop_name = getkname(propp->prop_name); /* XXX */

	if (propp->prop_len != 0)  {
		p = malloc((unsigned)propp->prop_len);

		if (p == 0)
			exit(_error(mfail));

		if (kvm_read(kd, (u_long)propp->prop_val, (char *)p,
		    (unsigned)propp->prop_len) != (int)propp->prop_len)
			exit(_error("kvm_read of property data"));

		propp->prop_val = (caddr_t)p;
	}
}

/* _error([no_perror, ] fmt [, arg ...]) */
/*VARARGS*/
int
_error(va_alist)
va_dcl
{
	int saved_errno;
	va_list ap;
	int no_perror = 0;
	char *fmt;
	extern int errno, _doprnt();

	saved_errno = errno;

	if (progname)
		(void) fprintf(stderr, "%s: ", progname);

	va_start(ap);
	if ((fmt = va_arg(ap, char *)) == 0) {
		no_perror = 1;
		fmt = va_arg(ap, char *);
	}
	(void) _doprnt(fmt, ap, stderr);
	va_end(ap);

	if (no_perror)
		(void) fprintf(stderr, "\n");
	else {
		(void) fprintf(stderr, ": ");
		errno = saved_errno;
		perror("");
	}

	return (1);
}

#ifdef	DPRINT
static void
dprint_dev_info(caddr_t kaddr, struct dev_info *dp)
{
	dprintf("Devinfo node at kaddr <0x%x>:\n", kaddr);
	dprintf("\tdevi_parent <0x%x>\n", DEVI(dp)->devi_parent);
	dprintf("\tdevi_child <0x%x>\n", DEVI(dp)->devi_child);
	dprintf("\tdevi_sibling <0x%x>\n", DEVI(dp)->devi_sibling);
	dprintf("\tdevi_name <0x%x>\n", DEVI(dp)->devi_name);
	dprintf("\tdevi_nodeid <0x%x>\n", DEVI(dp)->devi_nodeid);
	dprintf("\tdevi_instance <0x%x>\n", DEVI(dp)->devi_instance);
	dprintf("\tdevi_ops <0x%x>\n", DEVI(dp)->devi_ops);
	dprintf("\tdevi_drv_prop_ptr <0x%x>\n", DEVI(dp)->devi_drv_prop_ptr);
	dprintf("\tdevi_sys_prop_ptr <0x%x>\n", DEVI(dp)->devi_sys_prop_ptr);
}
#endif	DPRINT

/*
 * The rest of the routines handle printing the raw prom devinfo (-p option).
 *
 * 128 is the size of the largest (currently) property name
 * 4096 - MAXPROPSIZE - sizeof (int) is the size of the largest
 * (currently) property value that is allowed.
 * the sizeof (u_int) is from struct openpromio
 */

#define	MAXPROPSIZE	128
#define	MAXVALSIZE	(4096 - MAXPROPSIZE - sizeof (u_int))
#define	BUFSIZE		(MAXPROPSIZE + MAXVALSIZE + sizeof (u_int))
typedef union {
	char buf[BUFSIZE];
	struct openpromio opp;
} Oppbuf;

static void dump_node(), print_one(), promclose(), walk();
static int child(), getpropval(), next(), unprintable(), promopen();

static int prom_fd;

static int
is_openprom()
{
	Oppbuf	oppbuf;
	register struct openpromio *opp = &(oppbuf.opp);
	register unsigned int i;

	opp->oprom_size = MAXVALSIZE;
	if (ioctl(prom_fd, OPROMGETCONS, opp) < 0)
		exit(_error("OPROMGETCONS"));

	i = (unsigned int)((unsigned char)opp->oprom_array[0]);
	return ((i & OPROMCONS_OPENPROM) == OPROMCONS_OPENPROM);
}

static char *badarchmsg =
	"System architecure does not support this option of this command.\n";

int
do_prominfo()
{
	if (promopen(O_RDONLY))  {
		exit(_error("openeepr device open failed"));
	}

	if (is_openprom() == 0)  {
		fprintf(stderr, badarchmsg);
		return (1);
	}

	if (next(0) == 0)
		return (1);
	walk(next(0), 0);
	promclose();
	return (0);
}

static void
walk(id, level)
int id, level;
{
	register int curnode;

	dump_node(id, level);
	if (curnode = child(id))
		walk(curnode, level+1);
	if (curnode = next(id))
		walk(curnode, level);
}

/*
 * Print all properties and values
 */
static void
dump_node(int id, int level)
{
	Oppbuf	oppbuf;
	register struct openpromio *opp = &(oppbuf.opp);
	register int i = level;

	while (i--)
		(void) printf(indent_string);
	(void) printf("Node");
	if (verbose)
		(void) printf(" %#08lx\n", (long) id);

	/* get first prop by asking for null string */
	bzero(oppbuf.buf, BUFSIZE);
	for (;;) {
		/*
		 * get property
		 */
		opp->oprom_size = MAXPROPSIZE;

		if (ioctl(prom_fd, OPROMNXTPROP, opp) < 0)
			exit(_error("OPROMNXTPROP"));

		if (opp->oprom_size == 0) {
			break;
		}
		print_one(opp->oprom_array, level+1);
	}
	(void) putchar('\n');
}

/*
 * Print one property and its value.
 */
static void
print_one(var, level)
char	*var;
int	level;
{
	Oppbuf	oppbuf;
	register struct openpromio *opp = &(oppbuf.opp);
	register int i;

	while (verbose && level--)
		(void) printf(indent_string);
	if (verbose)
		(void) printf("%s: ", var);
	(void) strcpy(opp->oprom_array, var);
	if (getpropval(opp) || opp->oprom_size == -1) {
		(void) printf("data not available.\n");
		return;
	}
	if (verbose && unprintable(opp)) {
		(void) printf(" ");
		for (i = 0; i < opp->oprom_size; ++i) {
			if (i && (i % 4 == 0))
				(void) putchar('.');
			(void) printf("%02x",
			    opp->oprom_array[i] & 0xff);
		}
		(void) putchar('\n');
	} else if (verbose) {
		(void) printf(" '%s'\n", opp->oprom_array);
	} else if (strcmp(var, "name") == 0)
		(void) printf(" '%s'", opp->oprom_array);
}

static int
unprintable(opp)
struct openpromio *opp;
{
	register int i;

	/*
	 * Is this just a zero?
	 */
	if (opp->oprom_size == 0 || opp->oprom_array[0] == '\0')
		return (1);
	/*
	 * If any character is unprintable, or if a null appears
	 * anywhere except at the end of a string, the whole
	 * property is "unprintable".
	 */
	for (i = 0; i < opp->oprom_size; ++i) {
		if (opp->oprom_array[i] == '\0')
			return (i != (opp->oprom_size - 1));
		if (!isascii(opp->oprom_array[i]) ||
		    iscntrl(opp->oprom_array[i]))
			return (1);
	}
	return (0);
}

static int
promopen(oflag)
register int oflag;
{
	for (;;)  {
		if ((prom_fd = open(promdev, oflag)) < 0)  {
			if (errno == EAGAIN)   {
				sleep(5);
				continue;
			}
			if (errno == ENXIO)
				return (-1);
			exit(_error("cannot open %s", promdev));
		} else
			return (0);
	}
}

static void
promclose()
{
	if (close(prom_fd) < 0)
		exit(_error("close error on %s", promdev));
}

static
getpropval(opp)
register struct openpromio *opp;
{
	opp->oprom_size = MAXVALSIZE;

	if (ioctl(prom_fd, OPROMGETPROP, opp) < 0)
		return (_error("OPROMGETPROP"));
	return (0);
}

static int
next(id)
int id;
{
	Oppbuf	oppbuf;
	register struct openpromio *opp = &(oppbuf.opp);
	int *ip = (int *)(opp->oprom_array);

	bzero(oppbuf.buf, BUFSIZE);
	opp->oprom_size = MAXVALSIZE;
	*ip = id;
	if (ioctl(prom_fd, OPROMNEXT, opp) < 0)
		return (_error("OPROMNEXT"));
	return (*(int *)opp->oprom_array);
}

static int
child(id)
int id;
{
	Oppbuf	oppbuf;
	register struct openpromio *opp = &(oppbuf.opp);
	int *ip = (int *)(opp->oprom_array);

	bzero(oppbuf.buf, BUFSIZE);
	opp->oprom_size = MAXVALSIZE;
	*ip = id;
	if (ioctl(prom_fd, OPROMCHILD, opp) < 0)
		return (_error("OPROMCHILD"));
	return (*(int *)opp->oprom_array);
}

static void
bzero(char *p, int size)
{
	register int i;

	for (i = 0; i < size; ++i)
		*p++ = 0;
}

#ifdef i386
/*
 * Just return with a status of 1 which indicates that no separate
 * frame buffer from the console.
 * This fixes bug 3001499.
 */
int
do_fbname()
{
	return (1);
}
#else
/*
 * Get and print the name of the frame buffer device.
 */
int
do_fbname()
{
	Oppbuf	oppbuf;
	register struct openpromio *opp = &(oppbuf.opp);
	register unsigned int i;

	if (promopen(O_RDONLY))  {
		fprintf(stderr, "Cannot open openprom device\n");
		return (1);
	}

	opp->oprom_size = MAXVALSIZE;
	if (ioctl(prom_fd, OPROMGETCONS, opp) < 0)
		exit(_error("OPROMGETCONS"));

	i = (unsigned int)((unsigned char)opp->oprom_array[0]);
	if ((i & OPROMCONS_STDOUT_IS_FB) == 0)  {
		fprintf(stderr, "Console output device is not a framebuffer\n");
		return (1);
	}

	opp->oprom_size = MAXVALSIZE;
	if (ioctl(prom_fd, OPROMGETFBNAME, opp) < 0)
		exit(_error("OPROMGETFBNAME"));

	printf("%s\n", opp->oprom_array);
	promclose();
	return (0);
}
#endif /* i386 */

/*
 * Get and print the PROM version.
 */
#ifndef i386
int
do_promversion(void)
{
	Oppbuf	oppbuf;
	struct openpromio *opp = &(oppbuf.opp);
	char *p;

	if (promopen(O_RDONLY))  {
		fprintf(stderr, "Cannot open openprom device\n");
		return (1);
	}

	opp->oprom_size = MAXVALSIZE;
	if (ioctl(prom_fd, OPROMGETVERSION, opp) < 0)
		exit(_error("OPROMGETVERSION"));

	printf("%s\n", opp->oprom_array);
	promclose();
	return (0);
}
#endif /* i386 */

static struct name_to_major *
read_name_to_major(int maxdevs)
{
	FILE	*fp;
	char	name[MAXNAMELEN + 1];
	int	major;
	int	rval;
	struct name_to_major *head = NULL;
	struct name_to_major *tail = NULL;
	struct name_to_major *new;

	fp = fopen("/etc/name_to_major", "r");
	if (fp == NULL) {
		exit(_error("open of /etc/name_to_major failed"));
	}
	while ((rval = fscanf(fp, "%s%d", name, &major)) != EOF) {
		if (rval != 2) {
			exit(_error("parse error in /etc/name_to_major"));
		}

		if (major < 0 || major >= maxdevs) {
			exit(_error(
			    "/etc/name_to_major does not match kernel"));
		}

		new = malloc(sizeof (struct name_to_major));
		if (new == NULL) {
			exit(_error(mfail));
		}

		new->ntm_next = NULL;
		new->ntm_major = major;
		new->ntm_name = strdup(name);
		if (new->ntm_name == NULL) {
			exit(_error(mfail));
		}

		if (head == NULL) {
			head = new;
			head->ntm_next = NULL;
			tail = head;
		} else {
			tail->ntm_next = new;
			tail = tail->ntm_next;
		}
	}
	fclose(fp);
	return (head);
}
