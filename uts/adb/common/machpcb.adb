#include <sys/param.h>
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/regset.h>
#include <sys/machpcb.h>

machpcb
#if defined(sun4c) || defined(sun4d) || defined(sun4m)
./"spbuf"n{mpcb_spbuf,8X}
+/n"uwm"16t"swm"16t"wbcnt"n{mpcb_uwm,X}{mpcb_swm,X}{mpcb_wbcnt,D}
+/n"flags"16t"wocnt"16t"wucnt"n{mpcb_flags,D}{mpcb_wocnt,D}{mpcb_wucnt,D}{END}
#endif /* sun4c/sun4d/sun4m */
#ifdef sun4u 
./"spbuf"n{mpcb_spbuf,8X}
+/n"wbcnt"n{mpcb_wbcnt,D}
+/n"flags"16t"wocnt"16t"wucnt"n{mpcb_flags,D}{mpcb_wocnt,D}{mpcb_wucnt,D}{END}
#endif



