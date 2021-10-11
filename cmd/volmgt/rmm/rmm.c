/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)rmm.c	1.33	94/11/22 SMI"


#include	<stdio.h>
#include	<stdlib.h>
#include	<fcntl.h>
#include	<dirent.h>
#include	<string.h>
#include	<errno.h>
#include	<rmmount.h>
#include	<locale.h>
#include	<libintl.h>
#include	<libgen.h>

#include	<sys/vtoc.h>
#include	<rpc/types.h>
#include	<sys/param.h>
#include	<sys/stat.h>
#include	<sys/wait.h>
#include	<sys/types.h>
#include	<sys/vol.h>
#include	<sys/fs/cachefs_fs.h>
#include	<sys/types.h>
#include	<unistd.h>

#include	"rmm_int.h"

/*
 * This program (used with volume management) will figure out
 * what file system type you have and mount it up for you at
 * the pre-defined place.
 *
 * We set the nosuid flag for security, and we set it to be read-only,
 * if the device being mounted is read-only.
 *
 */

#define	NO_SUID

#define	FSCK_CMD	"/etc/fsck"


struct ident_list **ident_list = NULL;
struct action_list **action_list = NULL;

char	*prog_name = NULL;
pid_t	prog_pid = 0;

#define	DEFAULT_CONFIG	"/etc/rmmount.conf"
#define	DEFAULT_DSODIR	"/usr/lib/rmmount"

char	*rmm_dsodir = DEFAULT_DSODIR;
char	*rmm_config = DEFAULT_CONFIG;
bool_t	rmm_debug = FALSE;

#define	SHARE_CMD	"/usr/sbin/share"
#define	UNSHARE_CMD	"/usr/sbin/unshare"



/*
 * Production (i.e. non-DEBUG) mode is very, very, quiet.  The
 * -D flag will turn on printfs.
 */



int
main(int argc, char **argv)
{
	static void			usage(void);
	static void			find_fstypes(struct action_arg **);
	static int			exec_mounts(struct action_arg **);
	static int			exec_umounts(struct action_arg **);
	static void			exec_actions(struct action_arg **,
	    bool_t);
	static struct action_arg	**build_actargs(char *);
	extern	char			*optarg;
	int				c;
	char				*path = NULL;
	char				*vact;
	int				exval = 0;
	struct action_arg		**aa;
	char				*name = getenv("VOLUME_NAME");



	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif

	(void) textdomain(TEXT_DOMAIN);

	prog_name = argv[0];
	prog_pid = getpid();

	if (geteuid() != 0) {
		(void) fprintf(stderr,
		    gettext("Must be root to execute rmm\n"));
		return (-1);
	}

	if (name == NULL) {
		dprintf("%s(%d): VOLUME_NAME was null!!\n",
		    prog_name, prog_pid);
	}

	/* back to normal now... */
	while ((c = getopt(argc, argv, "d:c:D")) != EOF) {
		switch (c) {
		case 'D':
			rmm_debug = TRUE;
			break;
		case 'd':
			rmm_dsodir = (char *)optarg;
			break;
		case 'c':
			rmm_config = (char *)optarg;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}
	}

#ifdef	DEBUG
	if (rmm_debug) {
		char	*volume_name = getenv("VOLUME_NAME");
		char	*volume_path = getenv("VOLUME_PATH");
		char	*volume_action = getenv("VOLUME_ACTION");
		char	*volume_mediatype = getenv("VOLUME_MEDIATYPE");
		char	*volume_symdev = getenv("VOLUME_SYMDEV");

		/* ensure we don't have any null env vars (name already ok) */
		if (volume_path == NULL) {
			(void) fprintf(stderr,
			    "%s(%d): VOLUME_PATH was null!!\n",
			    prog_name, prog_pid);
			return (-1);
		}
		if (volume_action == NULL) {
			(void) fprintf(stderr,
			    "%s(%d): VOLUME_ACTION was null!!\n",
			    prog_name, prog_pid);
			return (-1);
		}
		if (volume_mediatype == NULL) {
			(void) fprintf(stderr,
			    "%s(%d): VOLUME_MEDIATYPE was null!!\n",
			    prog_name, prog_pid);
			return (-1);
		}
		if (volume_symdev == NULL) {
			(void) fprintf(stderr,
			    "%s(%d): VOLUME_SYMDEV was null!!\n",
			    prog_name, prog_pid);
			return (-1);
		}

		(void) fprintf(stderr, "\n");
		(void) fprintf(stderr, "DEBUG: Env Vars:\n");
		(void) fprintf(stderr, "DEBUG:   VOLUME_NAME=%s\n",
		    volume_name);
		(void) fprintf(stderr, "DEBUG:   VOLUME_PATH=%s\n",
		    volume_path);
		(void) fprintf(stderr, "DEBUG:   VOLUME_ACTION=%s\n",
		    volume_action);
		(void) fprintf(stderr, "DEBUG:   VOLUME_MEDIATYPE=%s\n",
		    volume_mediatype);
		(void) fprintf(stderr, "DEBUG:   VOLUME_SYMDEV=%s\n",
		    volume_symdev);
		(void) fprintf(stderr, "\n");
	}
#endif	/* DEBUG */

	/* for core files */
	(void) chdir(rmm_dsodir);

	if ((path = getenv("VOLUME_PATH")) == NULL) {
		dprintf("%s(%d): VOLUME_PATH was null!!\n",
		    prog_name, prog_pid);
		return (-1);
	}

	/* build the action_arg structure. */
	if ((aa = build_actargs(path)) == NULL) {
		return (0);
	}

	if ((vact = getenv("VOLUME_ACTION")) == NULL) {
		dprintf("%s(%d): VOLUME_ACTION unspecified",
		    prog_name, prog_pid);
		return (0);
	}
	/*
	 * read in our configuration file.
	 * Builds data structures used by find_fstypes
	 * and exec_actions,  and uses the configuration
	 * file as well as command line poop.
	 */
	config_read();

	if (strcmp(vact, "insert") == 0) {

		/* insert action */

		/* premount actions */
		exec_actions(aa, TRUE);
		/*
		 * If the media is unformatted, we don't try to figure out
		 * what fstype or try to mount it.
		 */
		if (strcmp(name, "unformatted") != 0) {
			/* Find the filesystem type of each entry in the aa. */
			find_fstypes(aa);

			/* try to mount the various file systems */
			exval = exec_mounts(aa);
		}
		/* execute user's (post mount) actions */
		if (exval == 0) {
			exec_actions(aa, FALSE);
		}

	} else if (strcmp(vact, "eject") == 0) {

		/* eject action */

		exec_actions(aa, TRUE);
		if (strcmp(name, "unformatted") != 0) {
			/* try to umount the various file systems */
			exval = exec_umounts(aa);
		} else {
			exval = 0;
		}
		/*
		 * Run the actions if things were unmounted properly.
		 */
		if (exval == 0) {
			exec_actions(aa, FALSE);
		}

	} else {
		dprintf("%s(%d): unknown action type %s\n",
			prog_name, prog_pid, vact);
		exval = 0;
	}

	return (exval);
}


