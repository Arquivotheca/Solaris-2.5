#ifndef lint
static char sccsid[] = "@(#)au_to.c 1.14 93/10/01 SMI";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <unistd.h>
#include <bsm/audit.h>
#include <bsm/audit_record.h>
#include <bsm/libbsm.h>
#ifdef SunOS_CMW
#include <cmw/priv.h>
#endif
#include <sys/ipc.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/time.h>
#ifdef SunOS_CMW
#include <sys/label.h>
#endif
#include <sys/vnode.h>
#include <malloc.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <string.h>

#define	NGROUPS		16	/* XXX - temporary */

#ifdef __STDC__
static token_t *au_to_exec(char **, char);
#else
static token_t *au_to_exec();
#endif

static token_t *
get_token(s)
	int s;
{
	token_t *token;	/* Resultant token */

	if ((token = (token_t *)malloc(sizeof (token_t))) == (token_t *)0)
		return ((token_t *)0);
	if ((token->tt_data = malloc(s)) == (char *)0) {
		free(token);
		return ((token_t *)0);
	}
	token->tt_size = s;
	token->tt_next = (token_t *)0;
	return  (token);
}

/*
 * au_to_header
 * return s:
 *	pointer to header token.
 */
token_t *
#ifdef __STDC__
au_to_header(au_event_t e_type, au_emod_t e_mod)
#else
au_to_header(e_type, e_mod)
	au_event_t e_type;
	au_emod_t e_mod;
#endif
{
	adr_t adr;			/* adr memory stream header */
	token_t *token;			/* token pointer */
	char data_header = AUT_HEADER;	/* header for this token */
	char version = TOKEN_VERSION;	/* version of token family */
	long byte_count;
	long time[2];

	token = get_token(2*sizeof (char) + 3*sizeof (long) + 2*sizeof (short));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);	/* token ID */
	adr_long(&adr, &byte_count, 1);		/* length of audit record */
	adr_char(&adr, &version, 1);		/* version of audit tokens */
	adr_short(&adr, &e_type, 1);		/* event ID */
	adr_short(&adr, &e_mod, 1);		/* event ID modifier */
	adr_long(&adr, (long *)time, 2);	/* time & date */

	return (token);
}

/*
 * au_to_trailer
 * return s:
 *	pointer to a trailer token.
 */
token_t *
au_to_trailer()
{
	adr_t adr;				/* adr memory stream header */
	token_t *token;				/* token pointer */
	char data_header = AUT_TRAILER;		/* header for this token */
	short magic = (short)AUT_TRAILER_MAGIC;	/* trailer magic number */
	long byte_count;

	token = get_token(sizeof (char) + sizeof (long) + sizeof (short));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);	/* token ID */
	adr_short(&adr, &magic, 1);		/* magic number */
	adr_long(&adr, &byte_count, 1);		/* length of audit record */

	return (token);
}

/*
 * au_to_arg
 * return s:
 *	pointer to an argument token.
 */
token_t *
#ifdef __STDC__
au_to_arg(char n, char *text, u_long v)
#else
au_to_arg(n, text, v)
	char n;		/* argument # being used */
	char *text;	/* optional text describing argument */
	u_long v;	/* argument value */
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_ARG;	/* header for this token */
	short bytes;			/* length of string */

	bytes = strlen(text) + 1;

	token = get_token((int)(2*sizeof (char) + sizeof (long) +
		sizeof (short) + bytes));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);	/* token type */
	adr_char(&adr, &n, 1);			/* argument id */
	adr_long(&adr, (long *)&v, 1);		/* argument value */
	adr_short(&adr, &bytes, 1);
	adr_char(&adr, text, bytes);

	return (token);
}

/*
 * au_to_attr
 * return s:
 *	pointer to an attribute token.
 */
token_t *
#ifdef __STDC__
au_to_attr(struct vattr *attr)
#else
au_to_attr(attr)
	struct vattr *attr;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_ATTR;	/* header for this token */
	long value;

	token = get_token(sizeof (char)+(sizeof (long)*6));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	value = (long)attr->va_mode;
	adr_long(&adr, &value, 1);
	value = (long)attr->va_uid;
	adr_long(&adr, &value, 1);
	value = (long)attr->va_gid;
	adr_long(&adr, &value, 1);
	adr_long(&adr, (long *)&(attr->va_fsid), 1);
	adr_long(&adr, (long *)&(attr->va_nodeid), 1);
	value = (long)attr->va_rdev;
	adr_long(&adr, &value, 1);

	return (token);
}

/*
 * au_to_data
 * return s:
 *	pointer a data token.
 */
