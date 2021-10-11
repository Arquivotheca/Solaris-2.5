/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)printreq.c	1.18	94/04/11 SMI"	/* SVr4.0 1.4	*/

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "lp.h"			/* includes fcntl.h sys/types.h sys/stat.h */
#include "lpNet.h"
#include "requests.h"
#include "printers.h"
#include "secure.h"
#include "lpd.h"

static	int	  response(void);
static	char	* mkcfile(char *, REQUEST *);
static	char	* r_print_request(int, char *, int);
static	int	  card(char **, char, char *);
static	int	  sysv_card(char **, char, char *);
static	int	  parselpd(char *, char *, char **);
static	int	  sendfile(char, char *, char *, int, uid_t, gid_t);

static char *
fixup_name (name, where)
  char *	name;
  char *	where;
{
	static char	ret_buf[256];
	static int	Lp_Tmp_len;

	if (!Lp_Tmp_len)
		Lp_Tmp_len = strlen (Lp_Tmp)+1;

	if (*name == '/')
		name += Lp_Tmp_len;

	sprintf (ret_buf, "%s/%s/%s", Lp_NetTmp, where, name);

	return ret_buf;
}

/*
 * Send print request to remote lpd
 * (S_PRINT_REQUEST processing)
 */
char *
s_print_request(char *msg)
{
	REQUEST	 *rp;
	SECURE	 *sp = NULL;
	char	 *rqfile;
	char	 *cf = NULL;
	char	 *num;
	char	**lpnm, lpdnm[MAX_LPD_SPFN_SZ];
	char	 *jobid;
	short	  status;
	int	  n;
	uid_t	orig_uid;
	gid_t	orig_gid;

        (void)getmessage(msg, S_PRINT_REQUEST, &rqfile);
	logit(LOG_DEBUG, "S_PRINT_REQUEST(rqfile=\"%s\")", rqfile);
	if (!(rp = getrequest(fixup_name(rqfile, "tmp")))
	 || !(sp = getsecure(fixup_name(rqfile, "requests")))) {
		status = MNOOPEN;
		goto out;
	}
	Printer = rp->destination;
	if (!(cf = mkcfile(sp->req_id, rp))) {
		status = MNOMEM;
		goto out;
	}
	if (!snd_lpd_msg(RECVJOB, Printer)) {	
		status = REPRINT;
		goto out;
	}
	switch (response()) {	
	case 0:
		break;
	case -1:
		status = REPRINT;
		goto out;
	default:
		status = MERRDEST;
		goto out;
	}
	jobid = rid2jid(sp->req_id);
	for (n=0, lpnm = rp->file_list; n<MAX_LPD_FILES && *lpnm; n++, lpnm++) {
		sprintf(lpdnm, "df%c%s%s", LPD_FILEID(n), jobid, Lhost);
		if ((status = sendfile(READDFILE, fixup_name (*lpnm, "tmp"),
				lpdnm, READ_FILE, sp->uid, sp->gid)) != MOK)
			goto out;
	}
	sprintf(lpdnm, "%s%s%s", CFPREFIX, jobid, Lhost);
	status = sendfile(READCFILE, cf, lpdnm, READ_BUF, 0, 0);
	closeRemote();
	if (openRemote())
		(void)snd_lpd_msg(PRINTJOB, Printer);

	/* Nuke them here for now */
	for (n=0, lpnm = rp->file_list; n<MAX_LPD_FILES && *lpnm; n++, lpnm++) {

		unlink (fixup_name (*lpnm, "tmp"));
	}
	unlink (fixup_name (rqfile, "tmp"));	/* nuke request file */
	unlink (fixup_name (rqfile, "requests"));/* nuke secure file */

out:
	/* Have to nuke the files */
	/* ? Do I nuke the files if there was an error (svChild doesn't) */
	freerequest(rp);
	if (cf)
		free(cf);
	if (status == REPRINT)
		cf = (char *)NULL;
	else
		cf = r_print_request(status, (sp ? sp->req_id : ""), 0);
	freesecure(sp);
	return(cf);
}

