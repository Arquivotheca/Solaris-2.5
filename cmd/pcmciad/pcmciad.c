/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident "@(#)pcmciad.c	1.21     95/03/01 SMI"

/*
 *  PCMCIA User Daemon
 */

#include	<stdio.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<sys/types.h>
#include	<sys/fcntl.h>
#include	<sys/dditypes.h>
#include	<sys/pctypes.h>
#include	<sys/stream.h>
#include	<sys/stropts.h>
#include	<sys/modctl.h>
#include	<sys/signal.h>
#include	<sys/sad.h>
#include	<sys/pem.h>
#include	<sys/pcmcia.h>
#include	<sys/sservice.h>
#include	<string.h>
#include	<ftw.h>

/* memory card support headers */
#include	<errno.h>
#include	<dirent.h>
#include	<signal.h>
#include	<limits.h>
#include	<sys/stat.h>
#include	<sys/mnttab.h>
#include	<sys/wait.h>
#include	<sys/dkio.h>
#include	<sys/procfs.h>

/* the following are defaults for the modload function */
#define	PCMCIA_ROOT_DIR		"/devices"
#define	PCMCIA_DEVICES_DIR	"pcmcia"
#define	PCMCIA_DRIVER_CLASS	"root"

/* XXX - what should this really be? */
#define	PCMCIAD_DIR	"/devices"

/* info needed to deal with modctl(MODCONFIG) call */
#define	NODE_PREPEND		".."
#define	NODE_PREPEND_LEN	2

/* device tree depth for nftw() */
#define	FT_DEPTH 		15

/* memory card */
#define	PCMEM_PATH	"/pcmem"
#define	DTYPE_PCMEM	"pcmem"
#define	DTYPE_PCRAM	"pcram"
#define	VOLDSK_PATH	"/vol/dev/dsk"
#define	VOLRDSK_PATH	"/vol/dev/rdsk"
#define	DEVDSK_PATH	"/dev/dsk"
#define	DEVRDSK_PATH	"/dev/rdsk"
#define	DEV_PCMCIA	"/devices/pcmcia"
#define	VOLD_PROC	"vold"
#define	START_CHAR	'c'
#define	PDIR		"/tmp/.pcmcia" /* piped directory */
#define	PCRAM_FILE	"/tmp/.pcmcia/pcram" /* special pipe file */

static char *pcmcia_root_dir = PCMCIA_ROOT_DIR;
static char *pcmcia_devices_dir = PCMCIA_DEVICES_DIR;

#ifndef	lint
static char *pcmcia_driver_class = PCMCIA_DRIVER_CLASS;
#endif

static char *events[] = {
	"CARD_REMOVAL",
	"CARD_INSERT",
	"CARD_READY",
	"CARD_BATTERY_WARN",
	"CARD_BATTERY_DEAD",
	"CARD_STATUS_CHANGE",
	"CARD_WRITE_PROTECT",
	"CARD_RESET",
	"CARD_UNLOCK",
	"CLIENT_INFO",
	"EJECTION_COMPLETE",
	"EJECTION_REQUEST",
	"ERASE_COMPLETE",
	"EXCLUSIVE_COMPLETE",
	"EXCLUSIVE_REQUEST",
	"INSERTION_COMPLETE",
	"INSERTION_REQUEST",
	"REGISTRATION_COMPLETE",
	"RESET_COMPLETE",
	"RESET_PHYSICAL",
	"RESET_REQUEST",
	"TIMER_EXPIRED",
	"PM_RESUME",
	"PM_SUSPEND",
	"EVENT 24",
	"EVENT 25",
	"EVENT 26",
	"EVENT 27",
	"EVENT 28",
	"EVENT 29",
	"DEV_IDENT",
	"INIT_DEV",
};

extern	char	*optarg;
extern	int	optind, opterr;
extern	int	putmsg(int, struct strbuf *, struct strbuf *, int);
extern	int	getmsg(int, struct strbuf *, struct strbuf *, int *);
extern	int	modctl(int, ...);
static void fixup_devnames(char *, char *);

/* memory card stuff */
extern	int	volmgt_running();
extern	char	*media_findname();

static int	debug = 0;
static int	num_sockets;

static char	*device = "/dev/pem";
static char	buff[4096];
static char	dbuff[4096];

