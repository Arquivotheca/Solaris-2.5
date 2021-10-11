/*
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)profile.c	1.13	95/01/04 SMI"

/*
 * Routines to provide profiling of ld.so itself, or any shared libraries
 * required by the called executable.
 */
#include	<stdio.h>
#include	<fcntl.h>
#include	<sys/mman.h>
#include	<unistd.h>
#include	<string.h>
#include	<sys/stat.h>
#include	<synch.h>
#include	<signal.h>
#include	<errno.h>
#include	<synch.h>
#include	"_rtld.h"
#include	"_elf.h"
#include	"profile.h"
#include	"debug.h"

#ifdef	PROF

static const char
	* Errmsg_ctfl =	"%s: can't truncate file: error=%d",
	* Errmsg_cgpb = "%s: failed to 'grow' profiling buffer;\n"
			"\tthis could be fatal";

char *		profile_name = 0;	/* file to be profiled */
char *		profile_dir = PRF_DEFDIR; /* profile buffer directory */

#ifdef	PROF_RTLD
int		profile_rtld = 0;	/* Indicate rtld is being profiled */
					/*	for LD_MCOUNT test */
#endif

static char *	Profile = 0;		/* Profile pathname */
static L_hdr *	Hptr;			/* profile buffer header pointer */
static L_cgarc *Cptr;			/* profile buffer call graph pointer */
static caddr_t	Hpc, Lpc;		/* Range of addresses being monitored */
static int	Fsize;			/* Size of mapped in profile buffer */

int
profile(const char * file, void * vlmp)
{
	int		hsize;		/* struct hdr size */
	int		psize;		/* profile histogram size */
	int		csize;		/* call graph array size */
	int		msize;		/* size of memory being profiled */
	caddr_t		lpc;		/* low pc */
	caddr_t		hpc;		/* high pc */
	unsigned int	nsym;		/* number of global symbols */
	int		fd;
	caddr_t		addr;
	struct stat	status;
	int		bind;
	int		new_buffer = 0;
	sigset_t	mask;
	Rt_map *	lmp = (Rt_map *)vlmp;


	lpc = (caddr_t)ADDR(lmp);
	hpc = (caddr_t)ETEXT(lmp);
	nsym = HASH(lmp)[1];

	if (Profile == 0) {
		char *	tmp;
		/*
		 * From the basename of the specified filename generate
		 * the appropriate profile buffer name.  The profile
		 * file is created if it does not already exist.
		 */
		if (((tmp = strrchr(file, '/')) != 0) && (*(++tmp)))
			file = (const char *)tmp;

		if ((Profile = (char *)malloc(strlen(file) +
		    strlen(profile_dir) + PRF_FILESIZE)) == 0) {
			eprintf(ERR_WARNING, "malloc failed, "
			    "profiling disabled");
			return (0);
		}

		(void) sprintf(Profile, PRF_FILEFORM, profile_dir, file);
	}

	if ((fd = open(Profile, (O_RDWR | O_CREAT), 0666)) == -1) {
		eprintf(ERR_WARNING, Errmsg_cofl, Profile, errno);
		return (0);
	}

	/*
	 * Determine the (minimum) size of the buffer to allocate
	 */
	Lpc = lpc = (caddr_t)PRF_ROUNDWN(lpc, sizeof (int));
	Hpc = hpc = (caddr_t)PRF_ROUNDUP(hpc, sizeof (int));

	hsize = sizeof (L_hdr);
	msize = (int)(hpc - lpc);
	psize = PRF_ROUNDUP((msize / PRF_BARSIZE), sizeof (int));
	csize = (nsym + 1) * PRF_CGINIT * sizeof (L_cgarc);
	Fsize = (hsize + psize + csize);

	/*
	 * If the file size is zero (ie. we just created it), truncate it
	 * to the minimum size.
	 */
	(void) stat(Profile, &status);
	if (status.st_size == 0) {
		if (ftruncate(fd, Fsize) == -1) {
			eprintf(ERR_WARNING, Errmsg_ctfl, Profile, errno);
			(void) close(fd);
			return (0);
		}
		new_buffer++;
	} else
		Fsize = status.st_size;

	/*
	 * Map the file in.
	 */
	if ((addr = (caddr_t)mmap(0, Fsize, (PROT_READ | PROT_WRITE),
	    /* LINTED */
	    MAP_SHARED, fd, 0)) == (char *)-1) {
		eprintf(ERR_WARNING, Errmsg_cmfl, Profile, errno);
		(void) close(fd);
		return (0);
	}
	(void) close(fd);

	/*
	 * Initialize the remaining elements of the header.  All pc addresses
	 * that are recorded are relative to zero thus allowing the recorded
	 * entries to be correlated with the symbols in the original file,
	 * and to compensate for any differences in where the file is mapped.
	 * If the high pc address has been initialized from a previous run,
	 * and the new entry is different from the original then a new library
	 * must have been installed.  In this case bale out.
	 */
	/* LINTED */
	Hptr = (L_hdr *)addr;
	profilelock = (mutex_t *)&Hptr->hd_mutex;
	if (new_buffer)
		profilelock->flags.type = USYNC_PROCESS;

	if ((bind = bind_guard(THR_FLG_BIND)) == 1) {
		(void) rt_mutex_lock((mutex_t *)&Hptr->hd_mutex, &mask);
	}
	if (Hptr->hd_hpc) {
		if (Hptr->hd_hpc != (caddr_t)(hpc - lpc)) {
			eprintf(ERR_WARNING, "%s: file being "
			    "profiled has changed size;\n"
			    "\trecreate profile buffer", Profile);
			(void) munmap((caddr_t) Hptr, (size_t) Fsize);
			if (bind) {
				(void) rt_mutex_unlock((mutex_t *)&Hptr->
				    hd_mutex, &mask);
				(void) bind_clear(THR_FLG_BIND);
			}
			return (0);
		}
	} else {
		/*
		 * Initialize the header information as we must have just
		 * created the output file.
		 */
		Hptr->hd_magic = (unsigned long)PRF_MAGIC;
		Hptr->hd_version = (unsigned long)PRF_VERSION;
		Hptr->hd_hpc = (caddr_t)(hpc - lpc);
		Hptr->hd_psize = psize;
		Hptr->hd_fsize = Fsize;
		Hptr->hd_ncndx = nsym;
		Hptr->hd_lcndx = (nsym + 1) * PRF_CGINIT;
	}
	if (bind) {
		/* LINTED */
		(void) rt_mutex_unlock((mutex_t *)&Hptr->hd_mutex, &mask);
		(void) bind_clear(THR_FLG_BIND);
	}
	/* LINTED */
	Cptr = (L_cgarc *)(addr + hsize + psize);

	/*
	 * allocate the dynamic plt's, we allocate one for each
	 * global symbol in the shared object being profiled.
	 */
	if ((DYNPLT(lmp) = calloc(nsym, M_DYN_PLT_ENT)) == 0)
		return (0);

	/*
	 * Turn on profiling
	 */
	/* LINTED */
	profil((unsigned short *)(addr + hsize),
		psize, (unsigned int)lpc, (unsigned int) PRF_SCALE);

	return (FLG_RT_PROFILE);
}


