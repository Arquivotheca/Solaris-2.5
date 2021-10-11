/*
 * Copyright (c) 1992 - 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)fnsp_nisplus_root.cc	1.2 95/01/29 SMI"

#include <xfn/xfn.hh>
#include <rpcsvc/nis.h>
#include <synch.h>

#include <string.h>

/*
 * Code that figures out whether NIS+ name is in home or foreign
 * NIS+ hierarchy
 */

extern "C" nis_name __nis_local_root();

static const FN_string*
FNSP_get_root_name()
{
	static	FN_string *root_directory = 0;
	static	mutex_t root_directory_lock = DEFAULTMUTEX;
	char *rootdir;

	if ((root_directory == 0) && (rootdir = __nis_local_root())) {
		mutex_lock(&root_directory_lock);
		if (root_directory == 0)
			root_directory = new
			    FN_string((unsigned char *)rootdir);
		mutex_unlock(&root_directory_lock);
	}
	return (root_directory);
}

/* returns whether 'child' is in the same domain as 'parent' */

static int
__in_domain_p(const char *child, const char *parent, size_t clen, size_t plen)
{
	if (plen > clen) {
		/* cannot be parent if name is longer than child */
		return (0);
	}

	size_t start = clen - plen;

	if (strcasecmp(&child[start], parent) != 0) {
		/* tail end of child's name is not that of parent */
		return (0);
	}

	if (start == 0)
		return (1); /* parent == child */

	/*
	 * p=abc.com. c=xabc.com. should NOT be equal
	 * p=abc.com. c=x.abc.com. should be equal
	 */
	if (child[start-1] == '.')
		return (1);
	return (0);
}

/* Returns 1 if given name is in same hierarchy as current NIS+ root */
int
FNSP_home_hierarchy_p(const FN_string &name)
{
	const FN_string* root = FNSP_get_root_name();
	unsigned int sstatus;

	if (root == 0)
		return (0);

	return (__in_domain_p((const char *)name.str(&sstatus),
			    (const char *)root->str(&sstatus),
			    name.charcount(),
			    root->charcount()));
}

/* Returns 1 if given name could be potential ancester of current NIS+ root */
int
FNSP_potential_ancestor_p(const FN_string &name)
{
	const FN_string* root = FNSP_get_root_name();
	unsigned int sstatus;

	if (root == 0)
		return (0); /* config error */

	return (__in_domain_p((const char *)root->str(&sstatus),
			    (const char *)name.str(&sstatus),
			    root->charcount(),
			    name.charcount()));
}

unsigned int
FNSP_set_access_flags(const FN_string &name, unsigned int &flags)
{
	if (FNSP_home_hierarchy_p(name) == 0)
		flags |= NO_AUTHINFO;

	return (flags);
}
