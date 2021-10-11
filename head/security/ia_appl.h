/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef _IA_APPL_H
#define	_IA_APPL_H

#pragma ident	"@(#)ia_appl.h	1.52	95/02/17 SMI"

#include <pwd.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	MAXLEN
#define	MAXLEN	64
#endif

/*
 * max # of authentication token attributes
 */
#ifndef	MAX_NUM_ATTR
#define	MAX_NUM_ATTR	10
#endif

/*
 * max size (in chars) of an authentication token attribute
 */
#ifndef	MAX_ATTR_SIZE
#define	MAX_ATTR_SIZE	80
#endif

/*
 * max # of messages that can be passed to the application through
 * the conversation function call
 */
#ifndef	MAX_NUM_MSG
#define	MAX_NUM_MSG	8
#endif

/*
 * max size (in chars) of each messages passed to the application
 * through the conversation function call
 */
#ifndef	MAX_MSG_SIZE
#define	MAX_MSG_SIZE	256
#endif

/*
 * max size (in chars) of each response passed from the application
 * through the conversation function call
 */
#ifndef	MAX_RESP_SIZE
#define	MAX_RESP_SIZE	256
#endif

/* Errors returned by the framework */
#define	IA_SUCCESS		0	/* Normal function return */
#define	IA_DLFAIL		1	/* Dlopen failure */
#define	IA_SYMFAIL		2	/* Symbol not found */
#define	IA_SCHERROR		3	/* Error in underlying scheme */
#define	IA_SYSERR		4	/* System error - out of memory, etc. */
#define	IA_BUFERR		5	/* Static buffer error */
#define	IA_CONV_FAILURE		6	/* start_conv() or cont_conv() */
					/*   failure */

/* Errors returned by ia_auth_xxx() */
#define	IA_MAXTRYS		7	/* Maximum number of trys exceeded */
#define	IA_NO_PWDENT		8	/* No account present for user */
#define	IA_AUTHTEST_FAIL  	9	/* Authentication test failure */
#define	IA_NEWTOK_REQD		10	/* Get a new authentication token */
					/*   from the user */

/* Errors returned by ia_open/close_session() */
#define	IA_NOENTRY		11	/* No entry found */
#define	IA_ENTRYFAIL		12	/* Couldn't remove the entry */

/* Errors returned by ia_chauthtok(), ia_s(g)et_authtokattr() */
#define	IA_NOPERM		13	/* No permission */
#define	IA_FMERR		14	/* Authentication token file */
					/*   manipulation error */
#define	IA_FATAL		15	/* Old authentication token file */
					/*   cannot be recovered */
#define	IA_FBUSY		16	/* Authentication token file */
					/*   lock busy */
#define	IA_BADAGE		17	/* Authentication token aging */
					/*   is disabled */

/* Errors returned by ia_setcred() */
#define	IA_BAD_GID  		18	/* Invalid Group ID */
#define	IA_INITGP_FAIL 		19	/* Initialization of group IDs failed */
#define	IA_BAD_UID 		20	/* Invaid User ID */
#define	IA_SETGP_FAIL 		21	/* Set of group IDs failed */

/* Errors returned by ia_auth_xxx() */
#define	IA_TOK_EXPIRED 		22	/* Password expired and no longer */
					/* usable */

/* repository */
#define	R_DEFAULT	0x0
#define	R_FILES		0x01
#define	R_NIS		0x02
#define	R_NISPLUS	0x04
#define	R_OPWCMD	0x08		/* for nispasswd, yppasswd */
#define	IS_FILES(x)	((x & R_FILES) == R_FILES)
#define	IS_NIS(x)	((x & R_NIS) == R_NIS)
#define	IS_NISPLUS(x)	((x & R_NISPLUS) == R_NISPLUS)
#define	IS_OPWCMD(x)	((x & R_OPWCMD) == R_OPWCMD)

/*
 * structure ia_status is used by all authentication schemes for passing
 * scheme specific infomation  to the application layer
 */
