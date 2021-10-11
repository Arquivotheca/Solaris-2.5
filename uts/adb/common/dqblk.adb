#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_quota.h>

dqblk
./"bhardlimit"16t"bsoftlimit"16t"curblocks"16t"fhardlimit"n{dqb_bhardlimit,D}{dqb_bsoftlimit,D}{dqb_curblocks,D}{dqb_fhardlimit,D}
+/"fsoftlimit"16t"curfiles"16t"btimelimit"16t"ftimelimit"n{dqb_fsoftlimit,D}{dqb_curfiles,D}{dqb_btimelimit,X}{dqb_ftimelimit,X}
