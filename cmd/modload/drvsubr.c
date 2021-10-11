/*
 *   Copyright (c) 1993 by Sun Microsystems, Inc.
 */
#pragma ident   "@(#)drvsubr.c 1.7     95/01/12 SMI"


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libintl.h>
#include <wait.h>
#include <sys/buf.h>
#include <sys/stat.h>
#include "addrem.h"
#include "errmsg.h"
#include <string.h>
#include <errno.h>


/*
 *  open file
 * for each entry in list
 *	where list entries are separated by <list_separator>
 * 	append entry : driver_name <entry_separator> entry
 * close file
 * return error/noerr
 */
int
append_to_file(
	char *driver_name,
	char *entry_list,
	char *filename,
	char list_separator,
	char *entry_separator)
{
	FILE *fp;
	int fpint;
	char *line;
	char *current_head;
	char *previous_head;
	char *one_entry;
	int len;
	int i;

	if ((fp = fopen(filename, "a")) == NULL) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_CANT_ACCESS_FILE),
		    filename);
		return (ERROR);
	}

	len = strlen(entry_list);

	one_entry = calloc(len + 1, 1);
	if (one_entry == NULL) {
		(void) fprintf(stderr, gettext(ERR_NO_UPDATE), filename);
		(void) fprintf(stderr, gettext(ERR_NO_MEM));
		(void) fclose(fp);
		return (ERROR);
	}

	previous_head = entry_list;

	line = calloc(strlen(driver_name) + len + 4, 1);
	if (line == NULL) {
		(void) fprintf(stderr, gettext(ERR_NO_MEM));
		(void) fclose(fp);
		err_exit();
	}

	/*
	 * get one entry at a time from list and append to
	 * <filename> file
	 */

	do {

		for (i = 0; i <= len; i++)
			one_entry[i] = 0;

		for (i = 0; i <= (int)strlen(line); i++)
			line[i] = 0;

		current_head = get_entry(previous_head, one_entry,
		    list_separator);
		previous_head = current_head;

		(void) strcpy(line, driver_name);
		(void) strcat(line, entry_separator);
		(void) strcat(line, one_entry);
		(void) strcat(line, "\n");

		if ((fputs(line, fp)) == EOF) {
			perror(NULL);
			(void) fprintf(stderr, gettext(ERR_NO_UPDATE),
			    filename);
		}

	} while (*current_head != '\0');


	(void) fflush(fp);

	fpint = fileno(fp);
	(void) fsync(fpint);

	(void) fclose(fp);

	free(one_entry);
	free(line);

	return (NOERR);
}


/*
 *  open file
 * read thru file, deleting all entries if first
 *    entry = driver_name
 * close
 * if error, leave original file intact with message
 * assumption : drvconfig has been modified to work with clone
 *  entries in /etc/minor_perm as driver:mummble NOT
 *  clone:driver mummble
 * this implementation will NOT find clone entries
 * clone:driver mummble
 */
