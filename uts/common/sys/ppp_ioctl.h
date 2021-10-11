/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef _SYS_PPP_IOCTL_H
#define	_SYS_PPP_IOCTL_H

#pragma ident	"@(#)ppp_ioctl.h	1.13	94/01/24 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Declare different ioctls for communication between the user and
 * the PPP implementation
 *
 *
 *		Message direction	User | Kernel | msgtype */

#define	NO_AUTH		0
#define	DO_CHAP		(1 << 0)
#define	DO_PAP		(1 << 1)

enum ppp_ioctls	 {
	PPP_SET_CONF = 0x100,	/*    --->   pppLinkControlEntry_t	*/
	PPP_GET_CONF,		/*    <---   pppLinkControlEntry_t	*/
	PPP_SET_AUTH,		/*    --->   pppAuthControlEntry_t	*/
	PPP_GET_AUTH,		/*    <---   pppAuthControlEntry_t	*/
	PPP_ACTIVE_OPEN,	/*    --->   void  (Old protocol)	*/
	PPP_PASSIVE_OPEN,	/*    --->   void  (Old protocol)	*/
	PPP_CLOSE,		/*    --->   pppExEvent_t		*/
	PPP_UP,			/*    --->   pppExEvent_t		*/
	PPP_DOWN,		/*    --->   pppExEvent_t		*/
	PPP_SET_LOCAL_PASSWD,	/*    --->   pppPAP_t			*/
	PPP_GET_REMOTE_PASSWD,	/*    <---   pppPAP_t			*/
	PPP_REMOTE_OK,		/*    --->   pppPAPMessage_t		*/
	PPP_REMOTE_NOK,		/*    --->   pppPAPMessage_t		*/
	PPP_GET_STATE,		/*    <---   pppLinkStatusEntry_t	*/
	PPP_GET_LCP_STATS,	/*    <---   pppCPEntry_t		*/
	PPP_GET_IPNCP_STATS,	/*    <---   pppCPEntry_t		*/
	PPP_GET_IP_STATS,	/*    <---   pppIPEntry_t		*/
	PPP_GET_ERRS,		/*    <---   pppErrorsEntry_t		*/
	PPP_SET_DEBUG,		/*    --->   unsigned int		*/
	PPP_DELETE_MIB_ENTRY,	/*    --->   unsigned int		*/
	PPP_GET_VERSION,	/*    <---   unsigned int		*/
	PPP_OPEN,		/*    --->   pppExEvent_t		*/
	PPP_AUTH_LOC,
	PPP_AUTH_REM,
	PPP_AUTH_BOTH,
	PPP_FORCE_REM,
	PPP_SET_REMOTE_PASSWD	/*    --->   pppPAP_t			*/
};

typedef enum ppp_ioctls ppp_ioctl_t;

/*
 * Note Get Version returns the version of the *Module* *Not* of the
 * the PPP protocol.
 */

/*
 * Declare different asynchronous indications which PPP can send to the
 * user at any time
 */
enum ppp_messages {
	PPP_TL_UP,		/* pppProtoUp_t		*/
	PPP_TL_DOWN,		/* pppProtoDown_t	*/
	PPP_TL_START,		/* pppProtoStart_t	*/
	PPP_TL_FINISH,		/* pppProtoFinish_t	*/
	PPP_NEED_VALIDATION,	/* pppReqValidation_t	*/
	PPP_CONFIG_CHANGED,	/* pppConfigChange_t	*/
	PPP_ERROR_IND,		/* pppError_t		*/
	PPP_AUTH_SUCCESS,
	PPP_REMOTE_FAILURE,
	PPP_LOCAL_FAILURE
};


/*
 * PPP protocol fields currently defined
 */