static void
usage()
{
	(void) fprintf(stderr, gettext("%d: usage: %s [-d filesystem_dev]\n"),
	    prog_pid, prog_name);

	exit(-1);
}


static struct action_arg **
build_actargs(char *path)
{
	struct stat		sb;
	DIR			*dirp;
	struct dirent		*dp;
	char			*mtype;
	char 			namebuf[MAXNAMELEN+1];
	int			aaoff;
	struct action_arg	**aa;



	/*
	 * Stat the file and make sure it's there.
	 */
	if (stat(path, &sb) < 0) {
		dprintf("%s(%d): fstat %s; %m\n", prog_name, prog_pid, path);
		return (NULL);
	}

	if ((mtype = getenv("VOLUME_MEDIATYPE")) == NULL) {
		dprintf("%s(%d): VOLUME_MEDIATYPE unspecified",
			prog_name, prog_pid);
		return (NULL);
	}

	/* this is the case where the device has no partitions */
	if ((sb.st_mode & S_IFMT) == S_IFBLK) {
		/*
		 * Just two action_args required here.
		 */
		aa = (struct action_arg **)calloc(2,
			sizeof (struct action_arg *));
		aa[0] = (struct action_arg *)calloc(1,
			sizeof (struct action_arg));
		aa[1] = (struct action_arg *)calloc(1,
			sizeof (struct action_arg));
		aa[0]->aa_path = strdup(path);
		aa[0]->aa_rawpath = rawpath(path);
		aa[0]->aa_media = strdup(mtype);
		aa[0]->aa_mnt = TRUE;			/* do the mount */
	} else if ((sb.st_mode & S_IFMT) == S_IFDIR) {
		/* ok, so it's a directory (i.e. device w/partitions) */
		if ((dirp = opendir(path)) == NULL) {
			dprintf("%s(%d): opendir failed on %s; %m",
				prog_name, prog_pid, path);
			return (NULL);
		}
		aaoff = 0;
		aa = (struct action_arg **)calloc(V_NUMPAR+1,
		    sizeof (struct action_arg *));
		while (dp = readdir(dirp)) {
			/* ignore "." && ".." */
			if ((strcmp(dp->d_name, ".") == 0)||
			    (strcmp(dp->d_name, "..") == 0)) {
				continue;
			}

			(void) sprintf(namebuf, "%s/%s", path, dp->d_name);

			if (stat(namebuf, &sb) < 0) {
				continue;
			}

			/*
			 * If we're looking though a raw directory,
			 * get outta here.
			 */
			if ((sb.st_mode & S_IFMT) == S_IFCHR) {
				return (NULL);
			}

			if ((sb.st_mode & S_IFMT) != S_IFBLK) {
				continue;
			}
			aa[aaoff] = (struct action_arg *)calloc(1,
				sizeof (struct action_arg));
			aa[aaoff]->aa_path = strdup(namebuf);
			aa[aaoff]->aa_media = strdup(mtype);
			aa[aaoff]->aa_partname = strdup(dp->d_name);
			aa[aaoff]->aa_rawpath = rawpath(namebuf);
			aa[aaoff]->aa_mnt = TRUE;
			/*
			 * This should never be the case, but who
			 * knows.  Let's just be careful.
			 */
			if (aaoff == V_NUMPAR) {
				break;
			}

			aaoff++;
		}
		aa[aaoff] = (struct action_arg *)calloc(1,
			sizeof (struct action_arg));

		closedir(dirp);
	} else {
		dprintf("%s(%d): %s is mode 0%o\n",
			prog_name, prog_pid, path, sb.st_mode);
		return (NULL);
	}
	return (aa);
}

