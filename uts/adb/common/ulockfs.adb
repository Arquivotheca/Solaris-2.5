#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_inode.h>

ulockfs
./"flag"16t"fs_lock"16t"fs_mod"16t"vnops_cnt"n{ul_flag,X}{ul_fs_lock,X}{ul_fs_mod,X}{ul_vnops_cnt,D}
+/"lock (mutex)"
.$<<mutex{OFFSETOK}
+/"cv"8t"sbowner"n{ul_cv,x}{ul_sbowner,X}n
+/"lockfs"
.$<<lockfs{OFFSETOK}
