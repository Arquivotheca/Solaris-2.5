/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)audlinks.c	1.8	93/10/07 SMI"

/*
 * Audio/ISDN minor nodes in the devfs file system are assumed to
 * have the following format:
 *
 *	DEV:chan,name
 *
 * Examples:
 *	SUNW,DBRIe@2,10000:te,d
 *	SUNW,DBRIe@2,10000:te,b1
 *	SUNW,DBRIe@2,10000:nt,d
 *	SUNW,DBRIe@2,10000:nt,b1
 *	SUNW,DBRIe@2,10000:sound,audio
 *	SUNW,DBRIe@2,10000:sound,audioctl
 */

#include <sys/types.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <locale.h>
#include <sys/sunddi.h>
#include <libdevinfo.h>

#include "utilhdr.h"


/*
 * Constants
 */

#define	ISDNDIR		"/dev/isdn"
#define	SOUNDDIR	"/dev/sound"

#define	AUDIODEV	"/dev/audio" /* default audio device */

#define	MAXCTLRS	(16)
#define	MAXMINOR	(64)

typedef enum controller_type controller_type_t;
enum controller_type {
	CONTROLLER_EMPTY = 0,
	CONTROLLER_NONE,
	CONTROLLER_AUDIO,
	CONTROLLER_ISDN
};


typedef enum dev_state dev_state_t;
enum dev_state {
	DEV_NONE = 0,		/* Nothing yet */
	DEV_NODEV,		/* devfs but not dev */
	DEV_NODEVFS,		/* dev but not devfs */
	DEV_EXISTS		/* both and they match */
};


/*
 * Type definitions
 */

typedef struct minornode minornode_t;
struct minornode {
	char *devfsname;	/* name of devfs entry */
	char *devname;		/* name of /dev entry */
	char *link;		/* contents of /dev symlink */
	dev_state_t state;	/* node state */

	char *interface;	/* sound, nt, te, etc */
	char *channel;		/* b1, b2, etc */
};

typedef struct controller controller_t;
struct controller {
	char *unitname;		/* controller name */
	int unit;		/* logical unit number */
	controller_type_t type;
	minornode_t minornode[MAXMINOR];
};


/*
 * Global Variables
 */

char *root_dir = "";		/* base directory */
char *isdn_dir;			/* /dev isdn directory path */
char *sound_dir;		/* /dev sound direcotry path */
char *isdnmiscdevfsrpath;	/* Relative path from isdn/0/nt/ to devfs */
char *isdnmgtdevfsrpath;	/* Relative path from isdn/0/ to devfs */
char *sounddevfsrpath;		/* Relative path from sound/unit/ to devfs */
static int debug = 0;		/* debugging flag */

static controller_t isdn_controllers[MAXCTLRS];
static controller_t audio_controllers[MAXCTLRS];
static controller_t *isdn_units[MAXCTLRS];
static controller_t *audio_units[MAXCTLRS];


/*
 * extract_isdnunit - get the generic isdn unit number out of the dev name.
 * returns -1 on failure, positive integer on success.
 *
 * isdn/[0]/...
 */
static int
extract_isdnunit(const char *devname)
{
	int isdnunit;
	size_t i;

	for (i = 0; i < strlen(devname); i++) {
		if (strncmp(devname + i, "/dev/", 5) == 0) {
			devname += i + 5;
		}
	}

	if (strncmp(devname, "isdn/", 5) != 0)
		return (-1);

	isdnunit = atoi(&devname[5]); /* XXX */

	return (isdnunit);
}


/*
 * extract_audiounit - get the generic isdn unit number out of the dev name.
 * returns -1 on failure, positive integer on success.
 *
 * sound/[0]...
 */
static int
extract_audiounit(const char *devname)
{
	int audiounit;
	size_t i;

	for (i = 0; i < strlen(devname); i++) {
		if (strncmp(devname + i, "/dev/", 5) == 0) {
			devname += i + 5;
		}
	}

	if (strncmp(devname, "sound/", 6) != 0)
		return (-1);

	audiounit = atoi(&devname[6]); /* XXX */

	return (audiounit);
}


/*
 * extract_unit - get the isdn or sound unit number from the dev name.
 */
