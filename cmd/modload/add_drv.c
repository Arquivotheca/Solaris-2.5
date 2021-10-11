#pragma ident	"@(#)add_drv.c 1.13	95/01/12 SMI"

/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <wait.h>
#include <unistd.h>
#include <libintl.h>
#include <sys/modctl.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <locale.h>
#include "addrem.h"
#include "errmsg.h"

static int module_not_found(char *, char *);
static int unique_driver_name(char *, char *, char *, int *);
static int check_perm_opts(char *);
static int update_name_to_major(char *, major_t *);
static int aliases_unique(char *);
static int unique_drv_alias(char *);
static int config_driver(char *, major_t, char *, char *, int, int);
static void usage();
static int update_minor_perm(char *, char *);
static int update_driver_classes(char *, char *);
static int update_driver_aliases(char *, char *);
static int do_the_update(char *, char *);
static void signal_rtn();
static int exec_command(char *, char **);

int
main(int argc, char *argv[])
{
	int opt;
	struct stat buf;
	FILE *fp;
	major_t major_num;
	char driver_name[FILENAME_MAX + 1];
	char *path_driver_name;
	char *perms = NULL;
	char *aliases = NULL;
	char *classes = NULL;
	int noload_flag = 0;
	int i_flag = 0;
	int c_flag = 0;
	int m_flag = 0;
	int cleanup_flag = 0;
	int server = 0;
	char *basedir = NULL;
	char dup_entry[MAX_N2M_ALIAS_LINE + 1];
	char *cmdline[MAX_CMD_LINE];
	int n;
	int is_unique;
	FILE *reconfig_fp;
	char basedir_rec[PATH_MAX + FILENAME_MAX + 1];
	char *slash;
	int pathlen;
	int x;

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

	while ((opt = getopt(argc, argv, "m:ni:b:c:")) != EOF) {
		switch (opt) {
		case 'm' :
			m_flag = 1;
			perms = calloc(strlen(optarg) + 1, 1);
			if (perms == NULL) {
				(void) fprintf(stderr, gettext(ERR_NO_MEM));
				exit(1);
			}
			(void) strcat(perms, optarg);
			break;
		case 'n':
			noload_flag++;
			break;
		case 'i' :
			i_flag = 1;
			aliases = calloc(strlen(optarg) + 1, 1);
			if (aliases == NULL) {
				(void) fprintf(stderr, gettext(ERR_NO_MEM));
				exit(1);
			}
			(void) strcat(aliases, optarg);
			break;
		case 'b' :
			server = 1;
			basedir = calloc(strlen(optarg) + 1, 1);
			if (basedir == NULL) {
				(void) fprintf(stderr, gettext(ERR_NO_MEM));
				exit(1);
			}
			(void) strcat(basedir, optarg);
			break;
		case 'c':
			c_flag = 1;
			classes = strdup(optarg);
			if (classes == NULL) {
				(void) fprintf(stderr, gettext(ERR_NO_MEM));
				exit(1);
			}
			break;
		case '?' :
		default:
			usage();
			exit(1);
		}
	}


	if (argv[optind] != NULL) {
		path_driver_name = calloc(strlen(argv[optind]) + 1, 1);
		if (path_driver_name == NULL) {
			(void) fprintf(stderr, gettext(ERR_NO_MEM));
			exit(1);

		}
		(void) strcat(path_driver_name, argv[optind]);
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

	/* get module name from path */

	/* if <path>/<driver> ends with slash; strip off slash/s */

	pathlen = strlen(path_driver_name);
	for (x = 1; ((path_driver_name[pathlen - x ] == '/') &&
	    (pathlen != 1)); x++) {
		path_driver_name[pathlen - x] = '\0';
	}

	slash = strrchr(path_driver_name, '/');

	if (slash == NULL) {
		(void) strcpy(driver_name, path_driver_name);

	} else {
		(void) strcpy(driver_name, ++slash);
		if (driver_name[0] == '\0') {
			(void) fprintf(stderr, gettext(ERR_NO_DRVNAME),
			    path_driver_name);
			usage();
			exit(1);
		}
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

	/*
	 * have opened lock file; want to be sure to remove it
	 * whenever we exit.
	 */

	(void) sigset(SIGINT, signal_rtn);
	(void) sigset(SIGHUP, signal_rtn);
	(void) sigset(SIGTERM, signal_rtn);

	if ((some_checking(m_flag, i_flag)) == ERROR)
		err_exit();

	/*
	 * check validity of options
	 */
	if (m_flag) {
		if ((check_perm_opts(perms)) == ERROR)
			err_exit();
	}

	if (i_flag) {
		if (aliases != NULL)
			if ((aliases_unique(aliases)) == ERROR)
				err_exit();
	}


	if ((unique_driver_name(driver_name, name_to_major,
	    dup_entry, &is_unique)) == ERROR)
		err_exit();

	if (is_unique == NOT_UNIQUE) {
		(void) fprintf(stderr, gettext(ERR_NOT_UNIQUE), driver_name);
		err_exit();
	}

	if (!server) {
		if ((module_not_found(driver_name, path_driver_name))
		    == ERROR) {
			perror(NULL);
			(void) fprintf(stderr, gettext(ERR_NOMOD), driver_name);
			err_exit();
		}
	}

	if ((update_name_to_major(driver_name, &major_num)) == ERROR) {
		err_exit();
	}

	cleanup_flag |= CLEAN_NAM_MAJ;


	if (m_flag) {
		if (update_minor_perm(driver_name, perms) == ERROR) {
			cleanup_flag |= CLEAN_MINOR_PERM;
			remove_entry(cleanup_flag, driver_name);
			err_exit();
		}
		cleanup_flag |= CLEAN_MINOR_PERM;
	}

	if (i_flag) {
		if (update_driver_aliases(driver_name, aliases) == ERROR) {
			cleanup_flag |= CLEAN_DRV_ALIAS;
			remove_entry(cleanup_flag, driver_name);
			err_exit();

		}
		cleanup_flag |= CLEAN_DRV_ALIAS;
	}

	if (c_flag) {
		if (update_driver_classes(driver_name, classes) == ERROR) {
			cleanup_flag |= CLEAN_DRV_CLASSES;
			remove_entry(cleanup_flag, driver_name);
			err_exit();

		}
		cleanup_flag |= CLEAN_DRV_CLASSES;
	}

	if (server) {
		(void) fprintf(stderr, gettext(BOOT_CLIENT));

		/*
		 * create /reconfigure file so system reconfigures
		 * on reboot
		 */
		(void) strcpy(basedir_rec, basedir);
		(void) strcat(basedir_rec, RECONFIGURE);
		reconfig_fp = fopen(basedir_rec, "a");
		(void) fclose(reconfig_fp);
	} else {
		if (config_driver(driver_name, major_num,
		    aliases, classes, cleanup_flag, noload_flag) == ERROR) {
			err_exit();
		}
	}

	if (!server && !noload_flag) {
		/*
		 * run devlinks -r /
		 * run disks -r /
		 * run ports -r /
		 * run tapes -r /
		 */

		n = 0;
		cmdline[n++] = "devlinks";
		cmdline[n] = (char *)0;

		if (exec_command(DEVLINKS_PATH, cmdline)) {
			(void) fprintf(stderr, gettext(ERR_DEVLINKS),
			    DEVLINKS_PATH);
		}

		cmdline[0] = "disks";
		if (exec_command(DISKS_PATH, cmdline)) {
			(void) fprintf(stderr, gettext(ERR_DISKS), DISKS_PATH);
		}

		cmdline[0] = "ports";
		if (exec_command(PORTS_PATH, cmdline)) {
			(void) fprintf(stderr, gettext(ERR_PORTS), PORTS_PATH);
		}

		cmdline[0] = "tapes";
		if (exec_command(TAPES_PATH, cmdline)) {
			(void) fprintf(stderr, gettext(ERR_TAPES), TAPES_PATH);
		}
	}

	exit_unlock();


	return (NOERR);
}


int
module_not_found(char *module_name, char *path_driver_name)
{
	char path[MAXPATHLEN + FILENAME_MAX + 1];
	struct stat buf;
	struct stat ukdbuf;
	char data [MAXMODPATHS];
	char usr_kernel_drv[FILENAME_MAX + 17];
	char *next = data;

	/*
	 * if path
	 * 	if (path/module doesn't exist AND
	 *	 /usr/kernel/drv/module doesn't exist)
	 *	error msg
	 *	exit add_drv
	 */


	if (strcmp(module_name, path_driver_name)) {
		(void) strcpy(usr_kernel_drv, "/usr/kernel/drv/");
		(void) strcat(usr_kernel_drv, module_name);

		if (((stat(path_driver_name, &buf) == 0) &&
			((buf.st_mode & S_IFMT) == S_IFREG)) ||

		    ((stat(usr_kernel_drv, &ukdbuf) == 0) &&
			((ukdbuf.st_mode & S_IFMT) == S_IFREG))) {

			return (NOERR);
		}
	} else {
		/* no path */
		if (modctl(MODGETPATH, NULL, data) != 0) {
			(void) fprintf(stderr, gettext(ERR_MODPATH));
			return (ERROR);
		}

		next = strtok(data, MOD_SEP);
		while (next != NULL) {
			(void) sprintf(path, "%s/drv/%s", next, module_name);

			if ((stat(path, &buf) == 0) &&
			    ((buf.st_mode & S_IFMT) == S_IFREG)) {
				return (NOERR);
			}
			next = strtok((char *)NULL, MOD_SEP);
		}
	}

	return (ERROR);
}

/*
 * search for driver_name in first field of file file_name
 * searching name_to_major and driver_aliases: name separated from rest of
 * line by blank
 * if there return
 * else return
 */
int
unique_driver_name(
	char *driver_name,
	char *file_name,
	char *matched_line,
	int *is_unique)
{
	FILE *fp;
	char drv[FILENAME_MAX + 1];
	char entry[FILENAME_MAX + 1];
	char line[MAX_N2M_ALIAS_LINE + 1];

	*is_unique = UNIQUE;

	matched_line[0] = '\0';
	fp = fopen(file_name, "r");

	if (fp != NULL) {
		while ((fgets(line, sizeof (line), fp) != 0) &&
		    *is_unique == UNIQUE) {
			if (sscanf(line, "%s%s", drv, entry) != 2) {
				(void) fprintf(stderr, gettext(ERR_BAD_LINE),
				    file_name, line);
				continue;
			}

			if (strcmp(driver_name, drv) == 0)
				*is_unique = NOT_UNIQUE;

			/* return line containing driver name */
			(void) strcpy(matched_line, drv);
			(void) strcat(matched_line, " ");
			(void) strcat(matched_line, entry);
		}

		(void) fclose(fp);

		/* XXX */
		/* check alias file for name collision */
		if (unique_drv_alias(driver_name) == ERROR) {
			return (ERROR);
		}

		return (NOERR);

	} else {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_CANNOT_OPEN), file_name);
		return (ERROR);
	}

}