typedef enum {
	pppDEVICE	   = 0x0000,	/* Device layer, not for transport */
	pppIP_PROTO	   = 0x0021,	/* Internet Protocol		*/
	pppOSI_PROTO	   = 0x0023,	/* OSI Network Layer		*/
	pppXNS_PROTO	   = 0x0025,	/* Xerox NS IDP			*/
	pppDECNET_PROTO	   = 0x0027,	/* DECnet phase IV		*/
	pppAPPLETALK_PROTO = 0x0029,	/* Appletalk			*/
	pppIPX_PROTO	   = 0x002b,	/* Novell IPX			*/
	pppVJ_COMP_TCP	   = 0x002d,	/* Van J Compressed TCP/IP	*/
	pppVJ_UNCOMP_TCP   = 0x002f,	/* Van J Uncompressed TCP/IP	*/
	pppBRIDGING_PDU	   = 0x0031,	/* Bridging PDU			*/
	pppSTREAM_PROTO	   = 0x0033,	/* Stream Protocol (ST-II)	*/
	pppBANYAN_VINES	   = 0x0035,	/* Banyan Vines			*/
	ppp802_1D	   = 0x0201,	/* 802.1d Hello Packets		*/
	pppLUXCOM	   = 0x0231,	/* Luxcom			*/
	pppSIGMA	   = 0x0232,	/* Sigma Network Systems	*/
	pppIP_NCP	   = 0x8021,	/* Internet Protocol NCP	*/
	pppOSI_NCP	   = 0x8023,	/* OSI Network Layer NCP	*/
	pppXNS_NCP	   = 0x8025,	/* Xerox NS IDP NCP		*/
	pppDECNET_NCP	   = 0x8027,	/* DECnet phase IV NCP		*/
	pppAPPLETALK_NCP   = 0x8029,	/* Appletalk NCP		*/
	pppIPX_NCP	   = 0x802b,	/* Novell IPX NCP		*/
	pppBRIDGING_NCP	   = 0x8031,	/* Bridging NCP			*/
	pppSTREAM_NCP	   = 0x8033,	/* Stream Protocol NCP		*/
	pppBANYAN_NCP	   = 0x8035,	/* Banyan Vines NCP		*/
	pppLCP		   = 0xc021,	/* Link Control Protocol	*/
	pppAuthPAP	   = 0xc023,	/* Password Authentication	*/
	pppLQM_REPORT	   = 0xc025,	/* Link Quality Report		*/
	pppCHAP		   = 0xc223	/* Challenge Handshake		*/

} pppProtocol_t;

/*
 * PPP link configuration structure; used by PPP_SET_CONF/PPP_GET_CONF
 */
typedef struct {
	u_short		pppLinkControlIndex;		/* 0 => this link */
	u_char		pppLinkMaxRestarts;
	u_int		pppLinkRestartTimerValue;	/* millisecs */
	u_int		pppLinkMediaType;		/* Async/Sync */

	u_int		pppLinkAllowMRU;		/* MRU negotiation */
	u_int		pppLinkAllowHdrComp;		/* IP Header comp */
	u_int		pppLinkAllowPAComp;		/* Proto address comp */
	u_int		pppLinkAllowACC;		/* Char mapping */
	u_int		pppLinkAllowAddr;		/* IP address neg. */
	u_int		pppLinkAllowAuth;		/* Authentication */
	u_int		pppLinkAllowQual;		/* Link quality */
	u_int		pppLinkAllowMagic;		/* Magic number */

	u_long		pppLinkLocalMRU;		/* bytes */
	u_long		pppLinkRemoteMRU;		/* bytes */
	u_int		pppLinkLocalACCMap;		/* not used */
	u_long		pppIPLocalAddr;			/* for IP addr neg */
	u_long		pppIPRemoteAddr;
	u_int		pppLinkMaxLoopCount;
	clock_t		pppLinkMaxNoFlagTime;
} pppLinkControlEntry_t;

typedef enum {
	pppVer1,
	pppVer2
} pppVer_t;

#define	OLD_ALLOW	1
#define	OLD_DISALLOW	0

#define	REM_OPTS	(0x0000ffff)
#define	REM_DISALLOW	(1 << 1)
#define	REM_OPTIONAL	(1 << 2)
#define	REM_MAND	(1 << 3)

#define	LOC_OPTS	(0xffff0000)
#define	LOC_DISALLOW	(1 << 16)
#define	LOC_OPTIONAL	(1 << 17)
#define	LOC_MAND	(1 << 18)

#define	DISALLOW_BOTH	(REM_DISALLOW | LOC_DISALLOW)

/*
 * Maximum PAP Peer ID/Password Length supported [RFC1172 Page 29]
 */
#define	PPP_MAX_PASSWD	(255)
#define	PPP_MAX_ERROR 	(255)

/*
 * Message which indicates a "this-layer-up" action (protocol is up)
 */
typedef struct {
	u_int			ppp_message;
	pppProtocol_t		protocol;
	u_char			data[1];
} pppProtoCh_t;

typedef struct {
	u_int			ppp_message;	/* set to PPP_TL_UP */
	pppProtocol_t		protocol;
} pppProtoUp_t;


