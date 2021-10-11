/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	  All Rights Reserved
 */
#pragma ident	"@(#)paths.c	1.32	95/07/28 SMI"

/*
 * PATH setup and search directory functions.
 */
#include	<stdio.h>
#include	<limits.h>
#include	<fcntl.h>
#include	<string.h>
#include	<unistd.h>
#include	<sys/param.h>
#include	<sys/utsname.h>
#include	<sys/systeminfo.h>
#include	"_rtld.h"
#include	"profile.h"
#include	"debug.h"

#define	NODENUM	4		/* number of nodes allocated at once */

/*
 * Defines for local functions.
 */
static Pnode *	get_dir_list();
static Pnode *	make_dir_list();

/*
 * Generate a pathname from a directory and file name.
 */
const char *
so_gen_path(Pnode * dir, char * file, Rt_map * lmp)
{
	PRF_MCOUNT(44, so_gen_path);

	/*
	 * Check the filename length (there probably should be a warning
	 * here should this ever fail).
	 */
	if ((int)(strlen(file) + dir->p_len + 2) > PATH_MAX)
		return ((char *)0);
	else
		return (LM_GET_SO(lmp)(dir->p_name, file, lmp));
}


/*
 * Function to locate an object and open it;
 * Returns open file descriptor for object and full path name,
 * if successful, -1 if not successful.
 */
int
so_find(const char * file, Rt_map * lmp, const char ** path)
{
	char *		cp;
	const char *	pname;
	Pnode *		dir, * dirlist = (Pnode *)0;
	int		fd, slash = 0, flen = 0;

	PRF_MCOUNT(45, so_find);

	if (!file) {
		eprintf(ERR_FATAL, "attempt to open file with null name");
		return (-1);
	}

	/*
	 * If filename contains any '/'s, use filename itself as the pathname.
	 */
	for (cp = (char *)file; *cp; cp++) {
		if (*cp == '/')
			slash++;
	}

	flen = (cp - file) + 1; /* length includes null at end */
	if (flen >= PATH_MAX) {
		eprintf(ERR_FATAL, "file name too long: %s", file);
		return (-1);
	}
	if (slash) {
		if ((fd = open(file, O_RDONLY)) == -1) {
			if ((FLAGS(lmp) & FLG_RT_AUX) == 0)
				eprintf(ERR_FATAL, Errmsg_cofl, file, errno);
			return (-1);
		}
		if ((cp = (char *)malloc(strlen(file) + 1)) == 0) {
			(void) close(fd);
			return (-1);
		}
		(void) strcpy(cp, file);
		*path = cp;
		return (fd);
	}

	DBG_CALL(Dbg_libs_find(file));

	/*
	 * No '/' - for each directory on list, make a pathname using that
	 * directory and filename and try to open that file.
	 */
	for (dir = get_next_dir(&dirlist, lmp); dir;
	    dir = get_next_dir(&dirlist, lmp)) {
		if (dir->p_name == 0)
			continue;

		/*
		 * Generate a pathname.
		 */
		if ((pname = so_gen_path(dir, (char *)file, lmp)) == 0)
			continue;

		DBG_CALL(Dbg_libs_found(pname));
		if (rtld_flags & RT_FL_SEARCH)
			(void) printf("    trying path=%s\n", pname);

		if ((fd = open(pname, O_RDONLY)) != -1) {
			if ((cp = (char *)malloc(strlen(pname) + 1)) == 0) {
				(void) close(fd);
				return (-1);
			}
			(void) strcpy(cp, pname);
			*path = cp;
			return (fd);
		}
	}
	return (-1);
}

/*
 * Process the $PLATFORM token.
 *
 * Returns 1 if:
 *	$PLATFORM token found in the original string and
 *	*new points to string created substituting the name of this platform
 *
 * Returns 0 if:
 *	$PLATFORM token NOT found in the original string
 *
 * Returns -1 if:
 *	$PLATFORM token found in the original string and
 *	an error was encountered in processing
 */
