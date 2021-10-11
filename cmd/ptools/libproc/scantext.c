#ident	"@(#)scantext.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "pcontrol.h"

#define	BLKSIZE	1024

#if u3b2
#	define	GATE1	0x30
#	define	GATE2	0x61
#	define	PCADJ	2
#elif i386
#	define	LCALL	0x9a
#	define	PCADJ	7
	static syscall_t lcall = { 0x9a, 0, 0, 0, 0, 0x7, 0 };
#elif sparc
#	define	PCADJ	0
#endif

int
scantext(Pr)		/* look for SYSCALL instruction in process */
process_t *Pr;
{
	int fd = Pr->pfd;		/* process filedescriptor */
#if u3b2 || i386
	register unsigned char *p;	/* pointer into buf */
#elif sparc
	register unsigned long *p;	/* pointer into buf */
#endif
	register unsigned long offset;	/* offset in text section */
	register unsigned long endoff;	/* ending offset in text section */
	register long sysaddr;		/* address of SYSCALL instruction */
	register int nbytes;		/* number of bytes in buffer */
	register int n2bytes;		/* number of bytes in second buffer */
	int nsegments;			/* current number of segments */
	syscall_t instr;		/* instruction from process */
	prmap_t *pdp;			/* pointer to map descriptor */
	char buf[2*BLKSIZE];		/* buffer for reading text */
	prmap_t prbuf[100];		/* buffer for map descriptors */

	/* try the most recently-seen syscall address */
	if ((sysaddr = Pr->sysaddr) != 0)
#if i386
		if (Pread(Pr, sysaddr, (char *)instr, (int)sizeof(instr))
		    != sizeof(instr) || memcmp(instr, lcall, sizeof(lcall)))
#else
		if (Pread(Pr, sysaddr, (char *)&instr, (int)sizeof(instr))
		    != sizeof(instr) || instr != (syscall_t)SYSCALL)
#endif
			sysaddr = 0;

	/* try the current PC minus sizeof(syscall) */
	if (sysaddr == 0 && (sysaddr = Pr->REG[R_PC]-PCADJ) != 0)
#if i386
		if (Pread(Pr, sysaddr, (char *)&instr, (int)sizeof(instr))
		    != sizeof(instr) || memcmp(instr, lcall, sizeof(lcall)))
#else
		if (Pread(Pr, sysaddr, (char *)&instr, (int)sizeof(instr))
		    != sizeof(instr) || instr != (syscall_t)SYSCALL)
#endif
			sysaddr = 0;

	Pr->sysaddr = 0;	/* assume failure */

	if (sysaddr) {		/* already have address of SYSCALL */
		Pr->sysaddr = sysaddr;
		return 0;
	}

	if (Ioctl(fd, PIOCNMAP, (int)&nsegments) == -1
	  || nsegments <= 0) {
		perror("scantext(): PIOCNMAP");
		return -1;
	}

	if (nsegments >= sizeof(prbuf)/sizeof(prbuf[0])) {
		(void) fprintf(stderr, "scantext(): too many segments\n");
		return -1;
	}

	if (Ioctl(fd, PIOCMAP, (int)&prbuf[0]) == -1) {
		perror("scantext(): PIOCMAP");
		return -1;
	}

	/* scan the segments looking for an executable segment */
	for (pdp = &prbuf[0]; sysaddr == 0 && pdp->pr_size; pdp++) {

		if ((pdp->pr_mflags&MA_EXEC) == 0)
			continue;

		offset = (unsigned long)pdp->pr_vaddr;	/* beginning of text */
		endoff = offset + pdp->pr_size;

		(void) lseek(fd, (long)offset, 0);

		if ((nbytes = read(fd, &buf[0], 2*BLKSIZE)) <= 0) {
			if (nbytes < 0)
				if (errno != EIO)
					perror("scantext(): read()");
			continue;
		}

		if (nbytes < BLKSIZE)
			n2bytes = 0;
		else {
			n2bytes = nbytes - BLKSIZE;
			nbytes  = BLKSIZE;
		}
#if u3b2 || i386
		p = (unsigned char *)&buf[0];
#elif sparc
		p = (unsigned long *)&buf[0];
#endif

		/* search text for a SYSCALL instruction */
		while (sysaddr == 0 && offset < endoff) {
			if (nbytes <= 0) {	/* shift buffers */
				if ((nbytes = n2bytes) <= 0)
					break;
				(void) memcpy(&buf[0], &buf[BLKSIZE], nbytes);
#if u3b2 || i386
				p = (unsigned char *)&buf[0];
#elif sparc
				p = (unsigned long *)&buf[0];
#endif
				n2bytes =
				  (nbytes==BLKSIZE && offset+BLKSIZE<endoff)?
					read(fd, &buf[BLKSIZE], BLKSIZE) : 0;
			}
#if u3b2
			if (*p++ == GATE1
			 && *p   == GATE2)
				sysaddr = offset;
			offset++;
			nbytes--;
#elif i386
			if (memcmp(p++, lcall, sizeof(lcall)) == 0)
				sysaddr = offset;
			offset++;
			nbytes--;
#elif sparc
			if (*p++ == SYSCALL)
				sysaddr = offset;
			offset += sizeof(long);
			nbytes -= sizeof(long);
#endif
		}
	}

	Pr->sysaddr = sysaddr;
	return sysaddr? 0 : -1;
}
