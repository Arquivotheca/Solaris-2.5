#ident "@(#)disks.c	1.10	94/01/11 SMI"
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
#include <locale.h>
#include <sys/sunddi.h>
#include <libdevinfo.h>

#include "diskhdr.h"


const char *disk_bdir;		/* Not really const in main, */
const char *disk_rdir;		/* but casts handle it there */

/*
 * get_devfs_ctrlr -- get base of devfs name.
 *
 * This gets the name of the 'controller' in the devfs tree -- that is,
 * it gets the directory name under which all devices attached to the
 * controller or host adapter will live.
 *
 * This is potentially quite difficult - there are many possible controller
 * configurations.  There are not nearly so many drive name possiblities, so
 * this command parses backwards along the provided name until it reaches the
 * controller entry.
 */
const char *
get_devfs_ctrlr(const char *lnname)
{
    /*
     * At present this code works for names of the form:
     *	.../../devices/junk0.../jnukN/xx@[targetno,]unito:devlet[,raw]
     */
	static char tmpbuf[PATH_MAX];
	char *cp, *sp;

	strcpy (tmpbuf, lnname);

	if ((sp = strrchr(tmpbuf, '/')) == NULL)
	    sp = tmpbuf;

	if ((sp = strrchr(sp, '@')) == NULL)
	    return NULL;

	*sp = '\0';

	return tmpbuf;

}

const char *
get_devfs_unit(const char *lnname)
{
    /*
     * At present this code works for names of the form:
     *	/dev/devfs/junk0.../jnukN/xx@[targetno,]unito:devlet[,raw]
     */
	static char tmpbuf[PATH_MAX];
	char *cp;
	const char *sp;

	
	if ((sp = strrchr(lnname, '/')) == NULL)
	    sp = lnname;

	if ((sp = strrchr(sp, '@')) == NULL)
	    return NULL;

	strcpy(tmpbuf, sp+1);

	if ((cp = strchr(tmpbuf, ':')) == NULL)
	    return NULL;
	else
	    *cp = '\0';

	return tmpbuf;
}

