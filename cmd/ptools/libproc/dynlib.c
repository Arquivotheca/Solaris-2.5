#ident	"@(#)dynlib.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <link.h>
#include <libelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>
#include <sys/mkdev.h>
#include "dynlib.h"

typedef struct {
	dev_t	dev;
	ino_t	ino;
	char *	name;
} lib_t;

static	lib_t	*lib = NULL;
static	int	Nlib = 0;
static	int	nlib = 0;

void
clear_names()
{
	lib_t * lp;
	lib_t * elp;

	for (lp = &lib[0], elp = &lib[nlib]; lp < elp; lp++)
		free(lp->name);
	nlib = 0;
}

static void
add_name(dev_t dev, ino_t ino, const char * name)
{
	lib_t * lp;
	lib_t * elp;

	if (nlib >= Nlib) {
		lib_t *newlib;
		int newNlib;

		if (Nlib == 0)
			newNlib = 200;
		else
			newNlib = 2 * Nlib;
		if ((newlib = malloc(newNlib * sizeof(lib_t))) == NULL)
			return;
		if (lib) {
			(void) memcpy((char *)newlib, (char *)lib,
				Nlib*sizeof(lib_t));
			free(lib);
		}
		lib = newlib;
		Nlib = newNlib;
	}

	for (lp = &lib[0], elp = &lib[nlib]; lp < elp; lp++)
		if (lp->dev == dev && lp->ino == ino)
			break;

	/* duplicate entry? */
	if (lp != elp)
		return;

	/* new entry */
	nlib++;
	lp->dev = dev;
	lp->ino = ino;

	if ((lp->name = malloc((unsigned)strlen(name)+1)) != NULL)
		(void) strcpy(lp->name, name);
	else
		nlib--;
}

void
load_lib_name(const char * name)
{
	struct stat statb;

	if (stat(name, &statb) == 0 && S_ISREG(statb.st_mode))
		add_name(statb.st_dev, statb.st_ino, name);
}

void
load_lib_dir(const char * dir)
{
	char	name[1024];
	int	dlen;
	DIR *	dirp;
	struct dirent * dentp;

	(void) strcpy(name, dir);

	if ((dirp = opendir(name)) == NULL) {
		/* perror(name); */
		return;
	}

	dlen = strlen(name);
	name[dlen++] = '/';

	/* for each file in dir --- */
	while (dentp = readdir(dirp)) {
		char *	s;
		struct stat statb;

		if (dentp->d_name[0] != '.'
		 && (s = strstr(dentp->d_name, ".so"))
		 && (*(s += 3) == '.' || *s == '\0')
		 && strcpy(name+dlen, dentp->d_name)
		 && lstat(name, &statb) == 0
		 && S_ISREG(statb.st_mode))
			add_name(statb.st_dev, statb.st_ino, name);
	}

	(void) closedir(dirp);
}

/*
 * Thanks to Bart Smaalders for the load_ldd_names() algorithm.
 * He thanks Rod Evans for the tip regarding DT_DEBUG == &r_debugger.
 */
