/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef lint
static char sccsid[] =
	"@(#)utilities.c 1.9 90/11/12 SMI"; /* from UCB 5.2 8/5/85 */
#endif not lint

#ident	"@(#)utilities.c 1.8 93/11/10"

#include "restore.h"
#include <ctype.h>

/*
 * Insure that all the components of a pathname exist.
 */
void
pathcheck(name)
	char *name;
{
	register char *cp;
	struct entry *ep;
	char *start;

	start = strchr(name, '/');
	if (start == 0)
		return;
	for (cp = start; *cp != '\0'; cp++) {
		if (*cp != '/')
			continue;
		*cp = '\0';
		ep = lookupname(name);
		if (ep == NIL) {
			ep = addentry(name, psearch(name), NODE);
			newnode(ep);
		}
		ep->e_flags |= NEW|KEEP;
		*cp = '/';
	}
}

/*
 * Change a name to a unique temporary name.
 */
void
mktempname(ep)
	register struct entry *ep;
{
	char oldname[MAXPATHLEN];

	if (ep->e_flags & TMPNAME)
		badentry(ep, gettext("mktempname: called with TMPNAME"));
	ep->e_flags |= TMPNAME;
	(void) strcpy(oldname, myname(ep));
	freename(ep->e_name);
	ep->e_name = savename(gentempname(ep));
	ep->e_namlen = strlen(ep->e_name);
	renameit(oldname, myname(ep));
}

/*
 * Generate a temporary name for an entry.
 */
char *
gentempname(ep)
	struct entry *ep;
{
	static char name[MAXPATHLEN];
	struct entry *np;
	long i = 0;

	for (np = lookupino(ep->e_ino); np != NIL && np != ep; np = np->e_links)
		i++;
	if (np == NIL)
		badentry(ep, gettext("not on ino list"));
	(void) sprintf(name, "%s%ld%lu", TMPHDR, i, ep->e_ino);
	return (name);
}

/*
 * Rename a file or directory.
 */
void
renameit(from, to)
	char *from, *to;
{
	if (rename(from, to) < 0) {
		(void) fprintf(stderr,
			gettext("Warning: cannot rename %s to %s"),
			from, to);
		(void) fflush(stderr);
		perror("");
		return;
	}
	vprintf(stdout, gettext("rename %s to %s\n"), from, to);
}

/*
 * Create a new node (directory).
 */
void
newnode(np)
	struct entry *np;
{
	char *cp;

	if (np->e_type != NODE)
		badentry(np, gettext("newnode: not a node"));
	cp = myname(np);
	if (mkdir(cp, 0777) < 0) {
		np->e_flags |= EXISTED;
		(void) fprintf(stderr, gettext("Warning: "));
		(void) fflush(stderr);
		(void) fprintf(stderr, "%s: %s\n", cp, strerror(errno));
		return;
	}
	vprintf(stdout, gettext("Make node %s\n"), cp);
}

/*
 * Remove an old node (directory).
 */
void
removenode(ep)
	register struct entry *ep;
{
	char *cp;

	if (ep->e_type != NODE)
		badentry(ep, gettext("removenode: not a node"));
	if (ep->e_entries != NIL)
		badentry(ep, gettext("removenode: non-empty directory"));
	ep->e_flags |= REMOVED;
	ep->e_flags &= ~TMPNAME;
	cp = myname(ep);
	if (rmdir(cp) < 0) {
		(void) fprintf(stderr, gettext("Warning: "));
		(void) fflush(stderr);
		(void) fprintf(stderr, "%s: %s\n", cp, strerror(errno));
		return;
	}
	vprintf(stdout, gettext("Remove node %s\n"), cp);
}

/*
 * Remove a leaf.
 */
void
removeleaf(ep)
	register struct entry *ep;
{
	char *cp;

	if (ep->e_type != LEAF)
		badentry(ep, gettext("removeleaf: not a leaf"));
	ep->e_flags |= REMOVED;
	ep->e_flags &= ~TMPNAME;
	cp = myname(ep);
	if (unlink(cp) < 0) {
		(void) fprintf(stderr, gettext("Warning: "));
		(void) fflush(stderr);
		(void) fprintf(stderr, "%s: %s\n", cp, strerror(errno));
		return;
	}
	vprintf(stdout, gettext("Remove leaf %s\n"), cp);
}

/*
 * Create a link.
 */
linkit(existing, new, type)
	char *existing, *new;
	int type;
{
	char linkbuf[MAXPATHLEN];
	struct stat s1[1], s2[1];
	int l;

	if (type == SYMLINK) {
		if (symlink(existing, new) < 0) {
			if (((l = readlink(new, linkbuf,
			    sizeof (linkbuf))) > 0) &&
			    (l == strlen(existing)) &&
			    (strncmp(linkbuf, existing, l) == 0)) {
				vprintf(stdout,
				    gettext("Symbolic link %s->%s ok\n"),
				    new, existing);
			    return (GOOD);
			} else {
			    (void) fprintf(stderr, gettext(
			    "Warning: cannot create symbolic link %s->%s: "),
			    new, existing);
			    (void) fflush(stderr);
			    perror("");
			    return (FAIL);
			}
		}
	} else if (type == HARDLINK) {
		if (link(existing, new) < 0) {
		    if ((stat(existing, s1) == 0) &&
			(stat(new, s2) == 0) &&
			(s1->st_dev == s2->st_dev) &&
			(s1->st_ino == s2->st_ino)) {
			    vprintf(stdout,
				gettext("Hard link %s->%s ok\n"),
				new, existing);
			    return (GOOD);
		    } else {
			(void) fprintf(stderr, gettext(
				"Warning: cannot create hard link %s->%s: "),
				new, existing);
			(void) fflush(stderr);
			perror("");
			return (FAIL);
		    }
		}
	} else {
		panic(gettext("%s: unknown type %d\n"), "linkit", type);
		return (FAIL);
	}
	if (type == SYMLINK)
		vprintf(stdout, gettext("Create symbolic link %s->%s\n"),
			new, existing);
	else
		vprintf(stdout, gettext("Create hard link %s->%s\n"),
			new, existing);
	return (GOOD);
}

