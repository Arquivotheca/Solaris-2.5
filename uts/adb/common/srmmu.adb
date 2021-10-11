#ifndef _KERNEL
#define _KERNEL
#endif

#include <sys/param.h>
#include <vm/hat.h>
#include <vm/hat_srmmu.h>

srmmu
./"hat"16t"l1ptbl"16t"l3ptbl"16t"root"n{s_hat,X}{s_l1ptbl,X}{s_l3ptbl,X}{srmmu_root,X}
+/"addr"16t"ctx"n{s_addr,X}{s_ctx,x}
