/*
 * adb: rwmap data in file/process address space.
 */

#ident "@(#)access.c	1.30	95/08/03 SMI"

#include "adb.h"
#include <sys/ptrace.h>
#ifndef KADB
#include <stdio.h>
#include <kvm.h>
#endif

put(addr, space, value) 
	addr_t addr;
	int space, value;
{
	db_printf(6, "put: addr=%X, space=%D, value=%X", 
							addr, space, value);
	(void) rwmap('w', addr, space, value);
}

get(addr, space)
	addr_t addr;
	int space;
{
	db_printf(6, "get: addr=%X, space=%D", addr, space);
	return (rwmap('r', addr, space, 0));
}

chkget(addr, space)
	addr_t addr;
	int space;
{
	int w = get(addr, space);

	db_printf(6, "chkget: addr=%X, space=%D", addr, space);
	chkerr();
	return (w);
}

bchkget(addr, space) 
	addr_t addr;
	int space;
{
	long val = chkget(addr, space);

	db_printf(6, "bchkget: addr=%X, space=%D", addr, space);
	/* try to be machine independent -- big end and little end machines */
	return (*(char *)&val);
}

rwmap(mode, addr, space, value)
	char mode;
	addr_t addr;
	int space, value;
{
	int file, w;
#if !defined(KADB)
	if (kernel && space == ISP)
		space = DSP;
#endif
	db_printf(5, "rwmap: mode='%c', addr=%X, space=%D, value=%D",
						mode, addr, space, value);
	if (space == NSP) {
		db_printf(5, "rwmap: space=NSP, returns 0");
		return 0;
	}
	if (pid) {
		if (mode == 'r') {
			if (readproc(addr, (char *)&value, 4) != 4)
				rwerr(space);
			db_printf(5, "rwmap: returns %X, space %s DSP",
					value, (space == DSP) ? "==" : "!=");
			return (value);
		}
		if (writeproc(addr, (char *)&value, 4) != 4)
			rwerr(space);
		db_printf(5, "rwmap: returns %X, space %s DSP",
					value, (space == DSP) ? "==" : "!=");
		return (value);
	}
	w = 0;
	if (mode == 'w' && wtflag == 0)
		error("not in write mode");
	if (!chkmap(&addr, space, &file)) {
		db_printf(5, "rwmap: returns 0 as chkmap() returned 0");
		return (0);
	}

#ifndef KADB
	db_printf(2, "rwmap: kernel=%D, space %s DSP",
					kernel, (space == DSP) ? "==" : "!=");
	if (kernel && space == DSP) {
		if ((mode == 'r') ? kread(addr, &w) : kwrite(addr, &value))
			rwerr(space);
		db_printf(5, "rwmap: returns %X", w);
		return (w);
	}
#endif !KADB
	db_printf(2, "rwmap: file=%D", file);
	if (file >= 0  &&
	                rwphys(file, addr, mode == 'r' ? &w : &value, mode) < 0)
		rwerr(space);
	db_printf(5, "rwmap: returns w which is *(%X)", &w);
	return (w);
}


static
rwerr(space)
	int space;
{
	extern int nprflag;

	/*
	 * Check whether someone has temporarily disabled this error msg.
	 * XXX - this should be passed in by the caller.
	 */
	if (!nprflag) {
		if (space & DSP)
			errflg = "data address not found";
		else
			errflg = "text address not found";
	}
	db_printf(3, "rwerr: %s", errflg);
	return (0);
}

rwphys(file, addr, aw, rw)
	int file;
	unsigned addr;
	int *aw, rw;
{
	int rc;

	db_printf(5, "rwphys: file=%D, addr=%X, aw=%X, rw='%c', kernel=%D", file, addr, aw, rw, kernel);

#ifdef notdef
	if (kernel)
		addr = KVTOPH(addr);
#endif notdef
	if (fileseek(file, addr) == 0) {
		db_printf(5, "rwphys: returns -1 as fileseek() returned 0");
		return (-1);
	}
	if (rw == 'r')
		rc = read(file, (char *)aw, sizeof (int));
	else
		rc = write(file, (char *)aw, sizeof (int));
	if (rc != sizeof (int)) {
		db_printf(5, "rwphys: returns -1 as rc != sizeof (int)");
		return (-1);
	}
	db_printf(5, "rwphys: returns 0");
	return (0);
}

