#ident	"@(#)find.c 1.10 91/12/20"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include "cmds.h"

/*
 * the `fast-find' command.  Like "find / -name arg -print".
 */
void
find(host, arg, curdir, timestamp)
	char *host;
	char *arg;
	char *curdir;
	time_t timestamp;
{
#if 0
	term_start_output();
	descend(host, arg, DIR_ROOTBLK, timestamp, "/");
	term_finish_output();
#else
	/*
	 * package up args and credentials and let the DB
	 * server do the find on its end.  If we call `descend()'
	 * here, we're much too slow because of all the RPC and
	 * XDR overhead as we traverse a large hierarchy.
	 */
	(void) db_find(dbserv, host, arg, curdir, timestamp);
#endif
}

#if 0
void
descend(host, arg, blknum, timestamp, path)
	char *host;
	char *arg;
	u_long blknum;
	time_t timestamp;
	char *path;
{
	u_long startblk, thisblk;
	struct dir_block *bp, holdblock;
	struct dir_entry *ep;
	struct instance_record *irp;
	struct dnode dn;
	char fullpath[MAXPATHLEN];
	char termbuf[MAXPATHLEN];
	int inroot = 0;

	if (path[0] == '/' && path[1] == '\0')
		inroot = 1;

	startblk = thisblk = blknum;
	do {
		if ((bp = dir_getblock(thisblk)) == NULL_DIRBLK)
			break;
		bcopy(bp, &holdblock, DIR_BLKSIZE);
		for (ep = (struct dir_entry *)holdblock.db_data;
		    ep != DE_END(&holdblock);
		    ep = DE_NEXT(ep)) {
			if ((ep->de_name[0] == '.' && ep->de_name[1] == '\0') ||
					(ep->de_name[0] == '.' &&
					ep->de_name[1] == '.' &&
					ep->de_name[2] == '\0')) {
				continue;
			}
			if (getdnode(host, &dn, ep, VREAD,
					timestamp, LOOKUP_DEFAULT) == 0) {
				continue;
			}

			if (gmatch(ep->de_name, arg)) {
				if (inroot)
					(void) sprintf(termbuf, "/%s\n",
							ep->de_name);
				else
					(void) sprintf(termbuf, "%s/%s\n",
							path, ep->de_name);
				term_putline(termbuf);
			}

			if (ep->de_directory != NONEXISTENT_BLOCK &&
					S_ISDIR(dn.dn_mode) &&
					permchk(&dn, VEXEC, host) == 0) {
				if (inroot)
					(void) sprintf(fullpath, "/%s",
							ep->de_name);
				else
					(void) sprintf(fullpath, "%s/%s",
							path, ep->de_name);
				descend(host, arg, ep->de_directory,
						timestamp, fullpath);
			}
		}
		thisblk = holdblock.db_next;
	} while (thisblk != startblk);
}
#endif
