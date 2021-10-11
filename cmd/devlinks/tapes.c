#ident	"@(#)tapes.c	1.4	93/09/16 SMI"
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/sunddi.h>
#include <locale.h>
#include <libdevinfo.h>

#include "tapehdr.h"


const char *tape_rdir;

const char *
get_devfs_base(const char *lnname)
{
    /*
     * At present this code works for names of the form:
     *	../../devices/junk0.../jnukN/xx@[targetno,]unito:ent_type
     */
	static char tmpbuf[512];
	char *cp, *sp;

	strcpy (tmpbuf, lnname);

	if ((sp = strrchr(tmpbuf, '/')) == NULL)
	    sp = tmpbuf;

	if ((sp = strrchr(sp, ':')) == NULL)
	    return NULL;

	*sp = '\0';

	return tmpbuf;
}

const char *
get_devfs_var(const char *lnname)
{
    /*
     * At present this code works for names of the form:
     *	../../devices/junk0.../jnukN/xx@[targetno,]unito:ent_type
     */
	static char tmpbuf[128];
	char *cp;
	const char *sp;

	
	/* First make sure we are looking only at last component */
	if ((sp = strrchr(lnname, '/')) == NULL)
	    sp = lnname;

	/* Now look for partition part (after the ':') */
	if ((sp = strrchr(sp, ':')) == NULL)
	    return NULL;

	strcpy(tmpbuf, sp+1);

	return tmpbuf;
}

struct tapedev *tpdev[MAX_NTAPES];

void
addtnode(int devno,		/* Tape device no. in /dev - land */
	 const char *attributes, /* atrribute string - common */
	 const char *fsbase,	/* devfs ctrlr name */
	 const char *fsvar,	/* devfs variant - same as attributes, we hope */
	 boolean_t in_dev	/* This entry was found in /dev */
	 )
{
    int i;

/*
fprintf(stderr, "Adding %s node for %s:%s\n", in_dev?"DEV":"DEVFS", fsbase, fsvar);
*/

    /*
     * If device number is undefined (<0), look for matching device
     * using 'fsbase' string, and assign new device if not found.
     */
    if (devno < 0) {
	int min_unused = -1;

	for (i = 0; i < MAX_NTAPES; i++) {
	    if (tpdev[i] == NULL) {
		if (min_unused < 0)
		    min_unused = i;
	    }
	    else if (strcmp(tpdev[i]->devfsname, fsbase) == 0) {
		devno = i;
		break;
	    }
	}

	if (devno < 0) {
	    /* Not found */
	    if (min_unused < 0) {
		/* and no free nodes, so warn about failure to add tape device */
		wmessage("Too many tape devices - more than %d devices found\n",
		       MAX_NTAPES);
		return;
	    }
	    else
		devno = min_unused;
	}
    }
    /*
     * Make sure this device is known about; malloc space for it if not
     */
    if (tpdev[devno] == NULL) {
	tpdev[devno] = (struct tapedev *)s_calloc(1, sizeof(struct tapedev));
	tpdev[devno]->devfsname = s_strdup(fsbase);
    }

    /*
     * Verify link bases are correct - if not, fail!
     *
     * This is a fatal error because it is impossible for this program to know
     * which of the two links the system administrator would want to keep.
     * So the only choice is to abort and let the administrator clear up the mess
     * manually.
     */
    if (strcmp(tpdev[devno]->devfsname, fsbase) != 0) {
	fmessage(3, "Subdevice of tape /dev/rmt/%d are linked to %s and %s\n\
\t- remove one or other of these links before rerunning this program\n",
		 devno, fsbase, tpdev[devno]->devfsname);
    }

    /*
     * Now look for device link
     */
    for (i=0; i<MAXSUB; i++) {
	if (tpdev[devno]->link[i].devvar == NULL) {
	    /*
	     * End of list, and no entry, so add it in.
	     */
	    tpdev[devno]->link[i].devvar = s_strdup(attributes);
	    tpdev[devno]->link[i].devfsvar = s_strdup(fsvar);
	    tpdev[devno]->link[i].state = (in_dev ? LN_DANGLING : LN_MISSING);
	    return;
	}
	if (strcmp(tpdev[devno]->link[i].devvar, attributes) == NULL)
	    break;
    }
    if (i >= MAXSUB) {
	wmessage("Too many subdevices of tape %d\n", devno);
	return;
    }
    else {
	/*
	 * Already-existing entry, so verify consistency
	 */
	if (strcmp(tpdev[devno]->link[i].devfsvar, fsvar) != 0) {
	    /* Invalid minor device - warn and flag for correction */
	    tpdev[devno]->link[i].state = LN_INVALID;
	}
	else
	    tpdev[devno]->link[i].state = LN_VALID;
    }
}

