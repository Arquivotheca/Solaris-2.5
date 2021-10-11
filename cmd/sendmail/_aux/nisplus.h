/*
 * Defines for Nisplus alias handling functions
 */

#define TABLE_TYPE "mail_aliases"

/*
 * Operating modes
 */

typedef enum t_mode 
{ NONE, ADD, CHANGE, DELETE, EDIT, MATCH, LIST, INIT } t_mode;

struct nis_mailias 
{
	char *name;
	char *expn;
	char *comments;
	char *options;
};

typedef struct nis_mailias nis_mailias;

#define FORWARD 0
#define REVERSE 1

#define MAILIAS_COLS  4          /* Number of cols in a mailias entry */
/*
 * These are the the columns in the NIS+ Table.
 */
#define ALIAS_COL     0          /* the name of the alias */
#define EXPANSION_COL 1          /* what the alias expands to */
#define COMMENTS_COL  2          /* Human readable comments */
#define OPTIONS_COL   3          /* Options column,
				  * This consists of a list of
				  * VARIABLE=VALUE, or VARIABLE names
				  */

#define EN_len zo_data.objdata_u.en_data.en_cols.en_cols_len
#define EN_colp zo_data.objdata_u.en_data.en_cols.en_cols_val
#define EN_col_len(col) zo_data.objdata_u.en_data.en_cols.en_cols_val[(col)].ec_value.ec_value_len
#define EN_col_flags(col) zo_data.objdata_u.en_data.en_cols.en_cols_val[(col)].ec_flags
#define EN_col(col) zo_data.objdata_u.en_data.en_cols.en_cols_val[(col)].ec_value.ec_value_val

/* Macros which extract the Alias, Expansion, Comments, or Options column
 * of an nis alias table object
 */
#define ALIAS(obj) ((obj)->EN_col(ALIAS_COL))
#define EXPN(obj) ((obj)->EN_col(EXPANSION_COL))
#define COMMENTS(obj) ((obj)->EN_col(COMMENTS_COL))
#define OPTIONS(obj) ((obj)->EN_col(OPTIONS_COL))

#define TA_val(col) zo_data.objdata_u.ta_data.ta_cols.ta_cols_val[(col)]

extern nis_result *nis_mailias_match(char *name, nis_name map, 
				     nis_name domain, int qtype);

extern nis_object *mailias_make_entry(struct nis_mailias a, 
				      nis_name map, nis_name domain);

extern nis_object *mailias_make_table(nis_name name, nis_name domain);

extern void *mailias_parse_file(FILE *fp, nis_name map, nis_name domain);

extern int print_comments;  /* Tells us whether to print comments and OPTIONS*/
int print_comments;
