/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)util.c	1.39	95/08/25 SMI"

/*
 * Utility routines for run-time linker.  some are duplicated here from libc
 * (with different names) to avoid name space collisions.
 */
#include	<sys/mman.h>
#include	<stdarg.h>
#include	<fcntl.h>
#include	<string.h>
#include	<ctype.h>
#include	<dlfcn.h>
#include	<unistd.h>
#include	<signal.h>
#include	"_rtld.h"
#include	"debug.h"
#include	"profile.h"

static int	sigkill =	SIGKILL;	/* default signal for kill() */


/*
 * All error messages go through eprintf().  During process initialization these
 * messages should be directed to the standard error, however once control has
 * been passed to the applications code these messages should be stored in an
 * internal buffer for use with dlerror().  Note, fatal error conditions that
 * may occur while running the application will still cause a standard error
 * message, see exit() in this file for details.
 * The `application' flag serves to indicate the transition between process
 * initialization and when the applications code is running.
 */

/*
 * Null function used as place where a debugger can set a breakpoint.
 */
void
r_debug_state()
{
}

/*
 * Execute any .init sections.  These are called for each shared object, in the
 * reverse order in which the objects have been loaded.  Skip any .init defined
 * in the main executable, as this will be called from crt0.
 * We set the `init-done' flag before calling the .init section in case the
 * .init section itself does some dlopen() activity, which may cause this .init
 * to be called again (recursively).
 */
void
call_init(Rt_map * lmp)
{
	void (*	iptr)();

	PRF_MCOUNT(60, call_init);

	if (NEXT(lmp))
		call_init((Rt_map *)NEXT(lmp));

	if (!(FLAGS(lmp) & (FLG_RT_ISMAIN | FLG_RT_INITDONE))) {
		/*
		 * Set the initdone flag regardless of whether this object
		 * actually contains an .init section.  This flag prevents us
		 * from processing this section again for a .init and also
		 * signifies that a .fini must be called should it exist.
		 */
		FLAGS(lmp) |= FLG_RT_INITDONE;

		if ((iptr = INIT(lmp)) != 0) {
			DBG_CALL(Dbg_util_call_init(NAME(lmp)));
			(*iptr)();
		}
	}
}

/*
 * Function called by atexit(3C).  Calls all .fini sections related with the
 * mains dependent shared libraries in the order in which the shared libraries
 * have been loaded.  Skip any .fini defined in the main executable, as this
 * will be called by crt0 (main was never marked as initdone).
 */
void
call_fini()
{
	Rt_map *	lmp;
	void (*		fptr)();

	PRF_MCOUNT(61, call_fini);

	for (lmp = lml_main.lm_head; lmp; lmp = (Rt_map *)NEXT(lmp))
		if (FLAGS(lmp) & FLG_RT_INITDONE) {
			FLAGS(lmp) &= ~FLG_RT_INITDONE;

			if ((fptr = FINI(lmp)) != 0) {
				DBG_CALL(Dbg_util_call_fini(NAME(lmp)));
				(*fptr)();
			}
		}
}

/*
 * Append an item to the specified list, and return a pointer to the list
 * node created.
 */
Listnode *
list_append(List * lst, const void * item)
{
	Listnode *	_lnp;

	PRF_MCOUNT(62, list_append);

	if ((_lnp = (Listnode *)malloc(sizeof (Listnode))) == 0)
		return (0);

	_lnp->data = (void *) item;
	_lnp->next = NULL;

	if (lst->head == NULL)
		lst->tail = lst->head = _lnp;
	else {
		lst->tail->next = _lnp;
		lst->tail = lst->tail->next;
	}
	return (_lnp);
}

/*
 * Append an item to the specified link map list.
 */
void
lm_append(Lm_list * lml, Rt_map * lmp)
{
	PRF_MCOUNT(63, lm_append);

	if (lml->lm_head == NULL) {
		lml->lm_head = lmp;
		PREV(lmp) = NULL;
	} else {
		NEXT(lml->lm_tail) = (Link_map *)lmp;
		PREV(lmp) = (Link_map *)lml->lm_tail;
	}
	lml->lm_tail = lmp;
}

