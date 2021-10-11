#ident	"@(#)fulldump.c 1.31 92/05/29"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <recrest.h>
#include "cmds.h"
#include "extract.h"
#ifdef USG
#include <sys/filio.h>
#endif
#include <config.h>

char *restorepath;

#ifdef __STDC__
static void show_header(struct dheader *, struct dnode *);
static int do_fullrestore(struct dheader *, char *, int, int);
#else
static void show_header();
static int do_fullrestore();
#endif

/*
 * support for browsing and restoring full dumps.
 */

/*
 * show the most recent full and incremental dumps of a given filesystem.
 * If no filesystem dumps are found, treat the argument as an individual
 * file and show the most current dump of it relative to the time setting.
 */
void
showdump(host, ap, timestamp)
	char *host;
	struct arglist *ap;
	time_t	timestamp;
{
	register struct afile *p = ap->head;
	struct dumplist *l, *t;
	struct dnode dn;
	struct dheader *h;
	u_long dumpid;
	char termbuf[MAXPATHLEN];

	term_start_output();
	while (p < ap->last) {
		if (read_dumps(dbserv, host, p->name, timestamp, &l) == 0) {
			/*
			 * a mount point.
			 */
			if (p != ap->head)
				term_putline("\n");
			(void) sprintf(termbuf, "%s:\n", p->name);
			term_putline(termbuf);
			t = l;
			while (t) {
				if (t != l)
					term_putline("\n");
				show_header(t->h, (struct dnode *)NULL);
				t = t->nxt;
			}
			free_dumplist(l);
		} else if ((dumpid = getdnode(host, &dn, p->dep, VREAD,
				timestamp, LOOKUP_DEFAULT, p->name)) == 0) {
			(void) sprintf(termbuf,
				gettext("no dumps of `%s'\n"), p->name);
			term_putline(termbuf);
		} else if (header_get(dbserv, host, dumpid, &h)) {
			(void) sprintf(termbuf,
				gettext("no dumps of `%s'\n"), p->name);
			term_putline(termbuf);
		} else {
			/*
			 * an individual file
			 */
			if (p != ap->head)
				term_putline("\n");
			(void) sprintf(termbuf, "%s:\n", p->name);
			term_putline(termbuf);
			show_header(h, &dn);
		}
		p++;
	}
	term_finish_output();
}

static void
show_header(h, dnp)
	struct dheader *h;
	struct dnode *dnp;
{
	register int i;
	char termbuf[MAXPATHLEN];

	(void) sprintf(termbuf, gettext("disk host:\t\t %s\n"), h->dh_host);
	term_putline(termbuf);
	(void) sprintf(termbuf, gettext("dumped device:\t\t %s\n"), h->dh_dev);
	term_putline(termbuf);
	(void) sprintf(termbuf,
		gettext("filesystem mount point:\t %s\n"), h->dh_mnt);
	term_putline(termbuf);
	(void) sprintf(termbuf, gettext("dump level:\t\t %lu\n"), h->dh_level);
	term_putline(termbuf);
	(void) sprintf(termbuf,
		gettext("dump time:\t\t %s"), lctime(&h->dh_time));
	term_putline(termbuf);
	if (h->dh_flags & DH_ACTIVE)
		term_putline(gettext(
		    "A re-dump of files active during the previous dump\n"));
	if (h->dh_flags & DH_TRUEINC)
		term_putline(gettext("A true incremental dump\n"));
	if (dnp == NULL) {
		for (i = 0; i < h->dh_ntapes; i++) {
			(void) sprintf(termbuf,
			    gettext("tape %d:\t`%.*s' file #%lu\n"),
				i+1, LBLSIZE, h->dh_label[i],
				i == 0 ? h->dh_position : 1);
			term_putline(termbuf);
		}
	} else {
		int volidx = dnp->dn_volid;

		(void) sprintf(termbuf,
			gettext("tape %d:\t`%.*s' file #%lu\n"),
				volidx+1, LBLSIZE, h->dh_label[volidx],
				volidx == 0 ? h->dh_position : 1);
		term_putline(termbuf);
		if (dnp->dn_flags & DN_MULTITAPE) {
			(void) sprintf(termbuf,
			    gettext("tape %d:\t`%.*s' file #1\n"),
				volidx+2, LBLSIZE, h->dh_label[volidx+1]);
			term_putline(termbuf);
		}
	}
}

