#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_inode.h>

lockfs
./"lock"16t"flags"16t"key"16t"comlen"n{lf_lock,X}{lf_flags,X}{lf_key,X}{lf_comlen,D}n
+/"comment"n{lf_comment,X}n
