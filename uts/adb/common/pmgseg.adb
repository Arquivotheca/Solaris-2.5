#include <sys/param.h>
#include <sys/types.h>
#if defined(sun4) || defined(sun4c)
#include <vm/hat_sunm.h>
#endif

pmgseg
#if defined(sun4) || defined(sun4c)
./"base"16t"next"16t"size"n{pms_base,X}{pms_next,X}{pms_size,X}{END}
#endif
