#include <sys/fs/cachefs_fs.h>

cnode
./"flags"16t"hash"n{c_flags,X}{c_hash,X}
+/"freeback"16t"freefront"n{c_freeback,X}{c_freefront,X}
+/"frontvp"16t"backvp"16t"size"n{c_frontvp,X}{c_backvp,X}{c_size,D}
+/"filegrp"16t"fileno"16t"invals"n{c_filegrp,X}{c_fileno,D}{c_invals,D}
+/"usage"n{c_usage,D}
+$<<vnode{OFFSETOK}
+$<<cachefsmeta{OFFSETOK}
+/"error"16t"nio"16t"ioflags"n{c_error,D}{c_nio,D}{c_ioflags,X}{END}
