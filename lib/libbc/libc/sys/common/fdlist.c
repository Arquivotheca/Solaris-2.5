#include <stdio.h>

#define NUM_FD	16

struct fd_lst{
        int     fd[NUM_FD];                 /* list of 16 descriptors */
	int 	fds[NUM_FD];
        struct fd_lst *next;     
};
 

static struct fd_lst *fdlist = NULL;
static struct fd_lst *fdtail = NULL;


fd_init(lst)
	struct fd_lst *lst;
{
	int i;
	
	for (i=0; i<NUM_FD; i++) {
		lst->fd[i] = -1;
		lst->fds[i] = -1;
	}
	lst->next = NULL;
}


	
int fd_add(fd, fds)
int fd, fds;
{
	int i;
	struct fd_lst *fdc, *fdnew;

	fdc = fdlist;

	while (fdc != NULL) {
		for (i=0; i<NUM_FD; i++) {
			if (fdc->fd[i] == -1) {
				fdc->fd[i] = fd;
				fdc->fds[i] = fds;
				return(0);
			}	
		}
		fdc = fdc->next;
	}

	if ((fdnew = (struct fd_lst *)malloc(sizeof(struct fd_lst))) == NULL) {
		fprintf(stderr,"fd_add: malloc failed\n");
		exit(1);
	}

	fd_init(fdnew);

	if (fdlist == NULL) 
		fdlist = fdnew;
	else 
		fdtail->next = fdnew;

	fdtail = fdnew;
	fdtail->fd[0] = fd;
	fdtail->fds[0] = fds;
	return(0);
}


int fd_rem(fd)
{
	int i;
	struct fd_lst *fdc = fdlist;

	while (fdc != NULL) {
		for (i=0; i<NUM_FD; i++) {
			if (fdc->fd[i] == fd) {
				fdc->fd[i] = -1;
				fdc->fds[i] = -1;
				return(0);
			}
		}
		fdc = fdc->next;
	}
}


int fd_get(fd)
{
	int i;
	struct fd_lst *fdc = fdlist;

	while (fdc != NULL) {
		for (i=0; i<NUM_FD; i++) {
			if (fdc->fd[i] == fd) {
				return(fdc->fds[i]);
			}
		}
		fdc = fdc->next;
	}
	return(-1);
}

				


	

	

	
