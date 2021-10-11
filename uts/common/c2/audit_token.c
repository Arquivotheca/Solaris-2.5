/*
 * @(#)audit_token.c 2.12 92/02/29 SMI; SunOS CMW
 * @(#)audit_token.c 4.2.1.2 91/05/08 SMI; BSM Module
 *
 * Support routines for building audit records.
 */

#include <sys/param.h>
#include <sys/systm.h>		/* for rval */
#include <sys/time.h>
#include <sys/types.h>
#include <sys/vnode.h>
#include <sys/mode.h>
#include <sys/user.h>
#include <sys/session.h>
#include <sys/ipc.h>
#include <sys/cmn_err.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <net/route.h>
#include <netinet/in_pcb.h>
#include <c2/audit.h>
#include <c2/audit_kernel.h>
#include <c2/audit_record.h>

extern kmutex_t  au_seq_lock;
/*
 * These are the control tokens
 */

/*
 * au_to_header
 * returns:
 *	pointer to au_membuf chain containing a header token.
 */
token_t *
au_to_header(byte_count, e_type, e_mod)

	long byte_count;
	short e_type;
	short e_mod;
{
	adr_t adr;			/* adr memory stream header */
	token_t *m;			/* au_membuf pointer */
	char data_header = AUT_HEADER;	/* header for this token */
	char version = TOKEN_VERSION;	/* version of token family */

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);	/* token ID */
	adr_long(&adr, (long *)&byte_count, 1);	/* length of audit record */
	adr_char(&adr, &version, 1);		/* version of audit tokens */
	adr_short(&adr, &e_type, 1);		/* event ID */
	adr_short(&adr, &e_mod, 1);		/* event ID modifier */
	adr_long(&adr, (long *)&hrestime, 2);	/* time & date */

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_trailer
 * returns:
 *	pointer to au_membuf chain containing a trailer token.
 */
token_t *
au_to_trailer(byte_count)

	long byte_count;
{
	adr_t adr;				/* adr memory stream header */
	token_t *m;				/* au_membuf pointer */
	char data_header = AUT_TRAILER;		/* header for this token */
	short magic = (short) AUT_TRAILER_MAGIC; /* trailer magic number */

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);	/* token ID */
	adr_short(&adr, &magic, 1);		/* magic number */
	adr_long(&adr, &byte_count, 1);		/* length of audit record */

	m->len = adr_count(&adr);

	return (m);
}
/*
 * These are the data tokens
 */

/*
 * au_to_data
 * returns:
 *	pointer to au_membuf chain containing a data token.
 */
token_t *
au_to_data(unit_print, unit_type, unit_count, p)

	char unit_print;
	char unit_type;
	char unit_count;
	char *p;
{
	adr_t adr;			/* adr memory stream header */
	token_t *m;			/* au_membuf pointer */
	char data_header = AUT_DATA;	/* header for this token */

	if (p == (char *)0)
		return (au_to_text("au_to_data: NULL pointer"));
	if (unit_count == 0)
		return (au_to_text("au_to_data: Zero unit count"));

	switch (unit_type) {
	case AUR_SHORT:
		if (sizeof (short) *unit_count >= AU_BUFSIZE)
			return (au_to_text("au_to_data: unit count too big"));
		break;
	case AUR_INT:
	case AUR_LONG:
		if (sizeof (long) *unit_count >= AU_BUFSIZE)
			return (au_to_text("au_to_data: unit count too big"));
		break;
	case AUR_BYTE:
	default:
		if (sizeof (char) *unit_count >= AU_BUFSIZE)
			return (au_to_text("au_to_data: unit count too big"));
		break;
	}

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
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
	case AUR_BYTE:
	default:
		adr_char(&adr, p, unit_count);
		break;
	}

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_process
 * au_to_subject
 * returns:
 *	pointer to au_membuf chain containing a process token.
 */
static token_t *au_to_any_process(char, struct proc *);

token_t *
au_to_process(pp)

	struct proc *pp;
{
	return (au_to_any_process(AUT_PROCESS, pp));
}

token_t *
au_to_subject(pp)

	struct proc *pp;
{
	return (au_to_any_process(AUT_SUBJECT, pp));
}