struct ia_status {
	int	iast_status;		/* Status code returned from scheme */
	char	iast_info[128];		/* Misc scheme specific info */
};

/*
 * structure ia_message is used to pass prompt, error message,
 * or any text information from scheme to application/user.
 */

struct ia_message {
	int msg_style;		/* Msg_style - see below */
	int msg_item_id;	/* Message item identifier */
	char *msg; 		/* Message string */
	int msg_len;		/* The number of charaters in the msg string */
};

/*
 * msg_style defines the interaction style between the
 * scheme and the application.
 */
#define	IA_PROMPT_ECHO_OFF	1	/* Echo off when getting response */
#define	IA_PROMPT_ECHO_ON	2 	/* Echo on when getting response */
#define	IA_ERROR_MSG		3	/* Error message */
#define	IA_TEXTINFO		4	/* Textual information */

/*
 * structure ia_response is used by the scheme to get the user's
 * response back from the application/user.
 */

struct ia_response {
	char *resp;		/* Response string */
	int resp_len;		/* Length of response */
	int resp_retcode;	/* Return code - for future use */
};

/*
 * structure ia_conv is used by authentication applications for passing
 * call back function pointers and application data pointers to the scheme
 */
struct ia_conv {
	int (*start_conv)(int, int, struct ia_message **,
	    struct ia_response **, void *);
	int (*cont_conv)(int, int, struct ia_message **,
	    struct ia_response **, void *);
	void (*end_conv)(int, void *);
	void *appdata_ptr;		/* Application data ptr */
};

/*
 * ia_start() is called to initiate an authentication exchange
 * with PAM.
 */
extern int
ia_start(
	char *program_name,		/* Program Name */
	char *user,			/* User Name */
	char *ttyn,			/* Login tty */
	char *rhost,			/* Remote host */
	struct ia_conv *,		/* Conversation structure */
	void	**iah			/* Address to store handle */
);

/*
 * ia_end() is called to end an authentication exchange with PAM.
 */
extern int
ia_end(
	void *iah			/* ia handle from ia_start() */
);


/*
 * ia_set_item is called to store an object in PAM static data area.
 */
extern int
ia_set_item(
	void *iah, 			/* Authentication handle */
	int item_type, 			/* Type of object - see below */
	void *item			/* Address of place to put pointer */
					/*   to object */
);

/*
 * ia_get_item is called to retrieve an object from the static data area
 */
extern int
ia_get_item(
	void *iah, 			/* Authentication handle */
	int item_type, 			/* Type of object - see below */
	void **				/* Address of place to put pointer */
					/*   to object */
);



#define	IA_MAX_IADS	32		/* Maximum # of authentication handle */
#define	IA_PROGRAM	1		/* The program name */
#define	IA_USER		2		/* The user name */
#define	IA_TTYN		3		/* The tty name */
#define	IA_RHOST	4		/* The remote host name */
#define	IA_CONV		5		/* The conversation structure */
#define	IA_AUTHTOK	6		/* The authentication token */

/*
 * ia_auth_netuser is to validate that current user can access the current
 * machine from a remote system.
 */
extern int
ia_auth_netuser(
	void *iah, 		/* Authentication handle */
	char *an_ruser, 	/* Users name on remote system */
	struct ia_status *	/* Return status */
);


/*
 * ia_auth_user is to authenticate the current user.
 * A pointer to a passwd structure for the new user is
 * assigned to location provided by passwd**.
 */
extern int
ia_auth_user(
	void *iah, 		/* Authentication handle */
	int au_flag, 		/* Flags - see below */
	struct passwd **, 	/* Passwd struct for user */
	struct ia_status *	/* Status - see note below */
);



/*
 * Flags for the au_flag field
 */

#define	AU_CONTINUE	0x00000001	/* Continue the authentication */
					/*   process even if an error is */
					/*   encountered, to hide the source */
					/*   of the error from the user */
#define	AU_PASSWD_REQ	0x00000002	/* Don't allow authentication of  */
					/*   users that don't have assigned */
					/*   passwords */