static void
find_fstypes(struct action_arg **aa)
{
	extern int	audio_only(struct action_arg *);

	int		ai;
	int		fd;
	int		i, j;
	int		foundfs, foundmedia;
	int		clean;
	char		*mtype = getenv("VOLUME_MEDIATYPE");


	if (mtype == NULL) {
		dprintf("%s(%d): VOLUME_MEDIATYPE unspecified",
			prog_name, prog_pid);
		exit(-1);
	}

	if (ident_list == NULL) {
		return;
	}

	/*
	 * If it's a cdrom and it only has audio on it, don't
	 * bother trying to figure out a file system type.
	 */
	if (strcmp(mtype, "cdrom") == 0) {
		if (audio_only(aa[0]) != FALSE) {
			return;
		}
	}

	/*
	 * We leave the file descriptor open on purpose so that
	 * the blocks that we've read in don't get invalidated
	 * on close, thus wasting i/o.  The mount (or attempted mount)
	 * command later on will have access to the blocks we have
	 * read as part of identification through the buffer cache.
	 * The only *real* difficulty here is that reading from the
	 * block device means that we always read 8k, even if we
	 * really don't need to.
	 */
	for (ai = 0; aa[ai]->aa_path; ai++) {
		/*
		 * if we're not supposed to mount it, just move along.
		 */
		if (aa[ai]->aa_mnt == FALSE) {
			continue;
		}

		if ((fd = open(aa[ai]->aa_path, O_RDONLY)) < 0) {
			dprintf("%s(%d): %s; %m\n", prog_name, prog_pid,
				aa[ai]->aa_path);
			continue;
		}
		foundfs = FALSE;
		for (i = 0; ident_list[i]; i++) {
			/*
			 * Look through the list of media that this
			 * file system type can live on, and continue
			 * on if this isn't an appropriate function.
			 */
			foundmedia = FALSE;
			for (j = 0; ident_list[i]->i_media[j]; j++) {
				if (strcmp(aa[ai]->aa_media,
				    ident_list[i]->i_media[j]) == 0) {
					foundmedia = TRUE;
					break;
				}
			}
			if (foundmedia == FALSE) {
				continue;
			}
			if (ident_list[i]->i_ident == NULL) {
				/*
				 * Get the id function.
				 */
				if ((ident_list[i]->i_ident = (int (*)())
				    dso_load(ident_list[i]->i_dsoname,
				    "ident_fs", IDENT_VERS)) == NULL) {
					continue;
				}
			}
			/*
			 * Call it.
			 */
			if (((*ident_list[i]->i_ident)
			    (fd, aa[ai]->aa_rawpath, &clean, 0)) != FALSE) {
				foundfs = TRUE;
				break;
			}
		}
		if (foundfs) {
			aa[ai]->aa_type = strdup(ident_list[i]->i_type);
			aa[ai]->aa_clean = clean;
		}
		close(fd);
	}
}


/*
 * return 0 if all goes well, else return the number of problems
 */
static int
exec_mounts(struct action_arg **aa)
{
	static bool_t		cache_mount(struct action_arg *,
				    struct mount_args *);
	static bool_t		auto_mount(struct action_arg *,
				    struct mount_args *);
	static bool_t		hard_mount(struct action_arg *,
				    struct mount_args *);
	static void		share_mount(struct action_arg *,
				    struct mount_args *);
	static void		netwide_mount(struct action_arg *,
				    struct mount_args *);
	static void		clean_fs(struct action_arg *);
	int			ai;
	int			mnt_ai = -1;
	char			symname[MAXNAMELEN+1];
	char			symcontents[MAXNAMELEN];
#ifdef	notdef
	char			*mountname;
	char			*s;
#endif
	char			*symdev = getenv("VOLUME_SYMDEV");
	char			*name = getenv("VOLUME_NAME");
	struct mount_args	*ma;
	int			i;
	bool_t			result;
	int			ret_val = 0;
	char			*mntpt;



	/*
	 * Find the right mount arguments for this device.
	 */
	for (ma = 0, i = 0; mount_args && mount_args[i]; i++) {
		if (regex(mount_args[i]->ma_namerecmp, name)) {
			ma = mount_args[i];
			break;
		}
		if (regex(mount_args[i]->ma_namerecmp, symdev)) {
			ma = mount_args[i];
			break;
		}
	}

	for (ai = 0; aa[ai]->aa_path; ai++) {

		/*
		 * if a premount action told us not to mount
		 * it, don't do it.
		 */
		if (aa[ai]->aa_mnt == FALSE) {
			dprintf("%s(%d): not supposed to mount %s\n",
			    prog_name, prog_pid, aa[ai]->aa_path);
			continue;
		}

		/*
		 * ok, let's do some real work here...
		 */
		dprintf("%s(%d): %s is type %s\n", prog_name, prog_pid,
		    aa[ai]->aa_path, aa[ai]->aa_type?aa[ai]->aa_type:"data");

		if (aa[ai]->aa_type) {		/* assuming we have a type */

			/* no need to try to clean/mount if already mounted */
			if (mntpt = getmntpoint(aa[ai]->aa_path)) {
				/* already mounted on! */
#ifdef	DEBUG
				dprintf(
				"DEBUG: %s already mounted on (%s dirty)\n",
				    aa[ai]->aa_path,
				    aa[ai]->aa_clean ? "NOT" : "IS");
#endif
				free(mntpt);
				ret_val++;
				continue;
			}
#ifdef	DEBUG
			dprintf("DEBUG: %s NOT already mounted on\n",
			    aa[ai]->aa_path);
#endif

			if (aa[ai]->aa_clean == FALSE) {
				clean_fs(aa[ai]);
			}

			if (ma != NULL) {
				if (ma->ma_flags & MA_CACHE) {
					result = cache_mount(aa[ai], ma);
				} else if (ma->ma_flags & MA_AUTO) {
					result = auto_mount(aa[ai], ma);
				} else {
					result = hard_mount(aa[ai], ma);
				}
			} else {
				result = hard_mount(aa[ai], NULL);
			}
			if (result == FALSE) {
				ret_val++;
			}

			/* remember if we mount one of these guys */
			if (mnt_ai == -1) {
				if (aa[ai]->aa_mountpoint)
					mnt_ai = ai;
			}

			if (ma) {
				/*
				 * export the file system.
				 */
				if ((ma->ma_flags & MA_SHARE) ||
				    (ma->ma_flags & MA_NETWIDE)) {
					share_mount(aa[ai], ma);
				}

				if (ma->ma_flags & MA_NETWIDE) {
					netwide_mount(aa[ai], ma);
				}
			}
		}
	}

	if (mnt_ai != -1) {
#ifdef notdef
		(void) sprintf(symname, "/%s/%s", aa[mnt_ai]->aa_media,
		    symdev);
		if (aa[0]->aa_partname) {
			mountname = strdup(aa[mnt_ai]->aa_mountpoint);
			if ((s = strrchr(mountname, '/')) != NULL) {
				*s = NULLC;
			}
			(void) unlink(symname);
			(void) symlink(mountname, symname);
		} else {
			(void) unlink(symname);
			(void) symlink(aa[mnt_ai]->aa_mountpoint, symname);
		}
#else
		(void) sprintf(symcontents, "./%s", name);
		(void) sprintf(symname, "/%s/%s", aa[mnt_ai]->aa_media,
		    symdev);
		(void) unlink(symname);
		(void) symlink(symcontents, symname);
#endif
	}

	return (ret_val);
}