const char *
get_devfs_part(const char *lnname)
{
    /*
     * At present this code works for names of the form:
     *	/dev/devfs/junk0.../jnukN/xx@[targetno,]unito:devlet[,raw]
     */
	static char tmpbuf[PATH_MAX];
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

struct diskctrlr *ctrlr[MAX_NCTRLRS];

void
adddnode(int ctrlrno,		/* Ctrlr No in /dev/dsk -- -ve if not known */
	 const char *devdsknm,	/* Name suffix (after cn) in /dev/dsk-land */
	 const int partno,	/* Partition no is common in dev and devfs land */
	 const char *fsctrlr,	/* devfs ctrlr name prefix - includes '../../devices' */
	 const char *fsunit,	/* devfs unit name (between the '@' and the ':') */
	 const char *fspart,	/* after the ':' */
	 const int devtype,	/* Raw or block */
	 boolean_t in_dev	/* This entry was found in /dev */
	 )
{

    int i;

    /*
     * If controller number is undefined (0), look for matching controller
     * using 'fsctrlr' string, and assign new controller if not found.
     */
    if (ctrlrno < 0) {
	int min_unused = -1;

	for (i = 0; i < MAX_NCTRLRS; i++) {
	    if (ctrlr[i] == NULL) {
		if (min_unused < 0)
		    min_unused = i;
	    }
	    else if (strcmp(ctrlr[i]->devfsctrlrnm, fsctrlr) == 0) {
		ctrlrno = i;
		break;
	    }
	}

	if (ctrlrno < 0) {
	    /* Not found */
	    if (min_unused < 0) {
		/* and no free nodes, so fatal error */
		fmessage(1, "Too many controllers - more than %d controllers found\n",
		       MAX_NCTRLRS);
	    }
	    else
		ctrlrno = min_unused;
	}
    }
	
    /*
     * Make sure the controller is known about; malloc space for it if not
     */
    if (ctrlr[ctrlrno] == NULL) {
	ctrlr[ctrlrno] = (struct diskctrlr *)s_calloc(1, sizeof(struct diskctrlr));
	ctrlr[ctrlrno]->devfsctrlrnm = s_strdup(fsctrlr);
    }

    /*
     * Verify link bases are correct - if not, fail for now
     */
    if (strcmp(ctrlr[ctrlrno]->devfsctrlrnm, fsctrlr) != 0) {
	fmessage(3, "Disk controller dsk/%d is linked to %s and %s\n",
		 ctrlrno, fsctrlr, ctrlr[ctrlrno]->devfsctrlrnm);
    }

    /*
     * Now look for disk
     */
    for (i=0; i<MAXSUB; i++) {
	if (ctrlr[ctrlrno]->dsk[i] == NULL) {
	    /*
	     * End of list, and no entry, so add it in.
	     */
	    ctrlr[ctrlrno]->dsk[i] = (struct disk *)s_calloc(1, sizeof(struct disk));
	    ctrlr[ctrlrno]->dsk[i]->devdsknm = s_strdup(devdsknm);
	    ctrlr[ctrlrno]->dsk[i]->devfsdsknm = s_strdup(fsunit);
	    break;
	}
	else if (strcmp(ctrlr[ctrlrno]->dsk[i]->devdsknm, devdsknm) == NULL)
	    break;
    }
    if (i >= MAXSUB)
	fmessage(2, "Too many subdevices of disk controller %d\n", ctrlrno);

    /*
     * Now we know correct disk; so add or verify device
     */
    if (ctrlr[ctrlrno]->dsk[i]->part[partno][devtype].devfspart != NULL) {
	/* devfspart already present, so check it was the same */
	if (strcmp(ctrlr[ctrlrno]->dsk[i]->part[partno][devtype].devfspart, fspart)
	    != 0) {
	    /* Invalid minor device - warn and flag for correction */
	    ctrlr[ctrlrno]->dsk[i]->part[partno][devtype].state = LN_INVALID;
	    ctrlr[ctrlrno]->dsk[i]->part[partno][devtype].devfspart =
		s_strdup(fspart);
	}
	else {
	    /* Clear flags; link is present and correct */
	    ctrlr[ctrlrno]->dsk[i]->part[partno][devtype].state = LN_VALID;
	}
    }
    else {
	/*
	 * devfspart not present; This is first time we encountered this entry.
	 * So we flag it as DANGLING (if found in /dev) or 'missing' (if found
	 * in /devfs).
	 *
	 * At present 'DANGLING' is a not-needed state, but may be used to remove
	 * unneeded links later.
	 */
	ctrlr[ctrlrno]->dsk[i]->part[partno][devtype].devfspart = s_strdup(fspart);
	/* Assume missing for now */
	ctrlr[ctrlrno]->dsk[i]->part[partno][devtype].state =
	    (in_dev == B_TRUE) ? LN_DANGLING : LN_MISSING;
    }
}

/*
 * is_raw_devfs -- check if name contains the ",raw" modifier
 *    this is always the second minor name component, if present.
 */
static int
is_raw_devfs(const char *name)
{
    const char *cp;

    /* Skip the main part of the name */
    if ((cp = strrchr(name, ':')) != NULL)
	cp++;			/* And position after the ':' */
    else
	cp = name;

    while ((cp = strchr(cp, ',')) != NULL) {
	cp++;

	if (strncmp(cp, "raw", 3) == 0 &&
	    (cp[3] == ',' || cp[3] == '\0'))
	    return(1);
    }

    return 0;
}

void
do_diskdir(const char *dname, const int dtype)
{
    DIR *dp;
    int rc;
    int linksize;
    int ctrlno, targno, diskno, partno;
    struct dirent *entp;
    struct stat sb;
    const char *fsctrlr, *fsunit, *fspart;
    char namebuf[PATH_MAX+1];
    char linkbuf[PATH_MAX+1];
    char devattr[20];

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

	if (entp->d_name[0] != 'c')
	    /* Silently Ignore for now any names not stating with c  */
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
	    wmessage("%s is not a symbolic link\n", namebuf);
	    continue;
	}
	/*
	 * To get here, we know file is link, starting with digit.
	 * So we presume it is a proper entry of form "cntndnsn" or 
	 * "cntndnpn or "cndnsn"
	 */
	/* First get number -- will not fail */
	if (sscanf(entp->d_name, "c%dt%dd%ds%d", &ctrlno, &targno, &diskno, &partno)
	    == 4) {
	    sprintf(devattr, "t%dd%d", targno, diskno);
	}
	else if (sscanf(entp->d_name, "c%dt%dd%dp%d", &ctrlno, &targno, &diskno, &partno)
	    == 4) {
	    sprintf(devattr, "t%dd%d", targno, diskno);
	    partno += MAXSLICE;
	}
	else if (sscanf(entp->d_name, "c%dd%ds%d", &ctrlno, &diskno, &partno) == 3) {
	    sprintf(devattr, "d%d", diskno);
	}
	else if (sscanf(entp->d_name, "c%dd%dp%d", &ctrlno, &diskno, &partno)
	    == 3) {
	    sprintf(devattr, "d%d", diskno);
	    partno += MAXSLICE;
	}
	else {
	    wmessage ("Skipping invalid format entry %s\n", entp->d_name);
	    continue;
	}

	/*
	 * OK, so now break apart devfs name (what link points to)
	 * into base and variant parts.  It would have been nice
	 * if these were identical, but this was not to be.
	 */
	if ((fsctrlr = get_devfs_ctrlr(linkbuf)) == NULL ||
	    (fsunit = get_devfs_unit(linkbuf)) == NULL ||
	    (fspart = get_devfs_part(linkbuf)) == NULL) {
	    fmessage(1, "Device file %s is link to invalidly formatted node %s\n",
		     namebuf, linkbuf);
	};

	/* Check for possible ",raw" in fspart */
	if (is_raw_devfs(fspart)) {
	    if (dtype != LN_D_RAW) {
		fmessage(1, "Device file %s is link to invalidly formatted node %s\n",
			 namebuf, linkbuf);
	    }
	}

	/*
	 * Add new entry to device node list
	 */
	adddnode(ctrlno, devattr, partno, fsctrlr, fsunit, fspart, dtype, B_TRUE);

    }

    closedir(dp);

    return;
}

