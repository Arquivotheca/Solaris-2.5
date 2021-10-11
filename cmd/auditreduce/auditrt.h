/*
 *	@(#)auditrt.h 2.2 92/01/30 SMI; SunOS CMW
 *	@(#)auditrt.h 4.8 91/11/09 SMI; BSM Module
 *	@(#)auditrt.h 3.2 90/11/13 SMI; SunOS MLS
 */

/*
 * Copyright (c) 1987 Sun Microsystems, Inc.
 */

#ifndef _AUDITRT_H
#define	_AUDITRT_H

/*
 * Auditreduce data structures.
 */

/*
 * File Control Block
 * Controls a single file.
 * These are held by the pcb's in audit_pcbs[] in a linked list.
 * There is one fcb for each file controlled by the pcb,
 * and all of the files in a list have the same suffix in their names.
 */
struct audit_fcb {
	struct audit_fcb *fcb_next;	/* ptr to next fcb in list */
	int	fcb_flags;	/* flags - see below */
	time_t	fcb_start;	/* start time from filename */
	time_t	fcb_end;	/* end time from filename */
	char	*fcb_suffix;	/* ptr to suffix in fcb_file */
	char	*fcb_name;	/* ptr to name in fcb_file */
	char	fcb_file[1];	/* full path and name string */
};

typedef struct audit_fcb audit_fcb_t;

/*
 * Flags for fcb_flags.
 */
#define	FF_NOTTERM	0x01	/* file is "not_terminated" */
#define	FF_DELETE	0x02	/* we may delete this file if requested */

/*
 * Process Control Block
 * A pcb comes in two types:
 * It controls either:
 *
 * 1.	A single group of pcbs (processes that are lower on the process tree).
 *	These are the pcb's that the process tree is built from.
 *	These are allocated as needed while the process tree is	being built.
 *
 * 2.	A single group of files (fcbs).
 *	All of the files in one pcb have the same suffix in their filename.
 *	They are controlled by the leaf nodes of the process tree.
 *	They are found in audit_pcbs[].
 *	They are initially setup by process_fileopt() when the files to be
 *	processes are gathered together. Then they are parsed out to
 *	the leaf nodes by mfork().
 *	A particular leaf node's range of audit_pcbs[] is determined
 *	in the call to mfork() by the lo and hi paramters.
 */
struct audit_pcb {
	struct audit_pcb *pcb_below;	/* ptr to group of pcb's */
	struct audit_pcb *pcb_next;	/* ptr to next - for list in mproc() */
	int	pcb_procno;	/* subprocess # */
	int	pcb_nrecs;	/* how many records read (current pcb/file) */
	int	pcb_nprecs;	/* how many records put (current pcb/file) */
	int	pcb_flags;	/* flags - see below */
	int	pcb_count;	/* count of active pcb's */
	int	pcb_lo;		/* low index for pcb's */
	int	pcb_hi;		/* hi index for pcb's */
	int	pcb_size;	/* size of current record buffer */
	time_t	pcb_time;	/* time of current record */
	time_t	pcb_otime;	/* time of previous record */
	char	*pcb_rec;	/* ptr to current record buffer */
	char	*pcb_suffix;	/* ptr to suffix name (string) */
	audit_fcb_t * pcb_first;	/* ptr to first fcb_ */
	audit_fcb_t * pcb_last;		/* ptr to last fcb_ */
	audit_fcb_t * pcb_cur;		/* ptr to current fcb_ */
	audit_fcb_t * pcb_dfirst;	/* ptr to first fcb_ for deleting */
	audit_fcb_t * pcb_dlast;	/* ptr to last fcb_ for deleting */
	FILE	 * pcb_fpr;		/* read stream */
	FILE	 * pcb_fpw;		/* write stream */
};

typedef struct audit_pcb audit_pcb_t;

/*
 * Flags for pcb_flags
 */
#define	PF_ROOT		0x01	/* current pcb is the root of process tree */
#define	PF_LEAF		0x02	/* current pcb is a leaf of process tree */
#define	PF_FILE		0x04	/* current pcb uses files as input, not pipes */

/*
 * Message selection options
 */
#define	M_AFTER		0x0001	/* 'a' after a time */
#define	M_BEFORE	0x0002	/* 'b' before a time */
#define	M_CLASS		0x0004	/* 'c' event class */
#define	M_GROUPE 	0x0008	/* 'f' effective group-id */
#define	M_GROUPR 	0x0010	/* 'g' real group-id */
#define	M_OBJECT	0x0020	/* 'o' object */
#define	M_SUBJECT	0x0040	/* 'j' subject */
#define	M_TYPE		0x0080	/* 'm' event type */
#define	M_USERA		0x0100	/* 'u' audit user */
#define	M_USERE		0x0200	/* 'e' effective user */
#define	M_USERR		0x0400	/* 'r' real user */
#ifdef SunOS_CMW
#define	M_ILABEL	0x0800	/* 'i' information label range */
#define	M_SLABEL	0x1000	/* 's','z' sensitivity label range */
#endif /* SunOS_CMW */
#define	M_SORF		0x4000	/* success or failure of event */
/*
 * object types
 */