/*
 * Internal getenv routine.  Only strings starting with `LD_' are reserved for
 * our use.  By convention, all strings should be of the form `LD_XXXXX=', if
 * the string is followed by a non-null value the appropriate functionality is
 * enabled.
 */
int
readenv(const char ** envp, int aout)
{
	const char *	s1, * dbg_str = 0;
	char *		s2;
	int		i;

	if (envp == (const char **)0)
		return (1);
	while (*envp != (const char *)0) {
		s1 = *envp++;
		if (*s1++ != 'L' || *s1++ != 'D' || *s1++ != '_' || *s1 == '\0')
			continue;

		if (strncmp(s1, "LIBRARY_PATH=", 13) == 0) {
			s1 += 13;
			if (*s1 != '\0')
				envdirs = s1;
		} else if (strncmp(s1, "PRELOAD=", 8) == 0) {
			s1 += 8;
			while (isspace(*s1))
				s1++;
			if ((i = strlen(s1)) == 0)
				continue;
			if ((s2 = malloc(++i)) == 0)
				return (0);
			(void) strcpy(s2, s1);
			s1 = s2;
			while (*s2 != '\0') {
				if (isspace(*s2)) {
					*s2++ = '\0';
					if (!(list_append(&preload, s1)))
						return (0);
					while (isspace(*s2))
						s2++;
					s1 = s2;
				} else
					s2++;
			}
			if (s1 != s2)
				if (!(list_append(&preload, s1)))
					return (0);
		} else if (strncmp(s1, "TRACE_LOADED_OBJECTS", 20) == 0) {
			s1 += 20;
			if ((*s1 == '=') && (*++s1 != '\0')) {
				tracing = *s1 - '0';
			} else if (((strncmp(s1, "_E=", 3) == 0) && !aout) ||
			    ((strncmp(s1, "_A=", 3) == 0) && aout)) {
				s1 += 3;
				if (*s1 != '\0')
					tracing = *s1 - '0';
			}
		} else if (strncmp(s1, "TRACE_SEARCH_PATHS=", 19) == 0) {
			s1 += 19;
			if (*s1 != '\0')
				rtld_flags |= RT_FL_SEARCH;
		} else if (strncmp(s1, "VERBOSE=", 8) == 0) {
			s1 += 8;
			if (*s1 != '\0')
				rtld_flags |= RT_FL_VERBOSE;
		} else if (strncmp(s1, "WARN=", 5) == 0) {
			s1 += 5;
			if (*s1 != '\0')
				rtld_flags |= RT_FL_WARN;
		} else if (strncmp(s1, "BINDINGS=", 9) == 0) {
			s1 += 9;
			if (*s1 != '\0') {
				/*
				 * NOTE, this variable is simply for backward
				 * compatibility.  If this and LD_DEBUG are both
				 * specified, only one of the strings is going
				 * to get processed.
				 */
				dbg_str = "bindings";
			}
		} else if (strncmp(s1, "BIND_NOW=", 9) == 0) {
			s1 += 9;
			if (*s1 != '\0')
				bind_mode = RTLD_NOW;
		} else if (strncmp(s1, "BIND_NOT=", 9) == 0) {
			s1 += 9;
			if (*s1 != '\0')
				rtld_flags |= RT_FL_NOBIND;
		} else if (strncmp(s1, "SIGNAL=", 7) == 0) {
			i = 0;
			while (s1[7] != '\0') {
				i = (i * 10) + (int)(s1[7] - '0');
				s1++;
			}
			sigkill = i;
		} else if (strncmp(s1, "NOVERSION=", 10) == 0) {
			s1 += 10;
			if (*s1 != '\0')
				rtld_flags |= RT_FL_NOVERSION;
#ifdef DEBUG
		} else if (strncmp(s1, "DEBUG", 5) == 0) {
			s1 += 5;
			if ((*s1 == '=') && (*++s1 != '\0'))
				dbg_str = s1;
			else if (strncmp(s1, "_OUTPUT=", 8) == 0) {
				s1 += 8;
				if (*s1 != '\0')
					dbg_file = s1;
			}
#endif
#ifdef	PROF
		} else if (strncmp(s1, "PROFILE", 7) == 0) {
			s1 += 7;
			if ((*s1 == '=') && (*++s1 != '\0')) {
				profile_name = (char *)s1;
			} else if (strncmp(s1, "_OUTPUT=", 8) == 0) {
				s1 += 8;
				if (*s1 != '\0')
					profile_dir = (char *)s1;
			}
#endif
		}
	}

	/*
	 * If debugging was requested initialize things now (we do this after
	 * checking all the environment strings in case an LD_LIBRARY_PATH
	 * setting should effect the debug library lookup path).  If we've been
	 * called via `ldd(1) -s' disable the tracing while processing the
	 * debug library.
	 */
	if (dbg_str) {
		int	_tracing;

		_tracing = tracing;
		tracing = 0;
		dbg_mask |= dbg_setup(dbg_str);
		tracing = _tracing;
	}

	/*
	 * LD_WARN, LD_TRACE_SEARCH_PATHS and LD_VERBOSE are meaningful only if
	 * tracing.
	 */
	if (!tracing)
		rtld_flags &= ~(RT_FL_SEARCH | RT_FL_WARN | RT_FL_VERBOSE);

	return (1);
}