void
get_dev_entries(void)
{
    /*
     * Search /dev/dsk and /dev/rdsk for entries, building or updating internal
     * nodes as necessary.
     */
    /*
     * create directories if they don't exist
     */
    create_dirs(disk_bdir);
    do_diskdir(disk_bdir, LN_D_BLK);
    create_dirs(disk_rdir);
    do_diskdir(disk_rdir, LN_D_RAW);
}

/*
 * devfs_entry -- routine called when a 'HARD_DISK' devfs entry is found
 *
 * This routine is called by devfs_find() when a matching devfs entry is found.
 * It is passwd the name of the devfs entry.
 */
void
devfs_entry(const char *devfsnm, const char *devfstype,
    const dev_info_t *dip, struct ddi_minor_data *dmip,
    struct ddi_minor_data *dmap)
{
    const char *fsctrlr, *fsunit, *fspart; /* In devfs-land */
    int partno;			/* In dev-land */
    int devtype;		/* Char or block */
    char ctrlrnm[PATH_MAX];
    char devattr[16];		/* In dev-land */
    int lun;
    int targ;
    int targ_flag = 0;
    char part_ch;
    char *cp;
    int name_addr_num;

    /*
     * First of all check type of device found.  This program does not handle
     * all ddi_block devices, so make sure what has been found is a block device
     * this program is interested in.
     *
     * At present this program handles plain block devices 'DDI_NT_BLOCK', and 
     * external channel devices (SCSI and IPI) 'DDI_NT_BLOCK_CHAN', as well as
     * their CD equivalents.
     */
    if (strcmp(devfstype, DDI_NT_BLOCK) != 0 &&
	strcmp(devfstype, DDI_NT_BLOCK_CHAN) != 0 &&
	strcmp(devfstype, DDI_NT_CD) != 0 &&	
	strcmp(devfstype, DDI_NT_CD_CHAN) != 0)
	return;			/* Not a device-type we handle */

