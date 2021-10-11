
#ident	"@(#)perms.c 1.7 92/03/11"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include "recover.h"
#include <pwd.h>
#include <grp.h>

static int thisuser, thisgroup;
static int ngroups;
#ifdef USG
static gid_t *gidset;
#else
static int *gidset;
#endif
static char thishost[BCHOSTNAMELEN+1];
static int perms_initted;

#ifdef __STDC__
static void setperms(void);
#else
static void setperms();
#endif

/*
 * see if the permissions on the given dnode are a match with
 * the credentials of the current user.
 */
/*ARGSUSED*/
permchk(dp, m, curhost)
	struct dnode *dp;
	int m;
	char *curhost;
{
	register int i;

	setperms();
	/*
	 * The super-user on the database machine sees all,
	 * since he could access the data directly anyway.
	 */
	if (thisuser == 0)
		return (0);

	if (thisuser != dp->dn_uid) {
		m >>= 3;	/* check `group' perms */
		if (thisgroup == dp->dn_gid)
			goto found;
		for (i = 0; i < ngroups; i++)
			if (dp->dn_gid == gidset[i])
				goto found;
		m >>= 3;	/* check `other' perms */
	}

found:
	if ((dp->dn_mode & m) == m)
		return (0);
	return (1);	/* EACCESS */
}

/*
 * put credential info into the `find' argument structure which
 * is sent to the database server.
 */
getperminfo(a)
	struct db_findargs *a;
{
	setperms();
	a->myhost = thishost;
	a->uid = thisuser;
	a->gid = thisgroup;
	a->ngroups = ngroups;
	bcopy((char *)gidset, (char *)a->gidlist, ngroups*sizeof (int));
	return (0);
}

/*
 * get credentials for the current user
 */
static void
#ifdef __STDC__
setperms(void)
#else
setperms()
#endif
{
	if (perms_initted)
		return;

	thisuser = getuid();
	thisgroup = getgid();
	if ((ngroups = sysconf(_SC_NGROUPS_MAX)) == -1) {
		perror("sysconf");
		exit(1);
	}
#ifdef USG
	if ((gidset = (gid_t *)malloc(
			(unsigned)(ngroups*sizeof (gid_t)))) == NULL) {
#else
	if ((gidset = (int *)malloc(
			(unsigned)(ngroups*sizeof (int)))) == NULL) {
#endif
		(void) fprintf(stderr,
			gettext("Cannot allocate group entries\n"));
		exit(1);
	}
	if ((ngroups = getgroups(ngroups, gidset)) == -1) {
		perror("getgroups");
		exit(1);
	}
	if (gethostname(thishost, BCHOSTNAMELEN) == -1) {
		perror("gethostname");
		exit(1);
	}
	perms_initted = 1;
}