token_t *
#ifdef __STDC__
au_to_data(char unit_print, char unit_type, char unit_count, char *p)
#else
au_to_data(unit_print, unit_type, unit_count, p)
	char unit_print;
	char unit_type;
	char unit_count;
	char *p;
#endif
{
	adr_t adr;			/* adr memory stream header */
	token_t *token;			/* token pointer */
	char data_header = AUT_DATA;	/* header for this token */
	int byte_count;			/* number of bytes */

	if (p == (char *)0 || unit_count < 1)
		return  ((token_t *)0);

	/*
	 * Check validity of print type
	 */
	if (unit_print < AUP_BINARY || unit_print > AUP_STRING)
		return  ((token_t *)0);

	switch (unit_type) {
	case AUR_SHORT:
		byte_count = unit_count * sizeof (short);
		break;
	case AUR_INT:
	case AUR_LONG:
		byte_count = unit_count * sizeof (long);
		break;
	/* case AUR_CHAR: */
	case AUR_BYTE:
		byte_count = unit_count * sizeof (char);
		break;
	default:
		return  ((token_t *)0);
	}

	token = get_token((int)(4 * sizeof (char) + byte_count));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, &unit_print, 1);
	adr_char(&adr, &unit_type, 1);
	adr_char(&adr, &unit_count, 1);

	switch (unit_type) {
	case AUR_SHORT:
		adr_short(&adr, (short *)p, unit_count);
		break;
	case AUR_INT:
	case AUR_LONG:
		adr_long(&adr, (long *)p, unit_count);
		break;
	/* case AUR_CHAR: */
	case AUR_BYTE:
		adr_char(&adr, p, unit_count);
		break;
	}

	return  (token);
}

/*
 * au_to_process
 * return s:
 *	pointer to a process token.
 */

token_t *
#ifdef __STDC__
au_to_process(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid, gid_t rgid,
		pid_t pid, au_asid_t sid, au_tid_t *tid)
#else
au_to_process(auid, euid, egid, ruid, rgid, pid, sid, tid)
	au_id_t auid;
	uid_t euid;
	gid_t egid;
	uid_t ruid;
	gid_t rgid;
	pid_t pid;
	au_asid_t sid;
	au_tid_t *tid;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_PROCESS;	/* header for this token */

	token = get_token(sizeof (char) + 9 * sizeof (long));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, &auid, 1);
	adr_long(&adr, &euid, 1);
	adr_long(&adr, &egid, 1);
	adr_long(&adr, &ruid, 1);
	adr_long(&adr, &rgid, 1);
	adr_long(&adr, &pid, 1);
	adr_long(&adr, &sid, 1);
	adr_long(&adr, (long *)tid, 2);

	return  (token);
}

/* au_to_seq
 * return s:
 *	pointer to token chain containing a sequence token
 */
token_t *
#ifdef __STDC__
au_to_seq(long audit_count)
#else
au_to_seq(long audit_count)
	long audit_count;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_SEQ;	/* header for this token */

	token = get_token(sizeof (char) + sizeof (long));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, &audit_count, 1);

	return (token);
}

/*
 * au_to_socket
 * return s:
 *	pointer to mbuf chain containing a socket token.
 */
token_t *
#ifdef __STDC__
au_to_socket(struct socket *so)
#else
au_to_socket(so)
	struct socket *so;
#endif
{
	adr_t adr;
	token_t *token;
	char data_header = AUT_SOCKET;
	struct inpcb *inp = (struct inpcb *)so->so_pcb;

	token = get_token(sizeof (char) + sizeof (short)*3 + sizeof (long)*2);
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, (short *)&so->so_type, 1);
	adr_short(&adr, (short *)&inp->inp_lport, 1);
	adr_long(&adr, (long *)&inp->inp_laddr, 1);
	adr_short(&adr, (short *)&inp->inp_fport, 1);
	adr_long(&adr, (long *)&inp->inp_faddr, 1);

	return  (token);
}

/* au_to_subject
 * return s:
 *	pointer to a process token.
 */

token_t *
#ifdef __STDC__
au_to_subject(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid, gid_t rgid,
		pid_t pid, au_asid_t sid, au_tid_t *tid)
#else
au_to_subject(auid, euid, egid, ruid, rgid, pid, sid, tid)
	au_id_t auid;
	uid_t euid;
	gid_t egid;
	uid_t ruid;
	gid_t rgid;
	pid_t pid;
	au_asid_t sid;
	au_tid_t *tid;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_SUBJECT;	/* header for this token */

	token = get_token(sizeof (char) + 9 * sizeof (long));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, &auid, 1);
	adr_long(&adr, &euid, 1);
	adr_long(&adr, &egid, 1);
	adr_long(&adr, &ruid, 1);
	adr_long(&adr, &rgid, 1);
	adr_long(&adr, &pid, 1);
	adr_long(&adr, &sid, 1);
	adr_long(&adr, (long *)tid, 2);

	return  (token);
}