/*
 * Simplified printing.  The following conversion specifications are supported:
 *
 *	% [#] [-] [min field width] [. precision] s|d|x|c
 *
 * We also assume the buffer passed to us is sufficiently large enough to
 * hold the string we're creating (no way do the linkers ever generate
 * error, or diagnostic, message greater than 1024 :-).
 */
#define	FLG_UT_MINUS	0x0001	/* - */
#define	FLG_UT_SHARP	0x0002	/* # */
#define	FLG_UT_DOTSEEN	0x0008	/* dot appeared in format spec */

int
doprf(const char * format, va_list args, char * buffer)
{
	char	c;
	char *	bp = buffer;

	PRF_MCOUNT(65, doprf);

	while ((c = *format++) != '\0') {
		if (c != '%') {
			*bp++ = c;
		} else {
			int	base = 0, flag = 0, width = 0, prec = 0;
			int	_c, _i, _n;
			char *	_s;
again:
			c = *format++;
			switch (c) {
			case '-':
				flag |= FLG_UT_MINUS;
				goto again;
			case '#':
				flag |= FLG_UT_SHARP;
				goto again;
			case '.':
				flag |= FLG_UT_DOTSEEN;
				goto again;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				if (flag & FLG_UT_DOTSEEN)
					prec = (prec * 10) + c - '0';
				else
					width = (width * 10) + c - '0';
				goto again;
			case 'x':
			case 'X':
				base = 16;
				break;
			case 'd':
			case 'D':
			case 'u':
				base = 10;
				flag &= ~FLG_UT_SHARP;
				break;
			case 'o':
			case 'O':
				base = 8;
				break;
			case 'c':
				_c = va_arg(args, int);
				for (_i = 24; _i >= 0; _i -= 8)
					if ((c = ((_c >> _i) & 0x7f)) != 0) {
						*bp++ = c;
					}
				break;
			case 's':
				_s = va_arg(args, char *);
				_i = strlen(_s);
				_n = width - _i;
				if (!prec)
					prec = _i;

				if (width && !(flag & FLG_UT_MINUS)) {
					while (_n-- > 0)
						*bp++ = ' ';
				}
				while (((c = *_s++) != 0) && prec--) {
					*bp++ = c;
				}
				if (width && (flag & FLG_UT_MINUS)) {
					while (_n-- > 0)
						*bp++ = ' ';
				}
				break;
			case '%':
				*bp++ = '%';
				break;
			default:
				break;
			}

			/*
			 * Numeric processing
			 */
			if (base) {
				char		local[11];
				const char *	string = "0123456789abcdef";
				unsigned long	num;
				int		ssize = 0, psize = 0;
				const char *	prefix = "";

				num = va_arg(args, unsigned long);

				if (flag & FLG_UT_SHARP) {
					if (base == 16) {
						prefix = "0x";
						psize = 2;
					} else {
						prefix = "0";
						psize = 1;
					}
				}
				if ((base == 10) && (int)num < 0) {
					prefix = "-";
					psize = 1;
					num = (unsigned)(-(int)num);
				}

				/*
				 * Convert the numeric value into a local
				 * string (stored in reverse order).
				 */
				_s = local;
				do {
					*_s++ = string[num % base];
					num /= base;
					ssize++;
				} while (num);

				/*
				 * Provide any precision or width padding.
				 */
				if (prec) {
					_n = prec - ssize;
					while (_n-- > 0) {
						*_s++ = '0';
						ssize++;
					}
				}
				if (width && !(flag & FLG_UT_MINUS)) {
					_n = width - ssize - psize;
					while (_n-- > 0)
						*bp++ = ' ';
				}

				/*
				 * Print any prefix and the numeric string
				 */
				while (*prefix)
					*bp++ = *prefix++;
				do {
					*bp++ = *--_s;
				} while (_s > local);

				/*
				 * Provide any width padding.
				 */
				if (width && (flag & FLG_UT_MINUS)) {
					_n = width - ssize - psize;
					while (_n-- > 0)
						*bp++ = ' ';
				}
			}
		}
	}
	*bp = '\0';
	return (bp - buffer);
}

