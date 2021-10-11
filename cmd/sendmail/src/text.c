#include "sendmail.h"
# ifdef TEXT
#include <sys/types.h>
#include <sys/stat.h>

/*
**  TEXT_MAP_OPEN -- open text table
*/

bool
text_map_open(map, mode)
	MAP *map;
	int mode;
{
	struct stat sbuf;

	if (tTd(38, 2))
		printf("text_map_open(%s)\n", map->map_file);

	if (mode != O_RDONLY)
	{
		errno = ENODEV;
		return FALSE;
	}

	if (*map->map_file == '\0')
		map->map_file = "/etc/mail/domain_aliases";

	if (map->map_file[0] != '/') {
		if (tTd(38, 2))
			printf(
                "text_map_open(%s): file name must be fully qualified\n",
		    	  map->map_file);
		return FALSE;
	}
	/* check to see if this map actually accessable */
	if (access(map->map_file, R_OK) <0) 
        	return FALSE; 
	/* check to see if this map actually exist */
	if (stat(map->map_file, &sbuf) <0) {
		if (tTd(38, 2))
			printf("text_map_open(%s): can not stat %s\n",
		    	  map->map_file, map->map_file);
        	return FALSE; 
	}
	
	if ((sbuf.st_mode & S_IFMT) != S_IFREG) {
		if (tTd(38, 2))
			printf("text_map_open(%s): %s is not a file\n",
		    	  map->map_file, map->map_file);
        	return FALSE; 
	}

	if (KEY_COLUMN_NAME(map) == NULL)
		KEY_COLUMN_IDX(map) = 0; 
	else {
		if (!isdigit(*((char *)KEY_COLUMN_NAME(map)))) {
			if (tTd(38, 2))
				printf(
	       "text_map_open(%s): -k should specify a number, not %s\n",
		    	     	    map->map_file, KEY_COLUMN_NAME(map));
			return FALSE;
		}
		KEY_COLUMN_IDX(map) = atoi((char *)KEY_COLUMN_NAME(map));
	}

	if (VALUE_COLUMN_NAME(map) == NULL)
		VALUE_COLUMN_IDX(map) = 0;
	else {
		if (!isdigit(*((char *)VALUE_COLUMN_NAME(map)))) {
			if (tTd(38, 2))
				printf(
	      "text_map_open(%s): -v should specify a number, not %s\n",
		    	  	  map->map_file, VALUE_COLUMN_NAME(map));
			return FALSE;
		}
		VALUE_COLUMN_IDX(map) = atoi((char *)VALUE_COLUMN_NAME(map));
	}

	if (MAP_DELIMITER(map) == '\0') {
		MAP_DELIMITER(map) = ':';
	}
	if (tTd(38, 2)) {
		printf("text_map_open(%s): delimiter = %c\n", 
		        map->map_file, MAP_DELIMITER(map));
	}
	return TRUE;
}

/*
**  TEXT_MAP_LOOKUP -- look up a datum in a TEXT table
*/

char *
text_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	char *vp;
	auto int vsize;
	int buflen;
	char search_key[MAXNAME + 1];
	char linebuf[MAXLINE];
	FILE *f;
	char buf[MAXNAME+1];
	char delim;
	int key_idx;
	bool found_it;
	extern char *get_column();


	found_it = FALSE;
	if (tTd(38, 20))
		printf("text_map_lookup(%s)\n", name);

	buflen = strlen(name);
	if (buflen > sizeof search_key - 1)
		buflen = sizeof search_key - 1;
	bcopy(name, search_key, buflen + 1);
	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
		makelower(search_key);

	
	f=fopen(map->map_file, "r");
	if (f == NULL) {
		map->map_mflags &= ~(MF_VALID|MF_OPEN);
#ifdef MAP_EXIT_STAT
		*statp = EX_UNAVAILABLE;
#endif
		return NULL;
	}
	key_idx = KEY_COLUMN_IDX(map);
	delim = MAP_DELIMITER(map);
	while (fgets(linebuf, MAXLINE, f)) {
		char *lf;
		if (linebuf[0] == '#')
			continue; /* skip comment line */
		if (lf = strchr(linebuf, '\n'))
			*lf = '\0';
		if (!strcasecmp(search_key,
			get_column(linebuf, key_idx, delim, buf))) {
			found_it = TRUE;
			break; /* found it */
		}
	}
	fclose(f); 
	if (!found_it) {
#ifdef MAP_EXIT_STAT
		*statp = EX_NOTFOUND;
#endif
		return(NULL);
	}
	vp = get_column(linebuf, VALUE_COLUMN_IDX(map), delim, buf);
	vsize = strlen(vp);
#ifdef MAP_EXIT_STAT
	*statp = EX_OK;
#endif
        if (bitset(MF_MATCHONLY, map->map_mflags))
                return map_rewrite(map, name, strlen(name), NULL);
        else
                return map_rewrite(map, vp, vsize, av);

}


/*
**  TEXT_MAP_STORE
*/

void
text_map_store(map, lhs, rhs)
	MAP *map;
	char *lhs;
	char *rhs;
{
	/* nothing */
}


/*
**  TEXT_MAP_CLOSE
*/

void
text_map_close(map)
	MAP *map;
{
	/* nothing */
}

#endif /* TEXT */
