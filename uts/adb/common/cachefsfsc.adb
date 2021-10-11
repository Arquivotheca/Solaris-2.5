#include <sys/fs/cachefs_fs.h>
#include <sys/fs/cachefs_fscache.h>

fscache
./"cfsid"n{fs_cfsid,X}
+/"flags"16t"fscdirvp"16t"cachep"n{fs_flags,X}{fs_fscdirvp,X}{fs_cache,X}
+/"options"n{fs_options,3X}
+/"filegrp"n{fs_filegrp,X}
+/"cfs vfsp"16t"back vfsp"16t"rootvp"n{fs_cfsvfsp,X}{fs_backvfsp,X}{fs_rootvp,X}
+/"vnoderef"16t"cfsops"n{fs_vnoderef,D}{fs_cfsops,X}
+/"acregmin"16t"acregmax"n{fs_acregmin,D}{fs_acregmax,X}
+/"acdirmin"16t"acdirmax"n{fs_acdirmin,D}{fs_acdirmax,X}
+/"next"n{fs_next,X}
+/"q head"16t"q tail"n{fs_workq.wq_head,X}{fs_workq.wq_tail,X}
+/"q len"16t"q threadcnt"n{fs_workq.wq_length,X}{fs_workq.wq_thread_count,X}
+/"q max"16t"q halt"n{fs_workq.wq_max_len,X}{END}
