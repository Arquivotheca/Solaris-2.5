#ident	"@(#)cwd.c	1.1	94/11/10 SMI"

#include        <sys/types.h>
#include        <string.h>
#include        <fcntl.h>
#include        <sys/syscall.h>
#include        <sys/dirent.h>
#if u3b2
#define _KERNEL	1
#endif
#include        <sys/stat.h>
#if u3b2
#undef _KERNEL
#endif

#ifdef _STAT_VER
#if u3b2
#	define	stat	xstat
#endif
#	define	SYS_STAT	SYS_xstat, _STAT_VER
#	define	SYS_FSTAT	SYS_fxstat, _STAT_VER
#else
#	define	SYS_STAT	SYS_stat
#	define	SYS_FSTAT	SYS_fstat
#endif

#define        NULL 0

#define MAX_PATH 1024
#define MAX_NAME 512
#define BUF_SIZE 1536 /* 3/2 of MAX_PATH for /a/a/a... case */

/* 
 * This algorithm does not use chdir.  Instead, it opens a 
 * successive strings for parent directories, i.e. .., ../..,
 * ../../.., and so forth.
 */
typedef struct data {
	char	path[MAX_PATH+4];
	struct stat	cdir;	/* current directory status */
	struct stat	tdir;
	struct stat	pdir;	/* parent directory status */
	char	dotdots[BUF_SIZE+MAX_NAME];
	int	dirsize;
	int	diroffset;
	char	dirbuf[1024];
} data_t;

int	syscall(int code, ...);
static	int	opendir();
static	int	closedir();
static struct dirent * readdir();

char *
cwd(p)
	register data_t * p;
{
	register char *str = p->path;
	register int size = sizeof(p->path);
	register int		pdfd;	/* parent directory stream */
	register struct dirent *dir;
	char *dotdot = p->dotdots + BUF_SIZE - 3;
	char *dotend = p->dotdots + BUF_SIZE - 1; 
	int i, maxpwd, ret; 

	*dotdot = '.';
	*(dotdot+1) = '.';
	*(dotdot+2) = '\0';
	maxpwd = size--;
	str[size] = 0;

	if(syscall(SYS_STAT, dotdot+1, &p->pdir) < 0)
		return NULL;

	for(;;)
	{
		/* update current directory */
		p->cdir = p->pdir;

		/* open parent directory */
		if ((pdfd = opendir(p, dotdot)) < 0)
			break;

		if(syscall(SYS_FSTAT, pdfd, &p->pdir) < 0)
		{
			(void)closedir(p, pdfd);
			break;
		}

		/*
		 * find subdirectory of parent that matches current 
		 * directory
		 */
		if(p->cdir.st_dev == p->pdir.st_dev)
		{
			if(p->cdir.st_ino == p->pdir.st_ino)
			{
				/* at root */
				(void)closedir(p, pdfd);
				if (size == (maxpwd - 1))
					/* pwd is '/' */
					str[--size] = '/';

				strcpy(str, &str[size]);
				return str;
			}
			do
			{
				if ((dir = readdir(p, pdfd)) == NULL)
				{
					(void)closedir(p, pdfd);
					goto out;
				}
			}
			while (dir->d_ino != p->cdir.st_ino);
		}
		else
		{
			/*
			 * must determine filenames of subdirectories
			 * and do stats
			 */
			*dotend = '/';
			do
			{
		again:
				if ((dir = readdir(p, pdfd)) == NULL)
				{
					(void)closedir(p, pdfd);
					goto out;
				}
				if (dir->d_name[0] == '.') {
					if (dir->d_name[1] == '\0')
						goto again;
					if (dir->d_name[1] == '.' &&
				    	dir->d_name[2] == '\0')
						goto again;
				}
				strcpy(dotend + 1, dir->d_name);
				/* skip over non-stat'able
				 * entries
				 */
				ret = syscall(SYS_STAT, dotdot, &p->tdir);

			}		
			while(ret == -1 || p->tdir.st_ino != p->cdir.st_ino || p->tdir.st_dev != p->cdir.st_dev);
		}
		(void)closedir(p, pdfd);

		i = strlen(dir->d_name);

		if (i > size - 1) {
			break;
		} else {
			/* copy name of current directory into pathname */
			size -= i;
			strncpy(&str[size], dir->d_name, i);
			str[--size] = '/';
		}
		if (dotdot - 3 < p->dotdots) 
			break;
		/* update dotdot to parent directory */
		*--dotdot = '/';
		*--dotdot = '.';
		*--dotdot = '.';
		*dotend = '\0';
	}
out:
	return NULL;
}