/* check that addr can be mapped to a file offset and if so set the
** offset and fd;  the first map range that includes the address is
** used - except that if the SPACE is ?* or /* the first (map_head) 
** range is bypassed - this continues support for using ?* or /*
** to explicitly specify use of the (b2,e2,f2) triple for mapping
*/
static
chkmap(addr, space, fdp)
	register addr_t *addr;
	int space;
	int *fdp;
{
	register struct map *amap;
	register struct map_range *mpr;

	db_printf(5, "chkmap: addr=%X, space=%D, fdp=%X", addr, space, fdp);
	amap = (space & DSP) ? &datmap : &txtmap;
	for(mpr = amap->map_head; mpr != NULL; mpr= mpr->mpr_next) {
		if(mpr == amap->map_head && space&STAR)
			continue;
		if(within(*addr, mpr->mpr_b, mpr->mpr_e)) {
			*addr += mpr->mpr_f - mpr->mpr_b;
			*fdp = mpr->mpr_fd; 
			db_printf(2, "chkmap: found %X in a valid map range (b=%X,e=%X,f=%X)\n\tcalculated *addr to be %X, in file desc %X", *addr, mpr->mpr_b, mpr->mpr_e, mpr->mpr_f, *addr, *fdp);
			return (1);
		}
	}

	rwerr(space);
	db_printf(2, "chkmap: failed to find addr in a valid map range");
	return (0);
}

static
within(addr, lbd, ubd)
	addr_t addr, lbd, ubd;
{
	db_printf(7, "within: addr=%X, lbd=%X, ubd=%X",
							addr, lbd, ubd);
	return (addr >= lbd && addr < ubd);
}

static
fileseek(f, a)
	int f;
	addr_t a;
{
	db_printf(7, "fileseek: f=%D, a=%X", f, a);
	return (lseek(f, (long)a, 0) != -1);
}


/*
 * VAX UNIX can read and write data from the user process at odd addresses.
 * 68000 UNIX does not have this capability, so we must attempt to ensure
 * even addresses.  The 68000 has the related problem of not being able
 * to do word accesses to memory at odd addresses, so we must be careful.
 *
 * On a Sparc, it's even worse.  All accesses must be on a 32-bit
 * boundary.  These routines sort of allow 68000s and sparcs to
 * pretend that they're vaxen (only as far as word/long boundaries
 * are concerned, not w.r.t. byte order).
 */

#if defined(KADB)

/*
 * This is a lot of old difficult stuff that is only needed
 * for KADB on sparc.  PTRACE_WRITEDATA and PTRACE_READDATA
 * are implemented by the fake ptrace() built on /proc.
 */

writeproc(a, buf, len0)
	int a;
	char *buf;
	int len0;
{
	int len = len0;
	int  count;
	unsigned long val;
	char *writeval;

	db_printf(5, "writeproc: a=%X, buf=%X, len0=%D", a, buf, len0);
	errno = 0;

	if (len <= 0)
		return len;

#ifdef sparc
	if (a & INSTR_ALIGN_MASK) {
	  int  ard32;		/* a rounded down to 32-bit boundary */
	  unsigned short *sh;

		/* We must align to a four-byte boundary */
		ard32 = a & ~3;
		(void) readproc( ard32, &val, 4);
		switch( a&3 ) {
		 case 1: /* Must insert a byte and a short */
			*((char *)&val + 1) = *buf++;	/* insert byte */
			--len;
			++a;
			/* FALL THROUGH to insert short */

		 case 2: /* insert short-word */
			*((char *)&val + 2) = *buf++;	/* insert byte */
			*((char *)&val + 3) = *buf++;	/* insert byte */
			len -= 2;
			a += 2;
			break;

		 case 3: /* insert byte -- big or little end machines */
			*((char *)&val +3) = *buf++;
			--len;
			++a;
			break;
		}
		/*
		 * to do shared library, we are not allowed to
		 * change the permit of data, never POKEDATA
		if (ptrace(a < txtmap.e1 ? PTRACE_POKETEXT : PTRACE_POKEDATA,
		    pid, ard32, val) == -1 && errno) {
			if (errno != ESRCH)
				perror(symfil);
			db_printf(3, "writeproc: ptrace failed, errno=%D",
									errno);
		}
		 */
		if (ptrace(PTRACE_POKETEXT, pid, ard32, val) == -1
		 && errno) {
			if (errno != ESRCH)
				perror(symfil);
			db_printf(3, "writeproc: ptrace failed, errno=%D",
									errno);
		}
	}
#elif !defined(i386)
        if (a & 01) {
		/* Only need to align to two bytes */
                /* here for stupid version of ptrace */
                (void) readproc(a & ~01, &val, 4);
                /* insert byte -- big or little end machines */
                *((char *)&val + 1) = *buf++;

		/*
		 * to do shared library, we are not allowed to
		 * change the permit of data, never POKEDATA
                (void) ptrace(a < txtmap.e1 ? PTRACE_POKETEXT :
                    PTRACE_POKEDATA, pid, a & ~01, val);
		 */
		(void) ptrace(PTRACE_POKETEXT, pid, a & ~01, val);
		--len;  ++a;
        }

#endif !sparc

#if defined(i386) && defined(KADB)

	len = ptrace ( PTRACE_WRITEDATA, pid, a, len, buf );

	if ( len < 0 || errno )
		return ( 0 );

#else	/* defined(i386) && defined(KADB) */

	db_printf(9, "writeproc: len=%D", len);
	while (len > 0) {
		if (len < 4) {
			(void) readproc(a, (char *) &val, 4);
			count = len;
		} else
			count = 4;
		writeval = (char *)&val;
		while (count--)
			*writeval++ = *buf++;
		/*
		 * to do shared library, we are not allowed to
		 * change the permit of data, never POKEDATA
		if (ptrace(a < txtmap.e1 ? PTRACE_POKETEXT : PTRACE_POKEDATA,
		    pid, a, val) == -1 && errno) {
			if (errno != ESRCH)
				perror(symfil);
			db_printf(3, "writeproc: ptrace failed, errno=%D",
									errno);
		}
		 */
		if (ptrace(PTRACE_POKETEXT, pid, a, val) == -1 && errno) {
			if (errno != ESRCH)
				perror(symfil);
			db_printf(3, "writeproc: ptrace failed, errno=%D",
									errno);
		}
		len -= 4;
		a += 4;
		db_printf(9, "writeproc: len=%D, a=%X", len, a);
	}
	if (errno) {
		db_printf(5, "writeproc: fails, errno=%D and returns 0",
									errno);
		return 0;
	}

#endif	/* defined(i386) && defined(KADB) */

	db_printf(5, "writeproc: returns %D", len0);
	return len0;
}

