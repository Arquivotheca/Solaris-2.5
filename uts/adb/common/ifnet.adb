#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>

ifnet
./"unit"8t"mtu"8t"flags"8t"addrlist"8t"next"n{if_unit,d}{if_mtu,d}{if_flags,x}{if_addrlist,X}{if_next,X}
+/"head"16t"tail"16t"len"16t"drops"n{if_snd.ifq_head,X}{if_snd.ifq_tail,X}{if_snd.ifq_len,D}{if_snd.ifq_drops,D}
+/"ipack"16t"ierr"16t"opack"16t"oerr"n{if_ipackets,D}{if_ierrors,D}{if_opackets,D}{if_oerrors,D}
