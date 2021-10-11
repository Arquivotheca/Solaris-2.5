#ident	"@(#)dedump.c	1.14	94/03/31 SMI"	/* SVr3.2H 	*/

/*
 * Dump streams module. Could be used anywhere on the stream to
 * print all message headers and data on to the console or logged
 * by using the log(7) driver.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/dedump.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern struct streamtab dedumpinfo;

static struct fmodsw fsw = {
	"dedump",
	&dedumpinfo,
	D_NEW
};

/*
 * Module linkage information for the kernel.
 */
extern struct mod_ops mod_strmodops;

static struct modlstrmod modlstrmod = {
	&mod_strmodops, "dump streams module", &fsw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlstrmod, NULL
};


int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

char  contents[64];

unsigned char
message_code[MESSAGE_NUM] = {
	M_DATA, M_PROTO, M_BREAK, M_PASSFP,
	M_SIG, M_DELAY, M_CTL, M_IOCTL,
	M_SETOPTS, M_RSE, M_IOCACK, M_IOCNAK,
	M_PCPROTO, M_PCSIG, M_READ, M_FLUSH,
	M_STOP, M_START, M_HANGUP, M_ERROR,
	M_COPYIN, M_COPYOUT, M_IOCDATA, M_PCRSE,
	M_STARTI, M_STOPI, M_UNHANGUP
};


char *
message_name[MESSAGE_NUM] = {
	"M_DATA", "M_PROTO", "M_BREAK", "M_PASSFP",
	"M_SIG", "M_DELAY", "M_CTL", "M_IOCTL",
	"M_SETOPTS", "M_RSE", "M_IOCACK", "M_IOCNAK",
	"M_PCPROTO", "M_PCSIG", "M_READ", "M_FLUSH",
	"M_STOP", "M_START", "M_HANGUP", "M_ERROR",
	"M_COPYIN", "M_COPYOUT", "M_IOCDATA", "M_PCRSE",
	"M_STARTI", "M_STOPI", "M_UNHANGUP"
};

int
message_size[MESSAGE_NUM] = {
	NULL, NULL, NULL, NULL,
	CHAR, INT, NULL, IOC_SIZE,
	OPT_SIZE, NULL, IOC_SIZE, IOC_SIZE,
	NULL, CHAR, LONG, CHAR,
	NULL, NULL, NULL, CHAR,
	REQ_SIZE, REQ_SIZE, REP_SIZE, NULL,
	NULL, NULL, NULL
};

static int dedumpopen(queue_t *q, dev_t *devp, int oflag, int sflag,
	cred_t *crp);
static int dedumpput(queue_t *q, mblk_t *mp);
static int dedumpclose(queue_t *q, int flag, cred_t *crp);

static void prt_message(queue_t *q, mblk_t *mp);

struct module_info dedumpmiinfo = {
	0xaaa,
	"dedump",
	0,
	INFPSZ,
	(unsigned long)INFPSZ,
	(unsigned long)INFPSZ
};

struct module_info dedumpmoinfo = {
	0xaaa,
	"dedump",
	0,
	INFPSZ,
	(unsigned long)INFPSZ,
	(unsigned long)INFPSZ
};

struct qinit dedumprinit = {
	dedumpput,
	NULL,
	dedumpopen,
	dedumpclose,
	NULL,
	&dedumpmiinfo,
	NULL
};

struct qinit dedumpwinit = {
	dedumpput,
	NULL,
	NULL,
	NULL,
	NULL,
	&dedumpmoinfo,
	NULL
};

struct streamtab dedumpinfo = {
	&dedumprinit,
	&dedumpwinit,
	NULL,
	NULL,
};

int ddbg;
#define	d1printf		if (ddbg >= 1) printf
#define	d2printf		if (ddbg >= 2) printf
#define	d3printf		if (ddbg >= 3) printf

extern struct dmp *dump;
extern int dump_cnt;


/*
 *  /dev/dump open
 */
