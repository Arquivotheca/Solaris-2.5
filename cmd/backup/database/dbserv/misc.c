#ident	"@(#)misc.c 1.11 93/05/12"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#define	_POSIX_SOURCE	/* hack to avoid redef of MAXNAMLEN */
#define	_POSIX_C_SOURCE
#include <dirent.h>
#undef	_POSIX_C_SOURCE
#undef	_POSIX_SOURCE
#include "dboper.h"

#ifdef __STDC__
duplicate_dump(const char *host,
	const char *file)
#else
duplicate_dump(host, file)
	char *host;
	char *file;
#endif
{
	FILE *fp;
	DIR *dp;
	struct bu_header buh;
	struct dheader  new;
	struct dheader	old;
	struct dirent *de;
	char filename[256];
	char opermsg[MAXMSGLEN];
	int rc = 0;

	/*
	 * scan all headers for the given host.  If any of them match
	 * the header in the specified file, return 1 else return 0.
	 */
	if ((fp = fopen(file, "r")) == NULL) {
		(void) fprintf(stderr, gettext("%s: cannot open `%s'\n"),
			"duplicate_dump", file);
		return (-1);
	}
	if (fread((char *)&buh, sizeof (struct bu_header), 1, fp) != 1) {
		(void) fprintf(stderr, gettext(
			"%s: cannot read update header\n"), "duplicate_dump");
		(void) fclose(fp);
		return (-1);
	}
	if (fread((char *)&new, sizeof (struct dheader), 1, fp) != 1) {
		(void) fprintf(stderr, gettext(
			"%s: cannot read dump header\n"), "duplicate_dump");
		(void) fclose(fp);
		return (-1);
	}
	(void) fclose(fp);

	new.dh_ntapes = buh.tape_cnt;

	if ((dp = opendir(host)) == NULL) {
		(void) fprintf(stderr,
		    gettext("%s: cannot open direcotry `%s'\n"),
			"duplicate_dump", host);
		return (-1);
	}
	while (de = readdir(dp)) {
		if (strncmp(de->d_name, HEADERFILE, strlen(HEADERFILE)) == 0) {
			(void) sprintf(filename, "%s/%s", host, de->d_name);
			if ((fp = fopen(filename, "r")) == NULL) {
				(void) fprintf(stderr,
				    gettext("%s: cannot open `%s'\n"),
					"duplicate_dump", filename);
				continue;
			}
			if (fread((char *)&old,
					sizeof (struct dheader), 1, fp) != 1) {
				(void) fprintf(stderr,
				    gettext("%s: cannot fread\n"),
					"duplicate_dump");
				(void) fclose(fp);
				continue;
			}
			(void) fclose(fp);
			if (strcmp(old.dh_host, new.dh_host)) {
				continue;
#ifdef NOTNOW
/*
 * XXX: dump doesn't fill this in!
 */
			} else if (old.dh_netid != new.dh_netid) {
				continue;
#endif
			} else if (strcmp(old.dh_dev, new.dh_dev)) {
				continue;
			} else if (strcmp(old.dh_mnt, new.dh_mnt)) {
				continue;
			} else if (old.dh_time != new.dh_time) {
				continue;
			} else if (old.dh_level != new.dh_level) {
				continue;
			} else if (old.dh_position != new.dh_position) {
				continue;
			} else if (old.dh_ntapes != new.dh_ntapes) {
				continue;
			} else {
				rc = 1;
				(void) sprintf(opermsg, gettext(
					"Duplicate level %lu dump for %s:%s"),
					new.dh_level, new.dh_host, new.dh_mnt);
				(void) oper_send(DBOPER_TTL, LOG_WARNING,
					DBOPER_FLAGS, opermsg);
				(void) fprintf(stderr, "%s\n", opermsg);
				break;
			}
		}
	}
	(void) closedir(dp);
	return (rc);
}

char *
lctime(timep)
	time_t	*timep;
{
	static char buf[256];
	struct tm *tm;

	tm = localtime(timep);
	(void) strftime(buf, sizeof (buf), "%c\n", tm);
	return (buf);
}