void
main(int argc, char *argv[])
{
	static	void	em_init(int);
	static	void	makepdir(void);
	static	void	em_process(int);
	static	int	modload(char *);
	int		fd;
	int		c;

	opterr = 0;

	while ((c = getopt(argc, argv, "DId:")) != EOF) {
		switch (c) {
		case 'D':	/* enable debug mode */
			debug++;
			break;
		case 'd':
			device = optarg;
			break;
		case '?':
			(void) fprintf(stderr,
			    "usage: pcmcaid [-D] [-d <device>]\n");
			exit(9);
		}
	}

	if (!debug) {
		(void) close(0);
		(void) close(1);
		(void) close(2);
	} else {
		(void) printf("entering debug mode, device = [%s]\n",
		    device);
	}

	(void) chdir(PCMCIAD_DIR);

	/* force load the nexus */
	if (modload("pcmcia") < 0) {
		exit(1);
	}

	fd = open(device, O_RDWR);
	if (fd < 0) {
		perror(device);
		exit(2);
	}

	em_init(fd);

	if (num_sockets == 0) {
		/* no hardware really present so leave */
		exit(1);
	}

	if (!debug) {
		pid_t		pid;

		/* now turn into a daemon */
		(void) setsid();
		pid = fork();
		if (pid < 0) {
			exit(3);
		}

		if (pid != 0) {
			(void) close(fd);
			/*
			 * the daemon is running but may not have
			 * the devices created yet. This is only a
			 * problem if coming up on the network via
			 * PCMCIA devices.
			 * We need to delay slightly but avoid
			 * making boot take any extra time
			 */
			(void) sleep(2);
			exit(0);
		}
	}

	/* create PCMCIA pipe directory */
	makepdir();

	for (;;) {
		em_process(fd);
	}
	/*NOTREACHED*/
}


static void
em_init(int fd)
{
	u_char			*evp;
	em_init_req_t		*init;
	em_adapter_info_req_t	*info;
	em_adapter_info_ack_t	*infoack;
	struct strbuf		strbuf;
	int			flags, i;


	if (debug) {
		(void) printf("em_init(%d)\n", fd);
	}

	/* LINTED lint won't shut up about this line */
	init = (em_init_req_t *)buff;
	(void) memset(buff, '\0', sizeof (buff));

	init->em_primitive = EM_INIT_REQ;
	init->em_logical_socket = -1; /* all sockets */
	init->em_event_mask_offset = sizeof (em_init_req_t);
	init->em_event_mask_length = sizeof (u_long);

	evp = (u_char *)buff + sizeof (em_init_req_t);
	/* LINTED lint won't shut up about this line */
	*(u_long *) evp = -1;	/* all events */

	strbuf.maxlen = sizeof (em_init_req_t) + sizeof (u_long);
	strbuf.len = strbuf.maxlen;
	strbuf.buf = buff;

	if (putmsg(fd, &strbuf, NULL, 0) < 0) {
		perror("putmsg");
		exit(2);
	}

	if (debug) {
		(void) printf("\tsent EM_INIT_REQ\n");
	}

	strbuf.maxlen = sizeof (buff);
	flags = 0;
	if (getmsg(fd, &strbuf, NULL, &flags) < 0) {
		perror("getmsg");
		exit(3);
	}

	if (debug) {
		(void) printf("\treceived primitive: %d\n",
		    (int) init->em_primitive);
	}

	/* LINTED lint won't shut up about this line */
	info = (em_adapter_info_req_t *)buff;
	info->em_primitive = EM_ADAPTER_INFO_REQ;
	strbuf.maxlen = sizeof (em_adapter_info_req_t);
	strbuf.len = strbuf.maxlen;
	strbuf.buf = buff;
	if (putmsg(fd, &strbuf, NULL, 0) < 0) {
		perror("putmsg");
		exit(2);
	}

	if (debug) {
		(void) printf("\tsent EM_ADAPTER_INFO_REQ\n");
	}

	strbuf.maxlen = sizeof (buff);
	flags = 0;
	if (getmsg(fd, &strbuf, NULL, &flags) < 0) {
		perror("getmsg");
		exit(3);
	}

	/* LINTED lint won't shut up about this line */
	infoack = (em_adapter_info_ack_t *)buff;
	if (infoack->em_primitive == EM_ADAPTER_INFO_ACK) {
		if (debug) {
			(void) printf("adapter info:\n");
			(void) printf("\tsockets: %d\n",
			    (int) infoack->em_num_sockets);
			(void) printf("\twindows: %d\n",
			    (int) infoack->em_num_windows);
		}

		num_sockets = infoack->em_num_sockets;
		for (i = 0; i < num_sockets; i++) {
			em_ident_socket_req_t *ident;
			/* LINTED lint won't shut up about this line */
			ident = (em_ident_socket_req_t *)buff;
			ident->em_primitive = EM_IDENT_SOCKET_REQ;
			ident->em_socket = i;
			strbuf.maxlen = sizeof (em_ident_socket_req_t);
			strbuf.len = strbuf.maxlen;
			strbuf.buf = buff;
			if (putmsg(fd, &strbuf, NULL, 0) < 0) {
				perror("putmsg");
			}
		}
	} else {
		if (debug) {
			(void) printf("\treceived primitive: %d\n",
				(int) infoack->em_primitive);
		}
	}
}


