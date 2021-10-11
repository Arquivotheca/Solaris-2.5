#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/ip_var.h>
#include <netinet/tcpip.h>

tcpiphdr
./"next"16t"prev"16t"pr"8t"len"n{ti_i.ih_next,X}{ti_i.ih_prev,X}{ti_i.ih_pr,b}{ti_i.ih_len,d}
+/"src"16t"dst"n{ti_i.ih_src.s_addr,D}{ti_i.ih_dst.s_addr,D}
+/"sport"8t"dport"16t"seq"16t"ack"n{ti_t.th_sport,d}{ti_t.th_dport,d}{ti_t.th_seq,D}{ti_t.th_ack,D}
+/"flags"8t"win"8t"sum"8t"urp"n{ti_t.th_flags,b}{ti_t.th_win,x}{ti_t.th_sum,x}{ti_t.th_urp,x}