/*
 * Mount the file system using cachefs.  We error out to hard_mount.
 * (code stolen mostly from hard_mount).
 */
static bool_t
cache_mount(struct action_arg *aa, struct mount_args *ma)
{
	static bool_t		hard_mount(struct action_arg *,
				    struct mount_args *);
	char		buf[BUFSIZ];
	char		*targ_dir;
	bool_t		rdonly = FALSE;
	bool_t		nosuid = FALSE;
	char		lopts[MAXNAMLEN];
	char		mountpoint[MAXNAMLEN];
	struct stat 	sb;
	time_t		tloc;
	int		rval;
	int		pfd[2];
	pid_t		pid;
	int		n;
	mode_t		mpmode;
	int		fd;
	struct vioc_info vi;
	bool_t		ret_val;



	if (stat(aa->aa_path, &sb)) {
		dprintf("%s(%d): %s; %m\n",
			prog_name, prog_pid, aa->aa_path);
		return (FALSE);
	}

	/*
	 * Here, we assume that the owners permissions are the
	 * most permissive and that if he can't "write" to the
	 * device that it should be mounted readonly.
	 */
	if (sb.st_mode & S_IWUSR) {
		/*
		 * If he wants it mounted readonly, give it to him.  The
		 * default is that if the device can be written, we mount
		 * it read/write.
		 */
		if (ma && (ma->ma_flags & MA_READONLY)) {
			rdonly = TRUE;
		} else {
			rdonly = FALSE;
		}
	} else {
		rdonly = TRUE;
		/*
		 * This is a bit of a hack, but later on, the share
		 * function may need to know whether this was read only
		 * or not.  This is how we pass the information down
		 * to it.
		 */
		if (ma != NULL) {
			ma->ma_flags |= MA_READONLY;
		}
	}

	if ((strcmp(aa->aa_type, "pcfs") == 0)||
	    (strcmp(aa->aa_type, "hsfs") == 0)) {
		/* pcfs and hsfs dont support the *#&$ing nosuid flag */
		nosuid = FALSE;
	} else {
		/*
		 * If he wants it mounted suid, give it to him.  The
		 * default is nosuid.
		 */
		if (ma && (ma->ma_flags & MA_SUID)) {
			nosuid = FALSE;
		} else {
			nosuid = TRUE;
		}
	}


	/*
	 * If the file system isn't clean, we attempt a ro mount.
	 * We already tried the fsck.
	 */
	if (aa->aa_clean == FALSE) {
		rdonly = TRUE;
	}

	targ_dir = getenv("VOLUME_NAME");

	if (targ_dir == NULL) {
		(void) fprintf(stderr,
		    gettext("%s(%d): VOLUME_NAME not set for %s\n"),
		    prog_name, prog_pid, aa->aa_path);
		return (FALSE);
	}

	if (aa->aa_partname) {
		(void) sprintf(mountpoint, "/%s/%s/%s", aa->aa_media,
		    targ_dir, aa->aa_partname);
	} else {
		(void) sprintf(mountpoint, "/%s/%s", aa->aa_media, targ_dir);
	}

	/* make our mountpoint */
	(void) makepath(mountpoint, 0755);

	/*
	 * set owner and modes.
	 */
	(void) chown(mountpoint, sb.st_uid, sb.st_gid);

	mpmode = (sb.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO));
	/* read implies execute */
	if (mpmode & S_IRUSR) {
		mpmode |= S_IXUSR;
	}
	if (mpmode & S_IRGRP) {
		mpmode |= S_IXGRP;
	}
	if (mpmode & S_IROTH) {
		mpmode |= S_IXOTH;
	}

	(void) chmod(mountpoint, mpmode);

	/* wack the flags */
	(void) sprintf(lopts, "backfstype=%s", aa->aa_type);

	if (rdonly) {
		(void) strcat(lopts, ",ro");
	}

	if (nosuid) {
		(void) strcat(lopts, ",nosuid");
	}

	if (ma && *ma->ma_cacheflags) {
		/* figure out the right opts to pass to the mount command */
		cache_opts(lopts, ma->ma_cacheflags);
	} else {
		/* default options */
		(void) strcat(lopts, ",non-shared,cachedir=");
		(void) strcat(lopts, DEFAULT_CACHEDIR);
	}

	/* figger out a cacheid */
	if ((fd = open(aa->aa_rawpath, O_RDONLY)) >= 0) {
		memset(&vi, 0, sizeof (vi));
		if (ioctl(fd, VOLIOCINFO, &vi) >= 0) {
			(void) sprintf(buf, ",cacheid=0x%llx", vi.vii_id);
			(void) strcat(lopts, buf);
		} else {
			dprintf("%s(%d) info ioctl failed %m\n",
			    prog_name, prog_pid);
		}
	} else {
		dprintf("%s(%d) open %s failed %m\n",
		    aa->aa_rawpath, prog_name, prog_pid);
	}

	pipe(pfd);
	if ((pid = fork()) == 0) {
		close(pfd[1]);
		dup2(pfd[0], 0);
		dup2(pfd[0], 1);
		dup2(pfd[0], 2);
		dprintf("/etc/mount -F cachefs -o %s %s %s\n",
		    lopts, aa->aa_path, mountpoint);
		execl("/etc/mount", "/etc/mount", "-F", "cachefs",
			"-o", lopts, aa->aa_path, mountpoint, NULL);

		fprintf(stderr,
			gettext("%s(%d): exec of /etc/mount failed; %m\n"),
			prog_name, prog_pid);
		_exit(-1);
	}
	close(pfd[0]);

	/* wait for the mount command to exit */
	while (wait(&rval) != pid) {
		/* do nothing */;
	}

	if (WEXITSTATUS(rval) == 0) {

		time(&tloc);
		dprintf("%s(%d): %s is cache mounted%s%s at %s",
		    prog_name, prog_pid, mountpoint,
		    rdonly ? " (read-only)": "",
		    nosuid ? " (nosuid)": " (suid)",
		    ctime(&tloc));
		aa->aa_mnt = TRUE;
		aa->aa_mountpoint = strdup(mountpoint);
		(void) close(pfd[1]);

		ret_val = FALSE;

	} else {
		/* if there was an error, print out the mount message */
		(void) fprintf(stderr, gettext("%s(%d): mount error:\n\t"),
		    prog_name, prog_pid);
		(void) fflush(stderr);
		while ((n = read(pfd[1], buf, BUFSIZ)) > 0) {
			(void) write(fileno(stderr), buf, n);
		}
		(void) close(pfd[1]);

		aa->aa_mnt = FALSE;
		(void) rmdir(mountpoint);	/* cleanup */
		(void) fprintf(stderr,
		    gettext("%s(%d): attempting regular mount\n"),
		    prog_name, prog_pid);
		ret_val = hard_mount(aa, ma);	/* try, try again... */
	}

	/* all done */
	return (ret_val);
}