/*
 * au_to_me
 * return s:
 *	pointer to a process token.
 */

token_t *
au_to_me()
{
	auditinfo_t info;

	if (getaudit(&info))
		return ((token_t *)0);
	return  (au_to_subject(info.ai_auid, geteuid(), getegid(), getuid(),
			    getgid(), getpid(), info.ai_asid, &info.ai_termid));
}
/*
 * au_to_text
 * return s:
 *	pointer to a text token.
 */
token_t *
#ifdef __STDC__
au_to_text(char *text)
#else
au_to_text(text)
	char *text;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_TEXT;	/* header for this token */
	short bytes;			/* length of string */

	bytes = strlen(text) + 1;
	token = get_token((int)(sizeof (char) + sizeof (short) + bytes));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, &bytes, 1);
	adr_char(&adr, text, bytes);

	return  (token);
}

/*
 * au_to_path
 * return s:
 *	pointer to a path token.
 */
token_t *
#ifdef __STDC__
au_to_path(char *path)
#else
au_to_path(path)
	char *path;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_PATH;	/* header for this token */
	short bytes;			/* length of string */

	bytes = (short) strlen(path) + 1;

	token = get_token((int)(sizeof (char) +  sizeof (short) + bytes));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, &bytes, 1);
	adr_char(&adr, path, bytes);

	return  (token);
}

/*
 * au_to_cmd
 * return s:
 *	pointer to an command line argument token
 */
token_t *
#ifdef __STDC__
au_to_cmd(u_long argc, char **argv, char **envp)
#else
au_to_cmd(argc, argv, envp)
	u_long argc;
	char **argv;
	char **envp;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_CMD;	/* header for this token */
	short len = 0;
	short cnt = 0;
	short envc = 0;

	/*
	 * one char for the header, one long for argc,
	 * one long for # envp strings.
	 */
	len = sizeof (char) + sizeof (short) + sizeof (short);

	/* get sizes of strings */

	for (cnt = 0; cnt < argc; cnt++) {
		len += (short) sizeof (short);
		len += (short) (strlen(argv[cnt]) + 1);
	}

	if (envp != (char **)0) {
		for (envc = 0; envp[envc] != (char *)0; envc++) {
			len += (short) sizeof (short);
			len += (short) (strlen(envp[envc]) + 1);
		}
	}

	token = get_token(len);
	if (token == (token_t *)0)
		return ((token_t *)0);

	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);

	adr_short(&adr, (short *)&argc, 1);

	for (cnt = 0; cnt < argc; cnt++) {
		len = (short) (strlen(argv[cnt]) + 1);
		adr_short(&adr, &len, 1);
		adr_char(&adr, argv[cnt], len);
	}

	adr_short(&adr, &envc, 1);

	for (cnt = 0; cnt < envc; cnt++) {
		len = (short) (strlen(envp[cnt]) + 1);
		adr_short(&adr, &len, 1);
		adr_char(&adr, envp[cnt], len);
	}

	return  (token);
}

/*
 * au_to_exit
 * return s:
 *	pointer to a exit value token.
 */
token_t *
#ifdef __STDC__
au_to_exit(int retval, int err)
#else
au_to_exit(retval, err)
	int retval;
	int err;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_EXIT;	/* header for this token */

	token = get_token(sizeof (char) + (2 * sizeof (long)));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, (long *)&retval, 1);
	adr_long(&adr, (long *)&err, 1);

	return  (token);
}

/*
 * au_to_return
 * return s:
 *	pointer to a return  value token.
 */
token_t *
#ifdef __STDC__
au_to_return (char number, u_int value)
#else
au_to_return (number, value)
	char number;
	u_int value;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_RETURN;	/* header for this token */

	token = get_token(2 * sizeof (char) + sizeof (long));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, &number, 1);
	adr_long(&adr, (long *)&value, 1);

	return (token);
}

/*
 * au_to_opaque
 * return s:
 *	pointer to a opaque token.
 */
token_t *
#ifdef __STDC__
au_to_opaque(char *opaque, short bytes)
#else
au_to_opaque(opaque, bytes)
	char *opaque;
	short bytes;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_OPAQUE;	/* header for this token */

	if (bytes < 1)
		return  ((token_t *)0);

	token = get_token((int)(sizeof (char) + sizeof (short) + bytes));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, &bytes, 1);
	adr_char(&adr, opaque, bytes);

	return  (token);
}