static void
em_process(int fd)
{
	static void		unmount_pcmem(long, char *);
	static int		modload(char *);
	static void		do_init_dev(int, char *, int);
	struct strbuf		ctl, data;
	struct pcm_make_dev	*md;
	union em_primitives	*prim;
	int			flags = 0;
	char			*name;


	if (debug) {
	    (void) printf("\nem_process: waiting on getmsg ...\n\n");
	}

	ctl.maxlen = sizeof (buff);
	ctl.buf = buff;

	data.maxlen = sizeof (dbuff);
	data.buf = dbuff;
	(void) memset(buff, '\0', sizeof (buff));

	if (getmsg(fd, &ctl, &data, &flags) < 0) {
		perror("getmsg");
		return;
	}

	/* LINTED lint won't shut up about this line */
	prim = (union em_primitives *)buff;

	switch (prim->event_ind.em_primitive) {

	case EM_EVENT_IND:

		if (debug) {
			(void) printf("event: %s\n",
				events[prim->event_ind.em_event]);
		}

		switch (prim->event_ind.em_event) {

		case PCE_PM_RESUME:
			break;

		case PCE_CARD_INSERT:
			break;

		case PCE_CARD_REMOVAL:
			/*
			 * Do not need to check for DTYPE_PCRAM
			 * name this point because it will
			 * be checked in unmount_pcmem routine
			 */
			unmount_pcmem(prim->event_ind.em_logical_socket,
				DTYPE_PCMEM);
			break;

		case PCE_DEV_IDENT:
			name = ctl.buf + prim->event_ind.em_event_info_offset;
			if (strcmp(name, "*unknown*")) {
			    if (debug) {
				(void) printf("\tmodload(%s)\n", name);
			    }
			    (void) modload(name);
			}
			break;

		case PCE_INIT_DEV:
			md = (struct pcm_make_dev *) (ctl.buf +
				/* LINTED lint won't shut up about this line */
				prim->event_ind.em_event_info_offset);
			if (debug) {
				(void) printf("\tsocket=%d, op=0x%x, ",
					md->socket, md->op);
				(void) printf("dev=0x%x, type=0x%x, ",
					(int) md->dev, md->type);
				(void) printf("path=[%s]\n", md->path);
			}
			do_init_dev(md->socket, md->path, md->flags);
			break;

		}  /* switch (prim->event_ind.em_event) */

		break;

	default:
		(void) printf("pcmciad: unknown primitive %x, len=%d\n",
			(int) prim->em_primitive, ctl.len);
		break;

	}  /* switch (prim->event_ind.em_primitive) */
}

/* ARGSUSED */
static int
modload(char *name)
{
#ifdef	lint
	return (0);
#else
	struct modconfig	mc;
	int			ret;

	(void) strcpy(mc.drvname, name);
	(void) strcpy(mc.rootdir, pcmcia_root_dir);
	(void) strcpy(mc.drvclass, pcmcia_driver_class);

	mc.major = -1;
	mc.num_aliases = 0;
	mc.ap = NULL;
	mc.debugflag = 0;

	if ((ret = modctl(MODCONFIG, NULL, (caddr_t)&mc)) < 0) {
		if (debug) {
			perror("pcmciad: modctl");
		}
	}

	return (ret);
#endif
}