/*
 * Tell autofs about the filesystem.  Backoff is to call hard_mount
 * if nis+ or the autofs aren't running.
 */
static bool_t
auto_mount(struct action_arg *aa, struct mount_args *ma)
{
	static bool_t		hard_mount(struct action_arg *,
				    struct mount_args *);

	/*
	 * check to see if he told us to automount a netwide or
	 * shared partition.  If so, complain and punt back to hard mount.
	 */
	if ((ma->ma_flags & MA_NETWIDE) ||
	    (ma->ma_flags & MA_SHARE)) {
		(void) fprintf(stderr,
		    gettext(
		    "%s(%d): can't automount shared or netwide fs's (%s)\n"),
		    prog_name, prog_pid, aa->aa_path);
	}

	/* a bit of work left to do here... */
	(void) fprintf(stderr, gettext(
	    "%s(%d): auto option not supported; doing nornal mount (%s)\n"),
	    prog_name, prog_pid, aa->aa_path);

	return (hard_mount(aa, ma));
}


/*
 * Mount the filesystem found at "path" of type "type".
 */
static bool_t
hard_mount(struct action_arg *aa, struct mount_args *ma)
{
	char		buf[BUFSIZ];
	char		*targ_dir;
	bool_t		rdonly = FALSE;
	bool_t		nosuid = FALSE;
	char		lopts[MAXNAMLEN];
	char		mountpoint[MAXNAMLEN];
	struct stat 	sb;
	time_t		tloc;
	int		rval;
	int		pfd[2];
	pid_t		pid;
	int		n;
	mode_t		mpmode;
	bool_t		ret_val;


	if (stat(aa->aa_path, &sb)) {
		dprintf("%s(%d): %s; %m\n",
		    prog_name, prog_pid, aa->aa_path);
		ret_val = FALSE;
		goto dun;
	}

	/*
	 * Here, we assume that the owners permissions are the
	 * most permissive and that if he can't "write" to the
	 * device that it should be mounted readonly.
	 */
	if (sb.st_mode & S_IWUSR) {
		/*
		 * If he wants it mounted readonly, give it to him.  The
		 * default is that if the device can be written, we mount
		 * it read/write.
		 */
		if (ma && (ma->ma_flags & MA_READONLY)) {
			rdonly = TRUE;
		} else {
			rdonly = FALSE;
		}
	} else {
		rdonly = TRUE;
		/*
		 * This is a bit of a hack, but later on, the share
		 * function may need to know whether this was read only
		 * or not.  This is how we pass the information down
		 * to it.
		 */
		if (ma != NULL) {
			ma->ma_flags |= MA_READONLY;
		}
	}

	if ((strcmp(aa->aa_type, "pcfs") == 0) ||
	    (strcmp(aa->aa_type, "hsfs") == 0)) {
		/* pcfs and hsfs dont support the *#&$ing nosuid flag */
		nosuid = FALSE;
	} else {
		/*
		 * If he wants it mounted suid, give it to him.  The
		 * default is nosuid.
		 */
		if (ma && (ma->ma_flags & MA_SUID)) {
			nosuid = FALSE;
		} else {
			nosuid = TRUE;
		}
	}

	/*
	 * If the file system isn't clean, we attempt a ro mount.
	 * We already tried the fsck.
	 */
	if (aa->aa_clean == FALSE) {
		rdonly = TRUE;
	}

	targ_dir = getenv("VOLUME_NAME");

	if (targ_dir == NULL) {
		(void) fprintf(stderr,
		    gettext("%s(%d): VOLUME_NAME not set for %s\n"),
		    prog_name, prog_pid, aa->aa_path);
		ret_val = FALSE;
		goto dun;
	}

	if (aa->aa_partname) {
		(void) sprintf(mountpoint, "/%s/%s/%s", aa->aa_media,
		    targ_dir, aa->aa_partname);
	} else {
		(void) sprintf(mountpoint, "/%s/%s", aa->aa_media, targ_dir);
	}

	/* make our mountpoint */
	(void) makepath(mountpoint, 0755);

	if (rdonly) {
		(void) strcpy(lopts, "ro");
	} else {
		(void) strcpy(lopts, "");
	}

	if (nosuid) {
		if (strcmp(lopts, "") == 0) {
			(void) strcpy(lopts, "nosuid");
		} else {
			(void) strcat(lopts, ",nosuid");
		}
	}

	(void) pipe(pfd);
	if ((pid = fork()) == 0) {

		(void) close(pfd[1]);
		dup2(pfd[0], fileno(stdin));
		dup2(pfd[0], fileno(stdout));
		dup2(pfd[0], fileno(stderr));
		if (strcmp(lopts, "") == 0) {
			(void) execl("/etc/mount", "/etc/mount", "-F",
			    aa->aa_type, aa->aa_path, mountpoint, NULL);
		} else {
			(void) execl("/etc/mount", "/etc/mount", "-F",
			    aa->aa_type, "-o", lopts, aa->aa_path, mountpoint,
			    NULL);
		}

		(void) fprintf(stderr,
		    gettext("%s(%d): exec of /etc/mount failed; %m\n"),
		    prog_name, prog_pid);
		_exit(-1);
	}
	(void) close(pfd[0]);

	/* wait for the mount command to exit */
	if (waitpid(pid, &rval, 0) < 0) {
		dprintf("%s(%d): waitpid() failed (errno %d)\n", prog_name,
		    prog_pid, errno);
		ret_val = FALSE;
		goto dun;
	}

	if (WEXITSTATUS(rval) == 0) {
		(void) time(&tloc);
		dprintf("%s(%d): %s mounted%s%s at %s",
		    prog_name, prog_pid, mountpoint,
		    rdonly ? " (read-only)": "",
		    nosuid ? " (nosuid)": " (suid)",
		    ctime(&tloc));
		aa->aa_mnt = TRUE;
		aa->aa_mountpoint = strdup(mountpoint);
#ifdef	DEBUG
		if (rmm_debug) {
			(void) fprintf(stderr, "\n");
			(void) fprintf(stderr,
			"DEBUG: Setting u.g of \"%s\" to %d.%d (me=%d.%d)\n",
			    mountpoint, sb.st_uid, sb.st_gid,
			    getuid(), getgid());
			(void) fprintf(stderr, "\n");
		}
#endif
		/*
		 * set owner and modes.
		 */
		(void) chown(mountpoint, sb.st_uid, sb.st_gid);

		mpmode = (sb.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO));
		/* read implies execute */
		if (mpmode & S_IRUSR) {
			mpmode |= S_IXUSR;
		}
		if (mpmode & S_IRGRP) {
			mpmode |= S_IXGRP;
		}
		if (mpmode & S_IROTH) {
			mpmode |= S_IXOTH;
		}
#ifdef	DEBUG
		if (rmm_debug) {
			(void) fprintf(stderr,
			    "DEBUG: Setting mode of \"%s\" to %05o\n",
			    mountpoint, mpmode);
			(void) fprintf(stderr, "\n");
		}
#endif
		(void) chmod(mountpoint, mpmode);

		ret_val = TRUE;

	} else {
		/* if there was an error, print out the mount message */
		(void) fprintf(stderr, gettext("%s(%d): mount error:\n\t"),
		    prog_name, prog_pid);
		(void) fflush(stderr);
		while ((n = read(pfd[1], buf, BUFSIZ)) > 0) {
			(void) write(fileno(stderr), buf, n);
		}
		aa->aa_mnt = FALSE;
		(void) rmdir(mountpoint);			/* cleanup */
		ret_val = FALSE;
	}