#ifdef	AU_MAY_USE_SOMEDAY
token_t *
au_to_server(pp)

	struct proc *pp;
{
	return (au_to_any_process(AUT_SERVER, pp));
}

#endif	AU_MAY_USE_SOMEDAY

/* should use credential from thread!!! */
static token_t *
au_to_any_process(char data_header, struct proc *pp)
{
	token_t *m;	/* local au_membuf */
	adr_t adr;	/* adr memory stream header */
	struct p_audit_data *pad = (struct p_audit_data *) P2A(pp);
	long temp = pad->pad_auid;
	long value;

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, &(temp), 1);
	value = (long)CRED()->cr_uid;
	adr_long(&adr, &value, 1);
	value = (long)CRED()->cr_gid;
	adr_long(&adr, &value, 1);
	value = (long)CRED()->cr_ruid;
	adr_long(&adr, &value, 1);
	value = (long)CRED()->cr_rgid;
	adr_long(&adr, &value, 1);
	value = (long)pp->p_pid;
	adr_long(&adr, &value, 1);
	value = (long)pad->pad_asid;
	adr_long(&adr, &value, 1);
	adr_long(&adr, (long *)&(pad->pad_termid.port), 1);
	adr_char(&adr, (char *)&(pad->pad_termid.machine), 4);

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_text
 * returns:
 *	pointer to au_membuf chain containing a text token.
 */
token_t *
au_to_text(text)

	char *text;
{
	token_t *token;			/* local au_membuf */
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_TEXT;	/* header for this token */
	short bytes;			/* length of string */

	token = au_getclr(au_wait);

	bytes = strlen(text) + 1;
	adr_start(&adr, memtod(token, char *));
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, &bytes, 1);

	token->len = (char) adr_count(&adr);
	/*
	 * Now really get the path
	 */
	for (; bytes > 0; bytes -= AU_BUFSIZE, text += AU_BUFSIZE) {
		m = au_getclr(au_wait);
		(void) au_append_buf(text,
			bytes > AU_BUFSIZE ? AU_BUFSIZE : bytes, m);
		au_append_rec((token_t *)token, (token_t *)m);
	}

	return (token);
}

/*
 * au_to_exec_strings
 * returns:
 *	pointer to au_membuf chain containing a argv/arge token.
 */
token_t *
au_to_exec_strings(header, argvp, start, count)

	char header;
	u_int *argvp;
	u_int start;
	int count;
{
	token_t *token;			/* local au_membuf */
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */

	token = au_getclr(au_wait);

	adr_start(&adr, memtod(token, char *));
	adr_char(&adr, &header, 1);
	adr_long(&adr, (long *) (&count), 1);

	token->len = (char) adr_count(&adr);
	/*
	 * Now really get the path
	 */
	for (; count > 0; count--, argvp++) {
		m = au_getclr(au_wait);
		(void) au_append_buf((char *) (*argvp + start),
			strlen((char *) (*argvp + start)) + 1, m);
		au_append_rec((token_t *)token, (token_t *)m);
	}

	return (token);
}

/*
 * au_to_exec_args
 * returns:
 *	pointer to au_membuf chain containing a argv token.
 */
token_t *
au_to_exec_args(argvp, start, count)

	u_int *argvp;
	u_int start;
	int count;
{
	char data_header = AUT_EXEC_ARGS;	/* header for this token */

	return (au_to_exec_strings(data_header, argvp, start, count));
}

/*
 * au_to_exec_env
 * returns:
 *	pointer to au_membuf chain containing a arge token.
 */
token_t *
au_to_exec_env(argvp, start, count)

	u_int *argvp;
	u_int start;
	int count;
{
	char data_header = AUT_EXEC_ENV;	/* header for this token */

	return (au_to_exec_strings(data_header, argvp, start, count));
}

/*
 * au_to_arg
 * returns:
 *	pointer to au_membuf chain containing an argument token.
 */
