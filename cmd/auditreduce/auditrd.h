/*
 * Copyright (c) 1987 Sun Microsystems, Inc.
 */

/*
 *	File history:
 *	@(#)auditrd.h 2.2 92/01/30 SMI; SunOS CMW
 *	@(#)auditrd.h 4.8 91/11/09 SMI; BSM Module
 *	@(#)auditrd.h 3.2 90/11/13 SMI; SunOS MLS
 */

#ifndef	_AUDITRD_H
#define	_AUDITRD_H

#pragma ident	"@(#)auditrd.h	1.6	93/10/26 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Global data for auditreduce
 */

/*
 * Message selection options
 */
#ifndef C2_MARAUDER
unsigned short	m_type;		/* 'm' message type */
long	m_groupr;		/* 'g' group-id */
long	m_groupe;		/* 'f' effective group-id */
long	m_usera;		/* 'u' user id */
long	m_usere;		/* 'e' effective user-id */
long	m_userr;		/* 'r' real user-id */
#else /* C2_MARAUDER */
ushort m_type;		/* 'm' message type */
ushort m_groupr;	/* 'g' group-id */
ushort m_groupe;	/* 'f' effective group-id */
ushort m_usera;		/* 'u' user id */
ushort m_usere;		/* 'e' effective user-id */
ushort m_userr;		/* 'r' real user-id */
#endif
time_t m_after;		/* 'a' after a time */
time_t m_before;	/* 'b' before a time */
audit_state_t mask;	/* used with m_class */
int	flags;
int	checkflags;
int	socket_flag;
int	obj_flag;		/* 'o' object type */
long	obj_id;			/* object identifier */
long	subj_id;		/* subject identifier  */
ushort ipc_event_type;		/* 'o' object type - tell what type of IPC */

#ifdef SunOS_CMW
bslabel_t slow;			/* 's','z' lower bound label range */
bslabel_t shigh;		/* 's','z' upper bound label range */
bilabel_t ilow;			/* 'i' lower bound label range */
bilabel_t ihigh;		/* 'i' upper bound label range */
int	exactslab;		/* exact slabel matching flag */
int	exactilab;		/* exact ilabel matching flag */
#endif /* SunOS_CMW */

#ifdef DUMP
int	dasht;		/* 'T' token */
#endif

/*
 * File selection options
 */
char	*f_machine;	/* 'M' machine (suffix) type */
char	*f_root;		/* 'R' audit root */
char	*f_server;		/* 'S' server */
char	*f_outfile;	/* 'W' output file */
char	*f_outtemp;	/* 'W' temporary file name */
int	f_all;		/* 'A' all records from a file */
int	f_complete;		/* 'C' only completed files */
int	f_delete;		/* 'D' delete when done */
int	f_quiet;		/* 'Q' sshhhh! */
int	f_verbose;		/* 'V' verbose */
int	f_stdin;		/* 'N' from stdin */
int	f_cmdline;		/* files specified on the command line */
char	*path_re;		/* path regular expression */

/*
 * Global error reporting
 */
char	*error_str;	/* current error message */
char	errbuf[256];	/* for creating error messages with sprintf */
char	*ar = "auditreduce:";
int	root_pid;		/* remember original process's pid */

/*
 * Global control blocks
 */
audit_pcb_t *audit_pcbs; /* ptr to array of pcbs that hold files (fcbs) */

int	pcbsize;		/* size of audit_pcb[] */
int	pcbnum;		/* number of pcbs in audit_pcb[] that are active */

/*
 * Time values
 */
time_t f_start;		/* time of first record written */
time_t f_end;		/* time of last record written */
time_t time_now;	/* time the program began */

/*
 * Global counting vars
 */
int	filenum;		/* number of files to process */

/*
 * Global variable, class of current record being processed.
 */
int global_class;

#ifdef __cplusplus
}
#endif

#endif	/* _AUDITRD_H */
