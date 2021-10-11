#ifndef _KERNEL
#define _KERNEL
#endif

#include <sys/param.h>
#include <vm/hat.h>
#include <vm/hat_srmmu.h>

ptbl
./"next"16t"as"16t"base"8t"lockcnt"n{ptbl_u.u_next,X}{ptbl_as,X}{ptbl_base,x}{ptbl_lockcnt,x}
+/"unused"8t"index"8t"valid"8t"flags"n{ptbl_unused,B}{ptbl_index,B}{ptbl_validcnt,B}{ptbl_flags,B}