token_t *
au_to_arg(n, text, v)

	char   n;	/* argument # being used */
	char  *text;	/* optional text describing argument */
	u_long v;	/* argument value */
{
	token_t *token;			/* local au_membuf */
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_ARG;	/* header for this token */
	short bytes;			/* length of string */

	token = au_getclr(au_wait);

	bytes = strlen(text) + 1;
	adr_start(&adr, memtod(token, char *));
	adr_char(&adr, &data_header, 1);	/* token type */
	adr_char(&adr, &n, 1);			/* argument id */
	adr_long(&adr, (long *)&v, 1);		/* argument value */
	adr_short(&adr, &bytes, 1);

	token->len = adr_count(&adr);
	/*
	 * Now really get the path
	 */
	for (; bytes > 0; bytes -= AU_BUFSIZE, text += AU_BUFSIZE) {
		m = au_getclr(au_wait);
		(void) au_append_buf(text,
			bytes > AU_BUFSIZE ? AU_BUFSIZE : bytes, m);
		au_append_rec((token_t *)token, (token_t *)m);
	}

	return (token);
}


/*
 * au_to_path
 * returns:
 *	pointer to au_membuf chain containing a path token.
 */
token_t *
au_to_path(path, pathlen)

	char  *path;
	u_int  pathlen;
{
	token_t *token;			/* local au_membuf */
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_PATH;	/* header for this token */
	short bytes;			/* length of string */
	struct p_audit_data *pad;	/* per process audit data */

	dprintf(0x1000, ("au_to_path(%x,%x)\n", path, pathlen));
	call_debug(0x1000);
	/*
	 * We simulate behaviour of adr_string so that we don't need
	 * any form of intermediate buffers.
	 */

	pad = (struct p_audit_data *) P2A(curproc);
		/* DEBUG sanity checks */
	if (pad == (struct p_audit_data *) 0)
		panic("au_to_path: no process audit structure");
	if (pad->pad_cwrd == (struct cwrd *) 0)
		panic("au_to_path: no process cwrd structure");

	bytes = (short) pathlen;
	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, &bytes, 1);

	m->len = adr_count(&adr);

		/* OPTIMIZATION - use clusters if path long enough */
	for (token = m; bytes > 0; bytes -= AU_BUFSIZE, path += AU_BUFSIZE) {
		m = au_getclr(au_wait);
		(void) au_append_buf(path,
			bytes > AU_BUFSIZE ? AU_BUFSIZE : bytes, m);
		token = au_append_token(token, m);
	}

	dprintf(0x1000, ("au_to_path: token: %x\n", token));
	call_debug(0x1000);

	return (token);
}

#ifdef REMOVE
/*
 * au_to_path_chain
 * returns:
 *	pointer to au_membuf chain containing a path token.
 */
token_t *
au_to_path_chain(path)

	token_t *path;
{
	token_t *token;			/* local au_membuf */
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_PATH;	/* header for this token */
	short bytes;			/* length of string */

	/*
	 * We simulate behaviour of adr_string so that we don't need
	 * any form of intermediate buffers.
	 */

	bytes = au_token_size(u.u_cwd->cw_root) + 1;
	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, &bytes, 1);

	m->len = adr_count(&adr);

	token = au_append_token(m, au_gather(u.u_cwd->cw_root));

	bytes = au_token_size(u.u_cwd->cw_dir) + 1;
	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_short(&adr, &bytes, 1);

	m->len = adr_count(&adr);

	token = au_append_token(token, m);
	token = au_append_token(token, au_gather(u.u_cwd->cw_dir));

	bytes = au_token_size(path) + 1;
	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_short(&adr, &bytes, 1);

	m->len = adr_count(&adr);

	token = au_append_token(token, m);
	token = au_append_token(token, au_gather(path));

	return (token);
}
#endif

/*
 * au_to_ipc
 * returns:
 *	pointer to au_membuf chain containing a System V IPC token.
 */
token_t *
au_to_ipc(type, id)

	char	type;
	int	id;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_IPC;	/* header for this token */

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, &type, 1);		/* type of IPC object */
	adr_long(&adr, (long *)&id, 1);

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_return
 * returns:
 *	pointer to au_membuf chain containing a return value token.
 */