static 
struct fmt_map fmt_map[] =  {
	OTROFF,		FTROFF,
	TROFF,		FDITROFF,
	TEX,		FDVI,
	PLOT,		FGRAPH,
	RASTER,		FRASTER,
	CIF,		FCIF,
	FORTRAN,	FFORTRAN,
	SIMPLE,		FFRMT,
	POSTSCRIPT,	FFRMT,
	"PS",           FFRMT,  /* fix for sunsoft bugid 1101456        */

	"",		FFRMT,
	NULL,		'\0'
};

static char *
mkcfile(char *reqid, REQUEST *rp)
{
	int	 	  n, nfiles;
	int		  i;
	char	 	  fmt = '\0';
	char		 *cp;
	char		 *cf = NULL;
	char		 *rval = NULL;
	char		 *flist;
	char		 *lpdargs;
	char		 *files[MAX_LPD_FILES];
	char		 *num;
	char 		 *name;
	char		 *fonts[4];	/* troff fonts */
	char		**options, **modes;
	char		 *argv[sizeof(LPDOPTS)];
	struct fmt_map	 *fmap;

/* for readability...  (be careful nesting in if-else) */
#define CARD(k,s)	if (!card(&cf, k, s)) goto out
#define SYSV_CARD(k,s)	if (!sysv_card(&cf, k, s)) goto out

	options = dashos(rp->options);
	modes = dashos(rp->modes);
	CARD(HOST, Lhost);
	CARD(PERSON, ((name=strrchr(rp->user,'!')) ? ++name : rp->user));

	/* BSD_PROTOCOL extension added for SYSV functionality support */
	if ((strcmp(SIP->extensions, "SVR4-EXTENSIONS") == 0) ||
	    (strcmp(SIP->extensions, "SVR4-Extensions") == 0) ||
	    (strcmp(SIP->extensions, "svr4-extensions") == 0) ||
	    (strcmp(SIP->extensions, "svr4-Extensions") == 0)) {
		if (rp->form) 
			SYSV_CARD(SYSV_FORM, rp->form);
		if (rp->actions) {
			if (rp->actions & ACT_IMMEDIATE == ACT_IMMEDIATE)
		  	SYSV_CARD(SYSV_HANDLING, NAME_IMMEDIATE);
			else if (rp->actions & ACT_HOLD == ACT_HOLD)
  		  	SYSV_CARD(SYSV_HANDLING, NAME_HOLD);
			else if (rp->actions & ACT_RESUME == ACT_RESUME)
  		  	SYSV_CARD(SYSV_HANDLING, NAME_RESUME);
		}
		if (rp->pages) 
			SYSV_CARD(SYSV_PAGES, rp->pages);
		if (rp->priority != 20) { /* default priority */
			char buf[64];
			sprintf(buf, "%d", rp->priority);
			SYSV_CARD(SYSV_PRIORITY, buf);
		}
		if (rp->charset) 
			SYSV_CARD(SYSV_CHARSET, rp->charset);
		if (rp->input_type) {
			int flag = 0;
	
			for (fmap = fmt_map; !flag && fmap->type; fmap++) 
				if (STREQU(rp->input_type, fmap->type))
					flag++;
			if (flag == 0) {
				SYSV_CARD(SYSV_TYPE, rp->input_type);
				rp->input_type = strdup(NAME_SIMPLE);
			}
		}
		if (rp->modes) 
			SYSV_CARD(SYSV_MODE, rp->modes);
		if (rp->options &&
	    	(strstr(rp->options, LPDFLD) == NULL) &&
	    	(strstr(rp->options, FLIST) == NULL)) 
			CARD(LP_OPTIONS, rp->options);
		if (rp->alert) {
	  		char buf[128];
	  		sprintf(buf, "generic:%s@%s", rp->user, Lhost);
	  		SYSV_CARD(SYSV_NOTIFICATION, buf);
		}
	}
	nfiles = lenlist(rp->file_list);
	nfiles = MIN(nfiles, MAX_LPD_FILES);
	if ((flist = find_listfld(FLIST, options)) &&
	    parseflist(flist+STRSIZE(FLIST), nfiles, files, NULL) != nfiles)
		goto out;
	cp = NULL;
	if (lpdargs = find_listfld(LPDFLD, options)) {
		parselpd(lpdargs+STRSIZE(LPDFLD), LPDOPTS, argv);
		if (argv[JOB_IDX])
			cp = argv[JOB_IDX];
	}
	if (!cp)
		if (!lpdargs && rp->title)	/* only use title if from lp */
			cp = rp->title;
		else if (flist)
			if (*files[0])
				cp = basename(files[0]);
			else
				cp = "stdin";
		else
			cp = reqid;
	name = cp;
	CARD(JOBNAME, cp);
	if (lpdargs && argv[CLASS_IDX]) {
		CARD(CLASS, argv[CLASS_IDX]);
	} else
		CARD(CLASS, Lhost);
	if (!find_listfld(NOBANNER, options))
		CARD(LITERAL, rp->user);
	if (cp = find_listfld(IDENT, modes))
		CARD(INDENT, cp+STRSIZE(IDENT));
	if (rp->actions & ACT_MAIL)
		CARD(MAILUSER, rp->user);
	if (cp = find_listfld(WIDTHFLD, options))
		CARD(WIDTH, cp+STRSIZE(WIDTHFLD));
	if (cp = find_listfld(PRTITLE, modes)) {
		rmesc(cp += STRSIZE(PRTITLE));
		fmt = FPR;
	   	CARD(TITLE, cp);
	}
	for (fmap = fmt_map; !fmt && fmap->type; fmap++)
		if (STREQU(rp->input_type, fmap->type))
			fmt = fmap->keyc;
	if (fmt == FFRMT && 
	    find_listfld(CATVFILTER, modes) &&
	    find_listfld(NOFILEBREAK, options))
		fmt = FFRMTCC;
	else if (!fmt)
		goto out;
	if (lpdargs && (fmt == FTROFF || fmt == FDITROFF || fmt == FDVI)) {
		if (argv[FONT1_IDX] && *argv[FONT1_IDX])
			CARD(FONTR, argv[FONT1_IDX]);
		if (argv[FONT2_IDX] && *argv[FONT2_IDX])
			CARD(FONTI, argv[FONT2_IDX]);
		if (argv[FONT3_IDX] && *argv[FONT3_IDX])
			CARD(FONTB, argv[FONT3_IDX]);
		if (argv[FONT4_IDX] && *argv[FONT4_IDX])
			CARD(FONTS, argv[FONT4_IDX]);
	}
	sprintf(Buf, "%sA%s%s", DFPREFIX, rid2jid(reqid), Lhost);
	for (n = 0; n < nfiles; n++) {
		LPD_FILEX(Buf) = LPD_FILEID(n);
		for (i = rp->copies; i; i--)
			CARD(fmt, Buf);
		CARD(UNLINK, Buf);
		if (flist) {
			CARD(FILENAME, *files[n] ? files[n] :"standard input");
		} else if (name) {
			CARD(FILENAME, name);
		} else {
			CARD(FILENAME, NO_FILENAME);
		}
	}
	rval = cf;
	cf = NULL;
out:
	if (options) freelist(options);
	if (modes) freelist(modes);
	if (cf) free(cf);
	return(rval);
}

