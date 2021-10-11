/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)rpc.c	1.16	95/03/08 SMI"	/* SVr4.0 1.2	*/
/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *  		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986,1987,1988,1989,1994  Sun Microsystems, Inc.
 *  	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *			All rights reserved.
 */
/*
 * this file consists of routines to support call_rpc();
 * client handles are cached in a hash table;
 * clntudp_create is only called if (site, prog#, vers#) cannot
 * be found in the hash table;
 * a cached entry is destroyed, when remote site crashes
 */

#include <stdio.h>
#include <rpc/rpc.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <syslog.h>
#include <netdb.h>
#include <netdir.h>

extern char *xmalloc();
extern int debug;

/*
 * The client handle cache is managed by two data strcutures.  First each
 * entry is stored in a hash chain based upon the name of the hostname of
 * the client.  Second, each entry is kept on a list in LRU order.  Since
 * each handle takes a file descriptor, it is necessary to choose one to
 * destroy when the number of file descriptors is used up.  The LRU list
 * is used for this purpose
 */

struct cache_link {
	struct cache_entry *cl_next;
	struct cache_entry *cl_prev;
};
struct cache_entry {
	char *ce_host;
	int ce_prognum;
	int ce_versnum;
	int ce_lastproc;
	int ce_retries;
	int ce_age;
	int ce_sock;
	CLIENT *ce_client;
	struct cache_link ce_hash;
	struct cache_link ce_list;
};

static struct cache_entry
	lru_list_head,
	lru_list_tail;
static int	lru_list_max;
static int	lru_list_size = 0;

/*
 * base number of file descriptors used by the statd process.  This is stdin,
 * stdout, stderr, and a file descriptor for communicating with the syslog
 * daemon
 */
#define	NUM_PROC_FDS	4

#define	MAX_HASHSIZE 100
#define	MAX_HASHRETRIES 10
#define	MAX_HASHAGE 20
int HASH_SIZE = MAX_HASHSIZE;
static struct cache_entry
	table[MAX_HASHSIZE];
static int cache_len = sizeof (struct cache_entry);

static void del_hash(struct cache_entry *cp);

/* initialize the head and tail entries in the lru list */
void
init_lru()
{
	struct rlimit
		rl;
	struct cache_entry
		*cp,
		*np;

	/*
	 * get the size of the client handle cache.  This is the maximum
	 * number of file descripters minus the standard number used by
	 * the process (stdin, stdout, stderr, syslog)
	 */
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		lru_list_max = rl.rlim_cur - NUM_PROC_FDS;
	} else {
		lru_list_max = 1;
	}

	/*
	 * If this is a fake crash, destroy all existing entries.
	 * Initially (at startup), lru_list_head is all zeroes.  After
	 * initialization, it should always have a non-null "next" pointer.
	 */
	if (lru_list_head.ce_list.cl_next != NULL) {
		for (cp = lru_list_head.ce_list.cl_next;
		    cp != &lru_list_tail;
		    cp = np) {
			np = cp->ce_list.cl_next;
			del_hash(cp);
		}
	}

	/* now initialize the list */
	lru_list_size = 0;
	lru_list_head.ce_list.cl_prev = (struct cache_entry *) NULL;
	lru_list_tail.ce_list.cl_next = (struct cache_entry *) NULL;
	lru_list_head.ce_list.cl_next = &lru_list_tail;
	lru_list_tail.ce_list.cl_prev = &lru_list_head;
}

void
init_hash()
{
	int
		i;

	/*
	 * there is no need to check if this is a simulated crash
	 * init_lru will take care of that
	 */
	for (i = 0; i < MAX_HASHSIZE; ++i) {
		table[i].ce_hash.cl_next = (struct cache_entry *) NULL;
		table[i].ce_hash.cl_prev = (struct cache_entry *) NULL;
	}
}

/* get the LRU entry from the list */
static struct cache_entry *
get_lru()
{
	return (lru_list_tail.ce_list.cl_prev);
}

/*
 * delete a particular entry from the lru ordered list of cached entries.
 * this happens when an entry is used.  It is deleted and moved to the
 * front.
 */
static void
del_lru(struct cache_entry *cp)
{
	/* remove the entry from the list */
	cp->ce_list.cl_prev->ce_list.cl_next = cp->ce_list.cl_next;
	cp->ce_list.cl_next->ce_list.cl_prev = cp->ce_list.cl_prev;
	/* bookkeeping */
	--lru_list_size;
}

