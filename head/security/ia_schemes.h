/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */


#ifndef	_IA_SCHEMES_H
#define	_IA_SCHEMES_H

#pragma ident	"@(#)ia_schemes.h	1.10	94/11/09 SMI"

#ifdef __cplusplus
extern "C" {
#endif


#define	AM_SCHEME	0
#define	EM_SCHEME	1
#define	PM_SCHEME	2
#define	SM_SCHEME	3
#define	XM_SCHEME	4
#define	AM_SCHEME_LIB	"/usr/lib/security/pam_authen.so"
#define	EM_SCHEME_LIB	"/usr/lib/security/pam_entry.so"
#define	PM_SCHEME_LIB	"/usr/lib/security/pam_pwmgt.so"
#define	SM_SCHEME_LIB	"/usr/lib/security/pam_session.so"
#define	XM_SCHEME_LIB	"/usr/lib/security/pam_extern.so"
#define	AM_SCHEME_ETC	"/etc/lib/pam_authen.so"
#define	EM_SCHEME_ETC	"/etc/lib/pam_entry.so"
#define	PM_SCHEME_ETC	"/etc/lib/pam_pwmgt.so"
#define	SM_SCHEME_ETC	"/etc/lib/pam_session.so"
#define	XM_SCHEME_ETC	"/etc/lib/pam_extern.so"

/* define authentication scheme structure */

struct scheme {
	void	*handle;
	int	(*sa_auth_netuser)();
	int	(*sa_auth_user)();
	int	(*sa_auth_port)();
	int	(*sa_auth_acctmg)();
	int	(*sa_open_session)();
	int	(*sa_close_session)();
	int	(*sa_setcred)();
	int	(*sa_set_authtokattr)();
	int	(*sa_get_authtokattr)();
	int	(*sa_chauthtok)();
};

/*
 * When adding a new ia_XXXX function you need to define one of these
 * to get the name of the sa_auth structure that you intend on calling.
 */
#define	SA_AUTH_NETUSER		"sa_auth_netuser"
#define	SA_AUTH_USER		"sa_auth_user"
#define	SA_AUTH_PORT		"sa_auth_port"
#define	SA_AUTH_ACCTMG		"sa_auth_acctmg"
#define	SA_OPEN_SESSION		"sa_open_session"
#define	SA_CLOSE_SESSION	"sa_close_session"
#define	SA_SETCRED		"sa_setcred"
#define	SA_SET_AUTHTOKATTR	"sa_set_authtokattr"
#define	SA_GET_AUTHTOKATTR	"sa_get_authtokattr"
#define	SA_CHAUTHTOK		"sa_chauthtok"

#ifdef __cplusplus
}
#endif

#endif	/* _IA_SCHEMES_H */
