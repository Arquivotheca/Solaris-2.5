#include <sys/types.h>
#include <sys/fs/ufs_fsdir.h>

direct
./"ino"16t"reclen"16t"namelen"n{d_ino,X}{d_reclen,u}{d_namlen,u}
+/"name"16t{d_name,256C}
