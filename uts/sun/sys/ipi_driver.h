/*
 * Copyright (c) 1991, 1992 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_IPI_DRIVER_H
#define	_SYS_IPI_DRIVER_H

#pragma ident	"@(#)ipi_driver.h	1.9	93/02/04 SMI"

/*
 * Definitions for IPI-3 device drivers.
 */

#include <sys/types.h>
#include <sys/buf.h>
#include <sys/ipi3.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Macros to convert IPI device addresses to channel, slave, and facility
 * numbers, and to form addresses from those numbers.
 *
 * For convenience, the channel, slave, and facility numbers are combined
 * into a 32 bit address:   16 bit channel, 8 bit slave, 8 bit facility.
 * The slave and facility numbers may be 0xff to indicate no slave or
 * no facility is being addressed.  If the facility is 0xff, the slave
 * itself is being addressed.
 */

#define	IPI_NO_ADDR		(0xff)
#define	IPI_NSLAVE		8		/* slaves per channel */
#define	IPI_NFAC		8		/* max facilities per slave */

#define	IPI_MAKE_ADDR(c, s, f)	((c)<<16|(s)<<8|(f))
#define	IPI_CHAN(a)		((unsigned int)(a)>>16)
#define	IPI_SLAVE(a)		(((int)(a)>>8)&0xff)
#define	IPI_FAC(a)		((int)(a)&0xff)
#define	IPI_SLAVE_ADDR(a)	((int)(a)|IPI_NO_ADDR)

typedef int ipi_addr_t;

/*
 * IPI queue request element.
 *
 * Fields explained:
 *
 * q_next	driver can modify before start or after completion only.
 *
 * q_addr	this is a readonly value (set by *id_setup (below))
 *
 * q_cmd	this points to an ipi3 command packet that the driver
 *		fills in prior to issuing the command (see id_start below).
 *		The channel subsystem will fill in the command reference
 *		number when the command is sent to the slave or facility.
 *		NOTE: for asynchronous responses, this field will be NULL!
 *
 * q_resp:	pointer to an ipi3 response packet, valid after a completion
 *		routine is called, but not valid after a return from the
 *		completion call. NOTE: this field will be NULL if there
 *		is no response.
 *
 * q_private:	private storage for the slave or facility driver.
 *
 * q_tnp:	pointer to transfer notification parameter that the
 *		slave or facility driver must insert into its command
 *		(length of this paramater is in q_tnp_len (below)).
 *
 * q_time:	time, in seconds, a driver allows for this command to
 *		complete. Set to zero to disable timeout for command.
 *
 * q_flag:	command flags
 *
 * q_tnp_len:	length of transfer notification parameter (read only to
 *		slave or facility driver).
 *
 * q_retry:	number of times a retry on a command attempted. It is up
 *		to the slave/facility driver to decide when enough retries
 *		have been done. This field is zeroed on ipiq_t allocation.
 *
 * q_result:	result/state code, valid only upon command completion.
 *
 */

typedef struct ipiq {
	struct ipiq 	*q_next;	/* next element in queue */
	ipi_addr_t	q_addr;		/* channel/slave/facility address */
	struct ipi3header *q_cmd;	/* command packet */
	struct ipi3resp	*q_resp;	/* response packet (if any) */
	u_long		q_private[2];	/* facility driver data */
#define	Q_BUF		0
#define	Q_ERRBLK	1
	caddr_t		q_tnp;		/* transfer notification parameter */
	u_long		q_flag;		/* flags for request */
	long		q_time;		/* timeout value in seconds */
	u_char		q_tnp_len;	/* length of transfer notification */
	u_char		q_retry;	/* number of times retry attempted */
	u_char		q_result;	/* code summarizing response */

} ipiq_t;

