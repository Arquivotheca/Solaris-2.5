#include <sys/types.h>
#include <sys/vfs.h>

vfs
./"next"16t"op"16t"vnodecvd"16t"flag"n{vfs_next,X}{vfs_op,p}{vfs_vnodecovered,X}{vfs_flag,X}
+/"bsize"16t"fstype"16t"fsid"n{vfs_bsize,D}{vfs_fstype,D}{vfs_fsid,2X}
+/"data"16t"maj"8t"min"8t"bcount"16t"nsubmnts"n{vfs_data,X}{vfs_dev,2x}{vfs_bcount,D}{vfs_nsubmounts,d}
+/"list"16t"hash"n{vfs_list,X}{vfs_hash,X}
+/"reflock (mutex)"
.$<<mutex{OFFSETOK}