#define	AU_CHECK_PASSWD	0x00000004	/* Check the password */

/* Values return in ia_status.iast_status: */

#define	AU_PWD_ENTERED	1		/* A password was typed in */

/*
 * ia_auth_port	is called to determine whether the current user is allowed to
 * access te current host via the current port.
 */
extern int
ia_auth_port(
	void *iah, 		/* Authentication handle */
	int ap_flag, 		/* Flags - see below */
	struct ia_status *	/* Status */
);

/*
 * Flags for the ap_flag field
 */
#define	AP_NOCNT  	1		/* Don't count the failure */

extern int
ia_auth_acctmg(
	void *iah, 		/* Authentication handle */
	int flag, 		/* Flag - accepts on AU_PASSWD_REQ */
	struct passwd **, 	/* Addess to return passwd structure */
	struct ia_status *	/* Status */
);


/*
 * ia_open_session is called to note the initiation of new session in the
 * appropriate administrative data bases.
 */
extern int
ia_open_session(
	void *iah, 		/* Authentication handle */
	int is_flags,		/* Flags - see below */
	int type,		/* type of utmp/wtmp entry */
	char is_id[],		/* 4 byte id field for utmp */
	struct ia_status *	/* Status */
);



/* Flags for the is_flags field */

#define	IS_UPDATE_ENT	1		/* Update an existing entry */
#define	IS_NOLOG	2		/* Don't log the new session */
#define	IS_LOGIN	4		/* login type entry (sigh...) */

/*
 * ia_close_session records the termination of a session.
 */
extern int
ia_close_session(
	void	*iah,			/* Authentication handle */
	int	flags,			/* control utmp/wtmp processing */
	pid_t	cs_pid,			/* logout process id */
	int	cs_status,		/* logout process status */
	char	cs_id[],		/* logout ut_id (/etc/inittab id) */
	struct	ia_status *		/* status returned from scheme */
);

#define	IS_NOOP		8		/* No utmp action desired */

/* ia_setcred is called to set the credentials of the current process */

extern int
ia_setcred(
	void *iah, 		/* Authentication handle */
	int sc_flags, 		/* Flags - see below */
	uid_t uid, 		/* User ID to set for this process */
	gid_t gid, 		/* Group ID */
	int ngroups, 		/* Number of groups */
	gid_t *grouplist, 	/* Group list */
	struct ia_status *	/* Return Status */
);

/* sc_flags indicates specific set credential actions */

#define	SC_INITGPS	0x00000001	/* Request to initgroups() */
#define	SC_SETGPS	0x00000002	/* Request to setgroups() */
#define	SC_SETEGID	0x00000004	/* Set effective gid only */
#define	SC_SETGID	0x00000008	/* Set real gid */
#define	SC_SETEUID	0x00000010	/* Set effective uid only */
#define	SC_SETUID	0x00000020	/* Set real uid */
#define	SC_SETEID	(SC_SETEGID|SC_SETEUID)
					/* Set effective ids only */
#define	SC_SETRID	(SC_SETGID|SC_SETUID)	/* Set real ids */

/* ia_chauthtok is called to change authentication token */

extern int
ia_chauthtok(
	void *iah, 		/* Authentication handle */
	struct ia_status *,	/* Status */
	int	repository,
	char	*domain
);

/* ia_set_authtokattr is called to set authentication token attributes */

extern int
ia_set_authtokattr(
	void *iah, 		/* Authentication handle */
	char **sa_setattr,	/* Pointer to an array */
				/*   of attr/value pairs */
	struct ia_status *,	/* Status */
	int repository,
	char	*domain
);

/* ia_get_authtokattr is called to get authentication token attributes */

extern int
ia_get_authtokattr(
	void *iah, 		/* Authentication handle */
	char ***ga_getattr,	/* Pointer to a pointer to */
				/*   an array of attr/value pairs */
	struct ia_status *,	/* Status */
	int	repository,
	char	*domain
);


#ifdef	__cplusplus
}
#endif

#endif /* _IA_APPL_H */