/*
 * On completion of IPI channel commands,
 * an interrupt handling function is called.
 *
 * It should be declared like this:
 *
 *	void
 *	ipi_handler(ipiq_t *q)
 *
 * The IPI channel subsystem is informed of the existence of this
 * interrupt handler function by the use of the id_ctrl IPI vector
 * function (see below) with the IPI_CTRL_REGISTER_IFUNC argument.
 *
 * The result of the operation is summarized in q->q_result
 * by one of the values described below.
 *
 * Notes:
 *
 * 1. IP_MISSING
 *
 *	The abort action recommended here is an actual IPI ABORT command
 *	if the slave supports it. Otherwise, just retry the command.
 *
 * 2. IP_ABORTED
 *
 *	The fact that this value is in the result code only reflects
 *	that channel has returned this command at the request of the
 *	slave or facility driver. The command may actually still be
 *	in progress on the slave or facility. An actul IPI ABORT
 *	command may be warranted to try and ensure that the command
 *	is actually not still running on the slave or facility.
 *
 * 3. IP_ERROR vs. IP_COMPLETE
 *
 *	The only time IP_ERROR should be set is when the transport
 *	layer detects some kind of local (i.e., non-IPI) fault condition.
 */

#define	IP_FREE		0	/* on free list */
#define	IP_SUCCESS	1	/* successful completion */
#define	IP_ERROR	2	/* error during transfer - see response */
#define	IP_COMPLETE	3	/* complete but response needs analysis */
#define	IP_OFFLINE	4	/* channel or device not available */
#define	IP_INVAL	5	/* host software detected invalid command */
#define	IP_MISSING	7	/* time limit expired - abort recommended */
#define	IP_RESET	8	/* reset of channel or slave destroyed cmd */
#define	IP_ABORTED	9	/* command aborted by request */
#define	IP_ASYNC	10	/* this is an asynchronous response packet */
#define	IP_ALLOCATED	11	/* not on free list, but not active either */
#define	IP_INPROGRESS	12	/* issued, active, but not done yet */

/*
 * Flags in ipiq_t.
 */

#define	IP_PRIORITY_CMD	0x1	/* run this command ahead of anything else */

#define	IP_DD_FLAG0	0x1000	/* flags for device drivers to pass */
#define	IP_DD_FLAG1	0x2000
#define	IP_DD_FLAG2	0x4000
#define	IP_DD_FLAG3	0x8000
#define	IP_DD_FLAG4	0x10000
#define	IP_DD_FLAG5	0x20000
#define	IP_DD_FLAG6	0x40000
#define	IP_DD_FLAG7	0x80000
#define	IP_DD_FLAG8	0x100000
#define	IP_DD_FLAGS	0x1ff000	/* mask for driver flags */
/*
 * Mask for driver-settable flags.
 */
#define	IP_DRIVER_FLAGS	IP_DD_FLAGS

/*
 * Table of response handling functions.
 * 	Terminated by zero entry in rt_parm_id.
 * 	These tables are passed to ipi_parse_resp() with a pointer to the
 *	ipiq.  The handling function for the parameters that are present
 *	are called with the ipiq and the parameter.  They return after
 *	setting flags in the ipiq for use by the driver.  The parameters
 *	may occur in any order.
 *	The table ends with a zero parameter ID.  If that entry has a
 *	function pointer, that function is called to handle any parameters
 *	that aren't explicitly mentioned in the rest of the table.
 *
 * Call to ipi_parse_resp:
 *
 *	void ipi_parse_resp(ipiq_t *q, rtable_t table, void *arg)
 *
 * Handling function call:
 *
 * 	void func(ipiq_t *q, int parm_id, u_char *parm, int len, void *arg)
 */
typedef struct rtable {
	char	rt_parm_id;	/* parameter ID (0 if end of table) */
	char	rt_min_len;	/* minimum length of parameter */
	int	(*rt_func)();	/* handling function */
} rtable_t;

/*
 * Autoconfiguration/IPI Glue
 */

