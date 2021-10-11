#include <fcntl.h>
#include <unistd.h>
#include <syscall.h>
#include <sys/errno.h>

#define OPEN_MAX 20		/* Taken from SVR4 limits.h */

int dup2(fildes, fildes2)
int fildes,		/* file descriptor to be duplicated */
    fildes2;		/* desired file descriptor */
{
      int     tmperrno;       /* local work area */
      register int open_max;  /* max open files */
      extern  int     errno;  /* system error indicator */
      int     ret;	      /* return value */
      int     fds;	      /* duplicate files descriptor */

      if ((open_max = ulimit(4, 0)) < 0)
              open_max = OPEN_MAX;    /* take a guess */

      /* Be sure fildes is valid and open */
      if (fcntl(fildes, F_GETFL, 0) == -1) {
              errno = EBADF;
              return (-1);
      }

      /* Be sure fildes2 is in valid range */
      if (fildes2 < 0 || fildes2 >= open_max) {
              errno = EBADF;
              return (-1);
      }

      /* Check if file descriptors are equal */
      if (fildes == fildes2) {
              /* open and equal so no dup necessary */
              return (fildes2);
      }
      /* Close in case it was open for another file */
      /* Must save and restore errno in case file was not open */
      tmperrno = errno;
      close(fildes2);
      errno = tmperrno;

      /* Do the dup */
      if ((ret = fcntl(fildes, F_DUPFD, fildes2)) != -1) {
          if ((fds = fd_get(fildes)) != -1)
                fd_add(fildes2, fds);
      }
      return(ret);

}
