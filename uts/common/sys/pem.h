/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * PCMCIA Event Manager Driver
 */

#ifndef _PEM_H
#define	_PEM_H

#pragma ident	"@(#)pem.h	1.9	95/01/31 SMI"

#ifdef __cplusplus
extern "C" {
#endif

/* #define	PEM_DEBUG */

#define	PEM_MAX_EVENTS	64
#define	PEM_IDNUM	0x454D
#define	PEM_NAME	"pem"
#define	PEM_MIN		0
#define	PEM_MAX		1024
#define	PEM_HIWATER	4096
#define	PEM_LOWATER	1024

#define	EM_INIT_REQ	0
#define	EM_INFO_REQ	1
#define	EM_MODIFY_EVENT_MASK_REQ	2
#define	EM_GET_FIRST_TUPLE_REQ	3
#define	EM_GET_NEXT_TUPLE_REQ	4

#define	EM_INIT_ACK	5
#define	EM_INFO_ACK	6
#define	EM_EVENT_IND	7

#define	EM_ADAPTER_INFO_REQ	8
#define	EM_ADAPTER_INFO_ACK	9
#define	EM_SOCKET_INFO_REQ	10
#define	EM_SOCKET_INFO_ACK	11
#define	EM_GET_SOCKET_REQ	12
#define	EM_GET_SOCKET_ACK	13
#define	EM_IDENT_SOCKET_REQ	14
#define	EM_GET_FIRST_TUPLE_ACK	15

#define	EM_BADPRIM	1


#define	___VERSION(a, b)	(((a)<<8)|(b))
#define	EM_VERSION	___VERSION(0, 1)
#define	EM_CURRENT_VERSION	EM_VERSION

typedef
struct em_init_req {
	long	em_primitive;
	long	em_logical_socket;
	long	em_event_mask_offset;
	long	em_event_mask_length;
	long	em_event_class_offset;
	long	em_event_class_length;
} em_init_req_t;

typedef
struct em_info_req {
	long	em_primitive;
} em_info_req_t;

typedef
struct em_modify_event_mask_req {
	long	em_primitive;
} em_modify_event_mask_req_t;

typedef
struct em_get_first_tuple_req {
	long	em_primitive;
	long	em_socket;
	long	em_desired_tuple;
} em_get_first_tuple_req_t;

typedef
struct em_get_first_tuple_ack {
	long	em_primitive;
} em_get_first_tuple_ack_t;

typedef
struct em_get_next_tuple_req {
	long	em_primitive;
} em_get_next_tuple_req_t;

typedef
struct em_get_next_tuple_ack {
	long	em_primitive;
} em_get_next_tuple_ack_t;

typedef
struct em_info_ack {
	long	em_primitive;
	long	em_version;
	long	em_state;
	long	em_event_mask_offset;
	long	em_event_mask_length;
	long	em_event_class_offset;
	long	em_event_class_length;
} em_info_ack_t;

typedef
struct em_event_ind {
	long	em_primitive;
	long	em_logical_socket;
	long	em_event;
	long	em_event_info_offset;
	long	em_event_info_length;
} em_event_ind_t;

typedef
struct em_init_ack {
	long	em_primitive;
} em_init_ack_t;

/* adapter info is essentially InquireAdapter */
typedef
struct em_adapter_info_req {
	long	em_primitive;
} em_adapter_info_req_t;

typedef
struct em_adapter_info_ack {
	long	em_primitive;
	long	em_num_sockets;
	long	em_num_windows;
	long	em_num_power;
	long	em_power_offset;
	long	em_power_length;
} em_adapter_info_ack_t;

/* socket_info is essentially InquireSocket */
typedef
struct em_socket_info_req {
	long	em_primitive;
	long	em_socket;
} em_socket_info_req_t;

typedef
struct em_socket_info_ack {
	long	em_primitive;
	long	em_status_int_caps;
	long	em_status_report_caps;
	long	em_control_indicator_caps;
	long	em_socket_caps;
} em_socket_info_ack_t;

/* get_socket */
typedef
struct em_get_socket_req {
	long	em_primitive;
	long	em_socket;
} em_get_socket_req_t;

typedef
struct em_get_socket_ack {
	long	em_primitive;
	long	em_socket;
	long	em_vcc_level;
	long	em_vpp1_level;
	long	em_vpp2_level;
	long	em_state;
	long	em_control_ind;
	long	em_ireq_routing;
	long	em_iftype;
} em_get_socket_ack_t;

typedef
struct em_ident_socket_req {
	long	em_primitive;
	long	em_socket;
} em_ident_socket_req_t;

union em_primitives {
	long		em_primitive;
	em_init_req_t	init_req;
	em_info_req_t	info_req;
	em_get_first_tuple_req_t	get_first_tuple_req;
	em_get_next_tuple_req_t	get_next_tuple_req;
	em_info_ack_t	info_ack;
	em_event_ind_t	event_ind;
	em_modify_event_mask_req_t	modify_event_mask_req;
	em_get_socket_req_t	get_socket_req;
	em_ident_socket_req_t	ident_socket_req;
};

#define	EM_CLASS_SIZE	32
#define	EM_EVENT_MASK_SIZE	32

#if defined(_KERNEL)
#define	EM_EVENT_SIZE	4
typedef struct pem {
	u_long	 pem_flags;
	u_long	 pem_state;
	u_long	 pem_id;
	minor_t  pem_minor;
	queue_t *pem_qptr;
	mblk_t	*pem_mb;
	u_char   pem_event_class[EM_CLASS_SIZE/sizeof (u_char)];
	u_char	 pem_event_mask[EM_EVENT_MASK_SIZE/sizeof (u_char)];
} pem_t;

#define	EM_INIT	1
#define	PEMF_EVENTS	0x0001
#define	PEMF_CLASSES	0x0002
#define	PEMF_INIT	0x0004

#define	PEMMAXINFO	64
struct pem_event {
	int	pe_owner;
	int	pe_id;
	int	pe_event;
	int	pe_socket;
	u_char	pe_info[PEMMAXINFO];
};
#define	PE_OWN_FREE	0
#define	PE_OWN_CLAIMED	1
#define	PE_OWN_HANDLER	2

#define	PEME_OK			0
#define	PEME_NO_INFO		1
#define	PEME_UNAVAILABLE	2
#define	PEME_NO_CIS		3
#define	PEME_NO_TUPLE		4

#define	PEMTRACE	0x0001
#define	PEMERRS		0x0002

#endif

#ifdef __cplusplus
}
#endif

#endif /* _PEM_H */