/*
 * Structure for communication between slave/facility and channel drivers.
 * Contains a vector of functions to call to perform services.
 *
 * id_setup-	allocate an ipiq_t (and possibly setup DMA).
 *
 *	Returns 0 for success, filling in the passed
 *	result pointer with the allocated ipiq_t.
 *
 *	Other Return values:
 *
 *		-1	could not allocate either an ipiq_t or
 *			DMA resources do to resource exhaustion.
 *			This value will not be returned if the
 *			callback arguments specified is DDI_DMA_SLEEP.
 *			Otherwise, this is not an error condition.
 *
 *		EINVAL	malformed arguments.
 *
 *		ENXIO	addressed slave doesn't exist (or went offline)
 *
 *		EFAULT	either a bad address in the passed buffer, or
 *			a failure to properly do the DMA mapping.
 *
 */

typedef struct ipi_driver {
#ifndef	__STDC__
	int		(*id_setup)();	/* allocate cmd and setup DMA */
	void		(*id_relse)();	/* free command packet */
	void		(*id_cmd)();	/* send command */
	void		(*id_ctrl)();	/* control functions */
#else	/* __STDC__ */
	int (*id_setup)(ipi_addr_t, struct buf *,
	    int ((*)()), caddr_t, ipiq_t **);
	void (*id_relse)(ipiq_t *);
	void (*id_cmd)(ipiq_t *);
	void (*id_ctrl)(ipi_addr_t, int, void *, int *);
#endif	/* __STDC__ */
} ipi_driver_t;

/*
 * Defines for control functions
 */
#define	IPI_CTRL_RESET_SLAVE	1	/* reset slave (synchronous)  */
#define	IPI_CTRL_REGISTER_IFUNC	3	/* register interrupt handler */
#define	IPI_CTRL_LIMIT_SQ	4	/* set slave queue limit */
#define	IPI_CTRL_NACTSLV	5	/* # of commands active on slave */
#define	IPI_CTRL_DMASYNC	6	/* do a ddi_dma_sync(CPU) on a q */
#define	IPI_CTRL_PRINTCMD	7	/* (ipi_print_cmd) */
#define	IPI_CTRL_PRINTRESP	8	/* (ipi_print_resp) */
#define	IPI_CTRL_PARSERESP	9	/* (ipi_parse_resp) */

/*
 * For IPI_CTRL_PRINTCMD and IPI_CTRL_PRINTRESP control functions,
 * the caller constructs this structure, where arg points to either
 * an ipi command or an ipi response, and where msg points to a
 * message to print. The 3rd argument to id_ctrl then is set to
 * point to this structure.
 */

struct icparg {
	void *arg;
	char *msg;
};

/*
 * For the IPI_CTRL_PARSERESP control function, the caller
 * fills in this kind of structure, where q points to the
 * ipiq just completed, rt points to an ipi response parse
 * table and a points to an opaque argument that will be
 * passed to the response parsing functions in the response
 * parse table. The 3rd argument to id_ctrl should then point
 * to this structure.
 */

struct icprarg {
	ipiq_t *q;
	rtable_t *rt;
	caddr_t a;
};

/*
 * Structure presented to IPI slave and facility drivers
 * at autoconfiguration time (as part of device private data).
 */

typedef struct ipi_config {
	ipi_addr_t	ic_addr;	/* System IPI address */
	ipi_driver_t	ic_vector;	/* IPI sw communication layer */
	void		*ic_lkinfo;	/* DDI lock seed information */
} ipi_config_t;

/*
 * IPI Library routines.
 */

#ifdef	_KERNEL
#ifdef	__STDC__
void ipi_parse_resp(ipiq_t *, rtable_t *, caddr_t);
void ipi_print_cmd(struct ipi3header *, char *);
void ipi_print_resp(struct ipi3resp *, char *);
#else	/* __STDC__ */
extern void ipi_parse_resp(), ipi_print_cmd(), ipi_print_resp();
#endif	/* __STDC__ */
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IPI_DRIVER_H */