void
profile_close(void * vlmp)
{
	Rt_map *	lmp = (Rt_map *)vlmp;

	/*
	 * Turn profil() off.
	 */
	profil(0, 0, 0, 0);

	profilelock = 0;

	if (DYNPLT(lmp))
		free(DYNPLT(lmp));
	(void) munmap((caddr_t) Hptr, (size_t) Fsize);
}


static void
remap_profile(int fd)
{
	caddr_t		addr;
	int		l_fsize;

	l_fsize = Hptr->hd_fsize;

	if (munmap((caddr_t) Hptr, (size_t) Fsize) == -1) {
		eprintf(ERR_FATAL, Errmsg_cgpb, Profile);
		exit(1);
	}

	Fsize = l_fsize;
	if ((addr = (caddr_t)mmap(0, Fsize, (PROT_READ | PROT_WRITE),
	    MAP_SHARED, fd, 0)) == (char *)-1) {
		eprintf(ERR_FATAL, Errmsg_cgpb, Profile);
		exit(1);
	}
	/* LINTED */
	Hptr = (L_hdr*) addr;
	profilelock = (mutex_t *)&Hptr->hd_mutex;
	/* LINTED */
	Cptr = (L_cgarc *)(addr + sizeof (L_hdr) + Hptr->hd_psize);
}


/*
 * Update a call graph arc entry.  This routine can be called three ways;
 * 	o	On initialization from one of the bndr() functions.
 *		In this case the `to' address is known, and may be used to
 *		initialize the call graph entry if this function has not
 *		been entered before.
 *	o	On initial relocation (ie. LD_BIND_NOW). In this case the `to'
 *		address is known but the `from' isn't.  The call graph entry
 *		is initialized to hold this dummy `to' address, but will be
 *		re-initialized later when a function is first called.
 *	o	From an initialized plt entry.  When profiling, the plt entries
 *		are filled in with the calling functions symbol index and
 *		the plt_cg_elf interface function.  This interface function
 *		calls here to determine the `to' functions address, and in so
 *		doing increments the call count.
 */
