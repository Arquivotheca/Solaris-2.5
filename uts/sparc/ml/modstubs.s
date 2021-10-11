/*
 * Copyright (c) 1991-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident  "@(#)modstubs.s	1.64	95/03/17 SMI"

#include <sys/asm_linkage.h>

#if defined(lint)

char stubs_base[1], stubs_end[1];

#else	/* lint */

/*
 * WARNING: there is no check for forgetting to write END_MODULE,
 * and if you do, the kernel will most likely crash.  Be careful
 *
 * This file assumes that all of the contributions to the data segment
 * will be contiguous in the output file, even though they are separated
 * by pieces of text.  This is safe for all assemblers I know of now...
 */

/*
 * This file uses ansi preprocessor features:
 *
 * 1. 	#define mac(a) extra_ ## a     -->   mac(x) expands to extra_a
 * The old version of this is
 *      #define mac(a) extra_/.*.*./a
 * but this fails if the argument has spaces "mac ( x )"
 * (Ignore the dots above, I had to put them in to keep this a comment.)
 *
 * 2.   #define mac(a) #a             -->    mac(x) expands to "x"
 * The old version is
 *      #define mac(a) "a"
 *
 * For some reason, the 5.0 preprocessor isn't happy with the above usage.
 * For now, we're not using these ansi features.
 *
 * The reason is that "the 5.0 ANSI preprocessor" is built into the compiler
 * and is a tokenizing preprocessor. This means, when confronted by something
 * other than C token generation rules, strange things occur. In this case,
 * when confronted by an assembly file, it would turn the token ".globl" into
 * two tokens "." and "globl". For this reason, the traditional, non-ANSI
 * prepocessor is used on assembly files.
 *
 * It would be desirable to have a non-tokenizing cpp (accp?) to use for this.
 */

/*
 * This file contains the stubs routines for modules which can be autoloaded.
 * stub_install_common is a small assembly routine in XXX.s that calls
 * the c function install_stub.
 */
#define MODULE(module,namespace) \
	.seg	".data"; \
module/**/_modname: \
	.ascii	"namespace/module" ; \
	.byte	0; \
	.align	4; \
	.global module/**/_modinfo; \
module/**/_modinfo: ; \
	.word module/**/_modname; \
	.word 0		/* storage for modctl pointer */

	/* then stub_info structures follow until a mods_func_adr is 0 */

/* this puts a 0 where the next mods_func_adr would be */
#define END_MODULE(module) .word 0

#define STUB(module, fcnname, retfcn) STUB_COMMON(module, fcnname, \
						mod_hold_stub, retfcn, 0)

/* "weak stub", don't load on account of this call */
#define WSTUB(module, fcnname, retfcn) STUB_COMMON(module, fcnname, \
						retfcn, retfcn, 1)

/*
 * "unloable stub", don't bother 'holding' module if it's already loaded
 * since the module cannot be unloaded.
 *
 * User *MUST* guarentee the module is not unloadable (no _fini routine).
 */
#define NO_UNLOAD_STUB(module, fcnname, retfcn) STUB_UNLOADABLE(module, \
						fcnname,  retfcn, retfcn, 0)


#define STUB_COMMON(module, fcnname, install_fcn, retfcn, weak) \
	.seg	".text"; \
	.global	fcnname; \
fcnname: \
	save	%sp,-SA(MINFRAME), %sp;	/* new window */ \
	sethi	%hi(fcnname/**/_info), %l5; \
	or	%l5, %lo(fcnname/**/_info), %l5; \
	ld	[%l5+0x10], %l1;	/* weak?? */ \
	cmp	%l1, 0; \
	be,a	1f;			/* not weak */ \
	restore; \
	ld	[%l5+0xc], %l1;		/* yes, installed?? */ \
	ld	[%l5], %l0; \
	cmp	%l1, %l0; \
	bne,a	1f;			/* yes, so do the mod_hold thing */ \
	restore; \
	mov	%l0, %g1; \
	jmpl	%g1, %g0;		/* no, just jump to retfcn */ \
	restore; \
