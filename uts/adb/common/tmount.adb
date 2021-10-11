#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/vnode.h>
#include <sys/fs/tmp.h>

tmount
./"next"16t"vfsp"16t"rootnode"16t"inomap"n{tm_next,X}{tm_vfsp,X}{tm_rootnode,X}{tm_inomap,X}
+/"direntries"16t"directories"n{tm_direntries,D}{tm_directories,D}
+/"files"16t"kmemspace"16t"anonmem"n{tm_files,D}{tm_kmemspace,D}{tm_anonmem,D}
+/"mntpath"n{tm_mntpath,X}
