#include <sys/cpuvar.h>

cpu
./"id"16t"seqid"16t"flags"n{cpu_id,D}{cpu_seqid,D}{cpu_flags,x}
+/"thread"16t"idle_t"16t"pause"n{cpu_thread,X}{cpu_idle_thread,X}{cpu_pause_thread,X}
+/"lwp"16t"callo"16t"fpowner"n{cpu_lwp,X}{cpu_callo,X}{cpu_fpowner,X}
+/"next"16t"prev"16t"next on"16t"prev on"n{cpu_next,X}{cpu_prev,X}{cpu_next_onln,X}{cpu_prev_onln,X}
+$<<disp{OFFSETOK}
+/"runrun"8t"kprnrn"8t"dispthread"16t"thread lock"n{cpu_runrun,B}{cpu_kprunrun,B}8t{cpu_dispthread,X}{cpu_thread_lock,B}
+/"intr_stack"16t"on_intr"16t"intr_thread"16t"intr_actv"n{cpu_intr_stack,X}{cpu_on_intr,X}{cpu_intr_thread,X}{cpu_intr_actv,X}
+/"base_spl"n{cpu_base_spl,D}{END}
