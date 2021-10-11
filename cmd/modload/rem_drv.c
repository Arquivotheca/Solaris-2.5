#pragma ident	"@(#)rem_drv.c 1.6	95/01/12 SMI"

/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <libintl.h>
#include <string.h>
#include <fcntl.h>
#include <sys/buf.h>
#include <sys/stat.h>
#include <sys/modctl.h>
#include <sys/wait.h>
#include <limits.h>
#include <malloc.h>
#include <locale.h>
#include "addrem.h"
#include "errmsg.h"

static int get_major_number(char *, major_t *);
static void get_mod(char *, int *);
static int devfs_clean(char *);
static int devfs_alias_clean(char *);
static void *get_aliases(char *, char *);
static void usage();

main(int argc, char *argv[])
{
	int opt;
	char *basedir = NULL;
	int server = 0;
	char *driver_name = NULL;
	major_t major;
	FILE *fp;
	FILE *reconfig_fp;
	struct stat buf;
	int modid;
	char maj_num[MAX_STR_MAJOR + 1];
	char dummy[FILENAME_MAX + 1];
	char ret_str[MAXLEN_NAM_TO_MAJ_ENT + 1];
	int found;

	(void) setlocale(LC_ALL, "");
#if	!defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	/*  must be run by root */

	if (getuid() != 0) {
		(void) fprintf(stderr, gettext(ERR_NOT_ROOT));
		exit(1);
	}

	while ((opt = getopt(argc, argv, "b:")) != -1) {
		switch (opt) {
		case 'b' :
			server = 1;
			basedir = calloc(strlen(optarg) + 1, 1);
			if (basedir == NULL) {
				(void) fprintf(stderr, gettext(ERR_NO_MEM));
				exit(1);
			}
			(void) strcat(basedir, optarg);
			break;
		case '?' :
			usage();
			exit(1);
		}
	}

	if (argv[optind] != NULL) {
		driver_name = calloc(strlen(argv[optind]) + 1, 1);
		if (driver_name == NULL) {
			(void) fprintf(stderr, gettext(ERR_NO_MEM));
			exit(1);

		}
		(void) strcat(driver_name, argv[optind]);
		/*
		 * check for extra args
		 */
		if ((optind + 1) != argc) {
			usage();
			exit(1);
		}

	} else {
		usage();
		exit(1);
	}

	/* set up add_drv filenames */
	if ((build_filenames(basedir)) == ERROR) {
		exit(1);
	}

	/* must be only running version of add_drv/rem_drv */

	if ((stat(add_rem_lock, &buf) == -1) && errno == ENOENT) {
		fp = fopen(add_rem_lock, "a");
		(void) fclose(fp);
	} else {
		(void) fprintf(stderr,
		gettext(ERR_PROG_IN_USE));
		exit(1);
	}

	if ((some_checking(1, 1)) == ERROR)
		err_exit();

	if (!server) {
		if ((get_major_number(driver_name, &major)) == ERROR) {
			err_exit();
		}

		/* get the module id for this driver */

		get_mod(driver_name, &modid);

		/* module is installed */
		if (modid != -1) {
			if (modctl(MODUNLOAD, modid) < 0) {
				perror(NULL);
				(void) fprintf(stderr, gettext(ERR_MODUN),
				driver_name);
			}
		}
	}

	if (devfs_alias_clean(driver_name) == ERROR) {
		(void) fprintf(stderr,
		    gettext(ERR_DEVFSALCLEAN),
			driver_name);
	}


	if (devfs_clean(driver_name) == ERROR) {
		(void) fprintf(stderr,
		    gettext(ERR_DEVFSCLEAN), driver_name);
	}



	/*
	 * add driver to rem_name_to_major; if this fails, don`t
	 * delete from name_to_major
	 */
	if (get_file_entry(driver_name, name_to_major, ret_str,
	    &found) == ERROR) {
		(void) fprintf(stderr, gettext(ERR_NO_UPDATE),
		    rem_name_to_major);
		err_exit();
	}

	/* no entry in REM_NAM_TO_MAJ */
	if (found == UNIQUE) {
		(void) fprintf(stderr, gettext(ERR_NOT_INSTALLED),
		    driver_name);
		err_exit();
	}

	if (sscanf(ret_str, "%s%s", dummy, maj_num) != 2) {
		(void) fprintf(stderr, gettext(ERR_NO_UPDATE),
		    rem_name_to_major);
		err_exit();
	}

	if (append_to_file(driver_name, maj_num,
	    rem_name_to_major, ' ', " ") == ERROR) {
		(void) fprintf(stderr, gettext(ERR_NO_UPDATE),
		    rem_name_to_major);
		err_exit();
	}


	/*
	 * delete references to driver in add_drv/rem_drv database
	 */

	remove_entry(CLEAN_ALL, driver_name);

	exit_unlock();

	/*
	 * Create reconfigure file; want the driver removed
	 * from kernel data structures upon reboot
	 */
	reconfig_fp = fopen(RECONFIGURE, "a");
	(void) fclose(reconfig_fp);

	return (NOERR);
}

