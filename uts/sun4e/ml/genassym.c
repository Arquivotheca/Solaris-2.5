/*
 * Copyright (c) 1990, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)genassym.c	1.48	94/06/28 SMI"

#ifndef _GENASSYM
#define	_GENASSYM
#endif

#define	SIZES	1

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysinfo.h>
#include <sys/vmmeter.h>
#include <sys/vmparam.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/map.h>
#include <sys/thread.h>
#include <sys/t_lock.h>
#include <sys/mutex_impl.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/rt.h>
#include <sys/ts.h>
#include <sys/msgbuf.h>
#include <sys/vmmac.h>
#include <sys/obpdefs.h>
#include <sys/cpuvar.h>
#include <sys/dditypes.h>
#include <sys/vtrace.h>

#include <sys/pte.h>
#include <sys/reg.h>
#include <sys/psw.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/scb.h>
#include <sys/clock.h>
#include <sys/memerr.h>
#include <sys/intreg.h>
#include <sys/eeprom.h>
#include <sys/eccreg.h>
#include <sys/vme.h>

#include <sys/devops.h>
#include <sys/ddi_impldefs.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>

#include <vm/hat_sunm.h>

#include <sys/avintr.h>

#include <sys/buserr.h>
#include <sys/auxio.h>

#include <sys/fdvar.h>

#include <sys/stream.h>
#include <sys/strsubr.h>

#include <sys/file.h>

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
	register struct ctx *ctx = (struct ctx *)0;
	register label_t *l = 0;
	register struct counterregs *cntr = (struct counterregs *)0;
	register struct fpu *fp = (struct fpu *)0;
#ifdef VAC
	struct flushmeter *fm = (struct flushmeter *)0;
#endif VAC
	register struct autovec *av = (struct autovec *)0;
	register struct timestruc *tms = (struct timestruc *) 0;

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
	printf("#define\tT_INTR 0x%x\n", &tid->t_intr);
	printf("#define\tT_FORW 0x%x\n", &tid->t_forw);
	printf("#define\tT_BACK 0x%x\n", &tid->t_back);
	printf("#define\tT_SIG 0x%x\n", &tid->t_sig);
	printf("#define\tT_TID 0x%x\n", &tid->t_tid);
	printf("#define\tT_PREEMPT 0x%x\n", &tid->t_preempt);
	printf("#define\tT_PRE_SYS 0x%x\n", &tid->t_pre_sys);
	printf("#define\tT_POST_SYS 0x%x\n", &tid->t_post_sys);
	printf("#define\tT_POST_SYS_AST 0x%x\n", &tid->t_post_sys_ast);
	printf("#define\tT_PROC_FLAG 0x%x\n", &tid->t_proc_flag);
	printf("#define\tT_MMUCTX 0x%x\n", &tid->t_mmuctx);
	printf("#define\tT_STARTPC 0x%x\n", &tid->t_startpc);
	printf("#define\tT_SYSNUM 0x%x\n", &tid->t_sysnum);
	printf("#define\tT_ASTFLAG 0x%x\n", &tid->t_astflag);
	printf("#define\tT_INTR_THREAD %d\n", T_INTR_THREAD);
	printf("#define\tFREE_THREAD 0x%x\n", TS_FREE);
	printf("#define\tTS_FREE 0x%x\n", TS_FREE);
	printf("#define\tTS_ZOMB 0x%x\n", TS_ZOMB);
	printf("#define\tTP_MSACCT 0x%x\n", TP_MSACCT);
	printf("#define\tONPROC_THREAD 0x%x\n", TS_ONPROC);
	printf("#define\tT0STKSZ 0x%x\n", DEFAULTSTKSZ);
	printf("#define\tTHREAD_SIZE %d\n", sizeof (kthread_t));

	printf("#define\tNSYSCALL %d\n", NSYSCALL);
	printf("#define\tSYSENT_SIZE %d\n", sizeof (struct sysent));
	printf("#define\tSY_CALLC 0x%x\n", OFFSET(struct sysent, sy_callc));

	printf("#define\tA_HAT 0x%x\n", &as->a_hat);
	printf("#define\tHAT_CTX 0x%x\n", &hat->hat_data[0]);
	printf("#define\tC_CLEAN 0x%x\n", &ctx->c_clean);
	printf("#define\tC_NUM 0x%x\n", &ctx->c_num);
	printf("#define\tC_TIME 0x%x\n", &ctx->c_time);

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
	printf("#define\tLWP_ARG 0x%x\n", lwpid->lwp_arg);
	printf("#define\tLWP_AP 0x%x\n", &lwpid->lwp_ap);
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
	printf("#define\tLWP_MS_PREV 0x%x\n",
				&lwpid->lwp_mstate.ms_prev);
	printf("#define\tLWP_MS_START 0x%x\n", &lwpid->lwp_mstate.ms_start);
	printf("#define\tLWP_PCB 0x%x\n", &lwpid->lwp_pcb);

	printf("#define\tPCB_WBUF 0x%x\n", lwpid->lwp_pcb.pcb_wbuf);
	printf("#define\tPCB_SPBUF 0x%x\n", lwpid->lwp_pcb.pcb_spbuf);
	printf("#define\tPCB_WBCNT 0x%x\n", &lwpid->lwp_pcb.pcb_wbcnt);
	printf("#define\tPCB_SWM 0x%x\n", &lwpid->lwp_pcb.pcb_swm);
	printf("#define\tPCB_UWM 0x%x\n", &lwpid->lwp_pcb.pcb_uwm);
	printf("#define\tPCB_FLAGS 0x%x\n", &lwpid->lwp_pcb.pcb_flags);
	printf("#define\tPCBSIZE 0x%x\n", sizeof (struct pcb));
	printf("#define\tPCB_FPU_REGS 0x%x\n",
				&lwpid->lwp_pcb.pcb_fpu.fpu_fr.fpu_regs[0]);
	printf("#define\tPCB_FPU_FSR 0x%x\n",
				&lwpid->lwp_pcb.pcb_fpu.fpu_fsr);
	printf("#define\tPCB_FPU_Q 0x%x\n",
				lwpid->lwp_pcb.pcb_fpu_q);
	printf("#define\tPCB_FPU_QCNT 0x%x\n",
				&lwpid->lwp_pcb.pcb_fpu.fpu_qcnt);
	printf("#define\tPCB_FPU_EN 0x%x\n",
				&lwpid->lwp_pcb.pcb_fpu.fpu_en);
	printf("#define\tPCB_TRAP0 0x%x\n", &lwpid->lwp_pcb.pcb_trap0addr);

	printf("#define\tFPU_REGS 0x%x\n", &fp->fpu_fr.fpu_regs[0]);
	printf("#define\tFPU_FSR 0x%x\n", &fp->fpu_fsr);
	printf("#define\tFPU_Q 0x%x\n", &fp->fpu_q);
	printf("#define\tFPU_QCNT 0x%x\n", &fp->fpu_qcnt);

	printf("#define\tPSR_PIL_BIT %d\n", bit(PSR_PIL));
	printf("#define\tPG_S_BIT %d\n", bit(PG_S));
	printf("#define\tREGSIZE %d\n", sizeof (struct regs));
	printf("#define\tPGT_SHIFT 0x%x\n", bit(PGT_MASK));
/*
 *	Add VME and ECC support to sun4e arch.
 */
	printf("#define\tVME_PTE 0x%x\n",
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_VME_ADDR));
	printf("#define\tECCREG_PTE 0x%x\n",
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_ECCREG_ADDR));
	printf("#define\tCOUNTER_PTE 0x%x\n",
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_COUNTER_ADDR));
	printf("#define\tEEPROM_PTE 0x%x\n",
		PG_V | PG_KR | PGT_OBIO | PG_NC | btop(OBIO_EEPROM_ADDR));
	printf("#define\tMEMERR_PTE 0x%x\n",
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_MEMERR_ADDR));
	printf("#define\tAUXIO_PTE 0x%x\n",
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_AUXIO_ADDR));
	printf("#define\tINTREG_PTE 0x%x\n",
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_INTREG_ADDR));
	printf("#define\tPTE_SIZE %d\n", sizeof (struct pte));

