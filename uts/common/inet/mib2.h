/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_INET_MIB2_H
#define	_INET_MIB2_H

#pragma ident	"@(#)mib2.h	1.7	93/11/01 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SNMP set/get via M_PROTO T_OPTMGMT_REQ.  Structure is that used
 * for [gs]etsockopt() calls.  get uses T_CURRENT, set uses T_NEOGTIATE
 * MGMT_flags value.  The following definition of opthdr is taken from
 * socket.h:
 *
 * An option specification consists of an opthdr, followed by the value of
 * the option.  An options buffer contains one or more options.  The len
 * field of opthdr specifies the length of the option value in bytes.  This
 * length must be a multiple of sizeof(long) (use OPTLEN macro).
 *
 * struct opthdr {
 *	long	level;	protocol level affected
 *	long	name;	option to modify
 *	long	len;	length of option value
 * };
 *
 * #define OPTLEN(x) ((((x) + sizeof(long) - 1) / sizeof(long)) * sizeof(long))
 * #define OPTVAL(opt) ((char *)(opt + 1))
 *
 * Following def's used to get unique "level" for T_OPTMGMT_REQ.
 * Assumes values > IPPROTO_MAX and < SOL_SOCKET exist and are
 * available for use here.
 *
 * For get requests (T_NEGOTIATE), any MIB2_xxx value can be used (only
 * "get all" is supported, so all modules get a copy of the request to
 * return everything it knows.   Recommend: Use MIB2_IP
 *
 * IMPORTANT:  some fields are grouped in a different structure than
 * suggested by MIB-II, e.g., checksum error counts.  The original MIB-2
 * field name has been retained.  Field names beginning with "mi" are not
 * defined in the MIB but contain important & useful information maintained
 * by the corresponding module.
 */
#ifndef IPPROTO_MAX
#define	IPPROTO_MAX	256
#endif
#define	MIB2_SYSTEM		(IPPROTO_MAX+1)
#define	MIB2_INTERFACES		(IPPROTO_MAX+2)
#define	MIB2_AT			(IPPROTO_MAX+3)
#define	MIB2_IP			(IPPROTO_MAX+4)
#define	MIB2_ICMP		(IPPROTO_MAX+5)
#define	MIB2_TCP		(IPPROTO_MAX+6)
#define	MIB2_UDP		(IPPROTO_MAX+7)
#define	MIB2_EGP		(IPPROTO_MAX+8)
#define	MIB2_CMOT		(IPPROTO_MAX+9)
#define	MIB2_TRANSMISSION	(IPPROTO_MAX+10)
#define	MIB2_SNMP		(IPPROTO_MAX+11)

#define	EXPER			1024	/* experimental - not part of mib */
#define	EXPER_IGMP		(EXPER+1)
#define	EXPER_DVMRP		(EXPER+2)

#define	BUMP_MIB(x)		(x)++
#define	SET_MIB(x, y)		x = y
#define	UPDATE_MIB(x, y)	x += y
#define	BUMP_LOCAL(x)		(x)++

#define	OCTET_LENGTH	32
typedef	struct Octet_s {
	int	o_length;
	char	o_bytes[OCTET_LENGTH];
} Octet_t;

typedef	unsigned long	Counter;
typedef	unsigned long	Gauge;
typedef	unsigned long	IpAddress;
typedef	Octet_t		DeviceName;
typedef	Octet_t		PhysAddress;

/*
 *  IP group
 */
#define	MIB2_IP_20	20
#define	MIB2_IP_21	21
#define	MIB2_IP_22	22
#define	EXPER_IP_GROUP_MEMBERSHIP	100

typedef struct mib2_ip {
		/* forwarder?  1 gateway, 2 NOT gateway	{ip 1} RW */
	int	ipForwarding;
		/* default Time-to-Live for iph		{ip 2} RW */
	int	ipDefaultTTL;
		/* # of input datagrams			{ip 3} */
	Counter	ipInReceives;
		/* # of dg discards for iph error	{ip 4} */
	Counter	ipInHdrErrors;
		/* # of dg discards for bad addr	{ip 5} */
	Counter	ipInAddrErrors;
		/* # of dg being forwarded		{ip 6} */
	Counter	ipForwDatagrams;
		/* # of dg discards for unk protocol	{ip 7} */
	Counter	ipInUnknownProtos;
		/* # of dg discards of good dg's	{ip 8} */
	Counter	ipInDiscards;
		/* # of dg sent upstream		{ip 9} */
	Counter ipInDelivers;
		/* # of outdgs recv'd from upstream	{ip 10} */
	Counter	ipOutRequests;
		/* # of good outdgs discarded		{ip 11} */
	Counter ipOutDiscards;
		/* # of outdg discards: no route found	{ip 12} */
	Counter	ipOutNoRoutes;
		/* sec's recv'd frags held for reass.	{ip 13}	*/
	int	ipReasmTimeout;
		/* # of ip frags needing reassembly	{ip 14} */
	Counter	ipReasmReqds;
		/* # of dg's reassembled		{ip 15} */
	Counter	ipReasmOKs;
		/* # of reassembly failures (not dg cnt){ip 16} */
	Counter	ipReasmFails;
		/* # of dg's fragged			{ip 17} */
	Counter	ipFragOKs;
		/* # of dg discards for no frag set	{ip 18} */
	Counter ipFragFails;
		/* # of dg frags from fragmentation	{ip 19} */
	Counter	ipFragCreates;
		/* {ip 20} */
	int	ipAddrEntrySize;
		/* {ip 21} */
	int	ipRouteEntrySize;
		/* {ip 22} */
	int	ipNetToMediaEntrySize;
		/* # of valid route entries discarded 	{ip 23} */
	Counter	ipRoutingDiscards;
/*
 * following defined in MIB-II as part of TCP & UDP groups:
 */
		/* total # of segments recv'd with error	{ tcp 14 } */
	Counter	tcpInErrs;
		/* # of recv'd dg's not deliverable (no appl.)	{ udp 2 } */
	Counter	udpNoPorts;
/*
 * In addition to MIB-II
 */
		/* # of bad IP header checksums */
	Counter	ipInCksumErrs;
		/* # of complete duplicates in reassembly */
	Counter	ipReasmDuplicates;
		/* # of partial duplicates in reassembly */
	Counter	ipReasmPartDups;
		/* # of packets not forwarded due to adminstrative reasons */
	Counter	ipForwProhibits;
		/* # of UDP packets with bad UDP checksums */
	Counter udpInCksumErrs;
		/* # of UDP packets droped due to queue overflow */
	Counter udpInOverflows;
		/*
		 * # of RAW IP packets (all IP protocols except UDP, TCP
		 * and ICMP) droped due to queue overflow
		 */
	Counter rawipInOverflows;
} mib2_ip_t;

/*
 * The IP address table contains this entity's IP addressing information.
 *
 *	    ipAddrTable OBJECT-TYPE
 *		SYNTAX  SEQUENCE OF IpAddrEntry
 *		ACCESS  not-accessible
 *		STATUS  mandatory
 *		DESCRIPTION
 *			"The table of addressing information relevant to
 *			this entity's IP addresses."
 *		::= { ip 20 }
 */

typedef struct mib2_ipAddrEntry {
			/* IP address of this entry	{ipAddrEntry 1} */
	IpAddress	ipAdEntAddr;
			/* Unique interface index	{ipAddrEntry 2} */
	DeviceName	ipAdEntIfIndex;
			/* Subnet mask for this IP addr	{ipAddrEntry 3} */
	IpAddress	ipAdEntNetMask;
			/* 2^lsb of IP broadcast addr	{ipAddrEntry 4} */
	int		ipAdEntBcastAddr;
			/* max size for dg reassembly	{ipAddrEntry 5} */
	int		ipAdEntReasmMaxSize;
			/* additional ipif_t fields */
	struct ipAdEntInfo_s {
		Gauge		ae_mtu;
				/* BSD if metric */
		int		ae_metric;
				/* ipif broadcast addr.  relation to above?? */
		IpAddress	ae_broadcast_addr;
				/* point-point dest addr */
		IpAddress	ae_pp_dst_addr;
		int		ae_flags;
	}		ipAdEntInfo;
/*
 * ae_flags defined in if.h...
 *	IFF_UP
 *	IFF_RUNNING
 *	IFF_LOOPBACK
 *	IFF_NOARP
 *	IFF_NOTRAILERS
 *	IFF_DEBUG
 *	IFF_BROADCAST
 */
} mib2_ipAddrEntry_t;

/*
 * The IP routing table contains an entry for each route presently known to
 * this entity.
 *
 *	    ipRouteTable OBJECT-TYPE
 *		SYNTAX  SEQUENCE OF IpRouteEntry
 *		ACCESS  not-accessible
 *		STATUS  mandatory
 *		DESCRIPTION
 *			"This entity's IP Routing table."
 *		::= { ip 21 }
 */

typedef struct mib2_ipRouteEntry {
		/* dest ip addr for this route		{ipRouteEntry 1 } RW */
	IpAddress	ipRouteDest;
		/* unique interface index for this hop	{ipRouteEntry 2 } RW */
	DeviceName	ipRouteIfIndex;
		/* primary route metric 		{ipRouteEntry 3 } RW */
	int		ipRouteMetric1;
		/* alternate route metric 		{ipRouteEntry 4 } RW */
	int		ipRouteMetric2;
		/* alternate route metric 		{ipRouteEntry 5 } RW */
	int		ipRouteMetric3;
		/* alternate route metric 		{ipRouteEntry 6 } RW */
	int		ipRouteMetric4;
		/* ip addr of next hop on this route	{ipRouteEntry 7 } RW */
	IpAddress	ipRouteNextHop;
		/* other(1), inval(2), dir(3), indir(4)	{ipRouteEntry 8 } RW */
	int		ipRouteType;
		/* mechanism by which route was learned	{ipRouteEntry 9 } */
	int		ipRouteProto;
		/* sec's since last update of route	{ipRouteEntry 10} RW */
	int		ipRouteAge;
		/* 					{ipRouteEntry 11} RW */
	IpAddress	ipRouteMask;
		/* alternate route metric 		{ipRouteEntry 12} RW */
	int		ipRouteMetric5;
		/* additional info from ire's		{ipRouteEntry 13 } */
	struct ipRouteInfo_s {
		Gauge		re_max_frag;
		Gauge		re_rtt;
		Counter		re_ref;
		int		re_frag_flag;
		IpAddress	re_src_addr;
		int		re_ire_type;
		Counter		re_obpkt;
		Counter		re_ibpkt;
	} 		ipRouteInfo;
} mib2_ipRouteEntry_t;


/*
 * The IP address translation table contain the IpAddress to
 * `physical' address equivalences.  Some interfaces do not
 * use translation tables for determining address
 * equivalences (e.g., DDN-X.25 has an algorithmic method);
 * if all interfaces are of this type, then the Address
 * Translation table is empty, i.e., has zero entries.
 *
 *	    ipNetToMediaTable OBJECT-TYPE
 *		SYNTAX  SEQUENCE OF IpNetToMediaEntry
 *		ACCESS  not-accessible
 *		STATUS  mandatory
 *		DESCRIPTION
 *			"The IP Address Translation table used for mapping
 *			from IP addresses to physical addresses."
 *		::= { ip 22 }
 */

typedef struct mib2_ipNetToMediaEntry {
	/* Unique interface index		{ ipNetToMediaEntry 1 } RW */
	DeviceName	ipNetToMediaIfIndex;
	/* Media dependent physical addr	{ ipNetToMediaEntry 2 } RW */
	PhysAddress	ipNetToMediaPhysAddress;
	/* ip addr for this physical addr	{ ipNetToMediaEntry 3 } RW */
	IpAddress	ipNetToMediaNetAddress;
	/* other(1), inval(2), dyn(3), stat(4)	{ ipNetToMediaEntry 4 } RW */
	int		ipNetToMediaType;
	struct ipNetToMediaInfo_s {
		PhysAddress	ntm_mask;	/* subnet mask for entry */
		int		ntm_flags;
/*
 * arp cache flag values...
 * #define	ACE_F_PERMANENT		0x1
 * #define	ACE_F_PUBLISH		0x2
 * #define	ACE_F_DYING		0x4
 * #define	ACE_F_RESOLVED		0x8
 * #define	ACE_F_MAPPING		0x10
 * #define	ACE_F_PHYS_ONLY		0x20
 */
	}		ipNetToMediaInfo;
} mib2_ipNetToMediaEntry_t;

/*
 * List of group members per interface
 */
typedef struct ip_member {
	/* Interface index */
	DeviceName	ipGroupMemberIfIndex;
	/* IP Multicast address */
	IpAddress	ipGroupMemberAddress;
	/* Number of member processes */
	Counter		ipGroupMemberRefCnt;
} ip_member_t;

/*
 * ICMP Group
 */
typedef struct mib2_icmp {
	/* total # of recv'd ICMP msgs			{ icmp 1 } */
	Counter	icmpInMsgs;
	/* recv'd ICMP msgs with errors			{ icmp 2 } */
	Counter	icmpInErrors;
	/* recv'd "dest unreachable" msg's		{ icmp 3 } */
	Counter	icmpInDestUnreachs;
	/* recv'd "time exceeded" msg's			{ icmp 4 } */
	Counter	icmpInTimeExcds;
	/* recv'd "parameter problem" msg's		{ icmp 5 } */
	Counter	icmpInParmProbs;
	/* recv'd "source quench" msg's			{ icmp 6 } */
	Counter	icmpInSrcQuenchs;
	/* recv'd "ICMP redirect" msg's			{ icmp 7 } */
	Counter	icmpInRedirects;
	/* recv'd "echo request" msg's			{ icmp 8 } */
	Counter	icmpInEchos;
	/* recv'd "echo reply" msg's			{ icmp 9 } */
	Counter	icmpInEchoReps;
	/* recv'd "timestamp" msg's			{ icmp 10 } */
	Counter	icmpInTimestamps;
	/* recv'd "timestamp reply" msg's		{ icmp 11 } */
	Counter	icmpInTimestampReps;
	/* recv'd "address mask request" msg's		{ icmp 12 } */
	Counter	icmpInAddrMasks;
	/* recv'd "address mask reply" msg's		{ icmp 13 } */
	Counter	icmpInAddrMaskReps;
	/* total # of sent ICMP msg's			{ icmp 14 } */
	Counter	icmpOutMsgs;
	/* # of msg's not sent for internal icmp errors	{ icmp 15 } */
	Counter	icmpOutErrors;
	/* # of "dest unreachable" msg's sent		{ icmp 16 } */
	Counter	icmpOutDestUnreachs;
	/* # of "time exceeded" msg's sent		{ icmp 17 } */
	Counter	icmpOutTimeExcds;
	/* # of "parameter problme" msg's sent		{ icmp 18 } */
	Counter	icmpOutParmProbs;
	/* # of "source quench" msg's sent		{ icmp 19 } */
	Counter	icmpOutSrcQuenchs;
	/* # of "ICMP redirect" msg's sent		{ icmp 20 } */
	Counter	icmpOutRedirects;
	/* # of "Echo request" msg's sent		{ icmp 21 } */
	Counter	icmpOutEchos;
	/* # of "Echo reply" msg's sent			{ icmp 22 } */
	Counter	icmpOutEchoReps;
	/* # of "timestamp request" msg's sent		{ icmp 23 } */
	Counter	icmpOutTimestamps;
	/* # of "timestamp reply" msg's sent		{ icmp 24 } */
	Counter	icmpOutTimestampReps;
	/* # of "address mask request" msg's sent	{ icmp 25 } */
	Counter	icmpOutAddrMasks;
	/* # of "address mask reply" msg's sent		{ icmp 26 } */
	Counter	icmpOutAddrMaskReps;
/*
 * In addition to MIB-II
 */
	/* # of received packets with checksum errors */
	Counter	icmpInCksumErrs;
	/* # of received packets with unknow codes */
	Counter	icmpInUnknowns;
	/* # of received unreachables with "fragmentation needed" */
	Counter	icmpInFragNeeded;
	/* # of sent unreachables with "fragmentation needed" */
	Counter	icmpOutFragNeeded;
	/*
	 * # of msg's not sent since original packet was broadcast/multicast
	 * or an ICMP error packet
	 */
	Counter	icmpOutDrops;
	/* # of ICMP packets droped due to queue overflow */
	Counter icmpInOverflows;
	/* recv'd "ICMP redirect" msg's	that are bad thus ignored */
	Counter	icmpInBadRedirects;
} mib2_icmp_t;

/*
 * the TCP group
 *
 * Note that instances of object types that represent
 * information about a particular TCP connection are
 * transient; they persist only as long as the connection
 * in question.
 */
#define	MIB2_TCP_13	13

typedef	struct mib2_tcp {
		/* algorithm used for transmit timeout value	{ tcp 1 } */
	int	tcpRtoAlgorithm;
		/* minimum retransmit timeout (ms)		{ tcp 2 } */
	int	tcpRtoMin;
		/* maximum retransmit timeout (ms)		{ tcp 3 } */
	int	tcpRtoMax;
		/* maximum # of connections supported		{ tcp 4 } */
	int	tcpMaxConn;
		/* # of direct transitions CLOSED -> SYN-SENT	{ tcp 5 } */
	Counter	tcpActiveOpens;
		/* # of direct transitions LISTEN -> SYN-RCVD	{ tcp 6 } */
	Counter	tcpPassiveOpens;
		/* # of direct SIN-SENT/RCVD -> CLOSED/LISTEN	{ tcp 7 } */
	Counter	tcpAttemptFails;
		/* # of direct ESTABLISHED/CLOSE-WAIT -> CLOSED	{ tcp 8 } */
	Counter	tcpEstabResets;
		/* # of connections ESTABLISHED or CLOSE-WAIT	{ tcp 9 } */
	Gauge	tcpCurrEstab;
		/* total # of segments recv'd			{ tcp 10 } */
	Counter	tcpInSegs;
		/* total # of segments sent			{ tcp 11 } */
	Counter	tcpOutSegs;
		/* total # of segments retransmitted		{ tcp 12 } */
	Counter	tcpRetransSegs;
		/* {tcp 13} */
	int	tcpConnTableSize;
	/* in ip			   {tcp 14} */
		/* # of segments sent with RST flag		{ tcp 15 } */
	Counter	tcpOutRsts;
/* In addition to MIB-II */
/* Sender */
	/* total # of data segments sent */
	Counter tcpOutDataSegs;
	/* total # of bytes in data segments sent */
	Counter tcpOutDataBytes;
	/* total # of bytes in segments retransmitted */
	Counter tcpRetransBytes;
	/* total # of acks sent */
	Counter tcpOutAck;
	/* total # of delayed acks sent */
	Counter tcpOutAckDelayed;
	/* total # of segments sent with the urg flag on */
	Counter tcpOutUrg;
	/* total # of window updates sent */
	Counter tcpOutWinUpdate;
	/* total # of zero window probes sent */
	Counter tcpOutWinProbe;
	/* total # of control segments sent (syn, fin, rst) */
	Counter tcpOutControl;
	/* total # of segments sent due to "fast retransmit" */
	Counter tcpOutFastRetrans;
/* Receiver */
	/* total # of ack segments received */
	Counter tcpInAckSegs;
	/* total # of bytes acked */
	Counter tcpInAckBytes;
	/* total # of duplicate acks */
	Counter tcpInDupAck;
	/* total # of acks acking unsent data */
	Counter tcpInAckUnsent;
	/* total # of data segments received in order */
	Counter tcpInDataInorderSegs;
	/* total # of data bytes received in order */
	Counter tcpInDataInorderBytes;
	/* total # of data segments received out of order */
	Counter tcpInDataUnorderSegs;
	/* total # of data bytes received out of order */
	Counter tcpInDataUnorderBytes;
	/* total # of complete duplicate data segments received */
	Counter tcpInDataDupSegs;
	/* total # of bytes in the complete duplicate data segments received */
	Counter tcpInDataDupBytes;
	/* total # of partial duplicate data segments received */
	Counter tcpInDataPartDupSegs;
	/* total # of bytes in the partial duplicate data segments received */
	Counter tcpInDataPartDupBytes;
	/* total # of data segments received past the window */
	Counter tcpInDataPastWinSegs;
	/* total # of data bytes received part the window */
	Counter tcpInDataPastWinBytes;
	/* total # of zero window probes received */
	Counter tcpInWinProbe;
	/* total # of window updates received */
	Counter tcpInWinUpdate;
	/* total # of data segments received after the connection has closed */
	Counter tcpInClosed;
/* Others */
	/* total # of failed attempts to update the rtt estimate */
	Counter tcpRttNoUpdate;
	/* total # of successful attempts to update the rtt estimate */
	Counter tcpRttUpdate;
	/* total # of retransmit timeouts */
	Counter tcpTimRetrans;
	/* total # of retransmit timeouts dropping the connection */
	Counter tcpTimRetransDrop;
	/* total # of keepalive timeouts */
	Counter tcpTimKeepalive;
	/* total # of keepalive timeouts sending a probe */
	Counter tcpTimKeepaliveProbe;
	/* total # of keepalive timeouts dropping the connection */
	Counter tcpTimKeepaliveDrop;
} mib2_tcp_t;

/*
 * The TCP connection table {tcp 13} contains information about this entity's
 * existing TCP connections.
 */

typedef struct mib2_tcpConnEntry {
		/* state of tcp connection		{ tcpConnEntry 1} RW */
	int		tcpConnState;
#define	MIB2_TCP_closed		1
#define	MIB2_TCP_listen		2
#define	MIB2_TCP_synSent	3
#define	MIB2_TCP_synReceived	4
#define	MIB2_TCP_established	5
#define	MIB2_TCP_finWait1	6
#define	MIB2_TCP_finWait2	7
#define	MIB2_TCP_closeWait	8
#define	MIB2_TCP_lastAck	9
#define	MIB2_TCP_closing	10
#define	MIB2_TCP_timeWait	11
#define	MIB2_TCP_deleteTCB	12		/* only writeable value */
		/* local ip addr for this connection	{ tcpConnEntry 2 } */
	IpAddress	tcpConnLocalAddress;
		/* local port for this connection	{ tcpConnEntry 3 } */
	int		tcpConnLocalPort;
		/* remote ip addr for this connection	{ tcpConnEntry 4 } */
	IpAddress	tcpConnRemAddress;
		/* remote port for this connection	{ tcpConnEntry 5 } */
	int		tcpConnRemPort;
	struct tcpConnEntryInfo_s {
			/* seq # of next segment to send */
		Gauge		ce_snxt;
				/* seq # of of last segment unacknowledged */
		Gauge		ce_suna;
				/* currect send window size */
		Gauge		ce_swnd;
				/* seq # of next expected segment */
		Gauge		ce_rnxt;
				/* seq # of last ack'd segment */
		Gauge		ce_rack;
				/* currenct receive window size */
		Gauge		ce_rwnd;
					/* current rto (retransmit timeout) */
		Gauge		ce_rto;
					/* current max segment size */
		Gauge		ce_mss;
				/* actual internal state */
		int		ce_state;
	} 		tcpConnEntryInfo;
} mib2_tcpConnEntry_t;


/*
 * the UDP group
 */
#define	MIB2_UDP_5	5

typedef	struct mib2_udp {
		/* total # of UDP datagrams sent upstream	{ udp 1 } */
	Counter	udpInDatagrams;
	/* in ip			   { udp 2 } */
		/* # of recv'd dg's not deliverable (other)	{ udp 3 }  */
	Counter	udpInErrors;
		/* total # of dg's sent				{ udp 4 } */
	Counter	udpOutDatagrams;
		/* { udp 5 } */
	int	udpEntrySize;
} mib2_udp_t;

/*
 * The UDP listener table contains information about this entity's UDP
 * end-points on which a local application is currently accepting datagrams.
 */

typedef	struct mib2_udpEntry {
		/* local ip addr of listener		{ udpEntry 1 } */
	IpAddress	udpLocalAddress;
		/* local port of listener		{ udpEntry 2 } */
	int		udpLocalPort;
	struct udpEntryInfo_s {
		int	ue_state;
	}		udpEntryInfo;
} mib2_udpEntry_t;
#define	MIB2_UDP_unbound	1
#define	MIB2_UDP_idle		2
#define	MIB2_UDP_unknown	3

/* DVMRP group */
#define	EXPER_DVMRP_VIF		1
#define	EXPER_DVMRP_MRT		2


#ifdef	__cplusplus
}
#endif

#endif	/* _INET_MIB2_H */