void
fullrestore(host, ap, localdir, timestamp, rflag, notify)
	char *host;
	struct arglist *ap;
	char *localdir;
	time_t	timestamp;
	int rflag;
	int notify;
{
	register struct afile *p = ap->head;
	struct dumplist *l, *t;
	static char thishost[2*BCHOSTNAMELEN];
	int rc;
	char delpath[MAXPATHLEN];
#ifdef USG
	sigset_t myset;
#else
	int mymask;
#endif;

	/*
	 * full restores can only be done on the machine from
	 * which they were dumped, and can only be done by root.
	 */
	if (*thishost == '\0') {
		if (gethostname(thishost, BCHOSTNAMELEN) == -1)
			perror("gethostname");
	}
	if (strncmp(thishost, host, strlen(thishost))) {
		(void) printf(gettext(
		    "Cannot do a full restore of a dump from another host\n"));
		return;
	}
	if (getuid() != 0) {
		(void) printf(gettext("must be root to do a full restore\n"));
		return;
	}

#ifdef USG
	(void) sigprocmask(0, (sigset_t *)0, &myset);
	(void) sigaddset(&myset, SIGINT);
	(void) sigprocmask(SIG_BLOCK, &myset, &myset);
#else
	mymask = sigblock(sigmask(SIGINT));
#endif
	setdelays(1);
	while (p < ap->last) {
		if (read_dumps(dbserv, host, p->name, timestamp, &l)) {
			(void) fprintf(stderr,
				gettext("no dumps of `%s'\n"), p->name);
			p++;
			continue;
		} else if (l->h->dh_level != 0) {
			char pbuf[MAXPATHLEN];
			(void) sprintf(pbuf, gettext(
				"No level 0 dumps of `%s' -- restore anyway?"),
				p->name);
			if (yesorno(pbuf) == 0) {
				free_dumplist(l);
				p++;
				continue;
			}
		}
		t = l;
		while (t) {
			rc = do_fullrestore(t->h, localdir, rflag, notify);
			t = t->nxt;
			if ((t || ((p+1) < ap->last)) && rc) {
				if (yesorno(gettext(
				    "Do you wish to continue?")) == 0) {
					free_dumplist(l);
					goto done;
				}
			}
		}
		free_dumplist(l);
		p++;
	}
done:
	if (rc == 0 && rflag) {
		(void) sprintf(delpath, "%s/%s", localdir, RESTORESYMTABLE);
		(void) unlink(delpath);
	}
	(void) sprintf(delpath, "%s/%s", localdir, RESTOREDEVICE);
	(void) unlink(delpath);
	setdelays(0);
#ifdef USG
	(void) sigprocmask(SIG_SETMASK, &myset, (sigset_t *)0);
#else
	(void) sigsetmask(mymask);
#endif
}

