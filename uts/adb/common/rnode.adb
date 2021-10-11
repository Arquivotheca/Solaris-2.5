#include <rpc/types.h>
#include <sys/time.h>
#include <sys/t_lock.h>
#include <sys/vfs.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <sys/tiuser.h>
#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <sys/vnode.h>
#include <nfs/rnode.h>

rnode
./"freef"16t"freeb"16t"hash"n{r_freef,X}{r_freeb,X}{r_hash,X}n
+$<<vnode{OFFSETOK}
+/"rwlock"
.$<<rwlock{OFFSETOK}
+/"statelock"
.$<<mutex{OFFSETOK}
+/"fh_len"16t"fh_buf"n{r_fh,17X}n
+/"nextr"16t"flags"8t"error"8t"cred"n{r_nextr,D}{r_flags,x}{r_error,d}{r_cred,X}n
+/"unlcred"16t"unlname"16t"unldvp"n{r_unlcred,X}{r_unlname,X}{r_unldvp,X}n
+/"size"n{r_size,D}n
+/"attr"
+$<<vattr{OFFSETOK}
+/"attrtime"16t"mtime"n{r_attrtime,D}{r_mtime,D}n
+/"mapcnt"16t"count"16t"seq"16t"access"n{r_mapcnt,D}{r_count,D}{r_seq,D}{r_acc,X}n
+/"putapage"16t"dir"16t"direof"n{r_putapage,X}{r_dir,X}{r_direof,X}n
+/"symlink"16t"len"16t"size"n{r_symlink,3X}n
+/"verf"32t"modaddr"n{r_verf,2X}{r_modaddr,D}n
+/"cpages"16t"cbase"32t"len"nX{OFFSETOK}3D{OFFSETOK}n
+/"truncaddr"16t"secattr"n{r_truncaddr,D}{r_secattr,X}
+/"cookieverf"32t"lmpl"n{r_cookieverf,2X}{r_lmpl,X}{END}
