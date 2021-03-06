/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*LINTLIBRARY*/
#ident	"@(#)setlist.c	1.15	94/11/22 SMI"	/* SVr4.0 1.3.1.1	*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <libintl.h>
#include <pkglocs.h>
#include <pkglib.h>
#include "libinst.h"

int	cl_NClasses = -1;
struct cl_attr	**cl_Classes = NULL;

static int new_order;
static struct cl_attr	**cl_list_alloc(void);
static struct cl_attr	*new_cl_attr(char *cl_name);

static unsigned	s_verify(char *class_name), d_verify(char *class_name);
static unsigned	s_pathtype(char *class_name);

extern void	quit(int exitval);

#define	MALSIZ	64
#define	ERR_MEMORY	"memory allocation failure, errno=%d"

static struct cl_attr **
cl_list_alloc(void)
{
	int i;
	struct cl_attr **listp;

	listp = (struct cl_attr **) calloc(MALSIZ, sizeof (struct cl_attr *));

	if (listp == NULL) {
		progerr(gettext(ERR_MEMORY), errno);
		quit(99);
	}
	for (i = 0; i < (MALSIZ); i++)
		listp[i] = (struct cl_attr *)NULL;

	return (listp);
}

static struct cl_attr *
new_cl_attr(char *cl_name)
{
	struct cl_attr *class;

	class = (struct cl_attr *) malloc(sizeof (struct cl_attr));
	if (class == NULL) {
		progerr(gettext(ERR_MEMORY), errno);
		quit(99);
	}

	strcpy(class->name, cl_name);
	class->inst_script = NULL;
	class->rem_script = NULL;
	class->src_verify = s_verify(cl_name);
	class->dst_verify = d_verify(cl_name);
	class->relpath_2_CAS = s_pathtype(cl_name);

	return (class);
}

/* Construct a list of class names one by one. */
void
addlist(struct cl_attr ***listp, char *item)
{
	int	i;
	int	j;

	/* If the list is already there, scan for this item */
	if (*listp) {
		for (i = 0; (*listp)[i]; i++)
			if (strcmp(item, (*listp)[i]->name) == 0)
				return;
	} else {
		*listp = cl_list_alloc();
		i = 0;
	}

	(*listp)[i] = new_cl_attr(item);

	if ((++i % MALSIZ) == 0) {
		*listp = (struct cl_attr **) realloc((void *)*listp,
			(i+MALSIZ) * sizeof (struct cl_attr *));
		if (*listp == NULL) {
			progerr(gettext(ERR_MEMORY), errno);
			quit(99);
		}
		for (j = i; j < (i+MALSIZ); j++)
			(*listp)[j] = (struct cl_attr *)NULL;
	}
}

/*
 * Create a list of all classes involved in this installation as well as
 * their attributes.
 */
int
setlist(struct cl_attr ***plist, char *slist)
{
	struct cl_attr	**list, *struct_pt;
	char	*pt;
	int	n;
	int	i;
	int	sn = -1;

	/* Initialize the environment scanners. */
	(void) s_verify(NULL);
	(void) d_verify(NULL);
	(void) s_pathtype(NULL);

	/* First pass allocate MALSIZ entries in the list. */
	list = cl_list_alloc();

	n = 0;

	/* Isolate the first token. */
	pt = strtok(slist, " \t\n");
	while (pt) {
		if (sn == -1 && strcmp(pt, "none") == 0)
			sn = n;

		/* Add new class to list. */
		list[n] = new_cl_attr(pt);

		/* reallocate list if necessary */
		if ((++n % MALSIZ) == 0) {
			list = (struct cl_attr **) realloc((void *)list,
				(n+MALSIZ)*sizeof (struct cl_attr *));
			if (list == NULL) {
				progerr(gettext(ERR_MEMORY), errno);
				quit(99);
			}
			for (i = n; i < (n+MALSIZ); i++)
				list[i] = (struct cl_attr *)NULL;
		}

		/* Next token. */
		pt = strtok(NULL, " \t\n");
		if (pt && sn != -1)
			if (strcmp(pt, "none") == 0)
				pt = strtok(NULL, " \t\n");
	}
	/*
	 * According to the ABI, if there is a class "none", it will be
	 * the first class to be installed.  This insures that iff there
	 * is a class "none", it will be the first to be installed.
	 * If there is no class "none", nothing happens!
	 */
	new_order = 0;
	if (sn > 0) {
		struct_pt = list[sn];
		for (i = sn; i > 0; i--)
			list[i] = list[i - 1];
		list[0] = struct_pt;
		new_order++;	/* the order is different now */
	}

	*plist = list;
	return (n);
}

/* Process the class list from the caller. */
void
cl_sets(char *slist)
{
	char *list_ptr;

	/* If there is a list, process it; else skip it */
	if (slist && *slist) {
		list_ptr = qstrdup(slist);

		if (list_ptr && *list_ptr) {
			cl_NClasses = setlist(&cl_Classes, list_ptr);
			if (new_order)		/* if list order changed ... */
				/* ... tell the environment. */
				cl_putl("CLASSES", cl_Classes);
		}
	}
}

int
cl_getn(void)
{
	return (cl_NClasses);
}

/*
 * Since the order may have changed, this puts the CLASSES list back into
 * the environment in the precise order to be used.
 */
