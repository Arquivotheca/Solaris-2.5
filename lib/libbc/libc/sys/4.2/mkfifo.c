#pragma ident	"@(#)mkfifo.c	1.5	92/07/20 SMI" 

#include <sys/types.h>
#include <sys/stat.h>
#include "chkpath.h"

mkfifo(path, mode)
	char *path;
	mode_t mode;
{
	CHKNULL(path);
	return mknod(path, S_IFIFO | (mode & (S_IRWXU|S_IRWXG|S_IRWXO)));
}