static void
do_init_dev(int socket, char *path, int flags)
{
	static void	run_prog(char *);
	static void	setup_autopush(char *);
	static void	signal_vold(long, char *);
	int		driver_length;
	char		*driver, *cmd, *cmd2, *dido;


	if (strlen(path) == 0) {
		return;
	}

	driver = (char *)malloc((unsigned int) strlen(path)+1);
	cmd = (char *)malloc(BUFSIZ);
	cmd2 = (char *)malloc(BUFSIZ);

	(void) strcpy(driver, path);
	driver_length = strcspn(path, "@");

	if (debug) {
	    (void) printf("\tdo_init_dev: strlen(path) = %d path = [%s]\n",
							strlen(path), path);

	    (void) printf("\tdo_init_dev: driver_length = %d\n",
							driver_length);
	} /* if debug */

	driver[driver_length] = NULL;

	if (strlen(driver) > (size_t)1) {

	    if (debug) {
		(void) printf("\tdo_init_dev: modload(%s)\n", driver);
	    }
	    (void) modload(driver);

	    if (!(flags & PCM_EVENT_MORE)) {
		(void) fixup_devnames(pcmcia_root_dir, pcmcia_devices_dir);
	    } else {
		if (debug) {
		    (void) printf("\tdo_init_dev: MORE devices coming for "
						"driver [%s]\n", driver);
		}
	    }

	    if (strcmp(driver, "pcser") == 0) {

		setup_autopush(driver);

		if (!(strcmp(&path[strlen(path)-3], ",cu"))) {
		    dido = "cua";
		} else {
		    dido = "term";
		}

		(void) sprintf(cmd, "/dev/%s/pc%d", dido, socket);
		if (debug) {
		    (void) printf("\tunlink(%s)\n", cmd);
		}
		(void) unlink(cmd);

		(void) sprintf(cmd, "%s/%s/%s", pcmcia_root_dir,
		    pcmcia_devices_dir, path);
		(void) chmod(cmd, 0666);

		(void) sprintf(cmd, "../..%s/%s/%s", pcmcia_root_dir,
		    pcmcia_devices_dir, path);
		(void) sprintf(cmd2, "/dev/%s/pc%d", dido, socket);
		if (debug) {
			(void) printf("\tsymlink(%s, %s)\n", cmd, cmd2);
		}
		(void) symlink(cmd, cmd2);

	    } else if (strcmp(driver, DTYPE_PCRAM) == 0) {

		(void) sprintf(cmd, "%s/%s/%s@%d/%s",
		    pcmcia_root_dir, pcmcia_devices_dir, DTYPE_PCMEM,
		    socket, path);

		(void) chmod(cmd, 0666);

		/* check the PCM_EVENT_MORE flag */
		if (flags & PCM_EVENT_MORE) {
			(void) printf(
				"\tMore devices coming after: [%s]\n",
				path);
		} else {
			(void) printf("\tFound last device: [%s]\n",
				path);
			run_prog("/usr/sbin/disks");
			signal_vold(socket, path);
		}
	    } else {
		run_prog("/usr/sbin/devlinks");
	    }
	}

	if (debug) {
		(void) printf("\tsocket = %d, driver = [%s]\n",
		    socket, driver);
	}

	free(cmd);
	free(cmd2);
	free(driver);
}


/*
 * Serial driver support code
 *
 */
/* ARGSUSED */
static void
setup_autopush(char *driver)
{

#ifdef	lint
	return;
#else;
	major_t			major_num;
	struct strapush		push;
	int			sadfd;


	if ((modctl(MODGETMAJBIND, driver, strlen(driver) + 1,
	    &major_num)) < 0) {
		if (debug) {
			perror("modctl(MODGETMAJBIND)");
		}
	    return;
	} else {
	    if (debug) {
		(void) printf("\tdriver = [%s], major_num = %d\n",
		    driver, (int) major_num);
	    }

	    push.sap_major = major_num;
	    push.sap_minor = 0;
	    push.sap_lastminor = 255;
	    push.sap_cmd = SAP_ALL;

	    (void) strcpy(push.sap_list[0], "ldterm");
	    (void) strcpy(push.sap_list[1], "ttcompat");
	    push.sap_npush = 2;

	    if ((sadfd = open(ADMINDEV, O_RDWR)) < 0) {
		if (debug) {
		    perror("open(ADMINDEV)");
		}
		return;
	    } else {
		if (ioctl(sadfd, SAD_SAP, &push) < 0) {
			if (debug) {
				perror("\tioctl(SAD_SAP)");
			}
		}
		(void) close(sadfd);
	    }
	}
#endif
}