/*
 * au_to_in_addr
 * return s:
 *	pointer to a internet address token
 */
token_t *
#ifdef __STDC__
au_to_in_addr(struct in_addr *internet_addr)
#else
au_to_in_addr(internet_addr)
	struct in_addr *internet_addr;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_IN_ADDR;	/* header for this token */

	token = get_token(sizeof (char) + sizeof (long));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, (long *)internet_addr, 1);

	return (token);
}

/*
 * au_to_iport
 * return s:
 *	pointer to token chain containing a ip port address token
 */
token_t *
#ifdef __STDC__
au_to_iport(u_short iport)
#else
au_to_iport(iport)
	u_short iport;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_IPORT;	/* header for this token */

	token = get_token(sizeof (char) + sizeof (short));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, (short *)&iport, 1);

	return  (token);
}

token_t *
#ifdef __STDC__
au_to_ipc(int id)
#else
au_to_ipc(id)
	int id;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_IPC;	/* header for this token */

	token = get_token(sizeof (char) + sizeof (int));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, (long *)&id, 1);

	return (token);
}


/*
 * The Modifier tokens
 */

/*
 * au_to_groups
 * return s:
 *	pointer to a group list token.
 *
 * This function is obsolete.  Please use au_to_newgroups.
 */
token_t *
#ifdef __STDC__
au_to_groups(int *groups)
#else
au_to_groups(groups)
	int *groups;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_GROUPS;	/* header for this token */

	token = get_token(sizeof (char) + NGROUPS * sizeof (long));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, (long *)groups, NGROUPS);

	return  (token);
}

/*
 * au_to_newgroups
 * return s:
 *	pointer to a group list token.
 */
token_t *
#ifdef __STDC__
au_to_newgroups(int n, gid_t *groups)
#else
au_to_newgroups(n, groups)
	int n;
	gid_t *groups;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_NEWGROUPS;	/* header for this token */
	short n_groups;

	if (n < NGROUPS_UMIN || n > NGROUPS_UMAX || groups == (gid_t *)0)
		return ((token_t *)0);
	token = get_token(sizeof (char) + sizeof (short) + n * sizeof (gid_t));
	if (token == (token_t *)0)
		return ((token_t *)0);
	n_groups = (short)n;
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, &n_groups, 1);
	adr_long(&adr, (long *)groups, n_groups);

	return (token);
}

/*
 * au_to_exec_args
 * returns:
 *	pointer to an exec args token.
 */
token_t *
#ifdef __STDC__
au_to_exec_args(char **argv)
#else
au_to_exec_args(argv)
	char **argv;
#endif
{
	return (au_to_exec(argv, AUT_EXEC_ARGS));
}

/*
 * au_to_exec_env
 * returns:
 *	pointer to an exec args token.
 */
token_t *
#ifdef __STDC__
au_to_exec_env(char **envp)
#else
au_to_exec_env(envp)
	char **envp;
#endif
{
	return (au_to_exec(envp, AUT_EXEC_ENV));
}

/*
 * au_to_exec
 * returns:
 *	pointer to an exec args token.
 */
static token_t *
#ifdef __STDC__
au_to_exec(char **v, char data_header)
#else
au_to_exec(v, data_header)
	char **v;
	char data_header;
#endif
{
	token_t *token;
	adr_t adr;
	char **p;
	long n = 0;
	int len = 0;

	for (p = v; *p != NULL; p++) {
		len += strlen(*p) + 1;
		n++;
	}
	token = get_token(sizeof (char) + sizeof (long) + len);
	if (token == (token_t *)NULL)
		return ((token_t *)NULL);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, &n, 1);
	for (p = v; *p != NULL; p++) {
		adr_char(&adr, *p, strlen(*p) + 1);
	}
	return (token);
}

/*
 * au_to_xatom
 * return s:
 *	pointer to a xatom token.
 */
token_t *
#ifdef __STDC__
au_to_xatom(u_short len, char *atom)
#else
au_to_xatom(len, atom)
	u_short len;
	char *atom;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_XATOM;	/* header for this token */

	token = get_token((int)(sizeof (char) + sizeof (u_short) + len));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, (short *)&len, 1);
	adr_char(&adr, atom, len);

	return  (token);
}

/*
 * au_to_xproto
 * return s:
 *	pointer to a X protocol token.
 */
token_t *
#ifdef __STDC__
au_to_xproto(pid_t pid)
#else
au_to_xproto(pid)
	pid_t pid;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_XPROTO;	/* header for this token */

	token = get_token(sizeof (char) + sizeof (pid));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, &pid, 1);

	return  (token);
}