/* VARARGS2 */
void
eprintf(Error error, const char * format, ...)
{
	va_list			args;
	static char *		buffer = 0;
	static const char *	nospace = "ld.so.1: internal: malloc failed";
	int			bind;
	int			len = 0;

	static const char *	strings[] = {
		"",	"warning: ",	"fatal: ",	"elf error: "
	};

	PRF_MCOUNT(66, eprintf);

	/*
	 * because eprintf() uses a global 'buffer' to store
	 * it's work a write lock is needed around the
	 * whole routine.
	 *
	 * Note: no lock is placed around printf() because
	 *	uses a buffer off of the stack and because
	 *	it does it's write in a single atomic write().
	 */
	if ((lc_version > 0) &&
	    ((bind = bind_guard(THR_FLG_PRINT) == 1)))
		(void) rw_wrlock(&printlock);

	/*
	 * Allocate the error string buffer, if one doesn't already exist.
	 * Reassign lasterr, incase it was `cleared' by a dlerror() call.
	 */
	if (!buffer) {
		if ((buffer = (char *)malloc(ERRSIZE)) == 0) {
			lasterr = (char *)nospace;
			if ((lc_version > 0) && bind) {
				(void) rw_unlock(&printlock);
				(void) bind_clear(THR_FLG_PRINT);
			}
			return;
		}
	}
	lasterr = buffer;

	/*
	 * If we have completed startup initialization all error messages
	 * must be saved.  These are reported through dlerror().  If we're
	 * still in the initialization stage output the error directly and
	 * add a newline.
	 */
	va_start(args, format);
	if (error > ERR_NONE) {
		(void) strcpy(buffer, rt_name);
		(void) strcat(buffer, ": ");
		(void) strcat(buffer, pr_name);
		(void) strcat(buffer, ": ");
		(void) strcat(buffer, strings[error]);
		len = strlen(buffer);
	}
	len += doprf(format, args, &buffer[len]);
	if (!(rtld_flags & RT_FL_APPLIC)) {
		buffer[len++] = '\n';
		(void) write(2, buffer, len);
	} else {
		DBG_CALL(Dbg_util_str(buffer));
	}
	va_end(args);
	if ((lc_version > 0) && bind) {
		(void) rw_unlock(&printlock);
		(void) bind_clear(THR_FLG_PRINT);
	}
}

/* VARARGS2 */
int
sprintf(char * buf, const char * format, ...)
{
	va_list	args;
	int	len;

	PRF_MCOUNT(67, sprintf);

	va_start(args, format);
	len = doprf(format, args, buf);
	va_end(args);
	return (len);
}

/* VARARGS1 */
int
printf(const char * format, ...)
{
	va_list	args;
	char 	buffer[ERRSIZE];
	int	len;

	PRF_MCOUNT(68, printf);

	va_start(args, format);
	len = doprf(format, args, buffer);
	va_end(args);
	return (write(1, buffer, len));
}


/*
 * Exit.  If we arrive here with a non zero status it's because of a fatal
 * error condition (most commonly a relocation error).  If the application has
 * already had control, then the actual fatal error message will have been
 * recorded in the dlerror() message buffer.  Print the message before really
 * exiting.
 */
void
exit(int status)
{
	if (status) {
		if (rtld_flags & RT_FL_APPLIC) {
			(void) write(2, lasterr, strlen(lasterr));
			(void) write(2, "\n", 1);
		}
		(void) kill(getpid(), sigkill);
	}
	_exit(status);
}

