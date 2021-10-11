/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)recvjob.c	1.21	94/05/06 SMI"	/* SVr4.0 1.5	*/

#include <sys/types.h>
#include <sys/resource.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <ctype.h>
#include "lp.h"			/* includes sys/stat.h fcntl.h */
#define	WHO_AM_I	I_AM_OZ
#include "oam_def.h"
#include "requests.h"
#include "secure.h"
#include "printers.h"
#include "lpNet.h"
#include "lpd.h"

#include <locale.h>

static char	Spoolfn[MAX_SV_SPFN_SZ]; /* name of last spool file created */
static char	*Jobname;
static int	Jobsize;

static	REQUEST	* mkrequest(char *, size_t, int);
static	char	* cvt_spoolfn(char *, int, int);
static	int	chksize(char *, size_t);
static	void	frecverr(char *, ...);
static	int	noresponse(void);
static	void	print_request(REQUEST *, char *);
static	int	qdisabled(void);
static	int	read2file(char *, size_t);
static	int	read2mem(char *, size_t);
static	void	smail(char *, long, ...);
static	void	catch(int);
static	void	rcleanup(void);
static	void	trashrequest(REQUEST *);
static	int	new_req_id(char *);

static	void	(*alrmhdlr)();

/*
 * Recieve print job from remote lpd
 */
void
recvjob(void)
{
	register char	*cp;
	char		buf[BUFSIZ];
	char		*blank;
	REQUEST		*rqp;
	size_t		size;
	char		*cfile = NULL;
	size_t		cfile_size = 0;
	char		*spool_file;
	struct stat	stb;
	int req_id = -1;
	int old_request = -1;
	enum { NONE, CF, DF } set_in = NONE;

	(void) sigset(SIGTERM, catch);
	(void) sigset(SIGPIPE, catch);
	alrmhdlr = sigset(SIGALRM, catch);

	if (!isprinter(Printer) && !isclass(Printer)) {
		frecverr("unknown printer %s", Printer);
		/* NOTREACHED */
	}
	if (qdisabled()) {
		NAK1();
		logit(LOG_INFO, "request for disabled printer: %s", Printer);
		done(1);
		/* NOTREACHED */
	}
	if (chdir(Lp_Requests) < 0 ||
	    stat(Rhost, &stb) < 0 &&
		(mkdir_lpdir(Rhost, MODE_DIR) < 0 || stat(Rhost, &stb) < 0) ||
	    (stb.st_mode & S_IFMT) != S_IFDIR) {
		frecverr("%s: %s/%s", Printer, Lp_Requests, Rhost);
		/* NOTREACHED */
	}
	if (chdir(Lp_Tmp) < 0 ||
	    stat(Rhost, &stb) < 0 &&
		(mkdir_lpdir(Rhost, MODE_DIR) < 0 || stat(Rhost, &stb) < 0) ||
	    (stb.st_mode & S_IFMT) != S_IFDIR) {
		frecverr("%s: %s/%s", Printer, Lp_Tmp, Rhost);
		/* NOTREACHED */
	}
	ACK();
	for (;;) {
		/*
		 * Read a command to tell us what to do
		 */

		cp = buf;	/* Msg; */
				/* borrow Msg buffer to read 2ndary request */
		do {
			int	nr;

			if ((nr = TLIRead(CIP->fd, cp, 1)) != 1) {
				if (nr < 0) {
					frecverr("%s: Lost connection",
								Printer);
					/* NOTREACHED */
				} else {
					if (!(rqp = mkrequest(cfile,
							cfile_size,
							old_request))) {
						frecverr(
"request creation failed (cf: %lu bytes)", cfile_size);
						/* NOTREACHED */
					}
					print_request(rqp, spool_file);
					trashrequest(rqp);
					free(spool_file);
					free(cfile);
				}
				return;
			}
		} while (*cp++ != '\n');
		*--cp = '\0';
		cp = buf; /* Msg; */
		logit(LOG_DEBUG, "received lpd message: %d%s", *cp, cp+1);
		switch (*cp++) {

		case CLEANUP:	/* cleanup because data sent was bad */
			rcleanup();
			continue;

		case READCFILE:	/* read cf file */
			if (set_in == NONE)
				set_in = CF;
			if (cfile_size != 0) {
					if (!(rqp = mkrequest(cfile, cfile_size,
								old_request))) {
						frecverr(
"request creation failed (cf: %lu bytes)",
							cfile_size);
						/* NOTREACHED */
					}
					print_request(rqp, spool_file);
					trashrequest(rqp);
					free(spool_file);
					free(cfile);
					cfile = NULL;
					spool_file = NULL;
					cfile_size = 0;
					if (set_in == CF)
						req_id = -1;
			}

			cfile_size = strtol(cp, &blank, 10);
			if (cp = blank, *cp++ != ' ')
				break;
			if (!(cfile = (char *)malloc(cfile_size+1))) {
				NAK1();	/* force remote to close & retry */
				rcleanup();
				continue;
			}
			memset(cfile, NULL, cfile_size+1);
			if (!read2mem(cfile, cfile_size)) {
				free(cfile);
				rcleanup();  /* remote will close & retry */
				continue;
			}
			if ((req_id < 0) && ((req_id = new_req_id(cp)) < 0))
				frecverr("%s: no more request ids", Rhost);
			cp = cvt_spoolfn(cp, CFILE, req_id);
			spool_file = strdup(cp);
			old_request = req_id;
			if (set_in == DF)
				req_id = -1;

			logit(LOG_DEBUG, "read Control File (%s)", spool_file);
			logit(LOG_DEBUG, "Data: (%s)", cfile);
			continue;

		case READDFILE:	/* read df file */
			if (set_in == NONE)
				set_in = DF;
			size = strtol(cp, &blank, 10);
			if (cp = blank, *cp++ != ' ')
				break;
			if (!chksize(Rhost, size)) {
				NAK2();	/* remote will wait and retry */
				continue;
			}
			if ((req_id < 0) && ((req_id = new_req_id(cp)) < 0))
				frecverr("%s: no more request ids", Rhost);
			cp = cvt_spoolfn(cp, DFILE, req_id);
			strcpy(Spoolfn, cp);
			(void) read2file(cp, size);
			continue;

		default:
			break;
		}
		errno = EINVAL;
		frecverr("protocol screwup");
		/* NOTREACHED */
	}
}