/*
 * Message which indicates a "this-layer-down" action (protocol is down)
 */
typedef struct {
	u_int			ppp_message;	/* set to PPP_TL_DOWN */
	pppProtocol_t		protocol;
} pppProtoDown_t;

/*
 * Message which indicates a "this-layer-finish" action (protocol is finish)
 */
typedef struct {
	u_int			ppp_message;	/* set to PPP_TL_FINISH */
	pppProtocol_t		protocol;
} pppProtoFinish_t;

/*
 * Message which indicates a "this-layer-start" action (starting protocol)
 */
typedef struct {
	u_int			ppp_message;	/* set to PPP_TL_START */
	pppProtocol_t		protocol;
} pppProtoStart_t;

/*
 * Message which indicates a "this-layer-start" action (starting protocol)
 */
typedef struct {
	u_int			message;	/* set to PPP_TL_START */
	pppProtocol_t		protocol;
} pppAuthMsg_t;

/*
 *  Message which indicates that the user is required to validate a PPP peer
 * using PPP_GET_REMOTE_PASSWD/PPP_REMOTE_OK/NOK
 */

typedef struct {
	u_int			ppp_message;    /* set to PPP_NEED_VALIDATION */
} pppReqValidation_t;

/*
 * Message which indicates an error has occurred
 */
typedef struct {
	u_int			ppp_message;	/* set to PPP_ERROR_IND */
	u_int			code;
	u_int			errlen;		/* optional error data */
	u_char			errdata[PPP_MAX_ERROR];
} pppError_t;

typedef struct {
	u_int			ppp_message;
	pppLinkControlEntry_t	config;
} pppConfig_t;


/*
 * and a union of the PPP asynchronous indications
 */
union PPPmessages {
	u_int			ppp_message;
	pppProtoUp_t		proto_up;
	pppProtoDown_t		proto_down;
	pppProtoFinish_t	proto_finish;
	pppProtoStart_t		proto_start;
	pppError_t		error_ind;
	pppAuthMsg_t		auth_msg;
	pppConfig_t		config;
};

/*
 * Error codes from PPP
 */
enum ppp_errors {
	pppConfigFailed,	/* Maximum number of configure reqs exceeded */
	pppNegotiateFailed,	/* Negotiation of mandatory options failed */
	pppAuthFailed,		/* Authentication Failed */
	pppProtoClosed,		/* Protocol closed */
	pppLocalAuthFailed,
	pppRemoteAuthFailed,
	pppLoopedBack
};

/*
 * PPP status and control information, derived from the
 * draft PPP MIB
 */

/*
 * enumeration to indicate mode in which options are used
 */
enum pppSense {
	pppReceiveOnly = 1,
	pppSendOnly,
	pppReceiveAndSend,
	pppNone
};

/*
 * enumeration to indicate PPP state
 */
enum pppState {
	pppInitial = 0,
	pppStarting,
	pppClosed,
	pppStopped,
	pppClosing,
	pppStopping,
	pppReqSent,
	pppAckRcvd,
	pppAckSent,
	pppOpened
};

/*
 * enumeration to indicate link quality estimate
 */
enum pppLinkQuality {
	pppGood = 1,
	pppBad
};

/*
 * enumeration to indicate size of CRC in use
 */
enum pppLinkCRCSize {
	pppCRC16 = 16,
	pppCRC32 = 32
};

/*
 * enumeration to indicate link media type
 */
enum pppLinkMediaType {
	pppSync,
	pppAsync
};

/*
 * enumeration to indicate PPP protocol version
 */
enum pppLinkVersions {
	pppRFC1171 = 1,
	pppRFC1331
};


/*
 * PPP Authentication Control; used by PPP_SET_AUTH/PPP_GET_AUTH
 */
typedef struct {
	u_short		pppAuthControlIndex;		/* 0 => this link */
	u_short		pppAuthTypeLocal;		/* 0 => none */
	u_short		pppAuthTypeRemote;		/* 0 => none */
} pppAuthControlEntry_t;

/*
 * Structure used to indicate protocol level for PPP_OPEN, PPP_CLOSE, PPP_UP,
 * PPP_DOWN ioclts
 */
typedef struct {
		pppProtocol_t	protocol;	/* Protocol to receive event */
} pppExEvent_t;

/*
 * Optional PAP message carried on Authenticate Ack/Nak
 */
typedef struct {
	u_char		pppPAPMessageLen;
	u_short		pppPAPMessage[1];
} pppPAPMessage_t;