1: \
	sub	%sp, %fp, %g1;	/* get (-)size of callers stack frame */ \
	save	%sp, %g1, %sp;	/* create new frame same size */ \
	sub	%g0, %g1, %l4;  /* size of stack frame */ \
	sethi	%hi(fcnname/**/_info), %l5; \
	b	stubs_common_code; \
	or	%l5, %lo(fcnname/**/_info), %l5; \
	.seg ".data"; \
	.align	 4; \
fcnname/**/_info: \
	.word	install_fcn; \
	.word	module/**/_modinfo; \
	.word	fcnname; \
	.word	retfcn; \
	.word   weak

#define STUB_UNLOADABLE(module, fcnname, install_fcn, retfcn, weak) \
	.seg	".text"; \
	.global	fcnname; \
fcnname: \
	save	%sp,-SA(MINFRAME), %sp;		/* new window */ \
	sethi	%hi(fcnname/**/_info), %l5; \
	or	%l5, %lo(fcnname/**/_info), %l5; \
	ld	[%l5+0xc], %l1;			/* installed?? */ \
	ld	[%l5], %g1; \
	cmp	%l1, %g1; \
	be,a	1f;				/* no, load module */ \
	restore; \
	jmp	%g1;				/* yes, off we go */ \
	restore; \
1: \
	sub	%sp, %fp, %g1;	/* get (-)size of callers stack frame */ \
	save	%sp, %g1, %sp;	/* create new frame same size */ \
	sub	%g0, %g1, %l4;  /* size of stack frame */ \
	sethi	%hi(fcnname/**/_info), %l5; \
	b	stubs_common_code; \
	or	%l5, %lo(fcnname/**/_info), %l5; \
	.seg ".data"; \
	.align	 4; \
fcnname/**/_info: \
	.word	install_fcn; \
	.word	module/**/_modinfo; \
	.word	fcnname; \
	.word	retfcn; \
	.word   weak

	/*
	 * We branch here with the fcnname_info pointer in l5
	 * and the frame size in %l4.
	 */
stubs_common_code:
	cmp	%l4, SA(MINFRAME)	/* If stack <= SA(MINFRAME) */
	ble,a	2f			/* then don't copy anything */
	ld	[%fp + 40], %g1		/* hidden param for aggreate return */

	sub	%l4, 0x40, %l4		/* skip locals and outs */
	add	%sp, 0x40, %l0
	add	%fp, 0x40, %l1		/* get original sp before save */
1:
	/* Copy stack frame */
	ldd	[%l1], %l2
	inc	8, %l1
	std	%l2, [%l0]
	deccc	8, %l4
	bg,a	1b
	inc	8, %l0
2:
	st	%g1, [%sp + 0x40]	/* aggregate return value */
	call	mod_hold_stub		/* Hold the module */
	mov	%l5, %o0
	cmp	%o0, -1 		/* If error then return error (panic?)*/
	bne,a	1f
	mov	%o0, %l0
	ld	[%l5+0xc], %i0
	ret
	restore
1:
	ld	[%l5], %g1
	mov	%i0, %o0	/* copy over incoming args, if number of */
	mov	%i1, %o1	/* args is > 6 then we copied them above */
	mov	%i2, %o2
	mov	%i3, %o3
	mov	%i4, %o4
	call	%g1		/* jump to the stub function */
	mov	%i5, %o5
	mov	%o0, %i0	/* copy any return values */
	mov	%o1, %i1
	mov	%l5, %o0
	call	mod_release_stub	/* release hold on module */
	mov	%l0, %o1
	ret			/* return to caller */
	restore

! this is just a marker for the area of text that contains stubs 
	.seg ".text"
	.global stubs_base
stubs_base:
	nop

/*
 * WARNING WARNING WARNING!!!!!!
 * 
 * On the MODULE macro you MUST NOT use any spaces!!! They are
 * significant to the preprocessor.  With ansi c there is a way around this
 * but for some reason (yet to be investigated) ansi didn't work for other
 * reasons!  
 *
 * When zero is used as the return function, the system will call
 * panic if the stub can't be resolved.
 */

/*
 * Stubs for specfs. A non-unloadable module.
 */

