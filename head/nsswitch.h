/*
 * Copyright (c) 1991-92, by Sun Microsystems, Inc.
 */

/*
 * nsswitch.h
 *
 * Low-level interface to the name-service switch.  The interface defined
 * in <nss_common.h> should be used in preference to this.
 *
 * This is a Project Private interface.  It may change in future releases.
 *	==== ^^^^^^^^^^^^^^^ ?
 */

#ifndef _NSSWITCH_H
#define	_NSSWITCH_H

#pragma ident	"@(#)nsswitch.h	1.14	94/10/03 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	__NSW_CONFIG_FILE "/etc/nsswitch.conf"

#define	__NSW_HOSTS_DB		"hosts"
#define	__NSW_PASSWD_DB		"passwd"
#define	__NSW_GROUP_DB		"group"
#define	__NSW_NETGROUP_DB	"netgroup"
#define	__NSW_NETWORKS_DB	"networks"
#define	__NSW_PROTOCOLS_DB	"protocols"
#define	__NSW_RPC_DB		"rpc"
#define	__NSW_SERVICES_DB	"services"
#define	__NSW_ETHERS_DB		"ethers"
#define	__NSW_BOOTPARAMS_DB	"bootparams"
#define	__NSW_NETMASKS_DB	"netmasks"
#define	__NSW_BROADCASTADDRS_DB	"broadcastaddrs"
#define	__NSW_MAIL_ALIASES_DB	"aliases"

#define	__NSW_STD_ERRS	4	/* number of reserved errors that follow */

#define	__NSW_SUCCESS	0	/* found the required data */
#define	__NSW_NOTFOUND	1	/* the naming service returned lookup failure */
#define	__NSW_UNAVAIL	2	/* could not call the naming service */
#define	__NSW_TRYAGAIN	3	/* bind error to suggest a retry */

typedef unsigned char action_t;
#define	__NSW_CONTINUE	0	/* the action is to continue to next service */
#define	__NSW_RETURN	1	/* the action is to return to the user */

#define	__NSW_STR_RETURN	"return"
#define	__NSW_STR_CONTINUE	"continue"
#define	__NSW_STR_SUCCESS	"success"
#define	__NSW_STR_NOTFOUND	"notfound"
#define	__NSW_STR_UNAVAIL	"unavail"
#define	__NSW_STR_TRYAGAIN	"tryagain"

/* prefix for all switch shared objects */
#define	__NSW_LIB	"nsw"

enum __nsw_parse_err {
	__NSW_CONF_PARSE_SUCCESS = 0,	/* parser found the required policy */
	__NSW_CONF_PARSE_NOFILE = 1,	/* the policy files does not exist */
	__NSW_CONF_PARSE_NOPOLICY = 2,	/* the required policy is not set */
					/* in the file */
	__NSW_CONF_PARSE_SYSERR = 3	/* system error in the parser */
};


struct __nsw_long_err {
	int nsw_errno;
	action_t action;
	struct __nsw_long_err *next;
};

struct __nsw_lookup {
	char *service_name;
	action_t actions[__NSW_STD_ERRS];
	struct __nsw_long_err *long_errs;
	struct __nsw_lookup *next;
};

struct __nsw_switchconfig {
	int vers;
	char *dbase;
	int num_lookups;
	struct __nsw_lookup *lookups;
};

#define	__NSW_ACTION(lkp, err) 	\
	((lkp)->next == NULL ? \
		__NSW_RETURN \
	: \
		((err) >= 0 && (err) < __NSW_STD_ERRS ? \
			(lkp)->actions[err] \
		: \
			__nsw_extended_action(lkp, err)))

#ifdef __STDC__

struct __nsw_switchconfig *__nsw_getconfig
	(const char *, enum __nsw_parse_err *);
int __nsw_freeconfig(struct __nsw_switchconfig *);
action_t __nsw_extended_action(struct __nsw_lookup *, int);

#else

struct __nsw_switchconfig *__nsw_getconfig();
int __nsw_freeconfig();
action_t __nsw_extended_action();

#endif /* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif /* _NSSWITCH_H */