static
sendfile(char type, char *buf, char *lpdnm, int flag, uid_t uid, gid_t gid)
{
	int		error;
	size_t		size;
	int		fd = -1;
	int		nrw;
	struct stat	stb;

	if (flag == READ_FILE) {	/* use file as input */
		uid_t	orig_uid;
		gid_t	orig_gid;
		char	*src_path;
		char	new_path[MAXPATHLEN];

		orig_gid = getegid();
		orig_uid = geteuid();

		switch(rstat(buf, new_path, &stb, uid, gid)) {
		case -1:
			logit(LOG_WARNING, "can't open spool file: %s", buf);
			seteuid(orig_uid);
			setegid(orig_gid);
			return(MUNKNOWN);

		case 0:
			src_path = buf;
			break;

		case 1:
			src_path = new_path;
		break;
		}

		if ((fd = open(src_path, O_RDONLY)) == -1) {
			logit(LOG_WARNING,
				"can't open spool file: %s", src_path);
			return(MUNKNOWN);
		}

		seteuid(orig_uid);
		setegid(orig_gid);

		size = stb.st_size;
		buf = Buf;
	} else				/* READ_BUF: use buffer as input */
		size = strlen(buf);
	if (!snd_lpd_msg(RECVJOB_2NDARY, type, size, lpdnm))
		return(REPRINT);
	if (response() != 0)		/* try harder ??? (sendfile) */
		return(REPRINT);
	error = 0;
	for (; size > 0; size -= nrw) {
		nrw = MIN(size, BUFSIZ);
		if (flag == READ_FILE) {
			if (!error && read(fd, buf, nrw) != nrw)
				error = errno ? errno : -1;
		}
		if (TLIWrite(CIP->fd, buf, nrw) != nrw) {
			if (fd >= 0) 
				close(fd);	/* close spool file */
			logit(LOG_INFO, "lost connection");
			return(REPRINT);
		}
		if (flag == READ_BUF)
			buf += BUFSIZ;
	}
	if (fd >= 0) 
		close(fd);			/* close spool file */
	if (error) {
		NAK1();
		logit(LOG_WARNING, "spool file read error (%d)", error);
		return(REPRINT);
	}
	if (!ACK_SENT() || response())
		return(REPRINT);
	return(MOK);
}

