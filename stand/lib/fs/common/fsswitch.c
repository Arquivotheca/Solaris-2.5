/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)fsswitch.c	1.1	94/07/01 SMI"

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/bootvfs.h>

static struct boot_fs_ops *dfl_fsw = (struct boot_fs_ops *) NULL;
static char *fsmsg = "Fstype has not been selected yet!\n";
static char *msg_noops = "not fs_ops supplied\n";

/*
 * return fs_ops pointer for a given file system name
 */

struct boot_fs_ops *
get_fs_ops_pointer(char *fsw_name)
{
	int	fsw_idx;

	for (fsw_idx = 0; fsw_idx < boot_nfsw; fsw_idx++)
		if (strcmp(boot_fsw[fsw_idx]->fsw_name, fsw_name) == 0) {
			return (boot_fsw[fsw_idx]);
		}
	return ((struct boot_fs_ops *)NULL);
}

/*
 * set default file system type
 */

void
set_default_fs(char *fsw_name)
{
	int	fsw_idx;

	for (fsw_idx = 0; fsw_idx < boot_nfsw; fsw_idx++)
		if (strcmp(boot_fsw[fsw_idx]->fsw_name, fsw_name) == 0) {
			dfl_fsw = boot_fsw[fsw_idx];
			return;
		}
	printf("Fstype <%s> is not recognized\n", fsw_name);
	prom_panic("");
}

void
boot_no_ops_void()
{
	prom_panic(msg_noops);
	/*NOTREACHED*/
}

int
boot_no_ops()
{
	prom_panic(msg_noops);
	/*NOTREACHED*/
}

int
close(int fd)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_close)(fd));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

int
mountroot(char *str)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_mountroot)(str));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

int
open(char *filename, int flags)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_open)(filename, flags));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

int
read(int fd, caddr_t buf, int size)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_read)(fd, buf, size));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

void
closeall(int flag)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL) {
		(*dfl_fsw->fsw_closeall)(flag);
		return;
	}
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

int
fstat(int fd, struct stat *buf)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_fstat)(fd, buf));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

off_t
lseek(int filefd, off_t addr, int whence)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_lseek)(filefd, addr, whence));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

/*
 * Kernel Interface
 */
int
kern_open(char *str, int flags)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_open)(str, flags));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

/*
 *  hi and lo refer to the MS end of the off_t word
 *  and the LS end of the off_t word for when we want
 *  to support 64-bit offsets.  For now, lseek() just
 *  supports 32 bits.
 */

/*ARGSUSED*/
off_t
kern_lseek(int filefd, off_t hi, off_t lo)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_lseek)(filefd, lo, 0));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

int
kern_read(int fd, caddr_t buf, u_int size)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_read)(fd, buf, size));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

int
kern_close(int fd)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_close)(fd));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}

int
kern_fstat(int fd, struct stat *buf)
{
	if (dfl_fsw != (struct boot_fs_ops *) NULL)
		return ((*dfl_fsw->fsw_fstat)(fd, buf));
	prom_panic(fsmsg);
	/*NOTREACHED*/
}