static int
do_fullrestore(h, localdir, rflag, notify)
	struct dheader *h;
	char *localdir;
	int rflag;
	int notify;
{
	char cmdfile[256];
	char devlist[1024];
	FILE *fp;
	int pid, status;
	register int i;
	extern char *tapedev;
	int rc = 0;
#ifdef USG
	sigset_t myset;
#endif

#if 1
	/*
	 * XXX: debug only?
	 */
	if (restorepath == NULL) {
		restorepath = getenv("RESTOREPATH");
	}
#endif
	(void) sprintf(cmdfile, "/tmp/restcmd.%lu", (u_long)getpid());
	if ((fp = fopen(cmdfile, "w")) == NULL) {
		(void) fprintf(stderr, gettext("Cannot make cmdfile\n"));
		(void) unlink(cmdfile);
		return (-1);
	}

	if (notify) {
		(void) putc(NOTIFYREC, fp);
		(void) putw(notify, fp);
	}

	if (rflag)
		(void) putc(RRESTORE, fp);
	else
		(void) putc(XRESTORE, fp);
	(void) putw((int)h->dh_time, fp);
	devlist[0] = '\0';
	for (i = 0; i < h->dh_ntapes; i++) {
		(void) putc(TAPEREC, fp);
		if (fwrite(h->dh_label[i], LBLSIZE, 1, fp) != 1) {
			(void) fprintf(stderr, gettext(
				"fullrestore cmd file write error\n"));
			(void) fclose(fp);
			(void) unlink(cmdfile);
			return (-1);
		}
		if (tapedev) {
			if (devlist[0])
				(void) strcat(devlist, ",");
			(void) strcat(devlist, getdevice());
		}
	}
	(void) fclose(fp);

	pid = fork();
	if (pid == -1) {
		perror("do_fullrestore/fork");
		(void) unlink(cmdfile);
		return (-1);
	} else if (pid) {
		(void) waitpid(pid, &status, NULL);
		if (WIFSIGNALED(status)) {
			(void) printf(gettext(
			    "%s terminated by signal %d\n"),
			    "hsmrestore", WTERMSIG(status));
			rc = -1;
		} else if (WIFEXITED(status)) {
			(void) printf(gettext(
			    "%s complete.  %s exit status: %d\n"),
			    "hsmrestore", "hsmrestore", WEXITSTATUS(status));
			rc = WEXITSTATUS(status);
		} else {
			(void) printf(gettext(
			    "unknown %s termination.  status = 0x%x\n"),
			    "hsmrestore", status);
			rc = -1;
		}
		(void) unlink(cmdfile);
	} else {
		char tape_position[32];
		char restpath[MAXPATHLEN+1];
		(void) sprintf(restpath, "%s/hsmrestore", gethsmpath(sbindir));
		if (chdir(localdir)) {
			perror("restore/chdir");
			exit(1);
		}
		(void) setuid(getuid());
#ifdef USG
		(void) sigemptyset(&myset);
		(void) sigprocmask(SIG_SETMASK, &myset, (sigset_t *)0);
#else
		(void) sigsetmask(0);
#endif
		/*
		 * Restore numbers the dumps starting with 1
		 * (i.e., specifying the `s' flag with an arg of 1
		 * is the same as not specifying it at all...)
		 */
		(void) sprintf(tape_position, "%lu", h->dh_position);
		if (tapedev && devlist[0]) {
#if 1
			if (restorepath)
				(void) execl(restorepath, "hsmrestore", "Mfs",
					cmdfile, devlist, tape_position, 0);
#endif
			(void) execl(restpath, "hsmrestore", "Mfs",
				cmdfile, devlist, tape_position, 0);
			(void) execl("/usr/sbin/hsmrestore", "hsmrestore",
				"Mfs", cmdfile, devlist, tape_position, 0);
			(void) execl("/usr/etc/hsmrestore", "hsmrestore",
				"Mfs", cmdfile, devlist, tape_position, 0);
			(void) execl("/sbin/hsmrestore", "hsmrestore", "Mfs",
				cmdfile, devlist, tape_position, 0);
		} else {
#if 1
			if (restorepath)
				(void) execl(restorepath, "hsmrestore", "Ms",
					cmdfile, tape_position, 0);
#endif
			(void) execl(restpath, "hsmrestore", "Ms",
				cmdfile, tape_position, 0);
			(void) execl("/usr/sbin/hsmrestore", "hsmrestore",
				"Ms", cmdfile, tape_position, 0);
			(void) execl("/usr/etc/hsmrestore", "hsmrestore",
				"Ms", cmdfile, tape_position, 0);
			(void) execl("/sbin/hsmrestore", "hsmrestore", "Ms",
				cmdfile, tape_position, 0);
		}
		perror("execl");
		exit(1);
	}
	return (rc);
}

static struct dio {
	char	*name;
	int	state;		/* initial DIO state */
	struct dio *nxt;
} *head;

void
delay_io(path)
	char *path;
{
	register struct dio *p;

	if (getuid() != 0) {
		(void) fprintf(stderr,
			gettext("must be root to enable delayed I/O\n"));
		return;
	}
	for (p = head; p; p = p->nxt) {
		if (strcmp(p->name, path) == 0)
			return;
	}
	p = (struct dio *)malloc(sizeof (struct dio));
	if (p) {
		p->name = malloc((unsigned)(strlen(path)+1));
		if (p->name) {
			(void) strcpy(p->name, path);
			p->state = -1;
			p->nxt = head;
			head = p;
		} else {
			free((char *)p);
		}
	}
}

void
setdelays(on)
	int on;
{
	register struct dio *p, *tp;
	int fd;
	u_long val;

	val = on;
	p = head;
	while (p) {
		if ((fd = open(p->name, O_RDONLY)) != -1) {
			/*
			 * If turning on delayed I/O, record the
			 * initial state.  When we go to turn
			 * delayed I/O off, we only do so if the
			 * initial state was off.
			 */
			if (p->state == -1) {
#ifdef USG
				(void) ioctl(fd, _FIOGDIO, &p->state);
#else
				(void) ioctl(fd, FIODIOS, &p->state):
#endif
			}
			if (on || p->state == 0) {
#ifdef USG
				(void) ioctl(fd, _FIOSDIO, &val);
#else
				(void) ioctl(fd, FIODIO, &val);
#endif
			}
			(void) close(fd);
		}
		if (!on) {
			tp = p;
			p = tp->nxt;
			free(tp->name);
			free((char *)tp);
		} else {
			p = p->nxt;
		}
	}
	if (!on)
		head = (struct dio *)0;
}