/*
 * add an entry to the lru ordered list of cached entries
 * This takes into account the initial condition where the cl_next and
 * cl_prev pointers are NULL
 */
static void
add_lru(struct cache_entry *cp)
{
	struct cache_entry
		*lrup;

	/*
	 * if the list is full choose the least recently used one
	 * and throw it away
	 */
	if (lru_list_size >= lru_list_max) {
		lrup = get_lru();
		del_hash(lrup);
	}
	/* add the entry to the list */
	lrup = lru_list_head.ce_list.cl_next;
	cp->ce_list.cl_next = lrup;
	cp->ce_list.cl_prev = &lru_list_head;
	lrup->ce_list.cl_prev = cp;
	lru_list_head.ce_list.cl_next = cp;
	/* bookkeeping */
	++lru_list_size;
}

/*
 * delete a particular entry from the doubly linked list of cache_entries
 * attached to the hash chain at index hidx.
 */
static void
del_hash(struct cache_entry *cp)
{
	/* remove it from the LRU list first */
	del_lru(cp);
	/* remove it from the hash chain */
	if (cp->ce_hash.cl_prev != (struct cache_entry *) NULL) {
		cp->ce_hash.cl_prev->ce_hash.cl_next = cp->ce_hash.cl_next;
	}
	if (cp->ce_hash.cl_next != (struct cache_entry *) NULL) {
		cp->ce_hash.cl_next->ce_hash.cl_prev = cp->ce_hash.cl_prev;
	}
	/* free any dynamically alocated space associated with the entry */
	if (cp->ce_client)
		clnt_destroy(cp->ce_client);
	if (cp->ce_host)
		free(cp->ce_host);
	free((char *) cp);
}

/*
 * add a particular entry to the hash list given
 */
static void
add_hash(struct cache_entry *cp, struct cache_entry *hlp)
{
	/* add it the LUR list first */
	add_lru(cp);
	/* add it to the hash chain */
	cp->ce_hash.cl_next = hlp->ce_hash.cl_next;
	cp->ce_hash.cl_prev = hlp;
	if (hlp->ce_hash.cl_next != (struct cache_entry *) NULL) {
		hlp->ce_hash.cl_next->ce_hash.cl_prev = cp;
	}
	hlp->ce_hash.cl_next = cp;
}

/*
 * calculate the hash index for a particular host.  The function is the
 * sum of the bytes in the name modulus the size of the hash table
 */
static int
hash(name)
	unsigned char *name;
{
	int len;
	int i, c;

	c = 0;
	len = strlen(name);
	for (i = 0; i < len; i++) {
		c = c +(int) name[i];
	}
	c = c %HASH_SIZE;
	return (c);
}

/*
 * remove all entries from the hash table associated with a particular host
 */
static void
remove_hash(host)
	char *host;
{
	struct cache_entry
		*cp,
		*next;
	int h;

	h = hash((unsigned char *) host);
	next = table[h].ce_hash.cl_next;
	while ((cp = next) != NULL) {
		next = cp->ce_hash.cl_next;
		if (strcmp(cp->ce_host, host) == 0) {
			del_hash(cp);
		}
	}
}

/*
 * find_hash returns the cached entry;
 * it returns NULL if not found;
 */
static struct cache_entry *
find_hash(host, prognum, versnum, procnum)
	char *host;
	int prognum, versnum, procnum;
{
	struct cache_entry
		*cp;

	cp = table[hash((unsigned char *) host)].ce_hash.cl_next;
	while (cp != NULL) {
		if (strcmp(cp->ce_host, host) == 0 &&
			cp->ce_prognum == prognum &&
			cp->ce_versnum == versnum) {
			if ((cp->ce_lastproc == procnum &&
				++cp->ce_retries > MAX_HASHRETRIES) ||
				(++cp->ce_age > MAX_HASHAGE)) {
					remove_hash(host);
					return (NULL);
			}
			cp->ce_lastproc = procnum;
			cp->ce_retries = 0;
			/* move entry to most recently used position */
			del_lru(cp);
			add_lru(cp);
			return (cp);
		}
		cp = cp->ce_hash.cl_next;
	}
	return (NULL);
}

