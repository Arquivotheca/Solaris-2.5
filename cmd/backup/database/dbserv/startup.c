#ident	"@(#)startup.c 1.20 93/10/05"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*
 * database server startup functionality.  The idea here is
 * to cleanup/recover from database updates which failed part
 * way through because of a crash of the server or the machine
 * on which it runs.
 */
#include <config.h>
#include "defs.h"
#define	_POSIX_SOURCE   /* hack to avoid redef of MAXNAMLEN */
#define	_POSIX_C_SOURCE
#include <dirent.h>
#undef	_POSIX_C_SOURCE
#undef	_POSIX_SOURCE
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <database/backupdb.h>
#include <database/activetape.h>
#include "dboper.h"

static	char	temp_update[sizeof (TEMP_PREFIX)+sizeof (UPDATE_FILE)+1];
static	char	temp_dnode[sizeof (TEMP_PREFIX)+sizeof (DNODEFILE)+1];
static	char	temp_path[sizeof (TEMP_PREFIX)+sizeof (PATHFILE)+1];
static	char	temp_header[sizeof (TEMP_PREFIX)+sizeof (HEADERFILE)+1];
static	char	temp_links[sizeof (TEMP_PREFIX)+sizeof (LINKFILE)+1];
static	char	dir_transfile[sizeof (DIRFILE)+sizeof (TRANS_SUFFIX)+1];
static	char	inst_transfile[sizeof (INSTANCEFILE)+sizeof (TRANS_SUFFIX)+1];
static	char	tape_transfile[sizeof (TAPEFILE)+sizeof (TRANS_SUFFIX)+1];

#ifdef __STDC__
static void checkroot(void (*)());
static int check_tape_update(DIR *);
static int readdeletefile(int *, char *);
static int readrenamefile(char *, u_long *, char *, u_long *);
static void check_name_ip(const char *);
static void check_partial(const char *);
static void check_new(const char *);
static int update_inprogress(const char *, const char *);
static int update_done(const char *, const char *);
static int rename_temp(const char *, const char *);
static void full_update(const char *, const char *);
static void init_names(void);
static void fail(void);
#else
static void checkroot();
static int check_tape_update();
static int readdeletefile();
static int readrenamefile();
static void check_name_ip();
static void check_partial();
static void check_new();
static int update_inprogress();
static int update_done();
static int rename_temp();
static void full_update();
static void init_names();
static void fail();
#endif

static void
#ifdef __STDC__
init_names(void)
#else
init_names()
#endif
{
	(void) sprintf(temp_update, "%s%s", TEMP_PREFIX, UPDATE_FILE);
	(void) sprintf(temp_dnode, "%s%s", TEMP_PREFIX, DNODEFILE);
	(void) sprintf(temp_path, "%s%s", TEMP_PREFIX, PATHFILE);
	(void) sprintf(temp_header, "%s%s", TEMP_PREFIX, HEADERFILE);
	(void) sprintf(temp_links, "%s%s", TEMP_PREFIX, LINKFILE);
	(void) sprintf(dir_transfile, "%s%s", DIRFILE, TRANS_SUFFIX);
	(void) sprintf(inst_transfile, "%s%s", INSTANCEFILE, TRANS_SUFFIX);
	(void) sprintf(tape_transfile, "%s%s", TAPEFILE, TRANS_SUFFIX);
}

/*
 * call this when the database server starts up.
 * This cleans up any updates that were in process when we went down,
 * and processes any additional update files that were waiting.
 */