/*
 * check each entry in perm_list for:
 *	4 arguments
 *	permission arg is in valid range
 * permlist entries separated by comma
 * return ERROR/NOERR
 */
int
check_perm_opts(char *perm_list)
{
	char *current_head;
	char *previous_head;
	char *one_entry;
	int i, len, scan_stat;
	char minor[FILENAME_MAX + 1];
	char perm[OPT_LEN + 1];
	char own[OPT_LEN + 1];
	char grp[OPT_LEN + 1];
	char dumb[OPT_LEN + 1];
	int status = NOERR;
	int intperm;

	len = strlen(perm_list);

	if (len == 0) {
		usage();
		return (ERROR);
	}

	one_entry = calloc(len + 1, 1);
	if (one_entry == NULL) {
		(void) fprintf(stderr, gettext(ERR_NO_MEM));
		return (ERROR);
	}

	previous_head = perm_list;
	current_head = perm_list;

	while (*current_head != '\0') {

		for (i = 0; i <= len; i++)
			one_entry[i] = 0;

		current_head = get_entry(previous_head, one_entry, ',');

		previous_head = current_head;
		scan_stat = sscanf(one_entry, "%s%s%s%s%s", minor, perm, own,
		    grp, dumb);

		if (scan_stat < 4) {
			(void) fprintf(stderr, gettext(ERR_MIS_TOK),
			    "-m", one_entry);
			status = ERROR;
		}
		if (scan_stat > 4) {
			(void) fprintf(stderr, gettext(ERR_TOO_MANY_ARGS),
			    "-m", one_entry);
			status = ERROR;
		}

		intperm = atoi(perm);
		if (intperm < 0000 || intperm > 4777) {
			(void) fprintf(stderr, gettext(ERR_BAD_MODE), perm);
			status = ERROR;
		}

	}

	free(one_entry);
	return (status);
}