static int
extract_unit(const char *devname, controller_type_t ct)
{
	return ((ct == CONTROLLER_AUDIO) ?
	    extract_audiounit(devname) :
	    extract_isdnunit(devname));
}


/*
 * extract_minor_name - get the minor part of the devfs name
 *
 * devices/sbus@1,f800000/SUNW,DBRIe: [dbri0,te,b1]
 */
static char *
extract_minor_name(const char *devfsname)
{
	static char tmpbuf[PATH_MAX];
	const char *sp;

	if ((sp = strrchr(devfsname, '/')) == NULL)
		sp = devfsname;

	if ((sp = strchr(sp, ':')) == NULL)
		return (NULL);

	(void) strcpy(tmpbuf, sp + 1);

	return (tmpbuf);
}


/*
 * normalize_devfs_name - strip off leading junk from a devfs name
 * (from devfs or symlink) up to and including "devices/".  Returns
 * result stored in STATIC buffer.
 */
static char *
normalize_devfs_name(const char *name)
{
	static char buf[PATH_MAX];
	char *c;

	c = (char *)name;
	if (c[0] == '.' && c[1] == '.' && c[2] == '/') {
		while (c[0] == '.' && c[1] == '.' && c[2] == '/')
			c += 3;
		if (strncmp(c, "devices/", 8) == 0)
			c += 8;
	}
	(void) strcpy(buf, c);

	return (buf);
}


/*
 * extract_unit_name - get the unit name from the devfs name; this
 * also normalizes the name (removes "../../../devices/")
 */
static char *
extract_unit_name(const char *name)
{
	static char tmpbuf[PATH_MAX];
	char *sp;

	sp = normalize_devfs_name(name);
	(void) strcpy(tmpbuf, sp);

	if ((sp = strrchr(tmpbuf, ':')) == NULL)
		return (NULL);

	*sp = '\0';

	return (tmpbuf);
}


/*
 * extract_interface_name - get the interface name from the minor name
 *
 * [te] ,b1
 */
static char *
extract_interface_name(const char *name)
{
	static char tmpbuf[PATH_MAX];
	char *sp;

	(void) strcpy(tmpbuf, name);

	if ((sp = strchr(tmpbuf, ',')) != NULL)
		*sp = '\0';

	return (tmpbuf);
}


/*
 * extract_channel_name - get the channel name from the minor name
 *
 * te, [b1]
 */
static char *
extract_channel_name(const char *name)
{
	static char tmpbuf[PATH_MAX];
	char *sp;

	tmpbuf[0] = '\0';	/* if strchr fails return null string */
	if ((sp = strchr(name, ',')) == NULL)
		return tmpbuf;	/* return null string */

	(void) strcpy(tmpbuf, sp+1);

	return (tmpbuf);
}


/*
 * is_audio_unit - given a devfs-style name, return non-zero if
 * the name indicates an audio unit
 */
static int
is_audio_unit(const char *name)
{
	if (strcmp(extract_interface_name(extract_minor_name(name)),
	    "sound") == 0)
		return (1);
	return (0);
}


/*
 * find_next_physical_unit - return the next free unit in either the
 * audio or isdn controller arrays.
 */
static int
find_next_physical_unit(controller_type_t ct)
{
	int i;
	controller_t *ctlrs;

	ctlrs = (ct == CONTROLLER_AUDIO) ? audio_controllers :
	    isdn_controllers;

	for (i = 0; i < MAXCTLRS; i++) {
		if (ctlrs[i].type == CONTROLLER_EMPTY) {
			ctlrs[i].type = CONTROLLER_NONE;
			return (i);
		}
	}

	return (-1);
}


/*
 * find_next_logical_unit - get the next logical unit number for either
 * an isdn or audio device (audio_units or isdn_units arrays).
 */
static int
find_next_logical_unit(controller_type_t ct)
{
	int i;
	controller_t **ctlrs;

	ctlrs = (ct == CONTROLLER_AUDIO) ? audio_units :
	    isdn_units;

	for (i = 0; i < MAXCTLRS; i++) {
		if (ctlrs[i] == NULL)
			return (i);
	}

	return (-1);
}


/*
 * find_physical_unit_from_devfsnm - given a devfs name and a controller type,
 * try to find a matching controller.
 */