/*
 * The following code is all to support the memory card driver
 */

/*
 * Create PCMCIA pipe directory
 */
static void
makepdir()
{
	extern int	errno;


	/* Make a pipe directory */
	if (debug) {
		(void) printf("\nmakepdir: \tmkdir %s\n", PDIR);
	}

	if (mkdir(PDIR, 0777) < 0 && errno != EEXIST) {
		if (debug) {
			(void) fprintf(stderr, (
		"error: can't create pipe directory %s (%s)\n"),
			    PDIR, strerror(errno));
		}
		return;
	}

	/* Make a fifo special named pipe file */
	if (debug) {
		(void) printf("\t\tmknod %s\n", PCRAM_FILE);
	}

	if (mknod(PCRAM_FILE, (mode_t)(S_IFIFO|0666), NULL) < 0) {
	    if (errno != EEXIST) {
		if (debug) {
			(void) fprintf(stderr, (
			    "error: can't create named pipe %s (%s)\n"),
			    PCRAM_FILE, strerror(errno));
		}
	    }
	}
}


/*
 * signal_vold - tell vold that a new path has been added
 */
static void
signal_vold(long socket, char *device_type)
{
	static void		wr_to_pipe(char *, char *, int);
	static const char	*get_devrdsk_path(int, char *);
	const char		*rpath;


	if (debug) {
		(void) printf("\tsignal_vold: entering for \"%s\"\n",
			device_type);
	}

#ifndef	lint
	/* Do not write to the pipe if vold is not running */
	if (volmgt_running() == 0) {
	    if (debug) {
		(void) printf("\tsignal_vold: volmgt NOT running\n");
	    }
	    return;
	}
#endif

	if ((rpath = get_devrdsk_path(socket, device_type)) == NULL) {
		/*
		 * disks(1) command does not work correctly
		 * or the devices can not be found in devfs tree
		 */
		if (debug) {
		    (void) fprintf(stderr, ("error: get NULL devpath\n"));
		}
		return;
	}

	wr_to_pipe("insert", (char *)rpath, socket);
}

/*
 * get_devrdsk - Get /dev/rdsk/cntndnsn path
 *
 * If cntn from /vol/dev/rdsk is a subset of cntndnsn
 * in /dev/rdsk directory then compare the given socketN
 * from /dev/pem with socket number in
 *	/devices/pcmcia/pcxxx@socketN/pcxxx@tN,dN:dev[,raw]
 *
 */
static const char *
get_devrdsk(long socket, char *path, char *device_type)
{
	DIR		*dskdirp;
	struct dirent	*dskentp;
	struct stat	sb;
	const char	*devp = NULL;
	char		namebuf[MAXNAMELEN];
	char		linkbuf[MAXNAMELEN];
	char		dtype[MAXNAMELEN];
	int		linksize;
	int		found = 0;


	if ((dskdirp = opendir(DEVRDSK_PATH)) == NULL) {
	    if (debug) {
		(void) printf("get_devrdsk: Error opening directory %s\n",
			DEVRDSK_PATH);
	    }
	    return (NULL);
	}

	while (dskentp = readdir(dskdirp)) {

		/* skip . and .. (and anything else starting with dot) */
		if (dskentp->d_name[0] == '.') {
			continue;
		}

		/*
		 * Silently Ignore for now any names not
		 * stating with START_CHAR
		 */
		if (dskentp->d_name[0] != START_CHAR) {
			continue;
		}

		/*
		 * Skip if path [cntn] is not a subset of
		 * dskentp->d_name [cntndnsn]
		 */
		if (strncmp(dskentp->d_name, path, strlen(path)) != 0) {
			continue;
		}

		/*
		 * found a name that matches!
		 */
		(void) sprintf(namebuf, "%s/%s", DEVRDSK_PATH,
		    dskentp->d_name);

		if (lstat(namebuf, &sb) < 0) {
		    if (debug) {
			(void) printf("get_devrdsk: Cannot stat %s\n",
				namebuf);
		    }
		    continue;
		}

		if (S_ISLNK(sb.st_mode)) {
			linksize = readlink(namebuf, linkbuf, MAXNAMELEN);
			if (linksize <= 0) {
			    if (debug) {
				(void) printf(
			    "get_devrdsk: Could not read symbolic link %s\n",
					namebuf);
			    }
			    continue;
			}
			linkbuf[linksize] = '\0';

			/* Skip [../..]/devices/pcmcia/... */
			devp = strstr(linkbuf, DEV_PCMCIA);
			if (devp == NULL) {
			    continue;
			}

			devp += strlen(DEV_PCMCIA);
			(void) sprintf(dtype, "/%s@", device_type);

			if (strncmp(devp, dtype, strlen(dtype)) == 0) {
				devp += strlen(dtype);
				if (socket == (long)atoi(devp)) {
					found++;
					break;	/* exit readdir() loop */
				}
			}
		}
	}

	(void) closedir(dskdirp);
	return (found ? namebuf : NULL);
}


