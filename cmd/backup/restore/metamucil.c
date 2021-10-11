#ident	"@(#)metamucil.c 1.39 93/11/10"

#include "restore.h"
#include <config.h>
#include <rmt.h>
#include <ctype.h>
#include <signal.h>
#include <sys/termio.h>
#include <sys/mtio.h>
#include <errno.h>
#include <recrest.h>
#include <operator.h>
#include <syslog.h>
#include <utime.h>

#ifdef USG
/*
 * translate signal routines
 */
#define	sigvec			sigaction		/* struct and func */
#define	sv_handler		sa_handler
#endif

extern char *sys_errlist[];
extern int sys_nerr;

/*
 * XXX: variables shared with tape.c
 */
extern int mt;
extern char magtape[];
extern char *host;
extern daddr_t rec_position;
extern int nometamucilseek;

static FILE *fp;
static time_t metamucil_dumptime;
static struct dirstats dirdata;
static int lastdumpnum = 1;
static int savedev;

int metamucil_mounts;
static int metamucil_notify;

static int use_operator;
static fd_set operfds;

#ifdef __STDC__
static void extract_done(void);
static int m_getinput(FILE *, char *, int *, int *, char *type);
static void m_mounttape(char *, struct s_spcl *);
static int mountprompt(char *, char *, char *devhost);
static void m_checkpath(char *);
static int newdir(char *);
static int name_hash(char *);
static void m_setdumpnum(int);
static void m_rewind(void);
static void default_device(void);
static void save_device(void);
static void operator_setup(void);
static int yesorno(char *);
static void send_oper(char *);
static void send_again(int);
static int askoper(char *, char *, char *, int *val);
#else
static void extract_done();
static int m_getinput();
static void m_mounttape();
static int mountprompt();
static void m_checkpath();
static int newdir();
static int name_hash();
static void m_setdumpnum();
static void m_rewind();
static void default_device();
static void save_device();
static void operator_setup();
static int yesorno();
static void send_oper();
static void send_again();
static int askoper();
/*
 * XXX broken string.h
 */
extern int strncasecmp(const char *, const char *, int);
#endif