/*
 * Return network response character
 */
static int
response(void)
{
	char	resp;

	logit( LOG_DEBUG, "response() - reading response from remote");

	if (TLIRead(CIP->fd, &resp, 1) != 1) {
		logit(LOG_INFO, "%s: lost connnection", Printer);
		return(-1);
	} else
		if (resp)
			logit(LOG_INFO, "NAKed by remote lpd (%d)", resp);
		return(resp);
}

static
parselpd(char *argp, char *opts, char **argv)
{
	char	*p;

	for (p = opts; *p; p++)
		argv[p-opts] = NULL;
	for (argp = getitem(argp, '-'); argp; argp = getitem(NULL, '-')) {
		for (p = opts; *p; p++)
			if (argp[0] == *p) {
				argv[p-opts] = &argp[1];
				break;
			}
	}
}

static
card(char **pbuf, char key, char *string)
{
	int	 	 n;
	static char	*buf;		/* current buffer	  */
	static int	 pcur;		/* current buffer pointer */
	static int	 bufsize;	/* current buffer size	  */

	n = strlen(string) + 3;		/* key + string + \n + NULL */
	if (!*pbuf) {
		if (!(buf = (char *)malloc(bufsize = CFSIZE_INIT)))
			return(0); 
		pcur = 0;
	} else
		if (pcur + n > bufsize) {
			if (!(buf = 
				(char *)realloc(buf, bufsize += CFSIZE_INC))) {
				free(*pbuf);		/* no looking back */
				*pbuf = NULL;
				return(0);
			}
		logit(LOG_DEBUG, "realloc(0x%x, %d) = 0x%x", buf, bufsize, *pbuf);
		}
	*pbuf = buf;
	logit(LOG_DEBUG, "cf card: %c%s", key, string);
	sprintf(&buf[pcur], "%c%s\n", key, string);
	pcur += n-1;			/* position to overwrite NULL byte */
	return(1);
}

static
sysv_card(char **pbuf, char key, char *string)
{
	char buf[BUFSIZ];

	sprintf(buf, "%c%s", key, string);
	return(card(pbuf, '5', buf));
}

static char *
r_print_request(int status, char *reqid, int chkbits)
{
	logit(LOG_DEBUG, "R_PRINT_REQUEST(%d, \"%s\")", status, reqid);
	if (putmessage(Msg, R_PRINT_REQUEST, status, reqid, chkbits) < 0)
		return(NULL);
	else
		return(Msg);
}
