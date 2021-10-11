#include <sys/types.h>
#if defined(sun4) || defined(sun4c) || defined(sun4e)
#include <vm/hat_sunm.h>
#endif
#if defined (sun4m) || defined(sun4d)
#include <vm/hat_srmmu.h>
#endif

ctx
#if defined(sun4) || defined(sun4c) || defined(sun4e)
./"lock"8t"clean"8t"num"8t"time"8t"as"n{c_lock,B}{c_clean,B}{c_num,B}{c_time,x}{c_as,X}{END}
#endif
#if defined (sun4m) || defined(sun4d)
./"as"n{c_as,X}{END}
#endif
