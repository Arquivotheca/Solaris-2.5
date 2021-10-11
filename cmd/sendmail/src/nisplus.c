#include "sendmail.h"
# ifdef NISPLUS
#undef NIS /* symbol conflict in nis.h */
#include <rpcsvc/nis.h>
#include <rpcsvc/nislib.h>


#define EN_col(col) zo_data.objdata_u.en_data.en_cols.en_cols_val[(col)].ec_value.ec_value_val
#define COL_NAME(res,i) ((res->objects.objects_val)->TA_data.ta_cols.ta_cols_val)[i].tc_name
#define COL_MAX(res) ((res->objects.objects_val)->TA_data.ta_cols.ta_cols_len)
#define PARTIAL_NAME(x) ((x)[strlen(x) - 1] != '.')

/*
**  NISPLUS_MAP_OPEN -- open nisplus table
*/

bool
nisplus_map_open(map, mode)
	MAP *map;
	int mode;
{
	register char *p;
	char qbuf[MAXLINE + NIS_MAXNAMELEN];
        nis_result *res = NULL;
        u_int objs_len;
        nis_object *obj_ptr;
	int retry_cnt, max_col, i;

	if (tTd(38, 2))
		printf("nisplus_map_open(%s)\n", map->map_file);

	if (mode != O_RDONLY)
	{
		errno = ENODEV;
		return FALSE;
	}

	if (*map->map_file == '\0')
		map->map_file = "mail_aliases.org_dir";

	if (PARTIAL_NAME(map->map_file) &&
	    (map->map_domain == NULL)) {
		/* set default NISPLUS Domain to $m */
		char *nisplus_default_domain();
		map->map_domain = newstr(nisplus_default_domain());
		if (tTd(38, 2))
			printf("nisplus_map_open(%s): using domain %s\n",
				 map->map_file, map->map_domain);

	}
	if (!PARTIAL_NAME(map->map_file))
		map->map_domain = newstr("");

	/* check to see if this map actually exists */
	if (PARTIAL_NAME(map->map_file))
		sprintf(qbuf, "%s.%s", map->map_file, map->map_domain);
	else
		strcpy(qbuf, map->map_file);
	
	retry_cnt = 0;
	while (res == NULL || res->status != NIS_SUCCESS) {

                res = nis_lookup(qbuf, FOLLOW_LINKS);
                switch (res->status) {
                case NIS_SUCCESS:
			break;
                case NIS_TRYAGAIN:
                case NIS_RPCERROR:
                case NIS_NAMEUNREACHABLE:
			if (retry_cnt++ > 4)  {
				errno = EBADR;
				return FALSE;
			}
			else {
                		sleep(2);       /* try not to overwhelm hosed server */
                        	continue;
			}
                default:        /* all other nisplus errors */
#ifdef notdef
			if (!bitset(MF_OPTIONAL, map->map_mflags))
				syserr("Cannot find table %s.%s: %s", 
					map->map_file, map->map_domain, 
					nis_sperrno(res->status));
#endif
			errno = EBADR;
			return FALSE;
                };
        }
 
        if (NIS_RES_NUMOBJ(res) != 1 ||
            (NIS_RES_OBJECT(res)->zo_data.zo_type != TABLE_OBJ)) {
		if (tTd(38, 10))
                        printf("nisplus_map_open: %s is not a table\n", qbuf);
#ifdef notdef
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("%s.%s: %s is not a table", 
				map->map_file, map->map_domain, 
				nis_sperrno(res->status));
#endif
		errno = EBADR;
                return FALSE;
        }
	/* default key column is column 0 */
	if (KEY_COLUMN_NAME(map) == NULL)
		KEY_COLUMN_NAME(map) = newstr(COL_NAME(res,0));

	max_col = COL_MAX(res);
	
	/* verify the key column exist */
	for (i=0; i< max_col; i++)
		if (!strcmp(KEY_COLUMN_NAME(map), COL_NAME(res,i)))
			break;
	if (i == max_col) { /* we did not find the key_column */
		if (tTd(38, 2))
			printf(
		"nisplus_map_open(%s): can not find column %s\n",
			 map->map_file, KEY_COLUMN_NAME(map));
		errno = EBADR;
        	return FALSE; /* con not find key column */
	}

	/* default value column is the last column */
	if (VALUE_COLUMN_NAME(map) == NULL) {
		VALUE_COLUMN_IDX(map) = max_col-1;
		return TRUE;
	}

	for (i=0; i< max_col; i++)
		if (!strcmp(VALUE_COLUMN_NAME(map), COL_NAME(res,i))) {
			VALUE_COLUMN_IDX(map) = i;
        		return TRUE;
		}

	if (tTd(38, 2))
		printf("nisplus_map_open(%s): can not find column %s\n",
			 map->map_file, KEY_COLUMN_NAME(map));
	errno = EBADR;
        return FALSE; /* con not find value column */
}


