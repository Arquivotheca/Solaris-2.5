/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef	_NETINET_IGMP_H
#define	_NETINET_IGMP_H

#pragma ident	"@(#)igmp.h	1.7	93/04/22 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Internet Group Management Protocol (IGMP) definitions.
 *
 * Written by Steve Deering, Stanford, May 1988.
 *
 * MULTICAST 1.1
 */

/*
 * IGMP packet format.
 */
struct igmp {
	u_char		igmp_type;	/* version & type of IGMP message  */
	u_char		igmp_code;	/* unused, should be zero	   */
	u_short		igmp_cksum;	/* IP-style checksum		   */
	struct in_addr	igmp_group;	/* group address being reported	   */
};					/*  (zero for queries)		   */

#ifdef _KERNEL
typedef struct igmp_s {
	u_char		igmp_type;	/* version & type of IGMP message  */
	u_char		igmp_code;	/* unused, should be zero	   */
	u_char		igmp_cksum[2];	/* IP-style checksum		   */
	u_char		igmp_group[4];	/* group address being reported	   */
} igmp_t;				/*  (zero for queries)		   */

/* Alligned igmp header */
typedef struct igmpa_s {
	u_char		igmpa_type;	/* version & type of IGMP message  */
	u_char		igmpa_code;	/* unused, should be zero	   */
	u_short		igmpa_cksum;	/* IP-style checksum		   */
	u_long		igmpa_group;	/* group address being reported	   */
} igmpa_t;				/*  (zero for queries)		   */
#endif	/* _KERNEL */

#define	IGMP_MINLEN			8

#define	IGMP_HOST_MEMBERSHIP_QUERY	0x11	/* message types, incl.	*/
						/* version		*/
#define	IGMP_HOST_MEMBERSHIP_REPORT	0x12
#define	IGMP_DVMRP			0x13	/* for experimental	*/
						/* multicast routing	*/
						/* protocol		*/

#define	IGMP_MAX_HOST_REPORT_DELAY	10	/* maximum delay for	*/
						/* response to query	*/
						/* (in seconds)		*/

#if defined(_KERNEL) && defined(__STDC__)

extern	int	igmp_timeout_handler(void);
extern	void	igmp_timeout_start(int);
extern	int	igmp_input(queue_t *q, mblk_t *mp, ipif_t *ipif);
extern	void	igmp_joingroup(ilm_t *ilm);
extern	void	igmp_leavegroup(ilm_t *ilm);

#endif	/* defined(_KERNEL) && defined(__STDC__) */

#ifdef	__cplusplus
}
#endif

#endif	/* _NETINET_IGMP_H */
