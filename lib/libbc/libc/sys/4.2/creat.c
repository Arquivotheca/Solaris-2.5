#include "chkpath.h"
#include <syscall.h>
#include <unistd.h>
#include <errno.h>
#include <sys/param.h>

extern int errno;

int creat(path, mode)
char	*path;
int	mode;
{
	CHKNULL(path);

	return(creat_com(path, mode));
}

		
