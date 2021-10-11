/*
 * @(#)audit_start.c 2.23 92/02/29 SMI; SunOS CMW
 * @(#)audit_start.c 4.2.1.2 91/05/08 SMI; BSM Module
 *
 * This file contains the envelope code for system call auditing.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/kmem.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/user.h>
#include <sys/stropts.h>
#include <sys/systm.h>
#include <sys/pathname.h>
#include <c2/audit.h>		/* needs to be included before ucred.h */
#include <c2/audit_kernel.h>
#include <c2/audit_kevents.h>
#include <c2/audit_record.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

extern int audit_active;
extern struct audit_queue au_queue;
struct p_audit_data *padata;		/* mars needs locks */
struct t_audit_data *tadata;		/* mars needs locks */
extern long audit_policy;	/* audit policy in force */
extern int  au_auditstate;
extern u_int num_syscall;	/* size of audit_s2e table */
extern kmutex_t pidlock;	/* proc table lock */

/*
 * Global flags for locking structures that may be referenced by multiple
 *   processes or threads.
 */
kmutex_t  cwrd_lock;	/* lock for cwrd that hangs off of p_audit_data */
kmutex_t  cktime_lock;	/* currently unused */
kmutex_t  au_membuf_lock;	/* lock for free au_membufs */
kmutex_t  au_stat_lock;	/* audit statistics lock */
kmutex_t  au_fstat_lock;	/* audit file statistics lock */
kmutex_t  au_seq_lock;	/* audit sequence count lock */
kmutex_t  checktime_lock; /* timer count lock */
kmutex_t  au_svc_lock;	/* Only one audit svc at a time */

int audit_load = 0;
struct p_audit_data *pad0;
struct t_audit_data *tad0;

/*
 * cv wait structures
 */
kcondvar_t checktime_cv;

/*
 * Das Boot. Initialize first process. Also generate an audit record indicating
 * that the system has been booted.
 */
