#ident	"@(#)getent.h	1.4	93/12/15 SMI"

#define	TRUE	1
#define	FALSE	0

#define	EXC_SUCCESS		0
#define	EXC_SYNTAX		1
#define	EXC_NAME_NOT_FOUND	2
#define	EXC_ENUM_NOT_SUPPORTED	3

extern int dogetpw(const char **);
extern int dogetgr(const char **);
extern int dogethost(const char **);
extern int dogetserv(const char **);
extern int dogetnet(const char **);
extern int dogetproto(const char **);
extern int dogetethers(const char **);