int
do_platform_token(char * orig, char ** new)
{
	char *	c1, * c2;
	int	len;

	if (((c1 = strchr(orig, '$')) != 0) &&
	    (strncmp(c1, "$PLATFORM", sizeof ("$PLATFORM") - 1) == 0)) {

		if (new == (char **)0) {
			return (-1);
		}

		/*
		 * If this platform's name wasn't received in the auxiliary
		 * vector, then attempt to get it via sysinfo(2).
		 */
		if (platform == (char *)0) {
			if ((platform = (char *)malloc(SYS_NMLN)) == 0) {
				return (-1);
			}
			if (sysinfo((int)SI_PLATFORM, platform,
			    (long)SYS_NMLN) < 0) {
				return (-1);
			}
		}

		/*
		 * Check length of the string including null at end
		 */
		len = strlen(orig) + strlen(platform) - strlen("$PLATFORM") + 1;
		if (len >= (size_t)PATH_MAX) {
			return (-1);
		}
		if ((*new = (char *)malloc(len)) == 0) {
			return (-1);
		}

		/*
		 * Construct new pathname
		 */
		c2 = c1 + sizeof ("$PLATFORM") - 1;
		(void) strncpy(*new, orig, (size_t)c1 - (size_t)orig);
		c1 = *new + (size_t)c1 - (size_t)orig;
		(void) strcpy(c1, platform);
		(void) strcat(*new, c2);
		return (1);
	} else {
		return (0);
	}
}

/*
 * Get the next dir in the search rules path.
 */
Pnode *
get_next_dir(Pnode ** dirlist, Rt_map * lmp)
{
	static int *	rules = NULL;

	PRF_MCOUNT(46, get_next_dir);
	/*
	 * Search rules consist of one or more directories names. If this is a
	 * new search, then start at the beginning of the search rules.
	 * Otherwise traverse the list of directories that make up the rule.
	 */
	if (!*dirlist) {
		rules = LM_SEARCH_RULES(lmp);
	} else {
		if ((*dirlist = (*dirlist)->p_next) != 0)
			return (*dirlist);
		else
			rules++;
	}

	while (*rules) {
		if ((*dirlist = get_dir_list(*rules, lmp)) != 0)
			return (*dirlist);
		else
			rules++;
	}


	/*
	 * If we got here, no more directories to search, return NULL.
	 */
	return ((Pnode *) NULL);
}

/*
 * Given a search rule type, return a list of directories to search according
 * to the specified rule.
 */
static Pnode *
get_dir_list(int rules, Rt_map * lmp)
{
	Pnode *	dirlist = (Pnode *)0;

	PRF_MCOUNT(47, get_dir_list);
	switch (rules) {
	case ENVDIRS:
		/*
		 * Initialize the environment variable (LD_LIBRARY_PATH) search
		 * path list.  Note, we always call Dbg_libs_path() so that
		 * every library lookup diagnostic can be preceeded with the
		 * appropriate search path information.
		 */
		if (envdirs) {
			DBG_CALL(Dbg_libs_path(envdirs));

			/*
			 * For ldd(1) -s, indicate the search paths that'll
			 * be used.  If this is a secure program then some
			 * search paths may be ignored, therefore reset the
			 * envlist pointer each time so that the diagnostics
			 * related to these unsecure directories will be
			 * output for each image loaded.
			 */
			if (rtld_flags & RT_FL_SEARCH)
				(void) printf("    search path=%s  "
				    "(LD_LIBRARY_PATH)\n", envdirs);
			if (envlist && (rtld_flags & RT_FL_SECURE) &&
			    ((rtld_flags & RT_FL_SEARCH) || dbg_mask)) {
				(void) free(envlist);
				envlist = 0;
			}
			if (!envlist) {
				/*
				 * If this is a secure application we need to
				 * to be selective over what LD_LIBRARY_PATH
				 * directories we use.  Pass the list of
				 * trusted directories so that the appropriate
				 * security check can be carried out.
				 */
				envlist = make_dir_list(envdirs,
				    LM_SECURE_DIRS(LIST(lmp)->lm_head));
			}
			dirlist = envlist;
		}
		break;
	case RUNDIRS:
		/*
		 * Initialize the runpath search path list.  To be consistant
		 * with the debugging display of ENVDIRS (above), always call
		 * Dbg_libs_rpath().
		 */
		if (RPATH(lmp)) {
			DBG_CALL(Dbg_libs_rpath(NAME(lmp), RPATH(lmp)));

			/*
			 * For ldd(1) -s, indicate the search paths that'll
			 * be used.  If this is a secure program then some
			 * search paths may be ignored, therefore reset the
			 * runlist pointer each time so that the diagnostics
			 * related to these unsecure directories will be
			 * output for each image loaded.
			 */
			if (rtld_flags & RT_FL_SEARCH)
				(void) printf("    search path=%s  "
				    "(RPATH from file %s)\n", RPATH(lmp),
				    NAME(lmp));
			if (RLIST(lmp) && (rtld_flags & RT_FL_SECURE) &&
			    ((rtld_flags & RT_FL_SEARCH) || dbg_mask)) {
				(void) free(RLIST(lmp));
				RLIST(lmp) = 0;
			}
			if (!(RLIST(lmp)))
				RLIST(lmp) = make_dir_list(RPATH(lmp), 0);
			dirlist = RLIST(lmp);
		}
		break;
	case DEFAULT:
		dirlist = LM_DFLT_DIRS(lmp);
		/*
		 * For ldd(1) -s, indicate the default paths that'll be used.
		 */
		if (dirlist && ((rtld_flags & RT_FL_SEARCH) || dbg_mask)) {
			Pnode *	pnp = dirlist;

			if (rtld_flags & RT_FL_SEARCH)
				(void) printf("    search path=");
			for (; pnp && pnp->p_name; pnp = pnp->p_next) {
				if (rtld_flags & RT_FL_SEARCH)
					(void) printf("%s ", pnp->p_name);
				else
					DBG_CALL(Dbg_libs_dpath(pnp->p_name));
			}
			if (rtld_flags & RT_FL_SEARCH)
				(void) printf(" (default)\n");
		}
		break;
	default:
		break;
	}
	return (dirlist);
}

