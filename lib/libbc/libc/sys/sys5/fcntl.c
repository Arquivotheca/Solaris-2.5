#include <sys/syscall.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/filio.h>
#include <sys/ioccom.h>
#include <unistd.h>

/* The following is an array of fcntl commands. The numbers listed
 * below are from SVR4. Array is indexed with SunOS 4.1 numbers to
 * obtain the SVR4 numbers.
 */
int cmd_op[14] = {0, 1, 2, 3, 4, 23, 24, 14, 6, 7, 21, 20, -1, 22};

/* SVR4/SunOS 5.0 equivalent modes */
#define N_O_NDELAY	0x04
#define N_O_SYNC	0x10
#define N_O_NONBLOCK	0x80
#define N_O_CREAT	0x100
#define N_O_TRUNC	0x200
#define N_O_EXCL	0x400

#define	S5_FASYNC 0x1000

/* from SVR4 stropts.h */
#define	S5_S_RDNORM		0x0040
#define	S5_S_WRNORM		0x0004
#define	S5_S_RDBAND		0x0080
#define	S5_S_BANDURG		0x0200
#define	S5_I_SETSIG		(('S'<<8)|011)
#define	S5_I_GETSIG		(('S'<<8)|012)

/* Mask corresponding to the bits above in SunOS 4.x */
#define FLAGS_MASK	(O_SYNC|O_NONBLOCK|O_CREAT|O_TRUNC|O_EXCL \
			|O_NDELAY)
#define N_FLAGS_MASK	(N_O_NDELAY|N_O_SYNC|N_O_NONBLOCK|N_O_CREAT \
			|N_O_TRUNC|N_O_EXCL)

/* Windows ioctls */
#define WINASYNC _IOW(g,98, int)
#define WINNBIO _IOW(g,99, int)

struct n_flock {
        short	l_type;
        short	l_whence;
        long	l_start;
        long	l_len;          /* len == 0 means until end of file */
        long	l_sysid;
        long	l_pid;
        long	pad[4];         /* reserve area */
} ;

int fcntl(fd, cmd, arg)
int fd, cmd, arg;
{
	return(bc_fcntl(fd, cmd, arg));
}

int bc_fcntl(fd, cmd, arg)
int fd, cmd, arg;
{
	int fds, ret;
	struct flock *savarg;
	struct n_flock nfl;
	extern int errno;
	int narg, i;

	if ((cmd == F_SETOWN) || (cmd == F_GETOWN)) {
		ret = s_fcntl(fd, cmd_op[cmd], arg);
		if ((ret != -1) || (errno != EINVAL))
			return (ret);
		else {
			if (cmd == F_GETOWN) {
				if (_ioctl(fd, S5_I_GETSIG, &i) < 0) {
					if (errno == EINVAL)
						i = 0;
					else
						return (-1);
				}
				if (i & (S5_S_RDBAND|S5_S_BANDURG|
				    S5_S_RDNORM|S5_S_WRNORM))
					return (getpid());
				return (0);
			} else { /* cmd == F_SETOWN */
				i = S5_S_RDNORM|S5_S_WRNORM|S5_S_RDBAND|S5_S_BANDURG;
				return (ioctl(fd, S5_I_SETSIG, i));
			}
		}
	}
	if (cmd == F_SETFL) {
                if (arg & (O_NDELAY|O_NONBLOCK)) {
	                i = 1;
                        _syscall(SYS_ioctl, fd, WINNBIO, &i);
                } else {
                        i = 0;
                        _syscall(SYS_ioctl, fd, WINNBIO, &i);
		}
                if (arg & FASYNC) {
			s_fcntl(fd,cmd_op[cmd], S5_FASYNC);
                } else {
                        i = 0;
                        _syscall(SYS_ioctl, fd, WINASYNC, &i);
                }
		if (arg & FLAGS_MASK) {
			narg = arg & ~FLAGS_MASK;
			if (arg & O_SYNC)
				narg |= N_O_SYNC;
			if (arg & O_CREAT)
				narg |= N_O_CREAT;
			if (arg & O_TRUNC)
				narg |= N_O_TRUNC;
			if (arg & O_EXCL)
				narg |= N_O_EXCL;
			if (arg & (O_NDELAY))
				narg |= N_O_NDELAY;
			if (arg & O_NONBLOCK) 
				narg |= N_O_NONBLOCK;
			arg = narg;
		}
        } else if (cmd == F_SETLK || cmd == F_SETLKW)  {
		if (arg == 0 || arg == -1) {
			errno = EFAULT;
			return(-1);
		}	
		savarg = (struct flock *)arg;
		arg = (int) &nfl;
		nfl.l_type = savarg->l_type;
		nfl.l_whence = savarg->l_whence;
		nfl.l_start = savarg->l_start;
		nfl.l_len = savarg->l_len;
		nfl.l_pid = savarg->l_pid;
	} 

	ret = _syscall(SYS_fcntl, fd, cmd_op[cmd], arg);

	if (ret != -1) {
		if (cmd == F_DUPFD) {
			if ((fds = fd_get(fd)) != -1) 
				fd_add(ret, fds);
       		} else if (cmd == F_GETFL) {
			if (ret & N_FLAGS_MASK) {
				narg = ret & ~N_FLAGS_MASK;
				if (ret & N_O_SYNC)
					narg |= O_SYNC;
				if (ret & N_O_NONBLOCK)
					narg |= O_NONBLOCK;
				if (ret & N_O_CREAT)
					narg |= O_CREAT;
				if (ret & N_O_TRUNC)
					narg |= O_TRUNC;
				if (ret & N_O_EXCL)
					narg |= O_EXCL;
				if (ret & (N_O_NDELAY))
					narg |= O_NDELAY;
				ret = narg;
			}
		} else if (cmd == F_SETLK || cmd == F_SETLKW) {
			savarg->l_type = nfl.l_type;
			savarg->l_whence = nfl.l_whence;
			savarg->l_start = nfl.l_start;
			savarg->l_len = nfl.l_len;
			savarg->l_pid = nfl.l_pid;
		}
	}
        return(ret);
}	
