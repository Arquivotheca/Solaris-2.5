#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/tiuser.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <nfs/nfs_clnt.h>

mntinfo
./"lock"
.$<<mutex{OFFSETOK}
+/"knetconfig"n{mi_knetconfig,X}
+/"addr"
+$<<netbuf{OFFSETOK}
+/"syncaddr"
.$<<netbuf{OFFSETOK}
+/"rootvp"16t"flags"16t"tsize"16t"stsize"n{mi_rootvp,X}{mi_flags,X}{mi_tsize,X}{mi_stsize,X}
+/"timeo"16t"retrans"n{mi_timeo,D}{mi_retrans,X}
+/"hostname"n{mi_hostname,32C}
+/"netname"16t"netnamelen"16t"authflavor"n{mi_netname,X}{mi_netnamelen,D}{mi_authflavor,D}
+/"acregmin"16t"acregmax"16t"acdirmin"16t"acdirmax"n{mi_acregmin,U}{mi_acregmax,U}{mi_acdirmin,U}{mi_acdirmax,U}
+/"mi_timers"
+$<<rpctimer{OFFSETOK}
+$<<rpctimer{OFFSETOK}
+$<<rpctimer{OFFSETOK}
+$<<rpctimer{OFFSETOK}
+/"curread"16t"curwrite"n{mi_curread,D}{mi_curwrite,D}
+/"async_reqs"16t"async_tail"16t"async_req_cv"n{mi_async_reqs,X}{mi_async_tail,X}{mi_async_reqs_cv,x}
+/"threads"8t"max_threads"8t"async_cv"8t"count"n{mi_threads,d}{mi_max_threads,d}{mi_async_cv,x}{mi_async_count,D}
+/"async_lock"
+$<<mutex{OFFSETOK}
+/"pathconf"16t"prog"16t"vers"n{mi_pathconf,X}{mi_prog,D}{mi_vers,D}
+/"rfsnames"16t"reqs"16t"call_type"16t"timer_type"n{mi_rfsnames,X}{mi_reqs,X}{mi_call_type,X}{mi_timer_type,X}
+/"aclnames"16t"aclreqs"16t"acl_call_type"16t"acl_timer_type"n{mi_aclnames,X}{mi_aclreqs,X}{mi_acl_call_type,X}{mi_acl_timer_type,X}
+/"printftime"n{mi_printftime,D}{END}
