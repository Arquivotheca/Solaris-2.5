#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_inode.h>

ufs_q
./"head"16t"ne"16t"maxne"16t"flags"n{uq_head,X}{uq_ne,D}{uq_maxne,D}{uq_flags,x}n
+/"cv"n{uq_cv,x}n
+/"mutex"
.$<<mutex{OFFSETOK}