/*
**  NISPLUS_MAP_LOOKUP -- look up a datum in a NISPLUS table
*/

char *
nisplus_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	char *vp;
	auto int vsize;
	int buflen;
	char search_key[MAXNAME + 1];
	char qbuf[MAXLINE + NIS_MAXNAMELEN];
        nis_result *result;

	if (tTd(38, 20))
		printf("nisplus_map_lookup(%s)\n", name);


	if (!bitset(MF_OPEN, map->map_mflags)) {
		if (nisplus_map_open(map, O_RDONLY))
			map->map_mflags |= MF_OPEN;
		else {
			*statp = EX_UNAVAILABLE;
			return NULL;
		}
	}
		
	buflen = strlen(name);
	if (buflen > sizeof search_key - 1)
		buflen = sizeof search_key - 1;
	bcopy(name, search_key, buflen + 1);
	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
		makelower(search_key);

        /* construct the query */
	if (PARTIAL_NAME(map->map_file))
        	sprintf(qbuf, "[%s=%s],%s.%s", KEY_COLUMN_NAME(map), 
			search_key, map->map_file, map->map_domain);
	else
        	sprintf(qbuf, "[%s=%s],%s", KEY_COLUMN_NAME(map), 
			search_key, map->map_file);

	if (tTd(38, 20))
		printf("qbuf=%s\n", qbuf); 
        result = nis_list(qbuf, FOLLOW_LINKS | FOLLOW_PATH, NULL, NULL);
        if (result->status == NIS_SUCCESS) {
                int count;
		char *str;

        	if ((count = NIS_RES_NUMOBJ(result)) != 1) {
			if (LogLevel > 10)
                        syslog(LOG_WARNING,
                          "%s:Lookup error, expected 1 entry, got (%d)",
                            map->map_file, count);
			/* ignore second entry */
			if (tTd(38, 20))
				printf("nisplus_map_lookup(%s), got %d entries, addtional entries ignores\n", name, count);
                } 

                vp = ((NIS_RES_OBJECT(result))->EN_col(VALUE_COLUMN_IDX(map)));
                /* set the length of the result */
		if (vp == NULL)
			vp = ""; 
                vsize = strlen(vp);
		if (tTd(38, 20))
			printf("nisplus_map_lookup(%s), found %s\n", 
				name, vp);
		if (bitset(MF_MATCHONLY, map->map_mflags))
			str = map_rewrite(map, name, strlen(name), NULL);
		else
			str = map_rewrite(map, vp, vsize, av);
        	nis_freeresult(result);
#ifdef MAP_EXIT_STAT
		*statp = EX_OK;
#endif
		return str;
        } else {
#ifdef MAP_EXIT_STAT
        	if (result->status == NIS_NOTFOUND)
			*statp = EX_NOTFOUND;
        	else if (result->status == NIS_TRYAGAIN)
			*statp = EX_TEMPFAIL;
		else { 
			*statp = EX_UNAVAILABLE;
			map->map_mflags &= ~(MF_VALID|MF_OPEN);
	     	}
#else
		if ((result->status != NIS_NOTFOUND) && 
		    (result->status != NIS_TRYAGAIN))
			map->map_mflags &= ~(MF_VALID|MF_OPEN);
#endif
        }
	if (tTd(38, 20))
		printf("nisplus_map_lookup(%s), failed\n", name);
       	nis_freeresult(result);
	return NULL;
}


/*
**  NISPLUS_MAP_STORE
*/

void
nisplus_map_store(map, lhs, rhs)
	MAP *map;
	char *lhs;
	char *rhs;
{
	/* nothing */
}


/*
**  NISPLUS_MAP_CLOSE
*/

void
nisplus_map_close(map)
	MAP *map;
{
	/* nothing */
}


char *
nisplus_default_domain()
{
	static char default_domain[MAXNAME] = "";
        nis_result *res = NULL;
	char *p;

	if (default_domain[0] != '\0')
		return(default_domain);
	
	/* use nis local directory */
	p = nis_local_directory();
	strcpy(default_domain, p);
	return default_domain;
}
#endif /* NISPLUS */
