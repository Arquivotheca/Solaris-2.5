/*
 * Copyright (c) 1989-1992 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)ipi.c	1.10 94/03/15 SMI"

/*
 * Host Adapter Layer for IPI Channels.
 *
 * This module is an extremely stripped down version of previous
 * Sun IPI subsystems. It exists at this point only to provide
 * a very few IPI3 subroutines common to everyone.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>

#include <sys/ipi3.h>
#include <sys/ipi_driver.h>
#include <sys/ipi_chan.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#define	RESP_BUF_LEN	512	/* size of response unpacking buf (on stack) */
/*
 * Parse response.
 *
 * Call functions in table for each parameter matched.
 * Align parameters apropriately.
 */

void
ipi_parse_resp(ipiq_t *q, rtable_t *table, caddr_t arg)
{
	register rtable_t *rtp;	/* response table pointer */
	register struct ipi3resp *rp;
	u_char		buf[RESP_BUF_LEN];
	register u_char	*cp;
	int		id;		/* parameter id */
	int		plen;		/* parameter length */
	int		rlen;		/* response length */

	rp = q->q_resp;
	rlen = rp->hdr_pktlen + sizeof (rp->hdr_pktlen) -
	    sizeof (struct ipi3resp);
	cp = (u_char *)rp + sizeof (struct ipi3resp);

	/*LINTED variable may be used before set: next */
	for (; rlen > 0; cp += plen) {
		if ((plen = *cp + 1) > rlen) {
			break;
		}
		rlen -= plen;

		if (plen <= 1)
			continue;
		/*
		 * Find parameter ID in the table.
		 * Stop when reaching zero or matching parameter.
		 */
		id = cp[1];
		for (rtp = table; rtp->rt_parm_id != 0 &&
		    rtp->rt_parm_id != id; rtp++)
			;
		if (rtp->rt_func == NULL)
			continue;
		/*
		 * Check alignment if the parameter contains more than
		 * just the length and parameter ID.
		 */
		if (plen > 2 && (((int)cp + 2) % sizeof (long)) != 0) {
			if (plen - 2 > sizeof (buf)) {
				cmn_err(CE_PANIC,
				    "ipi_parse_resp: buffer too short");
			}
			bcopy((caddr_t)cp+2, (caddr_t)buf, (u_int)plen-2);
			(*rtp->rt_func)(q, id, buf, plen-2, arg);
		} else {
			(*rtp->rt_func)(q, id, cp+2, plen-2, arg);
		}
	}
}

/*
 * Print IPI parameters from response or command packet for debugging.
 *
 * bp - start of packet (for length and offset of parms)
 * cp -  pointer to start of parameters
 */

static void
ipi_print_parms(u_char *bp, u_char *cp)
{
	int	len;	/* length remaining in packet */
	int	plen;	/* length of parameter */
	int	col;	/* column number */


	/*
	 * Length is in first short, and doesn't include the 2 bytes for the
	 * length itself.  Subtract out the header length.
	 */

	len = ((struct ipi3header *)bp)->hdr_pktlen + sizeof (u_short) -
	    (cp-bp);

	/*
	 * print header for parameters
	 */
	if (len > 0)
		cmn_err(CE_CONT,
		    "\n  addr  len ID  1  2  3  4  5  6  7  8  9  "
		    "a  b  c  d  e  f 10\n");

	while (len > 0) {
		plen = *cp + 1;		/* plen includes length byte */
		if (plen > len) {
			cmn_err(CE_CONT, "Parameter length error:\n");
			plen = len;
		}
		len -= plen;
		cmn_err(CE_CONT, "  %4x : ", cp - bp);
		col = -2;		/* column number for length */
		while (plen-- > 0) {
			if (col++ >= 0x10) {
				cmn_err(CE_CONT, "\n  %4x :       ", cp - bp);
				col = 1;
			}
			cmn_err(CE_CONT, "%2x ", *cp++);
		}
		cmn_err(CE_CONT, "\n");
	}
	cmn_err(CE_CONT, "\n");
}

/*
 * Print command packet for debugging and errors.
 */

void
ipi_print_cmd(struct ipi3header *ip, char *message)
{
	cmn_err(CE_CONT, "%s: channel 0  slave %x  facility %x\n", message,
	    ip->hdr_slave, ip->hdr_facility);
	cmn_err(CE_CONT, "IPI command:\n  len %x  refnum %x   opcode %2x  mod "
	    "%2x\n", ip->hdr_pktlen, ip->hdr_refnum, ip->hdr_opcode,
	    ip->hdr_mods);
	ipi_print_parms((u_char *)ip,
	    (u_char *)ip + sizeof (struct ipi3header));
}


/*
 * Print response for debugging and errors.
 */
void
ipi_print_resp(struct ipi3resp *rp, char *message)
{
	cmn_err(CE_CONT, "%s: channel 0  slave %x  facility %x\n", message,
	    rp->hdr_slave, rp->hdr_facility);
	cmn_err(CE_CONT, "IPI Response:\n  len %x  refnum %x   opcode %2x  mod"
	    " %2x  stat %4x\n", rp->hdr_pktlen, rp->hdr_refnum,
	    rp->hdr_opcode, rp->hdr_mods, rp->hdr_maj_stat);

	ipi_print_parms((u_char *)rp, (u_char *)rp + sizeof (struct ipi3resp));
}