dun:
	/* all done */
	if (pfd[1] >= 0) {
		(void) close(pfd[1]);
	}
	return (ret_val);
}


/*
 * export the filesystem
 */
static void
share_mount(struct action_arg *aa, struct mount_args *ma)
{
	extern void	share_readonly(struct mount_args *);
	extern void	quote_clean(int, char **);
	extern void	makeargv(int *, char **, char *);
	pid_t  		pid;
	int		pfd[2];
	int		rval;
	int		ac;
	char		*av[MAX_ARGC];
	char		buf[BUFSIZ];
	int		n;



	if (aa->aa_mnt == FALSE) {
		return;
	}

	/* if it's a readonly thing, make sure the share args are right */
	if (ma->ma_flags & MA_READONLY) {
		share_readonly(ma);
	}

	/* build our command line into buf */
	(void) strcpy(buf, SHARE_CMD);
	(void) strcat(buf, " ");
	(void) strcat(buf, ma->ma_shareflags);
	(void) strcat(buf, " ");
	(void) strcat(buf, aa->aa_mountpoint);

	makeargv(&ac, av, buf);
	quote_clean(ac, av);	/* clean up quotes from -d stuff... yech */

	(void) pipe(pfd);
	if ((pid = fork()) == 0) {

		(void) close(pfd[1]);
		(void) dup2(pfd[0], fileno(stdin));
		(void) dup2(pfd[0], fileno(stdout));
		(void) dup2(pfd[0], fileno(stderr));
		(void) execv(SHARE_CMD, av);
		(void) fprintf(stderr,
		    gettext("%s(%d): exec of %s failed; %m\n"),
		    prog_name, prog_pid, SHARE_CMD);
		_exit(-1);
	}
	(void) close(pfd[0]);

	/* wait for the share command to exit */
	if (waitpid(pid, &rval, 0) < 0) {
		dprintf("%s(%d): waitpid() failed (errno %d)\n",
		    prog_name, prog_pid, errno);
		return;
	}

	if (WEXITSTATUS(rval) != 0) {
		/* if there was an error, print out the mount message */
		(void) fprintf(stderr, gettext("%s(%d): share error:\n\t"),
		    prog_name, prog_pid);
		(void) fflush(stderr);
		while ((n = read(pfd[1], buf, BUFSIZ)) > 0) {
			(void) write(fileno(stderr), buf, n);
		}
	} else {
		(void) dprintf("%s(%d): %s shared\n",
		    prog_name, prog_pid, aa->aa_mountpoint);
	}
}