/*
 * get major number
 * write driver_name major_num to name_to_major file
 * major_num returned in major_num
 * return success/failure
 */
int
update_name_to_major(
	char *driver_name,
	major_t *major_num)
{
	char dup_entry[MAX_N2M_ALIAS_LINE + 1];
	char drv[FILENAME_MAX + 1];
	char major[MAX_STR_MAJOR + 1];
	struct stat buf;
	int max_dev;
	char *num_list;
	FILE *fp;
	char drv_majnum[MAX_STR_MAJOR + 1];
	char line[MAX_N2M_ALIAS_LINE + 1];
	int new_maj = -1;
	int i;
	int is_unique;

	/*
	 * if driver_name already in rem_name_to_major
	 * 	delete entry from rem_nam_to_major
	 *	put entry into name_to_major
	 */

	if (stat(rem_name_to_major, &buf) == 0) {

		if ((get_file_entry(driver_name,
		    rem_name_to_major, dup_entry, &is_unique)) == ERROR)
			return (ERROR);

		if (is_unique == NOT_UNIQUE) {

		/*
		 * found matching entry in /etc/rem_name_to_major
		 */

			if (dup_entry != NULL) {

				if (sscanf(dup_entry, "%s %s", drv,
				    major) != 2) {
					(void) fprintf(stderr,
					    gettext(ERR_BAD_LINE),
					    rem_name_to_major, dup_entry);

					return (ERROR);
				}

				if (append_to_file(driver_name, major,
				    name_to_major, ' ', " ") == ERROR) {
					(void) fprintf(stderr,
					    gettext(ERR_NO_UPDATE),
					    name_to_major);
					return (ERROR);
				} else {
					if (delete_entry(rem_name_to_major,
					    driver_name, " ") == ERROR) {
						(void) fprintf(stderr,
						    gettext(ERR_DEL_ENTRY),
						    driver_name,
						    rem_name_to_major);

						return (ERROR);
					}
				}
			} else {
				(void) fprintf(stderr, gettext(ERR_INT_UPDATE),
				    name_to_major);
				return (ERROR);
			}

			/* found matching entry : no errors */

			*major_num = atoi(major);
			return (NOERR);
		}
		/*
		 * no match found in rem_name_to_major
		 */

	}

	/*
	 * get maximum major number allowable on this system
	 */

	if (modctl(MODRESERVED, NULL, &max_dev) < 0) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_MAX_MAJOR));
		return (ERROR);
	}

	num_list = calloc(max_dev, 1);
	if (num_list == NULL) {
		(void) fprintf(stderr, gettext(ERR_NO_MEM));
		return (ERROR);
	}

	/*
	 * read thru name_to_major, marking each major number found
	 * order of name_to_major not relevant
	 */
	if ((fp = fopen(name_to_major, "r")) == NULL) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_CANT_ACCESS_FILE),
		    name_to_major);
		return (ERROR);
	}

	while (fgets(line, sizeof (line), fp) != 0) {

		if (sscanf(line, "%s %s", drv, drv_majnum) != 2) {
			(void) fprintf(stderr, gettext(ERR_BAD_LINE),
			    name_to_major, line);
			(void) fclose(fp);
			return (ERROR);
		}

		num_list[atoi(drv_majnum)] = 1;
	}

	/*
	 * read thru rem_name_to_major, marking each major number found
	 * order of rem_name_to_major not relevant
	 */

	(void) fclose(fp);
	fp = NULL;

	if (stat(rem_name_to_major, &buf) == 0) {
		if ((fp = fopen(rem_name_to_major, "r")) == NULL) {
			perror(NULL);
			(void) fprintf(stderr, gettext(ERR_CANT_ACCESS_FILE),
				rem_name_to_major);
			return (ERROR);
		}

		while (fgets(line, sizeof (line), fp) != 0) {

			if (sscanf(line, "%s %s", drv, drv_majnum) != 2) {
				(void) fprintf(stderr, gettext(ERR_BAD_LINE),
				name_to_major, line);
				(void) fclose(fp);
				return (ERROR);
			}

			num_list[atoi(drv_majnum)] = 1;
		}
		(void) fclose(fp);
	}

	/* find first free major number */
	for (i = 0; i < max_dev; i++) {
		if (num_list[i] != 1) {
			new_maj = i;
			break;
		}
	}

	if (new_maj == -1) {
		(void) fprintf(stderr, gettext(ERR_NO_FREE_MAJOR));
		return (ERROR);
	}

	(void) sprintf(drv_majnum, "%d", new_maj);
	if (do_the_update(driver_name, drv_majnum) == ERROR) {
		return (ERROR);
	}

	*major_num = new_maj;
	return (NOERR);
}

