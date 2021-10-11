/*	@(#)operator.x 1.13 92/03/11 */
/*
 * operator.x: Operator message protocol
 */
#ifdef RPC_HDR
%#include <config.h>
%#include <sys/types.h>
%#include <sys/param.h>
%#include <rpc/rpc.h>
%#ifdef USG
%#include <netdb.h>
%
%#ifndef bzero
%#define	bzero(s, n)	memset((s), 0, (n))
%#endif
%#endif
%
%#ifdef USG
%#define	OPER_GROUP	"sys"
%#else
%#define	OPER_GROUP	"operator"
%#endif
%
%#define	MAXIDLEN	32
%#define	MAXMSGLEN	512
%
%/*
% * Command codes
% */
#else
%/*LINTLIBRARY*/
%/*PROTOLIB1*/
#endif
enum msg_cmd {
	LOGIN = 1,
	LOGOUT = 2,
	SEND = 3,
	SENDALL = 4
};
#ifdef RPC_HDR
%
%/*
% * A msg_dest struct is used by oper_login and
% * oper_logout to register/unregister a host/program
% * from an operator daemon's destination list
% */
#endif
struct msg_dest {
	char	md_host[BCHOSTNAMELEN];		/* name of requesting host */
	char	md_domain[BCHOSTNAMELEN];	/* name of requester's domain */
	u_long	md_callback;			/* RPC program, 0 == default */
	time_t	md_gen;				/* generation number */
};
#ifdef RPC_HDR
%
%/*
% * A msg_id structure uniquely identifies
% * each message.
% */
#endif
struct msg_id {
	u_long	mid_pid;		/* pid of sender */
	u_long	mid_seq;		/* sender-generated sequence number */
	char	mid_host[BCHOSTNAMELEN]; /* hostname of originator */
	char	mid_domain[BCHOSTNAMELEN]; /* domain of originator */
};
#ifdef RPC_HDR
%
%/*
% * This structure describes a message.
% */
#endif
struct msg_t {
	time_t	msg_time;			/* timestamp */
	time_t	msg_ttl;			/* message's time-to-live */
	msg_id	msg_ident;			/* unique message identifier */
	char	msg_progname[MAXIDLEN];		/* name of sending program */
	char	msg_arbiter[BCHOSTNAMELEN];	/* response arbitrator */
	msg_id	msg_target;		 	/* for reply, cancel, (n)ack */
	u_long	msg_callback;			/* program for RPC callbacks */
	long	msg_auth;			/* auth flavor for callbacks */
	long	msg_uid;			/* user-ID of sender */
	long	msg_gid;			/* group-ID of sender */
	long	msg_level;			/* message's level 0-7 */
	long	msg_type;			/* message's type */
	long	msg_len;			/* length of actual message */
	char	msg_data[MAXMSGLEN];		/* message proper */
};
#ifdef RPC_HDR
%
%/*
% * Message types are constructed
% * from the following bits:
% */
%#define	MSG_DISPLAY	0x1		/* displayed by monitors if set */
%#define	MSG_OPERATOR	0x2		/* send to operators' terminals */
%#define	MSG_FORWARD	0x4		/* recipient should forward msg */
%#define	MSG_NEEDREPLY	0x8		/* message requires a response */
%#define	MSG_REPLY	0x10		/* target == msg to reply to */
%#define	MSG_CANCEL	0x20		/* target == msg to cancel */
%#define	MSG_ACK		0x40		/* target == msg to ACK */
%#define	MSG_NACK	0x80		/* target == msg to NACK */
%#define	MSG_NOTOP	0x100		/* special authentication NACK */
%#define	MSG_SYSLOG	0x200		/* send text to syslog */
%#define	MSG_BEENFWD	0x400		/* an operd has forwarded message */
%
%#define	HASTARGET(x)	((x)&(MSG_REPLY|MSG_CANCEL|MSG_ACK|MSG_NACK|MSG_NOTOP))
%
%#ifdef __STDC__
%extern int	oper_init(const char *, const char *, const int);
%extern int	oper_login(const char *, const int);
%extern int	oper_logout(const char *);
%extern int	oper_getall(const char *);
%extern CLIENT	*oper_connect(const char *, const u_long);
%extern u_long	oper_send(const time_t, const int, const int, const char *);
%extern u_long	oper_reply(const char *, const msg_id *, const char *);
%extern u_long	oper_cancel(const u_long, const int);
%extern void	oper_end(void);
%extern int	oper_control(int, caddr_t);
%extern int	oper_receive(const fd_set *, char *, const int, u_long *);
%extern int	oper_msg(const fd_set *, msg_t *);
%#else
%extern int	oper_init();
%extern int	oper_login();
%extern int	oper_logout();
%extern int	oper_getall();
%extern CLIENT	*oper_connect();
%extern u_long	oper_send();
%extern u_long	oper_reply();
%extern u_long	oper_cancel();
%extern void	oper_end();
%extern int	oper_control();
%extern int	oper_receive();
%extern int	oper_msg();
%#endif
%
%#define	xdr_time_t	xdr_long	/* time_t's are long's */
%
%/*
% * Return codes
% */
%#define	OPERMSG_ERROR		-1
%#define	OPERMSG_SUCCESS		0
%#define	OPERMSG_CONNECTED	1
%
%#define	OPERMSG_READY		0	/* oper_recieve: alt input ready */
%#define	OPERMSG_RCVD		1	/* oper_receive: message received */
%
%/*
% * Control commands
% */
%#define	OPER_SETMSGBUF		1	/* use arg for incoming messages */
%
%/*
% * Time outs
% */
%#define	DAEMON_TIMEOUT		2
%#define	USER_TIMEOUT		15
#endif

program OPERMSG_PROG {
	version OPERMSG_VERS {
		int	OPER_LOGIN(msg_dest) = LOGIN;
		int	OPER_LOGOUT(msg_dest) = LOGOUT;
		int	OPER_SEND(msg_t) = SEND;
		int	OPER_SENDALL(msg_dest) = SENDALL;
	} = 1;
} = 100088;