/*
 * PPP status structure; read-only values
 */
typedef struct {
	u_short		pppLinkStatusIndex;
	u_char		pppLinkVersion;
	u_char		pppLinkCurrentState;
	u_char		pppLinkPreviousState;
	u_char		pppLinkQuality;
#ifndef _SunOS4
	timestruc_t	pppLinkChangeTime;
#endif
	u_int		pppLinkMagicNumber;
	u_int		pppLinkLocalQualityPeriod;	/* microseconds */
	u_int		pppLinkRemoteQualityPeriod;	/* microseconds */
	u_char		pppLinkProtocolCompression;	/* pppSense */
	u_char		pppLinkACCompression;		/* pppSense */
	u_char		pppLinkMeasurementsValid;
	u_char		pppLinkPhysical;
} pppLinkStatusEntry_t;

/*
 * PPP error report structure; read-only values
 */
typedef struct {
	u_short		pppLinkErrorsIndex;
	u_short		pppLinkLastUnknownProtocol;
	u_int		pppLinkBadAddresses;
	u_int		pppLinkBadControls;
	u_short		pppLinkLastInvalidProtocol;
	u_char		pppLinkLastBadControl;
	u_char		pppLinkLastBadAddress;
	u_int		pppLinkInvalidProtocols;
	u_int		pppLinkUnknownProtocols;
	u_int		pppLinkPacketTooLongs;
	u_int		pppLinkPacketTooShorts;
	u_int		pppLinkHeaderTooShorts;
	u_int		pppLinkBadCRCs;
	u_int		pppLinkConfigTimeouts;
	u_int		pppLinkTerminateTimeouts;
} pppLinkErrorsEntry_t;

/*
 * PPP IP status; read-only values
 */
typedef struct {
	u_short		pppIPLinkNumber;
	u_int		pppIPRejects;
	u_int		pppIPInPackets;
	u_int		pppIPInOctets;
	u_int		pppIPOutPackets;
	u_int		pppIPOutOctets;
	u_int		pppIPInVJcomp;
	u_int		pppIPInVJuncomp;
	u_int		pppIPInIP;
	u_int		pppIPOutVJcomp;
	u_int		pppIPOutVJuncomp;
	u_int		pppIPOutIP;
} pppIPEntry_t;

/*
 * PPP IP NCP/LCP status; read-only values
 */
typedef struct {
	u_short		pppCPLinkNumber;
	u_int		pppCPRejects;
	u_int		pppCPInPackets;
	u_int		pppCPInOctets;
	u_int		pppCPOutPackets;
	u_int		pppCPOutOctets;
	u_int		pppCPOutCRs;
	u_int		pppCPInCRs;
	u_int		pppCPOutCAs;
	u_int		pppCPInCAs;
	u_int		pppCPOutCNs;
	u_int		pppCPInCNs;
	u_int		pppCPOutCRejs;
	u_int		pppCPInCRejs;
	u_int		pppCPOutTRs;
	u_int		pppCPInTRs;
	u_int		pppCPOutTAs;
	u_int		pppCPInTAs;
	u_int		pppCPOutCodeRejs;
	u_int		pppCPInCodeRejs;
	u_int		pppCPOutEchoReqs;
	u_int		pppCPInEchoReqs;
	u_int		pppCPOutEchoReps;
	u_int		pppCPInEchoReps;
	u_int		pppCPOutDiscReqs;
	u_int		pppCPInDiscReqs;
} pppCPEntry_t;

#define	CHAP_MAX_PASSWD 255
#define	CHAP_MAX_NAME	255

/*
 * CHAP password struct
 */
typedef struct {
	u_int		protocol;	/* pppCHAP */
	u_char		chapPasswdLen;
	u_char		chapPasswd[CHAP_MAX_PASSWD];
	u_char		chapNameLen;
	u_char		chapName[CHAP_MAX_NAME];
}chapPasswdEntry_t;

#define	PAP_MAX_PASSWD	255

/*
 * PAP password struct
 */
typedef struct {
	u_int		protocol;	/* pppPAPAuth */
	u_char		papPeerIdLen;
	u_char		papPeerId[PAP_MAX_PASSWD];
	u_char		papPasswdLen;
	u_char		papPasswd[PAP_MAX_PASSWD];
} papPasswdEntry_t;

typedef struct {
	papPasswdEntry_t passwd;
} papValidation_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PPP_IOCTL_H */