/*
 * Convert LPD-style spool file name to SVR4 spool file name
 */
static int
new_req_id(char *name)
{
	int fd;
	int start;
	int req_id;
	int done;
	char buf[BUFSIZ];

	strcpy(buf, name);
	*LPD_HOSTNAME(buf) = NULL;

	start = req_id = (atoi(LPD_JOBID(buf)) + (LPD_FILENO(buf) * NJOBIDS));
	sprintf(buf, "%s/%d-0", Rhost, req_id);

	do {
		done = 1;
		sprintf(buf, "%s/%d-0", Rhost, req_id);
		while ((req_id < 60000) &&
			((fd = open(buf, O_CREAT|O_EXCL, 0600)) < 0)) {
				logit(LOG_DEBUG, "ID collision %d", req_id);
				req_id += NJOBIDS;
				sprintf(buf, "%s/%d-0", Rhost, req_id);
		}
		if (req_id >= 60000) {
			if ((req_id = ((req_id % NJOBIDS) + 1)) >= NJOBIDS)
				req_id = 0;
			if (req_id == (start % NJOBIDS)) {
				logit(LOG_INFO, "ID (%s) no ids: start = %d",
					name, start);
				return (-1);
			}
			done = 0;
		}
	} while (done == 0);

	if (start != req_id)
		logit(LOG_INFO, "ID (%s) collision: start = %d, id = %d", name,
			start, req_id);
	close(fd);
	return (req_id);
}


static char *
cvt_spoolfn(char *cp, int offset, int id)
{
	char		c;
	static char	buf[MAX_SV_SPFN_SZ];

	c = *LPD_HOSTNAME(cp);
	*LPD_HOSTNAME(cp) = NULL;	/* terminate jobid string */
	(void) sprintf(buf, "%s/%d-%d", Rhost,
					id,
					LPD_FILENO(cp)+offset);
	*LPD_HOSTNAME(cp) = c;
	return (buf);
}

/* VARARGS1 */
static void
frecverr(char *msg, ...)
{
	char	*fmt;
	char	*errstr = NULL;
	va_list  argp;

	if (errno > 0 && errno < sys_nerr)
		errstr = PERROR;
	va_start(argp, msg);
	(void) vsprintf(Buf, msg, argp);
	va_end(argp);
	if (errstr) {
		strcat(Buf, ": ");
		strcat(Buf, errstr);
	}
	WriteLogMsg(Buf);

	rcleanup();
	NAK1();
	done(1);
	/* NOTREACHED */
}

