#ifndef _KERNEL
#define _KERNEL
#endif
#include <sys/param.h>
#include <vm/hat.h>

hment
./"prev"16t"next"16t"hat"8t"impl"8t"flags"n{hme_prev,X}{hme_next,X}x{OFFSETOK}2B{OFFSETOK}
./+"page"n{hme_page,X}