void
#ifdef __STDC__
startup(void)
#else
startup()
#endif
{
	int pid, status;
	struct sigvec vec, ovec;

	/*
	 * our current directory is the root of the database.
	 * We need to visit the directories of each host looking
	 * for partial updates.
	 * We look for files whose names begin with "T.",
	 * files whose names begin with "batch_update", and
	 * files whose names begin with "update."
	 */
	init_names();

	/*
	 * updates are always done in a subprocess since they are
	 * so memory intensive -- we cannot let a long-running process
	 * such as this one continue to consume large chunks of swap
	 * space...
	 */
	vec.sv_handler = SIG_IGN;
#ifdef USG
	vec.sa_flags = SA_RESTART;
	(void) sigemptyset(&vec.sa_mask);
#else
	vec.sv_flags = 0;
	vec.sv_mask = 0;
#endif
	/*
	 * register with syslog().  we don't have to use LOG_NOWAIT because
	 * we're ignoring SIGCHLD for now.
	 */
	closelog();
	openlog(myname, LOG_CONS, LOG_DAEMON);
	(void) sigvec(SIGCHLD, &vec, &ovec);
	for (;;) {
		startupreg(NULL); /* init shared memory */
		if ((pid = fork()) == -1) {
			perror("fork");
			(void) oper_send(DBOPER_TTL, LOG_WARNING,
			    DBOPER_FLAGS, gettext(
			    "Cannot start database server -- unable to fork"));
			exit(1);
		} else if (pid == 0) {
			yp_unbind(mydomain);
			closefiles();
			(void) oper_init(opserver, myname, 0);
			checkroot(check_name_ip);
			checkroot(check_partial);
			checkroot(check_new);
			oper_end();
			startupreg(NULL);
			exit(0);
		} else {
			if (waitpid(pid, &status, 0) == -1) {
				perror("waitpid");
				exit(1);
			}
			if ((WEXITSTATUS(status) != 0) ||
			    (WTERMSIG(status) != 0)) {
				if (startupunlink() == 0)
					continue;
				oper_send(DBOPER_TTL, LOG_WARNING,
				    DBOPER_FLAGS,
				    gettext("Cannot perform database startup"));
				(void) fprintf(stderr, gettext(
					"Cannot perform database startup\n"));
				oper_end();
				exit(1);
			}
		}
		break;
	}
	(void) sigvec(SIGCHLD, &ovec, (struct sigvec *)NULL);
	/* now that we catch SIGCHLD, syslog() must use LOG_NOWAIT */
	closelog();
	openlog(myname, LOG_CONS | LOG_NOWAIT, LOG_DAEMON);
}

/*
 * run this before every update to insure that prior updates completed
 * successfully.  This is important as we depend on updates completing
 * before any subsequent updates are attempted.
 */
void
#ifdef __STDC__
cleanup(void)
#else
cleanup()
#endif
{
	init_names();
	(void) startupunlink();
	checkroot(check_partial);
	checkroot(check_new);
}

static void
#ifdef __STDC__
checkroot(void (*func)())
#else
checkroot(func)
	void (*func)();
#endif
{
	DIR *dp;
	struct dirent *de;
	struct stat stbuf;

	if ((dp = opendir(".")) == NULL) {
		(void) fprintf(stderr,
			gettext("%s: cannot open `.'\n"), "startup");
		fail();
	}
	(void) check_tape_update(dp);
	while (de = readdir(dp)) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		if (stat(de->d_name, &stbuf) == -1) {
			(void) fprintf(stderr, gettext(
				"cannot stat `%s'\n"), de->d_name);
			continue;
		}
		if (!S_ISDIR(stbuf.st_mode))
			continue;
		(*func)(de->d_name);
	}
	(void) closedir(dp);
}

static int
check_tape_update(dp)
	DIR *dp;
{
	struct dirent *de;
	int fd;
	int filesize;
	int gotdelete = 0, gottrans = 0;

	while (de = readdir(dp)) {
		if (strncmp(de->d_name, TAPE_UPDATE,
				strlen(TAPE_UPDATE)) == 0) {
			if ((fd = open(de->d_name, O_RDONLY)) == -1) {
				perror(de->d_name);
				(void) fprintf(stderr,
				    gettext("%s: cannot open %s\n"),
					"startup", de->d_name);
				fail();
			}
			if (read(fd, (char *)&filesize,
						sizeof (int)) != sizeof (int)) {
				perror("read");
				(void) fprintf(stderr,
				    gettext("%s: cannot read %s\n"),
					"startup", de->d_name);
				(void) close(fd);
				fail();
			}
			(void) close(fd);
			if (truncate(TAPEFILE, (off_t)filesize)) {
				(void) fprintf(stderr,
				    gettext("%s: cannot truncate %s\n"),
					"startup", TAPEFILE);
				fail();
			}
			if (strcmp(de->d_name, TAPE_UPDATE) == 0) {
				/*
				 * update processing did not complete.
				 * Caller will have to resubmit the
				 * request.
				 */
				(void) unlink(tape_transfile);
				(void) unlink(de->d_name);
			} else if (strcmp(de->d_name, TAPE_UPDATEDONE) == 0) {
				if (tape_trans(".") != 0) {
					fail();
				}
				(void) unlink(TAPE_UPDATEDONE);
			}
			break;
		} else if (strcmp(de->d_name, DELETE_TAPE) == 0) {
			gotdelete = 1;
		} else if (strcmp(de->d_name, tape_transfile) == 0) {
			gottrans = 1;
		}
	}
	rewinddir(dp);
	if (!gotdelete && !gottrans) {
		return (0);
	} else if (gottrans && !gotdelete) {
		if (tape_trans(".") != 0) {
			fail();
		}
		return (0);
	}

	/*
	 * an incomplete delete
	 */
	(void) unlink(tape_transfile);

	if (gotdelete) {
		char label[LBLSIZE];

		if (readdeletefile(&filesize, label))
			fail();
		if (truncate(TAPEFILE, (off_t)filesize) == -1) {
			if (errno != ENOENT || filesize) {
				perror("truncate");
				(void) fprintf(stderr,
				    gettext("%s: cannot truncate %s\n"),
					"startup", TAPEFILE);
				(void) unlink(DELETE_TAPE);
				fail();
			}
		}
		return (delete_tape(label));
	}
	return (0);
}

