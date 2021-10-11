int setpgrp(pid, pgrp)
int pid, pgrp;
{
	if ((pgrp == 0) && (pid == getpid())) {
		return(bc_setsid());
	} else
		return(setpgid(pid, pgrp));
}


