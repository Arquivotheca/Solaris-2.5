/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

/*
 *	File history:
 *	@(#)audit_record.h 2.12 92/01/30 SMI; SunOS CMW
 *	@(#)audit_record.h 4.2.1.2 91/05/08 SMI; BSM Module
 *	@(#)adr.h 2.2 91/06/05 SMI; SunOS CMW
 *	@(#)adr.h 4.2.1.2 91/05/08 SMI; BSM Module
 *
 * Minimum and maximum record type values.  Change AUR_MAXRECTYPE when
 * adding new record types.
 */

#ifndef _BSM_AUDIT_RECORD_H
#define	_BSM_AUDIT_RECORD_H

#pragma ident	"@(#)audit_record.h	1.33	94/03/09 SMI"

#include <sys/socket.h>
#include <sys/socketvar.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Audit record token type codes
 */

/*
 * Control token types
 */

#define	AUT_INVALID		((char) 0x00)
#define	AUT_OTHER_FILE		((char) 0x11)
#define	AUT_OHEADER		((char) 0x12)
#define	AUT_TRAILER		((char) 0x13)
#define	AUT_HEADER		((char) 0x14)
#define	AUT_TRAILER_MAGIC	((short) 0xB105)

/*
 * Data token types
 */

#define	AUT_DATA		((char) 0x21)
#define	AUT_IPC			((char) 0x22)
#define	AUT_PATH		((char) 0x23)
#define	AUT_SUBJECT		((char) 0x24)
#define	AUT_SERVER		((char) 0x25)
#define	AUT_PROCESS		((char) 0x26)
#define	AUT_RETURN		((char) 0x27)
#define	AUT_TEXT		((char) 0x28)
#define	AUT_OPAQUE		((char) 0x29)
#define	AUT_IN_ADDR		((char) 0x2A)
#define	AUT_IP			((char) 0x2B)
#define	AUT_IPORT		((char) 0x2C)
#define	AUT_ARG			((char) 0x2D)
#define	AUT_SOCKET		((char) 0x2E)
#define	AUT_SEQ			((char) 0x2F)

/*
 * Modifier token types
 */

#define	AUT_ATTR		((char) 0x31)
#define	AUT_IPC_PERM		((char) 0x32)
#define	AUT_LABEL		((char) 0x33)
#define	AUT_GROUPS		((char) 0x34)
#define	AUT_ILABEL		((char) 0x35)
#define	AUT_SLABEL		((char) 0x36)
#define	AUT_CLEAR		((char) 0x37)
#define	AUT_PRIV		((char) 0x38)
#define	AUT_UPRIV		((char) 0x39)
#define	AUT_LIAISON		((char) 0x3A)
#define	AUT_NEWGROUPS		((char) 0x3B)
#define	AUT_EXEC_ARGS		((char) 0x3C)
#define	AUT_EXEC_ENV		((char) 0x3D)

/*
 * X windows token types
 */

#define	AUT_XATOM		((char) 0x40)
#define	AUT_XOBJ		((char) 0x41)
#define	AUT_XPROTO		((char) 0x42)
#define	AUT_XSELECT		((char) 0x43)

/*
 * Command token types
 */

#define	AUT_CMD   		((char) 0x51)
#define	AUT_EXIT   		((char) 0x52)
#define	AUT_ACL			((char) 0x60)

/*
 * Audit print suggestion types.
 */

#define	AUP_BINARY	((char) 0)
#define	AUP_OCTAL	((char) 1)
#define	AUP_DECIMAL	((char) 2)
#define	AUP_HEX		((char) 3)
#define	AUP_STRING	((char) 4)

/*
 * Audit data member types.
 */

#define	AUR_BYTE	((char) 0)
#define	AUR_CHAR	((char) 0)
#define	AUR_SHORT	((char) 1)
#define	AUR_INT		((char) 2)
#define	AUR_LONG	((char) 3)

/*
 * audit token IPC types (shm, sem, msg) [for ipc attribute]
 */

#define	AT_IPC_MSG	((char) 1)		/* message IPC id */
#define	AT_IPC_SEM	((char) 2)		/* semaphore IPC id */
#define	AT_IPC_SHM	((char) 3)		/* shared memory IPC id */

/*
 * Version of audit attributes
 */

#define	TOKEN_VERSION	2

#ifdef _KERNEL
/*
 * Audit token type is really an au_membuf pointer
 */
typedef au_buff_t token_t;
/*
 * token generation functions
 */
token_t *au_append_token();
token_t *au_getclr();
token_t *au_set();
void au_toss_token();

token_t *au_to_attr();
token_t *au_to_data();
token_t *au_to_header();
token_t *au_to_ipc();
token_t *au_to_ipc_perm();
token_t *au_to_iport();
token_t *au_to_in_addr();
token_t *au_to_ip();
token_t *au_to_groups();
#ifdef	SunOS_CMW
token_t *au_to_label();
token_t *au_to_ilabel();
token_t *au_to_slabel();
token_t *au_to_clearance();
token_t *au_to_privilege();
token_t *au_to_priv();
token_t *au_to_liaison();
#endif	/* SunOS_CMW */
token_t *au_to_path();
token_t *au_to_seq();
token_t *au_to_process();
token_t *au_to_subject();
token_t *au_to_return();
token_t *au_to_text();
token_t *au_to_trailer();
token_t *au_to_arg();
token_t *au_to_socket();
token_t *au_to_exec_args();
token_t *au_to_exec_env();