static int
readdeletefile(filesize, label)
	int *filesize;
	char *label;
{
	int fd;

	if ((fd = open(DELETE_TAPE, O_RDONLY)) == -1) {
		perror(DELETE_TAPE);
		(void) fprintf(stderr, gettext("%s: cannot open `%s'\n"),
			"check_tape_update", DELETE_TAPE);
		(void) unlink(DELETE_TAPE);
		return (-1);
	}
	if (read(fd, (char *)filesize, sizeof (int)) != sizeof (int)) {
		perror("read");
		(void) fprintf(stderr, gettext("%s: cannot read `%s'\n"),
			"check_tape_update", DELETE_TAPE);
		(void) close(fd);
		(void) unlink(DELETE_TAPE);
		return (-1);
	}
	if (read(fd, label, LBLSIZE) != LBLSIZE) {
		perror("read");
		(void) fprintf(stderr, gettext("%s: cannot read `%s'\n"),
			"check_tape_update", DELETE_TAPE);
		(void) close(fd);
		(void) unlink(DELETE_TAPE);
		return (-1);
	}
	(void) close(fd);
	return (0);
}

static void
check_name_ip(const char *name)
{
	char namebuf[MAXNAMELEN], *ipstring;
	struct hostent *hostent;
	u_long ipaddr;
	int i;

	(void) strcpy(namebuf, name);
	if ((ipstring = strchr(namebuf, '.')) == NULL)
		return; /* error? */
	*(ipstring++) = '\0';
	ipaddr = inet_addr(ipstring);

	hostent = gethostbyname(namebuf);
	if (hostent == NULL) {
		syslog(LOG_WARNING,
		    gettext("Cannot resolve hostname for %s"), name);
		return;
	}
	for (i = 0; ((u_long *)hostent->h_addr_list[i]) != NULL; i++)
		if (*((u_long *)hostent->h_addr_list[i]) == ipaddr)
			break;
	if (((u_long *)hostent->h_addr_list[i]) == NULL) {
		syslog(LOG_WARNING,
		    gettext("Host name does not resolve to ip address: %s"),
		    name);
		return;
	}
}

static void
#ifdef __STDC__
check_partial(const char *name)
#else
check_partial(name)
	char *name;
#endif
{
	DIR *dp;
	struct dirent *de;
	char fullname[256];
	int gotone = 0;
	register char *p;

	if ((dp = opendir(name)) == NULL) {
		(void) fprintf(stderr, gettext(
			"%s: cannot open directory `%s'\n"), "startup", name);
		return;
	}
	while (de = readdir(dp)) {
		/*
		 * on our first pass, we check for `update.inprogress' or
		 * `update.done'.  We assume that there is at most one of
		 * these since we lock the database for updates.  Note
		 * that we cannot remove any temp files yet since
		 * `update.done' will rename these and use them.
		 */
		if (strncmp(UPDATE_INPROGRESS, de->d_name,
					strlen(UPDATE_INPROGRESS)) == 0) {
			if (gotone) {
				(void) fprintf(stderr, gettext(
					"simultaneous updates!?!?!?\n"));
			}
			gotone++;
			if (update_inprogress(name, de->d_name))
				fail();
		}

		if (strncmp(UPDATE_DONE, de->d_name,
				strlen(UPDATE_DONE)) == 0) {
			/*
			 * rename any "T." files to get rid of the
			 * prefix and perform dir_trans() and
			 * inst_trans().
			 */
			if (gotone) {
				(void) fprintf(stderr, gettext(
					"simultaneous updates!?!?!?\n"));
			}
			gotone++;
			if (update_done(name, de->d_name))
				fail();
			break;
		}
	}

	/*
	 * on this pass we get rid of any temp files which still remain
	 * after update.done processing is completed.
	 * We "should" never find any...
	 */
	rewinddir(dp);
	while (de = readdir(dp)) {
		if (strncmp(de->d_name, TEMP_PREFIX,
				strlen(TEMP_PREFIX)) == 0) {
			(void) sprintf(fullname, "%s/%s", name, de->d_name);
			(void) unlink(fullname);
		} else if (p = strstr(de->d_name, TRANS_SUFFIX)) {
			if (strcmp(p, TRANS_SUFFIX) == 0) {
				(void) sprintf(fullname, "%s/%s",
					name, de->d_name);
				(void) unlink(fullname);
			}
		} else if (p = strstr(de->d_name, MAP_SUFFIX)) {
			if (strcmp(p, MAP_SUFFIX) == 0) {
				(void) sprintf(fullname, "%s/%s",
					name, de->d_name);
				(void) unlink(fullname);
			}
		}
	}

	(void) closedir(dp);
}

