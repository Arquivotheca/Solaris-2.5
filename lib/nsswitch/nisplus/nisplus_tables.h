/*
 *	nisplus_tables.h
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nisplus_tables.h	1.12	93/03/18 SMI"

#define	PW_TBLNAME		"passwd"
#define	PW_TYP		"passwd_tbl"
#define	PW_NDX_NAME		0
#define	PW_TAG_NAME		"name"
#define	PW_NDX_PASSWD	1
#define	PW_TAG_PASSWD	"passwd"
#define	PW_NDX_UID		2
#define	PW_TAG_UID		"uid"
#define	PW_NDX_GID		3
#define	PW_TAG_GID		"gid"
#define	PW_NDX_GCOS		4
#define	PW_TAG_GCOS		"gcos"
#define	PW_NDX_HOME		5
#define	PW_TAG_HOME		"home"
#define	PW_NDX_SHELL		6
#define	PW_TAG_SHELL		"shell"
#define	PW_NDX_SHADOW	7
#define	PW_TAG_SHADOW	"shadow"
#define	PW_COL		8

#define	GR_TBLNAME		"group"
#define	GR_TYP		"group_tbl"
#define	GR_NDX_NAME		0
#define	GR_TAG_NAME		"name"
#define	GR_NDX_PASSWD	1
#define	GR_TAG_PASSWD	"passwd"
#define	GR_NDX_GID		2
#define	GR_TAG_GID		"gid"
#define	GR_NDX_MEM		3
#define	GR_TAG_MEM		"members"
#define	GR_COL		4

#define	HOST_TBLNAME		"hosts"
#define	HOST_TYP		"hosts_tbl"
#define	HOST_NDX_CNAME	0
#define	HOST_TAG_CNAME	"cname"
#define	HOST_NDX_NAME	1
#define	HOST_TAG_NAME	"name"
#define	HOST_NDX_ADDR	2
#define	HOST_TAG_ADDR	"addr"
#define	HOST_NDX_COMMENT	3
#define	HOST_TAG_COMMENT	"comment"
#define	HOST_COL		4

#define	NET_TBLNAME		"networks"
#define	NET_TYP		"networks_tbl"
#define	NET_NDX_CNAME	0
#define	NET_TAG_CNAME	"cname"
#define	NET_NDX_NAME		1
#define	NET_TAG_NAME		"name"
#define	NET_NDX_ADDR		2
#define	NET_TAG_ADDR		"addr"
#define	NET_NDX_COMMENT	3
#define	NET_TAG_COMMENT	"comment"
#define	NET_COL		4

#define	PROTO_TBLNAME		"protocols"
#define	PROTO_TYP		"protocols_tbl"
#define	PROTO_NDX_CNAME	0
#define	PROTO_TAG_CNAME	"cname"
#define	PROTO_NDX_NAME	1
#define	PROTO_TAG_NAME	"name"
#define	PROTO_NDX_NUMBER	2
#define	PROTO_TAG_NUMBER	"number"
#define	PROTO_NDX_COMMENT	3
#define	PROTO_TAG_COMMENT	"comment"
#define	PROTO_COL		4

#define	RPC_TBLNAME		"rpc"
#define	RPC_TYP		"rpc_tbl"
#define	RPC_NDX_CNAME	0
#define	RPC_TAG_CNAME	"cname"
#define	RPC_NDX_NAME		1
#define	RPC_TAG_NAME		"name"
#define	RPC_NDX_NUMBER	2
#define	RPC_TAG_NUMBER	"number"
#define	RPC_NDX_COMMENT	3
#define	RPC_TAG_COMMENT	"comment"
#define	RPC_COL		4

#define	SERV_TBLNAME		"services"
#define	SERV_TYP		"services_tbl"
#define	SERV_NDX_CNAME	0
#define	SERV_TAG_CNAME	"cname"
#define	SERV_NDX_NAME	1
#define	SERV_TAG_NAME	"name"
#define	SERV_NDX_PROTO	2
#define	SERV_TAG_PROTO	"proto"
#define	SERV_NDX_PORT	3
#define	SERV_TAG_PORT	"port"
#define	SERV_NDX_COMMENT	4
#define	SERV_TAG_COMMENT	"comment"
#define	SERV_COL		5

/* common for hosts, networks, services, protocols, rpc */
#define	NETDB_COL	4
#define	NETDB_NDX_CNAME	0
#define	NETDB_NDX_NAME	1

#define	ETHER_TBLNAME		"ethers"
#define	ETHER_TYP		"ethers_tbl"
#define	ETHER_NDX_ADDR	0
#define	ETHER_TAG_ADDR	"addr"
#define	ETHER_NDX_NAME	1
#define	ETHER_TAG_NAME	"name"
#define	ETHER_NDX_COMMENT	2
#define	ETHER_TAG_COMMENT	"comment"
#define	ETHER_COL		3

/*
 * One way to implement netgroups.  This has the same contents as the YP
 *   'netgroup' map, but we represent each netgroup member as a separate
 *   entry.  Netgroup members may be either (host, user, domain) triples or
 *   recursive references to other netgroups;  we use separate (and
 *   mutually exclusive) columns to represent the two sorts of members.
 */
#define	NETGR_TBLNAME		"netgroup"
#define	NETGR_TYP		"netgroup_tbl"
#define	NETGR_NDX_NAME	0
#define	NETGR_TAG_NAME	"name"
#define	NETGR_NDX_GROUP	1
#define	NETGR_TAG_GROUP	"group"
#define	NETGR_NDX_HOST	2
#define	NETGR_TAG_HOST	"host"
#define	NETGR_NDX_USER	3
#define	NETGR_TAG_USER	"user"
#define	NETGR_NDX_DOMAIN	4
#define	NETGR_TAG_DOMAIN	"domain"
#define	NETGR_NDX_COMMENT	5
#define	NETGR_TAG_COMMENT	"comment"
#define	NETGR_COL		6

#define	BOOTPARAM_TBLNAME		"bootparams"
#define	BOOTPARAM_TYP		"bootparams_tbl"
#define	BOOTPARAM_NDX_KEY		0
#define	BOOTPARAM_TAG_KEY		"key"
#define	BOOTPARAM_NDX_DATUM		1
#define	BOOTPARAM_TAG_DATUM		"datum"
#define	BOOTPARAM_COL		2

/* According to Mukesh: */

/*
 * netmasks stuff implemented in /usr/src/cmd/cmd-inet/usr.sbin/ifconfig using
 * statically linked backends because diskless booting requirements do not
 * permit using dlopen() stuff
 */
#define	NETMASK_TBLNAME		"netmasks"
#define	NETMASK_TYP		"netmasks_tbl"
#define	NETMASK_NDX_ADDR	0
#define	NETMASK_TAG_ADDR	"addr"
#define	NETMASK_NDX_MASK	1
#define	NETMASK_TAG_MASK	"mask"
#define	NETMASK_NDX_COMMENT	2
#define	NETMASK_TAG_COMMENT	"comment"
#define	NETMASK_COL		3

/* macros to get values out of NIS+ entry objects */
#define	EC_LEN(ecp, ndx)		((ecp)[ndx].ec_value.ec_value_len)
#define	EC_VAL(ecp, ndx)		((ecp)[ndx].ec_value.ec_value_val)
#define	EC_SET(ecp, ndx, l, v) \
		((l) = EC_LEN(ecp,ndx), (v) = EC_VAL(ecp,ndx))