void
cl_putl(char *parm_name, struct cl_attr **list)
{
	int i, j;
	char *pt = NULL;

	if (list && *list) {
		j = 1; /* room for ending null */
		for (i = 0; list[i]; i++)
			j += strlen(list[i]->name) + 1;
		pt = (char *) calloc((unsigned)j, sizeof (char));
		(void) strcpy(pt, list[0]->name);
		for (i = 1; list[i]; i++) {
			(void) strcat(pt, " ");
			(void) strcat(pt, list[i]->name);
		}
		if (parm_name && *parm_name)
			putparam(parm_name, pt);
		free((void *)pt);
	}
}


int
cl_idx(char *cl_nam)
{
	int	n;

	for (n = 0; n < cl_NClasses; n++)
		if (strcmp(cl_Classes[n]->name, cl_nam) == 0)
			return (n);
	return (-1);
}

/* Return source verification level for this class */
unsigned
cl_svfy(int idx)
{
	if (cl_Classes && idx >= 0 && idx < cl_NClasses)
		return (cl_Classes[idx]->src_verify);
	return (0);
}

/* Return destination verify level for this class */
unsigned
cl_dvfy(int idx)
{
	if (cl_Classes && idx >= 0 && idx < cl_NClasses)
		return (cl_Classes[idx]->dst_verify);
	return (0);
}

/* Return path argument type for this class. */
unsigned
cl_pthrel(int idx)
{
	if (cl_Classes && idx >= 0 && idx < cl_NClasses)
		return (cl_Classes[idx]->relpath_2_CAS);
	return (0);
}

/* Return the class name associated with this class index */
char *
cl_nam(int idx)
{
	if (cl_Classes && idx >= 0 && idx < cl_NClasses)
		return (cl_Classes[idx]->name);
	return (NULL);
}

void
cl_setl(struct cl_attr **cl_lst)
{
	int	i;
	int	sn = -1;
	struct cl_attr	*pt;

	if (cl_lst) {
		for (cl_NClasses = 0; cl_lst[cl_NClasses]; cl_NClasses++)
			if (strcmp(cl_lst[cl_NClasses]->name, "none") == 0)
				if (sn == -1)
					sn = cl_NClasses;
		if (sn > 0) {
			pt = cl_lst[sn];
			for (i = sn; i > 0; i--)
				cl_lst[i] = cl_lst[i - 1];
			cl_lst[0] = pt;
		}
		i = 1;
		while (i < cl_NClasses) {
			if (strcmp(cl_lst[i]->name, "none") == 0)
				for (sn = i; sn < (cl_NClasses - 1); sn++)
					cl_lst[sn] = cl_lst[sn + 1];
			i++;
		}
		cl_Classes = cl_lst;
	} else {
		cl_Classes = NULL;
		cl_NClasses = -1;
	}
}

/*
 * Scan the given environment variable for an occurrance of the given
 * class name. Return 0 if not found or 1 if found.
 */
static unsigned
is_in_env(char *class_name, char *paramname, char **paramvalue, int *noentry)
{
	unsigned retval = 0;
	char *test_class;

	if (class_name && *class_name) {
		/*
		 * If a prior getenv() has not failed and there is no
		 * environment string then get environment info on
		 * this parameter.
		 */
		if (!(*noentry) && *paramvalue == NULL) {
			*paramvalue = getenv(paramname);
			if (*paramvalue == NULL)
				(*noentry)++;
		}

		/* If there's something there, evaluate it. */
		if (!(*noentry)) {
			int n;

			n = strlen(class_name);	/* end of class name */
			test_class = *paramvalue;	/* environ ptr */

			while (test_class = strstr(test_class, class_name)) {
				/*
				 * At this point we have a pointer to a
				 * substring within param that matches
				 * class_name for its length, but class_name
				 * may be a substring of the test_class, so
				 * we check that next.
				 */
				if (isspace(*(test_class + n)) ||
				    *(test_class + n) == '\0') {
					retval = 1;
					break;
				}
				if (*(++test_class) == '\0')
					break;
			}
		}
	}
	return (retval);
}

/* Assign source path verification level to this class */
static unsigned
s_verify(char *class_name)
{
	static int noentry;
	static char *noverify;

	if (class_name == NULL) {	/* initialize */
		noentry = 0;
		noverify = NULL;
	} else {
		if (is_in_env(class_name, "PKG_SRC_NOVERIFY", &noverify,
		    &noentry))
			return (NOVERIFY);
		else
			return (DEFAULT);
	}
	return(0);
}

/*
 * Set destination verify to default. This is usually called by pkgdbmerg()
 * in order to correct verification conflicts.
 */
void
cl_def_dverify(int idx)
{
	if (cl_Classes && idx >= 0 && idx < cl_NClasses)
		cl_Classes[idx]->dst_verify = DEFAULT;
}

/* Assign destination path verification level to this path. */
static unsigned
d_verify(char *class_name)
{
	static int noentry;
	static char *qkverify;

	if (class_name == NULL) {	/* initialize */
		noentry = 0;
		qkverify = NULL;
	} else {
		if (is_in_env(class_name, "PKG_DST_QKVERIFY", &qkverify,
		    &noentry))
			return (QKVERIFY);
		else
			return (DEFAULT);
	}
	return(0);
}

/* Assign CAS path type to this class */
static unsigned
s_pathtype(char *class_name)
{
	static int noentry;
	static char *type_list;

	if (class_name == NULL) {	/* initialize */
		noentry = 0;
		type_list = NULL;
	} else {
		if (is_in_env(class_name, "PKG_CAS_PASSRELATIVE", &type_list,
		    &noentry))
			return (REL_2_CAS);
		else
			return (DEFAULT);
	}
	return(0);
}