static int
find_physical_unit_from_devfsnm(controller_type_t ct, const char *devfsnm)
{
	controller_t *ctlrs;
	char *unitname;
	int i;

	ctlrs = (ct == CONTROLLER_AUDIO) ? audio_controllers :
	    isdn_controllers;

	unitname = extract_unit_name(devfsnm);

	for (i = 0; i < MAXCTLRS; i++) {
		if (ctlrs[i].unitname == NULL)
			continue;
		if (strcmp(ctlrs[i].unitname, unitname) == 0)
			return (i);
	}

	return (-1);
}


/*
 * format_dev_name - return in a static buffer the /dev name given
 * the components
 */
static const char *
format_dev_name(controller_t *ctlr, int minornum)
{
	static char buf[PATH_MAX + 1];
	minornode_t *minornode;

	minornode = &ctlr->minornode[minornum];

	if (strcmp(minornode->interface, "sound") == 0) {
		if ((strncmp(minornode->channel, "audioctl", 8) == 0)
		||  (strncmp(minornode->channel, "sbproctl", 8) == 0))
			(void) sprintf(buf, "sound/%dctl", ctlr->unit);
		else if ((strncmp(minornode->channel, "audio", 5) == 0)
		     ||  (strncmp(minornode->channel, "sbpro", 5) == 0))
			(void) sprintf(buf, "sound/%d", ctlr->unit);
		else
			buf[0] = '\0';

	} else if (strcmp(minornode->interface, "mgt") == 0) {
		(void) sprintf(buf, "isdn/%d/%s", ctlr->unit,
		    minornode->channel);
	} else {
		(void) sprintf(buf, "isdn/%d/%s/%s", ctlr->unit,
		    minornode->interface, minornode->channel);
	}

	return (buf);
}


/*
 * add_node - name always contains the name of the physical file (in
 * devfs or in /dev).  If it is a /dev entry, then link contains the
 * contents of the symlink (which looks like a devfs name).
 */
static void
add_node(const char *name, const char *link)
{
	controller_t *ctlr;
	controller_type_t ct;
	int unit;
	char *dname;	/* whichever of name or link is the devfsname */
	char *interface;
	char *channel;
	char *minorname;
	int i;

	if (debug > 1) {
		if (link != NULL) {
			fprintf(stderr,
			    "audlinks: add_node(%s, %s)\n", name, link);
		} else {
			fprintf(stderr, "add_node(%s, NULL)\n", name);
		}
	}


	dname = (char *)((link != NULL) ? link : name);
	ct = is_audio_unit(dname) ? CONTROLLER_AUDIO : CONTROLLER_ISDN;

	unit = find_physical_unit_from_devfsnm(ct, dname);
	minorname = extract_minor_name(dname);
	interface = strdup(extract_interface_name(minorname));
	channel = strdup(extract_channel_name(minorname));

	/*
	 * If a unit doesn't exist for this controller, then find a
	 * free one and initialize it.
	 */
	if (unit < 0) {
		unit = find_next_physical_unit(ct);
		if (unit < 0) {
			fprintf(stderr, "audlinks: Could not allocate unit\n");
			return;	/* XXX */
		}

		/*
		 * Initialize the new unit structure
		 */
		ctlr = (ct == CONTROLLER_AUDIO) ? &audio_controllers[unit] :
		    &isdn_controllers[unit];

		ctlr->unitname = strdup(extract_unit_name(dname));
		ctlr->type = ct;
	} else {
		ctlr = (ct == CONTROLLER_AUDIO) ? &audio_controllers[unit] :
		    &isdn_controllers[unit];
	}

	/*
	 * Loop through minor nodes until a match is found, a free slot is
	 * found, or we hit the end.
	 */
	for (i = 0; i < MAXMINOR; i++) {
		if (ctlr->minornode[i].state == DEV_NONE)
			break;

		if (strcmp(ctlr->minornode[i].interface, interface) != 0)
			continue;

		if (strcmp(ctlr->minornode[i].channel, channel) != 0)
			continue;

		break;
	}
	if (i >= MAXMINOR) {
		fprintf(stderr,
		    "audlinks: Too many minor devices for unit %s\n",
		    ctlr->unitname);
		goto done;
	}

	/*
	 * If we don't know our logical unit number and we found
	 * a /dev entry, we can derive it.
	 */
	if (ctlr->unit == -1 && link != NULL) {
		if (ct == CONTROLLER_AUDIO)
			ctlr->unit = extract_audiounit(name);
		else
			ctlr->unit = extract_isdnunit(name);
	}

	/*
	 * Fill in the blanks in the minor node structure.
	 */
	switch (ctlr->minornode[i].state) {
	case DEV_NONE:		/* new entry */
		ctlr->minornode[i].channel = strdup(channel);
		ctlr->minornode[i].interface = strdup(interface);

		if (link != NULL) {
			ctlr->minornode[i].devname = strdup(name);
			ctlr->minornode[i].link = strdup(link);
			ctlr->minornode[i].state = DEV_NODEVFS;
		} else {
			ctlr->minornode[i].devfsname = strdup(name);
			ctlr->minornode[i].state = DEV_NODEV;
		}
		break;

	case DEV_NODEVFS:
		if (link != NULL) {
			/*
			 * XXX - Found two /dev entries pointing to the
			 * devfs entry
			 */
			goto done;
		} else {
			ctlr->minornode[i].devfsname = strdup(name);
			ctlr->minornode[i].state = DEV_EXISTS;
		}
		break;

	case DEV_NODEV:
		if (link != NULL) {
			ctlr->minornode[i].devname = strdup(name);
			ctlr->minornode[i].link = strdup(link);
			ctlr->minornode[i].state = DEV_EXISTS;
		} else {
			/*
			 * XXX - Found two /devfs entries with identical
			 * names! :-)
			 */
			goto done;
		}
	} /* switch on minor node current state */

done:
	free(channel);
	free(interface);

	return;
}