/*
 * Routines to co-ordinate the opening and closing of /dev/zero
 */
static const char *	dz_name = "/dev/zero";
static int		dz_fd = DZ_UNAVAIL;

void
dz_init(int fd)
{
	PRF_MCOUNT(69, dz_init);

	dz_fd = fd;
}

int
dz_open()
{
	PRF_MCOUNT(70, dz_open);

	if (dz_fd == DZ_UNAVAIL) {
		if ((dz_fd = open(dz_name, O_RDONLY)) == DZ_UNAVAIL)
			eprintf(ERR_FATAL, Errmsg_cofl, dz_name, errno);
	}
	return (dz_fd);
}

void
dz_close()
{
	PRF_MCOUNT(71, dz_close);

	if (dz_fd != DZ_UNAVAIL)
		(void) close(dz_fd);
	dz_fd = DZ_UNAVAIL;
}

/*
 * Routines to manage init file map structure
 */
Fmap *
fm_init()
{
	Fmap * fm;

	PRF_MCOUNT(86, fm_init);

	if ((fm = (Fmap *)malloc(sizeof (Fmap))) == NULL)
		return (0);

	fm->fm_mflags = MAP_SHARED;
	fm->fm_maddr = 0;
	fm->fm_msize = syspagsz;
	return (fm);
}

void
fm_cleanup(Fmap * fm)
{
	PRF_MCOUNT(87, fm_cleanup);

	fm->fm_mflags = MAP_SHARED;
	if (fm->fm_maddr) {
		(void) munmap((caddr_t)fm->fm_maddr, fm->fm_msize);
		fm->fm_maddr = 0;
	}
	fm->fm_msize = syspagsz;
}


/*
 * Routines for initializing, testing, and freeing link map permission values.
 */
static Permit	__Permit = { 1, ~0UL };
static Permit *	_Permit = &__Permit;

Permit *
perm_get()
{
	unsigned long	_cnt, cnt = _Permit->p_cnt;
	unsigned long * _value, * value = &_Permit->p_value[0];
	Permit *	permit;

	PRF_MCOUNT(72, perm_get);

	/*
	 * Allocate a new Permit structure for return to the user based on the
	 * static Permit structure presently in use.
	 */
	if ((permit = (Permit *)calloc(sizeof (unsigned long), cnt + 1)) == 0)
		return ((Permit *)0);
	permit->p_cnt = cnt;
	_value = &permit->p_value[0];

	/*
	 * Determine the next available permission bit and update the global
	 * permit value to indicate this value is now taken.
	 */
	for (_cnt = 0; _cnt < cnt; _cnt++, _value++, value++) {
		unsigned long	bit;
		for (bit = 0x1; bit; bit = bit << 1) {
			if (*value & bit) {
				*value &= ~bit;
				*_value = bit;
				return (permit);
			}
		}
	}

	/*
	 * If all the present permission values have been exhausted allocate
	 * a new reference Permit structure.
	 */
	cnt++;
	if ((_Permit = (Permit *)calloc(sizeof (unsigned long), cnt + 1)) == 0)
		return ((Permit *)0);
	_Permit->p_cnt = cnt;
	value = &_Permit->p_value[cnt - 1];
	*value = ~0UL;

	/*
	 * Free the original Permit structure obtained for the user, and try
	 * again.
	 */
	free(permit);
	return (perm_get());
}

void
perm_free(Permit * permit)
{
	unsigned long	_cnt, cnt;
	unsigned long *	_value, * value;

	PRF_MCOUNT(73, perm_free);

	if (!permit)
		return;

	cnt = permit->p_cnt;
	_value = &permit->p_value[0];
	value = &_Permit->p_value[0];

	/*
	 * Set the users permit bit in the global Permit structure thus
	 * indicating thats its free for future use.
	 */
	for (_cnt = 0; _cnt < cnt; _cnt++, _value++, value++)
		*value |= *_value;

	free(permit);
}

