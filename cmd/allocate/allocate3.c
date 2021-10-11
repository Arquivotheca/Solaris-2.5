#if !defined(lint) && defined(SCCSIDS)
static char	*bsm_sccsid = "@(#)allocate3.c 1.6 93/07/13 SMI; SunOS BSM";
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <pwd.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#ifdef SunOS_CMW
#include <sys/label.h>
#endif

#include <bsm/devices.h>
#include "allocate.h"
#ifdef SunOS_CMW
#include <bsm/auditwrite.h>
#include <cmw/authoriz.h>
#include <cmw/priv.h>
#endif
#include <bsm/audit_uevents.h>

#define	EXIT(number) { \
	if (optflg & FORCE) \
		error = number; \
	else \
		return (number); \
}

extern void audit_allocate_list();
extern void audit_allocate_device();
extern void audit_deallocate_dev();

extern int	errno;
static int	alloc_uid = -1;
static int	alloc_gid = -1;
#ifdef SunOS_CMW
static bclabel_t cmw_plabel;
static bclabel_t alloc_label;
static bclabel_t file_label;
static bilabel_t info_label;
#endif

isso()
{
#ifdef CMW
	priv_set_t	priv_saved_set;
	priv_set_t	priv_forced_set;
	priv_set_t	priv_permitted_set;

#ifdef DEBUG
	fprintf(stderr, "\n Checking for ISSO privileges \n");
#endif

	if (getprocpriv(sizeof (priv_set_t),
	    priv_saved_set, PRIV_SAVED) != 0) {
		perror("allocate");
	}

	if (getprocpriv(sizeof (priv_set_t),
	    priv_permitted_set, PRIV_PERMITTED) != 0) {
		perror("allocate");
	}


	PRIV_COPY(priv_saved_set, priv_forced_set);
	PRIV_XOR (priv_permitted_set, priv_forced_set);

	if (PRIV_ISEMPTY(priv_forced_set)) {
		return (1);
	}

	return (0);
#else
	if (getuid() == 0)
		return (1);
	return (0);
#endif
}


allocate_uid()
{
	struct passwd *passwd;

	if ((passwd = getpwnam(ALLOC_USER)) == NULL)  {
#ifdef DEBUG
		perror("Unable to get the allocate uid\n");
#endif
		return (-1);
	}
	alloc_uid = passwd->pw_uid;
	alloc_gid = passwd->pw_gid;
	return (alloc_uid);
}


#ifdef	SunOS_CMW
get_curr_label()
{
	if (getcmwplabel(&cmw_plabel) != 0) {
#ifdef DEBUG
		perror("Unable to get process sensitivity label\n");
#endif DEBUG
		return (-1);
	} else
		return (0);
}


#endif	/* SunOS_CMW */

base_il()
{
#ifdef CMW
	int	error;

	if (stobil("system_low", &info_label, NEW_LABEL, &error)) {
		return (0);
	} else {
#ifdef DEBUG
		perror("Unable to set system_low info label\n");
#endif
		return (-1);
	}
#endif
	return (0);
}


#ifdef	SunOS_CMW
set_alloc_label()
{
	int	error;
	if (stobcl(DEALLOC_LABEL, &alloc_label, NEW_LABEL, &error) != 1) {
#ifdef DEBUG
		fprintf(stderr, "Unable to convert label DEALLOC_LABEL\n");
#endif
		return (error);
	}
	return (0);
}


#endif	/* SunOS_CMW */

check_dev_range(dev_min, dev_max)
char	*dev_min;
char	*dev_max;
{
#ifdef CMW
	brange_t   dev_range;
	int	error;

	if (dev_min == NULL)
		return (-1);
	if (dev_max == NULL)
		return (-1);

	if (stobsl(dev_min, &(dev_range.lower_bound), NEW_LABEL, &error) !=
	    1) {
#ifdef DEBUG
		fprintf(stderr, "Unable to convert label %s \n", dev_min);
#endif
		return (-1);
	}
	if (stobsl(dev_max, &(dev_range.upper_bound), NEW_LABEL, &error) !=
	    1) {
#ifdef DEBUG
		fprintf(stderr, "Unable to convert label %s \n", dev_max);
#endif
		return (-1);
	}
	if (!blinrange(cmw_plabel.bcl_sensitivity_label, dev_range))
		return (-1);

	return (0);
#else
	return (0);
#endif
}