readproc(a, buf, len0)
	int a;
	char *buf;
	int len0;
{
	int len = len0;
	int count;
	char *readchar;
	unsigned val;

	db_printf(5, "readproc: a=%X, buf=%X, len0=%D", a, buf, len0);
	errno = 0;

	if (len0 <= 0 || !pid) {
		db_printf(5, "readproc: returns len0=%D, pid=%D", len0, pid);
		return len0;
	}

#ifdef sparc
	if (a & INSTR_ALIGN_MASK) {
	  unsigned short *sh;
		/* We must align to a four-byte boundary */
		val = ptrace(a < txtmap.map_head->mpr_e ?
		    PTRACE_PEEKTEXT : PTRACE_PEEKDATA, pid, a & ~3, 0);
		if (val == -1 && errno) {
			/*
			 * There are cases that it is ok if ptrace fails.
			if (errno != ESRCH)
				perror(symfil);
			*/
			db_printf(1,
			    "readproc: ptrace failed, addr=%X errno=%D",
			    a, errno);
		}
		switch( a&3 ) {
		 case 1: /* Must insert a byte and a short */
			*buf++ = *((char *)&val + 1);
			--len; 
			++a;

			/* Fall through to handle the short */

		 case 2: /* insert two bytes */
			*buf++ = *((char *)&val + 2);
			*buf++ = *((char *)&val + 3);
			len -= 2;
			a += 2;
			break;

		 case 3: /* insert byte -- big or little end machines */
			*buf++ = *((char *)&val + 3);
			--len;
			++a;
			break;
		}
	}
#elif !defined(i386)

	/* Only need to align to two bytes */
	if (a & 01) {
		/* here for stupid ptrace that cannot grot odd addresses */
		val = ptrace(a < txtmap.map_head->mpr_e ?
		    PTRACE_PEEKTEXT : PTRACE_PEEKDATA, pid, a & ~01, 0);
		/* technically, should handle big & little end machines */
		*(buf++) = *((char*)&val + 1);
		--len; ++a;
	}
#endif !i386

#if defined(i386) && defined(KADB)
	/* source lines taken from i386 tree - no alignment problems here! */

	len = ptrace(a < txtmap.map_head->mpr_e ?
		PTRACE_READTEXT : PTRACE_READDATA, pid, a, len0, buf);

	if (len < 0 || errno)
		return (0);
#else
	db_printf(9, "readproc: len=%D", len);
	while (len > 0) {
		val = ptrace(a < txtmap.map_head->mpr_e ?
			PTRACE_PEEKTEXT : PTRACE_PEEKDATA, pid, a, 0);
		if (val == -1 && errno) {
			/*
			 * There are cases that it is ok if ptrace fails.
			if (errno != ESRCH)
				perror(symfil);
			*/
			db_printf(3,
			    "readproc: ptrace failed, addr=%X errno=%D",
			    a, errno);
		}
		readchar = (char *) &val;
		count = (len < 4) ? len : 4;
		while (count--)
			*buf++ = *readchar++;
		len -= 4;
		a += 4;
		db_printf(9, "readproc: len=%D, a=%X", len, a);
	}
	if (errno) {
		db_printf(5, "readproc: fails, errno=%D and returns 0",
									errno);
		return 0;
	}
#endif	/* defined(i386) && defined(KADB) */

	db_printf(5, "readproc: returns %D", len0);
	return len0;
}

