/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sm_statd.c	1.22	95/09/07 SMI"	/* SVr4.0 1.2	*/
/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *  		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986-1989,1994  Sun Microsystems, Inc.
 *  	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *			All rights reserved.
 */
/*
 * sm_statd.c consists of routines used for the intermediate
 * statd implementation(3.2 rpc.statd);
 * it creates an entry in "current" directory for each site that it monitors;
 * after crash and recovery, it moves all entries in "current"
 * to "backup" directory, and notifies the corresponding statd of its recovery.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/param.h>
#include <dirent.h>
#include <rpc/rpc.h>
#include <rpcsvc/sm_inter.h>
#include <errno.h>
#include <memory.h>
#include <signal.h>
#include "sm_statd.h"

#define	MAXPGSIZE 8192
#define	SM_INIT_ALARM 15
#define	SM_RPC_TIMEOUT	15
#define	SM_MAX_RPC_ERRS	5
extern int debug;
extern int errno;
extern char STATE[MAXHOSTNAMELEN], CURRENT[MAXHOSTNAMELEN];
extern char BACKUP[MAXHOSTNAMELEN];
extern char STATD_HOME[MAXHOSTNAMELEN];
extern char *strcpy(), *strcat();

extern char host_name[MAXIPADDRS][MAXHOSTNAMELEN]; /* store -a opts */
extern char path_name[MAXPATHS][MAXPATHLEN];   /* store -p opts */
extern int addrix;
extern int pathix;

extern int local_state;

int LOCAL_STATE;

struct name_entry {
	char *name;
	int count;
	int rpc_errs;
	struct name_entry *prev;
	struct name_entry *nxt;
};
typedef struct name_entry name_entry;

name_entry *find_name();
name_entry *insert_name();
name_entry *record_q;
name_entry *recovery_q;

static void delete_name(name_entry **namepp, char *name);
static void remove_name(char *name, char *queue);
static int statd_call_statd(char *name);

void
sm_notify(ntfp)
	stat_chge *ntfp;
{
	if (debug)
		printf("sm_notify: %s state =%d\n", ntfp->mon_name,
		    ntfp->state);
	send_notice(ntfp->mon_name, ntfp->state);
}

/*
 * called when statd first comes up; it searches /etc/sm to gather
 * all entries to notify its own failure
 */