void
metamucil(file)
	char *file;
{
	char name[MAXPATHLEN];
	char lastfile[MAXPATHLEN];
	int arg1, arg2;
	char type;
	ino_t ino;
	daddr_t offset;
	char *symtbl = RESTORESYMTABLE;

	if ((fp = fopen(file, "r")) == NULL) {
		(void) fprintf(stderr,
		    gettext("cannot open recover command file `%s'\n"),
			file);
		done(1);
	}

	/*
	 * tell setup() and getvol() that we want to do label
	 * processing on mounts
	 */
	metamucil_mounts = 1;
	metamucil_notify = 0;

	savedev = 0;

	/*
	 * get commands from metamucil input file and process them.
	 */
	while (m_getinput(fp, name, &arg1, &arg2, &type) != -1) {
		switch (type) {
		case XRESTORE:
			savedev = 1;
			if (metamucil_notify == -1) {
				vflag++;
				metamucil_notify = 0;
			}
			setup();
			if (arg1 != dumpdate) {
				(void) fprintf(stderr, gettext(
					"%s: dumpdate mismatch\n"), "xrestore");
				done(1);
			}
			extractdirs(1);
			initsymtable((char *)0);
			ino = dirlookup(".");
			treescan(".", ino, addfile);
			createfiles();
			createlinks();
			setdirmodes();
			break;
		case RRESTORE:
			savedev = 1;
			if (metamucil_notify == -1) {
				metamucil_notify = 0;
				vflag++;
			}
			setup();
			if (arg1 != dumpdate) {
				(void) fprintf(stderr,  gettext(
					"%s: dumpdate mismatch\n"), "rrestore");
				done(1);
			}

			if (dumptime > 0) {
				/*
				 * incremental dump tape
				 */
				initsymtable(symtbl);
				extractdirs(1);
				removeoldleaves();
				treescan(".", ROOTINO, nodeupdates);
				findunreflinks();
				removeoldnodes();
			} else {
				/*
				 * level 0 dump tape
				 */
				initsymtable((char *)0);
				extractdirs(1);
				treescan(".", ROOTINO, nodeupdates);
			}
			createleaves(symtbl);
			createlinks();
			setdirmodes();
			checkrestore();
			dumpsymtable(symtbl, (long)1);
			break;
		case NOTIFYREC:
			metamucil_notify = arg1;
			break;
		case TAPEREC:
			lastdumpnum = 1;
			reset_dump();
			m_mounttape(name, &spcl);
			skipmaps();
			findinode(&spcl);
			break;
		case DUMPREC:
			dumpnum = arg1;
			dumpdate = arg2;
			if (dumpnum > 1) {
				if (dumpnum <= lastdumpnum) {
					(void) fprintf(stderr,
						gettext("Bad %s\n"), "dumpnum");
					done(1);
				}
				m_setdumpnum(dumpnum - lastdumpnum);
				flsht();
				reset_dump();
				if (readhdr(&spcl) != GOOD) {
					(void) fprintf(stderr,
					    gettext("cannot read header\n"));
					done(1);
				}
				metamucil_dumptime = spcl.c_date;
				lastdumpnum = dumpnum;
				skipmaps();
				findinode(&spcl);
			}
			if (dumpdate != metamucil_dumptime) {
				(void) fprintf(stderr, gettext(
				    "Date mismatch.  Wrong dump or tape?\n"));
				done(1);
			}
			dumptime = spcl.c_ddate;
			break;
		case FILEREC:
			/*
			 * arg2 is the TP_BSIZE block offset in the current
			 * volume of the desired file.  The offset must
			 * be contained in the current or higher numbered
			 * record on this volume.
			 */

			ino = arg1;
			offset = (daddr_t)arg2;

			/*
			 * check that record is later or
			 * in the current buffer
			 */
			if ((offset) && (! nometamucilseek)) {
				daddr_t record;

				record = offset / ntrec;
				if (record < rec_position - 1)
					panic(gettext(
		"missed record error -- retry recover as `recover -r'\n"));
				/*
				 * don't record skip if the desired
				 * record is buffered
				 */
				if (record > rec_position) {
					/*
					 * metamucil_seek failure means the
					 * device cannot do this.
					 * XXX - if this is a file, we should
					 * do an lseek.
					 */
					if (metamucil_seek(record) != -1) {
						int nt;

						flsht();
						for (nt = 0;
						    nt < ntrec; nt++) {
							if (gethead(&spcl) !=
							    FAIL)
								break;
						}
						if (nt == ntrec)
							panic(gettext(
		    "record skip error -- retry recover as `recover -r'\n"));
						findinode(&spcl);
					}
				}
			}
again:
			while (curfile.ino < ino)
				skipfile();
			if (curfile.ino != ino) {
				if (S_ISDIR(curfile.dip->di_mode)) {
					while (S_ISDIR(curfile.dip->di_mode))
						skipfile();
					goto again;
				}
				panic(gettext("missed file\n"));
			}
			m_checkpath(name);
			(void) extractfile(name);
			if (metamucil_notify == -1) {
				/*
				 * note that `extractfile' calls
				 * `metamucil_notify_msg'
				 */
				(void) fprintf(stderr,
				    gettext("extracted %s\n"), name);
			}
			(void) strcpy(lastfile, name);
			break;
		case DIRREC:
			m_checkpath(name);
			if (mkdir(name, 0777) == -1 && !newdir(name))
				break;
#ifdef USG
			(void) chown(name, dirdata.dir_uid, dirdata.dir_gid);
#else
			(void) chown(name,
				(int)dirdata.dir_uid, (int)dirdata.dir_gid);
#endif
			(void) chmod(name, dirdata.dir_mode);
			utime(name, (struct utimbuf *)&dirdata.dir_atime);
			break;
		case LINKREC:
			m_checkpath(name);
			if (link(lastfile, name) == -1) {
				perror("link");
				(void) fprintf(stderr,
					gettext("cannot link `%s' to `%s'\n"),
					name, lastfile);
				break;
			}
			if (metamucil_notify == -1) {
				(void) fprintf(stderr,
					gettext("linked `%s' to `%s'\n"),
					name, lastfile);
			} else if (metamucil_notify) {
				metamucil_extract_msg();
			}
			break;
		default:
			(void) fprintf(stderr,
				gettext("unknown input record: %c\n"),
				(u_char)type);
			break;
		}
	}
	(void) fclose(fp);
	(void) unlink(file);
	extract_done();
	done(0);
}

static int extract_cnt;