    /*
     * Split the name given into "controller", "unit", and 
     * "partno" parts.
     */
    if ((fsctrlr = get_devfs_ctrlr(devfsnm)) == NULL ||
	(fsunit = get_devfs_unit(devfsnm)) == NULL ||
	(fspart = get_devfs_part(devfsnm)) == NULL) {
	wmessage("Device file entry %s has invalid format -- ignoring\n",
		 devfsnm);
	return;
    };

    /* add prefix to devfs name */
    sprintf(ctrlrnm, "../../devices/%s", fsctrlr);

    /*
     * Check if 'raw' or 'block', and set devtype accordingly.
     * (Scan unitp for a minro-filed of ",raw")
     */
    devtype = is_raw_devfs(fspart) ? LN_D_RAW : LN_D_BLK;

    partno = fspart[0] - 'a';	/* Only 26 chars -- sigh! */

    /*
     * Now determine the appropriate sub-device name for the disk (using
     * the 'unit' part of the devfs name).  This name uses the 'unit', 'slice'
     * and possibly 'target' number from the actual unit name.
     */
    if (strcmp(devfstype, DDI_NT_BLOCK_CHAN) == 0 ||
	strcmp(devfstype, DDI_NT_CD_CHAN) == 0)
	    targ_flag = 1;
    if ((name_addr_num = sscanf(fsunit, "%x,%x", &targ, &lun)) != 2) {
	if (targ_flag || (name_addr_num = sscanf(fsunit, "%x", &lun)) != 1) {
	    wmessage("Device file entry %s has invalid format -- ignoring\n",
		     devfsnm);
	    return;
	}
    }

    if (targ_flag)
	sprintf(devattr, "t%dd%d", targ, lun);
    else {
	if (name_addr_num == 2)
		sprintf(devattr, "d%d", targ);
	else
		sprintf(devattr, "d%d", lun);
    }


    /*
     * Now do actual adddnode()
     */
    adddnode(-1, devattr, partno, ctrlrnm, fsunit, fspart, devtype, B_FALSE);
}

void
get_devfs_entries(void)
{
    devfs_find(DDI_NT_BLOCK ":", devfs_entry, 0);
}

void
remove_links(void)
{
    int i, j, k;
    char nbuf[PATH_MAX];
    extern int errno;

    for (i=0; i<MAX_NCTRLRS; i++) {
	if (ctrlr[i] == NULL)
	    continue;
	for (j=0; j<MAXSUB; j++) {
	    if (ctrlr[i]->dsk[j] == NULL)
		continue;
	    for (k=0; k<MAXPART; k++) {
		if (ctrlr[i]->dsk[j]->part[k][LN_D_BLK].state == LN_INVALID ||
		    ctrlr[i]->dsk[j]->part[k][LN_D_BLK].state == LN_DANGLING) {
		    /* Make full name */
		    if (k < MAXSLICE) {
		    	sprintf(nbuf, "%s/c%d%ss%d", disk_bdir, i,
			    ctrlr[i]->dsk[j]->devdsknm, k);
		    } else {
		    	sprintf(nbuf, "%s/c%d%sp%d", disk_bdir, i,
			    ctrlr[i]->dsk[j]->devdsknm, (k-MAXSLICE));
		    }
		    if (unlink(nbuf) != 0) {
			wmessage("Could not unlink %s because: %s\n",
				 nbuf, strerror(errno));
		    }
		    else {
			if (ctrlr[i]->dsk[j]->part[k][LN_D_BLK].state == LN_INVALID)
			    ctrlr[i]->dsk[j]->part[k][LN_D_BLK].state = LN_MISSING;
		    }
		}

		if (ctrlr[i]->dsk[j]->part[k][LN_D_RAW].state == LN_INVALID ||
		    ctrlr[i]->dsk[j]->part[k][LN_D_RAW].state == LN_DANGLING) {
		    /* Make full name */
		    if (k < MAXSLICE) {
		    	sprintf(nbuf, "%s/c%d%ss%d", disk_rdir, i,
			    ctrlr[i]->dsk[j]->devdsknm, k);
		    } else {
		    	sprintf(nbuf, "%s/c%d%sp%d", disk_rdir, i,
			    ctrlr[i]->dsk[j]->devdsknm, (k-MAXSLICE));
		    }
		    if (unlink(nbuf) != 0) {
			wmessage("Could not unlink %s because: %s\n",
				 nbuf, strerror(errno));
		    }
		    else {
			if (ctrlr[i]->dsk[j]->part[k][LN_D_RAW].state == LN_INVALID)
			    ctrlr[i]->dsk[j]->part[k][LN_D_RAW].state = LN_MISSING;
		    }
		}
	    }
	}
    }
}

