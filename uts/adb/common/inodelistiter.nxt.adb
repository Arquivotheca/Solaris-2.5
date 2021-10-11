#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_inode.h>

inode
{*i_number,<F}="INODE:"Dn
<F/"flag"8t"mode"8t"dev"16t"size"n{i_flag,x}{i_mode,o}{i_dev,X}{i_size_o,X}{i_size,X}n
{*i_forw,<F}>F
,(<F-<S)$<inodelistiter.nxt
$<inodelist.nxt