void
statd_init()
{
	int cc;
	struct dirent *dirp;
	DIR 	*dp;
	char from[MAXPATHLEN], to[MAXPATHLEN], path[MAXPATHLEN];
	FILE *fp, *fp_tmp;
	int i, tmp_state;
	char state_file[MAXPATHLEN];

	if (debug)
		printf("enter statd_init\n");

	/* initialize the data structures used for the client handle cache */
	init_lru();
	init_hash();

	if ((mkdir(STATD_HOME, SM_DIRECTORY_MODE)) == -1) {
		if (errno != EEXIST) {
			syslog(LOG_ERR, "statd: mkdir current %m\n");
			return;
		}
	}
	/*
	 *	First try to open the file.  If that fails, try to create it.
	 *	If that fails, give up.
	 */
	if ((fp = fopen(STATE, "r+")) == (FILE *)NULL)
		if ((fp = fopen(STATE, "w+")) == (FILE *)NULL) {
			syslog(LOG_ERR, "statd: fopen(stat file) error\n");
			exit(1);
		} else
			(void) chmod(STATE, 0644);
	if (fseek(fp, 0, 0) == -1) {
		syslog(LOG_ERR, "statd: fseek failed\n");
		exit(1);
	}
	if ((cc = fscanf(fp, "%d", &LOCAL_STATE)) == EOF) {
		if (debug >= 2)
			printf("empty file\n");
		LOCAL_STATE = 0;
	}

	/*
	 * Scan alternate paths for largest "state" number
	 */
	for (i = 0; i < pathix; i++) {
		sprintf(state_file, "%s/statmon/state", path_name[i]);
		if ((fp_tmp = fopen(state_file, "r+")) == (FILE *)NULL) {
			if ((fp_tmp = fopen(state_file, "w+"))
				== (FILE *)NULL) {
				if (debug)
				    syslog(LOG_ERR,
					"statd: %s: fopen failed\n",
					state_file);
				continue;
			} else
				(void) chmod(state_file, 0644);
		}
		if (fseek(fp_tmp, 0, 0) == -1) {
			if (debug)
			    syslog(LOG_ERR,
				"statd: %s: fseek failed\n", state_file);
			(void) fclose(fp_tmp);
			continue;
		}
		if ((cc = fscanf(fp_tmp, "%d", &tmp_state)) == EOF) {
			if (debug)
			    syslog(LOG_ERR,
				"statd: %s: file empty\n", state_file);
			(void) fclose(fp_tmp);
			continue;
		}
		if (tmp_state > LOCAL_STATE) {
			LOCAL_STATE = tmp_state;
			if (debug)
				printf("Update LOCAL STATE: %d\n", tmp_state);
		}
		(void) fclose(fp_tmp);
	}

	LOCAL_STATE = ((LOCAL_STATE%2) == 0) ? LOCAL_STATE+1 : LOCAL_STATE+2;

	/* Copy the LOCAL_STATE value back to all stat files */
	if (fseek(fp, 0, 0) == -1) {
		syslog(LOG_ERR, "statd: fseek failed\n");
		exit(1);
	}
	fprintf(fp, "%d", LOCAL_STATE);
	(void) fflush(fp);
	if (fsync(fileno(fp)) == -1) {
		syslog(LOG_ERR, "statd: fsync failed\n");
		exit(1);
	}
	(void) fclose(fp);

	for (i = 0; i < pathix; i++) {
		sprintf(state_file, "%s/statmon/state", path_name[i]);
		if ((fp_tmp = fopen(state_file, "r+")) == (FILE *)NULL) {
			if ((fp_tmp = fopen(state_file, "w+"))
				== (FILE *)NULL) {
				syslog(LOG_ERR,
				    "statd: %s: fopen failed\n", state_file);
				continue;
			} else
				(void) chmod(state_file, 0644);
		}
		if (fseek(fp_tmp, 0, 0) == -1) {
			syslog(LOG_ERR, "statd: %s: seek failed\n", state_file);
			(void) fclose(fp_tmp);
			continue;
		}
		fprintf(fp_tmp, "%d", LOCAL_STATE);
		(void) fflush(fp_tmp);
		if (fsync(fileno(fp_tmp)) == -1) {
			syslog(LOG_ERR,
			    "statd: %s: fsync failed\n", state_file);
			(void) fclose(fp_tmp);
			exit(1);
		}
		(void) fclose(fp_tmp);
	}

	if (debug)
		printf("local state = %d\n", LOCAL_STATE);

	if ((mkdir(CURRENT, SM_DIRECTORY_MODE)) == -1) {
		if (errno != EEXIST) {
			syslog(LOG_ERR, "statd: mkdir current, error %m\n");
			exit(1);
		}
	}
	if ((mkdir(BACKUP, SM_DIRECTORY_MODE)) == -1) {
		if (errno != EEXIST) {
			syslog(LOG_ERR, "statd: mkdir backup, error %m\n");
			exit(1);
		}
	}

	/* get all entries in CURRENT into BACKUP */
	if ((dp = opendir(CURRENT)) == (DIR *)NULL) {
		syslog(LOG_ERR, "statd: open current directory, error %m\n");
		exit(1);
	}
	for (dirp = readdir(dp); dirp != (struct dirent *)NULL;
		dirp = readdir(dp)) {
		if (debug)
			printf("d_name = %s\n", dirp->d_name);
		if (strcmp(dirp->d_name, ".") != 0 &&
			strcmp(dirp->d_name, "..") != 0) {
		/* rename all entries from CURRENT to BACKUP */
			(void) strcpy(from, CURRENT);
			(void) strcpy(to, BACKUP);
			(void) strcat(from, "/");
			(void) strcat(to, "/");
			(void) strcat(from, dirp->d_name);
			(void) strcat(to, dirp->d_name);
			if (rename(from, to) == -1) {
				syslog(LOG_ERR, "statd: rename, error %m\n");
			} else {
				if (debug >= 2)
					printf("rename: %s to %s\n", from, to);
			}
		}
	}
	closedir(dp);

	/* get all entries in BACKUP into recovery_q */
	if ((dp = opendir(BACKUP)) == (DIR *)NULL) {
		syslog(LOG_ERR, "statd: open backup directory, error %m\n");
		exit(1);
	}
	for (dirp = readdir(dp); dirp != (struct dirent *)NULL;
		dirp = readdir(dp)) {
		if (strcmp(dirp->d_name, ".") != 0 &&
			strcmp(dirp->d_name, "..") != 0) {
		/* get all entries from BACKUP to recovery_q */
			if (statd_call_statd(dirp->d_name) != 0) {
				(void) insert_name(&recovery_q, dirp->d_name);
			} else { /* remove from BACKUP directory */
				(void) strcpy(path, BACKUP);
				(void) strcat(path, "/");
				(void) strcat(path, dirp->d_name);
				if (debug >= 2)
					printf("remove monitor entry %s\n",
					    path);
				if (unlink(path) == -1) {
					syslog(LOG_ERR,
					    "statd: %s, error %m", path);
				}
			}
		}
	}
	closedir(dp);

	/* notify statd */
	if (recovery_q != (name_entry *)NULL)
		(void) alarm(SM_INIT_ALARM);
}

