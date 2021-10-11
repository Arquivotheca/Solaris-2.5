/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_INET_ARP_H
#define	_INET_ARP_H

#pragma ident	"@(#)arp.h	1.11	93/11/01 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	ARP_REQUEST	1
#define	ARP_RESPONSE	2
#define	RARP_REQUEST	3
#define	RARP_RESPONSE	4

#define	AR_IOCTL		(((unsigned)'A' & 0xFF)<<8)

#define	AR_ENTRY_ADD		(AR_IOCTL + 1)
#define	AR_ENTRY_DELETE		(AR_IOCTL + 2)
#define	AR_ENTRY_QUERY		(AR_IOCTL + 3)
#define	AR_XMIT_REQUEST		(AR_IOCTL + 4)
#define	AR_XMIT_TEMPLATE	(AR_IOCTL + 5)
#define	AR_ENTRY_SQUERY		(AR_IOCTL + 6)
#define	AR_MAPPING_ADD		(AR_IOCTL + 7)
#define	AR_CLIENT_NOTIFY	(AR_IOCTL + 8)
#define	AR_INTERFACE_UP		(AR_IOCTL + 9)
#define	AR_INTERFACE_DOWN	(AR_IOCTL + 10)
#define	AR_XMIT_RESPONSE	(AR_IOCTL + 11)

#define	ACE_F_PERMANENT		0x1
#define	ACE_F_PUBLISH		0x2
#define	ACE_F_DYING		0x4
#define	ACE_F_RESOLVED		0x8

/* Using bit mask extraction from target address */
#define	ACE_F_MAPPING		0x10

/* ARP Command Structures */

/* arc_t - Common command overlay */
typedef struct ar_cmd_s {
	ulong	arc_cmd;
	ulong	arc_name_offset;
	ulong	arc_name_length;
} arc_t;

typedef	struct ar_entry_add_s {
	u_long	area_cmd;
	u_long	area_name_offset;
	u_long	area_name_length;
	u_long	area_proto;
	u_long	area_proto_addr_offset;
	u_long	area_proto_addr_length;
	u_long	area_proto_mask_offset;
	u_long	area_flags;		/* Same values as ace_flags */
	u_long	area_hw_addr_offset;
	u_long	area_hw_addr_length;
} area_t;

typedef	struct ar_entry_delete_s {
	u_long	ared_cmd;
	u_long	ared_name_offset;
	u_long	ared_name_length;
	u_long	ared_proto;
	u_long	ared_proto_addr_offset;
	u_long	ared_proto_addr_length;
} ared_t;

typedef	struct ar_entry_query_s {
	u_long	areq_cmd;
	u_long	areq_name_offset;
	u_long	areq_name_length;
	u_long	areq_proto;
	u_long	areq_target_addr_offset;
	u_long	areq_target_addr_length;
	u_long	areq_flags;
	u_long	areq_sender_addr_offset;
	u_long	areq_sender_addr_length;
	u_long	areq_xmit_count;	/* 0 ==> cache lookup only */
	u_long	areq_xmit_interval;	/* # of milliseconds; 0: default */
	u_long	areq_max_buffered;	/* # ofquests to buffer; 0: default */
	u_char	areq_sap[8];		/* to insert in returned template */
} areq_t;

typedef	struct ar_mapping_add_s {
	u_long	arma_cmd;
	u_long	arma_name_offset;
	u_long	arma_name_length;
	u_long	arma_proto;
	u_long	arma_proto_addr_offset;
	u_long	arma_proto_addr_length;
	u_long	arma_proto_mask_offset;
	u_long	arma_proto_extract_mask_offset;
	u_long	arma_flags;
	u_long	arma_hw_addr_offset;
	u_long	arma_hw_addr_length;
	u_long	arma_hw_mapping_start;	/* Offset were we start placing */
					/* the mask&proto_addr */
} arma_t;

/* Structure used to notify clients of interesting conditions. */
typedef struct ar_client_notify_s {
	ulong	arcn_cmd;
	ulong	arcn_name_offset;
	ulong	arcn_name_length;
	ulong	arcn_code;			/* Notification code. */
} arcn_t;

/* Client Notification Codes */
#define	AR_CN_BOGON	1
#define	AR_CN_ANNOUNCE	2

/* ARP Header */
typedef struct arh_s {
	u_char	arh_hardware[2];
	u_char	arh_proto[2];
	u_char	arh_hlen;
	u_char	arh_plen;
	u_char	arh_operation[2];
	/* The sender and target hw/proto pairs follow */
} arh_t;

extern int	arpdevflag;

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_ARP_H */