#ifndef SPEC_MODULE
	MODULE(specfs,fs);
	NO_UNLOAD_STUB(specfs, common_specvp,  	nomod_zero);
	NO_UNLOAD_STUB(specfs, makectty,		nomod_zero);
	NO_UNLOAD_STUB(specfs, makespecvp,      	nomod_zero);
	NO_UNLOAD_STUB(specfs, smark,           	nomod_zero);
	NO_UNLOAD_STUB(specfs, spec_segmap,     	nomod_einval);
	NO_UNLOAD_STUB(specfs, specfind,        	nomod_zero);
	NO_UNLOAD_STUB(specfs, specvp,          	nomod_zero);
	NO_UNLOAD_STUB(specfs, stillreferenced, 	nomod_zero);
	NO_UNLOAD_STUB(specfs, spec_getvnodeops,	nomod_zero);
	END_MODULE(specfs);
#endif


/*
 * Stubs for nfs common code.
 * XXX nfs_getvnodeops should go away with removal of kludge in vnode.c
 */
#ifndef NFS_MODULE
	MODULE(nfs,fs);
	WSTUB(nfs,	nfs_getvnodeops,	nomod_zero);
	WSTUB(nfs,	nfs_perror,		nomod_zero);
	WSTUB(nfs,	nfs_cmn_err,		nomod_zero);
	END_MODULE(nfs);
#endif

/*
 * Stubs for nfs_dlboot (diskless booting).
 */
#ifndef NFS_DLBOOT_MODULE
	MODULE(nfs_dlboot,misc);
	STUB(nfs_dlboot,	mount_root,	nomod_minus_one);
	STUB(nfs_dlboot,	mount3_root,	nomod_minus_one);
	END_MODULE(nfs_dlboot);
#endif

/*
 * Stubs for nfs server-only code.
 * nfs_fhtovp and nfs3_fhtovp are not listed here because of NFS
 * performance considerations.
 */
#ifndef NFSSRV_MODULE
	MODULE(nfssrv,misc);
	NO_UNLOAD_STUB(nfssrv,	nfs_svc,	nomod_zero);
	END_MODULE(nfssrv);
#endif

/*
 * Stubs for kernel lock manager.
 */
#ifndef KLM_MODULE
	MODULE(klmmod,misc);
	NO_UNLOAD_STUB(klmmod, lm_svc,		nomod_zero);
	NO_UNLOAD_STUB(klmmod, lm_shutdown,	nomod_zero);
	END_MODULE(klmmod);
#endif

#ifndef KLMOPS_MODULE
	MODULE(klmops,misc);
	NO_UNLOAD_STUB(klmops, lm_frlock,	nomod_zero);
	NO_UNLOAD_STUB(klmops, lm4_frlock,	nomod_zero);
	NO_UNLOAD_STUB(klmops, lm_nlm_dispatch,	nomod_zero);
	NO_UNLOAD_STUB(klmops, lm_nlm4_dispatch,	nomod_zero);
	NO_UNLOAD_STUB(klmops, lm_nlm_reclaim,	nomod_zero);
	NO_UNLOAD_STUB(klmops, lm_nlm4_reclaim,	nomod_zero);
	END_MODULE(klmops);
#endif

/*
 * Stubs for kernel TLI module
 *   XXX currently we never allow this to unload
 */
#ifndef TLI_MODULE
	MODULE(tlimod,misc);
	NO_UNLOAD_STUB(tlimod, t_kopen,		nomod_minus_one);
	NO_UNLOAD_STUB(tlimod, t_kunbind,  	nomod_zero);
	NO_UNLOAD_STUB(tlimod, t_kadvise,  	nomod_zero);
	NO_UNLOAD_STUB(tlimod, t_krcvudata,  	nomod_zero);
	NO_UNLOAD_STUB(tlimod, t_ksndudata,  	nomod_zero);
	NO_UNLOAD_STUB(tlimod, t_kalloc,  	nomod_zero);
	NO_UNLOAD_STUB(tlimod, t_kbind,  	nomod_zero);
	NO_UNLOAD_STUB(tlimod, t_kclose,  	nomod_zero);
	NO_UNLOAD_STUB(tlimod, t_kspoll,  	nomod_zero);
	NO_UNLOAD_STUB(tlimod, t_kfree,  	nomod_zero);
	END_MODULE(tlimod);
