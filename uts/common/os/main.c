/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)main.c	1.62	95/08/22 SMI"	/* from SVr4.0 1.31 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/pcb.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/priocntl.h>
#include <sys/procset.h>
#include <sys/var.h>
#include <sys/callo.h>
#include <sys/callb.h>
#include <sys/debug.h>
#include <sys/conf.h>
#include <sys/bootconf.h>
#include <sys/utsname.h>
#include <sys/cmn_err.h>
#include <sys/vmparam.h>
#include <sys/modctl.h>
#include <sys/vm.h>
#include <sys/callb.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>

#include <vm/as.h>
#include <vm/seg_vn.h>

#include <c2/audit.h>

/* well known processes */
proc_t *proc_sched;		/* memory scheduler */
proc_t *proc_init;		/* init */
proc_t *proc_pageout;		/* pageout daemon */
proc_t *proc_fsflush;		/* fsflush daemon */

int	maxmem;		/* Maximum available memory in clicks.	*/
int	freemem;	/* Current available memory in clicks.	*/
vnode_t	*rootdir;
long	dudebug;
int 	nodevflag = D_OLD; /* If an old driver, devsw flag entry points here */
int	audit_active;

extern pri_t maxclsyspri;
extern pri_t minclsyspri;

/*
 * Machine-independent initialization code
 * Called from cold start routine as
 * soon as a stack and segmentation
 * have been established.
 * Functions:
 *	clear and free user core
 *	turn on clock
 *	hand craft 0th process
 *	call all initialization routines
 *	fork	- process 0 to schedule
 *		- process 1 execute bootstrap
 *		- process 2 to page out
 *	create system threads
 */

int gdbon;

#ifdef REDCHECK
int	checkredzone = 0;
#endif /* REDCHECK */

void
main()
{
	register int	(**initptr)();
	extern int	sched();
	extern int	fsflush();
	extern void	icode();
	extern void	thread_reaper();
	extern int	(*init_tbl[])();
	extern int	(*mp_init_tbl[])();
	extern id_t	syscid, initcid;
	extern int	swaploaded;
	extern int	netboot;
	extern int	strplumb(void);
	extern void	vm_init(void);

	/*
	 * In the horrible world of x86 inlines, you can't get symbolic
	 * structure offsets a la genassym.  This assertion is here so
	 * that the next poor slob who innocently changes the offset of
	 * cpu_thread doesn't waste as much time as I just did finding
	 * out that it's hard-coded in i86/ml/i86.il.  You're welcome.
	 */
	ASSERT(curthread == CPU->cpu_thread);

	startup();
	callb_init();
	callout_init();	/* callout table MUST be init'd before clock starts */
	clkstart();

	/*
	 * Initialize file descriptor info in uarea.
	 * NB:  getf() in fio.c expects u.u_nofiles >= NFPCHUNK
	 * NB: falloc() gets called by l_strplumb() when auto pushing
	 * modules. Thus this initialization must be done early in the game.
	 */
	u.u_nofiles = NFPCHUNK;
	u.u_flist = (uf_entry_t *)
	    kmem_zalloc(NFPCHUNK * sizeof (struct uf_entry), KM_SLEEP);

	/*
	 * Call all system initialization functions.
	 */
	for (initptr = &init_tbl[0]; *initptr; initptr++)
		(**initptr)();

	/*
	 * initialize vm related stuff.
	 */
	vm_init();

	ttolwp(curthread)->lwp_error = 0; /* XXX kludge for SCSI driver */

	/*
	 * Drop the interrupt level and allow interrupts.  At this point
	 * the DDI guarantees that interrupts are enabled.
	 */
	spl0();

	vfs_mountroot();	/* Mount the root file system */
	cpu_kstat_init(CPU);	/* after vfs_mountroot() so TOD is valid */

	post_startup();
	swaploaded = 1;

	/*
	 * Initial C2 audit system
	 */
#ifdef C2_AUDIT
	audit_init();	/* C2 hook */
#endif

	/*
	 * Plumb the protocol modules and drivers only if we are not
	 * networked booted, in this case we already did it in rootconf().
	 */
	if (netboot == 0)
		strplumb();

	u.u_start = hrestime.tv_sec;
	ttoproc(curthread)->p_mstart = gethrtime();
	init_mstate(curthread, LMS_SYSTEM);

	/*
	 * Perform setup functions that can only be done after root
	 * and swap have been set up.
	 */

	if (kern_setup2())
		cmn_err(CE_PANIC, "main - kern_setup2 failed");

	/*
	 * Set the scan rate and other parameters of the paging subsystem.
	 */
	setupclock();

	/*
	 * Make init process; enter scheduling loop with system process.
	 */

	/* create init process */
	if (newproc((void (*)())icode, initcid, 59)) {
		panic("main: unable to fork init.");
	}

	/* create pageout daemon */
	if (newproc((void (*)())pageout, syscid, maxclsyspri - 1)) {
		panic("main: unable to fork pageout()");
	}

	/* create fsflush daemon */
	if (newproc((void (*)())fsflush, syscid, minclsyspri)) {
		panic("main: unable to fork fsflush()");
	}

	/*
	 * Create system threads (threads are associated with p0)
	 */

	/* create thread_reaper daemon */
	if (thread_create(NULL, PAGESIZE, (void (*)())thread_reaper,
	    0, 0, &p0, TS_RUN, minclsyspri) == NULL) {
		panic("main: unable to create thread_reaper thread");
	}

	/* create module uninstall daemon */
	/* BugID 1132273. If swapping over NFS need a bigger stack */
	if ((thread_create(NULL, DEFAULTSTKSZ, (void (*)())mod_uninstall_daemon,
	    0, 0, &p0, TS_RUN, minclsyspri)) == NULL) {
		panic("main: unable to create module uninstall thread");
	}

	pid_setmin();

	/*
	 * Perform MP initialization, if any.
	 */
	mp_init();

	/*
	 * After mp_init(), number of cpus are known (this is
	 * true for the time being, when there are acutally
	 * hot pluggable cpus then this scheme  would not do).
	 * Any per cpu initialization is done here.
	 */

	/*
	 * Create the kmem async thread.  This *must* be done after
	 * mp_init() -- it relies on ncpus being known and on each
	 * cpu's cpu_seqid field being set and never changing.
	 * Yet another reason not to support hot plugging.
	 */
	if (thread_create(NULL, PAGESIZE, kmem_async_thread,
	    0, 0, &p0, TS_RUN, minclsyspri) == NULL) {
		panic("main: unable to create kmem async thread");
	}

	for (initptr = &mp_init_tbl[0]; *initptr; initptr++)
		(**initptr)();

	bcopy("sched", u.u_psargs, 6);
	bcopy("sched", u.u_comm, 5);
#ifdef REDCHECK
	checkredzone = 1;
#endif /* REDCHECK */
	sched();
	/* NOTREACHED */
}