int
delete_entry(
	char *oldfile,
	char *driver_name,
	char *marker)
{
	FILE *fp;
	FILE *newfp;
	int newfpint;
	char line[MAX_DBFILE_ENTRY];
	char drv[FILENAME_MAX + 1];

	int i;
	int status = NOERR;

	char *newfile;
	char *tptr;

	if ((fp = fopen(oldfile, "r")) == NULL) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_CANT_ACCESS_FILE), oldfile);
		return (ERROR);
	}

	/*
	 * Build filename for temporary file
	 */

	if ((tptr = calloc(strlen(oldfile) + strlen(XEND) + 1, 1)) == NULL) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_NO_MEM));
	}

	(void) strcpy(tptr, oldfile);
	(void) strcat(tptr, XEND);

	newfile = mktemp(tptr);

	if ((newfp = fopen(newfile, "w")) == NULL) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_CANT_ACCESS_FILE),
		    newfile);
		return (ERROR);
	}

	while ((fgets(line, sizeof (line), fp) != 0) && status == NOERR) {
		if (*line == '#' || *line == '\n') {
			if ((fputs(line, newfp)) == EOF) {
				(void) fprintf(stderr, gettext(ERR_UPDATE),
				    oldfile);
				status = ERROR;
			}
			continue;
		}
		if (sscanf(line, "%s", drv) != 1) {
			(void) fprintf(stderr, gettext(ERR_BAD_LINE),
			    oldfile, line);
			status = ERROR;
		}


		for (i = strcspn(drv, marker); i < FILENAME_MAX; i++) {
			drv[i] =  '\0';
		}

		if (strcmp(driver_name, drv) != 0) {
			if ((fputs(line, newfp)) == EOF) {
				(void) fprintf(stderr, gettext(ERR_UPDATE),
				    oldfile);
				status = ERROR;
			}

		}
	}

	(void) fclose(fp);

	newfpint = fileno(newfp);
	(void) fsync(newfpint);
	(void) fclose(newfp);

	/*
	 * if error, leave original file, delete new file
	 * if noerr, replace original file with new file
	 */

	if (status == NOERR) {
		if (rename(oldfile, tmphold) == -1) {
			perror(NULL);
			(void) fprintf(stderr, gettext(ERR_UPDATE), oldfile);
			(void) unlink(newfile);
			return (ERROR);
		} else if (rename(newfile, oldfile) == -1) {
			perror(NULL);
			(void) fprintf(stderr, gettext(ERR_UPDATE), oldfile);
			(void) unlink(oldfile);
			(void) unlink(newfile);
			if (link(tmphold, oldfile) == -1) {
				perror(NULL);
				(void) fprintf(stderr, gettext(ERR_BAD_LINK),
				    oldfile, tmphold);
			}
			return (ERROR);
		}
		(void) unlink(tmphold);
	} else {
		/*
		 * since there's an error, leave file alone; remove
		 * new file
		 */
		if (unlink(newfile) == -1) {
			(void) fprintf(stderr, gettext(ERR_CANT_RM), newfile);
		}
		return (ERROR);
	}

	return (NOERR);

}

/*
 * search for driver_name in first field of file file_name
 * searching name_to_major and driver_aliases: name separated from rest of
 * line by blank
 * if there return
 * else return
 */
int
get_file_entry(
	char *driver_name,
	char *file_name,
	char *matched_line,
	int *is_unique)
{
	FILE *fp;
	char drv[FILENAME_MAX + 1];
	char entry[FILENAME_MAX + 1];
	char line[MAX_N2M_ALIAS_LINE];

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
		return (NOERR);

	} else {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_CANT_OPEN), file_name);
		return (ERROR);
	}
}


/*
 * given pointer to member n in space separated list, return pointer
 * to member n+1, return member n
 */
char *
get_entry(
	char *prev_member,
	char *current_entry,
	char separator)
{
	char *ptr;

	ptr = prev_member;

	/* skip white space */
	while (*ptr == '\t' || *ptr == ' ')
		ptr++;

	/* read thru the current entry */
	while (*ptr != separator && *ptr != '\0') {
		*current_entry++ = *ptr++;
	}
	*current_entry = '\0';

	if ((separator == ',') && (*ptr == separator))
		ptr++;	/* skip over comma */

	/* skip white space */
	while (*ptr == '\t' || *ptr == ' ') {
		ptr++;
	}

	return (ptr);
}

void
err_exit()
{
	/* remove add_drv/rem_drv lock */
	exit_unlock();
	exit(1);
}

void
exit_unlock()
{
	struct stat buf;

	if (stat(add_rem_lock, &buf) == NOERR) {
		if (unlink(add_rem_lock) == -1) {
			(void) fprintf(stderr, gettext(ERR_REM_LOCK),
			    add_rem_lock);
		}
	}
}

/*
 * error adding driver; need to back out any changes to files.
 * check flag to see which files need entries removed
 * entry removal based on driver name
 */
void
remove_entry(
	int c_flag,
	char *driver_name)
{

	if (c_flag & CLEAN_NAM_MAJ) {
		if (delete_entry(name_to_major, driver_name, " ") == ERROR) {
			(void) fprintf(stderr, gettext(ERR_NO_CLEAN),
			    name_to_major, driver_name);
		}
	}

	if (c_flag & CLEAN_DRV_ALIAS) {
		if (delete_entry(driver_aliases, driver_name, " ") == ERROR) {
			(void) fprintf(stderr, gettext(ERR_DEL_ENTRY),
			    driver_name, driver_aliases);
		}
	}

	if (c_flag & CLEAN_DRV_CLASSES) {
		if (delete_entry(driver_classes, driver_name, "\t") == ERROR) {
			(void) fprintf(stderr, gettext(ERR_DEL_ENTRY),
			    driver_name, driver_classes);
		}
	}

	if (c_flag & CLEAN_MINOR_PERM) {
		if (delete_entry(minor_perm, driver_name, ":") == ERROR) {
			(void) fprintf(stderr, gettext(ERR_DEL_ENTRY),
			    driver_name, minor_perm);
		}
	}
}