void
#ifdef __STDC__
metamucil_extract_msg(void)
#else
metamucil_extract_msg()
#endif
{
	time_t now;

	if (metamucil_notify == -1 || metamucil_notify == 0)
		return;
	++extract_cnt;
	if ((extract_cnt % metamucil_notify) == 0) {
		now = time(0);
		(void) fprintf(stderr, "%s: %s", progname, lctime(&now));
		if (extract_cnt > 1)
			(void) fprintf(stderr,
				gettext("%d files extracted\n"), extract_cnt);
		else
			(void) fprintf(stderr,
				gettext("%d file extracted\n"), extract_cnt);
	}
}

static void
#ifdef __STDC__
extract_done(void)
#else
extract_done()
#endif
{
	time_t now;

	if (metamucil_notify == -1 || metamucil_notify == 0)
		return;
	if (extract_cnt % metamucil_notify) {
		now = time(0);
		(void) fprintf(stderr, "%s: %s", progname, lctime(&now));
		if (extract_cnt > 1)
			(void) fprintf(stderr,
				gettext("%d files extracted\n"), extract_cnt);
		else
			(void) fprintf(stderr,
				gettext("%d file extracted\n"), extract_cnt);
	}
}

/*
 * called from `tape.c/getvol()' when we're in metamucil mode.
 * When we're called we expect a TAPEREC to be the next thing
 * in the metamucil command file - if not we panic since the
 * protocol has apparently broken down.  When we successfully get
 * a TAPEREC, we call  `m_mounttape()' for the given tape label.
 */
void
metamucil_getvol(s)
	struct s_spcl *s;
{
	char name[MAXPATHLEN];
	int arg1, arg2;
	char type;
	if (m_getinput(fp, name, &arg1, &arg2, &type) == -1) {
		panic(gettext("%s: no input\n"), "metamucil_getvol");
	}
	if (type != TAPEREC) {
		panic(gettext("%s: input not tape\n"), "metamucil_getvol");
	}
	lastdumpnum = 1;
	m_mounttape(name, s);
}

/*
 * read a input record from the command list
 */
static int
m_getinput(fp, name, arg1, arg2, type)
	FILE *fp;
	char *name;
	int *arg1, *arg2;
	char *type;
{
	int c;
	register char *p = name;

	c = getc(fp);
	*type = (char)c;
	switch (c) {
	case TAPEREC:
		if (fread(name, LBLSIZE, 1, fp) != 1)
			return (-1);
		name[LBLSIZE] = '\0';
		break;
	case DUMPREC:
		*arg1 = getw(fp);
		if (*arg1 == -1)
			return (-1);
		*arg2 = getw(fp);
		if (*arg2 == -1)
			return (-1);
		break;
	case FILEREC:
		*arg1 = getw(fp);
		if (*arg1 == -1)
			return (-1);
		*arg2 = getw(fp);
		if (*arg2 == -1)
			return (-1);
		while (c = getc(fp)) {
			if (c == -1)
				return (-1);
			*p++ = c;
		}
		*p = '\0';
		break;
	case DIRREC:
		*arg1 = getw(fp);
		if (*arg1 == -1)
			return (-1);
		while (c = getc(fp)) {
			if (c == -1)
				return (-1);
			*p++ = c;
		}
		*p = '\0';
		if (fread(&dirdata, sizeof (struct dirstats), 1, fp) != 1)
			return (-1);
		break;
	case XRESTORE:
	case RRESTORE:
		*arg1 = getw(fp);
		if (*arg1 == -1)
			return (-1);
		break;
	case NOTIFYREC:
		*arg1 = getw(fp);
		if (feof(fp))
			return (-1);
		break;
	case LINKREC:
		while (c = getc(fp)) {
			if (c == -1)
				return (-1);
			*p++ = c;
		}
		*p = '\0';
		break;
	case EOF:
		return (-1);
	default:
		(void) fprintf(stderr, gettext("bad command\n"));
		return (-1);
	}

	return (0);
}

/*
 * get a tape mounted.
 */
static void
m_mounttape(label, s)
	char *label;
	struct s_spcl *s;
{
	static int firstcall = 1;
	extern int pipein;
	static char lastdev[256], lasthost[256];
	int doprompt = 0, doclose = 0;
	int mounted;

	if (firstcall) {
		firstcall = 0;
	} else {
		closemt();
		get_next_device();
	}

