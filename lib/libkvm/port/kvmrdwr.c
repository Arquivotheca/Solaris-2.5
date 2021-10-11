/*
 * Copyright (c) 1987, 1988, 1989, 1990, 1991, 1992 Sun Microsystems, Inc.
 */

#pragma ident	"@(#)kvmrdwr.c	2.24	95/09/11 SMI"

#include "kvm_impl.h"
#include <sys/param.h>
#include <sys/vmmac.h>
#include <sys/file.h>

static int kvm_rdwr();
extern int read();
extern int write();

int
kvm_read(kd, addr, buf, nbytes)
	kvm_t *kd;
	u_long addr;
	char *buf;
	u_int nbytes;
{
	return (kvm_rdwr(kd, addr, buf, nbytes, read));
}

int
kvm_write(kd, addr, buf, nbytes)
	kvm_t *kd;
	u_long addr;
	char *buf;
	u_int nbytes;
{
	if (!kd->wflag)		/* opened for write? */
		return (-1);
	return (kvm_rdwr(kd, addr, buf, nbytes, write));
}

static int
kvm_rdwr(kd, addr, buf, nbytes, rdwr)
	kvm_t *kd;
	u_long addr;
	char *buf;
	u_int nbytes;
	int (*rdwr)();
{

	return (kvm_as_rdwr(kd,
		addr >= (u_long)kd->kernelbase ? &kd->Kas : &kd->Uas,
		addr, buf, nbytes, rdwr));
}


int
kvm_uread(kd, addr, buf, nbytes)
	kvm_t *kd;
	u_long addr;
	char *buf;
	u_int nbytes;
{
	return (kvm_as_read(kd, &kd->Uas, addr, buf, nbytes));
}

int
kvm_uwrite(kd, addr, buf, nbytes)
	kvm_t *kd;
	u_long addr;
	char *buf;
	u_int nbytes;
{
	if (!kd->wflag)		/* opened for write? */
		return (-1);
	return (kvm_as_write(kd, &kd->Uas, addr, buf, nbytes));
}


int
kvm_kread(kd, addr, buf, nbytes)
	kvm_t *kd;
	u_long addr;
	char *buf;
	u_int nbytes;
{
	return (kvm_as_read(kd, &kd->Kas, addr, buf, nbytes));
}

int
kvm_kwrite(kd, addr, buf, nbytes)
	kvm_t *kd;
	u_long addr;
	char *buf;
	u_int nbytes;
{
	if (!kd->wflag)		/* opened for write? */
		return (-1);
	return (kvm_as_write(kd, &kd->Kas, addr, buf, nbytes));
}


kvm_as_read(kd, as, addr, buf, nbytes)
	kvm_t *kd;
	struct as *as;
	u_long addr;
	char *buf;
	u_int nbytes;
{
	return (kvm_as_rdwr(kd, as, addr, buf, nbytes, read));
}

kvm_as_write(kd, as, addr, buf, nbytes)
	kvm_t *kd;
	struct as *as;
	u_long addr;
	char *buf;
	u_int nbytes;
{
	if (!kd->wflag)		/* opened for write? */
		return (-1);
	return (kvm_as_rdwr(kd, as, addr, buf, nbytes, write));
}

kvm_as_rdwr(kd, as, addr, buf, nbytes, rdwr)
	kvm_t *kd;
	struct as *as;
	u_long addr;
	char *buf;
	u_int nbytes;
	int (*rdwr)();
{
	register u_long a;
	register u_long end;
	register int n;
	register int cnt;
	register char *bp;
	register int i;
	int fd;
	u_longlong_t paddr;

	if (addr == 0) {
		KVM_ERROR_1("kvm_as_rdwr: address == 0");
		return (-1);
	}

	if (kd->corefd == -1) {
		KVM_ERROR_1("kvm_as_rdwr: no corefile descriptor");
		return (-1);
	}

	/*
	 * If virtual addresses may be used, by all means use them.
	 * Reads can go fast through /dev/kmem on a live system.
	 * 1203435: writes to kernel memory must take the long path and
	 * go through /dev/mem. /dev/kmem does not allow writes to kernel
	 * text, which prevents adb from patching the kernel.
	 */
	if ((as->a_segs.list == kd->Kas.a_segs.list) &&
	    (kd->virtfd != -1) && (rdwr == read)) {
		if (lseek(kd->virtfd, (off_t)addr, L_SET) == -1) {
			KVM_PERROR_2("%s: seek error", kd->virt);
			return (-1);
		}
		cnt = (*rdwr)(kd->virtfd, buf, nbytes);
#ifdef _KVM_DEBUG
		if (cnt != nbytes) {
			KVM_ERROR_2("%s: i/o error", kd->virt);
		}
#endif _KVM_DEBUG
		return (cnt);
	}
	cnt = 0;
	end = addr + nbytes;
	for (a = addr, bp = buf; a < end; a += n, bp += n) {
		n = kd->pagesz - (a & kd->pageoffset);
		if ((a + n) > end)
			n = end - a;
		if (a == 0)
			printf("kvm_as_rdwr: addr = 0\n");
		_kvm_physaddr(kd, as, (u_int)a, &fd, &paddr);
		if (fd == -1) {
			KVM_ERROR_1("kvm_rdwr: _kvm_physaddr failed");
			goto out;
		}

		/* see if file is of condensed type */
		switch (_uncondense(kd, fd, &paddr)) {
		case -1:
			KVM_ERROR_2("%s: uncondense error", kd->core);
			goto out;
		case -2:
			/* paddr corresponds to a hole */
			if (rdwr == read) {
				memset(bp, 0, n);
			}
			/* writes are ignored */
			cnt += n;
			continue;
		}

		/* could be physical memory or swap */
		if (llseek(fd, (offset_t)paddr, L_SET) == -1) {
			KVM_PERROR_2("%s: seek error", kd->core);
			goto out;
		}

		if ((i = (*rdwr)(fd, bp, n)) > 0)
			cnt += i;

		if (i != n) {
			KVM_PERROR_2("%s: i/o error", kd->core);
			break;
		}
	}

out:
	return (cnt > 0 ? cnt : -1);
}

int
_uncondense(kd, fd, paddrp)
	kvm_t *kd;
	int fd;
	u_longlong_t *paddrp;
{
	struct condensed *cdp;

	/* There ought to be a better way! */
	if (fd == kd->corefd)
		cdp = kd->corecdp;
	else if (fd == kd->swapfd)
		cdp = kd->swapcdp;
	else {
		KVM_ERROR_1("uncondense: unknown fd");
		return (-1);
	}

	if (cdp) {
		u_longlong_t paddr = *paddrp;
		off_t *atoo = cdp->cd_atoo;
		off_t offset;
		int chunksize = cdp->cd_chunksize;
		int i;

		i = paddr / chunksize;
		if (i < cdp->cd_atoosize) {
			if ((offset = atoo[i]) == NULL)
				return (-2);	/* Hole */
		} else {
			/*
			 * An attempt to read/write beyond the end of
			 * the logical file; convert to the equivalent
			 * offset, and let a read hit EOF and a write do
			 * an extension.
			 */
			offset = (i - cdp->cd_atoosize) * chunksize +
				cdp->cd_dp->dump_dumpsize;
		}

		*paddrp = offset + (paddr % chunksize);
	}
	return (0);
}