#else	/* defined(KADB) */

/*
 * This is the simple stuff that uses PTRACE_WRITEDATA and PTRACE_READDATA
 * as implemented by the fake ptrace() built on /proc.
 */

writeproc(a, buf, len0)
	int a;
	char *buf;
	int len0;
{
	int len;

	db_printf(4, "writeproc: a=%X, buf=%X, len0=%D", a, buf, len0);
	errno = 0;

	if (len0 <= 0 || !pid) {
		db_printf(4, "writeproc: returns len0=%D, pid=%D", len0, pid);
		return len0;
	}

	len = ptrace(PTRACE_WRITEDATA, pid, a, len0, buf);

	if (len < 0 || errno)
		return (0);
	db_printf(4, "writeproc: returns %D", len0);
	return len0;
}

readproc(a, buf, len0)
	int a;
	char *buf;
	int len0;
{
	int len;

	db_printf(4, "readproc: a=%X, buf=%X, len0=%D", a, buf, len0);
	errno = 0;

	if (len0 <= 0 || !pid) {
		db_printf(4, "readproc: returns len0=%D, pid=%D", len0, pid);
		return len0;
	}

	len = ptrace(PTRACE_READDATA, pid, a, len0, buf);

	if (len < 0 || errno)
		return (0);
	db_printf(4, "readproc: returns %D", len0);
	return len0;
}

#endif	/* defined(KADB) */

exitproc(void)
{
#ifndef KADB
	extern kvm_t	*kvmd;
	extern FILE	*OPENFILE;

	db_printf(7, "exitproc: pid=%D", pid);
	if (!pid)
		return 0;

	if (ptrace(PTRACE_KILL, pid, 0, 0) == -1 && errno) {
		char s[16];

		sprintf(s, "pid %D", pid);
		perror(s);
		db_printf(3, "exitproc: ptrace failed, errno=%D", errno);
	}
	pid = 0;
	db_printf(2, "exitproc: process killed, pid reset to 0");
#endif /* !KADB */
}

#ifndef KADB
/* allocate a new map_range; fill in fields and add it to the map so that
** "ranges are stored in increasing starting value (mpr_b)" remains true
*/
#define INFINITE 0x7fffffff
void
add_map_range(struct map *map, const int start, const int end, const int offset,
	      char *file_name)
{
	struct map_range *mpr;

	db_printf(7, "add_map_range: map=%X, start=%D, end=%D, "
		     "offset=%D, file_name='%s'",
		     map, start, end, offset, file_name);

	if (map != NULL) {
		for (mpr = txtmap.map_head; mpr != NULL; mpr = mpr->mpr_next)
			if (mpr->mpr_b == start &&
			    mpr->mpr_e == end &&
			    mpr->mpr_f == offset)
				return;
		for (mpr = datmap.map_head; mpr != NULL; mpr = mpr->mpr_next)
			if (mpr->mpr_b == start &&
			    mpr->mpr_e == end &&
			    mpr->mpr_f == offset)
				return;
	}

	mpr = (struct map_range*) calloc(1,sizeof(struct map_range));
	if (mpr == NULL)
		outofmem();
	mpr->mpr_fd = getfile(file_name, INFINITE);
	mpr->mpr_fn = file_name;
	mpr->mpr_b = start;
	mpr->mpr_e = end;
	mpr->mpr_f = offset;
	map->map_tail->mpr_next = mpr;
	map->map_tail = mpr;
	db_printf(2, "add_map_range: added (b=%X, e=%X, f=%X) for '%s'",
					start, end, offset, file_name);
}

/* free any ranges which may refer to shared libraries
**  -the first two ranges are tied to corfil and symfil
*/
free_shlib_map_ranges(map)
struct map *map;
{
	struct map_range *mpr, *tmp;

	db_printf(5, "free_shlib_map_range: map=%X", map);
	if( map->map_head && map->map_head->mpr_next) {
		map->map_tail = map->map_head->mpr_next;
		for(mpr = map->map_tail->mpr_next; mpr; mpr =  tmp) {
			if(mpr->mpr_fd)
				(void) close(mpr->mpr_fd);
			tmp = mpr->mpr_next;
			free(mpr);
		}
		map->map_tail->mpr_next = 0;
	} else /* corrupt range list..*/
		map->map_tail = map->map_head;
}
#endif !KADB
