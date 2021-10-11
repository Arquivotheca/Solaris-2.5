#include <sys/thread.h>

_kthread
.
./n"link"16t"stk"n{t_link,X}{t_stk,X}
+/n"bound"16t"affcnt"16t"bind_cpu"n{t_bound_cpu,X}{t_affinitycnt,d}16t{t_bind_cpu,d}
+/n"flag"16t"procflag"8t"schedflag"16t"state"n{t_flag,x}8t{t_proc_flag,x}8t{t_schedflag,B}8t{t_state,X}
+/"pri"16t"epri"16t"pc"16t"sp"n{t_pri,d}16t{t_epri,d}16t{t_pcb.val[0],X}{t_pcb.val[1],X}
+/"wchan0"16t"wchan"16t"cid"16t"clfuncs"n{t_wchan0,X}{t_wchan,X}{t_cid,X}{t_clfuncs,X}
+/n"cldata"16t"ctx"16t"lofault"16t"onfault"n{t_cldata,X}{t_ctx,X}{t_lofault,X}{t_onfault,X}
+/n"nofault"16t"swap"16t"lock"16t"cpu"n{t_nofault,X}{t_swap,X}{t_lock,B}16t{t_cpu,X}
+/n"intr"16t"delay_cv"16t"tid"16t"alarmid"n{t_intr,X}{t_delay_cv,x}16t{t_tid,D}{t_alarmid,X}"realitimer"
+$<<itimerval{OFFSETOK}
+/n"itimerid"16t"sigqueue"16t"sig"n{t_itimerid,X}{t_sigqueue,X}{t_sig,2X}
+/n"hold"16t""16t"forw"16t"back"n{t_hold,2X}{t_forw,X}{t_back,X}
+/n"lwp"16t"procp"16t"next"16t"prev"n{t_lwp,X}{t_procp,X}{t_next,X}{t_prev,X}
+/n"preempt"16t"trace"16t"whystop"8t"whatstop"n{t_preempt,d}16t{t_trace,X}{t_whystop,d}{t_whatstop,d}
+/n"kpri_req"16t"sysnum"8t"astflag"16t"pollstate"16t"cred"n{t_kpri_req,D}{t_sysnum,d}{t_astflag,B}{t_pollstate,X}{t_cred,X}
+/n"lbolt"16t"pctcpu"8t"trapret"8t"pre_sys"8t"post_sys sig_check"n{t_lbolt,X}{t_pctcpu,x}{t_trapret,B}{t_pre_sys,B}{t_post_sys,B}{t_sig_check,B}
+/n"lockp"16t"oldspl"16t"disp queue"16t"disp time"n{t_lockp,X}{t_oldspl,x}16t{t_disp_queue,X}{t_disp_time,D}
+/n"mstate"16t"waitrq"16t16t"rprof"n{t_mstate,D}{t_waitrq,2X}{t_rprof,X}
+/n"prioinv"16t"ts"16t"sobj_ops"n{t_prioinv,X}{t_ts,X}{t_sobj_ops,X}{END}