#endif

/*
 * Stubs for kernel RPC module
 *   XXX currently we never allow this to unload
 */
#ifndef RPC_MODULE
	MODULE(rpcmod,misc);
	NO_UNLOAD_STUB(rpcmod, clnt_tli_kcreate,	nomod_minus_one);
	NO_UNLOAD_STUB(rpcmod, svc_tli_kcreate,		nomod_minus_one);
	NO_UNLOAD_STUB(rpcmod, bindresvport,		nomod_minus_one);
	NO_UNLOAD_STUB(rpcmod, xdrmblk_init,		nomod_zero);
	NO_UNLOAD_STUB(rpcmod, xdrmem_create,		nomod_zero);
	END_MODULE(rpcmod);
#endif

/*
 * Stubs for des
 */
#ifndef DES_MODULE
	MODULE(des,misc);
	STUB(des, cbc_crypt, 	 	nomod_zero);
	STUB(des, ecb_crypt, 		nomod_zero);
	STUB(des, _des_crypt,		nomod_zero);
	END_MODULE(des);
#endif

/*
 * Stubs for procfs. An non-unloadable module.
 */
#ifndef PROC_MODULE
	MODULE(procfs,fs);
	NO_UNLOAD_STUB(procfs, prfree,		nomod_zero);
	NO_UNLOAD_STUB(procfs, prexit,      	nomod_zero);
	NO_UNLOAD_STUB(procfs, prlwpexit,	nomod_zero);
	NO_UNLOAD_STUB(procfs, prinvalidate,	nomod_zero);
	NO_UNLOAD_STUB(procfs, prnsegs,     	nomod_zero);
	NO_UNLOAD_STUB(procfs, prgetstatus, 	nomod_zero);
	NO_UNLOAD_STUB(procfs, prgetpsinfo, 	nomod_zero);
	NO_UNLOAD_STUB(procfs, prnotify,	nomod_zero);
	NO_UNLOAD_STUB(procfs, prexecstart,	nomod_zero);
	NO_UNLOAD_STUB(procfs, prexecend,	nomod_zero);
	NO_UNLOAD_STUB(procfs, prbarrier,	nomod_zero);
	END_MODULE(procfs);
#endif

/*
 * Stubs for fifofs
 */
#ifndef FIFO_MODULE
	MODULE(fifofs,fs);
	STUB(fifofs, fifovp,      	0);
	STUB(fifofs, fifo_getinfo,	0);
	STUB(fifofs, fifo_vfastoff,	0);
	END_MODULE(fifofs);
#endif

/*
 * Stubs for ufs
 *
 * This is needed to support the old quotactl system call.
 * When the old sysent stuff goes away, this will need to be revisited.
 */
#ifndef UFS_MODULE
	MODULE(ufs,fs);
	STUB(ufs, quotactl, nomod_minus_one);
	END_MODULE(ufs);
#endif

/*
 * Stubs for namefs
 */
#ifndef NAMEFS_MODULE
	MODULE(namefs,fs);
	STUB(namefs, nm_unmountall, 	0);
	END_MODULE(namefs);
#endif

/*
 * Stubs for ts_dptbl
 */
#ifndef TS_DPTBL_MODULE
	MODULE(TS_DPTBL,sched);
	STUB(TS_DPTBL, ts_getdptbl,		0);
	STUB(TS_DPTBL, ts_getkmdpris,		0);
	STUB(TS_DPTBL, ts_getmaxumdpri,	0);
	END_MODULE(TS_DPTBL);
#endif

/*
 * Stubs for rt_dptbl
 */
#ifndef RT_DPTBL_MODULE
	MODULE(RT_DPTBL,sched);
	STUB(RT_DPTBL, rt_getdptbl,		0);
	END_MODULE(RT_DPTBL);
#endif

/*
 * Stubs for ia_dptbl
 */
#ifndef IA_DPTBL_MODULE
	MODULE(IA_DPTBL,sched);
	STUB(IA_DPTBL, ia_getdptbl,		0);
	STUB(IA_DPTBL, ia_getkmdpris,		0);
	STUB(IA_DPTBL, ia_getmaxumdpri,	0);
	END_MODULE(IA_DPTBL);
