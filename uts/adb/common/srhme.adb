#ifndef _KERNEL
#define _KERNEL
#endif

#include <sys/param.h>
#include <vm/hat.h>
#include <vm/hat_srmmu.h>

srhment
.$<<hme{OFFSETOK}
+/"ptbl"16t"hash"n{hme_ptbl,X}{hme_hash,X}