caddr_t
/* VARARGS2 */
plt_cg_interp(int ndx, caddr_t from, caddr_t to)
{
	L_cgarc *	cptr, cbucket;
	int		bind;
	sigset_t	mask;

	/*
	 * If the from address is outside of the address range being profiled,
	 * simply assign it to the `outside' address.
	 */
	if (from != PRF_UNKNOWN) {
		if ((from > Hpc) || (from < Lpc))
			from = PRF_OUTADDR;
		else
			from = (caddr_t)(from - Lpc);
	}

	if ((bind = bind_guard(THR_FLG_BIND)) == 1) {
		(void) rt_mutex_lock((mutex_t *)&Hptr->hd_mutex, &mask);
	}
	/*
	 * Has the buffer grown since last we looked at it (another processes
	 * could have grown it...).
	 */
	if (Hptr->hd_fsize != Fsize) {
		int fd;
		fd = open(Profile, O_RDWR, 0);
		remap_profile(fd);
		(void) close(fd);
	}

	cptr = &Cptr[ndx];

	if (cptr->cg_to == 0) {
		/*
		 * If this is the first time this function has been called we
		 * got here from one of the binders or an initial relocation
		 * (ie. LD_BIND_NOW).  In this case the `to' address is
		 * provided.  Initialize this functions call graph entry with
		 * the functions address (retained as a relative offset).
		 * If we know where the function call originated from
		 * initialize the count field.
		 */
		cptr->cg_to = (caddr_t)(to - Lpc);
		cptr->cg_from = from;
		if (from != PRF_UNKNOWN)
			cptr->cg_count = 1;
	} else {
		/*
		 * If a function has been called from a previous run, but we
		 * don't know where we came from (ie. LD_BIND_NOW), then later
		 * calls through the plt will be able to obtain the required
		 * functions address, thus there is no need to proceed further.
		 */
		if (from != PRF_UNKNOWN) {
			/*
			 * If the from addresses match simply bump the count.
			 * If not scan the link list to find a match for this
			 * `from' address.  If one doesn't exit create a new
			 * entry and link it in.
			 */
			while ((cptr->cg_from != from) &&
				(cptr->cg_from != PRF_UNKNOWN)) {
				if (cptr->cg_next != 0)
					cptr = &Cptr[cptr->cg_next];
				else {
					to = cptr->cg_to;
					cptr->cg_next = Hptr->hd_ncndx++;
					cptr = &Cptr[cptr->cg_next];
					/*
					 * If we've run out of file, extend it.
					 */
					if (Hptr->hd_ncndx == Hptr->hd_lcndx) {
						caddr_t	addr;
						int	fd;

						Hptr->hd_fsize += PRF_CGNUMB *
						    sizeof (L_cgarc);
						fd = open(Profile, O_RDWR, 0);
						if (ftruncate(fd,
						    Hptr->hd_fsize) == -1) {
							eprintf(ERR_FATAL,
							    Errmsg_ctfl,
							    Profile, errno);
							(void) close(fd);
							cptr = &cbucket;
						}
						/*
						 * Since the buffer will be
						 * remapped, we need to be
						 * prepared to adjust cptr.
						 */
						addr = (caddr_t)((int)cptr -
						    (int)Cptr);
						remap_profile(fd);
						cptr = (L_cgarc *)((int)addr +
						    (int)Cptr);
						(void) close(fd);
						Hptr->hd_lcndx += PRF_CGNUMB;
					}
					cptr->cg_from = from;
					cptr->cg_to = to;
				}
			}
			/*
			 * If we're updating an entry from an unknown call
			 * address initialize this element, otherwise
			 * increment the call count.
			 */
			if (cptr->cg_from == PRF_UNKNOWN) {
				cptr->cg_from = from;
				cptr->cg_count = 1;
			} else
				cptr->cg_count++;
		}
	}
	/*
	 * Return the real address of the function.
	 */
	if (bind) {
		(void) rt_mutex_unlock((mutex_t *)&Hptr->hd_mutex, &mask);
		(void) bind_clear(THR_FLG_BIND);
	}
	DBG_CALL(Dbg_bind_profile(ndx, cptr->cg_count));
	return ((caddr_t)((int)cptr->cg_to + (int)Lpc));
}

#endif
