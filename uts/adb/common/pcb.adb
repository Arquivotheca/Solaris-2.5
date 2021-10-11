#include <sys/param.h>
#include <sys/types.h>
#include <sys/pcb.h>
#include <sys/user.h>

pcb
#ifdef m68k
./"pc"16t"sp"16t"psr"n{pcb_pc,X}{pcb_sp,X}{pcb_psr,X}
+/"p0br"16t"p0lr"16t"p1br"16t"p1lr"n{pcb_p0br,X}{pcb_p0lr,X}{pcb_p1br,X}{pcb_p1lr,X}
+/"szpt"16t"sswap"n{pcb_szpt,X}{pcb_sswap,X}{END}
#endif /* m68k */
#ifdef sparc
./"flags"16t"n{pcb_flags,D}
+/n"regstat"16t"step"16t"tracepc"16t"instr"n{pcb_xregstat,U}{pcb_step,U}{pcb_tracepc,X}{pcb_instr,X}{END}
#endif /* sparc */
