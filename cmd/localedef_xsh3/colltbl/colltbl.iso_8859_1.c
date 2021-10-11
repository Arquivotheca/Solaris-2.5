#include <stdio.h>
#include <widec.h>

size_t
_strcoll_(s1, s2)
 char *s1;
 char *s2;
{
	if (s1 == s2)
		return (0);

	for(; *s1 == *s2; s1++, s2++)
		if (*s1 == 0)
			return (0);
	return ((unsigned char) *s1 - (unsigned char) *s2);
}



size_t
_wscoll_(s1, s2)
 wchar_t *s1, *s2;
{
        if (s1 == s2)
                return (0);
	for(; *s1 == *s2; s1++, s2++)
		if (*s1 == 0)
			return (0);
	return (*s1 - *s2);
	
}


size_t
_strxfrm_(char *s1, const char *s2, size_t n)
{
	int	len2=strlen(s2);

	if (!s1){ /* Inquery of the needed size of the output buffer. */
		return (len2+1); /* '+1' for NUL. */
	}else{ /* Just copy s2 to s2 and return the # of bytes copied. */
		strncpy(s1, s2, n);
		return ((len2>n)?n:len2);
	}
}

size_t _wsxfrm_(wchar_t *s1, const wchar_t *s2, size_t n)
{
	int	len2=wslen(s2);

	if (!s1){/* Inquery of the needed size of the output buffer. */
		return (len2+1); /* '+1' for NUL. */
	}else{/* Just copy s2 to s2 and return the # of chars copied. */
		wsncpy(s1, s2, n);
		return (len2>n)?n:len2;
	}
}

