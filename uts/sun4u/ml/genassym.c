/*
 * Copyright (c) 1991, 1993, 1995 by Sun Microsystems, Inc.
 * All Rights Reserved.
 */

#pragma ident	"@(#)genassym.c	1.47	95/08/18 SMI"

#ifndef	_GENASSYM
#define	_GENASSYM
#endif

#define	SIZES	1

#include <sys/types.h>
#include <sys/param.h>
#include <sys/elf_notes.h>
#include <sys/sysinfo.h>
#include <sys/vmmeter.h>
#include <sys/vmparam.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/map.h>
#include <sys/thread.h>
#include <sys/t_lock.h>		/* XXX Payne */
#include <sys/mutex_impl.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/rt.h>
#include <sys/ts.h>
#include <sys/msgbuf.h>
#include <sys/vmmac.h>
#include <sys/obpdefs.h>
#include <sys/cpuvar.h>
#include <sys/dditypes.h>	/* XXX Payne */
#include <sys/vtrace.h>

#include <sys/pte.h>
#include <sys/privregs.h>
#include <sys/machpcb.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/scb.h>
#include <sys/clock.h>
#include <sys/intr.h>
#include <sys/ivintr.h>
#include <sys/eeprom.h>

#include <sys/devops.h>
#include <sys/ddi_impldefs.h>
#include <sys/sysiosbus.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/hat_sfmmu.h>

#include <sys/avintr.h>

#include <sys/fdvar.h>		/* XXX Payne */

#include <sys/stream.h>
#include <sys/strsubr.h>

#include <sys/file.h>

#include <sys/psr_compat.h>

#define	OFFSET(type, field)	((int)(&((type *)0)->field))


