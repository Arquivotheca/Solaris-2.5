/*
 *	Copyright (c) 1993 by Sun Microsystems, Inc
 */

#pragma ident	"@(#)sock.h	1.4	94/04/15 SMI"


int __accept(struct _si_user *siptr, struct sockaddr *addr,
		int *addrlen, sigset_t *accmaskp);
int __bind(struct _si_user *siptr, struct sockaddr *name,
		int namelen, char *raddr, int *raddrlen);

int __getpeername(struct _si_user *siptr, struct sockaddr *name, int *namelen);
int __getsockname(struct _si_user *siptr, struct sockaddr *name, int *namelen);

int __setsockopt(struct _si_user *siptr, int level, int optname,
		char *optval, int optlen);

int _connect2(struct _si_user *siptr, struct t_call *sndcall,
		sigset_t *connmaskp);


void _s_aligned_copy(char *buf, int len, int init_offset,
		char *datap, int *rtn_offset);
struct _si_user	*_s_checkfd(int fd);
void _s_close(struct _si_user *siptr);
int _s_cpaddr(struct _si_user *siptr, char *to, int tolen,
		char *from, int fromlen);
int _s_do_ioctl(int fd, char *buf, int size, int cmd, int *retlen);
int _s_is_ok(struct _si_user *siptr, long type, struct strbuf *ctlbufp);
int _s_min(int x, int y);
int _s_max(int x, int y);
int _s_rcv_conn_con(struct _si_user *siptr, struct strbuf *ctlbufp);
int _s_snd_conn_req(struct _si_user *siptr, struct t_call *call,
		struct strbuf *ctlbufp);
int _s_soreceivexx(struct _si_user *siptr, int flags, char *buf, int len,
		struct sockaddr *from, int *fromlen,
		char *accrights, int *accrightslen,
		sigset_t *rcvmaskp);
int _s_sosendxx(struct _si_user *siptr, int flags, char *buf, int len,
		struct sockaddr *to, int tolen,
		char *accrights, int accrightslen,
		sigset_t *sndmaskp);
int _s_synch(struct _si_user *);
int _s_uxpathlen(struct sockaddr_un *un);

struct _si_user	*find_silink(int fd);
int s_getfflags(struct _si_user *);
void s_invalfflags(struct _si_user *);

/*
 * Socket library debugging
 */
extern int		_s_sockdebug;
#ifdef SOCKDEBUG
#undef SOCKDEBUG
#endif
#ifdef lint
#define	SOCKDEBUG(S, A, B)
#else
#define	SOCKDEBUG(S, A, B)	\
			if ((((S) && (S)->udata.so_options & SO_DEBUG)) || \
						_s_sockdebug) { \
				(void) _syslog(LOG_ERR, (A), (B)); \
			}
#endif