void au_uwrite();
void au_close(caddr_t *, int, short, short);
void au_free_rec(au_buff_t *);
void au_queue_kick(int);
void audit_dont_stop();
void au_write();
void au_mem_init(void);
void au_enqueue(au_buff_t *);
int	au_isqueued(void);
int	au_doio(struct vnode *, int);
int	au_token_size(token_t *);
int	au_append_rec(au_buff_t *, au_buff_t *);
int	au_append_buf(char *, int, au_buff_t *);

#else /* _KERNEL */

#include <limits.h>
#include <sys/types.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/ipc.h>

struct token_s {
	struct token_s	*tt_next;	/* Next in the list	*/
	short		tt_size;	/* Size of data		*/
	char		*tt_data;	/* The data		*/
};
typedef struct token_s token_t;

struct au_arg_tok {
	u_char num;
	u_long val;
	u_short length;
	char *data;
};
typedef struct au_arg_tok au_arg_tok_t;

struct au_attr_tok {
	u_long mode;
	u_long uid;
	u_long gid;
	long fs;
	long node;
	u_long dev;
};
typedef struct au_attr_tok au_attr_tok_t;

struct au_data_tok {
	u_char pfmt;
	u_char size;
	u_char number;
	char *data;
};
typedef struct au_data_tok au_data_tok_t;

struct au_exit_tok {
	long status;
	long retval;
};
typedef struct au_exit_tok au_exit_tok_t;

struct au_file_tok {
	time_t time;
	time_t msec;
	u_short length;
	char *fname;
};
typedef struct au_file_tok au_file_tok_t;


struct au_groups_tok {
	gid_t groups[NGROUPS_MAX];
};
typedef struct au_groups_tok au_groups_tok_t;

struct au_header_tok {
	u_long length;
	u_char version;
	au_event_t event;
	u_short emod;
	time_t time;
	time_t msec;
};
typedef struct au_header_tok au_header_tok_t;

struct au_inaddr_tok {
	struct in_addr ia;
};
typedef struct au_inaddr_tok au_inaddr_tok_t;

struct au_ip_tok {
	u_char version;
	struct ip ip;
};
typedef struct au_ip_tok au_ip_tok_t;

struct au_ipc_tok {
	key_t id;
};
typedef struct au_ipc_tok au_ipc_tok_t;

struct au_ipc_perm_tok {
	struct ipc_perm ipc_perm;
};
typedef struct au_ipc_perm_tok au_ipc_perm_tok_t;

struct au_iport_tok {
	u_short iport;
};
typedef struct au_iport_tok au_iport_tok_t;

struct au_invalid_tok {
	u_short length;
	char *data;
};
typedef struct au_invalid_tok au_invalid_tok_t;

struct au_opaque_tok {
	u_short length;
	char *data;
};
typedef struct au_opaque_tok au_opaque_tok_t;

struct au_path_tok {
	u_short length;
	char *name;
};
typedef struct au_path_tok au_path_tok_t;

struct au_proc_tok {
	au_id_t auid;
	uid_t euid;
	gid_t egid;
	uid_t ruid;
	gid_t rgid;
	pid_t pid;
	pid_t sid;
	au_tid_t tid;
};
typedef struct au_proc_tok au_proc_tok_t;

struct au_ret_tok {
	u_char error;
	u_long retval;
};
typedef struct au_ret_tok au_ret_tok_t;

struct au_seq_tok {
	u_long num;
};
typedef struct au_seq_tok au_seq_tok_t;

struct au_socket_tok {
	short type;
	u_short lport;
	struct in_addr laddr;
	u_short fport;
	struct in_addr faddr;
};
typedef struct au_socket_tok au_socket_tok_t;

struct au_subj_tok {
	au_id_t auid;
	uid_t euid;
	gid_t egid;
	uid_t ruid;
	gid_t rgid;
	pid_t pid;
	pid_t sid;
	au_tid_t tid;
};
typedef struct au_subj_tok au_subj_tok_t;

struct au_server_tok {
	au_id_t auid;
	uid_t euid;
	uid_t ruid;
	gid_t egid;
	pid_t pid;
};
typedef struct au_server_tok au_server_tok_t;

struct au_text_tok {
	u_short length;
	char *data;
};
typedef struct au_text_tok au_text_tok_t;

struct au_trailer_tok {
	u_short magic;
	u_long length;
};
typedef struct au_trailer_tok au_trailer_tok_t;