check_auth(uid, authoriz)
int	uid;
char	*authoriz;
{
#ifdef CMW
	aud_accume(AUTHORIZ, authoriz);
#ifdef DEBUG
	printf("Checking authorization for %d\n", uid);
#endif
	if (check_authorization(uid, authoriz, NULL, 0) != 1) {
		aud_auth(-1);
		return (0);
	}
	aud_auth(0);
#endif
	return (1);
}


check_devs(list)
char	*list;
{
	char	*file;

	file = strtok(list, " ");
	while (file != NULL) {

		if (access(file, F_OK) == -1) {
#ifdef DEBUG
			fprintf(stderr, "Unable to access file %s\n", file);
#endif
			return (-1);
		}
		file = strtok(NULL, " ");
	}
	return (0);
}


print_dev(dev_list)
devmap_t *dev_list;
{
	char	*file;

	printf("device: %s ", dev_list->dmap_devname);
	printf("type: %s ", dev_list->dmap_devtype);
	printf("files: ");

	file = strtok(dev_list->dmap_devlist, " ");
	while (file != NULL) {
		printf("%s ", file);
		file = strtok(NULL, " ");
	}
	printf("\n");
	return (0);
}




list_device(optflg, uid, device)
int	optflg;
int	uid;
char	*device;

{
	devalloc_t * dev_ent;
	devmap_t * dev_list;
	char	file_name[1024];
	struct stat stat_buf;
#ifdef SunOS_CMW
	bclabel_t flabel;
#endif
	char	*list;

	if ((dev_ent = getdanam(device)) == NULL) {
		if ((dev_list = getdmapdev(device)) == NULL) {
#ifdef DEBUG
			fprintf(stderr, "Unable to find %s ", device);
			fprintf("in the allocate database\n");
#endif
			return (NODMAPENT);
		} else if ((dev_ent = getdanam(dev_list->dmap_devname)) ==
		    NULL) {
#ifdef DEBUG
			fprintf(stderr, "Unable to find %s ", device);
			fprintf(stderr, "in the allocate database\n");
#endif
			return (NODAENT);
		}
	} else if ((dev_list = getdmapnam(device)) == NULL) {
#ifdef DEBUG
		fprintf(stderr, "Unable to find %s in the allocate database\n",
			device);
#endif
		return (NODMAPENT);
	}

	strcpy(file_name, DAC_DIR);
	strcat(file_name, "/");
	strcat(file_name, dev_ent->da_devname);

	if (stat(file_name, &stat_buf)) {
#ifdef DEBUG
		fprintf(stderr, "Unable to stat %s\n", file_name);
		perror("Error:");
#endif
		return (DACACC);
	}

	if ((optflg & FREE) && (stat_buf.st_uid != alloc_uid))
		return (ALLOC);

	if ((optflg & LIST) && (stat_buf.st_uid != alloc_uid) &&
	    (stat_buf.st_uid != uid))
		return (ALLOC_OTHER);

	if ((optflg & CURRENT) && (stat_buf.st_uid != uid))
		return (NALLOC);

	if ((stat_buf.st_mode & ~S_IFMT) == ALLOC_ERR_MODE)
		return (ALLOCERR);

	if (check_dev_range(dev_ent->da_devmin, dev_ent->da_devmax) == -1)
		return (DEVRNG);

#ifdef CMW
	if (getcmwlabel(file_name, &flabel) == -1) {
#ifdef DEBUG
		fprintf(stderr, "list_device: unable to get cmw label, %s\n",
			file_name);
		perror("Error:");
#endif
		return (GETLABEL_PERR);
	}

#ifndef SunOS_CMW
	if (!isso() && (stat_buf.st_uid != alloc_uid))
#else

	if (!isso() && (stat_buf.st_uid != alloc_uid) &&
		!bldominates(bcltosl(&cmw_plabel), bcltosl(&flabel)))
#endif
	{
#ifdef DEBUG
		fprintf(stderr, "deallocate: proc label doesn't dominate ");
		fprintf(stderr, "file label, %s\n", file_name);
#endif
		return (MACERR);
	}
#endif

	if ((list = (char *) malloc((unsigned) strlen(dev_list->dmap_devlist)
	    *sizeof (char) + 1)) == NULL)
		return (SYSERROR);

	strcpy(list, dev_list->dmap_devlist);
	if (check_devs(list) == -1) {
		free(list);
		return (DSPMISS);
	}

	print_dev(dev_list);

	free(list);
	return (0);
}