	if (magtape[0] == '\0') {
		default_device();
		doprompt = 1;
	} else if (strcmp(magtape, lastdev) == 0) {
		if ((host && strcmp(host, lasthost) == 0) ||
				(host == NULL && lasthost[0] == '\0')) {
			/*
			 * if re-mounting the same device, we must prompt.
			 * Otherwise we'll make a try in case user has
			 * pre-mounted it...
			 */
			doprompt = 1;
		}
	}
	mounted = 0;
	while (!mounted) {
		if (doclose)
			closemt();
		flsht();
		if (doprompt) {
			if (mountprompt(label, magtape, host) != GOOD) {
				if (yesorno(gettext(
		"Bad mount reply -- do you want to continue?")) == GOOD) {
					continue;
				}
				done(1);
			}
		} else {
			if (host)
				(void) fprintf(stderr, gettext(
					"trying device `%s' on host %s"),
					magtape, host);
			else
				(void) fprintf(stderr,
					gettext("trying device `%s'"), magtape);
			(void) fprintf(stderr, "\n");
		}
		doprompt = doclose = 1;
		if (host) {
			if ((mt = rmtopen(magtape, 0)) < 0) {
				continue;
			}
		} else {
			if ((mt = open(magtape, 0)) < 0) {
				/* perror(magtape); */
				(void) fprintf(stderr, gettext(
					"Error opening %s\n\t[errno %d: %s]\n"),
					magtape, errno,
					errno < sys_nerr ?
						sys_errlist[errno] : "");
				continue;
			}
		}
		m_rewind();

		if (!pipein && !bflag)
			findtapeblksize(TAPE_FILE);

		if (gethead(s) == FAIL) {
			(void) fprintf(stderr, gettext("Cannot read header\n"));
			continue;
		}

		if (strcmp(s->c_label, "none") == 0) {
			char buffer[256];
			(void) sprintf(buffer, gettext(
				"OK to use un-named volume as `%s'?"), label);
			if (yesorno(buffer) != GOOD) {
				continue;
			}
			mounted = 1;
		} else {
			if (bcmp(s->c_label, label, LBLSIZE)) {
				(void) fprintf(stderr,
					gettext("label mismatch\n"));
			} else {
				/* got it */
				mounted = 1;
				volno = s->c_volume;
			}
		}
	}
	metamucil_dumptime = s->c_date;
	dumptime = s->c_ddate;
	(void) strcpy(lastdev, magtape);
	if (host)
		(void) strcpy(lasthost, host);
	else
		lasthost[0] = '\0';
	save_device();
}

/*
 * issue a operator request, and send back a reply
 */
static int
mountprompt(label, dev, devhost)
	char *label;
	char *dev;
	char *devhost;
{
	static char holdhost[256];
	char *s, buf[MAXMSGLEN], ttybuf[MAXMSGLEN];
	int rc;

	(void) sprintf(buf, gettext("Mount volume `%s' then enter device name"),
		label);
	(void) strcpy(ttybuf, buf);
	s = ttybuf+strlen(ttybuf);
	(void) sprintf(s, gettext("\n(default-> %s%s%s)"),
		devhost ? devhost : "", devhost ? ":" : "", dev);

	if (askoper(buf, ttybuf, buf, &rc) == FAIL)
		return (FAIL);

	if (buf[0] == '\n')
		(void) sprintf(buf, "%s%s%s",
			devhost ? devhost : "", devhost ? ":" : "", dev);
	if (s = strchr(buf, '\n'))
		*s = '\0';
	if (strchr(buf, '/')) {
		metamucil_setinput(buf, NULL);
		if (host) {
			/*
			 * the setinput routine will modify `buf' and
			 * leave the global var `host' pointing into 'buf'
			 * In this case, we need to keep
			 * a non-stack based copy of `host' around for
			 * future use...
			 */
			(void) strcpy(holdhost, host);
			host = holdhost;
		}
		return (GOOD);
	} else if (use_operator && rc == OPERMSG_RCVD) {
		/* operator must tell us what device to use */
		return (FAIL);
	} else if (*buf == '\0') {
		/* using the device we've already set up */
		return (GOOD);
	} else {
		return (FAIL);
	}
}

#define	NEWNAME_HASHSIZE	31531
static struct namehash {
	char *np;
	struct namehash *nxt;
} buckets[NEWNAME_HASHSIZE];

#ifdef __STDC__
static void hashin(struct namehash *);
#endif