void
add_links(void)
{
    int i, j, k;
    char devnmbuf[PATH_MAX];
    char devfsnmbuf[PATH_MAX];
    extern int errno;

    for (i=0; i<MAX_NCTRLRS; i++) {
	if (ctrlr[i] == NULL)
	    continue;
	for (j=0; j<MAXSUB; j++) {
	    if (ctrlr[i]->dsk[j] == NULL)
		continue;
	    for (k=0; k<MAXPART; k++) {
		if (ctrlr[i]->dsk[j]->part[k][LN_D_BLK].state == LN_MISSING) {
		    /* Make full name */
		    if (k < MAXSLICE) {
		    	sprintf(devnmbuf, "%s/c%d%ss%d", disk_bdir, i,
			    ctrlr[i]->dsk[j]->devdsknm, k);
		    } else {
		    	sprintf(devnmbuf, "%s/c%d%sp%d", disk_bdir, i,
			    ctrlr[i]->dsk[j]->devdsknm, (k-MAXSLICE));
		    }
		    sprintf(devfsnmbuf, "%s@%s:%s", ctrlr[i]->devfsctrlrnm,
			ctrlr[i]->dsk[j]->devfsdsknm,
			ctrlr[i]->dsk[j]->part[k][LN_D_BLK].devfspart);

		    if (symlink(devfsnmbuf, devnmbuf) != 0) {
			wmessage("Could not create symlink %s because: %s\n",
				 devnmbuf, strerror(errno));
		    }
		}
		if (ctrlr[i]->dsk[j]->part[k][LN_D_RAW].state == LN_MISSING) {
		    /* Make full name */
		    if (k < MAXSLICE) {
		    	sprintf(devnmbuf, "%s/c%d%ss%d", disk_rdir, i,
			    ctrlr[i]->dsk[j]->devdsknm, k);
		    } else {
		    	sprintf(devnmbuf, "%s/c%d%sp%d", disk_rdir, i,
			    ctrlr[i]->dsk[j]->devdsknm, (k-MAXSLICE));
		    }
		    sprintf(devfsnmbuf, "%s@%s:%s", ctrlr[i]->devfsctrlrnm,
			ctrlr[i]->dsk[j]->devfsdsknm,
			ctrlr[i]->dsk[j]->part[k][LN_D_RAW].devfspart);

		    if (symlink(devfsnmbuf, devnmbuf) != 0) {
			wmessage("Could not create symlink %s because: %s\n",
				 devnmbuf, strerror(errno));
		    }
		}
	    }
	}
    }
}

/*
 * Next routine finds disks that have not got /dev/[r]SA links and creates them.
 * Why do we need this now sysadm has gone away?
 */
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
	    fmessage(1, "Usage: disks [-r root_directory]\n");
	}

    if (optind < argc)
	fmessage(1, "Usage: disks [-r root_directory]\n");

    /*
     * Set address of disk bdir and disk raw dir
     */
    disk_bdir = s_malloc(strlen(rootdir) + sizeof("/dev/dsk"));
    sprintf((char *)disk_bdir, "%s%s", rootdir, "/dev/dsk");
				/* Explicitly override const attribute */

    disk_rdir = s_malloc(strlen(rootdir) + sizeof("/dev/rdsk"));
    sprintf((char *)disk_rdir, "%s%s", rootdir, "/dev/rdsk");

    /*
     * Start building list of disk devices by looking through /dev/[r]dsk.
     */
    get_dev_entries();

    /*
     * Now add to this real device configuration from /devices
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
     * FInally add admin database info -- really just the /dev/SA and /dev/rSA
     * stuff.
     */
    update_admin_db();

    return(0);
}