list_devices(optflg, uid, device)
int	optflg;
int	uid;
char	*device;
{
	DIR   * dev_dir;
	struct dirent *dac_file;
	int	error = 0, ret_code = 1;

	if (optflg & USER) {
		if (!isso())
			return (NOTROOT);
	} else if ((uid = getuid()) == (uid_t) -1)
		return (SYSERROR);

	if (allocate_uid() == (uid_t) -1)
		return (SYSERROR);

#ifdef	SunOS_CMW
	if (get_curr_label() == -1)
		return (SYSERROR);
#endif	/* SunOS_CMW */

	setdaent();

	if (device) {
		error = list_device(optflg, uid, device);
		return (error);
	}


	if ((dev_dir = opendir(DAC_DIR)) == NULL) {

#ifdef DEBUG
		perror("Can't open DAC_DIR\n");
#endif
		return (DACACC);
	}

	while ((dac_file = readdir(dev_dir)) != NULL) {
		if (!strcmp(dac_file->d_name, ".") || !strcmp(dac_file->d_name,
			".."))
			continue;
		else {
			error = list_device(optflg, uid, dac_file->d_name);
			ret_code = ret_code ? error : ret_code;
		}
	}
	closedir(dev_dir);
	enddaent();
	return (ret_code);
}


#ifdef	SunOS_CMW
set_il(label)
char	*label;
{
	int	error;
	if (!stobcl(label, &cmw_plabel, NULL, &error))
		return (-1);
		else
		return (0);
}


#endif	/* SunOS_CMW */

mk_dac(file, uid)
char	*file;
int	uid;
{
	if (chown(file, uid, getgid()) == -1) {
#ifdef DEBUG
		perror("mk_dac, unable to chown\n");
#endif
		return (CHOWN_PERR);
	}

	if (chmod(file, ALLOC_MODE) == -1) {
#ifdef DEBUG
		perror("mk_dac, unable to chmod\n");
#endif
		return (CHMOD_PERR);
	}

#ifdef CMW
	if (setcmwlabel(file, &cmw_plabel, SETCL_ALL) == -1) {
#ifdef DEBUG
		perror("mk_dac, unable to set cmw label\n");
#endif
		return (SETLABEL_PERR);
	}
#endif

	return (0);
}


unmk_dac(file)
char	*file;
{
	int	error = 0;

	if (chown(file, alloc_uid, alloc_gid) == -1) {
#ifdef DEBUG
		perror("set_alloc_err, unable to chown\n");
#endif
		error = CHOWN_PERR;
	}

	if (chmod(file, DEALLOC_MODE) == -1) {
#ifdef DEBUG
		perror("set_alloc_err, unable to chmod\n");
#endif
		error = CHMOD_PERR;
	}

#ifdef CMW
	if (setcmwlabel(file, &alloc_label, SETCL_ALL) == -1) {
#ifdef DEBUG
		perror("un_mkdac, unable to set cmw label\n");
#endif
		error = SETLABEL_PERR;
	}
#endif
	return (error);
}


set_alloc_err(file)
char	*file;
{
	if (chown(file, alloc_uid, alloc_gid) == -1) {
#ifdef DEBUG
		perror("set_alloc_err, unable to chown\n");
#endif
		return (CHOWN_PERR);
	}

	if (chmod(file, ALLOC_ERR_MODE) == -1) {
#ifdef DEBUG
		perror("set_alloc_err, unable to chmod\n");
#endif
		return (CHMOD_PERR);
	}

	return (0);
}


lock_dev(file)
char	*file;
{
	int	fd;

	if ((fd = open(file, O_RDONLY)) == -1) {
#ifdef DEBUG
		perror("lock_dev, cannot open DAC file\n");
#endif
		return (DACACC);
	}

#ifdef CMW
	if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
#ifdef DEBUG
		perror("lock_dev, cannot set lock\n");
#endif
		return (DACLCK);
	}
#else	/* SVR4 */
	if (lockf(fd, F_TLOCK, 0) == -1) {
#ifdef DEBUG
		perror("lock_dev, cannot set lock\n");
#endif
		return (DACLCK);
	}
#endif

	close(fd);
	return (0);
}