static int
statd_call_statd(name)
	char *name;
{
	stat_chge ntf;
	int err;
	int i;
	int rc = 0;

	ntf.mon_name = hostname;
	ntf.state = LOCAL_STATE;
	if (debug)
		printf("statd_call_statd at %s\n", name);
	err = call_rpc(name, 0, SM_PROG, SM_VERS, SM_NOTIFY,
		xdr_stat_chge, &ntf, xdr_void, NULL, 1,
		SM_RPC_TIMEOUT);
	if (err != (int) RPC_SUCCESS) {
		syslog(LOG_WARNING,
			"statd: cannot talk to statd at %s, error %d\n",
			name, err);
		rc = -1;
	}

	ntf.state = LOCAL_STATE;
	for (i = 0; i < addrix; i++) {
		ntf.mon_name = host_name[i];
		if (debug)
			printf("statd_call_statd at %s\n", name);
		err = call_rpc(name, 0, SM_PROG, SM_VERS, SM_NOTIFY,
			xdr_stat_chge, &ntf, xdr_void, NULL, 1,
			SM_RPC_TIMEOUT);
		if (err != (int) RPC_SUCCESS) {
			syslog(LOG_WARNING,
			    "statd: cannot talk to statd at %s, error %d\n",
			    name, err);
			rc = -1;
		}
	}

	return (rc);
}

/* ARGSUSED */
void
sm_try(signum)
	int signum;
{
	name_entry *nl, *next;

	(void) signal(SIGALRM, sm_try);
	if (debug >= 2)
		printf("enter sm_try: recovery_q = %s\n", recovery_q->name);
	next = recovery_q;
	while ((nl = next) != (name_entry *)NULL) {
		next = next->nxt;
		if (statd_call_statd(nl->name) == 0) {
			/* remove entry from recovery_q */
			delete_name(&recovery_q, nl->name);
			remove_name(nl->name, BACKUP);
		} else {
			if (nl->rpc_errs == 0) {
			    syslog(LOG_WARNING,
				"statd: host %s is not responding\n",
				nl->name);
			}
			if (++nl->rpc_errs >= SM_MAX_RPC_ERRS) {
			    /* give up, we can't seem to get through */
			    syslog(LOG_ERR,
				"statd: host %s failed to respond\n",
				nl->name);
				if (debug >= 2)
				    printf("sm_try bailing out on %s\n",
				    nl->name);
				delete_name(&recovery_q, nl->name);
				remove_name(nl->name, BACKUP);
			}
		}
	}
	if (recovery_q != (name_entry *)NULL)
		(void) alarm(SM_INIT_ALARM);
}

char *
xmalloc(len)
	unsigned len;
{
	char *new;

	if ((new = malloc(len)) == 0) {
		syslog(LOG_ERR, "statd: malloc, error %m\n");
		return ((char *)NULL);
	} else {
		(void) memset(new, 0, len);
		return (new);
	}
}

/*
 * the following two routines are very similar to
 * insert_mon and delete_mon in sm_proc.c, except the structture
 * is different
 */
name_entry *
insert_name(namepp, name)
	name_entry **namepp;
	char *name;
{
	name_entry *new;

	new = (name_entry *) xmalloc(sizeof (struct name_entry));
	new->name = xmalloc(strlen(name) + 1);
	(void) strcpy(new->name, name);
	new->nxt = *namepp;
	if (new->nxt != (name_entry *)NULL)
		new->nxt->prev = new;
	new->rpc_errs = 0;
	*namepp = new;
	return (new);
}