#ifdef VAC
	printf("#define\tFM_CTX 0x%x\n", &fm->f_ctx);
	printf("#define\tFM_SEGMENT 0x%x\n", &fm->f_segment);
	printf("#define\tFM_PAGE 0x%x\n", &fm->f_page);
	printf("#define\tFM_PARTIAL 0x%x\n", &fm->f_partial);
	printf("#define\tFM_USR 0x%x\n", &fm->f_usr);
#endif VAC

	printf("#define\tCTR_LIMIT10 0x%x\n", &cntr->limit10);
	printf("#define\tCTR_COUNT10 0x%x\n", &cntr->counter10);
	printf("#define\tCTR_LIMIT14 0x%x\n", &cntr->limit14);
	printf("#define\tCTR_COUNT14 0x%x\n", &cntr->counter14);

	printf("#define\tAV_VECTOR 0x%x\n", &av->av_vector);
	printf("#define\tAV_INTARG 0x%x\n", &av->av_intarg);
	printf("#define\tAV_MUTEX 0x%x\n", &av->av_mutex);
	printf("#define\tAV_INT_SPURIOUS %d\n", AV_INT_SPURIOUS);
	printf("#define\tAUTOVECSIZE 0x%x\n", sizeof (struct autovec));

	printf("#define\tGENERIC_PROTERR 0x%x\n", SE_PROTERR);
	printf("#define\tGENERIC_INVALID 0x%x\n", SE_INVALID);

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
	printf("#define\tCPU_FPU 0x%x\n", OFFSET(struct cpu, cpu_fpu));
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
	printf("#define\tCPU_SYSINFO_SYSCALL 0x%x\n",
		OFFSET(struct cpu, cpu_stat.cpu_sysinfo.syscall));

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

	printf("#define\tDEVI_BUS_CTL 0x%x\n", &devi->devi_bus_ctl);
	printf("#define\tDEVI_BUS_DMA_MAP 0x%x\n", &devi->devi_bus_dma_map);
	printf("#define\tDEVI_BUS_DMA_CTL 0x%x\n", &devi->devi_bus_dma_ctl);
	printf("#define\tDEVI_DEV_OPS 0x%x\n", &devi->devi_ops);
	printf("#define\tDEVI_BUS_OPS 0x%x\n", &ops->devo_bus_ops);
	printf("#define\tOPS_MAP 0x%x\n", &busops->bus_dma_map);
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