unlock_dev(file)
char	*file;
{
	int	fd;

	if ((fd = open(file, O_RDONLY)) == -1) {
#ifdef DEBUG
		perror("lock_dev, cannot open DAC file\n");
#endif
		return (DACACC);
	}

#ifdef CMW
	if (flock(fd, LOCK_UN | LOCK_NB) == -1) {
#ifdef DEBUG
		perror("lock_dev, cannot remove lock\n");
#endif
		return (DACLCK);
	}
#else
	if (lockf(fd, F_ULOCK, 0) == -1) {
#ifdef DEBUG
		perror("lock_dev, cannot remove lock\n");
#endif
		return (DACLCK);
	}
#endif

	close(fd);
	return (0);
}


mk_alloc(list, uid)
char	*list;
int	uid;
{
	char	*file;
	int	gid;

	gid = getgid();
#ifdef	SunOS_CMW
	aud_accume(DEV_LIST, list);
#else
	audit_allocate_list(list);
#endif
	file = strtok(list, " ");
	while (file != NULL) {

#ifdef DEBUG
		fprintf(stderr, "Allocating %s\n", file);
#endif
		if (chown(file, uid, gid) == -1) {
#ifdef DEBUG
			perror("mk_alloc, unable to chown\n");
#endif
			return (CHOWN_PERR);
		}

		if (chmod(file, ALLOC_MODE) == -1) {
#ifdef DEBUG
			perror("mk_alloc, unable to chmod\n");
#endif
			return (CHMOD_PERR);
		}

#ifdef CMW
		if (setcmwlabel(file, &cmw_plabel, SETCL_ALL) == -1) {
#ifdef DEBUG
			perror("mk_alloc, unable to set cmw label\n");
#endif
			return (SETLABEL_PERR);
		}
#endif

		file = strtok(NULL, " ");
	}
	return (0);
}


mk_unalloc(list)
char	*list;
{
	char	*file;
	int	error = 0;
	char	base[256];
	int child, status;

#ifdef	SunOS_CMW
	aud_accume(DEV_LIST, list);
#else
	audit_allocate_list(list);
#endif	/* SunOS_CMW */
	child = vfork();
	switch (child) {
	case -1:
		return (-1);
	case 0:
		setuid(0);
		base_il();
		file = strtok(list, " ");
		while (file != NULL) {

#ifdef DEBUG
			fprintf(stderr, "Deallocating %s\n", file);
#endif

#ifdef	SunOS_CMW
			if (revoke(file, 1) == -1) {
#ifdef DEBUG
				fprintf("mk_unalloc: unable to revoke %s\n",
				    file);
				perror("");
#endif
				error = CNTFRC;
			}
#else
			sprintf(base, "/usr/sbin/fuser -k %s", file);
			if (system(base) < 0) {
#ifdef DEBUG
				fprintf("mk_unalloc: unable to revoke %s\n",
					file);
				perror("");
#endif
				error = CNTFRC;
			}
#endif	/* SunOS_CMW */

			if (chown(file, alloc_uid, alloc_gid) == -1) {
#ifdef DEBUG
				perror("mk_unalloc, unable to chown\n");
#endif
				error = CHOWN_PERR;
			}

			if (chmod(file, DEALLOC_MODE) == -1) {
#ifdef DEBUG
				perror("mk_unalloc, unable to chmod\n");
#endif
				error = CHMOD_PERR;
			}

#ifdef	SunOS_CMW
			if (getcmwlabel(file, &file_label) == -1) {
#ifdef DEBUG
				perror("mk_unalloc, unable to get cmw label\n");
#endif
				error = GETLABEL_PERR;
			}

			bilconjoin(&info_label,
				&(file_label.bcl_information_label));

			if (setcmwlabel(file, &alloc_label, SETCL_ALL) == -1) {
#ifdef DEBUG
				perror("mk_unalloc, unable to set cmw label\n");
#endif
				error = SETLABEL_PERR;
			}
#endif	/* SunOS_CMW */

			file = strtok(NULL, " ");
		}
		exit(error);
	default:
		while (wait(&status) != child);
		if (WIFEXITED(status)) return (WEXITSTATUS(status));
		return (-1);
	}
}