static void
usage()
{
	(void) fprintf(stderr, gettext(USAGE));
}

/*
 * check each alias :
 *	alias list members separated by white space
 *	cannot exist as driver name in /etc/name_to_major
 *	cannot exist as driver or alias name in /etc/driver_aliases
 */
int
aliases_unique(char *aliases)
{
	char *current_head;
	char *previous_head;
	char *one_entry;
	char return_string[MAX_N2M_ALIAS_LINE + 1];
	int i, len;
	int is_unique;

	len = strlen(aliases);

	one_entry = calloc(len + 1, 1);
	if (one_entry == NULL) {
		(void) fprintf(stderr, gettext(ERR_NO_MEM));
		return (ERROR);
	}

	previous_head = aliases;

	do {
		for (i = 0; i <= len; i++)
			one_entry[i] = 0;

		current_head = get_entry(previous_head, one_entry, ' ');
		previous_head = current_head;

		if ((unique_driver_name(one_entry, name_to_major,
		    return_string, &is_unique)) == ERROR) {
			free(one_entry);
			return (ERROR);
		}

		if (is_unique != UNIQUE) {
			(void) fprintf(stderr, gettext(ERR_ALIAS_IN_NAM_MAJ),
			    one_entry);
			free(one_entry);
			return (ERROR);
		}

		if (unique_drv_alias(one_entry) != NOERR) {
			free(one_entry);
			return (ERROR);
		}

	} while (*current_head != '\0');

	free(one_entry);

	return (NOERR);

}