/*ARGSUSED1*/
static int
dedumpopen(queue_t *q, dev_t *devp, int oflag, int sflag,
	cred_t *crp)
{

	struct dmp *dp;
	short i;

	d3printf("open routine called\n");
	printf("dump: in dumpopen routine\n");

	if (!sflag) {
		printf("Non module open");
		return (ENXIO);
	}

	if (q->q_ptr) {
		return (0); /* already attached */
	}
	for (dp = dump, i = 0; dp->dump_flags & DUMP_IN_USE; dp++, i++)
		if (dp >= &dump[dump_cnt-1]) {
			printf("dump: No dump structures\n");
			ttolwp(curthread)->lwp_error = ENOSPC;
			return (ENOSPC);
		}

	dp->dump_flags 	= DUMP_IN_USE;
	dp->dump_no	= i;


	dp->dump_wq = WR(q);
	dp->dump_flags = PRT_TO_CONSOLE;  /* default is print to console */

	q->q_ptr = (caddr_t) dp;
	WR(q)->q_ptr = (caddr_t) dp;

	return (0);
}

/*ARGSUSED1*/
static int
dedumpclose(queue_t *q, int flag, cred_t *crp)
{

	struct dmp *dp;
	d3printf("close routine called\n");

	dp = (struct dmp *)q->q_ptr;

	dp->dump_flags = dp->dump_no = 0;
	dp->dump_wq = NULL;

	q->q_ptr = WR(q)->q_ptr = NULL;
	return (0);
}

/*
 * dump put procedure.
 * Common for upstream and downstream
 */
static int
dedumpput(queue_t *q, mblk_t *mp)
{
	struct dmp *dp;
	struct iocblk *iocp;

	d3printf("put routine called\n");

	dp = (struct dmp *)q->q_ptr;

	switch (mp->b_datap->db_type) {

		case M_IOCTL:
			iocp = (struct iocblk *)mp->b_rptr;

			switch (iocp->ioc_cmd) {
				case SET_OPTIONS:
					dp->dump_flags =
					    *(short *)mp->b_cont->b_rptr;
					d3printf("wput: flags %o\n",
					    dp->dump_flags);
					mp->b_datap->db_type = M_IOCACK;
					qreply(q, mp);
					return (0);
				default:
					prt_message(q, mp);
					putnext(q, mp);
					return (0);
			}
		default:
			prt_message(q, mp);
			putnext(q, mp);
			return (0);
	}
}

static void
prt_message(queue_t *q, mblk_t *mp)
{
	struct dmp *dp;
	short index, i;
	unsigned char type, direction, mod, div;

	dp = (struct dmp *)q->q_ptr;

	type = mp->b_datap->db_type;
	direction = (q->q_flag & QREADR)? READ : WRITE;

	/* find the index into message code array */

	for (index = 0; index < MESSAGE_NUM; index++) {
		if (type == message_code[index]) {
			if (message_size[index] != NULL)
				bcopy((caddr_t)mp->b_rptr, contents,
				    (uint)message_size[index]);
			if (dp->dump_flags & PRT_TO_CONSOLE) {
				printf("MID 0x%x SID %d %s %s",
					q->q_qinfo->qi_minfo->mi_idnum,
					dp->dump_no, message_name[index],
					(direction) ? "DOWN ":"UP ");
				if (message_size[index] != NULL) {
					for (i = 0; i < message_size[index];
					    i++) {
						if (contents[i] == 0)
							printf("00");
						else {
							mod = contents[i] / 16;
							div = contents[i] -
							    16 * mod;
							printf("%x%x", mod,
							    div);
						}
					}
				}
				printf("\n");
			}
#ifdef notneeded
			if (dp->dump_flags & PRT_TO_LOG)
				(void) strlog(q->q_qinfo->qi_minfo->mi_idnum,
					dp->dump_no, 1, SL_TRACE,
					"D %d %d %s\n", type, direction,
					contents);
#endif
			return;
		}
	}
}