void
load_ldd_names(int pfd)
{
	int 		i;
	int		efd;
	size_t		size;
	Elf32_Ehdr  	elfhdr;
	Elf32_Phdr * 	proghdr = NULL;
	Elf32_Phdr * 	phdr;
	Elf32_Dyn  * 	dyn = NULL;
	Elf32_Dyn  * 	dynp;
	struct stat	estat;
	struct r_debug 	r_debugger;
	Link_map 	link_map;

	/*
	 * Open the process's executable file.
	 */
	if ((efd = ioctl(pfd, PIOCOPENM, NULL)) < 0)
		return;

	/*
	 * Get its stat structure.
	 */
	if (fstat(efd, &estat) != 0 || !S_ISREG(estat.st_mode))
		goto out;

	/*
	 * Read in the program header.
	 */
	if (pread(efd, (char *)&elfhdr, sizeof(elfhdr), 0) != sizeof(elfhdr))
		goto out;

	/*
	 * Check if ELF binary - could be a.out
	 */
	if (strncmp((char *)elfhdr.e_ident, "\177ELF", 4) != 0
	 || elfhdr.e_type != ET_EXEC
	 || elfhdr.e_version != EV_CURRENT)
		goto out;

	/*
	 * Grab program header headers.
	 */
	size = elfhdr.e_phentsize * elfhdr.e_phnum;
	if ((proghdr = malloc(size)) == NULL
	 || pread(efd, (char *)proghdr, size, elfhdr.e_phoff) != size)
		goto out;

	/*
	 * look for DYNAMIC section
	 */
	phdr = proghdr;
	for (i = 0; i < (int)elfhdr.e_phnum; i++) {
		if (phdr->p_type == PT_DYNAMIC)
			break;

		/*
		 * We increment by the size of the structure as
		 * specified in the elf header, rather than the size
		 * of the structure at compile time.  This allows the
		 * structure to grow later w/o obsoleting this binary
		 */
		/* LINTED possible alignment error */
		phdr = (Elf32_Phdr *)((char *)phdr + elfhdr.e_phentsize);
	}
	if (phdr->p_type != PT_DYNAMIC)	/* statically linked */
		goto out;

	/*
	 * Load the dynamic section from process memory.
	 *
	 * As I read the docs, I should be using the in memory size rather
	 * than the filesize.  However, the in meory size is zero, and 
	 * this works.
	 */
	size = phdr->p_filesz;
	if ((dyn = malloc(size)) == NULL
	 || pread(pfd, (void *)dyn, size, phdr->p_vaddr) != size)
		goto out;
	for (dynp = dyn, i = 0; i < size; dynp++, i += sizeof(*dynp))
		if (dynp->d_tag == DT_DEBUG)
			break;
	if (dynp->d_tag != DT_DEBUG)
		goto out;

	/*
	 * ld.so.1 patches the value of DT_DEBUG to point to the structure
	 * r_debug defined in link.h.  Load this structure from process memory.
	 */
	if (pread(pfd, &r_debugger, sizeof(r_debugger), dynp->d_un.d_ptr)
	    != sizeof(r_debugger))
		goto out;

	/*
	 * Traverse the linked list of mapped ELF objects,
	 * adding each one to the set of known libraries.
	 */
	link_map.l_next = r_debugger.r_map;
	while (link_map.l_next) {
		struct stat statb;
		char name[PATH_MAX+1];	/* should be large enough */

		name[PATH_MAX] = '\0';	/* paranoia */
		if (pread(pfd, &link_map, sizeof(link_map),
		    (long)link_map.l_next) != sizeof(link_map))
			break;
		if (read_string(pfd, name, PATH_MAX+1, (long)link_map.l_name)
		    <= 0)
			break;
		/*
		 * If the object is a regular file with a full pathname and
		 * if it isn't the executable file itself, add it to the set.
		 */
		if (name[0] == '/'
		 && stat(name, &statb) == 0
		 && S_ISREG(statb.st_mode)
		 && !(statb.st_dev == estat.st_dev
		   && statb.st_ino == estat.st_ino))
			add_name(statb.st_dev, statb.st_ino, name);
	}

out:
	if (dyn)
		free(dyn);
	if (proghdr)
		free(proghdr);
	(void) close(efd);
}

/*
 * Get the dev/ino info of the executable from the PATH.
 */
void
load_exec_name(const char *name)
{
	char	*path = NULL;
	char	*comp;
	const char	*nameptr;
	struct stat statb;
	char	buff[1024];

	if (*name == '/') {	/* fully qualified */
		if (access(nameptr = name, X_OK))
			return;
	} else {		/* not fully qualified */
		if ((path = getenv("PATH")) == NULL)
			return;
		path = strdup(path);
		for (comp = strtok(path, ":"); comp; comp = strtok(NULL, ":")) {
			strcpy(buff, comp);
			strcat(buff, "/");
			strcat(buff, name);
			if (access(buff, X_OK) == 0) {
				nameptr = buff;
				break;
			}
		}

	}

	if (stat(nameptr, &statb) == 0 && S_ISREG(statb.st_mode))
		add_name(statb.st_dev, statb.st_ino, nameptr);

	if (path)
		free(path);
	return;
}

static	lib_t	executable = { NODEV , 0 , NULL };
static	char	exec_name[512];

void
make_exec_name(const char * name)
{
	struct stat statb;

	if (stat(name, &statb) == 0 && S_ISREG(statb.st_mode)) {
		executable.dev = statb.st_dev;
		executable.ino = statb.st_ino;
		executable.name = exec_name;
		(void) strcpy(exec_name, name);
	}
}

/* returns NULL if name not found */
char *
lookup_raw_file(dev_t dev, ino_t ino)
{
	lib_t * lp;
	lib_t * elp;

	if (executable.dev == dev && executable.ino == ino)
		return executable.name;
	for (lp = &lib[0], elp = &lib[nlib]; lp < elp; lp++)
		if (lp->dev == dev && lp->ino == ino)
			return lp->name;
	return NULL;
}

/* never returns NULL */
char *
lookup_file(dev_t dev, ino_t ino)
{
	static char buf[40];
	char *name = lookup_raw_file(dev, ino);

	if (name == NULL)
		(void) sprintf(name = buf, "dev: %3d,%-3d ino: %d",
			major(dev), minor(dev), ino);
	return name;
}

char *
index_name(int index)
{
	if (index < 0 || index >= nlib)
		return NULL;
	return lib[index].name;
}
