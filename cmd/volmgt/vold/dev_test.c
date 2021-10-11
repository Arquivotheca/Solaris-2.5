/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)dev_test.c	1.24	95/05/26 SMI"

#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/mkdev.h>

#include	<string.h>
#include	<stdlib.h>
#include	<unistd.h>

#include	"vold.h"
#include	"multithread.h"
#include	"../test/voltestdrv.h"

static bool_t	test_use(char *, char *);
static bool_t	test_error(struct ve_error *);
static int	test_getfd(dev_t);
static void	test_close(char *);
static void	test_devmap(vol_t *, int, int);
static void	test_thread(char *);


static struct devsw testdevsw = {
	test_use,		/* d_use */
	test_error,		/* d_error */
	test_getfd,		/* d_getfd */
	NULL,			/* d_poll */
	test_devmap,		/* d_devmap */
	test_close,		/* d_close */
	NULL, 			/* d_eject */
	NULL, 			/* d_find */
	NULL,			/* d_check */
	TEST_MTYPE,		/* d_mtype */
	TEST_CLASS,		/* d_dtype */
	(ulong_t) 0 		/* d_flags */
};

bool_t
dev_init()
{
	extern void	dev_new(struct devsw *);

	dev_new(&testdevsw);
	return (TRUE);
}

struct t_priv {
	int	t_fd;
};


/*ARGSUSED*/
static bool_t
test_use(char *path, char *symname)
{
	char		namebuf[40];
	struct stat	statbuf;
	int		fd;
	int		nunits;
	int		i;
	struct devs	*dp;
	static int	in_use = 0;
	struct t_priv	*tp;
#ifdef	DEBUG
	int		ttid;			/* test thread id */
#endif



	info("test_use: %s\n", path);

	if (in_use) {
		debug(1, "test driver already in use\n");
		return (TRUE);
	}
	in_use = 1;

	(void) sprintf(namebuf, "%s/%s", path, VTCTLNAME);
	if ((fd = open(namebuf, O_RDWR)) < 0) {
		warning("test: %s; %m\n", namebuf);
		return (FALSE);
	}
	(void) fcntl(fd, F_SETFD, 1);
	(void) fstat(fd, &statbuf);


	(void) ioctl(fd, VTIOCUNITS, &nunits);
	for (i = 1; i < nunits; i++) {
		(void) sprintf(namebuf, "%s/%d", path, i);
		dp = dev_makedp(&testdevsw, namebuf);
		tp = (struct t_priv *)calloc(1, sizeof (struct t_priv));
		dp->dp_priv = (void *)tp;
		tp->t_fd = -1;
		(void) sprintf(namebuf, "/dev/test/%d", i);
		/* we only have character devices */
		dp->dp_rvn = dev_dirpath(namebuf);
	}
	if (close(fd) < 0) {
		warning("test_use: close of \"%s\" failed; %m\n", namebuf);
	}
#ifdef MT
	(void) sprintf(namebuf, "%s/%s", path, VTCTLNAME);
#ifdef	DEBUG
	ttid =
#else
	(void)
#endif
	thread_create(0, 0, (void *(*)(void *))test_thread,
	    (void *)strdup(namebuf));
#ifdef	DEBUG
	debug(6, "test_use: test_thread id %d created\n", ttid);
#endif
#endif	/* MT */

	return (TRUE);
}


static int
test_getfd(dev_t dev)
{
	struct devs 	*dp = dev_getdp(dev);
	struct t_priv 	*tp = (struct t_priv *)dp->dp_priv;


	ASSERT(tp->t_fd >= 0);
	return (tp->t_fd);
}


/*ARGSUSED*/
static bool_t
test_error(struct ve_error *vie)
{
	debug(1, "test_error\n");
	return (TRUE);
}


/*ARGSUSED*/
static void
test_close(char *path)
{
	/* do nothing */
	debug(1, "test_close: called for \"%s\" -- ignoring\n", path);
}


/*ARGSUSED*/
static void
test_devmap(vol_t *v, int part, int off)
{
	struct devs *dp = dev_getdp(v->v_basedev);

	v->v_devmap[off].dm_path = strdup(dp->dp_path);
}


static void
test_thread(char *path)
{
	extern void		vol_event(struct vioc_event *);
	extern int		vold_running;
	extern cond_t 		running_cv;
	extern mutex_t		running_mutex;
	int			fd;
	struct stat		sb;
	struct vioc_event	vie;
	struct vt_event		vte;
	struct devs		*dp;
	dev_t			dev;
	struct t_priv		*tp;


#ifdef	DEBUG
	debug(9, "test_thread: entering for \"%s\"\n", path);
#endif

	(void) mutex_enter(&running_mutex);
	while (vold_running == 0) {
		(void) cv_wait(&running_cv, &running_mutex);
	}
	(void) mutex_exit(&running_mutex);

	if ((fd = open(path, O_RDWR)) < 0) {
		warning("test driver: %s; %m\n", path);
		return;
	}
	(void) fcntl(fd, F_SETFD, 1);	/* close-on-exec */
	(void) fstat(fd, &sb);

	/*CONSTCOND*/
	while (1) {

#ifdef	DEBUG
		debug(9, "test_thread: waiting for a VT event ...\n");
#endif

		/* this ioctl blocks until an event happens */
		if (ioctl(fd, VTIOCEVENT, &vte) < 0) {
			debug(1, "test driver: VTIOCEVENT; %m\n");
			continue;
		}
		if ((vte.vte_dev == (minor_t)-1) || (vte.vte_dev == 0)) {
			/* no device -> no event */
			continue;
		}

		debug(1, "test_thread: insert on unit %d\n", vte.vte_dev);

		dev = makedev(major(sb.st_rdev), vte.vte_dev);
		dp = dev_getdp(dev);
		ASSERT(dp != NULL);

		tp = (struct t_priv *)dp->dp_priv;

		if (tp->t_fd < 0) {
#ifdef	DEBUG
			debug(9, "test_thread: opening \"%s\" RO\n",
			    dp->dp_path);
#endif
			if ((tp->t_fd = open(dp->dp_path, O_RDONLY)) < 0) {
				warning(
			"test_thread: read-only open of \"%s\" failed; %m\n",
				    dp->dp_path);
			} else {
				/* set close-on-exec */
				(void) fcntl(tp->t_fd, F_SETFD, 1);
			}
		}

		/* generate an insert event for this device */
#ifdef	DEBUG
		debug(9, "test_thread: creating insert event for (%d.%d)\n",
		    major(dev), minor(dev));
#endif
		(void) memset(&vie, 0, sizeof (struct vioc_event));
		vie.vie_type = VIE_INSERT;
		vie.vie_insert.viei_dev = dev;
		vol_event(&vie);
	}
}