static struct cache_entry *
add_hash_entry(host, prognum, versnum, procnum)
	char *host;
	int prognum, versnum, procnum;
{
	struct cache_entry *cp;
	int h;

	/* allocate space for the cache entry */
	if ((cp = (struct cache_entry *) xmalloc((u_int) cache_len)) == NULL) {
		return (NULL);	/* malloc error */
	}
	if ((cp->ce_host = xmalloc((u_int) (strlen(host)+1))) == NULL) {
		free((char *) cp);
		return (NULL);	/* malloc error */
	}
	/* fill in the cache entry */
	(void) strcpy(cp->ce_host, host);
	cp->ce_prognum = prognum;
	cp->ce_versnum = versnum;
	cp->ce_lastproc = procnum;
	cp->ce_retries = 0;
	cp->ce_age = 0;
	h = hash((unsigned char *) host);
	add_hash(cp, &table[h]);
	return (cp);
}


static CLIENT *
create_client(host, rsys, prognum, versnum)
	char	*host;
	long	rsys;
	int	prognum;
	int	versnum;
{
	int		fd;
	struct timeval	timeout;
	CLIENT		*client;
	struct t_info	tinfo;
	struct hostent	*hp;


	if (((client = clnt_create(host, prognum, versnum,
		"netpath")) == NULL) && ((rsys == 0) ||
		((hp = gethostbyaddr((char *)&rsys, sizeof (long),
			AF_INET)) == NULL) ||
			((client = clnt_create(hp->h_name, prognum, versnum,
				"netpath")) == NULL)))
					return (NULL);

	(void) CLNT_CONTROL(client, CLGET_FD, (caddr_t)&fd);
	if (t_getinfo(fd, &tinfo) != -1) {
		if (tinfo.servtype == T_CLTS) {
			/*
			 * Set time outs for connectionless case
			 */
			timeout.tv_usec = 0;
			timeout.tv_sec = 15;
			(void) CLNT_CONTROL(client,
				CLSET_RETRY_TIMEOUT, (caddr_t)&timeout);
		}
	} else {
		rpc_createerr.cf_stat = RPC_TLIERROR;
		rpc_createerr.cf_error.re_terrno = t_errno;
		remove_hash(host);
		return (NULL);
	}

	return (client);
}

/*
 * make an rpc call to host.  If rsys is not 0, it is the ip address of host
 * It looks for host in a hash table of hosts being accessed.  If it is
 * there and the entry matches the < prognum, versnum, procnum > triple
 * the hashed entry is used otherwise a new client handle will be created.
 */
int
call_rpc(host, rsys, prognum, versnum, procnum,
	inproc, in, outproc, out, valid_in, timeout_sec)
	char *host;
	long	rsys;
	u_long prognum, versnum;
	int	procnum;
	xdrproc_t inproc, outproc;
	char *in, *out;
	int valid_in;
	int timeout_sec;
{
	enum clnt_stat clnt_stat;
	struct timeval timeout, tottimeout;
	struct cache_entry *cp;
	struct t_info tinfo;
	int fd;
	extern int t_errno;

	if (debug)
		printf("enter call_rpc() ...\n");

	if (valid_in == 0) remove_hash(host);

	if ((cp = find_hash(host, (int) prognum, (int) versnum,
		(int) procnum)) == (struct cache_entry *)NULL) {
		if ((cp = (struct cache_entry *) add_hash_entry(
			host,
			(int) prognum,
			(int) versnum,
			(int) procnum)) == (struct cache_entry *)NULL) {
			syslog(LOG_ERR, "udp cannot send due to out of cache");
			return (-1);
		}
		if (debug)
			printf("(%x):[%s, %d, %d] is a new connection\n",
				cp, host, prognum, versnum);

		if ((cp->ce_client = create_client(host, rsys, prognum,
			versnum)) == NULL) {
				remove_hash(host);
				return (RPC_TIMEDOUT);
		}
	} else {
		if (valid_in == 0) { /* cannot use cache */
			if (debug)
				printf("(%x):[%s, %d, %d] new connection\n",
					cp, host, prognum, versnum);

			if ((cp->ce_client = create_client(host, rsys, prognum,
				versnum)) == NULL) {
				return (RPC_TIMEDOUT);
			}
		}
	}

	tottimeout.tv_sec = timeout_sec;
	tottimeout.tv_usec = 0;
	clnt_stat = clnt_call(cp->ce_client, procnum, inproc, in,
	    outproc, out, tottimeout);
	if (debug) {
		printf("clnt_stat=%d\n", (int) clnt_stat);
	}

	return ((int) clnt_stat);
}