#define	OBJ_LP		0x0001  /* 'o' lp object */
#define	OBJ_MSG		0x0002  /* 'o' msgq object */
#define	OBJ_PATH	0x0004  /* 'o' file system object */
#define	OBJ_PROC	0x0008  /* 'o' process object */
#define	OBJ_SEM		0x0010  /* 'o' semaphore object */
#define	OBJ_SHM		0x0020  /* 'o' shared memory object */
#define	OBJ_SOCK	0x0040  /* 'o' socket object */

#define	SOCKFLG_MACHINE 0	/* search socket token by machine name */
#define	SOCKFLG_PORT    1	/* search socket token by port number */

/*
 * Global variables
 */
#ifndef C2_MARAUDER
extern unsigned short m_type;	/* 'm' message type */
extern long	m_groupr;	/* 'g' real group-id */
extern long	m_groupe;	/* 'f' effective group-id */
extern long	m_usera;	/* 'u' audit user */
extern long	m_userr;	/* 'r' real user */
extern long	m_usere;	/* 'f' effective user */
#else /* C2_MARAUDER */
extern ushort	m_type;		/* 'm' message type */
extern ushort	m_groupr;	/* 'g' real group-id */
extern ushort	m_groupe;	/* 'f' effective group-id */
extern ushort	m_usera;	/* 'u' audit user */
extern ushort	m_userr;	/* 'r' real user */
extern ushort	m_usere;	/* 'f' effective user */
#endif
extern time_t	m_after;	/* 'a' after a time */
extern time_t	m_before;	/* 'b' before a time */
extern audit_state_t mask;	/* used with m_class */
extern int	flags;
extern int	checkflags;
extern int	socket_flag;
extern int	obj_flag;	/* 'o' object type */
extern long	obj_id;		/* object identifier */
extern long	subj_id; 	/* subject identifier */
extern ushort  ipc_event_type; /* 'o' object type - tell what type of IPC */

#ifdef SunOS_CMW
extern bslabel_t	slow;		/* 's','z' sensitivity label range */
extern bslabel_t	shigh;		/* 's','z' sensitivity label range */
extern bilabel_t	ilow;		/* 'i' information label range */
extern bilabel_t	ihigh;		/* 'i' information label range */
extern int	exactslab; 	/* exact slabel matching flag */
extern int	exactilab; 	/* exact ilabel matching flag */
#endif /* SunOS_CMW */

#ifdef DUMP
extern int	dasht;		/* 'T' token */
#endif

/*
 * File selection options
 */
extern char	*f_machine;	/* 'M' machine (suffix) type */
extern char	*f_root;	/* 'R' audit root */
extern char	*f_server;	/* 'S' server */
extern char	*f_outfile;	/* 'W' output file */
extern char	*f_outtemp;	/* 'W' temporary file name */
extern int	f_all;		/* 'A' all records from a file */
extern int	f_complete;	/* 'C' only completed files */
extern int	f_delete;	/* 'D' delete when done */
extern int	f_quiet;	/* 'Q' sshhhh! */
extern int	f_verbose;	/* 'V' verbose */
extern int	f_stdin;	/* 'N' from stdin */
extern int	f_cmdline;	/*	files specified on the command line */
extern char	*path_re;	/* path regular expression */

/*
 * Error reporting
 * Error_str is set whenever an error occurs to point to a string describ
ing
 * the error. When the error message is printed error_str is also
 * printed to describe exactly what went wrong.
 * Errbuf is used to build messages with variables in them.
 */
extern char	*error_str;	/* current error message */
extern char	errbuf[];	/* buffer for building error message */
extern char	*ar;		/* => "auditreduce:" */

/*
 * Control blocks
 * Audit_pcbs[] is an array of pcbs that control files directly.
 * In the program's initialization phase it will gather all of the input

 * files it needs to process. Each file will have one fcb allocated for i
t,
 * and each fcb will belong to one pcb from audit_pcbs[]. All of the file
s
 * in a single pcb will have the same suffix in their filenames. If the
 * number of active pcbs in audit_pcbs[] is greater that the number of op
en
 * files a single process can have then the program will need to fork
 * subprocesses to handle all of the files.
 */
extern audit_pcb_t *audit_pcbs;	/* file-holding pcb's */
extern int	pcbsize;		/* current size of audit_pcbs[] */
extern int	pcbnum;		/* total # of active pcbs in audit_pcbs[] */

/*
 * Time values
 */
extern time_t f_start;		/* time of start rec for outfile */
extern time_t f_end;		/* time of end rec for outfile */
extern time_t time_now;		/* time program began */

/*
 * Counting vars
 */
extern int	filenum;		/* number of files total */

/*
 * Global variable, class of current record being processed.
 */
extern int	global_class;

#endif /* _AUDITRT_H */
