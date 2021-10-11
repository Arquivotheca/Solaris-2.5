#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_acl.h>

ic_acl
./"owner"16t"group"16t"other"16t"users"n{owner,X}{group,X}{other,X}
+/"users"n{users,X}
+/"class"
.$<<ufs_aclmask{OFFSETOK}
