#include <sys/types.h>
#include <sys/vnode.h>

vattr
./"mask"16t"type"16t"mode"16t"uid"n{va_mask,X}{va_type,U}{va_mode,O}{va_uid,U}
+/"gid"16t"fsid"16t"nodeid"16t"nlink"n{va_gid,U}{va_fsid,X}{va_nodeid,X}{va_nlink,U}
+/"size0"16t"size"16t"atime.sec"16t"atime.usec"n{va_size0,X}{va_size,X}{va_atime,2X}
+/"mtime.sec"16t"mtime.usec"16t"ctime.sec"16t"ctime.usec"n{va_mtime,2X}{va_ctime,2X}
+/"rdev"16t"blksize"16t"nblocks"16t"vcode"n{va_rdev,X}{va_blksize,X}{va_nblocks,X}{va_vcode,X}{END}
