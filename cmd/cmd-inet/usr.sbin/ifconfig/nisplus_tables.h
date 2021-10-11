/* 
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */


/* NOTE: This file is copied from /usr/src/lib/nsswitch/nisplus/nisplus_tables.h */
/*       to make use of useful nisplus programming routines. It should track modifications */
/*       to the original file */


#define	NT_PW_RDN		"passwd"
#define	NT_PW_TYP		"passwd_tbl"
#define	NT_PW_NDX_NAME		0
#define	NT_PW_TAG_NAME		"name"
#define	NT_PW_NDX_PASSWD	1
#define	NT_PW_TAG_PASSWD	"passwd"
#define	NT_PW_NDX_UID		2
#define	NT_PW_TAG_UID		"uid"
#define	NT_PW_NDX_GID		3
#define	NT_PW_TAG_GID		"gid"
#define	NT_PW_NDX_GCOS		4
#define	NT_PW_TAG_GCOS		"gcos"
#define	NT_PW_NDX_HOME		5
#define	NT_PW_TAG_HOME		"home"
#define	NT_PW_NDX_SHELL		6
#define	NT_PW_TAG_SHELL		"shell"
#define	NT_PW_NDX_SHADOW	7
#define	NT_PW_TAG_SHADOW	"shadow"
#define	NT_PW_COL		8

#define	NT_GR_RDN		"group"
#define	NT_GR_TYP		"group_tbl"
#define	NT_GR_NDX_NAME		0
#define	NT_GR_TAG_NAME		"name"
#define	NT_GR_NDX_PASSWD	1
#define	NT_GR_TAG_PASSWD	"passwd"
#define	NT_GR_NDX_GID		2
#define	NT_GR_TAG_GID		"gid"
#define	NT_GR_NDX_MEM		3
#define	NT_GR_TAG_MEM		"members"
#define	NT_GR_COL		4

#define	NT_HOST_RDN		"hosts"
#define NT_HOST_TYP		"hosts_tbl"
#define NT_HOST_NDX_CNAME	0
#define	NT_HOST_TAG_CNAME	"cname"
#define	NT_HOST_NDX_NAME	1
#define NT_HOST_TAG_NAME	"name"
#define	NT_HOST_NDX_ADDR	2
#define	NT_HOST_TAG_ADDR	"addr"
#define	NT_HOST_NDX_COMMENT	3
#define	NT_HOST_TAG_COMMENT	"comment"
#define NT_HOST_COL		4

#define	NT_NET_RDN		"networks"
#define NT_NET_TYP		"networks_tbl"
#define NT_NET_NDX_CNAME	0
#define	NT_NET_TAG_CNAME	"cname"
#define	NT_NET_NDX_NAME		1
#define NT_NET_TAG_NAME		"name"
#define	NT_NET_NDX_ADDR		2
#define	NT_NET_TAG_ADDR		"addr"
#define	NT_NET_NDX_COMMENT	3
#define	NT_NET_TAG_COMMENT	"comment"
#define NT_NET_COL		4

#define	NT_PROTO_RDN		"protocols"
#define NT_PROTO_TYP		"protocols_tbl"
#define NT_PROTO_NDX_CNAME	0
#define	NT_PROTO_TAG_CNAME	"cname"
#define	NT_PROTO_NDX_NAME	1
#define NT_PROTO_TAG_NAME	"name"
#define	NT_PROTO_NDX_NUMBER	2
#define	NT_PROTO_TAG_NUMBER	"number"
#define	NT_PROTO_NDX_COMMENT	3
#define	NT_PROTO_TAG_COMMENT	"comment"
#define NT_PROTO_COL		4

#define	NT_RPC_RDN		"rpc"
#define NT_RPC_TYP		"rpc_tbl"
#define NT_RPC_NDX_CNAME	0
#define	NT_RPC_TAG_CNAME	"cname"
#define	NT_RPC_NDX_NAME		1
#define NT_RPC_TAG_NAME		"name"
#define	NT_RPC_NDX_NUMBER	2
#define	NT_RPC_TAG_NUMBER	"number"
#define	NT_RPC_NDX_COMMENT	3
#define	NT_RPC_TAG_COMMENT	"comment"
#define NT_RPC_COL		4

#define	NT_SERV_RDN		"services"
#define NT_SERV_TYP		"services_tbl"
#define NT_SERV_NDX_CNAME	0
#define	NT_SERV_TAG_CNAME	"cname"
#define	NT_SERV_NDX_NAME	1
#define NT_SERV_TAG_NAME	"name"
#define	NT_SERV_NDX_PROTO	2
#define	NT_SERV_TAG_PROTO	"proto"
#define	NT_SERV_NDX_PORT	3
#define	NT_SERV_TAG_PORT	"port"
#define	NT_SERV_NDX_COMMENT	4
#define	NT_SERV_TAG_COMMENT	"comment"
#define NT_SERV_COL		5

#define	NT_ETHER_RDN		"ethers"
#define NT_ETHER_TYP		"ethers_tbl"
#define	NT_ETHER_NDX_ADDR	0
#define	NT_ETHER_TAG_ADDR	"addr"
#define	NT_ETHER_NDX_NAME	1
#define NT_ETHER_TAG_NAME	"name"
#define	NT_ETHER_NDX_COMMENT	2
#define	NT_ETHER_TAG_COMMENT	"comment"
#define NT_ETHER_COL		3

#define	NT_BOOTPARAM_RDN		"bootparams"
#define NT_BOOTPARAM_TYP		"bootparams_tbl"
#define	NT_BOOTPARAM_NDX_KEY		0
#define	NT_BOOTPARAM_TAG_KEY		"key"
#define	NT_BOOTPARAM_NDX_DATUM		1
#define NT_BOOTPARAM_TAG_DATUM		"datum"
#define NT_BOOTPARAM_COL		2

/* netmasks stuff implemented in /usr/src/cmd/cmd-inet/usr.sbin/ifconfig using */
/* statically linked backends because diskless booting requirements do not */
/* permit using dlopen() stuff */
#define	NT_NETMASK_RDN		"netmasks"
#define NT_NETMASK_TYP		"netmasks_tbl"
#define	NT_NETMASK_NDX_ADDR	0
#define	NT_NETMASK_TAG_ADDR	"addr"
#define	NT_NETMASK_NDX_MASK	1
#define NT_NETMASK_TAG_MASK	"mask"
#define	NT_NETMASK_NDX_COMMENT	2
#define	NT_NETMASK_TAG_COMMENT	"comment"
#define NT_NETMASK_COL		3