/*
 * unmount_pcmem - Unmount memory card file system
 *
 * If the user accidentally removes the memory card without
 * using eject(1) command, this routine is called for unmounting
 * a mounted file system assuming that the directory is not busy
 */
static void
unmount_pcmem(long socket, char *device_type)
{
	static void	start_unmount(char *, char *);
	static FILE	*fp = NULL;
	struct mnttab	mnt;
	const char	*nvp;
	char		pname[100];
	int		isit_voldp, isit_pcmemp, isit_devp;
	int		dnlen;


	/* mtab is gone... let it go */
	if ((fp = fopen(MNTTAB, "r")) == NULL) {
		perror(MNTTAB);
		goto out;
	}

	while (getmntent(fp, &mnt) == 0) {

		isit_voldp = strncmp(mnt.mnt_special, VOLDSK_PATH,
		    strlen(VOLDSK_PATH));
		isit_pcmemp = strncmp(mnt.mnt_mountp, PCMEM_PATH,
		    strlen(PCMEM_PATH));
		isit_devp = strncmp(mnt.mnt_special, DEVDSK_PATH,
		    strlen(DEVDSK_PATH));

		/*
		 * Skip if mnt_special is not a VOLDSK_PATH
		 * or DEVDSK_PATH
		 */
		if ((isit_voldp == 0) && (isit_pcmemp == 0) ||
		    (isit_devp == 0)) {

			/* Check for cn[tn]dnsn in /dev/dsk */
			nvp = strchr(mnt.mnt_special, START_CHAR);
			(void) strcpy(pname, nvp);

			/*
			 * extract cn[tn]dnsn from
			 * /vol/dev/dsk/cn[tn]dnsn/...
			 */
			if (isit_devp) {
				dnlen = strcspn(pname, "/");
				pname[dnlen] = NULL;
			}

			if (get_devrdsk(socket, pname, device_type) != NULL) {
			    start_unmount(mnt.mnt_special, mnt.mnt_mountp);
			}
		}
	}
out:
	(void) fclose(fp);
}


/*
 * start_unmount - Start to unmount mounting directory
 *
 * Using mnt_special for unmounting vold path, and
 * mnt_mountp for regular umount(1M)
 */
