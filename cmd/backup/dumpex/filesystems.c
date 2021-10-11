#ident	"@(#)filesystems.c 1.12 93/04/28"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include <config.h>
#include <pwd.h>
#include <netdb.h>
#include <ulimit.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>

static char	**filesystems;
static int	maxfilesystems;

#define	getdtablesize()		ulimit(UL_GDESLIM, 0)

#define	GROWFS() \
	while (nfilesystems >= maxfilesystems) { \
		maxfilesystems += GROW; \
		filesystems = (char **)checkrealloc((char *)filesystems, \
			maxfilesystems * sizeof (char *)); \
	}

static int parse_df_line(char *, char *, void (*)());

/*
 * Add a single file system to the current list.  Returns
 * the new number of file systems in the list.
 */
void
addfs(filesystem)
	char	*filesystem;
{
	/*LINTED [alignment ok]*/
	GROWFS();
	filesystems[nfilesystems++] = strdup(filesystem);
}

/*
 * Probe a system for a list of its file systems using df.  Add
 * all local, non-tmp file systems to the current list of file
 * systems.  Returns the new number of file systems in the list.
 *
 * Here are prototypes of what this routine expects from df:
 *
 * Filesystem		 kbytes	   used	  avail	capacity  Mounted on
 * /dev/sd0a		  14508	   7619	   5439	   58%	  /
 * rmtc:/export/exec/sun4.sunos.4.1.1
 *			 425707	 196394	 186743	   51%	  /usr
 * /dev/sd1c		  95421	  39339	  46540	   46%	  /tmp
 * rmtc:/home/rmtc	2826177	2475170	  68390	   97%	  /home/rmtc
 *
 * System-V based systems give us lines like:
 *
 * /dev/dsk/c0t0d0s0	  29355	  18101	   3324	   68%	  /
 */
void
probefs(targethost, thishost, func)
	char	*targethost;
	char	*thishost;
	void	(*func)();
{
	int n, i, gotone, returncode;
	char *ruser;
	char **cmd;
	static char *dfcmds[] = {
		"",			/* Online: Backup 2.x path */
		"/usr/lib/fs/ufs/df",	/* Solaris 2.x path */
		"/bin/df -t 4.2",	/* SunOS 4.x path */
		0
	};
	char buf[MAXLINELEN];
	char *line;

	if (dfcmds[0][0] == '\0') {
		dfcmds[0] = malloc(strlen(gethsmpath(libdir)) +
		    strlen("/dumpdf") + 1);
		if (dfcmds[0] == NULL)
			dfcmds[0] = "";
		else
			(void) sprintf(dfcmds[0], "%s/dumpdf",
			    gethsmpath(libdir));
	}

	for (gotone = 0, cmd = dfcmds; *cmd && gotone == 0; cmd++) {
		returncode = NORETURNCODE;
		(void) gatherline(0, 0, (int *) NULL);
		parse_df_line((char *) NULL, (char *) NULL, (void (*)()) NULL);
		if (strcmp(thishost, targethost) != 0) {
			/* remote! */
			rhp_t rhp;

			printf(gettext("Running `%s' on %s\n"), *cmd,
			    targethost);
			(void) fflush(stdout);
			rhp = remote_setup(targethost, cf_rdevuser, *cmd, 1);
			if (rhp == NULL)
				continue;

			while ((n = remote_read(rhp, buf, sizeof (buf))) > 0) {
				for (i = 0; i < n; i++) {
					line = gatherline((int) buf[i], 0,
					    &returncode);
					if (line && parse_df_line(targethost,
					    line, func))
						gotone = 1;
				}
			}
			remote_shutdown(rhp);
		} else {
			/* local! */
			FILE *df;
			int c;

			df = popen(*cmd, "r");
			if (df == NULL)
				continue;
			setbuf(df, (char *) NULL);

			while ((c = getc(df)) != EOF) {
				line = gatherline(c, 0, &returncode);
				if (line && parse_df_line((char *) NULL,
				    line, func))
					gotone = 1;
			}
			(void) pclose(df);
		}
	}
}

static int
parse_df_line(char *host, char *line, void (*func)())
{
	enum estate {
		Header, Normal, Continuation
	};
	static enum estate nextline;
	char fs[MAXLINELEN];
	int field;

	if (line == NULL) {
		nextline = Header;	/* expect a header first */
		return (0);
	}

	if (nextline == Header) {
		nextline = Normal;	/* just eat the header */
		return (0);
	}

	chop(line);
	split(line, " \t");
	/*
	 * yields splitfields[] & nsplitfields
	 */
	if (nsplitfields == 1) {
		/*
		 * long filesystem name -- expect a continuation
		 */
		if (index(line, ':') != NULL /* NFS mounted */ ||
		    index(line, '/') == NULL /* swap? */) {
			nextline = Header; /* ignore next line */
			return (0);
		}
		nextline = Continuation;
		return (0);
	} else {
		if (index(splitfields[0], ':') != NULL /* NFS mounted */ ||
		    index(splitfields[0], '/') == NULL /* swap? */) {
			nextline = Normal; /* ignore this line */
			return (0);
		}
	}
	field = (nextline == Continuation) ? 4 : 5;
	nextline = Normal;

	/* ignore /tmp */
	if (strcmp(splitfields[field], "/tmp") == 0)
		return (0);

	if (host)
		(void) sprintf(fs, "%s:%s", host, splitfields[field]);
	else
		(void) strcpy(fs, splitfields[field]);
	func(fs);
	return (1);			/* gotone! */
}

char *
getfs(n)
	int	n;
{
	if (n > nfilesystems || nfilesystems == 0)
		return ((char *)0);
	return (filesystems[n]);
}