exec_clean(optflg, name, path)
int	optflg;
char	*name, *path;
{
	char	mode[8], *cmd, *info_ascii = NULL;
#ifdef CMW
	union wait status;
#else
	int	status;
#endif
	int	c;

	if (optflg & FORCE_ALL)
		sprintf(mode, "-i");
	else if (optflg & FORCE)
		sprintf(mode, "-f");
	else
		sprintf(mode, "-s");
	if ((cmd = strrchr(path, '/')) == NULL)
		cmd = path;
#ifdef SunOS_CMW
	biltos(&info_label, &info_ascii, 0, 0);
#endif
	c = vfork();
	switch (c) {
	case -1:
		return (-1);
	case 0:
		setuid(0);
		execl(path, cmd, mode, name, info_ascii, NULL);
#ifdef DEBUG
		fprintf(stderr, "Unable to execute clean up script %s\n",
			path);
		perror("");
#endif
		exit(CNTDEXEC);
	default:
#ifdef SunOS_CMW
		while (wait(&status) != c);
#else
		while (wait(&status) != c);
#endif
		if (WIFEXITED(status))
			return (WEXITSTATUS(status));
#ifdef DEBUG
		fprintf(stderr, "exit status %d\n", status);
#endif
		return (-1);
	}
}


allocate_dev(optflg, uid, dev_ent)
	int	optflg;
	uid_t	uid;
	devalloc_t *dev_ent;
{
	devmap_t * dev_list;
	char	file_name[1024];
	struct stat stat_buf;
	char	*list;
	int	error = 0;

	strcpy(file_name, DAC_DIR);
	strcat(file_name, "/");
	strcat(file_name, dev_ent->da_devname);
#ifdef	SunOS_CMW
	aud_accume(DEVICE, file_name);
#else
	audit_allocate_device(file_name);			/* BSM */
#endif

	if (stat(file_name, &stat_buf)) {
#ifdef DEBUG
		fprintf(stderr, "Unable to stat %s\n", file_name);
		perror("Error:");
#endif
		return (DACACC);
	}

	if (stat_buf.st_uid != alloc_uid) {
		if (optflg & FORCE) {
			if (deallocate_dev(FORCE, dev_ent, uid)) {
#ifdef DEBUG
				fprintf(stderr,
					"Couldn't force deallocate device\n");
#endif
				return (CNTFRC);
			}
		} else if (stat_buf.st_uid == uid) {
			return (ALLOC);
		} else
			return (ALLOC_OTHER);
	}
	if ((stat_buf.st_mode & ~S_IFMT) == ALLOC_ERR_MODE)
		return (ALLOCERR);

	if (!strcmp(dev_ent->da_devauth, "*")) {
#ifdef DEBUG
		fprintf(stderr,
			"Device %s is not allocatable\n", dev_ent->da_devname);
#endif
		return (AUTHERR);
	}

	if (strcmp(dev_ent->da_devauth, "@")) {
		if (!isso() && (check_auth(uid, dev_ent->da_devauth) != 1)) {
#ifdef DEBUG
			fprintf(stderr, "User %d is unauthorized to allocate\n",
				uid);
#endif
			return (IMPORT_ERR);
		}
	}

	if ((dev_list = getdmapnam(dev_ent->da_devname)) == NULL) {
#ifdef DEBUG
		fprintf(stderr, "Unable to find %s in device map database\n",
			dev_ent->da_devname);
#endif
		return (NODMAPENT);
	}

	if (check_dev_range(dev_ent->da_devmin, dev_ent->da_devmax) == -1)
		return (DEVRNG);

	if ((list = (char *) malloc((unsigned) strlen(dev_list->dmap_devlist)
	    *sizeof (char) + 1)) == NULL)
		return (SYSERROR);

	strcpy(list, dev_list->dmap_devlist);
	if (check_devs(list) == -1) {
		set_alloc_err(file_name);
		free(list);
		return (DSPMISS);
	}

	/* All checks passed, time to lock and allocate */
	if (lock_dev(file_name) == -1) {
		free(list);
		return (DACLCK);
	}

	if ((error = mk_dac(file_name, uid)) != 0) {
		set_alloc_err(file_name);
		unlock_dev(file_name);
		free(list);
		return (error);
	}

	strcpy(list, dev_list->dmap_devlist);
	if (mk_alloc(list, uid) != 0) {
		set_alloc_err(file_name);
		strcpy(list, dev_list->dmap_devlist);
		mk_unalloc(list);
		unlock_dev(file_name);
		free(list);
		return (DEVLST);
	}

	free(list);
	return (0);
}