static void
#ifdef __STDC__
check_new(const char *name)
#else
check_new(name)
	char *name;
#endif
{
	DIR *dp;
	struct dirent *de;
	char fullname[256];

	/*
	 * scan for any complete `batch_update' files
	 * which we still need to process.  Is the order of them
	 * important??
	 */
	if ((dp = opendir(name)) == NULL) {
		(void) fprintf(stderr, gettext(
			"startup cannot opendir `%s'\n"), name);
		return;
	}
	while (de = readdir(dp)) {
		if (strncmp(UPDATE_FILE, de->d_name,
		    strlen(UPDATE_FILE)) == 0) {
			full_update(name, de->d_name);
		}
	}

	(void) closedir(dp);
}

static void
#ifdef __STDC__
full_update(const char *host, const char *file)
#else
full_update(host, file)
	char *host;
	char *file;
#endif
{
	char fullname[MAXPATHLEN];

	(void) sprintf(fullname, "%s/%s", host, file);
	if (!duplicate_dump(host, fullname)) {
		if (batch_update(host, fullname)) {
			(void) fprintf(stderr, "batch_update!\n");
			fail();
		}
	} else {
		/*
		 * XXX: oper stuff here?
		 */
		(void) fprintf(stderr, gettext("duplicate dump discarded\n"));
	}
	(void) unlink(fullname);
}

/*
 * found a `update.inprogress' in the given directory
 *
 * remove all "T." files
 * and all ".trans" files.
 * Then locate the "batch_update.dumpid" file
 * which started all this madness and perform
 * the update for it again.
 */
static int
#ifdef __STDC__
update_inprogress(const char *host, const char *name)
#else
update_inprogress(host, name)
	char *host;
	char *name;
#endif
{
	char filename[256];
	char namemap[256];
	char opermsg[MAXMSGLEN];
	struct dirent *de;
	int fd;
	char *p;
	u_long dumpid;
	DIR *dp;

	/*
	 * scan the directory looking for temporary files.  We
	 * remove any that begin with "T." and any that end
	 * with ".trans" or ".map"
	 */
	if ((dp = opendir(host)) == NULL) {
		(void) fprintf(stderr, gettext("cannot %s %s\n"),
			"opendir", host);
		return (-1);
	}
	while (de = readdir(dp)) {
		if (strncmp(de->d_name, TEMP_PREFIX,
				strlen(TEMP_PREFIX)) == 0) {
			(void) sprintf(filename, "%s/%s", host, de->d_name);
			(void) unlink(filename);
			continue;
		}
		p = strstr(de->d_name, TRANS_SUFFIX);
		if (p && strcmp(p, TRANS_SUFFIX) == 0) {
			(void) sprintf(filename, "%s/%s", host, de->d_name);
			(void) unlink(filename);
			continue;
		}
		p = strstr(de->d_name, MAP_SUFFIX);
		if (p && strcmp(p, MAP_SUFFIX) == 0) {
			(void) sprintf(filename, "%s/%s", host, de->d_name);
			(void) unlink(filename);
			continue;
		}
	}
	closedir(dp);
	(void) sprintf(filename, "%s/%s", host, name);
	(void) unlink(filename);

	(void) strcpy(namemap, UPDATE_INPROGRESS);
	(void) strcat(namemap, ".%d");
	if (sscanf(name, namemap, &dumpid) != 1) {
		(void) fprintf(stderr, gettext(
			"%s: cannot get dumpid\n"), "update_inprogress");
		return (-1);
	}
	(void) sprintf(filename, "%s.%d", UPDATE_FILE, dumpid);
	(void) sprintf(opermsg, gettext("Re-trying update file %s/%s"),
		host, filename);
	oper_send(DBOPER_TTL, LOG_NOTICE, DBOPER_FLAGS, opermsg);
	(void) fprintf(stderr, "%s\n", opermsg);
	full_update(host, filename);
	return (0);
}