token_t *
au_to_return(error, rvp)

	int error;
	rval_t *rvp;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_RETURN;	/* header for this token */
	long val;
	char ed = error;

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, &ed, 1);

	if (error) {
		val = -1;
		adr_long(&adr, (long *)&(val), 1);
	} else {
		val = (rvp)?rvp->r_val1:0;
		adr_long(&adr, (long *)&val, 1);
	}
	m->len = adr_count(&adr);

	return (m);
}

#ifdef	AU_MAY_USE_SOMEDAY
/*
 * au_to_opaque
 * returns:
 *	pointer to au_membuf chain containing a opaque token.
 */
token_t *
au_to_opaque(bytes, opaque)

	short bytes;
	char *opaque;
{
	token_t *token;			/* local au_membuf */
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_OPAQUE;	/* header for this token */

	token = au_getclr(au_wait);

	adr_start(&adr, memtod(token, char *));
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, &bytes, 1);

	token->len = adr_count(&adr);

	for (; bytes > 0; bytes -= AU_BUFSIZE, opaque += AU_BUFSIZE) {
		m = au_getclr(au_wait);
		(void) au_append_buf(
			opaque, bytes > AU_BUFSIZE ? AU_BUFSIZE : bytes, m);
		token = au_append_token(token, m);
	}

	return (token);
}
#endif	AU_MAY_USE_SOMEDAY

/*
 * au_to_ip
 * returns:
 *	pointer to au_membuf chain containing a ip header token
 */
token_t *
au_to_ip(ipp)

	struct ip *ipp;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_IP;	/* header for this token */

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, (char *)ipp, 2);
	adr_short(&adr, &(ipp->ip_len), 3);
	adr_char(&adr, (char *)&(ipp->ip_ttl), 2);
	adr_short(&adr, (short *)&(ipp->ip_sum), 1);
	adr_long(&adr, (long *)&(ipp->ip_src), 2);

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_iport
 * returns:
 *	pointer to au_membuf chain containing a ip path token
 */
token_t *
au_to_iport(iport)

	u_short iport;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_IPORT;	/* header for this token */

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, (short *)&iport, 1);

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_in_addr
 * returns:
 *	pointer to au_membuf chain containing a ip path token
 */
token_t *
au_to_in_addr(internet_addr)

	struct in_addr *internet_addr;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_IN_ADDR;	/* header for this token */

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, (char *)internet_addr, 4);

	m->len = adr_count(&adr);

	return (m);
}
/*
 * The Modifier tokens
 */

/*
 * au_to_attr
 * returns:
 *	pointer to au_membuf chain containing an attribute token.
 */
token_t *
au_to_attr(attr)

	struct vattr *attr;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_ATTR;	/* header for this token */
	long value;

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	value = (long)attr->va_mode;
	ADDTRACE("[%x] au_to_attr: type %x, mode %x\n", attr->va_type, value,
		0, 0, 0, 0);
	value |= (long) (VTTOIF(attr->va_type));
	adr_long(&adr, &value, 1);
	value = (long)attr->va_uid;
	adr_long(&adr, &value, 1);
	value = (long)attr->va_gid;
	adr_long(&adr, &value, 1);
	adr_long(&adr, (long *)&(attr->va_fsid), 1);
	adr_long(&adr, (long *)&(attr->va_nodeid), 1);
	value = (long)attr->va_rdev;
	adr_long(&adr, &value, 1);

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_ipc_perm
 * returns:
 *	pointer to au_membuf chain containing a System V IPC attribute token.
 */
token_t *
au_to_ipc_perm(perm)

	struct ipc_perm *perm;
{
	token_t *m;				/* local au_membuf */
	adr_t adr;				/* adr memory stream header */
	char data_header = AUT_IPC_PERM;	/* header for this token */
	long value;

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
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

	m->len = adr_count(&adr);

	return (m);
}

#ifdef NOTYET
/*
 * au_to_label
 * returns:
 *	pointer to au_membuf chain containing a label token.
 */
token_t *
au_to_label(label)

	bilabel_t *label;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_LABEL;	/* header for this token */
	short bs = sizeof (bilabel_t);

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, &bs, 1);
	adr_char(&adr, (char *)label, bs);

	m->len = adr_count(&adr);

	return (m);
}
#endif	/* NOTYET */

