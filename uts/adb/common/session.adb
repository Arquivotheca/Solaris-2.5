#include <sys/types.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/session.h>

sess
./"ref"8t"mode"8t"uid"16t"gid"n{s_ref,d}{s_mode,x}{s_uid,D}{s_gid,D}
+/"ctime"16t"maj"8t"min"8t"vnode"n{s_ctime,X}{s_dev,2x}{s_vp,X}
+/"sidp"16t"cred"n{s_sidp,X}{s_cred,X}
