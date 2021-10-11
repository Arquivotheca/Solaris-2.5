#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_fs.h>

cg
.>x
./"link"16t"magic"16t"time"n{cg_link,X}{cg_magic,X}{cg_time,Y}n
+/"cgx"16t"ncyl"8t"niblk"8t"ndblk"n{cg_cgx,D}{cg_ncyl,x}{cg_niblk,x}{cg_ndblk,X}n
+/"cg summary"n
.$<<csum{OFFSETOK}
+/"rotor"16t"frotor"16t"irotor"n{cg_rotor,X}{cg_frotor,X}{cg_irotor,X}n
+/"frsum"
+/{cg_frsum[0],X}{cg_frsum[1],X}{cg_frsum[2],X}{cg_frsum[3],X}
+/{cg_frsum[4],X}{cg_frsum[5],X}{cg_frsum[6],X}{cg_frsum[7],X}
+/"btotoff"16t"boff"16t"iusedoff"16t"freeoff"n{cg_btotoff,X}{cg_boff,X}{cg_iusedoff,X}{cg_freeoff,X}n
+/"nextfreeoff"n{cg_nextfreeoff,X}n
+/"block totals per cylinder"n{OFFSETOK}
{*cg_btotoff,<x}>S
{*cg_boff,<x}>E
<x+<S,(<E-<S)%4/X{OFFSETOK}
+/"free block positions"n{OFFSETOK}
<E>S
{*cg_iusedoff,<x}>E
<x+<S,(<E-<S)%2/x{OFFSETOK}
+/"used inode map"n{OFFSETOK}
<E>S
{*cg_freeoff,<x}>E
<x+<S,(<E-<S)%2/x{OFFSETOK}
+/"free block map"n{OFFSETOK}
<E>S
{*cg_nextfreeoff,<x}>E
<x+<S,(<E-<S)%2/x{OFFSETOK}
