#include <sys/param.h>
#include <sys/types.h>
#include <sys/reg.h>

fpu
#ifdef sparc
./"fp regs"n{fpu_regs,32X}
+/"fpu_q"16t"fpu_fsr"16t"fpu_qcnt"8t"entsize"8t"en"n{fpu_q,X}{fpu_fsr,X}{fpu_qcnt,V}{fpu_q_entrysize,V}{fpu_en,V}{END}
#endif