void
do_tapedir(const char *dname)
{
    DIR *dp;
    int rc;
    int linksize;
    int tapeno;
    struct dirent *entp;
    struct stat sb;
    const char *devattr;
    const char *fsbase, *fsvar;
    char namebuf[PATH_MAX+1];
    char linkbuf[PATH_MAX+1];

    /*
     * Search a directory for special names
     */
    dp = opendir(dname);

    if (dp == NULL) {
	return;
    }

    while ((entp = readdir(dp)) != NULL) {
	if (strcmp(entp->d_name, ".") == 0 || strcmp(entp->d_name, "..") == 0)
	    continue;

	if (!isdigit(entp->d_name[0]))
	    /* Silently Ignore for now any names not stating with 0-9  */
	    continue;

	sprintf(namebuf, "%s/%s", dname, entp->d_name);

	if ((rc = lstat(namebuf, &sb)) < 0) {
	    wmessage ("Cannot stat %s\n", namebuf);
	    continue;
	}

	switch(sb.st_mode & S_IFMT) {
	case S_IFLNK:
	    linksize = readlink(namebuf,linkbuf, PATH_MAX);

	    if (linksize <= 0) {
		wmessage("Could not read symbolic link %s\n", namebuf);
		continue;
	    }

	    linkbuf[linksize] = '\0';
	    break;

	default:
	    wmessage("Should only have symbolic links in %s; \
%s is not a symbolic link\n", dname, namebuf);
	    continue;
	}
	/*
	 * To get here, we know file is link, starting with digit.
	 * So we presume it is a proper entry of form "<devno><flags>"
	 */
	/* First get number -- will not fail */
	(void)sscanf(entp->d_name, "%d", &tapeno);

	/*
	 * Now get optional terminating string
	 */
	for (devattr = entp->d_name; isdigit(*devattr); devattr++);

	/*
	 * OK, so now break apart devfs name (what link points to)
	 * into base and variant parts.  It would have been nice
	 * if these were identical, but this was not to be.
	 */
	if ((fsbase = get_devfs_base(linkbuf)) == NULL ||
	    (fsvar = get_devfs_var(linkbuf)) == NULL) {
	    wmessage("Device file %s is link to invalidly formatted node %s\n",
		     namebuf, linkbuf);
	};

	/*
	 * Add new entry to device node list
	 */
	addtnode(tapeno, devattr, fsbase, fsvar, B_TRUE);

    }

    closedir(dp);

    return;
}

void
get_dev_entries(void)
{
    /*
     * Search /dev/mt and /dev/rmt for entries, building or updating internal
     * nodes as necessary.
     */

    create_dirs(tape_rdir);	/* Make sure it exists */
    do_tapedir(tape_rdir);
}

/*
 * devfs_entry -- routine called when a 'TAPE' devfs entry is found
 *
 * This routine is called by devfs_find() when a matching devfs entry is found.
 * It is passwd the name of the devfs entry.
 */
void
devfs_entry(const char *devfsnm, const char *ignored,
    const dev_info_t *dip, struct ddi_minor_data *dmip,
    struct ddi_minor_data *dmap)
{
    const char *fsbase, *fsvar;
    char devicenm[PATH_MAX];

    if ((fsbase = get_devfs_base(devfsnm)) == NULL ||
	(fsvar = get_devfs_var(devfsnm)) == NULL) {
	wmessage("Device file entry %s has invalid format -- ignoring\n",
		 devfsnm);
	return;
    };

    sprintf(devicenm, "../../devices/%s", fsbase);

    addtnode(-1, fsvar, devicenm, fsvar, B_FALSE);
}

void
get_devfs_entries(void)
{
    devfs_find(DDI_NT_TAPE, devfs_entry, 0);
}

void
remove_links(void)
{
    int i, j;
    char nbuf[PATH_MAX];
    extern int errno;

    for (i=0; i<MAX_NTAPES; i++) {
	if (tpdev[i] == NULL)
	    continue;
	for (j=0; j<MAXSUB && tpdev[i]->link[j].devvar != 0; j++) {
	    if (tpdev[i]->link[j].state == LN_INVALID) {
		/* Make full name */
		sprintf(nbuf, "%s/%d%s", tape_rdir, i, tpdev[i]->link[j].devvar);

		if (unlink(nbuf) != 0) {
		    wmessage("Could not unlink %s because: %s\n",
			     nbuf, strerror(errno));
		}
		else
		    tpdev[i]->link[j].state = LN_MISSING;
	    }
	}
    }
}

void
add_links(void)
{
    int i, j;
    char devnmbuf[PATH_MAX];
    char devfsnmbuf[PATH_MAX];
    extern int errno;

    for (i=0; i<MAX_NTAPES; i++) {
	if (tpdev[i] == NULL)
	    continue;
	for (j=0; j<MAXSUB && tpdev[i]->link[j].devvar != 0; j++) {
	    if (tpdev[i]->link[j].state == LN_MISSING) {
		/* Make full names */
		sprintf(devnmbuf, "%s/%d%s", tape_rdir, i, tpdev[i]->link[j].devvar);
		sprintf(devfsnmbuf, "%s:%s", tpdev[i]->devfsname,
			tpdev[i]->link[j].devfsvar);

		if (symlink(devfsnmbuf, devnmbuf) != 0) {
		    wmessage("Could not create symlink %s because: %s\n",
			     devnmbuf, strerror(errno));
		}
	    }
	}
    }
}

void
update_admin_db(void)
{
}

main(int argc, char **argv)
{
    extern int optind;
    char *rootdir = "";
    int c;

    (void) setlocale(LC_ALL, "");

    (void) textdomain(TEXT_DOMAIN);

    while ((c = getopt(argc, argv, "r:")) != EOF)
	switch (c) {
	case 'r':
	    rootdir = optarg;
	    break;
	case '?':
	    fmessage(1, "Usage: tapes [-r root_directory]\n");
	}

    if (optind < argc)
	fmessage(1, "Usage: tapes [-r root_directory]\n");

    /*
     * Set address of tape raw dir
     */
    tape_rdir = s_malloc(strlen(rootdir) + sizeof("/dev/rmt"));
    sprintf((char *)tape_rdir, "%s%s", rootdir, "/dev/rmt");
				/* Explicitly override const attribute */

    /*
     * Start building list of tape devices by looking through /dev/[r]mt.
     */
    get_dev_entries();

    /*
     * Now add to this real device configuration from /devfs
     */
    get_devfs_entries();

    /*
     * Delete unwanted or incorrect nodes
     */
    remove_links();

    /*
     * Make new links
     */
    add_links();

    /*
     * Finally add admin database info -- really just the /dev/SA and /dev/rSA
     * stuff.
     */
    update_admin_db();

    return(0);
}