/*
 * walk_dev_dir - walk a directory looking for symlinks.
 */
static void
walk_dev_dir(const char *dname)
{
	DIR *dp;
	int linksize;
	struct dirent *entp;
	struct stat sb;
	char namebuf[PATH_MAX+1];
	char linkbuf[PATH_MAX+1];

	/*
	 * Search a directory for special names
	 */
	dp = opendir(dname);

	if (dp == NULL)
		return;

	while ((entp = readdir(dp)) != NULL) {
		linkbuf[0] = '\0';

		if (strcmp(entp->d_name, ".") == 0 ||
		    strcmp(entp->d_name, "..") == 0)
			continue;

		(void) sprintf(namebuf, "%s/%s", dname, entp->d_name);

		if (lstat(namebuf, &sb) < 0) {
			fprintf(stderr, "audlinks: Cannot stat %s\n", namebuf);
			continue;
		}

		switch (sb.st_mode & S_IFMT) {
		case S_IFLNK:
			linksize = readlink(namebuf, linkbuf, PATH_MAX);

			if (linksize <= 0) {
				fprintf(stderr,
"audlinks: Could not read symbolic link %s\n", namebuf);
				break;
			}

			linkbuf[linksize] = '\0';
			break;

		case S_IFDIR:
			walk_dev_dir(namebuf);
			break;

		default:
			fprintf(stderr,
			    "audlinks: %s is not a symbolic link\n", namebuf);
			break;
		} /* switch on directory entry type */

		if (linkbuf[0] == '\0')
			continue;

		if (debug > 1) {
			fprintf(stderr,
			    "audlinks: walk_dev_dir got %s\n", namebuf);
			fprintf(stderr, "  --> %s\n", linkbuf);
		}

		add_node(namebuf, linkbuf);

	} /* while another directory entry */

	(void) closedir(dp);

	return;
}


/*
 * devfs_entry -- routine called when an 'AUDIO' devfs entry is found
 *
 * This routine is called by devfs_find() when a matching devfs entry is found.
 * It is passed the name of the devfs entry.
 */
static void
devfs_entry(const char *devfsnm,
    const char *devfstype, const dev_info_t *dip,
    struct ddi_minor_data *dmip, struct ddi_minor_data *dmap)
{
	/*
	 * Look for audio (isdn) devices only
	 */
	if (strcmp(devfstype, DDI_NT_AUDIO) != 0)
		return;

	if (debug > 2)
		fprintf(stderr, "audlinks: devfs_entry: got %s\n", devfsnm);

	add_node(devfsnm, NULL);

	return;
}