#endif

/*
 * Stubs for kb (only needed for 'win')
 */
#ifndef KB_MODULE
	MODULE(kb,strmod);
	STUB(kb, strsetwithdecimal,	0);
	END_MODULE(kb);
#endif

/*
 * Stubs for swapgeneric
 */
#ifndef SWAPGENERIC_MODULE
	MODULE(swapgeneric,misc);
	STUB(swapgeneric, rootconf,     0);
	STUB(swapgeneric, getfstype,    0);
	STUB(swapgeneric, dumpinit,     0);
	STUB(swapgeneric, getswapdev,   0);
	STUB(swapgeneric, getrootdev,   0);
	STUB(swapgeneric, getfsname,    0);
	STUB(swapgeneric, loadrootmodules, 0);
	STUB(swapgeneric, getlastfrompath, 0);
	END_MODULE(swapgeneric);
#endif

/*
 * stubs for strplumb...
 */
#ifndef STRPLUMB_MODULE
	MODULE(strplumb,misc);
	STUB(strplumb, strplumb,     0);
	STUB(strplumb, strplumb_get_driver_list, 0);
	END_MODULE(strplump);
#endif

/*
 * Stubs for console configuration module
 */
#ifndef CONSCONFIG_MODULE
	MODULE(consconfig,misc);
	STUB(consconfig, consconfig,     0);
	END_MODULE(consconfig);
#endif

/*
 * Stubs for zs (uart) module
 */
#ifndef ZS_MODULE
	MODULE(zs,drv);
	STUB(zs, zsgetspeed,		0);
	END_MODULE(zs);
#endif

/* 
 * Stubs for accounting.
 */
#ifndef SYSACCT_MODULE
	MODULE(sysacct,sys);
	WSTUB(sysacct, acct,  		nomod_zero);
	END_MODULE(sysacct);
#endif

/*
 * Stubs for semaphore routines. sem.c
 */
#ifndef SEMSYS_MODULE
	MODULE(semsys,sys);
	WSTUB(semsys, semexit,		nomod_zero);
	END_MODULE(semsys);
#endif

/*
 * Stubs for shmem routines. shm.c
 */
#ifndef SHMSYS_MODULE
	MODULE(shmsys,sys);
	WSTUB(shmsys, shmexit,		nomod_zero);
	WSTUB(shmsys, shmfork,		nomod_zero);
	END_MODULE(shmsys);
#endif

/*
 * Stubs for timod
 */
#ifndef TIMOD_MODULE
	MODULE(timod,strmod);
	STUB(timod, ti_doname,		nomod_minus_one);
	END_MODULE(timod)
#endif

/*
 * Stubs for doors
 */
#ifndef DOORFS_MODULE
	MODULE(doorfs,sys);
	WSTUB(doorfs, door_slam,	nomod_zero);
	END_MODULE(doorfs);
#endif

/*
 * Stubs for the remote kernel debugger.
 */
#ifndef DSTUB4C_MODULE
	MODULE(dstub4c,misc);
	STUB(dstub4c,  _db_install,	nomod_zero);
	WSTUB(dstub4c, _db_poll,	nomod_zero);
	WSTUB(dstub4c, _db_scan_pkt,	nomod_zero);
	END_MODULE(dstub4c);
#endif

/*
 * Stubs for dma routines. dmaga.c
 * (These are only needed for cross-checks, not autoloading)
 */
#ifndef DMA_MODULE
	MODULE(dma,drv);
	WSTUB(dma, dma_alloc,		nomod_zero); /* (DMAGA *)0 */
	WSTUB(dma, dma_free,		nomod_zero); /* (DMAGA *)0 */
	END_MODULE(dma);
#endif

/*
 * Stubs for auditing.
 */