static void
start_unmount(char *mnt_special, char *mnt_mountp)
{
	static int	req_vold_umount(char *);
	static int	do_umount(char *);
	int		err = 0;

	/*
	 * If vold is running we have to request the vold
	 * to unmount the file system (sigh!) in order to
	 * to clean up /vol enviroment (?)
	 */
#ifdef	lint
	/* LINTED */
	if (0) {
#else
	if (volmgt_running()) {
#endif
		if (req_vold_umount(mnt_special) == 0) {
			err++;
		}
	} else {

		/*
		 * Great! we can do a simple umount(1M)
		 * if the vold is not runing by umount <mnt_mountp>
		 * (including /pcmem/<mnt_mountp> after vold is disabled
		 *
		 * OR when the user removes the memory card WITHOUT
		 * using eject(1) command
		 */
		if (do_umount(mnt_mountp) == 0) {
			err++;
		}
	}

	if (err && debug) {
		(void) fprintf(stderr, ("start_unmount: %s is busy\n"),
		    mnt_mountp);
	}
}

/*
 * req_vold_umount - Request vold to unmount /vol/dev/rdsk/cntndnsn/..
 *
 * If vold is running, this routine is called when the user
 * removes the memory card WITHOUT using eject(1) command
 */
static int
req_vold_umount(char *path)
{
	int		fd;
	int		rval = 0;
	const char	*rawpath;



	/* Convert to "raw" path (rdsk) for DKIOCEJECT ioctl() */
#ifdef	lint
	rawpath = path;
#else
	rawpath = media_findname(path);
#endif
	if ((fd = open(rawpath, O_RDONLY|O_NDELAY)) < 0) {
		if (errno == EBUSY)
			if (debug) {
				(void) printf(
				    "\treq_vold_umount: %s is busy\n",
				    rawpath);
				perror(rawpath);
			}
		goto out;
	}

	if (debug) {
		(void) printf(
		    "\treq_vold_umount: Unmount vold path [%s]\n",
		    rawpath);
	}

	/*
	 * This simulates the volmgt eject(1) command
	 * to request the vold to eject/umount and cleanup
	 * its enviroment after unmount so we can use the same
	 * slot for different memory card
	 */
	if (ioctl(fd, DKIOCEJECT, 0) < 0) {
		/* suppose to see ENOSYS from pcmem driver */
		if (errno != ENOSYS) {
			/* Could be EBUSY [16] (Mount device busy) */
			if (debug) {
				(void) printf(
			"\treq_vold_umount: DKIOCEJECT errno [%d]\n",
				    errno);
			}
		goto out;
	    }
	}
	rval = 1;
out:
	(void) close(fd);
	return (rval);
}


/*
 * do_umount - Unmount a file system when volmgt is not running
 */
static int
do_umount(char *path)
{
	pid_t	pid;
	int	fd;


	if ((pid = fork()) == 0) {
		/* the child */
		/* get rid of those nasty err messages */
		fd = open("/dev/null", O_RDWR);
		(void) dup2(fd, 0);
		(void) dup2(fd, 1);
		(void) dup2(fd, 2);
		(void) execl("/etc/umount", "/etc/umount", path, NULL);
		(void) fprintf(stderr,
		    "pcmciad: exec of  \"/etc/umount %s\" failed; %s\n",
		    path, strerror(errno));
		return (-1);
	}

	/* the parent */
	/* wait for the umount command to exit */
	(void) waitpid(pid, NULL, 0);
	if (debug) {
		(void) printf("do_umount: \"/etc/umount %s\"\n", path);
	}

	return (1);
}


/*
 * run_prog - running system command
 */
static void
run_prog(char *path)
{
	pid_t	pid;


	if ((pid = fork()) < 0) {
		(void) fprintf(stderr, "error: can't fork (%s)\n",
		    strerror(errno));
		return;
	}

	if (pid == 0) {
		/* the child */
		if (debug) {
			(void) printf("\trun_prog: running \"%s\"\n",
			    path);
		}
		(void) execl(path, path, NULL);
		(void) fprintf(stderr,
		    "pcmciad: exec of  \"%s\" failed; %s\n", path,
		    strerror(errno));
		return;
	}
	/* the parent */
	(void) waitpid(pid, NULL, 0);
}


static void
wr_to_pipe(char *event, char *path, int socket)
{
	static int	fd = -1;
	char		buf[BUFSIZ];


	/* open a named pipe without blocking */
	if (fd < 0) {
		if ((fd = open(PCRAM_FILE, O_WRONLY | O_NDELAY)) < 0) {
			/*
			 * May be reader process does NOT open
			 * the other end ofthe pipe yet
			 */
			if (debug) {
				(void) printf(
	"wr_to_pipe: open(\"%s\") failed (errno %d, NO reader?)\n",
					PCRAM_FILE, errno);
			}
			return;
		}
	}

	(void) sprintf(buf, "%s, %s, %d", event, path, socket);
	(void) write(fd, buf, (unsigned int) strlen(buf));
	(void) write(fd, "\n", 1);
	if (debug) {
		(void) printf("\twr_to_pipe: wrote: \"%s\"\n", buf);
	}
}


/*
 * given pcmem info, return the path in /dev that matches it
 */
static const char *
get_devrdsk_path(int socket, char *dev_type)
{
	char		*res = NULL;
	char		path_buf[MAXPATHLEN+1];
	struct stat	sb_targ;
	struct stat	sb;
	DIR		*dp = NULL;
	struct dirent	*ent;


	/* get the dev_t for our target */
	(void) sprintf(path_buf, "%s/%s/%s@%d/%s", pcmcia_root_dir,
	    pcmcia_devices_dir, DTYPE_PCMEM, socket, dev_type);

	if (stat(path_buf, &sb_targ) < 0) {
		if (debug) {
			(void) printf(
		"error: can't stat %s (errno %d)\n",
				path_buf, errno);
		}
		goto dun;
	}

	/* scan the disks directory for the "right device" */
	if ((dp = opendir(DEVRDSK_PATH)) == NULL) {
		if (debug) {
			(void) printf(
			"error: can't open directory %s (errno %d)\n",
				DEVRDSK_PATH, errno);
		}
		goto dun;
	}

	while ((ent = readdir(dp)) != NULL) {
		if (ent->d_name[0] == '.') {
			continue;
		}
		(void) sprintf(path_buf, "%s/%s", DEVRDSK_PATH,
			ent->d_name);
		if (stat(path_buf, &sb) < 0) {
			if (debug) {
				(void) printf(
			"error: can't stat \"%s\" (errno %d)\n",
					path_buf, errno);
			}
			continue;
		}

		if (sb.st_rdev != sb_targ.st_rdev) {
			continue;
		}

		/* found it! */
		res = strdup(path_buf);
		if (debug) {
			(void) printf(
		"\tget_devrdsk_path: found \"%s\"\n",
				res);
		}
		break;
	}

dun:
	if (dp != NULL) {
		(void) closedir(dp);
	}
	return (res);

}

/*
 * Bug ID: 1172076 drvconfig changes permissions on existing device
 *	entries under /devices
 */
static void
fixup_devnames(char *root, char *dev)
{
	int			walk_flags = FTW_PHYS | FTW_MOUNT;
	static int		check_node(const char *, const struct stat *,
				    int, struct FTW *);
	char			*rootdir;

	if ((rootdir = (char *)malloc(strlen(root) + strlen(dev) + 2))
								== NULL) {
	    if (debug) {
		perror("pcmciad: fixup_devnames malloc");
	    }
	    return;
	}

	(void) sprintf(rootdir, "%s/%s", root, dev);

	if (debug) {
	    (void) printf("\tfixup_devnames: starting at [%s]\n", rootdir);
	}

	if (nftw(rootdir, check_node, FT_DEPTH, walk_flags) < 0) {
	    if (debug) {
		perror("pcmciad: nftw error");
	    }
	} /* if nftw */

	free(rootdir);

}

/*
 * check_node() is called by nftw().  Node contains the current
 * device node (full pathname).  If the node is a valid device file,
 * and node was just created by modctl(MODCONFIG),
 * check_node does:
 *   rename /devices/<stuff>/..<devicename>
 */
static int
check_node(const char *node, const struct stat *node_stat, int flags,
	struct FTW *ftw_info)
{
	char *i;
	static char *devname = NULL;

	if (devname == NULL) {
		/* XXX - 256 should be a symbol instead */
		if ((devname = (char *)malloc(256 + NODE_PREPEND_LEN))
		    == NULL) {
			if (debug) {
				perror("pcmciad: check_node malloc");
			}
			return (-1);
		}
	}

	if (debug) {
	    (void) printf("\tcheck_node: node = [%s]\n", node);
	}

	if ((flags == FTW_F) && ((node_stat->st_mode & S_IFCHR) ||
	    (node_stat->st_mode & S_IFBLK))) {
		if (strncmp(NODE_PREPEND, node + ftw_info->base,
		    NODE_PREPEND_LEN) == 0) {
			(void) strcpy(devname, node);
			i = devname + ftw_info->base;

			(void) sprintf(i, "%s", node + ftw_info->base +
				NODE_PREPEND_LEN);

			if (rename(node, devname) == -1) {
				if (debug) {
					(void) fprintf(stderr, "pcmciad:"
					    "rename of %s to %s failed.",
					    node, devname);
				}
				return (-1);
			} else {
			    if (debug) {
				(void) printf("\tcheck_node: new "
						"name = [%s]\n", devname);
			    }
			} /* if rename */
		}
	}
	return (0);
}
