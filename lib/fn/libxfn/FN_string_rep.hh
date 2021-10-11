/*
 * Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _XFN_FN_STRING_REP_HH
#define	_XFN_FN_STRING_REP_HH

#pragma ident	"@(#)FN_string_rep.hh	1.3	94/08/03 SMI"

/*
 * Alternate code sets may be implemented by subclassing from the
 * FN_string_rep class.
 */

#include <synch.h>

#define DEFAULT_CODESET	PCS_CODESET
#define PCS_CODESET	(0x0001UL)		/* PCS code set */
#define LATIN1_CODESET	(0x0011UL)		/* Latin-1 code set */

/*
 * HAVE_LOCALE set to != 0 turns on "some" locale_info handling.
 * This information is currently not used.
 */

#define HAVE_LOCALE	(0)

class FN_string_rep {
	static int	nnodes;

	mutex_t		ref;
	int		refcnt;

 protected:
	unsigned long	p_code_set;
	unsigned char	*p_native;
	size_t		p_native_bytes;
#if HAVE_LOCALE
	void		*p_locale;
	size_t		p_locale_bytes;
#endif

 public:
	// Alloc size bytes, copy length bytes.
	FN_string_rep(unsigned long code_set);
	FN_string_rep();			// disable default
	FN_string_rep(const FN_string_rep &);	// disable default
	operator=(const FN_string_rep &);	// disable default
	virtual ~FN_string_rep();

	// Manage ref count.
	FN_string_rep *share(void);
	int release(void);

	const unsigned char *as_str(size_t *nbytes = 0);
	void zap_native(void);
#if HAVE_LOCALE
	int set_locale(/*unsigned long code_set,*/
		       const void *locale_info,
		       size_t locale_bytes);
#endif

	virtual valid(void) const = 0;
	// Clone the derived object.
	virtual FN_string_rep *clone(void) const = 0;
	// Clone the derived object, using only selected substring.
	virtual FN_string_rep *clone(unsigned from,
				     size_t charcount,
				     size_t storlen) const = 0;

	virtual void *typetag(void) const = 0;
	virtual const void *contents(void) const = 0;
	virtual unsigned long code_set(const void **linfo,
				       size_t *lbytes) const;
	virtual size_t charcount(void) const = 0;
	// Should include padding bytes.
	// virtual size_t bytecount(void) const = 0;

	// These code set specific string ops have the same compare semantics
	// as the standard routines named by prepended "str".
	virtual int cmp(unsigned from, const FN_string_rep *,
			unsigned int &status) const = 0;
	virtual int casecmp(unsigned from, const FN_string_rep *,
			    unsigned int &status) const = 0;
	virtual int ncmp(unsigned from, const FN_string_rep *, size_t charcount,
			 unsigned int &status)	const = 0;
	virtual int ncasecmp(unsigned from, const FN_string_rep *,
			     size_t charcount, unsigned int& status) const = 0;
	virtual unsigned int cat(const FN_string_rep *s) = 0;
};

/*
 * This implements the standard string ((char *) with '\0' terminator)
 * subclass.  This covers ASCII and Latin-1 characters.
 */

#define CLASS	string_char
#define TYPE	char

class CLASS : public FN_string_rep {
	static unsigned long	nnodes;

	/*
	 * cstorlen, cstorlen are in units of codes
	 */
	TYPE	*cstring;
	size_t	cstorlen;		// storage length (incl. gap and '\0')
	size_t	clength;		// actual used length (excl. '\0')
	int	p_valid;		// structure is valid

 public:
	CLASS();			// diasble default
	CLASS(const CLASS &r);
	operator=(const CLASS &r);	// disable default

	// construct str of length storlen and copy len codes
	// neither len nor storlen should account for '\0' terminator.
	// string should not include '\0' chars.
	CLASS(const TYPE *p, size_t len, size_t storlen, unsigned long code_set);
	~CLASS();

	// narrow scope of pointer.
	static CLASS *narrow(FN_string_rep *);
	static const CLASS *narrowc(const FN_string_rep *);
	static void *s_typetag();
	void *typetag() const;

	valid(void) const;
	FN_string_rep *clone(void) const;
	FN_string_rep *clone(unsigned from, size_t len, size_t storlen) const;

	const void *contents() const;
	size_t charcount() const;
	// size_t bytecount() const;

	int cmp(unsigned from, const FN_string_rep *s,
		unsigned int &status) const;
	int casecmp(unsigned from, const FN_string_rep *s,
		unsigned int &status) const;
	int ncmp(unsigned from, const FN_string_rep *s, size_t len,
		unsigned int &status) const;
	int ncasecmp(unsigned from, const FN_string_rep *s, size_t len,
		unsigned int &status) const;
	unsigned int cat(const FN_string_rep *s);
};

#undef CLASS
#undef TYPE

#include <widec.h>

/*
 * This implements the wide string ((wchar_t *) with '\0' terminator)
 * subclass.
 */

#define CLASS	string_wchar
#define TYPE	wchar_t

class CLASS : public FN_string_rep {
	static unsigned long	nnodes;

	/*
	 * cstorlen, clength are in units of codes
	 */
	TYPE	*cstring;
	size_t	cstorlen;		// storage length (incl. '\0')
	size_t	clength;		// actual used length (excl. '\0')
	int	p_valid;		// structure is valid

 protected:
	static void *s_typetag();

 public:
	CLASS();			// diasble default
	CLASS(const CLASS &r);
	operator=(const CLASS &r);	// disable default

	// construct str of length storlen and copy len codes
	// neither len nor storlen should account for '\0' terminator.
	// string should not include '\0' chars.
	CLASS(const TYPE *p, size_t len, size_t storlen, unsigned long code_set);
	~CLASS();

	// narrow scope of pointer.
	static CLASS *narrow(FN_string_rep *);
	static const CLASS *narrowc(const FN_string_rep *);
	void *typetag() const;

	valid(void) const;
	FN_string_rep *clone(void) const;
	FN_string_rep *clone(unsigned from, size_t len, size_t storlen) const;

	const void *contents() const;
	size_t charcount() const;
	// size_t bytecount() const;

	int cmp(unsigned from, const FN_string_rep *s,
		unsigned int &status) const;
	int casecmp(unsigned from, const FN_string_rep *s,
		unsigned int &status) const;
	int ncmp(unsigned from, const FN_string_rep *s, size_t len,
		unsigned int &status) const;
	int ncasecmp(unsigned from, const FN_string_rep *s, size_t len,
		unsigned int &status) const;
	unsigned int cat(const FN_string_rep *s);
};

#undef CLASS
#undef TYPE

#endif // _XFN_FN_STRING_REP_HH
