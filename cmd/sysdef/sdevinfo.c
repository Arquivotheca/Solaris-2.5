/*
 * Copyright (c) 1990-1993, Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)sdevinfo.c	1.10	93/05/03 SMI"

/*
 * For machines that support the openprom, fetch and print the list
 * of devices that the kernel has fetched from the prom or conjured up.
 */

#include <stdio.h>
#include <string.h>
#include <kvm.h>
#include <nlist.h>
#include <fcntl.h>
#include <varargs.h>
#include <sys/utsname.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>

/*
 * function declarations
 */

extern char *malloc();
static void build_devs(), walk_devs(), dump_devs();
static char *getkname();
static int _error();

/*
 * local data
 */
static kvm_t *kd;
static char *mfail = "malloc";
static char *progname = "sysdef";

extern int devflag;		/* SunOS4.x devinfo compatible output */

#define	DSIZE	(sizeof (struct dev_info))
#define	P_DSIZE	(sizeof (struct dev_info *))
#define	NO_PERROR	((char *) 0)

#define TRUE  1
#define FALSE 0


void
sysdef_devinfo(void)
{
	struct dev_info root_node;
	dev_info_t *rnodep;
	static struct nlist list[] = { { "top_devinfo" }, 0 };

	if ((kd = kvm_open((char *)0, (char *)0, (char *)0, O_RDONLY, progname))
	    == NULL) {
		exit(_error("kvm_open failed"));
	} else if ((kvm_nlist(kd, &list[0])) != 0) {
		struct utsname name_buf;
		(void)uname (&name_buf);
		(void)_error(NO_PERROR,
	    	    "%s not available on kernel architecture %s (yet).",
		    progname, name_buf.machine);
		exit(1);
	}

	/*
	 * first, build the root node...
	 */

	if (kvm_read(kd, list[0].n_value, (char *)&rnodep, P_DSIZE)
	    != P_DSIZE) {
		exit(_error("kvm_read of root node pointer fails"));
	}

	if (kvm_read(kd, (u_long)rnodep, (char *)&root_node, DSIZE) != DSIZE) {
		exit(_error("kvm_read of root node fails"));
	}

	/*
	 * call build_devs to fetch and construct a user space copy
	 * of the dev_info tree.....
	 */

	build_devs(&root_node);
	(void) kvm_close(kd);

	/*
	 * ...and call walk_devs to report it out...
	 */
	walk_devs (&root_node);
}

/*
 * build_devs copies all appropriate information out of the kernel
 * and gets it into a user addrssable place. the argument is a
 * pointer to a just-copied out to user space dev_info structure.
 * all pointer quantities that we might de-reference now have to
 * be replaced with pointers to user addressable data.
 */

static void
build_devs(dp)
register dev_info_t *dp;
{
	char *tptr;
	unsigned amt;

	if (DEVI(dp)->devi_name)
		DEVI(dp)->devi_name = getkname(DEVI(dp)->devi_name);

	if (DEVI(dp)->devi_child) {
		if (!(tptr = malloc(DSIZE))) {
			exit(_error(mfail));
		}
		if (kvm_read(kd, (u_long)DEVI(dp)->devi_child, tptr, DSIZE)
		    != DSIZE) {
			exit(_error("kvm_read of devi_child"));
		}
		DEVI(dp)->devi_child = (struct dev_info *) tptr;
		build_devs(DEVI(dp)->devi_child);
	}

	if (DEVI(dp)->devi_sibling) {
		if (!(tptr = malloc(DSIZE))) {
			exit(_error(mfail));
		}
		if (kvm_read(kd, (u_long)DEVI(dp)->devi_sibling, tptr, DSIZE) !=
		    DSIZE) {
			exit(_error("kvm_read of devi_sibling"));
		}
		DEVI(dp)->devi_sibling = (struct dev_info *) tptr;
		build_devs(DEVI(dp)->devi_sibling);
	}
}

/*
 * print out information about this node, descend to children, then
 * go to siblings
 */

static void
walk_devs(dp)
register dev_info_t *dp;
{
	static int root_yn      = TRUE;
	static int indent_level = -1;		/* we would start at 0, except
						   that we skip the root node*/
	register i;

	if (devflag && indent_level < 0)
		indent_level = 0;

	for (i = 0; i < indent_level; i++)
		(void)putchar('\t');

	if (root_yn && !devflag) 
		root_yn = FALSE;
	else {
		if (devflag) {
			/*
			 * 4.x devinfo(8) compatible..
			 */
			(void) printf("Node '%s', unit #%d",
				DEVI(dp)->devi_name, DEVI(dp)->devi_instance);
			if (DEVI(dp)->devi_ops == NULL)
				(void) printf(" (no driver)");
		} else {
			/*
			 * prtconf(1M) compatible..
			 */
			(void) printf("%s", DEVI(dp)->devi_name);
			if (DEVI(dp)->devi_instance >= 0)
				(void) printf(", instance #%d",
				    DEVI(dp)->devi_instance);
			if (DEVI(dp)->devi_ops == NULL)
				(void)printf(" (driver not attached)");
		}
		dump_devs(dp, indent_level+1);
		(void)printf("\n");	
	}
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
dump_devs(dp, ilev)
register dev_info_t *dp;
{
}

static char *
getkname(kaddr)
char *kaddr;
{
	auto char buf[32], *rv;
	register i = 0;
	char c;

	if (kaddr == (char *) 0) {
		(void)strcpy(buf, "<null>");
		i = 7;
	} else {
		while (i < 31) {
			if (kvm_read(kd, (u_long)kaddr++, (char *)&c, 1) != 1) {
				exit(_error("kvm_read of name string"));
			}
			if ((buf[i++] = c) == (char) 0)
				break;
		}
		buf[i] = 0;
	}
	if ((rv = malloc((unsigned)i)) == 0) {
		exit(_error(mfail));
	}
	strncpy(rv, buf, i);
	return (rv);
}

/* _error([no_perror, ] fmt [, arg ...]) */
/*VARARGS*/
static int
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
 