/*
 * au_to_xobj
 * return s:
 *	pointer to a X object token.
 */
token_t *
#ifdef __STDC__
au_to_xobj(int oid, long xid, long cuid)
#else
au_to_xobj(oid, xid, cuid)
	int oid;
	long xid;
	long cuid;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_XOBJ;	/* header for this token */

	token = get_token(sizeof (char) + 3*sizeof (long));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, (long *)&oid, 1);
	adr_long(&adr, &xid, 1);
	adr_long(&adr, &cuid, 1);

	return (token);
}

/*
 * au_to_xselect
 * return s:
 *	pointer to a X select token.
 */
token_t *
#ifdef __STDC__
au_to_xselect(char *pstring, char *type, short dlen, char *data)
#else
au_to_xselect(pstring, type, dlen, data)
	char  *pstring;
	char  *type;
	short  dlen;
	char  *data;
#endif
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_XSELECT;	/* header for this token */
	short bytes;

	bytes = strlen(pstring) + strlen(type) + 2 + dlen;
	token = get_token((int)(sizeof (char)+(sizeof (short)*3)+bytes));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	bytes = strlen(pstring) + 1;
	adr_short(&adr, &bytes, 1);
	adr_char(&adr, pstring, bytes);
	bytes = strlen(type) + 1;
	adr_short(&adr, &bytes, 1);
	adr_char(&adr, type, bytes);
	adr_short(&adr, &dlen, 1);
	adr_char(&adr, data, dlen);
	return  (token);
}

#ifdef	SunOS_CMW
/*
 * au_to_ilabel
 * return s:
 *	pointer to an information label token.
 */
token_t *
au_to_ilabel(label)
	bilabel_t *label;
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_ILABEL;	/* header for this token */
	short bs = sizeof (bilabel_t);

	token = get_token(sizeof (char) + bs);
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, (char *)label, bs);

	return  (token);
}


/*
 * au_to_ipc_perm
 * return s:
 *	pointer to token containing a System V IPC attribute token.
 */
token_t *
au_to_ipc_perm(perm)
	struct ipc_perm *perm;
{
	token_t *token;				/* local token */
	adr_t adr;				/* adr memory stream header */
	char data_header = AUT_IPC_PERM;	/* header for this token */
	long value;

	token = get_token(sizeof (char) + (sizeof (long)*7));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	value = (long) perm->uid;
	adr_long(&adr, &value, 1);
	value = (long) perm->gid;
	adr_long(&adr, &value, 1);
	value = (long) perm->cuid;
	adr_long(&adr, &value, 1);
	value = (long) perm->cgid;
	adr_long(&adr, &value, 1);
	value = (long) perm->mode;
	adr_long(&adr, &value, 1);
	value = (long) perm->seq;
	adr_long(&adr, &value, 1);
	value = (long) perm->key;
	adr_long(&adr, &value, 1);

	return (token);
}


/*
 * au_to_slabel
 * return s:
 *	pointer to token chain containing a sensitivity label token.
 */
token_t *
au_to_slabel(label)
	bslabel_t *label;
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_SLABEL;	/* header for this token */
	short bs = sizeof (bslabel_t);

	token = get_token(sizeof (char) + bs);
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, (char *)label, bs);

	return (token);
}

/*
 * au_to_clearance
 * return s:
 *	pointer to token chain containing a clearance token.
 */
token_t *
au_to_clearance(clear)
	bclear_t *clear;
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_CLEAR;	/* header for this token */
	short bs = sizeof (bclear_t);

	token = get_token(sizeof (char) + bs);
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, (char *)clear, bs);

	return  (token);
}

/*
 * au_to_privilege
 * return s:
 *	pointer to token chain containing a privilege token.
 */
token_t *
au_to_privilege(p_set)
	priv_set_t p_set;
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_PRIV;	/* header for this token */
	short bs = sizeof (priv_set_t);

	token = get_token(sizeof (char) + bs);
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, (char *)p_set, bs);

	return  (token);
}

/*
 * au_to_priv
 * return s:
 *	pointer to token chain containing a use of a privilege token.
 */
token_t *
au_to_priv(flag, priv)
	char   flag;
	priv_t priv;
{
	token_t *token;			/* local token */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_UPRIV;	/* header for this token */

	token = get_token(sizeof (char) + sizeof (priv_t) + sizeof (char));
	if (token == (token_t *)0)
		return ((token_t *)0);
	adr_start(&adr, token->tt_data);
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, &flag, 1);		/* success/failure */
	adr_long(&adr, (long *)&priv, 1);	/* privilege value */

	return  (token);
}

#endif	/* SunOS_CMW */