int
unique_drv_alias(char *drv_alias)
{
	FILE *fp;
	char drv[FILENAME_MAX + 1];
	char line[MAX_N2M_ALIAS_LINE + 1];
	char alias[FILENAME_MAX + 1];
	int status = NOERR;

	fp = fopen(driver_aliases, "r");

	if (fp != NULL) {
		while ((fgets(line, sizeof (line), fp) != 0) &&
		    status != ERROR) {
			if (sscanf(line, "%s %s", drv, alias) != 2)
				(void) fprintf(stderr, gettext(ERR_BAD_LINE),
				    driver_aliases, line);

			if ((strcmp(drv_alias, drv) == 0) ||
			    (strcmp(drv_alias, alias) == 0)) {
				(void) fprintf(stderr,
				    gettext(ERR_ALIAS_IN_USE),
				    drv_alias);
				status = ERROR;
			}
		}
		(void) fclose(fp);
		return (status);
	} else {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_CANT_OPEN), driver_aliases);
		return (ERROR);
	}

}

/*
 * check that major_num doesn`t exceed maximum on this machine
 * do this here (again) to support add_drv on server for diskless clients
 */
int
config_driver(
	char *driver_name,
	major_t major_num,
	char *aliases,
	char *classes,
	int cleanup_flag,
	int noload_flag)
{
	int max_dev;
	int n = 0;
	char *cmdline[MAX_CMD_LINE];
	char maj_num[128];
	char *previous;
	char *current;
	int exec_status;
	int len;
	FILE *fp;

	if (modctl(MODRESERVED, NULL, &max_dev) < 0) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_MAX_MAJOR));
		return (ERROR);
	}

	if (major_num >= max_dev) {
		(void) fprintf(stderr, gettext(ERR_MAX_EXCEEDS),
		    major_num, max_dev);
		return (ERROR);
	}

	/* bind major number and driver name */

	/* build command line */
	cmdline[n++] = DRVCONFIG;
	if (noload_flag)
		cmdline[n++] = "-n";
	cmdline[n++] = "-b";
	if (classes) {
		cmdline[n++] = "-c";
		cmdline[n++] = classes;
	}
	cmdline[n++] = "-i";
	cmdline[n++] = driver_name;
	cmdline[n++] = "-m";
	(void) sprintf(maj_num, "%lu", major_num);
	cmdline[n++] = maj_num;

	if (aliases != NULL) {
		len = strlen(aliases);
		previous = aliases;
		do {
			cmdline[n++] = "-a";
			cmdline[n] = calloc(len + 1, 1);
			if (cmdline[n] == NULL) {
				(void) fprintf(stderr,
				    gettext(ERR_NO_MEM));
				return (ERROR);
			}
			current = get_entry(previous,
			    cmdline[n++], ' ');
			previous = current;

		} while (*current != '\0');

	}
	cmdline[n] = (char *)0;

	exec_status = exec_command(DRVCONFIG_PATH, cmdline);

	if (exec_status != NOERR) {
		perror(NULL);
		remove_entry(cleanup_flag, driver_name);
		return (ERROR);
	}


	/*
	 * now that we have the name to major number bound,
	 * config the driver
	 */

	/*
	 * create /reconfigure file so system reconfigures
	 * on reboot if we're actually loading the driver
	 * now
	 */
	if (!noload_flag) {
		fp = fopen(RECONFIGURE, "a");
		(void) fclose(fp);
	}

	/* build command line */

	n = 0;
	cmdline[n++] = DRVCONFIG;
	if (noload_flag)
		cmdline[n++] = "-n";
	cmdline[n++] = "-i";
	cmdline[n++] = driver_name;
	cmdline[n++] = "-r";
	cmdline[n++] = DEVFS_ROOT;
	cmdline[n] = (char *)0;

	exec_status = exec_command(DRVCONFIG_PATH, cmdline);

	if (exec_status != NOERR) {
		/* no clean : name and major number are bound */
		(void) fprintf(stderr, gettext(ERR_CONFIG), driver_name);
		return (ERROR);
	}

	return (NOERR);
}