int
perm_test(Permit * permit1, Permit * permit2)
{
	unsigned long	_cnt, cnt;
	unsigned long * _value1, * _value2;

	PRF_MCOUNT(74, perm_test);

	if (!permit1 || !permit2)
		return (0);

	_value1 = &permit1->p_value[0];
	_value2 = &permit2->p_value[0];

	/*
	 * Determine which permit structure is the smaller.  Loop through the
	 * `p_value' elements looking for a match.
	 */
	if ((cnt = permit1->p_cnt) > permit2->p_cnt)
		cnt = permit2->p_cnt;

	for (_cnt = 0; _cnt < cnt; _cnt++, _value1++, _value2++)
		if (*_value1 & *_value2)
			return (1);
	return (0);
}

Permit *
perm_set(Permit * permit1, Permit * permit2)
{
	unsigned long	_cnt, cnt;
	unsigned long * _value1, * _value2;
	Permit *	_permit = permit1;

	PRF_MCOUNT(75, perm_set);

	if (!permit2)
		return ((Permit *)0);

	cnt = permit2->p_cnt;
	_value2 = &permit2->p_value[0];

	/*
	 * If the original permission structure has not yet been initialized
	 * allocate a new structure for return to the user and simply copy the
	 * new structure to it.
	 */
	if (_permit == 0) {
		if ((_permit = (Permit *)calloc(sizeof (unsigned long),
		    cnt + 1)) == 0)
			return ((Permit *)0);
		_permit->p_cnt = cnt;
		_value1 = &_permit->p_value[0];
		for (_cnt = 0; _cnt < cnt; _cnt++, _value1++, _value2++)
			*_value1 = *_value2;
		return (_permit);
	}

	/*
	 * If we don't presently have room in the destination permit structure
	 * to hold the new permission bit, reallocate a new structure.
	 */
	if (cnt > _permit->p_cnt) {
		if ((_permit = (Permit *)realloc((void *) _permit,
		    (size_t)((cnt + 1) * sizeof (unsigned long)))) == 0)
			return ((Permit *)0);

		/*
		 * Make sure the newly added entries are cleared, and update the
		 * new permission structures count.
		 */
		for (_cnt = _permit->p_cnt; _cnt < cnt; _cnt++)
			_permit->p_value[_cnt] = 0;
		_permit->p_cnt = cnt;
	}

	/*
	 * Set the appropriate permission bits.
	 */
	_value1 = &_permit->p_value[0];
	for (_cnt = 0; _cnt < cnt; _cnt++, _value1++, _value2++)
		*_value1 |= *_value2;

	return (_permit);
}

Permit *
perm_unset(Permit * permit1, Permit * permit2)
{
	unsigned long	_cnt, cnt;
	unsigned long * _value1, * _value2;
	unsigned long	_inuse = 0;

	PRF_MCOUNT(76, perm_unset);

	if (!permit1 || !permit2)
		return ((Permit *)0);

	cnt = permit2->p_cnt;
	_value1 = &permit1->p_value[0];
	_value2 = &permit2->p_value[0];

	/*
	 * Unset the appropriate permission bits.
	 */
	for (_cnt = 0; _cnt < cnt; _cnt++, _value1++, _value2++) {
		*_value1 &= ~(*_value2);
		_inuse |= *_value1;
	}

	/*
	 * If there are no longer any permission bits set, free the permission
	 * structure.
	 */
	if (_inuse)
		return (permit1);
	else {
		free(permit1);
		return ((Permit *)0);
	}
}

/*
 * Determine whether we have a secure executable.  Uid and gid information
 * can be passed to us via the aux vector, however if these values are -1
 * then use the appropriate system call to obtain them.
 *
 *  o	If the user is the root they can do anything
 *
 *  o	If the real and effective uid's don't match, or the real and
 *	effective gid's don't match then this is determined to be a `secure'
 *	application.
 *
 * This function is called prior to any dependency processing (see _setup.c).
 * Any secure setting will remain in effect for the life of the process.
 */
void
security(uid_t uid, uid_t euid, gid_t gid, gid_t egid)
{
	if (uid == -1)
		uid = getuid();
	if (uid) {
		if (euid == -1)
			euid = geteuid();
		if (uid != euid)
			rtld_flags |= RT_FL_SECURE;
		else {
			if (gid == -1)
				gid = getgid();
			if (egid == -1)
				egid = getegid();
			if (gid != egid)
				rtld_flags |= RT_FL_SECURE;
		}
	}
}