/*
 * Take a path specification (consists of one or more directories spearated by
 * `:') and build a list of Pnode structures.  We allocate sufficient space to
 * maintain NODENUM Pnodes together with space to hold the individual directory
 * strings each followed by a `\0'.
 */
static Pnode *
make_dir_list(const char * list, Pnode * sdir)
{
	int		flen, ndx;
	char *		dirs, * _dirs;
	Pnode *		pnp, * p1, * p2 = 0;

	PRF_MCOUNT(48, make_dir_list);

	if ((pnp = (Pnode *)malloc((2 * strlen(list)) +
	    (NODENUM * sizeof (Pnode)))) == 0)
		return ((Pnode *)0);
	_dirs = dirs = (char *)pnp + (NODENUM * sizeof (Pnode));
	for (p1 = pnp, ndx = 1; *list; p1++, ndx++, _dirs = dirs) {
		if (ndx > NODENUM) {
			/*
			 * Allocate another set of pathnodes.
			 */
			if ((p1 = (Pnode *)malloc(NODENUM *
			    sizeof (Pnode))) == 0)
				return ((Pnode *)0);
			ndx = 1;
		}
		p1->p_next = 0;
		p1->p_name = 0;
		p1->p_len = 0;

		if (*list == ';')
			++list;
		if (*list == ':') {
			*dirs++ = '.';
			flen = 1;
		} else {
			flen = 0;
			while (*list && (*list != ':') && (*list != ';')) {
				*dirs++ = *list++;
				flen++;
			}
		}
		if (*list)
			list++;
		*dirs++ = '\0';

		/*
		 * If we're only allowed to recognize secure paths make sure
		 * that the path just processed is valid.  If not reset the
		 * Pnode pointers so that this path is ignored.
		 */
		if (rtld_flags & RT_FL_SECURE) {
			Pnode *		_sdir;
			int		ok = 0;

			if (*_dirs == '/') {
				if (sdir) {
					for (_sdir = sdir;
					    (_sdir && _sdir->p_name);
					    _sdir = _sdir->p_next) {
						if (strcmp(_dirs,
						    _sdir->p_name) == 0) {
							ok = 1;
							break;
						}
					}
				} else
					ok = 1;
			}
			if (!ok) {
				DBG_CALL(Dbg_libs_ignore(_dirs));
				if (rtld_flags & RT_FL_SEARCH)
					(void) printf("    ignore path=%s  "
					    "(insecure directory name)\n",
					    _dirs);
				--p1, --ndx;
				dirs = _dirs;
				continue;
			}
		}

		p1->p_name = _dirs;
		p1->p_len = flen;
		if (p2)
			p2->p_next = p1;
		p2 = p1;
	}
	return ((Pnode *)pnp);
}