static int
update_driver_classes(
	char *driver_name,
	char *classes)
{
	/* make call to update the classes file */
	return (append_to_file(driver_name, classes, driver_classes,
	    ' ', "\t"));
}

static int
update_driver_aliases(
	char *driver_name,
	char *aliases)
{
	/* make call to update the aliases file */
	return (append_to_file(driver_name, aliases, driver_aliases, ' ', " "));

}

static int
update_minor_perm(
	char *driver_name,
	char *perm_list)
{
	return (append_to_file(driver_name, perm_list, minor_perm, ',', ":"));
}

static int
do_the_update(
	char *driver_name,
	char *major_number)
{

	return (append_to_file(driver_name, major_number, name_to_major,
	    ' ', " "));
}

static void
signal_rtn()
{
	exit_unlock();
}

static int
exec_command(
	char *path,
	char *cmdline[MAX_CMD_LINE])
{
	pid_t pid;
	u_int stat_loc;
	int waitstat;
	int exit_status;

	/* child */
	if ((pid = fork()) == 0) {

		(void) execv(path, cmdline);
		perror(NULL);
		return (ERROR);
	} else if (pid == -1) {
		/* fork failed */
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_FORK_FAIL), cmdline);
		return (ERROR);
	} else {
		/* parent */
		do {
			waitstat = waitpid(pid, (int *)&stat_loc, 0);

		} while ((!WIFEXITED(stat_loc) &&
			!WIFSIGNALED(stat_loc)) || (waitstat == 0));

		exit_status = WEXITSTATUS(stat_loc);

		return (exit_status);
	}
}