#ifdef OLD_GROUP
token_t *
au_to_groups(pp)

	struct proc *pp;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_GROUPS;	/* header for this token */

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, (long *)pp->p_cred->cr_groups, ngroups_max);

	m->len = adr_count(&adr);

	return (m);
}
#endif /* OLD_GROUP */

token_t *
au_to_groups(pp)
	struct proc *pp;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_NEWGROUPS;	/* header for this token */
	short n_groups;

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	n_groups = (short) (pp->p_cred->cr_ngroups);
	adr_short(&adr, &n_groups, 1);
	adr_long(&adr, (long *)pp->p_cred->cr_groups,
		(int) pp->p_cred->cr_ngroups);

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_socket
 * returns:
 *	pointer to au_membuf chain containing a socket token.
 */
token_t *
au_to_socket(so)

	struct socket *so;
{
	adr_t adr;
	token_t *m;
	char data_header = AUT_SOCKET;
	struct inpcb *inp = (struct inpcb *)so->so_pcb;

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_short(&adr, (short *)&so->so_type, 1);
	adr_short(&adr, (short *)&inp->inp_lport, 1);
	adr_char(&adr, (char *)&inp->inp_laddr, 4);
	adr_short(&adr, (short *)&inp->inp_fport, 1);
	adr_char(&adr, (char *)&inp->inp_faddr, 4);

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_seq
 * returns:
 *	pointer to au_membuf chain containing a sequence token.
 */
token_t *
au_to_seq()
{
	adr_t adr;
	token_t *m;
	char data_header = AUT_SEQ;
	extern long audit_count;

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	mutex_enter(&au_seq_lock);
	audit_count ++;
	mutex_exit(&au_seq_lock);
	adr_long(&adr, &audit_count, 1);

	m->len = adr_count(&adr);

	return (m);
}

#ifdef	SunOS_CMW
/*
 * au_to_ilabel
 * returns:
 *	pointer to au_membuf chain containing a information label token.
 */
token_t *
au_to_ilabel(label)
	bilabel_t *label;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_ILABEL;	/* header for this token */
	short bs = sizeof (bilabel_t);

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, (char *)label, bs);

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_slabel
 * returns:
 *	pointer to au_membuf chain containing a sensitivity label token.
 */
token_t *
au_to_slabel(label)

	bslabel_t *label;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_SLABEL;	/* header for this token */
	short bs = sizeof (bslabel_t);

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, (char *)label, bs);
	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_clearance
 * returns:
 *	pointer to au_membuf chain containing a clearance token.
 */
token_t *
au_to_clearance(clear)

	bclear_t *clear;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_CLEAR;	/* header for this token */
	short bs = sizeof (bclear_t);

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, (char *)clear, bs);
	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_privilege
 * returns:
 *	pointer to au_membuf chain containing a privilege token.
 */
token_t *
au_to_privilege(p_set)

	priv_set_t p_set;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_PRIV;	/* header for this token */
	short bs = sizeof (priv_set_t);

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, (char *)p_set, bs);

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_priv
 * returns:
 *	pointer to au_membuf chain containing a use of a privilege token.
 */
token_t *
au_to_priv(priv, flag)

	priv_t priv;
	int    flag;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_UPRIV;	/* header for this token */
	char sf = (char)flag;

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_char(&adr, &sf, 1);			/* success/failure */
	adr_long(&adr, (long *)&priv, 1);	/* privilege value */

	m->len = adr_count(&adr);

	return (m);
}

/*
 * au_to_liaison
 * returns:
 *	pointer to au_membuf chain containing a secureware liason token
 */
token_t *
au_to_liaison(liaison)

	u_long liaison;
{
	token_t *m;			/* local au_membuf */
	adr_t adr;			/* adr memory stream header */
	char data_header = AUT_LIAISON;	/* header for this token */

	m = au_getclr(au_wait);

	adr_start(&adr, memtod(m, char *));
	adr_char(&adr, &data_header, 1);
	adr_long(&adr, (long *)&liaison, 1);		/* success/failure */

	m->len = adr_count(&adr);

	return (m);
}
#endif	/* SunOS_CMW */
