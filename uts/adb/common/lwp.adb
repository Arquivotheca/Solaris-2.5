#include	<sys/types.h>
#include	<sys/thread.h>
#include	<sys/lwp.h>

_klwp
./n"oldcontext"16t"ap"16t"errno"16t"error"8t"eosys"n{lwp_oldcontext,X}{lwp_ap,X}{lwp_errno,D}{lwp_error,B}{lwp_eosys,B}n"arg(s)"
+/{lwp_arg,8X}
#ifdef i386
+/n"ar0"16t"qsav.pc"16t"qsav.sp"n{lwp_regs,X}{lwp_qsav,6X}
#else
+/n"ar0"16t"qsav.pc"16t"qsav.sp"n{lwp_regs,X}{lwp_qsav,2X}
#endif
+/n"cursig"8t"curflt"8t"sysabrt"8t"asleep"8tn{lwp_cursig,B}{lwp_curflt,B}{lwp_sysabort,B}{lwp_asleep,B}n"sigaltstack"
+$<<sigaltstack{OFFSETOK}
+/n"curinfo"n{lwp_curinfo,X}n"siginfo"
+$<<ksiginfo{OFFSETOK}
+/n"sigoldmask"n{lwp_sigoldmask,2X}
+/n"pr_base"16t"pr_size"16t"pr_off"16t"pr_scale"n{lwp_prof.pr_base,X}{lwp_prof.pr_size,X}{lwp_prof.pr_off,X}{lwp_prof.pr_scale,X}
#ifdef i386
+/n"mstate"n{lwp_mstate,27X}
#else
+/n"mstate"n{lwp_mstate,28X}
#endif
+/n"ru"n{lwp_ru,12X}
+/n"lastfault"16t"lastfaddr"n{lwp_lastfault,D}{lwp_lastfaddr,X}n"timer"
+$<<itimerval{OFFSETOK}
+$<<itimerval{OFFSETOK}
+$<<itimerval{OFFSETOK}
+/"oweupc"8t"state"8t"nostop"8t"cv"n{lwp_oweupc,B}{lwp_state,B}{lwp_nostop,d}{lwp_cv,x}
+/"utime"16t"stime"16t"thread"16t"procp"n{lwp_utime,X}{lwp_stime,X}{lwp_thread,X}{lwp_procp,X}{END}
