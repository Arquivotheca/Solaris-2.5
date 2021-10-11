#ident	"@(#)isprocdir.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#define	TRUE	1
#define	FALSE	0

int
isprocdir(dir)	/* return TRUE iff dir is a PROC directory */
	const char *dir;	/* this is filthy */
{
	/*
	 * This is based on the fact that "/proc/<n>" and "/proc/0<n>"
	 * are the same file, namely process <n>.
	 */

	struct stat stat1;	/* dir/<pid>  */
	struct stat stat2;	/* dir/0<pid> */
	char path[200];
	register char * p;
	pid_t pid;

	/* make a copy of the directory name without trailing '/'s */
	if (dir == NULL)
		(void) strcpy(path, ".");
	else {
		(void) strncpy(path, dir, (int)sizeof(path)-4);
		path[sizeof(path)-4] = '\0';
		p = path + strlen(path);
		while (p > path && *--p == '/')
			*p = '\0';
		if (*path == '\0')
			(void) strcpy(path, ".");
	}

	pid = getpid();

	/* append "/<pid>" to the directory path and lstat() the file */
	p = path + strlen(path);
	(void) sprintf(p, "/%ld", pid);
	if (lstat(path, &stat1) != 0)
		return FALSE;

	/* append "/0<pid>" to the directory path and lstat() the file */
	(void) sprintf(p, "/0%ld", pid);
	if (lstat(path, &stat2) != 0)
		return FALSE;

	/* see if we ended up with the same file */
	if (stat1.st_dev   != stat2.st_dev
	 || stat1.st_ino   != stat2.st_ino
	 || stat1.st_mode  != stat2.st_mode
	 || stat1.st_nlink != stat2.st_nlink
	 || stat1.st_uid   != stat2.st_uid
	 || stat1.st_gid   != stat2.st_gid
	 || stat1.st_size  != stat2.st_size)
		return FALSE;

	return TRUE;
}
