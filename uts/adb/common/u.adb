#ifndef _KERNEL
#define _KERNEL
#endif
#include <sys/param.h>
#include <sys/types.h>
#include <sys/user.h>

user
.>c
{*u_flist,<c}>s
{*u_nofiles,<c}>v
<c/n"execid"16t"execsz"16t"tsize"n{u_execid,U}{u_execsz,X}{u_tsize,X}
+/n"dsize"16t"start"16t"ticks"16t"cv"n{u_dsize,X}{u_start,X}{u_ticks,X}{u_cv,x}
+/n"exdata"{u_exdata}
+$<<exdata{OFFSETOK}
+/"aux vector"n{u_auxv,36X}
+/"psargs"n{u_psargs,80C}
+/"comm"n{u_comm,17C}
+/n"cdir"16t"rdir"16t"ttyvp"16t"cmask"n{u_cdir,X}{u_rdir,X}{u_ttyvp,X}{u_cmask,X}
+/n"mem"16t"systrap"8t"ttyp"16t"ttyd"n{u_mem,X}{u_systrap,b}{u_ttyp,X}{u_ttyd,x}
+/"entrymask"n{u_entrymask,7X}
+/"exitmask"n{u_exitmask,7X}
+/n"signodefer"16t"sigonstack"n{u_signodefer,2X}{u_sigonstack,2X}
+/n"sigresethand"16t"sigrestart"n{u_sigresethand,2X}{u_sigrestart,2X}n"sigmask"
+/{u_sigmask,88X}n"signal"
+/{u_signal,44X}n"ru"
+/n"nshmseg"8t"acflag"n{u_nshmseg,d}{u_acflag,b}
+/"rlimit"n{u_rlimit,14X}n"flock"
+$<<mutex{OFFSETOK}
+/n"nofiles"n{u_nofiles,U}n"flist"n{u_flist,X}
./"ofile"16t"pofile"16t"refcnt"n
<s,<v$<ufchunk.nxt
+/n{END}
