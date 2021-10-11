#include <string.h>
#include <sys/file.h>
#include <sys/syscall.h>

execve(file, argv, arge)
char *file;
char **argv, **arge;
{
	char *c;
	char path[256];

	if (strncmp(file, "/usr/bin", strlen("/usr/bin")) == 0 ||
		strncmp(file, "/bin", strlen("/bin")) == 0) {
		if (_syscall(SYS_access, file, F_OK) == -1) {
			strcpy(path, "/usr/ucb");
			strcat(path, strrchr(file, '/'));
			file = path;
		}
	}
	else if (strncmp(file, "/usr/ucb", strlen("/usr/ucb")) == 0) { 
		strcpy(path, "/usr/bin");
		strcat(path, strrchr(file, '/'));
		if (_syscall(SYS_access, path, F_OK) == 0) 
			file = path;
	}
	else if (strncmp(file, "/usr/5bin", strlen("/usr/5bin")) == 0) {
		strcpy(path, "/usr/bin");
		strcat(path, strrchr(file, '/'));
		if (_syscall(SYS_access, path, F_OK) == 0) 
			file = path;
		else {
			strcpy(path, "/usr/ucb");
			strcat(path, strrchr(file, '/'));
			if (_syscall(SYS_access, path, F_OK) == 0) 
				file = path;
		}
	}
	
	return(_syscall(SYS_execve, file, argv, arge));
}