static int
opendir(p, path)
	data_t * p;
	char * path;
{
	p->dirsize = p->diroffset = 0;
	return syscall(SYS_open, path, O_RDONLY, 0);
}

static int
closedir(p, pdfd)
	data_t * p;
	int pdfd;
{
	p->dirsize = p->diroffset = 0;
	return syscall(SYS_close, pdfd);
}

static struct dirent *
readdir(p, pdfd)
	data_t * p;
	register int pdfd;
{
	register struct dirent * dp;

	if (p->diroffset >= p->dirsize) {
		p->diroffset = 0;
		dp = (struct dirent *)p->dirbuf;
		p->dirsize = syscall(SYS_getdents, pdfd, dp, sizeof(p->dirbuf));
		if (p->dirsize <= 0) {
			p->dirsize = 0;
			return NULL;
		}
	}

	dp = (struct dirent *)&p->dirbuf[p->diroffset];
	p->diroffset += dp->d_reclen;

	return dp;
}

static char *
strcpy(t, s)
	register char * t;
	register const char * s;
{
	register char * p = t;

	while (*t++ = *s++)
		;

	return p;
}

static char *
strncpy(t, s, n)
	register char * t;
	register const char * s;
	register size_t n;
{
	register char * p = t;

	while (n-- && (*t++ = *s++))
		;

	return p;
}

static size_t
strlen(s)
	register const char * s;
{
	register const char * t = s;

	while (*t++)
		;

	return (t - s - 1);
}

#if u3b2

asm("	.text");
asm("	.align	4");
asm("syscall:");
asm("	movw	&4,%r0");
asm("	ALSW3	&3,0(%ap),%r1");
asm("	addw2	&4,%ap");
asm("	GATE");
asm("	BGEUB	.L1001");
asm("	mnegw	&1,%r0");
asm(".L1001:");
asm("	subw2	&4,%ap");
asm("	RET	");
asm("	.type	syscall,@function");
asm("	.size	syscall,.-syscall");

#elif i386

static
syscall(int code, ...) {
asm("	leave	");	/* get out of this stack frame */

asm("	pop	%edx");
asm("	pop	%eax");
asm("	pushl	%edx");
asm("	lcall	$0x7,$0");
asm("	movl	0(%esp),%edx");
asm("	pushl	%edx");
asm("	jae	.+7");
asm("	movl	$-1,%eax");

asm("	pushl	%ebp");	/* reenter the stack frame */
asm("	movl	%esp,%ebp");
}

/* This is a crock */
static _xstat() {}
static _fxstat() {}
static _lxstat() {}
static _xmknod() {}

#elif sparc

static int
syscall(int code, ...)
{
asm("	mov	%i0, %o0");
asm("	mov	%i1, %o1");
asm("	mov	%i2, %o2");
asm("	mov	%i3, %o3");
asm("	mov	%i4, %o4");
asm("	mov	%i5, %o5");
asm("	mov	0, %g1");
asm("	ta	8");
asm("	bcs,a	1f");
asm("	mov	-1, %o0");
asm("1:");
asm("	mov	%o0, %i0");
asm("	mov	%o1, %i1");
}

#endif
