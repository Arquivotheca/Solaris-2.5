/*
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#define	MAX_ID_LENGTH	256
#define	MAX_BYTES		10
#define	ERROR			-1

#define	RANGE		1
#define	SINGLE		2

/*
 * Symbol table structure
 */
typedef struct	Symbol {
	char	type;
	char	name[MAX_ID_LENGTH];
	int		length;
	unsigned char	value[MAX_BYTES];
	int		range;
	struct Symbol	*next;
} Symbol;

/*
 * Output table header
 */
typedef struct	Header {
	int		mb_cur_max;
	int		mb_cur_min;
	char	escape_char;
	char	comment_char;
	int		num_of_elements;
} Header;
