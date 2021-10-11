/*
 * Copyright (c) 1986, 1990 by Sun Microsystems, Inc.
 *	All rights reserved.
 */

/*
 * Header file for dump module for debugging message flow in a stream
 */

#ifndef	_SYS_DEDUMP_H
#define	_SYS_DEDUMP_H

#pragma ident	"@(#)dedump.h	1.6	92/07/14 SMI"	/* SVr3.2H 1.1	*/

#ifdef	__cplusplus
extern "C" {
#endif

#define	MESSAGE_NUM	27

#define	IOC_SIZE	sizeof (struct iocblk)
#define	OPT_SIZE	sizeof (struct stroptions)
#define	REQ_SIZE	sizeof (struct copyreq)
#define	REP_SIZE	sizeof (struct copyresp)
#define	CHAR		sizeof (char)
#define	INT		sizeof (int)
#define	LONG		sizeof (long)


struct dmp {
	short dump_flags;
	short dump_no;
	queue_t *dump_wq;
};

/* Flags used for print options */
#define	DUMP_IN_USE	00
#define	PRT_TO_CONSOLE 	01
#define	PRT_TO_LOG	02


#define	SET_OPTIONS	(('d' << 8)|1)

#define	READ		0
#define	WRITE		1

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DEDUMP_H */