/*
 * basename - return just the last component of a pathname in a STATIC
 * buffer.
 */
static const char *
basename(const char *path)
{
	static char buf[PATH_MAX];
	char *c;

	(void) strcpy(buf, path);
	c = strrchr(buf, '/');
	if (c == NULL)
		return (buf);

	*c = '\0';

	return (buf);
}


/*
 * process_nodes - Go through the controller list and assign isdn
 * and audio entries to point to generic audio/isdn controllers.
 * Nodes are assumed to have been deleted by now if they should
 * no longer exist.
 */
static void
process_nodes(controller_type_t ct)
{
	int i, j, found, remove;
	controller_t *ctlr;
	controller_t **logicalunits;
	controller_t *physicalunits;

	logicalunits = (ct == CONTROLLER_AUDIO) ? audio_units :
	    isdn_units;
	physicalunits = (ct == CONTROLLER_AUDIO) ? audio_controllers :
	    isdn_controllers;


	/*
	 * Loop through the physical unit array and assign logical
	 * unit numbers to those units which have none.
	 */
	for (i = 0; i < MAXCTLRS; i++) {
		ctlr = &physicalunits[i];

		if (ctlr->type == CONTROLLER_EMPTY)
			continue;

		/*
		 * Units that have no devfs entries have the dev entries
		 * deleted and do not need logical numbers.
		 */
		found = remove = 0;
		for (j = 0; j < MAXMINOR; j++) {
			if (ctlr->minornode[j].state == DEV_NONE)
				continue;
			found++;
			if (ctlr->minornode[j].state == DEV_NODEVFS)
				remove++;
		}
		if (found == remove) {
			if (ctlr->unit < 0) {
				for (j = 0; j < MAXMINOR; j++) {
					if (ctlr->minornode[j].state ==
					    DEV_NODEVFS) {
						ctlr->unit =
				extract_unit(ctlr->minornode[j].devname, ct);
					}
					if (ctlr->unit >= 0)
						break;
				}
			}
			if (debug && ctlr->unit < 0) {
				fprintf(stderr,
				    "audlinks: bogus /dev/entries\n");
			}
			continue;
		}

		/*
		 * If this unit has not had a logical unit number
		 * assigned, do it now.
		 */
		if (ctlr->unit < 0 || ctlr->unit >= MAXCTLRS) {
			ctlr->unit = find_next_logical_unit(ct);
				if (ctlr->unit == -1)
					continue;
		}

		if (logicalunits[ctlr->unit] != NULL) {
			fprintf(stderr,
			    "audlinks: Conflicting logical units\n");
			continue;
		}

		logicalunits[ctlr->unit] = ctlr;
	}

	return;
}


/*
 */
static void
add_links(controller_type_t ct)
{
	char deventry[PATH_MAX], devfsentry[PATH_MAX];
	controller_t *ctlr;
	char *rpath;
	int i;
	int j;

	for (i = 0; i < MAXCTLRS; i++) {
		ctlr = (ct == CONTROLLER_AUDIO) ? audio_units[i] :
		    isdn_units[i];

		if (ctlr == NULL)
			continue;

		if (ctlr->type == CONTROLLER_EMPTY ||
		    ctlr->type == CONTROLLER_NONE)
			continue;

		for (j = 0; j < MAXMINOR; j++) {

			if (ctlr->minornode[j].state == DEV_NONE)
				continue;

			/*
			 * Choose a relative path to the /devices directory
			 * based on controller type and interface.
			 */
			if (ct == CONTROLLER_AUDIO) {
				rpath = sounddevfsrpath;
			} else {
				if (strcmp(ctlr->minornode[j].interface,
				    "mgt") == 0)
					rpath = isdnmgtdevfsrpath;
				else
					rpath = isdnmiscdevfsrpath;
			}

			switch (ctlr->minornode[j].state) {
			case DEV_NODEV:
				(void) sprintf(deventry, "/dev/%s",
				    format_dev_name(ctlr, j));
				(void) sprintf(devfsentry, "%s/%s", rpath,
				    ctlr->minornode[j].devfsname);

				/*
				 * Create directories in this path that
				 * do not exist
				 */
				create_dirs(basename(deventry));

				if (symlink(devfsentry, deventry) != 0) {
					fprintf(stderr,
"audlinks: Error adding %s\n", deventry);
					perror("symlink");
				}
				break;

			case DEV_EXISTS:
				(void) sprintf(deventry, "/dev/%s",
				    format_dev_name(ctlr, j));
				(void) sprintf(devfsentry, "%s/%s", rpath,
				    ctlr->minornode[j].devfsname);
				break;

			default:
				break;
			} /* switch on device state */
		} /* for... each minor node */
	} /* for... each unit */

	return;
}