static void
m_checkpath(name)
	char *name;
{
	register char *cp;
	char *start;

	/*
	 * make sure all directory components of path 'p' exist.
	 */
	start = strchr(name, '/');
	if (start == 0)
		return;
	for (cp = start; *cp != '\0'; cp++) {
		if (*cp != '/' || cp == name)
			continue;
		*cp = '\0';
		if (mkdir(name, 0777) == -1) {
			if ((errno != EEXIST) && (errno != EROFS))
				perror("mkdir");
		} else {
			/*
			 * note the created name
			 */
			struct namehash *hp;

			hp = (struct namehash *)
				malloc(sizeof (struct namehash));
			if (hp) {
				hp->np = (char *)malloc(strlen(name)+1);
				if (hp->np) {
					(void) strcpy(hp->np, name);
					hashin(hp);
				}
			}
		}
		*cp = '/';
	}
}

static int
newdir(path)
	char *path;
{
	int bucketnum;
	register struct namehash *p;

	/*
	 * see if this is a directory we created (in m_checkpath above).
	 */
	bucketnum = name_hash(path);
	for (p = buckets[bucketnum].nxt; p; p = p->nxt)
		if (strcmp(p->np, path) == 0)
			return (1);
	return (0);
}

static void
hashin(hp)
	struct namehash *hp;
{
	int bucketnum;

	bucketnum = name_hash(hp->np);
	hp->nxt = buckets[bucketnum].nxt;
	buckets[bucketnum].nxt = hp;
}

static int
name_hash(cp)
	char *cp;
{
	int sum = 0;

	for (; *cp; *cp++)
		sum = (sum << 3) + *cp;
	return ((sum & 0x7fffffff) % NEWNAME_HASHSIZE);
}

static void
m_setdumpnum(space)
	int space;
{
	struct mtop tcom;

	if (space > 1)
		(void) fprintf(stderr, gettext("forward spacing %d files\n"),
			space);
	else
		(void) fprintf(stderr, gettext("forward spacing %d file\n"),
			space);
	tcom.mt_op = MTFSF;
	tcom.mt_count = space;
	if (host)
		(void) rmtioctl(MTFSF, space);
	else
		if (ioctl(mt, (int)MTIOCTOP, (char *)&tcom) < 0)
			perror("ioctl MTFSF");
}

static void
#ifdef __STDC__
m_rewind(void)
#else
m_rewind()
#endif
{
	struct mtop tcom;

	tcom.mt_op = MTREW;
	tcom.mt_count = 0;
	if (host)
		(void) rmtioctl(MTREW, 0);
	else
		if (ioctl(mt, (int)MTIOCTOP, (char *)&tcom) < 0) {
			(void) fprintf(stderr, gettext(
			"Warning: unable to rewind %s\n\t[errno %d: %s]\n"),
				magtape, errno,
				errno < sys_nerr ? sys_errlist[errno] : "");
		}
}

static void
#ifdef __STDC__
default_device(void)
#else
default_device()
#endif
{
	char *tp;
	FILE *fp;
	static char inbuf[MAXPATHLEN];

	if (fp = fopen(RESTOREDEVICE, "r")) {
		if (fgets(inbuf, MAXPATHLEN, fp)) {
			if (tp = strchr(inbuf, ':')) {
				*tp++ = '\0';
				host = inbuf;
				(void) strcpy(magtape, tp);
				if (rmthost(host, ntrec) == 0)
					done(1);
			} else {
				(void) strcpy(magtape, inbuf);
			}
			return;
		}
	}

	if (tp = getenv("TAPE")) {
		(void) strcpy(magtape, tp);
	} else {
		if (metamucil_mode == METAMUCIL)
			(void) strcpy(magtape, TAPE);
		else
			(void) strcpy(magtape, DEFTAPE);
	}

}

static void
#ifdef __STDC__
save_device(void)
#else
save_device()
#endif
{
	FILE *fp;
	int unlinkit = 0;

	if (savedev == 0)
		return;

	if (fp = fopen(RESTOREDEVICE, "w")) {
		if (host) {
			if (fputs(host, fp) == EOF)
				unlinkit = 1;
			if (fputc(':', fp) == EOF)
				unlinkit = 1;
		}
		if (fputs(magtape, fp) == EOF)
			unlinkit = 1;
		(void) fclose(fp);
		if (unlinkit)
			(void) unlink(RESTOREDEVICE);
	}
}