/*
 * unexport the filesystem.
 */
/*ARGSUSED*/
static void
unshare_mount(struct action_arg *aa, struct mount_args *ma)
{
	extern void	makeargv(int *, char **, char *);
	pid_t  		pid;
	int		pfd[2];
	int		rval;
	int		ac;
	char		*av[MAX_ARGC];
	char		buf[BUFSIZ];
	int		n;
	char		mountpoint[MAXNAMELEN];
	char		*targ_dir = getenv("VOLUME_NAME");


	/*
	 * reconstruct the mount point and hope the media's still
	 * mounted there. :-(
	 */
	if (aa->aa_partname != NULL) {
		(void) sprintf(mountpoint, "/%s/%s/%s", aa->aa_media,
		    targ_dir, aa->aa_partname);
	} else {
		(void) sprintf(mountpoint, "/%s/%s", aa->aa_media, targ_dir);
	}

	/* build our command line into buf */
	(void) strcpy(buf, UNSHARE_CMD);
	(void) strcat(buf, " ");
	(void) strcat(buf, mountpoint);

	makeargv(&ac, av, buf);

	(void) pipe(pfd);
	if ((pid = fork()) == 0) {

		(void) close(pfd[1]);

		(void) dup2(pfd[0], fileno(stdin));
		(void) dup2(pfd[0], fileno(stdout));
		(void) dup2(pfd[0], fileno(stderr));

		(void) execv(UNSHARE_CMD, av);

		(void) fprintf(stderr,
		    gettext("%s(%d): exec of %s failed; %m\n"),
		    prog_name, prog_pid, UNSHARE_CMD);
		_exit(-1);
	}
	(void) close(pfd[0]);

	/* wait for the share command to exit */
	if (waitpid(pid, &rval, 0) < 0) {
		dprintf("%s(%d): waitpid() failed (errno %d)\n",
		    prog_name, prog_pid, errno);
		return;
	}

	if (WEXITSTATUS(rval) != 0) {
		/* if there was an error, print out the mount message */
		(void) fprintf(stderr, gettext("%s(%d): unshare error:\n\t"),
		    prog_name, prog_pid);
		(void) fflush(stderr);
		while ((n = read(pfd[1], buf, BUFSIZ)) > 0) {
			(void) write(fileno(stderr), buf, n);
		}
	}

}


/*
 * Automount the file system around on the network.
 */
/*ARGSUSED*/
static void
netwide_mount(struct action_arg *aa, struct mount_args *ma)
{
	(void) fprintf(stderr,
	    gettext("%s(%d): netwide mounting not supported yet.\n"),
	    prog_name, prog_pid);
}


static void
clean_fs(struct action_arg *aa)
{
	pid_t  		pid;
	int		rval;
	struct stat	sb;


	if (stat(aa->aa_path, &sb)) {
		dprintf("%s(%d): %s; %m\n",
		    prog_name, prog_pid, aa->aa_path);
		return;
	}

	/*
	 * Here, we assume that the owners permissions are the
	 * most permissive and that if he can't "write" to the
	 * device that it should be mounted readonly.
	 */
	if ((sb.st_mode & S_IWUSR) == 0) {
		dprintf("%s(%d): %s is dirty but read-only (no fsck)\n",
		    prog_name, prog_pid, aa->aa_path);
		return;
	}

	(void) fprintf(stderr,
	    gettext("%s(%d): %s is dirty, cleaning (please wait)\n"),
	    prog_name, prog_pid, aa->aa_path);

	if ((pid = fork()) == 0) {
		int	fd;

		/* get rid of those nasty err messages */
		if ((fd = open("/dev/null", O_RDWR)) >= 0) {
			(void) dup2(fd, 0);
			(void) dup2(fd, 1);
			(void) dup2(fd, 2);
		}
		(void) execl(FSCK_CMD, FSCK_CMD, "-F", aa->aa_type, "-o",
		    "p", aa->aa_path, NULL);
		(void) fprintf(stderr,
		    gettext("%s(%d): exec of %s failed; %m\n"),
		    prog_name, prog_pid, FSCK_CMD);
		_exit(-1);
	}

	/* wait for the fsck command to exit */
	if (waitpid(pid, &rval, 0) < 0) {
		(void) fprintf(stderr,
		    gettext("%s(%d): can't wait for pid %d (%s)\n"),
		    pid, FSCK_CMD);
		    return;
	}

	if (WEXITSTATUS(rval) != 0) {
		(void) fprintf(stderr,
		    gettext("%s(%d): fsck -p of %s failed, error code %d\n"),
		    prog_name, prog_pid, aa->aa_path, WEXITSTATUS(rval));
	} else {
		aa->aa_clean = TRUE;
	}
}