/*
 * here we found a `update.done' file.  This indicates that we have all
 * the data necessary to complete a update operation without re-processing
 * a batch_udpate file.
 */
static int
#ifdef __STDC__
update_done(const char *host, const char *name)
#else
update_done(host, name)
	char *host;
	char *name;
#endif
{
	struct dirent *de;
	int dnode_cnt, path_cnt, header_cnt, link_cnt;
	char filename[256];
	int fd;
	u_long dumpid;
	DIR *dp;

	(void) strcpy(filename, UPDATE_DONE);
	(void) strcat(filename, ".%d");
	if (sscanf(name, filename, &dumpid) != 1) {
		(void) fprintf(stderr, gettext(
			"%s: cannot parse filename `%s'\n"),
			"update_done", name);
		return (-1);
	}

	/*
	 * for `dir' `instance', and `activetape', we check to see
	 * if a transaction file exists.
	 * If so, we truncate back to the length specified
	 * in `update.done' and apply the transactions.  If not, we assume
	 * that transaction processing had already completed and leave
	 * the file as is.
	 *
	 * XXX: do we need to verify dumpid's on dnode and friends or
	 * can we assume that DB locking will only allow the ones
	 * we're expecting?
	 */
	if ((dp = opendir(host)) == NULL) {
		(void) fprintf(stderr, gettext("%s: cannot %s %s\n"),
			"update_done", "opendir", host);
		return (-1);
	}
	dnode_cnt = path_cnt = header_cnt = link_cnt = 0;
	while (de = readdir(dp)) {
		if (strncmp(de->d_name, temp_dnode, strlen(temp_dnode)) == 0) {
			if (dnode_cnt++) {
				(void) fprintf(stderr,
					gettext("too many temp dnodes!\n"));
				continue;
			}
			if (rename_temp(host, de->d_name)) {
				closedir(dp);
				return (-1);
			}
		} else if (strncmp(de->d_name, temp_path,
					strlen(temp_path)) == 0) {
			if (path_cnt++) {
				(void) fprintf(stderr, gettext(
					"too many temp path components!\n"));
				continue;
			}
			if (rename_temp(host, de->d_name)) {
				closedir(dp);
				return (-1);
			}
		} else if (strncmp(de->d_name, temp_header,
					strlen(temp_header)) == 0) {
			if (header_cnt++) {
				(void) fprintf(stderr,
					gettext("too many temp headers!\n"));
				continue;
			}
			if (rename_temp(host, de->d_name)) {
				closedir(dp);
				return (-1);
			}
		} else if (strncmp(de->d_name, temp_links,
					strlen(temp_links)) == 0) {
			if (link_cnt++) {
				(void) fprintf(stderr,
					gettext("too many temp linkfiles!\n"));
				continue;
			}
			if (rename_temp(host, de->d_name)) {
				closedir(dp);
				return (-1);
			}
		} else if (strcmp(de->d_name, dir_transfile) == 0) {
			if (dir_trans(host) != 0) {
				closedir(dp);
				return (-1);
			}
		} else if (strcmp(de->d_name, inst_transfile) == 0) {
			if (instance_trans(host) != 0) {
				closedir(dp);
				return (-1);
			}
		} else if (strcmp(de->d_name, tape_transfile) == 0) {
			if (tape_trans(host) != 0) {
				closedir(dp);
				return (-1);
			}
		}
	}
	closedir(dp);
	(void) sprintf(filename, "%s/%s", host, name);
	(void) unlink(filename);
	(void) sprintf(filename, "%s/%s.%lu", host, UPDATE_FILE, dumpid);
	(void) unlink(filename);
	return (0);
}

static int
#ifdef __STDC__
rename_temp(const char *host,
	const char *name)
#else
rename_temp(host, name)
	char *host;
	char *name;
#endif
{
	char oldname[256], newname[256];

	(void) sprintf(oldname, "%s/%s", host, name);
	(void) sprintf(newname, "%s/%s", host, &name[2]);
	if (rename(oldname, newname) == -1) {
		perror("rename");
		(void) fprintf(stderr,
			gettext("cannot rename(%s,%s)\n"), oldname, newname);
		return (-1);
	}
	return (0);
}

static void
fail()
{
	char opermsg[MAXMSGLEN];

	/*
	 * XXX: failure msg to syslog too...
	 */
	(void) sprintf(opermsg, gettext("Database startup failed"));
	oper_send(DBOPER_TTL, LOG_NOTICE, DBOPER_FLAGS, opermsg);
	(void) fprintf(stderr, "%s\n", opermsg);
	oper_end();
	exit(1);
}