static void
catch(int s)
{
#if DEBUG
	logit(LOG_DEBUG, "caught signal %d, cleaning up and exiting\n", s);
#endif
	rcleanup();
	if (s == SIGALRM && alrmhdlr != SIG_DFL && alrmhdlr != SIG_IGN)
		(*alrmhdlr)(s);
	done(1);
	/* NOTREACHED */
}

/*
 * Clean-up after partially submitted job
 */
static void
rcleanup(void)
{
	char	*cp;
	int	n;

#ifndef DEBUG
	if (Spoolfn[0]) {
		cp = strrchr(Spoolfn, '-');
		n = atoi(++cp);
		do {
			(void) sprintf(cp, "%d", n);
			(void) unlink(Spoolfn);
		} while (n--);

		/*
		 * 1120892 - /var/spool/lp/requests gets removed
		 * Move this unlink inside test for null Spoolfn.  Otherwise
		 * this unlinks /var/spool/lp/requests/.
		 */
		cp = makepath(Lp_Requests, Spoolfn, NULL);
		(void) unlink(cp);		/* secure file also */
		free(cp);
	}
#endif
	Spoolfn[0] = NULL;
}

/*
 * See if spool filesystem has a chance of holding incoming file
 */
static
chksize(char *dir, size_t size)
{
	register size_t	bsize;
	struct statfs	stfb;

	if (statfs(dir, &stfb, sizeof (struct statfs), 0) < 0)
		return (1);	/* ??? */
	bsize = stfb.f_bsize;
	size = (size + bsize - 1) / bsize;
	if (size > stfb.f_bfree)
		return (0);
	return (1);
}

/*
 * Convert lpd cf file to request structure
 * (sets Jobname and Jobsize as a side-effect)
 */