void
audit_init()
{
	extern int naevent;
	extern char *au_buffer;
	int flag = 1;
	kthread_t *au_thread;

	if (audit_load == 0) {
		audit_active = 0;
		au_auditstate = AUC_DISABLED;
		return;
	} else if (audit_load == 2) {
		debug_enter((char *)NULL);
	}

	audit_active = 1;
	set_all_proc_sys();		/* set pre- and post-syscall flags */

	dprintf(1, ("audit_init()\n"));
	call_debug(1);

		/* initialize statistics structure */
	audit_statistics.as_version  = TOKEN_VERSION;
	audit_statistics.as_numevent = naevent;
	audit_file_stat.af_filesz = 0;
	audit_file_stat.af_currsz = 0;

#ifdef C2_DEBUG
		/* initial policy DEBUG only */
	audit_policy |= AUDIT_SEQ;
#endif

/* setup mutex's for audit package */
	mutex_init(&au_queue.lock, "audit queue lock", MUTEX_DEFAULT,
	    DEFAULT_WT);
	mutex_init(&cwrd_lock, "cwrd lock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&cktime_lock, "free space lock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&au_stat_lock, "audit stat lock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&au_fstat_lock, "au_fstat lock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&au_membuf_lock, "au_membuf alloc lock", MUTEX_DEFAULT,
	    DEFAULT_WT);
	mutex_init(&checktime_lock, "time lock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&au_svc_lock, "audit svc lock", MUTEX_DEFAULT, DEFAULT_WT);

	cv_init(&au_queue.write_cv, "audit queue write lock", CV_DRIVER, NULL);
	cv_init(&au_queue.read_cv, "audit queue read lock", CV_DRIVER, NULL);

		/* inital thread structure */
	AS_INC(as_memused, sizeof (struct t_audit_data));
	tad0 = (struct t_audit_data *)
		kmem_zalloc(sizeof (struct t_audit_data), KM_NOSLEEP);

		/* initial process structure */
	AS_INC(as_memused, sizeof (struct p_audit_data));
	pad0 = (struct p_audit_data *)
		kmem_zalloc(sizeof (struct p_audit_data), KM_NOSLEEP);

	T2A(curthread) = (caddr_t)tad0;
	P2A(curproc) = (caddr_t)pad0;
	dprintf(1, ("audit_init: tad: %x  pad: %x\n", tad0, pad0));
	call_debug(1);

/* The kernel allocates a bunch of threads make sure they have a valid tad */

	mutex_enter(&pidlock);

	au_thread = curthread;
	do {
		if (T2A(au_thread) == NULL) {
			T2A(au_thread) = (caddr_t)tad0;
#ifdef NOTDEF
			printf("Null tad (%x)\n", au_thread);
#endif
		}
		au_thread = au_thread->t_next;
	} while (au_thread != curthread);

		/* initialize memory allocator */

	au_mem_init();

	tad0->tad_ad   = NULL;
	mutex_exit(&pidlock);

	pad0->pad_auid = -2;
	pad0->pad_cwrd = getcw();
	dprintf(1, ("audit_init: pad_cwrd: %x\n", pad0->pad_cwrd));
	call_debug(1);

	pad0->pad_cwrd->cwrd_ref = 1;
	pad0->pad_cwrd->cwrd_ldbuf   = 2;
	pad0->pad_cwrd->cwrd_dirlen  = 2;
	AS_INC(as_memused, 2);
	pad0->pad_cwrd->cwrd_dir = (caddr_t)kmem_alloc(2, KM_NOSLEEP);
	bcopy("/", pad0->pad_cwrd->cwrd_dir, 2);
	pad0->pad_cwrd->cwrd_lrbuf   = 2;
	pad0->pad_cwrd->cwrd_rootlen = 2;
	AS_INC(as_memused, 2);
	pad0->pad_cwrd->cwrd_root = (caddr_t)kmem_alloc(2, KM_NOSLEEP);
	bcopy("/", pad0->pad_cwrd->cwrd_root, 2);
	mutex_init(&pad0->pad_lock, "pad lock", MUTEX_DEFAULT, DEFAULT_WT);


	dprintf(1, ("audit_init: initial pad %x tad %x\n", pad0, tad0));
	call_debug(1);

	/*
	 * allocate pseudo structures padata and tadata for
	 * non-user generated events
	 */
		/* process structure */
	AS_INC(as_memused, sizeof (struct p_audit_data));
	padata = (struct p_audit_data *)
		kmem_zalloc(sizeof (struct p_audit_data), KM_NOSLEEP);
	padata->pad_auid = -2;
	padata->pad_cwrd = pad0->pad_cwrd;
	padata->pad_cwrd->cwrd_ref++;

	/* thread structure */
	AS_INC(as_memused, sizeof (struct t_audit_data));
	tadata = (struct t_audit_data *)
		kmem_zalloc(sizeof (struct t_audit_data), KM_NOSLEEP);

	dprintf(1, ("audit_init: tadata: %x padata: %x\n", tadata, padata));
	call_debug(1);

		/* setup defaults for audit queue flow control */
	au_queue.hiwater = AQ_HIWATER;
	au_queue.lowater = AQ_LOWATER;
	au_queue.bufsz   = AQ_BUFSZ;		/* default is per au_membuf */
	au_queue.buflen  = AQ_BUFSZ;		/* default is per au_membuf */
	au_queue.delay   = AQ_DELAY;

		/* allocat write buffer */
	AS_INC(as_memused, au_queue.bufsz);
	au_buffer = kmem_alloc((u_int)au_queue.bufsz, KM_NOSLEEP);

		/* fire off timeout event to kick audit queue awake */
	(void) timeout(au_queue_kick, (caddr_t)0, (int)au_queue.delay);

		/* generate a system-booted audit record */
	au_write(&(tadata->tad_ad), au_to_text("booting kernel"));

	AS_INC(as_generated, 1);
	AS_INC(as_nonattrib, 1);

	if (audit_async_block())
		flag = 0;

		/* Add an optional sequence token */
	if ((audit_policy & AUDIT_SEQ) && flag)
		au_write(&(tadata->tad_ad), au_to_seq());

	au_close(&(tadata->tad_ad), flag, AUE_SYSTEMBOOT, PAD_NONATTR);

	dprintf(1, ("exit audit_init\n"));
	call_debug(1);
}

/*ARGSUSED*/
void
au_queue_kick(t)
	int t;
{
		/* wakeup reader if its not running */
	if (au_queue.cnt && au_queue.rd_block)
		cv_broadcast(&au_queue.read_cv);

		/* fire off timeout event to kick audit queue awake */
	(void) timeout(au_queue_kick, (caddr_t)0, (int)au_queue.delay);
}

void
audit_free()
{
}

/*
 * Enter system call. Do any necessary setup here. allocate resouces, etc.
 */

#include <sys/syscall.h>


/*ARGSUSED*/
void
audit_start(type, scid, error, lwp)

	unsigned type;
	unsigned scid;
	int error;
	klwp_t *lwp;		/* new */

{	/* AUDIT_START */

	long ctrl;
	au_event_t event;
	au_state_t estate;
	struct t_audit_data *tad;


	dprintf(2, ("audit_start(%x,%x,%x)\n", type, scid, error));
	call_debug(2);

	tad = (struct t_audit_data *)U2A(u);
	if (tad == NULL) {
		printf("process/thread: %x/%x\n", (int)curproc, (int)curthread);
		cmn_err(CE_PANIC, "AUDIT_START: no thread audit data");
	}
	dprintf(2, ("audit_start: tad: %x\n", tad));
	call_debug(2);

	if (error) {
		tad->tad_ctrl = 0;
		tad->tad_flag = 0;
		return;
	}
		/*
		 * if this is an indirect system call then don't do anything.
		 * audit_start will be called again from indir() in trap.c
		 */
	if (scid == 0) {
		tad->tad_ctrl = 0;
		tad->tad_flag = 0;
		return;
	}
	if (scid >= num_syscall)
		scid = 0;

		/* we can no longer ber guarantied a valid lwp_ap */
		/* so we need to force it valid a lot of stuff needs it */

	save_syscall_args();

		/* get basic event for system call */
	event = (*audit_s2e[scid].au_init)(audit_s2e[scid].au_event);
		/* get control information */
	ctrl  = audit_s2e[scid].au_ctrl;

	estate = audit_ets[event];

	dprintf(2, ("audit_start: event: %x ctrl %x estate %x\n",
		event, ctrl, estate));
	call_debug(2);

	/*
	 * We need to gather paths for certain system calls even if they are
	 * not audited so that we can audit the various f* calls and be
	 * sure to have a CWD and CAR. Thus we thus set tad_ctrl over the
	 * system call regardless if the call is audited or not.
	 */
	tad->tad_ctrl   = ctrl;
	tad->tad_scid   = scid;

ADDTRACE("[%x] audit_start type %x scid %x error %x tad %x event %x",
	type, scid, error, tad, event, 0);
	/* now do preselection. Audit or not to Audit, that is the question */
	if ((tad->tad_flag = auditme(tad, estate)) == 0)
		return;

#ifdef C2_DEBUG
	if (audit_debug)
		printf("auditting scid %x error %x tadf %x event %x",
			scid, error, tad->tad_flag, event);
#endif
	if (au_auditstate != AUC_AUDITING) {
		tad->tad_flag = 0;
		dprintf(2, ("audit_start done; audit off\n"));
		call_debug(2);
		return;
	}

	tad->tad_event  = event;
	tad->tad_evmod  = 0;

	dprintf(2, ("audit_start processing\n"));
	call_debug(2);

#ifdef SunOS_CMW
		/* DEBUG - take out when safe */
	if (tad->tad_event == 0) {
		int *uap = ttolwp(curthread)->lwp_ap;
		au_uwrite(au_to_arg(0, "system call ID", (u_long) scid-2));
		au_uwrite(au_to_arg(1, "arg 0", (u_long) uap[0]));
		au_uwrite(au_to_arg(2, "arg 1", (u_long) uap[1]));
		au_uwrite(au_to_arg(3, "arg 2", (u_long) uap[2]));
	}
#endif
	(*audit_s2e[scid].au_start)(tad);

	dprintf(2, ("audit_start done; tad %x\n", tad));
	call_debug(2);

}	/* AUDIT_START */

/*
 * system call has completed. Now determine if we genearate an audit record
 * or not.
 */
/*ARGSUSED*/

void
audit_finish(type, scid, error, rval)

	register unsigned type;
	register unsigned scid;
	register int error;
	register rval_t *rval;

{	/* AUDIT_FINISH */

	struct t_audit_data *tad;
	int	flag;

	dprintf(2, ("audit_finish(%x, %x, %x, %x)\n",
		type, scid, error, rval));
	call_debug(2);

	tad = (struct t_audit_data *)U2A(u);

	dprintf(8, ("audit_finish: tad: %x\n", tad));
/*
ADDTRACE("[%x] audit_finish type %x scid %x error: %x rval %x",
	type, scid, error, rval, 0, 0);
*/

	if (tad->tad_flag == 0 && !(tad->tad_ctrl & PAD_SAVPATH)) {
		dprintf(2, ("audit_finish: no auditing\n"));
		call_debug(2);
		/*
		 * clear the ctrl flag so that we don't have spurious
		 * collection of audit information.
		 */
		tad->tad_ctrl = 0;
		return;
	}

	scid = tad->tad_scid;
	dprintf(2, ("scid: %x\n", scid));
	call_debug(2);

	/*
	 * Perform any extra processing and determine if we are
	 * really going to generate any audit record.
	 */
	(*audit_s2e[scid].au_finish)(tad, error, rval);
	dprintf(2, ("audit_finish(%x,%x,%x,%x) done\n",
		type, scid, error, rval));
	call_debug(2);
	if (tad->tad_flag) {
		tad->tad_flag = 0;
		if (flag = audit_success(tad, error)) {
				/* Add a process token */
			au_write(&(u_ad), au_to_subject(curproc));
				/* Add an optional group token */
			if (audit_policy&AUDIT_GROUP)
				au_write(&(u_ad), au_to_groups(curproc));
#ifdef	SunOS_CMW
				/* Add a sensitivity label for the process */
			au_write(&(u_ad), au_to_slabel(&u.u_slabel));
#endif	/* SunOS_CMW */
				/* Add a return token */
			au_write(&(u_ad), au_to_return(error, rval));

			AS_INC(as_generated, 1);
			AS_INC(as_kernel, 1);

			if (audit_sync_block())
				flag = 0;

				/* Add an optional sequence token */
			if ((audit_policy&AUDIT_SEQ) && flag)
				au_write(&(u_ad), au_to_seq());
		}
			/* Close up everything */
		au_close(&(u_ad), flag, tad->tad_event, tad->tad_evmod);
	}
		/* sanity check */
	if (u_ad != NULL) {
		cmn_err(CE_PANIC, "AUDIT_FINISH: residue audit record");
	}

		/* free up any space remaining with the path's */
	if (tad->tad_pathlen) {
		dprintf(2, ("audit_finish: %x %x\n",
				tad->tad_pathlen, tad->tad_path));
		call_debug(2);

		AS_DEC(as_memused, tad->tad_pathlen);
		kmem_free(tad->tad_path, tad->tad_pathlen);
		tad->tad_pathlen = 0;
		tad->tad_path	= NULL;
		tad->tad_vn	= NULL;
	}
	/*
	 * clear the ctrl flag so that we don't have spurious collection of
	 * audit information.
	 */
	dprintf(2, ("audit_finish(%x,%x,%x,%x) done\n",
		type, scid, error, rval));
	tad->tad_ctrl = 0;
	call_debug(2);

}	/* AUDIT_FINISH */

audit_success(tad, error)
	struct t_audit_data *tad;
	int	error;
{
	au_state_t ess;
	au_state_t esf;
	p_audit_data_t *pad = (struct p_audit_data *)P2A(curproc);

	ess = esf = audit_ets[tad->tad_event];

	if (error)
		tad->tad_evmod |= PAD_FAILURE;

#ifdef SunOS_CMW
		/* also check for MAC used to see what else is audited */
	if ((tad->tad_ctrl&PAD_SMAC) && (tad->tad_evmod&PAD_MACUSE))
		return (1);

	if ((tad->tad_ctrl&PAD_FMAC) && (tad->tad_evmod&PAD_MACMASK)) {
		esf |= audit_ets[AUE_MAC];
		return (esf & pad->pad_mask.as_failure);
	}

		/* success use of privilege with system call */
	if ((tad->tad_ctrl&PAD_SPRIV) && (tad->tad_evmod&PAD_SPRIVUSE))
		return (1);

		/* failed use of privilege with system call */
	if ((tad->tad_ctrl&PAD_FPRIV) && (tad->tad_evmod&PAD_FPRIVUSE))
		return (1);

		/* object label floats */
	if ((tad->tad_ctrl&PAD_IFLOAT) && (tad->tad_ctrl&PAD_LFLOAT))
		return (1);
#endif /* SunOS_CMW */

		/* see if we really want to generate an audit record */
	if (tad->tad_ctrl & PAD_NOAUDIT)
		return (0);

		/*
		 * nfs operation and we're auditing privilege or MAC. This
		 * is so we have a client audit record to match a nfs server
		 * audit record.
		 */
	if (tad->tad_ctrl & PAD_AUDITME)
		return (1);

	if (error == 0)
		return (ess & pad->pad_mask.as_success);
	else {
		return (esf & pad->pad_mask.as_failure);
	}
}

/*
 * determine if we've preselected this event (system call). Note that when
 * we preselect use-of-privilege or MAC, we audit everything and throw away
 * the audit record if we never used privilege or MAC. Eventially we will
 * do this more intelligently and only audit everything that might use
 * privilege. We treat label floating specially here (extremely ugly).
 */

auditme(tad, estate)
	struct t_audit_data *tad;
	au_state_t	estate;
{
	int flag = 0;
	p_audit_data_t *pad = (struct p_audit_data *)P2A(curproc);

		/* preselected system call */
	if (pad->pad_mask.as_success & estate ||
	    pad->pad_mask.as_failure & estate) {
		flag = 1;
	} else if ((tad->tad_scid == SYS_putmsg) ||
		(tad->tad_scid == SYS_getmsg)) {
		estate = audit_ets[AUE_SOCKCONNECT]	|
			audit_ets[AUE_SOCKACCEPT]	|
			audit_ets[AUE_SOCKSEND]		|
			audit_ets[AUE_SOCKRECEIVE];
		if (pad->pad_mask.as_success & estate ||
		    pad->pad_mask.as_failure & estate)
			flag = 1;
	}


#ifdef SunOS_CMW
		/* looking for successful use of privilege */
	if (pad->pad_mask.as_success & audit_ets[AUE_UPRIV]) {
		tad->tad_ctrl |= PAD_SPRIV;
		flag = 1;
	}

		/* looking for successful use of MAC */
	if (pad->pad_mask.as_success & audit_ets[AUE_MAC]) {
		tad->tad_ctrl |= PAD_SMAC;
		flag = 1;
	}

		/* looking for failed use of MAC */
	if (pad->pad_mask.as_failure & audit_ets[AUE_MAC]) {
		tad->tad_ctrl |= PAD_FMAC;
		flag = 1;
	}

		/* looking for failed use of privilege */
	if (pad->pad_mask.as_failure & audit_ets[AUE_UPRIV]) {
		tad->tad_ctrl |= PAD_FPRIV;
		flag = 1;
	}

		/* are we preselected for information label floats */
	if ((pad->pad_mask.as_success|pad->pad_mask.as_failure) &
						audit_ets[AUE_IFLOAT]) {
		tad->tad_ctrl |= PAD_LFLOAT;
		flag = 1;
	}
#endif	/* SunOS_CMW */

	return (flag);
}