/* get major number from kernel, not name_to_major */
int
get_major_number(
	char *driver_name,
	major_t *major_number)
{
	major_t major;

	if (modctl(MODGETMAJBIND, driver_name, strlen(driver_name) + 1,
	    &major) < 0) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_NO_MAJ), driver_name);
		return (ERROR);
	} else {
		*major_number = major;
		return (NOERR);
	}

}

void
get_mod(char *driver_name, int *mod)
{
	struct modinfo	modinfo;

	modinfo.mi_id = -1;
	modinfo.mi_info = MI_INFO_ALL;
	do {
		/*
		 * If we are at the end of the list of loaded modules
		 * then set *mod = -1 and return
		 */
		if (modctl(MODINFO, 0, &modinfo) < 0) {
			*mod = -1;
			return;
		}

		*mod = modinfo.mi_id;
	} while (strcmp(driver_name, modinfo.mi_name) != 0);
}

int
devfs_clean(char *driver_name)
{
	char devname[PATH_MAX + FILENAME_MAX + 1];
	char dev_colon[FILENAME_MAX + 1];
	char dev_at[FILENAME_MAX + 1];
	char dev_clone[FILENAME_MAX + 1];
	int fd[2];
	int fdhold[2];
	int child;
	int waitstat;

	if (pipe(fd)) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_PIPE));
		return (ERROR);
	}

	if (pipe(fdhold)) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_PIPE));
		return (ERROR);
	}

	/* save values of stdin/stout for next call */
	(void) dup2(1, fdhold[1]);
	(void) dup2(0, fdhold[0]);

	if ((child = fork()) == 0) {


		(void) close(1);
		(void) dup(fd[1]);
		(void) close(fd[1]);
		(void) close(fd[0]);

		(void) strcpy(dev_colon, driver_name);
		(void) strcat(dev_colon, ":*");

		(void) strcpy(dev_at, driver_name);
		(void) strcat(dev_at, "@*");

		(void) strcpy(dev_clone, "clone:");
		(void) strcat(dev_clone, driver_name);

		(void) execl("/usr/bin/find", "find", devfs_root, "(", "-name",
		    dev_colon, "-o", "-name", dev_at, "-o", "-name",
		    dev_clone, ")", "-print", 0);

		/* i really shouldn`t ever be here .... */
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_EXEC));
	}

	/*
	 * Close writer end of pipe so catch EOF
	 */

	(void) close(0);
	(void) dup(fd[0]);
	(void) close(fd[0]);
	(void) close(fd[1]);
	(void) close(1);

	devname[0] = '\0';

	while ((gets(devname)) != NULL) {
		if (unlink(devname)) {
			perror(NULL);
			(void) fprintf(stderr, gettext(ERR_UNLINK), devname);
		}
	}

	(void) waitpid(child, &waitstat, 0);

	/*
	 * reset stdin/stdout
	 */
	(void) dup2(fdhold[1], 1);
	(void) dup2(fdhold[0], 0);

	return (NOERR);

}

int
devfs_alias_clean(char *driver)
{
	char *alias_list = NULL;
	char *prev;
	char *current;
	char one_alias[FILENAME_MAX];
	int status = NOERR;

	alias_list = get_aliases(driver, DRIVER_ALIAS);
	if (alias_list == NULL) {
		return (NOERR);
	}

	prev = alias_list;

	do {
		current = get_entry(prev, one_alias, ' ');
		if (devfs_clean(one_alias) == ERROR) {
			status = ERROR;
		}
		prev = current;

	} while (*current != '\0');

	return (status);
}

/*
 * Search thru <file_name> for <driver_name>
 * if found, add next field to matches list
 * return matches list
 * Assumption:  file format: <driver_name> <string>
 * Assumption:  may be 0-n entries for any given driver
 */
void *
get_aliases(
	char *driver_name,
	char *file_name)
{
	FILE *fp;
	char drv[MAX_STR_MAJOR + 1];
	char entry[FILENAME_MAX + 1];
	char line[MAX_N2M_ALIAS_LINE + 1];
	int first_match = 1;
	char *matches = NULL;
	char *tmp_match;
	int len;

	fp = fopen(file_name, "r");
	if (fp == NULL) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_CANNOT_OPEN),
		    file_name);
		return (NULL);
	}


	while ((fgets(line, sizeof (line), fp) != 0)) {
		if (sscanf(line, "%s%s", drv, entry) != 2) {
			(void) fprintf(stderr, gettext(ERR_BAD_LINE),
			    file_name, line);
			continue;
		}

		if (strcmp(driver_name, drv) == 0) {
			if (first_match) {
				matches = calloc(strlen(entry) +2, 1);
				if (matches == NULL) {
					perror(NULL);
					(void) fprintf(stderr,
					    gettext(ERR_NO_MEM));
					return (NULL);
				}

				(void) strcpy(matches, entry);
				first_match = 0;

			} else {

				len = strlen(matches) + strlen(entry) + 2;
				tmp_match = realloc(matches, len);
				matches = tmp_match;
				(void) strcat(matches, " ");
				(void) strcat(matches, entry);
			}
		}
	}
	(void) fclose(fp);
	return (matches);
}


static void
usage()
{
	(void) fprintf(stderr, gettext(REM_USAGE1));
}