/*
 * find lowest number file (above "start") that needs to be extracted
 */
ino_t
lowerbnd(start)
	ino_t start;
{
	register struct entry *ep;

	for (; start < maxino; start++) {
		ep = lookupino(start);
		if (ep == NIL || ep->e_type == NODE)
			continue;
		if (ep->e_flags & (NEW|EXTRACT))
			return (start);
	}
	return (start);
}

/*
 * find highest number file (below "start") that needs to be extracted
 */
ino_t
upperbnd(start)
	ino_t start;
{
	register struct entry *ep;

	for (; start > ROOTINO; start--) {
		ep = lookupino(start);
		if (ep == NIL || ep->e_type == NODE)
			continue;
		if (ep->e_flags & (NEW|EXTRACT))
			return (start);
	}
	return (start);
}

/*
 * report on a badly formed entry
 */
void
badentry(ep, msg)
	register struct entry *ep;
	char *msg;
{

	(void) fprintf(stderr, gettext("bad entry: %s\n"), msg);
	(void) fprintf(stderr, gettext("name: %s\n"), myname(ep));
	(void) fprintf(stderr, gettext("parent name %s\n"),
		myname(ep->e_parent));
	if (ep->e_sibling != NIL)
		(void) fprintf(stderr, gettext("sibling name: %s\n"),
			myname(ep->e_sibling));
	if (ep->e_entries != NIL)
		(void) fprintf(stderr, gettext("next entry name: %s\n"),
			myname(ep->e_entries));
	if (ep->e_links != NIL)
		(void) fprintf(stderr, gettext("next link name: %s\n"),
			myname(ep->e_links));
	if (ep->e_next != NIL)
		(void) fprintf(stderr, gettext("next hashchain name: %s\n"),
			myname(ep->e_next));
	(void) fprintf(stderr, gettext("entry type: %s\n"),
		ep->e_type == NODE ? gettext("NODE") : gettext("LEAF"));
	(void) fprintf(stderr, gettext("inode number: %lu\n"), ep->e_ino);
	panic(gettext("flags: %s\n"), flagvalues(ep));
}

/*
 * Construct a string indicating the active flag bits of an entry.
 */
char *
flagvalues(ep)
	register struct entry *ep;
{
	static char flagbuf[BUFSIZ];

	(void) strcpy(flagbuf, gettext("|NIL"));
	flagbuf[0] = '\0';
	if (ep->e_flags & REMOVED)
		(void) strcat(flagbuf, gettext("|REMOVED"));
	if (ep->e_flags & TMPNAME)
		(void) strcat(flagbuf, gettext("|TMPNAME"));
	if (ep->e_flags & EXTRACT)
		(void) strcat(flagbuf, gettext("|EXTRACT"));
	if (ep->e_flags & NEW)
		(void) strcat(flagbuf, gettext("|NEW"));
	if (ep->e_flags & KEEP)
		(void) strcat(flagbuf, gettext("|KEEP"));
	if (ep->e_flags & EXISTED)
		(void) strcat(flagbuf, gettext("|EXISTED"));
	return (&flagbuf[1]);
}

/*
 * Check to see if a name is on a dump tape.
 */
ino_t
dirlookup(name)
	char *name;
{
	ino_t ino;

	ino = psearch(name);
	if (ino == 0 || BIT(ino, dumpmap) == 0)
		(void) fprintf(stderr, gettext("%s is not on volume\n"), name);
	return (ino);
}

/*
 * Elicit a reply.
 */
reply(question)
	char *question;
{
	char *yesorno = gettext("yn");
	int c;

	do	{
		(void) fprintf(stderr, "%s? [%s] ", question, yesorno);
		(void) fflush(stderr);
		c = getc(terminal);
		while (c != '\n' && getc(terminal) != '\n') {
			if (ferror(terminal)) {
				(void) fprintf(stderr, gettext(
					"Error reading response\n"));
				(void) fflush(stderr);
				return (FAIL);
			}
			if (feof(terminal))
				return (FAIL);
		}
		if (isupper(c))
			c = tolower(c);
	} while (c != yesorno[0] && c != yesorno[1]);
	if (c == yesorno[0])
		return (GOOD);
	return (FAIL);
}

/*
 * handle unexpected inconsistencies
 */
#ifdef __STDC__
#include <stdarg.h>

/* VARARGS1 */
void
panic(const char *msg, ...)
{
	va_list	args;

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);
	if (reply(gettext("abort")) == GOOD) {
		if (reply(gettext("dump core")) == GOOD)
			abort();
		done(1);
	}
}
#else
#include <varargs.h>

/* VARARGS1 */
void
panic(va_alist)
	va_dcl
{
	va_list	args;
	char	*msg;

	va_start(args);
	msg = va_arg(args, char *);
	vfprintf(stderr, msg, args);
	va_end(args);
	if (reply(gettext("abort")) == GOOD) {
		if (reply(gettext("dump core")) == GOOD)
			abort();
		done(1);
	}
#endif

/*
 * Locale-specific version of ctime
 */
char *
lctime(tp)
	time_t	*tp;
{
	static char buf[256];
	struct tm *tm;

	tm = localtime(tp);
	(void) strftime(buf, sizeof (buf), "%c\n", tm);
	return (buf);
}
