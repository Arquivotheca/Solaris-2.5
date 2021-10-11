/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_AIO_IMPL_H
#define	_SYS_AIO_IMPL_H

#pragma ident	"@(#)aio_impl.h	1.6	94/11/19 SMI"

#include <sys/aio_req.h>
#include <sys/aio.h>
#include <sys/uio.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL

#define	AIO_HASHSZ		256		/* power of 2 */

/*
 * async I/O request struct - one per I/O request.
 */
typedef struct aio_req_t {
	struct aio_req	aio_req;
	int		aio_req_fd;		/* aio's file descriptor */
	int		aio_req_flags;		/* flags */
	aio_result_t	*aio_req_resultp;	/* user's resultp */
	int		(*aio_req_cancel)();	/* driver's cancel cb. */
	struct aio_req_t *aio_req_next;		/* next free or done */
	struct aio_req_t *aio_hash_next;	/* next in a hash bucket */
	struct uio	aio_req_uio;		/* uio struct */
	struct iovec	aio_req_iov;		/* iovec struct */
	struct buf	aio_req_buf;		/* buf struct */
} aio_req_t;

/*
 * Struct for asynchronous I/O (aio) information per process.
 * Each proc stucture has a field pointing to this struct.
 * The field will be null if no aio is used.
 */
typedef struct aio {
	int		aio_pending;		/* # uncompleted requests */
	int		aio_outstanding;	/* total # of requests */
	int		aio_ok;			/* everything ok when set */
	int		aio_flags;		/* flags */
	aio_req_t	*aio_free;  		/* freelist */
	struct aio_done_queue {			/* done queue */
		aio_req_t *head;
		aio_req_t *tail;
	} aio_doneq;
	struct aio_poll_queue {			/* AIO_POLL queue */
		aio_req_t *head;
		aio_req_t *tail;
	} aio_pollq;
	kmutex_t    	aio_mutex;		/* mutex for aio struct */
	kcondvar_t  	aio_waitcv;		/* cv for aiowait()'ers */
	kcondvar_t  	aio_cleanupcv;		/* notify cleanup, aio_done */
	int 		aio_notifycnt;		/* # user-level notifications */
	aio_req_t 	*aio_hash[AIO_HASHSZ];	/* hash list of requests */
} aio_t;

/*
 * aio_flags in aio_req struct
 */
#define	AIO_POLL	 0x1			/* AIO_INPROGRESS is set */
#define	AIO_DONE	 0x2			/* aio req has completed */
#define	AIO_CLEANUP	0x4			/* do aio cleanup processing */

extern int aphysio(int (*)(), int (*)(), dev_t, int, void (*)(),
		struct aio_req *);
extern void aphysio_cleanup(aio_req_t *);
extern void aio_cleanup(void);
void aio_copyout_result(aio_req_t *);
extern void aio_cleanup_exit(void);

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_AIO_IMPL_H */
