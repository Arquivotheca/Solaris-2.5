/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)putjob.c	1.13	94/06/03 SMI"	/* SVr4.0 1.4	*/

#include "unistd.h"
#include "stdlib.h"
#include "limits.h"
#include "string.h"
#include <libgen.h>

#include "lpsched.h"

static void
makelink(char *src, char *dst, uid_t uid, gid_t gid)
{
	ENTRY ("makelink")

	uid_t		orig_uid;
	gid_t		orig_gid;
	struct stat	stbuf;

	(void)Unlink (dst);
	if (Access (src, R_OK) != 0) {
		orig_uid = geteuid();
		orig_gid = getegid();

		/* if these fail, the stat will probably fail also */
		setegid(gid);
		seteuid(uid);
		/* if can't read the path or can't read the file */
		if (Stat(src, &stbuf) == -1 || ! (stbuf.st_mode & 0444))
			src = "/dev/null";

		seteuid(orig_uid);
		setegid(orig_gid);
	}
	else if (uid == 0) {
		/*
		 * Root printed the job and access succeeded, however, this
		 * still doesn't mean that lpNet can read the file if it is NFS
		 * mounted and the mode is rr-.  In this case, the seteuid
		 * workaround will fail since we set back to root.  Since there
		 * is no good way to check if this is an NFS mounted file,
		 * check if we can read the file and if not, use the /dev/null
		 * workaround.
		 */

		int fd;
		char buff[2];

		if ((fd = open(src, O_RDONLY)) == -1)
			src = "/dev/null";
		else {
			if (read(fd, buff, 1) == -1)
				src = "/dev/null";
			close(fd);
		}
	}

	if (Symlink(src, dst) == -1) {
		char *fullpath, *tpath, *rpath, buf[PATH_MAX];
		struct stat sbuf;

		fullpath = strdup(dst);
		tpath = dirname(fullpath);
		if (Stat(tpath, &sbuf) == 0 || errno != ENOENT)
			fail("symlink(%s,%s) failed (%s)\n", src, dst, PERROR);

		if (mkdir(tpath, MODE_NODIR))
			note("mkdir failed (%s): %s\n", tpath, PERROR);
		rpath = basename(tpath);
		sprintf(buf, "%s/requests/%s", Lp_NetTmp, rpath);
		if (mkdir(buf, MODE_NODIR))
			note("mkdir failed (%s): %s\n", buf, PERROR);

		if (Symlink(src, dst) == -1)
			fail("symlink(%s,%s) failed (%s)\n", src, dst, PERROR);

		Free(fullpath);
	}

	return;
}

/**
 ** putjobfiles()
 **/

void 
#if	defined(__STDC__)
putjobfiles (
	RSTATUS *		prs
)
#else
putjobfiles (prs)
	RSTATUS *		prs;
#endif
{
	ENTRY ("putjobfiles")

	char **			listp;
	char **			flist;

	char *			reqno;
	char *			basename;
	char *			src_fdf		= 0;
	char *			src_fdf_no;
	char *			dst_df;
	char *			dst_df_no;
	char *			bogus;
	char *			bogus_no;
	char *			rfile;

	int			count;

	RSTATUS			rs;

	REQUEST			rtmp;

	SECURE			stmp;


	/*
	 * WARNING! DON'T FREE PREVIOUSLY ALLOCATED POINTERS WHEN
	 * REPLACING THEM WITH NEW VALUES IN rs.secure AND rs.request,
	 * AS THE ORIGINAL POINTERS ARE STILL IN USE IN THE ORIGINAL
	 * COPIES OF THESE STRUCTURES.
	 */

	rs = *(prs);
	rtmp = *(prs->request);
	rs.request = &rtmp;
	stmp = *(prs->secure);
	rs.secure = &stmp;

	reqno = getreqno(rs.secure->req_id);


	/*
	 * Link the user's data files into the network temporary
	 * directory, and construct a new file-list for a copy
	 * of the request-file.
	 */

	if (rs.request->outcome & RS_FILTERED) {
		basename = makestr("F", reqno, "-", MOST_FILES_S, (char *)0);
		src_fdf = makepath(Lp_Tmp, rs.secure->system, basename, (char *)0);
		src_fdf_no = strchr(src_fdf, 0) - STRSIZE(MOST_FILES_S);
		Free (basename);
	}

	basename = makestr(reqno, "-", MOST_FILES_S, (char *)0);
	dst_df = makepath(Lp_NetTmp, "tmp", rs.secure->system, basename, (char *)0);
	dst_df_no = strchr(dst_df, 0) - STRSIZE(MOST_FILES_S);
	bogus = makepath(Lp_Tmp, rs.secure->system, basename, (char *)0);
	bogus_no = strchr(bogus, 0) - STRSIZE(MOST_FILES_S);
	Free (basename);

	count = 0;
	flist = 0;
	for (listp = rs.request->file_list; *listp; listp++) {
		char *			src_df;

		count++;

		/*
		 * Link the next data file to a name in the
		 * network temporary directory.
		 */
		sprintf (dst_df_no, "%d", count);
		if (rs.request->outcome & RS_FILTERED) {
			sprintf (src_fdf_no, "%d", count);
			src_df = src_fdf;
		} else
			src_df = *listp;
		makelink(src_df, dst_df, rs.secure->uid, rs.secure->gid);

		/*
		 * Add this name to the list we'll put in the
		 * request file. Note: The prefix of this name
		 * is bogus; the "lpNet" daemon will replace it
		 * with the real prefix.
		 */
		sprintf (bogus_no, "%d", count);
		appendlist (&flist, bogus);
	}

	if (src_fdf)
		Free (src_fdf);
	Free (dst_df);
	Free (bogus);


	/*
	 * Change the content of the request and secure files,
	 * to reflect how they should be seen on the remote side.
	 */
	if (rs.request->alert)
		rs.request->alert = 0;
	rs.request->actions &= ~(ACT_WRITE|ACT_MAIL);
	rs.request->actions |= ACT_NOTIFY;
	rs.request->file_list = flist;
	rs.request->destination = Strdup(rs.printer->remote_name);
	if (prs->output_type)
		rs.request->input_type = prs->output_type;
	if (strchr(rs.secure->user, BANG_C))
		rs.secure->user = Strdup(rs.secure->user);
	else
		rs.secure->user = makestr(Local_System, BANG_S, rs.secure->user, (char *)0);

	/*
	 * Copy the request and secure files to the network temporary
	 * directory.
	 */

	basename = makestr(reqno, "-0", (char *)0);

	rfile = makepath(Lp_NetTmp, "tmp", rs.secure->system, basename, (char *)0);
	if (putrequest(rfile, rs.request) == -1)
		fail ("putrequest(%s,...) failed (%s)\n", rfile, PERROR);

	Free (rfile);

	rfile = makepath(Lp_NetTmp, "requests", rs.secure->system, basename, (char *)0);
	/* fix for bugid 1103890. user Putsecure() instead of putsecure() */
	if (Putsecure(rfile, rs.secure) == -1)
		fail ("Putsecure(%s,...) failed (%s)\n", rfile, PERROR);
	Free (rfile);

	Free (basename);


	freelist (rs.request->file_list);
	Free (rs.request->destination);
	Free (rs.secure->user);

	return;
}
