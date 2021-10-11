#include <syscall.h>
#include <unistd.h>
#include <sys/param.h>

int access(path, mode)
char	*path;
int	mode;
{
	return(access_com(path, mode));
}

