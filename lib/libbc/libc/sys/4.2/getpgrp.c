int getpgrp(pid)
int pid;
{
	return(getpgid(pid));
}