/*
 */
static void
remove_links(controller_type_t ct)
{
	char deventry[PATH_MAX];
	controller_t *ctlr;
	int i;
	int j;

	for (i = 0; i < MAXCTLRS; i++) {
		ctlr = (ct == CONTROLLER_AUDIO) ? &audio_controllers[i] :
		    &isdn_controllers[i];

		if (ctlr == NULL)
			continue;

		if (ctlr->type == CONTROLLER_EMPTY)
			continue;

		for (j = 0; j < MAXMINOR; j++) {
			switch (ctlr->minornode[j].state) {
			default:
				continue;

			case DEV_NODEVFS:
				(void) sprintf(deventry, "/dev/%s",
				    format_dev_name(ctlr, j));

				if (unlink(deventry) != 0) {
					fprintf(stderr,
"audlinks Error removing %s\n", deventry);
					perror("unlink");
				}

				/*
				 * Strip off the filename and try to remove
				 * the containing directory.  It'll only
				 * remove empty directories.  We know that
				 * this strrchr can never return NULL.
				 */
				*strrchr(deventry, '/') = '\0';
				(void) rmdir(deventry);
				break;
			}
		} /* for... each minor node in this unit */
	} /* for... each unit */

	return;
}


/*
 */
static void
free_memory()
{
	int i, j;
	controller_t *ctlr;

	for (i = 0; i < MAXCTLRS; i++) {
		ctlr = &audio_controllers[i];

		if (ctlr->unitname != NULL)
			free(ctlr->unitname);

		for (j = 0; j < MAXMINOR; j++) {
			if (ctlr->minornode[j].devfsname != NULL)
				free(ctlr->minornode[j].devfsname);
			if (ctlr->minornode[j].devname != NULL)
				free(ctlr->minornode[j].devname);
			if (ctlr->minornode[j].link != NULL)
				free(ctlr->minornode[j].link);
			if (ctlr->minornode[j].channel != NULL)
				free(ctlr->minornode[j].channel);
			if (ctlr->minornode[j].interface != NULL)
				free(ctlr->minornode[j].interface);
		}
	}

	return;
}


/*
 * If /dev/audio does not already exist, then create symlinks from
 * /dev/audio to /dev/sound/0 to create a "default" audio device.
 */
static void
default_audio_links()
{
	struct stat sb;
	char name[PATH_MAX+1];
	char namectl[PATH_MAX+1];
	boolean_t need_symlink;

	need_symlink = B_FALSE;
	(void) sprintf(name, "%s%s", root_dir, AUDIODEV);
	(void) sprintf(namectl, "%s%sctl", root_dir, AUDIODEV);

	if (lstat(name, &sb) < 0 && errno == ENOENT) {
		need_symlink = B_TRUE;
	} else if (stat(name, &sb) < 0 && errno == ENOENT) {
		need_symlink = B_TRUE;
	}
	if (lstat(namectl, &sb) < 0 && errno == ENOENT) {
		need_symlink = B_TRUE;
	} else if (stat(namectl, &sb) < 0 && errno == ENOENT) {
		need_symlink = B_TRUE;
	}
	if (need_symlink) {
		int i;
		controller_t *ctlr;
		char lname[PATH_MAX+1];
		char lnamectl[PATH_MAX+1];

		if (unlink(name) < 0 && errno != ENOENT) {
			perror("unlink(/dev/audio)");
		}

		if (unlink(namectl) < 0 && errno != ENOENT) {
			perror("unlink(/dev/audioctl)");
		}

		for (i = 0; i < MAXCTLRS; i++) {
			ctlr = audio_units[i];

			if (ctlr == NULL)
				continue;

			if (ctlr->type == CONTROLLER_AUDIO)
				break;
		}
		if (i >= MAXCTLRS) {
			if (debug) {
				fprintf(stderr, "audlinks: "
					"No audio device to link to!\n");
			}
			goto nodev;
		}

		if (debug)
			fprintf(stderr, "audlinks: installing /dev/audio\n");
		sprintf(lname, "/dev/sound/%d", i);
		sprintf(lnamectl, "/dev/sound/%dctl", i);
		if (symlink(lname, name) < 0)
			perror("symlink");
		if (symlink(lnamectl, namectl) < 0)
			perror("symlink");

		nodev:;
	} else {
		if (debug) {
			fprintf(stderr,
			    "audlinks: /dev/audio exists, not creating it\n");
		}
	}

	return;
}


