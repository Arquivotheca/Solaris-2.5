/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _TNFDRV_H
#define	_TNFDRV_H

#pragma ident  "@(#)tnfdrv.h 1.7 94/12/06 SMI"

typedef struct {
	int probenum;
	int enabled;
	int traced;
	int attrsize;
} probevals_t;

typedef struct {
	enum {
		TIFIOCBUF_NONE,
		TIFIOCBUF_UNINIT,
		TIFIOCBUF_OK,
		TIFIOCBUF_BROKEN
	} buffer_state;
	int buffer_size;
	int trace_stopped;
	int pidfilter_mode;
} tifiocstate_t;

/*
 * Defines - Project private interfaces
 */

/* ioctl codes -- values to be fixed */

#define	TIFIOCGMAXPROBE		(('t' << 8) | 1) /* get max probe number */
#define	TIFIOCGPROBEVALS	(('t' << 8) | 2) /* get probe info */
#define	TIFIOCGPROBESTRING	(('t' << 8) | 3) /* get probe string */
#define	TIFIOCSPROBEVALS	(('t' << 8) | 4) /* set probe info */
#define	TIFIOCGSTATE		(('t' << 8) | 5) /* get tracing system state */
#define	TIFIOCALLOCBUF		(('t' << 8) | 6) /* allocate trace buffer */
#define	TIFIOCDEALLOCBUF	(('t' << 8) | 7) /* dealloc trace buffer */
#define	TIFIOCSTRACING		(('t' << 8) | 8) /* set ktrace mode */
#define	TIFIOCSPIDFILTER	(('t' << 8) | 9) /* set pidfilter mode */
#define	TIFIOCGPIDSTATE		(('t' << 8) | 10) /* check pid filter member */
#define	TIFIOCSPIDON		(('t' << 8) | 11) /* add pid to filter */
#define	TIFIOCSPIDOFF		(('t' << 8) | 12) /* drop pid from filter */

#endif			/* _TNFDRV_H */
