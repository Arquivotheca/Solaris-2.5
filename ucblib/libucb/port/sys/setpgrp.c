#include <sys/types.h>


setpgrp(pid1, pid2)
pid_t pid1;
pid_t pid2;
{
	return(setpgid(pid1, pid2));
}