main()
{
	register struct dev_info *devi = (struct dev_info *)0;
	register struct dev_ops *ops = (struct dev_ops *)0;
	register struct bus_ops *busops = (struct bus_ops *)0;
	register struct proc *p = (struct proc *)0;
	register kthread_id_t tid = (kthread_id_t)0;
	register klwp_id_t lwpid = (klwp_id_t)0;
	register struct user *up = (struct user *)0;
	register struct regs *rp = (struct regs *)0;
	register struct as *as = (struct as *)0;
	register struct hat *hat = (struct hat *)0;
	register struct sfmmu *sfmmu = (struct sfmmu *)0;
	register struct hmehash_bucket *hmebp = (struct hmehash_bucket *)0;
	register struct vmhatstat *hatstat = (struct vmhatstat *)0;
	register struct ctx *ctx = (struct ctx *)0;
	register ism_map_blk_t *ismblk = (ism_map_blk_t *)0;
	register ism_map_t *ism_map = (ism_map_t *)0;
	register tte_t *ttep = (tte_t *)0;
	register struct tsbe *tsbe = (struct tsbe *)0;
	register union tsb_tag *tsbtag = (union tsb_tag *)0;
	register struct hme_blk *hmeblkp = (struct hme_blk *)0;
	register struct sf_hment *sfhmep = (struct sf_hment *)0;
	register label_t *l = 0;
	register struct clk_info *clkp = (struct clk_info *)0;
	register struct v9_fpu *fp = (struct v9_fpu *)0;
	struct flushmeter *fm = (struct flushmeter *)0;
	register struct autovec *av = (struct autovec *)0;
	register struct timestruc *tms = (struct timestruc *)0;
	register struct sbus_soft_state *sbus = (struct sbus_soft_state *)0;
	register struct machpcb *mpcb = (struct machpcb *)0;

	printf("#define\tP_LINK 0x%x\n", &p->p_link);
	printf("#define\tP_NEXT 0x%x\n", &p->p_next);
	printf("#define\tP_CHILD 0x%x\n", &p->p_child);
	printf("#define\tP_SIBLING 0x%x\n", &p->p_sibling);
	printf("#define\tP_SIG 0x%x\n", &p->p_sig);
	printf("#define\tP_WCODE 0x%x\n", &p->p_wcode);
	printf("#define\tP_FLAG 0x%x\n", &p->p_flag);
	printf("#define\tP_TLIST 0x%x\n", &p->p_tlist);
	printf("#define\tP_AS 0x%x\n", &p->p_as);
	printf("#define\tP_FLAG 0x%x\n", &p->p_flag);
	printf("#define\tP_LOCK 0x%x\n", &p->p_lock);
	printf("#define\tP_PIDP 0x%x\n", &p->p_pidp);
	printf("#define\tPID_PIDID 0x%x\n",
		OFFSET(struct pid, pid_id));
	printf("#define\tPROCSIZE 0x%x\n", sizeof (struct proc));
	printf("#define\tSLOAD 0x%x\n", SLOAD);
	printf("#define\tSSLEEP 0x%x\n", SSLEEP);
	printf("#define\tSRUN 0x%x\n", SRUN);
	printf("#define\tSONPROC 0x%x\n", SONPROC);

	printf("#define\tT_LOCK 0x%x\n", &tid->t_lock);
	printf("#define\tT_LOCKP 0x%x\n", &tid->t_lockp);
	printf("#define\tT_OLDSPL 0x%x\n", &tid->t_oldspl);
	printf("#define\tT_PRI 0x%x\n", &tid->t_pri);
	printf("#define\tT_LWP 0x%x\n", &tid->t_lwp);
	printf("#define\tT_PROCP 0x%x\n", &tid->t_procp);
	printf("#define\tT_LINK 0x%x\n", &tid->t_link);
	printf("#define\tT_STATE 0x%x\n", &tid->t_state);
	printf("#define\tT_MSTATE 0x%x\n", &tid->t_mstate);
	printf("#define\tT_STACK 0x%x\n", &tid->t_stk);
	printf("#define\tT_SWAP 0x%x\n", &tid->t_swap);
	printf("#define\tT_WCHAN 0x%x\n", &tid->t_wchan);
	printf("#define\tT_FLAGS 0x%x\n", &tid->t_flag);
	printf("#define\tT_CTX 0x%x\n", &tid->t_ctx);
	printf("#define\tT_PC 0x%x\n", &tid->t_pcb.val[0]);
	printf("#define\tT_SP 0x%x\n", &tid->t_pcb.val[1]);
	printf("#define\tT_LOFAULT 0x%x\n", &tid->t_lofault);
	printf("#define\tT_ONFAULT 0x%x\n", &tid->t_onfault);
	printf("#define\tT_CPU 0x%x\n", &tid->t_cpu);
	printf("#define\tT_BOUND_CPU 0x%x\n", &tid->t_bound_cpu);
	printf("#define\tT_INTR 0x%x\n", &tid->t_intr);
	printf("#define\tT_FORW 0x%x\n", &tid->t_forw);
	printf("#define\tT_BACK 0x%x\n", &tid->t_back);
	printf("#define\tT_SIG 0x%x\n", &tid->t_sig);
	printf("#define\tT_TID 0x%x\n", &tid->t_tid);
	printf("#define\tT_POST_SYS 0x%x\n", &tid->t_post_sys);
	printf("#define\tT_POST_SYS_AST 0x%x\n", &tid->t_post_sys_ast);
	printf("#define\tT_PRE_SYS 0x%x\n", &tid->t_pre_sys);
	printf("#define\tT_PREEMPT 0x%x\n", &tid->t_preempt);
	printf("#define\tT_PROC_FLAG 0x%x\n", &tid->t_proc_flag);
	printf("#define\tT_STARTPC 0x%x\n", &tid->t_startpc);
	printf("#define\tT_SYSNUM 0x%x\n", &tid->t_sysnum);
	printf("#define\tT_ASTFLAG 0x%x\n", &tid->t_astflag);
	printf("#define\tT_INTR_THREAD %d\n", T_INTR_THREAD);
	printf("#define\tTP_MSACCT 0x%x\n", TP_MSACCT);
	printf("#define\tFREE_THREAD 0x%x\n", TS_FREE);
	printf("#define\tTS_FREE 0x%x\n", TS_FREE);
	printf("#define\tTS_ZOMB 0x%x\n", TS_ZOMB);
	printf("#define\tONPROC_THREAD 0x%x\n", TS_ONPROC);
	printf("#define\tT0STKSZ 0x%x\n", DEFAULTSTKSZ);
	printf("#define\tTHREAD_SIZE %d\n", sizeof (kthread_t));
	printf("#define\tNSYSCALL %d\n", NSYSCALL);
	printf("#define\tSYSENT_SIZE %d\n", sizeof (struct sysent));
	printf("#define\tSY_CALLC 0x%x\n", OFFSET(struct sysent, sy_callc));

	printf("#define\tA_HAT 0x%x\n", &as->a_hat);
	printf("#define\tHAT_SFMMU 0x%x\n", &hat->hat_data[0]);
	printf("#define\tSFMMU_ISMBLK 0x%x\n", &sfmmu->sfmmu_ismblk);
	printf("#define\tSFMMU_CNUM 0x%x\n", &sfmmu->sfmmu_cnum);
	printf("#define\tSFMMU_CPUSRAN 0x%x\n", &sfmmu->sfmmu_cpusran);

	printf("#define\tHMEBUCK_MUTEX 0x%x\n", &hmebp->hmehash_mutex);
	printf("#define\tHMEBUCK_HBLK 0x%x\n", &hmebp->hmeblkp);
	printf("#define\tHMEBUCK_NEXTPA 0x%x\n", &hmebp->hmeh_nextpa);
	printf("#define\tHMEBUCK_SIZE %d\n", sizeof (struct hmehash_bucket));

	printf("#define\tHATSTAT_ITLBMISS 0x%x\n",
		OFFSET(struct vmhatstat, vh_itlb_misses.value.ul));
	printf("#define\tHATSTAT_DTLBMISS 0x%x\n",
		OFFSET(struct vmhatstat, vh_dtlb_misses.value.ul));
	printf("#define\tHATSTAT_TSBHIT 0x%x\n",
		OFFSET(struct vmhatstat, vh_tsb_hits.value.ul));
	printf("#define\tHATSTAT_UTSBMISS 0x%x\n",
		OFFSET(struct vmhatstat, vh_utsb_misses.value.ul));
	printf("#define\tHATSTAT_KTSBMISS 0x%x\n",
		OFFSET(struct vmhatstat, vh_ktsb_misses.value.ul));
	printf("#define\tHATSTAT_UMODFAULTS 0x%x\n",
		OFFSET(struct vmhatstat, vh_umod_faults.value.ul));
	printf("#define\tHATSTAT_KMODFAULTS 0x%x\n",
		OFFSET(struct vmhatstat, vh_kmod_faults.value.ul));
	printf("#define\tHATSTAT_PAGEFAULT 0x%x\n",
		OFFSET(struct vmhatstat, vh_pagefaults.value.ul));
	printf("#define\tHATSTAT_UHASH_SEARCH 0x%x\n",
		OFFSET(struct vmhatstat, vh_uhash_searches.value.ul));
	printf("#define\tHATSTAT_UHASH_LINKS 0x%x\n",
		OFFSET(struct vmhatstat, vh_uhash_links.value.ul));
	printf("#define\tHATSTAT_KHASH_SEARCH 0x%x\n",
		OFFSET(struct vmhatstat, vh_khash_searches.value.ul));
	printf("#define\tHATSTAT_KHASH_LINKS 0x%x\n",
		OFFSET(struct vmhatstat, vh_khash_links.value.ul));

	printf("#define\tC_SFMMU 0x%x\n", &ctx->c_sfmmu);
	printf("#define\tC_REFCNT 0x%x\n", &ctx->c_refcnt);
	printf("#define\tC_FLAGS 0x%x\n", &ctx->c_flags);
	printf("#define\tC_SIZE 0x%x\n", sizeof (struct ctx));
	printf("#define\tC_ISMBLKPA 0x%x\n", &ctx->c_ismblkpa);

	printf("#define\tISMBLK_MAPS 0x%x\n", &ismblk->maps);
	printf("#define\tISMBLK_NEXT 0x%x\n", &ismblk->next);

	printf("#define\tISMMAP_VBASE 0x%x\n", &ism_map->vbase);
	printf("#define\tISM_MAP_SZ 0x%x\n",  sizeof (ism_map_t));

	printf("#define\tCACHE_PAC 0x%x\n", CACHE_PAC);
	printf("#define\tCACHE_VAC 0x%x\n", CACHE_VAC);
	printf("#define\tCACHE_WRITEBACK 0x%x\n", CACHE_WRITEBACK);

	printf("#define\tTSBE_TAG 0x%x\n", &tsbe->tte_tag);
	printf("#define\tTSBE_TTE 0x%x\n", &tsbe->tte_data);
	printf("#define\tTSBTAG_INTHI 0x%x\n", &tsbtag->tag_inthi);
	printf("#define\tTSBTAG_INTLO 0x%x\n", &tsbtag->tag_intlo);

	printf("#define\tHMEBLK_NEXT 0x%x\n", &hmeblkp->hblk_next);
	printf("#define\tHMEBLK_TAG 0x%x\n", &hmeblkp->hblk_tag);
	printf("#define\tHMEBLK_MISC 0x%x\n", &hmeblkp->hblk_misc);
	printf("#define\tHMEBLK_HME1 0x%x\n", &hmeblkp->hblk_hme);
	printf("#define\tHMEBLK_NEXTPA 0x%x\n", &hmeblkp->hblk_nextpa);

	printf("#define\tSFHME_TTE 0x%x\n", &sfhmep->hme_tte);
	printf("#define\tSFHME_SIZE 0x%x\n", sizeof (struct sf_hment));

	printf("#define\tMSGBUFSIZE 0x%x\n", sizeof (struct msgbuf));

	printf("#define\tS_READ 0x%x\n", (int)S_READ);
	printf("#define\tS_WRITE 0x%x\n", (int)S_WRITE);
	printf("#define\tS_EXEC 0x%x\n", (int)S_EXEC);
	printf("#define\tS_OTHER 0x%x\n", (int)S_OTHER);

	printf("#define\tL_PC 0x%x\n", &l->val[0]);
	printf("#define\tL_SP 0x%x\n", &l->val[1]);

	printf("#define\tU_COMM 0x%x\n", up->u_comm);
	printf("#define\tU_SIGNAL 0x%x\n", up->u_signal);
	printf("#define\tUSIZEBYTES 0x%x\n", sizeof (struct user));

	printf("#define\tLWP_THREAD 0x%x\n", &lwpid->lwp_thread);
	printf("#define\tLWP_REGS 0x%x\n", &lwpid->lwp_regs);
	printf("#define\tLWP_FPU 0x%x\n", &lwpid->lwp_fpu);
	printf("#define\tLWP_ARG 0x%x\n", &lwpid->lwp_arg);
	printf("#define\tLWP_CURSIG 0x%x\n", &lwpid->lwp_cursig);
	printf("#define\tLWP_RU_SYSC 0x%x\n", &lwpid->lwp_ru.sysc);
	printf("#define\tLWP_STATE 0x%x\n", &lwpid->lwp_state);
	printf("#define\tLWP_STIME 0x%x\n", &lwpid->lwp_stime);
	printf("#define\tLWP_USER 0x%x\n", LWP_USER);
	printf("#define\tLWP_UTIME 0x%x\n", &lwpid->lwp_utime);
	printf("#define\tLWP_SYS 0x%x\n", LWP_SYS);
	printf("#define\tLWP_STATE_START 0x%x\n",
				&lwpid->lwp_mstate.ms_state_start);
	printf("#define\tLWP_ACCT_USER 0x%x\n",
				&lwpid->lwp_mstate.ms_acct[LMS_USER]);
	printf("#define\tLWP_ACCT_SYSTEM 0x%x\n",
				&lwpid->lwp_mstate.ms_acct[LMS_SYSTEM]);
	printf("#define\tLWP_MS_PREV 0x%x\n", &lwpid->lwp_mstate.ms_prev);
	printf("#define\tLWP_MS_START 0x%x\n", &lwpid->lwp_mstate.ms_start);

	printf("#define\tLWP_PCB 0x%x\n", &lwpid->lwp_pcb);
	printf("#define\tPCB_FLAGS 0x%x\n", &lwpid->lwp_pcb.pcb_flags);
	printf("#define\tPCB_TRAP0 0x%x\n", &lwpid->lwp_pcb.pcb_trap0addr);

	printf("#define\tMPCBSIZE 0x%x\n", sizeof (struct machpcb));
	printf("#define\tMPCB_REGS 0x%x\n", &mpcb->mpcb_regs);
	printf("#define\tMPCB_WBUF 0x%x\n", mpcb->mpcb_wbuf);
	printf("#define\tMPCB_SPBUF 0x%x\n", mpcb->mpcb_spbuf);
	printf("#define\tMPCB_WBCNT 0x%x\n", &mpcb->mpcb_wbcnt);
	printf("#define\tMPCB_FLAGS 0x%x\n", &mpcb->mpcb_flags);
	printf("#define\tMPCB_THREAD 0x%x\n", &mpcb->mpcb_thread);

	printf("#define\tV9FPUSIZE 0x%x\n", sizeof (struct v9_fpu));
	printf("#define\tFPU_REGS 0x%x\n", &fp->fpu_fr.fpu_regs[0]);
	printf("#define\tFPU_FSR 0x%x\n", &fp->fpu_fsr);
	printf("#define\tFPU_FPRS 0x%x\n", &fp->fpu_fprs);
	printf("#define\tFPU_Q 0x%x\n", &fp->fpu_q);
	printf("#define\tFPU_QCNT 0x%x\n", &fp->fpu_qcnt);
	printf("#define\tFPU_GSR 0x%x\n", sizeof (struct v9_fpu));

	printf("#define\tPSR_PIL_BIT %d\n", bit(PSR_PIL));
	printf("#define\tREGSIZE %d\n", sizeof (struct regs));

	/*
	 * Originally from reg.h; relocated here to support v7/v9.
	 * Location of the users' stored registers relative to R0.
	 * Used as an index into a gregset_t array.
	 * v9 numbers increment by 2 for 64b regs.
	 */
	printf("#define\tTSTATE\t(%d)\n", ((long)(&rp->r_tstate))/4);
	printf("#define\tG1\t(%d)\n", ((long)(&rp->r_g1))/4);
	printf("#define\tG2\t(%d)\n", ((long)(&rp->r_g2))/4);
	printf("#define\tG3\t(%d)\n", ((long)(&rp->r_g3))/4);
	printf("#define\tG4\t(%d)\n", ((long)(&rp->r_g4))/4);
	printf("#define\tG5\t(%d)\n", ((long)(&rp->r_g5))/4);
	printf("#define\tG6\t(%d)\n", ((long)(&rp->r_g6))/4);
	printf("#define\tG7\t(%d)\n", ((long)(&rp->r_g7))/4);
	printf("#define\tO0\t(%d)\n", ((long)(&rp->r_o0))/4);
	printf("#define\tO1\t(%d)\n", ((long)(&rp->r_o1))/4);
	printf("#define\tO2\t(%d)\n", ((long)(&rp->r_o2))/4);
	printf("#define\tO3\t(%d)\n", ((long)(&rp->r_o3))/4);
	printf("#define\tO4\t(%d)\n", ((long)(&rp->r_o4))/4);
	printf("#define\tO5\t(%d)\n", ((long)(&rp->r_o5))/4);
	printf("#define\tO6\t(%d)\n", ((long)(&rp->r_o6))/4);
	printf("#define\tO7\t(%d)\n", ((long)(&rp->r_o7))/4);
	printf("#define\tPC\t(%d)\n", ((long)(&rp->r_pc))/4);
	printf("#define\tnPC\t(%d)\n", ((long)(&rp->r_npc))/4);
	printf("#define\tY\t(%d)\n", ((long)(&rp->r_y))/4);

	/*
	 * The following defines are for portability.
	 */
	printf("#define\tSP\tO6\n");

	printf("#define\tELF_NOTE_SOLARIS \"%s\"\n", ELF_NOTE_SOLARIS);
	printf("#define\tELF_NOTE_PAGESIZE_HINT %d\n", ELF_NOTE_PAGESIZE_HINT);

	printf("#define\tFM_CTX 0x%x\n", &fm->f_ctx);
	printf("#define\tFM_USR 0x%x\n", &fm->f_usr);
	printf("#define\tFM_REGION 0x%x\n", &fm->f_region);
	printf("#define\tFM_SEGMENT 0x%x\n", &fm->f_segment);
	printf("#define\tFM_PAGE 0x%x\n", &fm->f_page);
	printf("#define\tFM_PARTIAL 0x%x\n", &fm->f_partial);

	printf("#define\tCLK_COUNT 0x%x\n", &clkp->clk_count_addr);
	printf("#define\tCLK_LIMIT 0x%x\n", &clkp->clk_limit_addr);

	printf("#define\tCLK_CLRINTR 0x%x\n", &clkp->clk_clrintr_addr);
	printf("#define\tCLK_COUNT 0x%x\n", &clkp->clk_count_addr);
	printf("#define\tCLK_LIMIT 0x%x\n", &clkp->clk_limit_addr);

	printf("#define\tAV_VECTOR 0x%x\n", &av->av_vector);
	printf("#define\tAV_MUTEX 0x%x\n", &av->av_mutex);
	printf("#define\tAV_INTARG 0x%x\n", &av->av_intarg);
	printf("#define\tAV_INT_SPURIOUS %d\n", AV_INT_SPURIOUS);
	printf("#define\tAUTOVECSIZE 0x%x\n", sizeof (struct autovec));

	printf("#define\tCPU_ID 0x%x\n", OFFSET(struct cpu, cpu_id));
	printf("#define\tCPU_ENABLE %d\n", CPU_ENABLE);
	printf("#define\tCPU_FLAGS 0x%x\n", OFFSET(struct cpu, cpu_flags));
	printf("#define\tCPU_READY %d\n", CPU_READY);
	printf("#define\tCPU_QUIESCED %d\n", CPU_QUIESCED);
	printf("#define\tCPU_THREAD 0x%x\n", OFFSET(struct cpu, cpu_thread));
	printf("#define\tCPU_THREAD_LOCK 0x%x\n",
		OFFSET(struct cpu, cpu_thread_lock));
	printf("#define\tCPU_KPRUNRUN 0x%x\n",
		OFFSET(struct cpu, cpu_kprunrun));
	printf("#define\tCPU_LWP 0x%x\n", OFFSET(struct cpu, cpu_lwp));
	printf("#define\tCPU_FPOWNER 0x%x\n", OFFSET(struct cpu, cpu_fpowner));
	printf("#define\tCPU_IDLE_THREAD 0x%x\n",
		OFFSET(struct cpu, cpu_idle_thread));
	printf("#define\tCPU_INTR_THREAD 0x%x\n",
		OFFSET(struct cpu, cpu_intr_thread));
	printf("#define\tCPU_INTR_ACTV 0x%x\n",
		OFFSET(struct cpu, cpu_intr_actv));
	printf("#define\tCPU_BASE_SPL 0x%x\n",
		OFFSET(struct cpu, cpu_base_spl));
	printf("#define\tCPU_ON_INTR 0x%x\n", OFFSET(struct cpu, cpu_on_intr));
	printf("#define\tCPU_INTR_STACK 0x%x\n",
		OFFSET(struct cpu, cpu_intr_stack));
	printf("#define\tCPU_STATS 0x%x\n", OFFSET(struct cpu, cpu_stat));
	printf("#define\tCPU_SYSINFO_INTR 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.intr));
	printf("#define\tCPU_SYSINFO_INTRTHREAD 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.intrthread));
	printf("#define\tCPU_SYSINFO_INTRBLK 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.intrblk));
	printf("#define\tCPU_SYSINFO_CPUMIGRATE 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.cpumigrate));
	printf("#define\tCPU_SYSINFO_SYSCALL 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.syscall));
	printf("#define\tCPU_SYSINFO_UO_CNT 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.win_uo_cnt));
	printf("#define\tCPU_SYSINFO_UU_CNT 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.win_uu_cnt));
	printf("#define\tCPU_SYSINFO_SO_CNT 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.win_so_cnt));
	printf("#define\tCPU_SYSINFO_SU_CNT 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.win_su_cnt));
	printf("#define\tCPU_SYSINFO_SUO_CNT 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.win_suo_cnt));
	printf("#define\tCPU_KPRUNRUN 0x%x\n",
		OFFSET(struct cpu, cpu_kprunrun));

	printf("#define\tCPU_TRACE 0x%x\n",
		OFFSET(struct cpu, cpu_trace));
	printf("#define\tCPU_TRACE_START 0x%x\n",
		OFFSET(struct cpu, cpu_trace.tbuf_start));
	printf("#define\tCPU_TRACE_END 0x%x\n",
		OFFSET(struct cpu, cpu_trace.tbuf_end));
	printf("#define\tCPU_TRACE_WRAP 0x%x\n",
		OFFSET(struct cpu, cpu_trace.tbuf_wrap));
	printf("#define\tCPU_TRACE_HEAD 0x%x\n",
		OFFSET(struct cpu, cpu_trace.tbuf_head));
	printf("#define\tCPU_TRACE_TAIL 0x%x\n",
		OFFSET(struct cpu, cpu_trace.tbuf_tail));
	printf("#define\tCPU_TRACE_REDZONE 0x%x\n",
		OFFSET(struct cpu, cpu_trace.tbuf_redzone));
	printf("#define\tCPU_TRACE_OVERFLOW 0x%x\n",
		OFFSET(struct cpu, cpu_trace.tbuf_overflow));
	printf("#define\tCPU_TRACE_REAL_MAP 0x%x\n",
		OFFSET(struct cpu, cpu_trace.real_event_map));
	printf("#define\tCPU_TRACE_EVENT_MAP 0x%x\n",
		OFFSET(struct cpu, cpu_trace.event_map));
	printf("#define\tCPU_TRACE_HRTIME 0x%x\n",
		OFFSET(struct cpu, cpu_trace.last_hrtime_lo32));
	printf("#define\tCPU_TRACE_THREAD 0x%x\n",
		OFFSET(struct cpu, cpu_trace.last_thread));
	printf("#define\tCPU_TRACE_SCRATCH 0x%x\n",
		OFFSET(struct cpu, cpu_trace.scratch[0]));

	printf("#define\tTRACE_KTID_HEAD 0x%x\n",
		OFFSET(struct vt_raw_kthread_id, head));
	printf("#define\tTRACE_KTID_TID 0x%x\n",
		OFFSET(struct vt_raw_kthread_id, tid));
	printf("#define\tTRACE_KTID_SIZE 0x%x\n",
		sizeof (struct vt_raw_kthread_id));

	printf("#define\tTRACE_ETIME_HEAD 0x%x\n",
		OFFSET(struct vt_elapsed_time, head));
	printf("#define\tTRACE_ETIME_TIME 0x%x\n",
		OFFSET(struct vt_elapsed_time, time));
	printf("#define\tTRACE_ETIME_SIZE 0x%x\n",
		sizeof (struct vt_elapsed_time));

	printf("#define\tINTR_VECTOR 0x%x\n",
		sizeof (struct intr_vector));

	printf("#define\tCPU_MPCB 0x%x\n", OFFSET(struct cpu, cpu_m.mpcb));
	printf("#define\tCPU_TMP1	0x%x\n",
		OFFSET(struct cpu, cpu_m.tmp1));
	printf("#define\tCPU_TMP2	0x%x\n",
		OFFSET(struct cpu, cpu_m.tmp2));
	printf("#define\tINTR_POOL 0x%x\n",
		OFFSET(struct cpu, cpu_m.intr_pool[0]));
	printf("#define\tINTR_HEAD 0x%x\n",
		OFFSET(struct cpu, cpu_m.intr_head[0]));
	printf("#define\tINTR_TAIL 0x%x\n",
		OFFSET(struct cpu, cpu_m.intr_tail[0]));

	printf("#define\tTICKINT_INTRVL 0x%x\n",
		OFFSET(struct cpu, cpu_m.tickint_intrvl));

	printf("#define\tTICK_HAPPENED 0x%x\n",
		OFFSET(struct cpu, cpu_m.tick_happened));

	printf("#define\tCPU_PROFILING  0x%x\n",
		OFFSET(struct cpu, cpu_profiling));
	printf("#define\tPROFILING 0x%x\n",
		OFFSET(struct kern_profiling, profiling));
	printf("#define\tPROF_LOCK 0x%x\n",
		OFFSET(struct kern_profiling, profiling_lock));
	printf("#define\tPROF_RP 0x%x\n",
		OFFSET(struct kern_profiling, rp));
	printf("#define\tPROF_FROMS 0x%x\n",
		OFFSET(struct kern_profiling, froms));
	printf("#define\tPROF_TOS 0x%x\n",
		OFFSET(struct kern_profiling, tos));
	printf("#define\tPROF_FROMSSIZE 0x%x\n",
		OFFSET(struct kern_profiling, fromssize));
	printf("#define\tPROF_TOSSIZE 0x%x\n",
		OFFSET(struct kern_profiling, tossize));
	printf("#define\tPROF_TOSNEXT 0x%x\n",
		OFFSET(struct kern_profiling, tosnext));
	printf("#define\tKPC_LINK 0x%x\n",
		OFFSET(struct kp_call, link));
	printf("#define\tKPC_FROM 0x%x\n",
		OFFSET(struct kp_call, frompc));
	printf("#define\tKPC_TO 0x%x\n",
		OFFSET(struct kp_call, topc));
	printf("#define\tKPC_COUNT 0x%x\n",
		OFFSET(struct kp_call, count));
	printf("#define\tKPCSIZE 0x%x\n",
		sizeof (struct kp_call));

	printf("#define\tIV_HANDLER 0x%x\n",
		OFFSET(struct intr_vector, iv_handler));
	printf("#define\tIV_ARG 0x%x\n",
		OFFSET(struct intr_vector, iv_arg));
	printf("#define\tIV_PIL 0x%x\n",
		OFFSET(struct intr_vector, iv_pil));
	printf("#define\tIV_PENDING 0x%x\n",
		OFFSET(struct intr_vector, iv_pending));
	printf("#define\tIV_MUTEX 0x%x\n",
		OFFSET(struct intr_vector, iv_mutex));

	printf("#define\tINTR_NUMBER 0x%x\n",
		OFFSET(struct intr_req, intr_number));
	printf("#define\tINTR_NEXT 0x%x\n",
		OFFSET(struct intr_req, intr_next));


	printf("#define\tDEVI_BUS_CTL 0x%x\n", &devi->devi_bus_ctl);
	printf("#define\tDEVI_BUS_DMA_MAP 0x%x\n", &devi->devi_bus_dma_map);
	printf("#define\tDEVI_BUS_DMA_ALLOCHDL 0x%x\n",
		&devi->devi_bus_dma_allochdl);
	printf("#define\tDEVI_BUS_DMA_FREEHDL 0x%x\n",
		&devi->devi_bus_dma_freehdl);
	printf("#define\tDEVI_BUS_DMA_BINDHDL 0x%x\n",
		&devi->devi_bus_dma_bindhdl);
	printf("#define\tDEVI_BUS_DMA_UNBINDHDL 0x%x\n",
		&devi->devi_bus_dma_unbindhdl);
	printf("#define\tDEVI_BUS_DMA_FLUSH 0x%x\n", &devi->devi_bus_dma_flush);
	printf("#define\tDEVI_BUS_DMA_WIN 0x%x\n", &devi->devi_bus_dma_win);
	printf("#define\tDEVI_BUS_DMA_CTL 0x%x\n", &devi->devi_bus_dma_ctl);
	printf("#define\tDEVI_DEV_OPS 0x%x\n", &devi->devi_ops);
	printf("#define\tDEVI_BUS_OPS 0x%x\n", &ops->devo_bus_ops);
	printf("#define\tOPS_MAP 0x%x\n", &busops->bus_dma_map);
	printf("#define\tOPS_ALLOCHDL 0x%x\n", &busops->bus_dma_allochdl);
	printf("#define\tOPS_FREEHDL 0x%x\n", &busops->bus_dma_freehdl);
	printf("#define\tOPS_BINDHDL 0x%x\n", &busops->bus_dma_bindhdl);
	printf("#define\tOPS_UNBINDHDL 0x%x\n", &busops->bus_dma_unbindhdl);
	printf("#define\tOPS_FLUSH 0x%x\n", &busops->bus_dma_flush);
	printf("#define\tOPS_WIN 0x%x\n", &busops->bus_dma_win);
	printf("#define\tOPS_MCTL 0x%x\n", &busops->bus_dma_ctl);
	printf("#define\tOPS_CTL 0x%x\n", &busops->bus_ctl);

	printf("#define\tM_TYPE\t0x%x\n",
		OFFSET(struct adaptive_mutex, m_type));
	printf("#define\tM_LOCK\t0x%x\n",
		OFFSET(struct adaptive_mutex, m_lock));
	printf("#define\tM_WAITERS\t0x%x\n",
		OFFSET(struct adaptive_mutex, m_waiters));
	printf("#define\tM_SPINLOCK\t0x%x\n",
		OFFSET(struct spin_mutex, m_spinlock));
	printf("#define\tM_OWNER\t0x%x\n",
		OFFSET(struct adaptive_mutex2, m_owner_lock));

	printf("#define\tFD_NEXT 0x%x\n", OFFSET(struct fdctlr, c_next));
	printf("#define\tFD_REG 0x%x\n", OFFSET(struct fdctlr, c_control));
	printf("#define\tFD_HIINTCT 0x%x\n", OFFSET(struct fdctlr, c_hiintct));
	printf("#define\tFD_SOFTIC 0x%x\n", OFFSET(struct fdctlr, c_softic));
	printf("#define\tFD_HILOCK 0x%x\n", OFFSET(struct fdctlr, c_hilock));
	printf("#define\tFD_OPMODE 0x%x\n",
		OFFSET(struct fdctlr, c_csb.csb_opmode));
	printf("#define\tFD_RADDR 0x%x\n",
		OFFSET(struct fdctlr, c_csb.csb_raddr));
	printf("#define\tFD_RLEN 0x%x\n",
		OFFSET(struct fdctlr, c_csb.csb_rlen));
	printf("#define\tFD_RSLT 0x%x\n",
		OFFSET(struct fdctlr, c_csb.csb_rslt[0]));

	printf("#define\tSD_LOCK 0x%x\n",
	    OFFSET(struct stdata, sd_lock));
	printf("#define\tQ_FLAG 0x%x\n",
	    OFFSET(queue_t, q_flag));
	printf("#define\tQ_NEXT 0x%x\n",
	    OFFSET(queue_t, q_next));
	printf("#define\tQ_STREAM 0x%x\n",
	    OFFSET(queue_t, q_stream));
	printf("#define\tQ_SYNCQ 0x%x\n",
	    OFFSET(queue_t, q_syncq));
	printf("#define\tQ_QINFO 0x%x\n",
	    OFFSET(queue_t, q_qinfo));
	printf("#define\tQI_PUTP 0x%x\n",
	    OFFSET(struct qinit, qi_putp));
	printf("#define\tSQ_FLAGS 0x%x\n",
	    OFFSET(syncq_t, sq_flags));
	printf("#define\tSQ_COUNT 0x%x\n",
	    OFFSET(syncq_t, sq_count));
	printf("#define\tSQ_LOCK 0x%x\n",
	    OFFSET(syncq_t, sq_lock));
	printf("#define\tSQ_WAIT 0x%x\n",
	    OFFSET(syncq_t, sq_wait));
	printf("#define\tSQ_EXITWAIT 0x%x\n",
	    OFFSET(syncq_t, sq_exitwait));
	printf("#define\tSQ_SAVE 0x%x\n",
	    OFFSET(syncq_t, sq_save));

	printf("#define\tQUNSAFE 0x%x\n", QUNSAFE);

	printf("#define\tSQ_EXCL 0x%x\n", SQ_EXCL);
	printf("#define\tSQ_BLOCKED 0x%x\n", SQ_BLOCKED);
	printf("#define\tSQ_FROZEN 0x%x\n", SQ_FROZEN);
	printf("#define\tSQ_WRITER 0x%x\n", SQ_WRITER);
	printf("#define\tSQ_QUEUED 0x%x\n", SQ_QUEUED);
	printf("#define\tSQ_WANTWAKEUP 0x%x\n", SQ_WANTWAKEUP);
	printf("#define\tSQ_WANTEXWAKEUP 0x%x\n", SQ_WANTEXWAKEUP);
	printf("#define\tSQ_CIPUT 0x%x\n", SQ_CIPUT);
	printf("#define\tSQ_UNSAFE 0x%x\n", SQ_UNSAFE);
	printf("#define\tSQ_TYPEMASK 0x%x\n", SQ_TYPEMASK);
	printf("#define\tSQ_GOAWAY 0x%x\n", SQ_GOAWAY);
	printf("#define\tSQ_STAYAWAY 0x%x\n", SQ_STAYAWAY);

	printf("#define\tFKIOCTL\t0x%x\n", FKIOCTL);

	/* SYSIO sbus definitions */
	printf("#define\tIOMMU_FLUSH_REG 0x%x\n", &sbus->iommu_flush_reg);
	printf("#define\tSBUS_CTRL_REG 0x%x\n", &sbus->sbus_ctrl_reg);

/*
 * Gross hack... Although genassym is a user program and hence exit has one
 * parameter, it is compiled with the kernel headers and the _KERNEL define
 * so ANSI-C thinks it should have two!
 */
	exit(0, 0);
}

bit(mask)
	register long mask;
{
	register int i;

	for (i = 0; i < sizeof (int) * NBBY; i++) {
		if (mask & 1)
			return (i);
		mask >>= 1;
	}

/*
 * Gross hack... Although genassym is a user program and hence exit has one
 * parameter, it is compiled with the kernel headers and the _KERNEL define
 * so ANSI-C thinks it should have two!
 */
	exit(1, 0);
}