struct au_token {
	char id;
	struct au_token *next;
	struct au_token *prev;
	char *data;
	u_short size;
	union {
		au_arg_tok_t arg;
		au_attr_tok_t attr;
		au_data_tok_t data;
		au_exit_tok_t exit;
		au_file_tok_t file;
		au_groups_tok_t groups;
		au_header_tok_t header;
		au_inaddr_tok_t inaddr;
		au_ip_tok_t ip;
		au_ipc_perm_tok_t ipc_perm;
		au_ipc_tok_t ipc;
		au_iport_tok_t iport;
		au_invalid_tok_t invalid;
		au_opaque_tok_t opaque;
		au_path_tok_t path;
		au_proc_tok_t proc;
		au_ret_tok_t ret;
		au_server_tok_t server;
		au_seq_tok_t seq;
		au_socket_tok_t socket;
		au_subj_tok_t subj;
		au_text_tok_t text;
		au_trailer_tok_t trailer;
	} un;
};
typedef struct au_token au_token_t;

#ifdef __STDC__
extern token_t *au_to_arg(char n, char *text, u_long v);
extern token_t *au_to_attr(struct vattr *attr);
extern token_t *au_to_cmd(u_long argc, char **argv, char **envp);
extern token_t *au_to_data(char unit_print, char unit_type,
	char unit_count, char *p);
extern token_t *au_to_exec_args(char **);
extern token_t *au_to_exec_env(char **);
extern token_t *au_to_exit(int retval, int errno);
extern token_t *au_to_groups(int *groups);
extern token_t *au_to_newgroups(int n, gid_t *groups);
extern token_t *au_to_header(au_event_t e_type, au_emod_t e_mod);
extern token_t *au_to_in_addr(struct in_addr *internet_addr);
extern token_t *au_to_ipc(int id);
extern token_t *au_to_ipc_perm(struct ipc_perm *perm);
extern token_t *au_to_iport(u_short iport);
extern token_t *au_to_me(void);
extern token_t *au_to_opaque(char *opaque, short bytes);
extern token_t *au_to_path(char *path);
extern token_t *au_to_process(au_id_t auid, uid_t euid, gid_t egid,
	uid_t ruid, gid_t rgid, pid_t pid, au_asid_t sid, au_tid_t *tid);
extern token_t *au_to_return(char number, u_int value);
extern token_t *au_to_seq(long audit_count);
extern token_t *au_to_socket(struct socket *so);
extern token_t *au_to_subject(au_id_t auid, uid_t euid, gid_t egid,
	uid_t ruid, gid_t rgid, pid_t pid, au_asid_t sid, au_tid_t *tid);
extern token_t *au_to_text(char *text);
extern token_t *au_to_trailer(void);
extern token_t *au_to_xatom(u_short len, char *atom);
extern token_t *au_to_xobj(int oid, long xid, long cuid);
extern token_t *au_to_xproto(pid_t pid);
extern token_t *au_to_xselect(char *pstring, char *type, short dlen,
	char *data);

#ifdef SunOS_CMW
extern token_t *au_to_clearance(bclear_t *clear);
extern token_t *au_to_ilabel(bilabel_t *label);
extern token_t *au_to_priv(char flag, priv_t priv);
extern token_t *au_to_privilege(priv_set_t p_set);
extern token_t *au_to_slabel(bslabel_t *label);
#endif

#else /* not __STDC__ */

extern token_t *au_to_arg();
extern token_t *au_to_attr();
extern token_t *au_to_cmd();
extern token_t *au_to_data();
extern token_t *au_to_exec_args();
extern token_t *au_to_exec_env();
extern token_t *au_to_exit();
extern token_t *au_to_groups();
extern token_t *au_to_newgroups();
extern token_t *au_to_header();
extern token_t *au_to_in_addr();
extern token_t *au_to_ipc();
extern token_t *au_to_ipc_perm();
extern token_t *au_to_iport();
extern token_t *au_to_me();
extern token_t *au_to_opaque();
#ifdef SunOS_CMW
extern token_t *au_to_path();
#else
extern token_t *au_to_path();
#endif
extern token_t *au_to_process();
extern token_t *au_to_return();
extern token_t *au_to_seq();
extern token_t *au_to_socket();
extern token_t *au_to_subject();
extern token_t *au_to_text();
extern token_t *au_to_trailer();
extern token_t *au_to_xatom();
extern token_t *au_to_xobj();
extern token_t *au_to_xproto();
extern token_t *au_to_xselect();

#ifdef SunOS_CMW
extern token_t *au_to_clearance();
extern token_t *au_to_ilabel();
extern token_t *au_to_priv();
extern token_t *au_to_privilege();
extern token_t *au_to_slabel();
#endif

#endif /* __STDC__ */
#endif /* _KERNEL */


/*
 * Adr structures
 */

struct adr_s {
	char *adr_stream;	/* The base of the stream */
	char *adr_now;		/* The location within the stream */
};

typedef struct adr_s adr_t;


#ifdef	_KERNEL

void	adr_char();
void	adr_long();
void	adr_short();
void	adr_start();

char	*adr_getchar();
char	*adr_getshort();
char	*adr_getlong();

int	adr_count();

#endif	/* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* _BSM_AUDIT_RECORD_H */
