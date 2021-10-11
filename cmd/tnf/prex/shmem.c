/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)shmem.c 1.13 94/08/25 SMI"

/*
 * Includes
 */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "prbutlp.h"
#include "dbg.h"


/*
 * Static Globals
 */

struct shmem_msg {
	char			err[256];
	boolean_t	   spin;
};

static volatile struct shmem_msg *smp;


/*
 * prb_shmem_init() - initializes the shared memory region
 */

prb_status_t
prb_shmem_init(void)
{
	/*
	 * Set up the shmem communication buffer. We need only one which gets
	 * reused every time we fork a child. so it never needs to get
	 * unmapped.
	 */

	int			 shmem_fd;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_shmem_init:\n");
#endif

	shmem_fd = open("/dev/zero", O_RDWR);
	if (shmem_fd == -1) {
		DBG((void) fprintf(stderr, "couldn't open \"/dev/zero\""));
		return (prb_status_map(errno));
	}
	smp = (struct shmem_msg *) mmap(0,
		sizeof (struct shmem_msg),
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		shmem_fd,
		/*LINTED pointer cast may result in improper alignment*/
		0);
	if (smp == (struct shmem_msg *) - 1) {
		DBG((void) fprintf(stderr, "couldn't mmap \"/dev/zero\""));
		return (prb_status_map(errno));
	}
	(void) close(shmem_fd);
	return (PRB_STATUS_OK);

}				/* end shmem_init */


/*
 * prb_shmem_set() - sets the shared memory region to cause waiting
 */

prb_status_t
prb_shmem_set(void)
{
#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_shmem_set:\n");
#endif

	smp->err[0] = '\0';
	smp->spin = B_TRUE;

	return (PRB_STATUS_OK);

}				/* end prb_shmem_set */


/*
 * prb_shmem_wait() - spins until the shared memory flag is cleared
 */

static boolean_t
getspin(void)
{
	return (smp->spin);
}

prb_status_t
prb_shmem_wait(void)
{
#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_shmem_wait: begin\n");
#endif

	while (getspin());

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_shmem_wait: done\n");
#endif

	return (PRB_STATUS_OK);

}				/* end prb_shmem_wait */


/*
 * prb_shmem_clear() - clears the shared memory flag and allows waiters to
 * proceed.
 */

prb_status_t
prb_shmem_clear(void)
{
#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_shmem_clear:\n");
#endif

	smp->spin = B_FALSE;

	return (PRB_STATUS_OK);

}				/* end prb_shmem_clear */