#ifndef C2AUDIT_MODULE
	MODULE(c2audit,sys);
	STUB(c2audit,  audit_init,		nomod_zero);
	STUB(c2audit,  _auditsys,		nomod_zero);
	WSTUB(c2audit, audit_free,		nomod_zero);
	WSTUB(c2audit, audit_start, 		nomod_zero);
	WSTUB(c2audit, audit_finish,		nomod_zero);
	WSTUB(c2audit, audit_suser,		nomod_zero);
	WSTUB(c2audit, audit_newproc,		nomod_zero);
	WSTUB(c2audit, audit_pfree,		nomod_zero);
	WSTUB(c2audit, audit_thread_free,	nomod_zero);
	WSTUB(c2audit, audit_thread_create,	nomod_zero);
	WSTUB(c2audit, audit_falloc,		nomod_zero);
	WSTUB(c2audit, audit_unfalloc,		nomod_zero);
	WSTUB(c2audit, audit_closef,		nomod_zero);
	WSTUB(c2audit, audit_copen,		nomod_zero);
	WSTUB(c2audit, audit_core_start,	nomod_zero);
	WSTUB(c2audit, audit_core_finish,	nomod_zero);
	WSTUB(c2audit, audit_stropen,		nomod_zero);
	WSTUB(c2audit, audit_strclose,		nomod_zero);
	WSTUB(c2audit, audit_strioctl,		nomod_zero);
	WSTUB(c2audit, audit_strputmsg,		nomod_zero);
	WSTUB(c2audit, audit_c2_revoke,		nomod_zero);
	WSTUB(c2audit, audit_savepath,		nomod_zero);
	WSTUB(c2audit, audit_anchorpath,	nomod_zero);
	WSTUB(c2audit, audit_addcomponent,	nomod_zero);
	WSTUB(c2audit, audit_exit,		nomod_zero);
	WSTUB(c2audit, audit_exec,		nomod_zero);
	WSTUB(c2audit, audit_symlink,		nomod_zero);
	WSTUB(c2audit, audit_vncreate_start,	nomod_zero);
	WSTUB(c2audit, audit_vncreate_finish,	nomod_zero);
	WSTUB(c2audit, audit_enterprom,		nomod_zero);
	WSTUB(c2audit, audit_exitprom,		nomod_zero);
	WSTUB(c2audit, audit_chdirec,		nomod_zero);
	WSTUB(c2audit, audit_getf,		nomod_zero);
	WSTUB(c2audit, audit_setf,		nomod_zero);
	WSTUB(c2audit, audit_sock,		nomod_zero);
	WSTUB(c2audit, audit_strgetmsg,		nomod_zero);
	END_MODULE(c2audit);
#endif

#ifndef SAD_MODULE
	MODULE(sad,drv);
	STUB(sad, sadinit, 0);
	STUB(sad, ap_free, 0);
	END_MODULE(sad);
#endif

#ifndef WC_MODULE
	MODULE(wc,drv);
	STUB(wc, wcvnget, 0);
	STUB(wc, wcvnrele, 0);
	END_MODULE(wc);
#endif

#ifndef IWSCN_MODULE
	MODULE(iwscn,drv);
	STUB(iwscn, srredirecteevp_lookup, 0);
	STUB(iwscn, srredirecteevp_rele, 0);
	STUB(iwscn, srpop, 0);
	END_MODULE(iwscn);
#endif

/*
 * Stubs for checkpoint-resume module
 */
#ifndef CPR_MODULE
        MODULE(cpr,misc);
        STUB(cpr, cpr, 0);
        END_MODULE(cpr);
#endif

/*
 * Stubs for kernel probes (tnf module).  Not unloadable.
 */
#ifndef TNF_MODULE
	MODULE(tnf,drv);
	NO_UNLOAD_STUB(tnf, tnf_ref32_1,	nomod_zero);
	NO_UNLOAD_STUB(tnf, tnf_string_1,	nomod_zero);
	NO_UNLOAD_STUB(tnf, tnf_opaque_array_1,	nomod_zero);
	NO_UNLOAD_STUB(tnf, tnf_struct_tag_1,	nomod_zero);
	NO_UNLOAD_STUB(tnf, tnf_allocate,	nomod_zero);
	END_MODULE(tnf);
#endif

! this is just a marker for the area of text that contains stubs 
	.seg ".text"
	.global stubs_end
stubs_end:
	nop

#endif	/* lint */