static void
delete_name(namepp, name)
	name_entry **namepp;
	char *name;
{
	name_entry *nl;

	nl = *namepp;
	while (nl != (name_entry *)NULL) {
		if (strncmp(nl->name, name, strcspn(name, ".")) == 0) {
			if (nl->prev != (name_entry *)NULL)
				nl->prev->nxt = nl->nxt;
			else
				*namepp = nl->nxt;
			if (nl->nxt != (name_entry *)NULL)
				nl->nxt->prev = nl->prev;
			free(nl->name);
			free(nl);
			return;
		}
		nl = nl->nxt;
	}
}

name_entry *
find_name(namep, name)
	name_entry *namep;
	char *name;
{
	name_entry *nl;

	nl = namep;
	while (nl != (name_entry *)NULL) {
		if (strncmp(nl->name, name, strcspn(name, ".")) == 0) {
			return (nl);
		}
		nl = nl->nxt;
	}
	return ((name_entry *)NULL);
}

int
create_file(name)
	char *name;
{
	int fd;

	if (debug >= 2)
		printf("create monitor entry %s\n", name);
	if ((fd = open(name, O_CREAT, 00200)) == -1) {
		syslog(LOG_ERR, "statd: open of %s, error %m\n", name);
		if (errno != EACCES)
			return (1);
	} else {
		if (debug >= 2)
			printf("%s is created\n", name);
		if (close(fd)) {
			syslog(LOG_ERR, "statd: close, error %m\n");
			return (1);
		}
	}
	return (0);
}

int
delete_file(name)
	char *name;
{
	if (debug >= 2)
		printf("remove monitor entry %s\n", name);
	if (unlink(name) == -1) {
		syslog(LOG_ERR, "statd: unlink of %s, error %m", name);
		return (1);
	}
	return (0);
}

/*
 * remove the name from the specified queue
 */
static void
remove_name(char *name, char *queue)
{
	char
		path[MAXPATHLEN];

	(void) strcpy(path, queue);
	(void) strcat(path, "/");
	(void) strcat(path, name);
	(void) delete_file(path);
}

/*
 * Manage the cache of hostnames.  An entry for each host that has recently
 * locked a file is kept.  There is an in-ram queue (record_q) and an empty
 * file in the file system name space (/var/statmon/sm/<name>).  This
 * routine adds (deletes) the name to (from) the in-ram queue and the entry
 * to (from) the file system name space.
 *
 * If op == 1 then the name is added to the queue otherwise the name is
 * deleted.
 */
void
record_name(name, op)
	char *name;
	int op;
{
	name_entry *nl;
	int i;
	char path[MAXPATHLEN];

	if (op == 1) { /* insert */
		if ((nl = find_name(record_q, name)) == (name_entry *)NULL) {
			nl = insert_name(&record_q, name);
			/* make an entry in current directory */
			(void) strcpy(path, CURRENT);
			(void) strcat(path, "/");
			(void) strcat(path, name);
			(void) create_file(path);

			/* make an entry in alternate paths */
			for (i = 0; i < pathix; i++) {
				(void) strcpy(path, path_name[i]);
				(void) strcat(path, "/statmon/sm/");
				(void) strcat(path, name);
				(void) create_file(path);
			}
		}
		nl->count++;
	} else { /* delete */
		if ((nl = find_name(record_q, name)) == (name_entry *)NULL) {
			return;
		}
		nl->count--;
		if (nl->count == 0) {
			delete_name(&record_q, name);
			/* remove this entry from current directory */
			remove_name(name, CURRENT);

			/* remove this entry from alternate paths */
			for (i = 0; i < pathix; i++) {
				(void) strcpy(path, path_name[i]);
				(void) strcat(path, "/statmon/sm/");
				(void) strcat(path, name);
				delete_file(path);
			}
		}
	}
}

void
sm_crash()
{
	name_entry *nl, *next;

	if (record_q == (name_entry *)NULL)
		return;
	next = record_q;	/* clean up record queue */
	while ((nl = next) != (name_entry *)NULL) {
		next = next->nxt;
		delete_name(&record_q, nl->name);
	}

	if (recovery_q != (name_entry *)NULL) {
		/* clean up all onging recovery act */
		if (debug)
			printf("sm_crash clean up\n");
		(void) alarm(0);
		next = recovery_q;
		while ((nl = next) != (name_entry *)NULL) {
			next = next ->nxt;
			delete_name(&recovery_q, nl->name);
		}
	}
	statd_init();
}
