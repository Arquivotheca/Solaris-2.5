#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_quota.h>

dquot
./"forw"16t"back"16t"freef"16t"freeb"n{dq_forw,X}{dq_back,X}{dq_freef,X}{dq_freeb,X}
+/"flags"8t"cnt"8t"uid"16t"ufsvfsp"n{dq_flags,x}{dq_cnt,d}{dq_uid,D}{dq_ufsvfsp,X}
+/"mof"n{dq_mof,2X}
+/"dqb"
.$<<dqblk{OFFSETOK}
+/"lock"
.$<<mutex
