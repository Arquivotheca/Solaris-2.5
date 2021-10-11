#include <sys/modctl.h>
modctl
.>m
{*mod_modname,<m}>f
./"module"16t"'"s"'"
{*mod_filename,<m}/"file  "16t"'"s"'"
<m/"next"16t"prev"16t"id"16t"mp"n{mod_next,X}{mod_prev,X}{mod_id,D}{mod_mp,X}
+/"thread"16t"modinfo"16t"linkage"n{mod_inprogress_thread,X}{mod_modinfo,X}{mod_linkage,X}
+/"filename"16t"module name"n{mod_filename,X}{mod_modname,X}
+/"busy"16t"stub"16t"loaded"8t"install"8t"ldflag"8t"want"n{mod_busy,X}{mod_stub,X}{mod_loaded,B}{mod_installed,B}{mod_loadflags,B}{mod_want,B}
+/"requisites"16t"dependents"16t"loadcnt"n{mod_requisites,X}{mod_dependents,X}{mod_loadcnt,D}{END}
