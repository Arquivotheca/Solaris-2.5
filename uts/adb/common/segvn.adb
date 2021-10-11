#include <sys/param.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <vm/seg_vn.h>

segvn_data
./"lock"
.$<<rwlock{OFFSETOK}
+/"pgprot"8t"prot"8t"maxprot"8t"type"n{pageprot,b}{prot,b}{maxprot,b}{type,b}
+/"vnode"16t"offset"16t"anon_index"16t"anon_map"n{vp,X}{offset,X}{anon_index,X}{amp,X}
+/"vpage"16t"cred"16t"swresv"16t"advise"8t"pgadvise"n{vpage,X}{cred,X}{swresv,X}{advice,b}{pageadvice,b}{END}