allocate(optflg, uid, device, label)
int	optflg;
int	uid;
char	*device, *label;
{
	devalloc_t   * dev_ent;
	devmap_t	 * dev_list;
#ifdef	SunOS_CMW
	char	*f_aud_event, *s_aud_event;
#endif	/* SunOS_CMW */
	int	error = 0;

#ifdef SunOS_CMW
	if (optflg & (USER | LABEL | FORCE) && !isso())
		return (NOTROOT);
#else
	if (optflg & (USER | FORCE) && !isso())
		return (NOTROOT);
#endif

	if (!(optflg & USER) && ((uid = getuid()) == -1))
		return (SYSERROR);

	if (allocate_uid() == (uid_t) -1)
		return (SYSERROR);

#ifdef SunOS_CMW
	if (get_curr_label() == -1)
		return (SYSERROR);

	if (set_alloc_label() != 0)
		return (SYSERROR);

	if (optflg & LABEL && (set_il(label) == -1))
		return (SETLABEL_PERR);

	if (optflg & FORCE) {
		s_aud_event = "AUE_realloc";
		f_aud_event = "AUE_realloc_fail";
	} else {
		s_aud_event = "AUE_alloc";
		f_aud_event = "AUE_alloc_fail";
	}
#endif


	setdaent();
	setdmapent();

#ifdef	SunOS_CMW
	aud_rec();
#endif	/* SunOS_CMW */

	if (!(optflg & TYPE)) {
		if ((dev_ent = getdanam(device)) == NULL) {
			if ((dev_list = getdmapdev(device)) == NULL)
				return (NODMAPENT);
			else if ((dev_ent = getdanam(dev_list->dmap_devname))
			    == NULL)
				return (NODAENT);
		}
		error = allocate_dev(optflg, uid, dev_ent);

#ifdef	SunOS_CMW
		if (error)
			aud_accume(EVENT, f_aud_event);
			else
			aud_accume(EVENT, s_aud_event);
		aud_accume(D_LABEL, cmw_plabel);
#endif	/* SunOS_CMW */

		return (error);
	}

	while ((dev_ent = getdatype(device)) != NULL) {
#ifdef DEBUG
		fprintf(stderr, "trying to allocate %s\n", dev_ent->da_devname);
#endif
		if (!allocate_dev(optflg, uid, dev_ent)) {
#ifdef	SunOS_CMW
			aud_accume(EVENT, s_aud_event);
			aud_accume(D_LABEL, cmw_plabel);
#endif	/* SunOS_CMW */
			return (0);
		}
#ifdef	SunOS_CMW
		else
	aud_clean();
#endif
	}
	enddaent();
#ifdef	SunOS_CMW
	aud_accume(EVENT, f_aud_event);
	aud_accume(D_LABEL, cmw_plabel);
	aud_accume(D_TYPE, device);
#endif	/* SunOS_CMW */
	return (NO_DEVICE);
}


deallocate_dev(optflg, dev_ent, uid)
int	optflg;
devalloc_t   *dev_ent;
int	uid;
{
	devmap_t * dev_list;
	char	file_name[1024];
	struct stat stat_buf;
	char	*list;
#ifdef SunOS_CMw
	bclabel_t flabel;
#endif
	int	error = 0, err = 0;

	strcpy(file_name, DAC_DIR);
	strcat(file_name, "/");
	strcat(file_name, dev_ent->da_devname);

#ifdef	SunOS_CMW
	aud_accume(DEVICE, file_name);
#else
	audit_allocate_device(file_name); /* BSM */
#endif
	if (stat(file_name, &stat_buf)) {
#ifdef DEBUG
		fprintf(stderr, "Unable to stat %s\n", file_name);
		perror("Error:");
#endif
		return (DACACC);
	}

	if (!(optflg & FORCE) && stat_buf.st_uid != uid && stat_buf.st_uid !=
	    alloc_uid) {
		return (NALLOCU);
	}

	if (stat_buf.st_uid == alloc_uid) {
		if ((stat_buf.st_mode & ~S_IFMT) == ALLOC_ERR_MODE) {
			if (!(optflg & FORCE))
				return (ALLOCERR);
		} else
			return (NALLOC);
	}

#ifdef	SunOS_CMW
	if (getcmwlabel(file_name, &flabel) == -1) {
#ifdef DEBUG
		fprintf(stderr, "list_device: unable to get cmw label, %s\n",
			file_name);
		perror("Error:");
#endif
		return (GETLABEL_PERR);
	}

	if (!isso() && !bldominates(bcltosl(&cmw_plabel), bcltosl(&flabel))) {
#ifdef DEBUG
		fprintf(stderr, "list_device: proc label doesn't ",
			"dominate file label, %s\n", file_name);
#endif
		return (MACERR);
	}
#endif	/* SunOS_CMW */


	if ((err = unmk_dac(file_name)) != 0) {
		set_alloc_err(file_name);
		EXIT(err);
	}

	if ((dev_list = getdmapnam(dev_ent->da_devname)) == NULL) {
#ifdef DEBUG
		fprintf(stderr, "Unable to find %s ", dev_ent->da_devname);
		fprintf(stderr, "in the device map database\n");
#endif
		EXIT(NODMAPENT);
	} else {
		if ((list = (char *) malloc(strlen(dev_list->dmap_devlist) *
				sizeof (char) + 1)) == NULL)
			EXIT(SYSERROR)
		else {
			strcpy(list, dev_list->dmap_devlist);
			if (mk_unalloc(list) != 0) {
				set_alloc_err(file_name);
				free(list);
				list = NULL;
				EXIT(DEVLST);
			}
		}
	}

	if (!error || optflg & FORCE_ALL)
		if ((err = unlock_dev(file_name)) != 0) {
			set_alloc_err(file_name);
			EXIT(err);
		}

	if (list != NULL)
		free(list);
	if (exec_clean(optflg, dev_ent->da_devname, dev_ent->da_devexec))
		EXIT(CLEAN_ERR);
	return (error);
}





