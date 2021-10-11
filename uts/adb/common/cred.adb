#include <sys/param.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/cred.h>

cred
./"ref"8t"ngroups"8t"uid"16t"gid"16t"ruid"n{cr_ref,U}{cr_ngroups,U}{cr_uid,U}{cr_gid,U}{cr_ruid,U}
+/"rgid"16t"suid"16t"sgid"16t"groups"n{cr_rgid,U}{cr_suid,D}{cr_sgid,U}{cr_groups,U}{END}