static REQUEST *
mkrequest(char *pcf, size_t n, int req_id)
{
	register int	i;
	register char	*cp;
	static REQUEST	rq;
	REQUEST		*ret = NULL;
	int		copies = 0;
	int		nfiles = 0;
	int		title_set = 0;
	int		reqno;
	int		nps = 0;
	int		banner = 0;
	int		priority = -1;
	char		*pend;
	char		*class = NULL,
			*noptions = NULL;
	char		**opts = NULL,
			**modes = NULL,
			**flist = NULL;
	struct stat	stb;
	char *LP_BSD_FORM = (char *)getenv("LP_BSD_FORM");

	if ((pcf == NULL) || (n == 0))
		return ((REQUEST *)NULL);
	logit(LOG_DEBUG, "mkrequest(%d), size %d, (%s)", req_id, n, pcf);

	Jobname = NULL;				/* global */
	Jobsize = 0;				/* global */
	pend = pcf + n;
	for (cp = pcf, i = 0; n--; cp++)		/* convert to strings */
		if (*cp == '\n')
			*cp = NULL;
	while (pcf < pend) {
		switch (*pcf++) {

		case JOBNAME:
			Jobname = pcf;
			break;

		case CLASS:
			class = pcf;
			break;

		case LITERAL:
			banner = 1;
			break;

		case TITLE:
			if (title_set)
				break;
			(void) sprintf(Buf, "%s'", PRTITLE);
			canonize(strchr(Buf, NULL), pcf, PRTITLE_ESCHARS);
			strcat(Buf, "'");
			appendlist(&modes, Buf);
			title_set = 1;
			break;

		case HOST:
			/* use local concept of who remote is: Rhost */
			break;

		case PERSON:
			if (strrchr(pcf, '!') != NULL)
				rq.user = makestr(pcf, NULL);
			else
				rq.user = makestr(Rhost, "!", pcf, NULL);
			break;

		case MAILUSER:
			rq.actions |= ACT_MAIL;
			break;

		case WIDTH:
			(void) sprintf(Buf, "%s%s", WIDTHFLD, pcf);
			appendlist(&opts, Buf);
			break;

		case INDENT:
			(void) sprintf(Buf, "%s%s", IDENT, pcf);
			appendlist(&opts, Buf);
			break;

		case FILENAME:
			if (!flist) {
				(void) sprintf(Buf, "%s'", FLIST);
				appendlist(&flist, Buf);
			}
			cp = cvt_spoolfn(cp, DFILE, req_id);
			if (stat(cp, &stb) < 0)
				goto out;
			if (*pcf)
				canonize(Buf, pcf, FLIST_ESCHARS);
			else
				strcpy(Buf, "-");
			(void) sprintf(strchr(Buf, NULL), ":%lu", stb.st_size);
			Jobsize += stb.st_size;
			appendlist(&flist, Buf);
			break;
		/*
		 * BSD PROTOCOL ENHANCEMENTS added here to support SYSV
		 * printing features across the wire
		 */
		case LP_OPTIONS:
			logit(LOG_DEBUG, "S5-options: %s", pcf);
			appendlist(&opts, pcf);
			noptions = strdup(pcf);
			break;

		case LP_FUNCTION:
			switch (*pcf++) {
				case SYSV_FORM:
					logit(LOG_DEBUG, "S5-form: %s", pcf);
					rq.form = strdup(pcf);
					break;
				case SYSV_HANDLING:
					logit(LOG_DEBUG, "S5-handling: %s",
						pcf);
					if (strcmp(pcf, NAME_IMMEDIATE) == 0)
						rq.actions |= ACT_IMMEDIATE;
					else if (strcmp(pcf, NAME_RESUME) == 0)
						rq.actions |= ACT_RESUME;
					else if (strcmp(pcf, NAME_HOLD) == 0)
						rq.actions |= ACT_HOLD;
					break;
				case SYSV_PRIORITY:
					logit(LOG_DEBUG, "S5-priority: %s",
								pcf);
					priority = atoi(pcf);
					break;
				case SYSV_NOTIFICATION:
					logit(LOG_DEBUG, "S5-notify: %s",
						pcf);
					/* rq.alert = strdup(pcf); */
					rq.actions |= ACT_MAIL;
					break;
				case SYSV_PAGES:
					logit(LOG_DEBUG, "S5-pages: %s", pcf);
					rq.pages = strdup(pcf);
					break;
				case SYSV_CHARSET:
					logit(LOG_DEBUG, "S5-charset: %s", pcf);
					rq.charset = strdup(pcf);
					break;
				case SYSV_TYPE:
					logit(LOG_DEBUG, "S5-type: %s", pcf);
					rq.input_type = strdup(pcf);
					break;
				case SYSV_MODE:
					logit(LOG_DEBUG, "S5-mode: %s", pcf);
					appendlist(&modes, pcf);
					break;
				default:
					logit(LOG_INFO, "S5-unknown: %s",
						pcf-1);
			}
			break;

		default:
			/* Look for format lines */
			if (!copies) {
				i = *LPD_HOSTNAME(pcf);
				*LPD_HOSTNAME(pcf) = NULL;
				reqno = atoi(LPD_JOBID(pcf));
				*LPD_HOSTNAME(pcf) = i;

				switch (*(pcf-1)) {

				case FFRMTCC:
					appendlist(&modes, CATVFILTER);
					appendlist(&opts, NOFILEBREAK);
					rq.input_type = SIMPLE;
					break;

				case FPR:
					if (!title_set) { /* shouldn't happen */
						(void) sprintf(Buf, "%s''",
								PRTITLE);
						appendlist(&modes, Buf);
						title_set = 1;
					}
					rq.input_type = SIMPLE;
					break;

				case FTROFF:
					rq.input_type = OTROFF;
					break;

				case FDITROFF:
					rq.input_type = TROFF;
					break;

				case FDVI:
					rq.input_type = TEX;
					break;

				case FGRAPH:
					rq.input_type = PLOT;
					break;

				case FCIF:
					rq.input_type = CIF;
					break;

				case FRASTER:
					rq.input_type = RASTER;
					break;

				case FFORTRAN:
					rq.input_type = FORTRAN;
					break;

				case FFRMT:
					break;
				default:
					logit(LOG_INFO, "unknown: %s", pcf-1);
					break;
				}
			}
			if (FORMAT_LINE(*(pcf-1))) {
				if (!flist)	/* count first group only */
					copies++;
				if (cp == NULL || strcmp(cp, pcf) != 0)
					nfiles++;
				cp = pcf;	/* remember df name */
			}
			break;
		}
		pcf += strlen(pcf) + 1;
	}
	if (flist != NULL)
		appendlist(&flist, "'");
	if (banner) {
		if (!(cp = (char *)malloc(strlen(NB(Jobname)) +
						strlen(NB(class)) +
							sizeof (JCSEP))))
			goto out;
		(void) sprintf(cp, "%s%s%s", NB(Jobname), JCSEP, NB(class));
		rq.title = cp;		/* jobname class */
	} else
		appendlist(&opts, NOBANNER);
	mergelist(&opts, flist);
	if (noptions == NULL)
		rq.options = sprintlist(opts);
	else
		rq.options = noptions;
	rq.copies = copies;
	rq.destination = Printer;
	rq.modes = sprintlist(modes);
	rq.priority = priority;
	if (LP_BSD_FORM && (rq.form == NULL)) rq.form = Strdup(LP_BSD_FORM);
	rq.version = VERSION_BSD;
	for (i = 1; i <= nfiles; i++) {
		(void) sprintf(Buf, "%s/%s/%d-%d", Lp_Tmp, Rhost, req_id, i);
		appendlist(&rq.file_list, Buf);
		if (!rq.input_type && psfile(Buf))	/* PostScript file? */
				nps++;
	}
	if (!rq.input_type)
		if (nps == nfiles)	/* if one is text, they all are */
			rq.input_type = POSTSCRIPT;
		else
			rq.input_type = SIMPLE;
	ret = &rq;
out:
	if (!ret)
		trashrequest(&rq);
	if (opts)
		freelist(opts);
	if (modes)
		freelist(modes);
	if (flist)
		freelist(flist);
	return (ret);
}

