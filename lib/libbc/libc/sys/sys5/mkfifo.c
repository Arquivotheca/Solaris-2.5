#pragma ident	"@(#)mkfifo.c	1.2	92/07/20 SMI" 

#include <sys/types.h>
#include <sys/stat.h>

mkfifo(path, mode)
	char *path;
	mode_t mode;
{
	return mknod(path, S_IFIFO | (mode & (S_IRWXU|S_IRWXG|S_IRWXO)));
}