deallocate(optflg, device)
int	optflg;
char	*device;
{
	DIR   * dev_dir;
	struct dirent *dac_file;
	devalloc_t   * dev_ent;
	devmap_t	 * dev_list;
	int	error = 0;
	int	uid;
	char	*f_aud_event, *s_aud_event;

	if (optflg & (FORCE | FORCE_ALL) && !isso())
		return (NOTROOT);
	if (optflg & FORCE_ALL)
		optflg |= FORCE;

	if (allocate_uid() == -1)
		return (SYSERROR);

#ifdef	SunOS_CMW
	if (get_curr_label() == -1)
		return (SYSERROR);

	if (set_alloc_label() != 0)
		return (SYSERROR);
#endif	/* SunOS_CMW */

	if (!(optflg & USER) && ((uid = getuid()) == (uid_t) -1))
		return (SYSERROR);

#ifdef	SunOS_CMW
	if (optflg & FORCE) {
		s_aud_event = "ACMW_fdealloc";
		f_aud_event = "ACMW_fdealloc_fail";
	} else {
		s_aud_event = "ACMW_dealloc";
		f_aud_event = "ACMW_dealloc_fail";
	}
#endif	/* SunOS_CMW */

	setdaent();
	setdmapent();

	if (!(optflg & FORCE_ALL)) {
		if ((dev_ent = getdanam(device)) == NULL) {
			if ((dev_list = getdmapdev(device)) == NULL)
				return (NODMAPENT);
			else if ((dev_ent = getdanam(dev_list->dmap_devname))
			    == NULL)
				return (NODAENT);
		}
#ifdef	SunOS_CMW
		aud_rec();
#endif	/* SunOS_CMW */
		error = deallocate_dev(optflg, dev_ent, uid);
#ifdef	SunOS_CMW
		if (error)
			aud_accume(EVENT, f_aud_event);
		else
			aud_accume(EVENT, s_aud_event);
#endif	/* SunOS_CMW */
		return (error);
	}

	if ((dev_dir = opendir(DAC_DIR)) == NULL) {

#ifdef DEBUG
		perror("Can't open DAC_DIR\n");
#endif
		return (DACACC);
	}

	while ((dac_file = readdir(dev_dir)) != NULL) {
		if (!strcmp(dac_file->d_name, ".") || !strcmp(dac_file->d_name,
				".."))
			continue;
		else {
			if ((dev_ent = getdanam(dac_file->d_name)) == NULL) {
				error = NODAENT;
				continue;
			}
#ifdef	SunOS_CMW
			aud_rec();
#endif	/* SunOS_CMW */
			error = deallocate_dev(optflg, dev_ent, uid);
#ifdef	SunOS_CMW
			if (error)
				aud_accume(EVENT, f_aud_event);
				else
				aud_accume(EVENT, s_aud_event);
#endif	/* SunOS_CMW */
		}
	}
	closedir(dev_dir);
	enddaent();
	return (error);
}