/*
 */
int
main(int argc, char *argv[])
{
	extern int optind;
	int c;
	int i;
	int j;

	(void) setlocale(LC_ALL, "");

	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "dr:")) != EOF) {
		switch (c) {
		case 'd':
			debug++;
			break;

		case 'r':
			root_dir = optarg;
			break;

		default:
		case '?':
			fprintf(stderr,
			    "Usage: audlinks [-d] [-r root_directory]\n");
			exit (-1);
		}
	}

	if (optind < argc) {
		fmessage(1, "Usage: audlinks [-d] [-r root_directory]\n");
		exit (-1);
	}

	/*
	 * Initialize data structures
	 */
	isdnmiscdevfsrpath = "../../../../devices";
	isdnmgtdevfsrpath = "../../../devices";
	sounddevfsrpath = "../../devices";

	for (i = 0; i < MAXCTLRS; i++) {
		audio_controllers[i].type = CONTROLLER_EMPTY;
		isdn_controllers[i].type = CONTROLLER_EMPTY;
		audio_controllers[i].unitname = NULL;
		isdn_controllers[i].unitname = NULL;
		audio_controllers[i].unit = -1;
		isdn_controllers[i].unit = -1;
		audio_units[i] = NULL;
		isdn_units[i] = NULL;
		for (j = 0; j < MAXMINOR; j++) {
			audio_controllers[i].minornode[j].state = DEV_NONE;
			isdn_controllers[i].minornode[j].state = DEV_NONE;
		}
	}

	/*
	 * Get directory names...
	 */
	isdn_dir = (char *)s_malloc(strlen(root_dir) + sizeof (ISDNDIR));
	(void) sprintf((char *)isdn_dir, "%s%s", root_dir, ISDNDIR);

	sound_dir = (char *)s_malloc(strlen(root_dir) + sizeof (SOUNDDIR));
	(void) sprintf((char *)sound_dir, "%s%s", root_dir, SOUNDDIR);

	if (debug) {
		fprintf(stderr, "audlinks: isdn_dir=%s\n", isdn_dir);
		fprintf(stderr, "audlinks: sound_dir=%s\n", sound_dir);
	}

	/*
	 * Make our base directories if they do not exist
	 */
	create_dirs(isdn_dir);
	create_dirs(sound_dir);

	/*
	 * Start building list of audio/isdn devices by looking through
	 * /dev/sound and /dev/isdn
	 */
	walk_dev_dir(isdn_dir);
	walk_dev_dir(sound_dir);

	/*
	 * Now add to this real device configuration from /devices
	 */
	devfs_find(DDI_NT_AUDIO ":", devfs_entry, 0);

	/*
	 * Assign ISDN and SOUND unit numbers from the generic array
	 */
	process_nodes(CONTROLLER_AUDIO);
	process_nodes(CONTROLLER_ISDN);

	/*
	 * Remove old entries in /dev that point to non-existent
	 * /devfs entries
	 */
	remove_links(CONTROLLER_AUDIO);
	remove_links(CONTROLLER_ISDN);

	/*
	 * Create /dev entries for /devfs entries that are not pointed
	 * to
	 */
	add_links(CONTROLLER_AUDIO);
	add_links(CONTROLLER_ISDN);

	/*
	 * Make /dev/audio and /dev/audioctl symlinks to /dev/sound/0
	 * if they do not exist (for backwards compatability with existing
	 * audio applications).
	 */
	default_audio_links();

	/*
	 * Free everying in the controller and minor structures that
	 * were copied with strdup().
	 */
	free_memory();

	return (0);
}