/*
 * This is fairly irritating.  The biggest problem is that if
 * there are several things to umount and one of them is busy,
 * there is no easy way to mount the other file systems back up
 * again.  So, the semantic that I implement here is that we
 * umount ALL umountable file systems, and return failure if
 * any file systems in "aa" cannot be umounted.
 */
static bool_t
exec_umounts(struct action_arg **aa)
{
	static bool_t		umount_fork(char *);
	int			ai;
	int			i;
	bool_t			fail = FALSE;
	char			*mountpoint, *oldmountpoint = NULL;
	char			symname[MAXNAMELEN+1];
	char			*symdev = getenv("VOLUME_SYMDEV");
	char			*name = getenv("VOLUME_NAME");
	char			*s;
	struct mount_args	*ma;



	/*
	 * Find the right mount arguments for this device.
	 */
	for (ma = 0, i = 0; mount_args && mount_args[i]; i++) {

		if (regex(mount_args[i]->ma_namerecmp, name) != NULL) {
			ma = mount_args[i];
			break;
		}

		if (regex(mount_args[i]->ma_namerecmp, symdev) != NULL) {
			ma = mount_args[i];
			break;
		}
	}

	for (ai = 0; aa[ai]->aa_path; ai++) {

		/*
		 * If it's not in the mount table, we assume it's
		 * not mounted.  Obviously, mnttab must be kept up
		 * to date in all cases for this to work properly
		 */
		if ((mountpoint = getmntpoint(aa[ai]->aa_path)) == NULL) {
			continue;
		}

		/* unshare the mount before umounting */
		if ((ma != NULL) &&
		    ((ma->ma_flags & MA_SHARE) ||
		    (ma->ma_flags & MA_NETWIDE))) {
			unshare_mount(aa[ai], ma);
		}

		/*
		 * do the actual umount.
		 */
		if (umount_fork(mountpoint) == FALSE) {
			fail = TRUE;
		}

		/* remove the mountpoint, if it's a partition */
		if (aa[ai]->aa_partname) {
			(void) rmdir(mountpoint);
		}

		/* save a good mountpoint */
		if (oldmountpoint == NULL) {
			oldmountpoint = strdup(mountpoint);
		}
		free(mountpoint);
		mountpoint = NULL;
	}

	/*
	 * clean up our directories and such if all went well.
	 */
	if (!fail) {
		/*
		 * if we have partitions, we'll need to remove the last
		 * component of the path.
		 */
		if (aa[0]->aa_partname) {
			if ((s = strrchr(oldmountpoint, '/')) != NULL) {
				*s = NULLC;
			}
		}

		(void) rmdir(oldmountpoint);
		free(oldmountpoint);

		(void) sprintf(symname, "/%s/%s", aa[0]->aa_media, symdev);
		(void) unlink(symname);
	}

	return (fail);
}


static bool_t
umount_fork(char *path)
{
	int	pid;
	int	rval;


	if ((pid = fork()) == 0) {
		int	fd;
		/* get rid of those nasty err messages */
		if ((fd = open("/dev/null", O_RDWR)) >= 0) {
			(void) dup2(fd, 0);
			(void) dup2(fd, 1);
			(void) dup2(fd, 2);
		}
		(void) execl("/etc/umount", "/etc/umount", path, NULL);
		(void) fprintf(stderr,
		    gettext("%s(%d): exec of /etc/umount failed; %m\n"),
		    prog_name, prog_pid);
		_exit(-1);
		/*NOTREACHED*/
	}
	/* wait for the umount command to exit */
	(void) waitpid(pid, &rval, 0);


	if (WEXITSTATUS(rval) != 0) {
		(void) fprintf(stderr, gettext(
		    "%s(%d): umount %s failed, error code %d\n"),
		    prog_name, prog_pid, path, WEXITSTATUS(rval));
		return (FALSE);
	}
	return (TRUE);
}


static void
exec_actions(struct action_arg **aa, bool_t premount)
{
	int		(*act_func)();
	bool_t		rval;
	int		i;
	static int	no_more_actions = FALSE;


	if (action_list == NULL) {
		return;
	}

	if (aa[0]->aa_path == NULL) {
		return;
	}

	if (no_more_actions) {
		return;
	}

	for (i = 0; action_list[i]; i++) {

		if (strcmp(aa[0]->aa_media, action_list[i]->a_media) != 0) {
			continue;
		}

		/*
		 * if we're doing premount actions, don't execute ones
		 * without the premount flag set.
		 */
		if (premount && ((action_list[i]->a_flag & A_PREMOUNT) == 0)) {
			continue;
		}

		/*
		 * don't execute premount actions if we've already done
		 * the mount.
		 */
		if (!premount && (action_list[i]->a_flag & A_PREMOUNT)) {
			continue;
		}

		/*
		 * Get the action function.
		 */
		if ((act_func =
		    (bool_t (*)()) dso_load(action_list[i]->a_dsoname,
		    "action", ACT_VERS)) == NULL) {
			continue;
		}

		/*
		 * Call it.
		 */
		rval = (*act_func)(aa, action_list[i]->a_argc,
		    action_list[i]->a_argv);

		/*
		 * TRUE == don't execute anymore actions.
		 */
		if (rval) {
			no_more_actions = TRUE;
			break;
		}
	}
}
