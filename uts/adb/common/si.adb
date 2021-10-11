#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_acl.h>

si
./"next"16t"forw"16t"fore"16tn{s_next,X}{s_forw,X}{s_fore,X}
+/"flags"16t"shadow"16t"dev"16t"signature"n{s_flags,X}{s_shadow,D}{s_dev,X}{s_signature,X}
+/"use"16t"ref"n{s_use,D}{s_ref,D}
+/"rwlock"
.$<<rwlock{OFFSETOK}
+/"s_a(acls)"
.$<<ic_acl{OFFSETOK}
+/"s_d(defaults)"
.$<<ic_acl{OFFSETOK}