/*
 * Free request structure
 */
static void
trashrequest(register REQUEST *rqp)
{
	rqp->input_type = NULL;		/* since we didn't strdup() */
	rqp->destination = NULL;
	freerequest(rqp);
	memset((char *)rqp, 0, sizeof (REQUEST));
}

/*
 * Read incoming file into spool directory
 */
static
read2file(char *file, size_t size)
{
	int	nr;
	int	nrw;
	int	fd;
	char	*cp;

	fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	if (fd < 0) {
		frecverr(file);
		/* NOTREACHED */
	}
	ACK();
	for (; size > 0; size -= nrw) {
		nrw = MIN(BUFSIZ, size);
		cp = Buf;
		do {
			nr = TLIRead(CIP->fd, cp, nrw);
			if (nr <= 0) {
				frecverr("Lost connection");
				/* NOTREACHED */
			}
			nrw -= nr;
			cp += nr;
		} while (nrw > 0);
		nrw = MIN(BUFSIZ, size);
		if (write(fd, Buf, nrw) != nrw) {
			frecverr("%s: write error", file);
			/* NOTREACHED */
		}
	}
	(void) close(fd);		/* close spool file */
	if (noresponse()) {		/* file sent had bad data in it */
		(void) unlink(file);
		return (0);
	}
	ACK();
	return (1);
}

/*
 * Read incoming file into memory buffer
 */
static
read2mem(char *pmem, size_t size)
{
	int	nr;

	ACK();
	while (size) {
		nr = TLIRead(CIP->fd, pmem, size);
		if (nr <= 0) {
			frecverr("Lost connection");
			/* NOTREACHED */
		}
		size -= MIN(nr, size);
		pmem += nr;
	}
	if (noresponse())	/* file sent had bad data in it */
		return (0);
	ACK();
	return (1);
}

static
noresponse(void)
{
	char resp;

	if (read(CIP->fd, &resp, 1) != 1) {
		frecverr("Lost connection");
		/* NOTREACHED */
	}
	if (resp == ACKBYTE)
		return (0);
	return (1);
}

static void
print_request(REQUEST *rqp, char *file)
{
	short	status;
	long	chkbits;
	char	*reqid;
	char	*user = rqp->user;
	SECURE	secure;


	secure.size = Jobsize;		/* calculated by mkrequest() */
	secure.date = time(0);
	secure.system = Rhost;
	secure.user = user;
	reqid = strrchr(file, '-');	/* construct req-id from file name */
	*reqid = NULL;
	secure.req_id = mkreqid(Printer, basename(file));
	*reqid = '-';			/* restore file name */
	secure.uid = secure.gid = UID_MAX + 1;
	if (putrequest(file, rqp) < 0 || putsecure(file, &secure) < 0) {
		char	*error = PERROR;

		smail(user, E_LP_PUTREQUEST, error);
		frecverr("can't putrequest: %s", error);
		/* NOTREACHED */
	}
	free(secure.req_id);
	putmessage(Msg, S_PRINT_REQUEST, file);
	if (msend(Msg) == -1 || mrecv(Msg, MSGMAX) != R_PRINT_REQUEST) {
		smail(user, E_LP_NEEDSCHED);
		frecverr("%s link to lpsched down", Name);
		/* NOTREACHED */
	}
	getmessage(Msg, R_PRINT_REQUEST, &status, &reqid, &chkbits);
	if (status != MOK)
		logit(LOG_DEBUG, "%s print request failed, status = %d",
							Printer, status);
	switch (status) {
	case MOK:
		break;
	case MNOMEM:
		smail(user, E_LP_MNOMEM);
		break;
	case MDENYDEST:
		if (chkbits) {
			if (chkbits & PCK_TYPE)
				smail(user, E_LP_PGONE, rqp->destination);
			else {
				char buf[20];

				buf[0] = NULL;
				if (chkbits & PCK_WIDTH)
					strcat(buf, "-w width ");
				if (chkbits & PCK_BANNER)
					strcat(buf, "-h ");
				smail(user, E_LP_PTRCHK, buf);
			}
		} else
			smail(user, E_LP_DENYDEST, rqp->destination);
		break;
	case MNOFILTER:
		smail(user, E_LP_NOFILTER);
		break;
	case MERRDEST:
		smail(user, E_LP_REQDENY, rqp->destination);
		break;
	case MNOOPEN:
		smail(user, E_LPP_NOOPEN);
		frecverr("lpsched can't open %s", file);	/* cleans-up */
		/* NOTREACHED */
	default:
		smail(user, E_LP_BADSTATUS, status);
		break;
	}
}