static void
#ifdef __STDC__
operator_setup(void)
#else
operator_setup()
#endif
{
	char	opserver[BCHOSTNAMELEN];

	use_operator = 0;
	if (metamucil_mode == NOT_METAMUCIL)
		return;
	(void) getopserver(opserver, sizeof (opserver));
	if (oper_init(opserver, progname, 1) != OPERMSG_CONNECTED)
		return;
	use_operator = 1;
	FD_ZERO(&operfds);
	if (terminal) {
		FD_SET(fileno(terminal), &operfds);
	}
}

/*
 * pose a question to the operator and get a `yes' or `no' answer.
 */
static int
yesorno(msg)
	char *msg;
{
	char *yes = gettext("yes");
	char reply[MAXMSGLEN];
	register char *p, *cp;
	int rc;

	if (askoper(msg, NULL, reply, &rc) == FAIL)
		return (FAIL);
	if (p = strchr(reply, '\n'))
		*p = '\0';
	p = reply;
	while (isspace((u_char)*p))
		p++;

	if (strncasecmp(p, yes, 1) == 0)
		return (GOOD);

	return (FAIL);
}

static char *holdmsg;
static int timeout;
static u_long msgnum;

static void
send_oper(msg)
	char *msg;
{
	holdmsg = msg;
	timeout += 120;
	msgnum = oper_send(timeout, LOG_ALERT,
			MSG_DISPLAY|MSG_NEEDREPLY, holdmsg);
	if (msgnum == 0) {
		(void) fprintf(stderr,
			gettext("unable to send operator message\n"));
	}
	(void) alarm(timeout);
}

/*ARGSUSED*/
static void
send_again(sig)
	int	sig;
{
	(void) oper_cancel(msgnum, 1);
	send_oper(holdmsg);
}

static int
askoper(opermsg, ttymsg, reply, val)
	char *opermsg, *ttymsg, *reply;
	int *val;
{
	u_long retmsg;
	int rc;
	struct sigvec sigalrm, oalrm;

	operator_setup();

	if (use_operator == 0 && terminal == NULL) {
		(void) fprintf(stderr,
			gettext("cannot ask operator `%s'\n"), opermsg);
		done(1);
	}

	if (use_operator) {
		sigalrm.sv_handler = send_again;
#ifdef USG
		sigalrm.sa_flags = SA_RESTART;
		(void) sigemptyset(&sigalrm.sa_mask);
#else
		sigalrm.sv_mask = 0;
		sigalrm.sv_flags = 0;
#endif
		(void) sigvec(SIGALRM, &sigalrm, &oalrm);
		timeout = 0;
		send_oper(opermsg);
		if (msgnum == 0) {
			(void) fprintf(stderr,
				gettext("unable to send operator message\n"));
		}
	}

	if (terminal) {
		if (ttymsg)
			(void) fprintf(stderr, "%s ", ttymsg);
		else
			(void) fprintf(stderr, "%s ", opermsg);
	}

	*val = -1;

	if (use_operator) {
		rc = oper_receive(&operfds, reply, MAXMSGLEN, &retmsg);
		*val = rc;
		switch (rc) {
		case OPERMSG_READY:
			/* input from the terminal */
			(void) fgets(reply, MAXMSGLEN, terminal);
			if (feof(terminal))
				done(1);
			break;
		case OPERMSG_RCVD:
			/* input from operator daemon */
			if (terminal) {
				(void) fprintf(stderr,  gettext(
					"\nreply received from operator\n"));
				(void) ioctl(fileno(terminal),
					TCFLSH, TCIFLUSH);
			}
			if (retmsg != msgnum) {
				(void) fprintf(stderr, gettext("%s mismatch\n"),
					"oper_recieve msgnum");
				return (FAIL);
			}
			break;
		case OPERMSG_ERROR:
			(void) fprintf(stderr,
				gettext("%s error\n"), "oper_recieve");
			return (FAIL);
		default:
			(void) fprintf(stderr,
				gettext("unknown %s return code (%d)\n"),
				"oper_receive", rc);
			break;
		}

		/*
		 * cancel the operator message -- we don't want it
		 * to remain sitting on the monitor screen forever...
		 */
		if (oper_cancel(msgnum, 1) == 0)
			(void) fprintf(stderr,
				gettext("%s error\n"), "oper_cancel");
		oper_end();
		(void) alarm(0);
		(void) sigvec(SIGALRM, &oalrm, NULL);
	} else if (terminal) {
		(void) fgets(reply, MAXMSGLEN, terminal);
		if (feof(terminal))
			done(1);
	}
	return (GOOD);
}
