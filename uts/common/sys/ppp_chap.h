/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef _PPP_CHAP_H
#define	_PPP_CHAP_H

#pragma ident	"@(#)ppp_chap.h	1.7	94/01/19 SMI"



#ifdef	__cplusplus
extern "C" {
#endif

typedef enum { AuthBoth, AuthRem, AuthLoc, Chall, Succ, Fail, GoodResp,
	BadResp, TogtResp, ToeqResp, TogtChall, ToeqChall, Force, ChapClose }
		chapEvent_t;

typedef enum {NullChap, Srcirc, Srrirr, Srsaas, Srs, Srfraf, Aas, Laf, Raf,
	Srr, Src } chapAction_t;

typedef enum {C0, C1, C2, C3, C4, C5, C6, C7, C8, C9, C10, C11 }
	chapState_t;

#define	CHAP_NEVENTS	14
#define	CHAP_STATES	12
#define	MAX_CHALL_SIZE	255
#define	CHAP_MAX_NAME	255

#define	CHAP_DEF_RESTIMER	(3000)	/* restart timer interval (millisecs) */
#define	CHAP_DEF_MAXRESTART	(10)	/* maximum number of restarts	*/

#define	SRCIRC		(Srcirc << 8)
#define	SRRIRR		(Srrirr << 8)
#define	SRS		(Srs << 8)
#define	SRFRAF		(Srfraf << 8)
#define	LAF		(Laf << 8)
#define	AAS		(Aas << 8)
#define	SRSAAS		(Srsaas << 8)
#define	RAF		(Raf << 8)
#define	SRR		(Srr << 8)
#define	SRC		(Src << 8)

#define	FSM_ERR		(-1)

typedef short chapTuple_t;

struct chap_hdr {
	u_char	code;
	u_char	ident;
	u_short length;
};

struct chall_resp {
	u_char	value_size;
	u_char	value[1];
};

struct succ_fail {
	u_char	code;
	u_char	ident;
	u_short length;
	u_char	message[1];
};


/*
 * typedef struct chapMachine chapMachine_t;
 */

struct chapMachine {
	queue_t		*readq;
	pppProtocol_t	protocol;

	chapState_t	state;

	int		chall_restart;
	int		resp_restart;

	int		chall_restart_counter;
	int		resp_restart_counter;
	int		chall_timedoutid;
	int		resp_timedoutid;

	u_char		chall_value[MAX_CHALL_SIZE];
	int		chall_size;

	u_char		local_secret[CHAP_MAX_PASSWD];
	int		local_secret_size;

	u_char		local_name[CHAP_MAX_NAME];
	int		local_name_size;

	u_char		remote_secret[CHAP_MAX_PASSWD];
	int		remote_secret_size;

	u_char		remote_name[CHAP_MAX_NAME];
	int		remote_name_size;

	int		chapMaxRestarts;
	int		chapRestartTimerValue;

	mblk_t		*response, *result, *chall;

	u_short		crid;
	u_short		respid;


	pppLink_t	*linkp;		/* ptr to parent link */


};

typedef enum { Challenge = 1, Response, Success, Failure } chapCode_t;

typedef struct {
	u_int			chap_result;	/* set to PPP_TL_UP */
	pppProtocol_t		protocol;
} chapProtoResult_t;



/*
 * MD5.H - header file for MD5C.C
 */

/*
 * Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
 * rights reserved.

 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.

 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD5 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.

 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 */

/* MDPOINTER defines a generic pointer type */
typedef unsigned char *MDPOINTER;

/* MDUINT2 defines a two byte word */
typedef unsigned short int MDUINT2;

/* MDUINT4 defines a four byte word */
typedef unsigned long int MDUINT4;

/* MD5 context. */
typedef struct {
	MDUINT4 state[4];	/* state (ABCD) */
	MDUINT4 count[2];	/* number of bits, modulo 2^64 (lsb first) */
	unsigned char buffer[64];	/* input buffer */
} MD5_CTX;

#ifdef __STDC__
void MD5Init(MD5_CTX *);
void MD5Update(MD5_CTX *, unsigned char *, unsigned int);
void MD5Final(unsigned char [16], MD5_CTX *);
#else /* __STDC__ */
void MD5Init(/* MD5_CTX * */);
void MD5Update(/* MD5_CTX *, unsigned char *, unsigned int */);
void MD5Final(/* unsigned char [16], MD5_CTX * */);
#endif

#define	DIGEST_SIZE	16

#ifdef __cplusplus
}
#endif

#endif	/* _PPP_CHAP_H */