/*
 * Send message to remote user
 */
/* VARARGS2 */
static void
smail(char *user, long msgid, ...)
{
	int		p[2];
	int		i;
	va_list		argp;
	struct rlimit	rl;

	if (pipe(p) < 0) {
		logit(LOG_WARNING, "%s can't create mail pipe: %s", Name,
			PERROR);
		return;
	}
	switch (fork()) {
	case -1:
		logit(LOG_WARNING, "%s can't fork: %s", Name, PERROR);
		return;

	case 0:
		(void) fclose(stdout);
		(void) fclose(stderr);
		(void) dup2(p[0], 0);			/* stdin */
		(void) open("/dev/null", O_RDWR);	/* stdout */
		(void) open("/dev/null", O_RDWR);	/* stderr */
		if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
			for (i = 3; i < rl.rlim_cur; i++)
				(void) close(i);
		putenv("LOGNAME=lp");
		execl(BINMAIL, basename(BINMAIL), user, NULL);
		logit(LOG_WARNING,
			"%s can't execl %s: %s", Name, BINMAIL, PERROR);
		exit(1);
		/* NOTREACHED */

	default:
		fflush(stderr);
		(void) dup2(p[1], fileno(stderr)); /* _lp_msg() needs stderr */
		(void) close(p[0]);
		(void) close(p[1]);
		fprintf(stderr, gettext("Subject: printer job\n\n"));
		fprintf(stderr, gettext("Your printer job "));
		if (Jobname && *Jobname)
			fprintf(stderr, "(%s)", Jobname);
		fprintf(stderr, gettext("\ncould not be printed.\n"));
		fprintf(stderr, gettext("\nReason for failure:\n\n"));
		va_start(argp, msgid);
		_lp_msg(msgid, argp);
		va_end(argp);
		fclose(stderr);			/* close pipe to mail */
		wait(0);
	}
}

/*
 * Check to see if printer queue is rejecting
 */
static
qdisabled(void)
{
	char	*pjunk;
	short	status, pstatus;
	long	ljunk;

	putmessage(Msg, S_INQUIRE_PRINTER_STATUS, Printer);
	if (msend(Msg) == -1 ||
	    mrecv(Msg, MSGMAX) != R_INQUIRE_PRINTER_STATUS) {
		logit(LOG_WARNING, "%s link to lpsched down", Name);
		return (1);
	}
	getmessage(Msg, R_INQUIRE_PRINTER_STATUS, &status,
						&pjunk,
						&pjunk,
						&pjunk,
						&pjunk,
						&pjunk,
						&pstatus,
						&pjunk,
						&ljunk,
						&ljunk);
	/*
	 * If there is no printer check for a class
	 */
	if (status == MNODEST) {
		putmessage(Msg, S_INQUIRE_CLASS, Printer);
		if (msend(Msg) == -1 ||
		    mrecv(Msg, MSGMAX) != R_INQUIRE_CLASS) {
			logit(LOG_WARNING, "%s link to lpsched down", Name);
			return (1);
		}
		getmessage(Msg, R_INQUIRE_CLASS, &status,
						&pjunk,
						&pstatus,
						&pjunk,
						&pjunk);
	}
	if (status != MOK || pstatus & PS_REJECTED)
		return (1);
	return (0);
}
