#include "chkpath.h"
#include <syscall.h>
#include <unistd.h>
#include <sys/param.h>

int access(path, mode)
char	*path;
int	mode;
{
	CHKNULL(path);

	return(access_com(path, mode));
}