int
some_checking(
	int m_flag,
	int i_flag)
{
	int status = NOERR;
	int mode = 0;

	/* check name_to_major file : exists and is writable */

	mode = R_OK | W_OK;

	if (access(name_to_major, mode)) {
		perror(NULL);
		(void) fprintf(stderr, gettext(ERR_CANT_ACCESS_FILE),
		    name_to_major);
		status =  ERROR;
	}

	/* check minor_perm file : exits and is writable */
	if (m_flag) {
		if (access(minor_perm, mode)) {
			perror(NULL);
			(void) fprintf(stderr, gettext(ERR_CANT_ACCESS_FILE),
			    minor_perm);
			status =  ERROR;
		}
	}

	/* check driver_aliases file : exits and is writable */
	if (i_flag) {
		if (access(driver_aliases, mode)) {
			perror(NULL);
			(void) fprintf(stderr, gettext(ERR_CANT_ACCESS_FILE),
			    driver_aliases);
			status =  ERROR;
		}
	}

	return (status);
}

/*
 * All this stuff is to support a server installing
 * drivers on diskless clients.  When on the server
 * need to prepend the basedir
 */
int
build_filenames(char *basedir)
{
	int len;

	if (basedir == NULL) {
		driver_aliases = DRIVER_ALIAS;
		driver_classes = DRIVER_CLASSES;
		minor_perm = MINOR_PERM;
		name_to_major = NAM_TO_MAJ;
		rem_name_to_major = REM_NAM_TO_MAJ;
		add_rem_lock = ADD_REM_LOCK;
		tmphold = TMPHOLD;
		devfs_root = DEVFS_ROOT;

	} else {
		len = strlen(basedir);

		driver_aliases = calloc(len + strlen(DRIVER_ALIAS) +1, 1);
		driver_classes = calloc(len + strlen(DRIVER_CLASSES) +1, 1);
		minor_perm = calloc(len + strlen(MINOR_PERM) +1, 1);
		name_to_major = calloc(len + strlen(NAM_TO_MAJ) +1, 1);
		rem_name_to_major = calloc(len +
		    strlen(REM_NAM_TO_MAJ) +1, 1);
		add_rem_lock = calloc(len + strlen(ADD_REM_LOCK) +1, 1);
		tmphold = calloc(len + strlen(TMPHOLD) +1, 1);
		devfs_root = calloc(len + strlen(DEVFS_ROOT) + 1, 1);


		if ((driver_aliases == NULL) ||
		    (driver_classes == NULL) ||
		    (minor_perm == NULL) ||
		    (name_to_major == NULL) ||
		    (rem_name_to_major == NULL) ||
		    (add_rem_lock == NULL) ||
		    (tmphold == NULL)) {
			(void) fprintf(stderr, gettext(ERR_NO_MEM));
			return (ERROR);
		}

		(void) strcpy(driver_aliases, basedir);
		(void) strcat(driver_aliases, DRIVER_ALIAS);

		(void) strcpy(driver_classes, basedir);
		(void) strcat(driver_classes, DRIVER_CLASSES);

		(void) strcpy(minor_perm, basedir);
		(void) strcat(minor_perm, MINOR_PERM);

		(void) strcpy(name_to_major, basedir);
		(void) strcat(name_to_major, NAM_TO_MAJ);

		(void) strcpy(rem_name_to_major, basedir);
		(void) strcat(rem_name_to_major, REM_NAM_TO_MAJ);

		(void) strcpy(add_rem_lock, basedir);
		(void) strcat(add_rem_lock, ADD_REM_LOCK);

		(void) strcpy(tmphold, basedir);
		(void) strcat(tmphold, TMPHOLD);

		(void) strcpy(devfs_root, basedir);
		(void) strcat(devfs_root, DEVFS_ROOT);

	}

	return (NOERR);
}
