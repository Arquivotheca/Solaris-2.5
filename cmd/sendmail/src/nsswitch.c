
#include "sendmail.h"
#ifdef NSSWITCH
#include <nsswitch.h>
#include "ns_switch.h"

static struct __nsw_lookup lkp0 =  { "files", DEF_ACTION, NULL, NULL};
static struct __nsw_switchconfig lkp_default = { 0, __NSW_SENDMAIL_DB , 3, &lkp0};

bool
nsswitch_map_open(map, mode)
        MAP *map;
        int mode;
{
	if (tTd(38, 2))
		printf("nsswitch_map_open(%s)\n", map->map_file);

	if (mode != O_RDONLY)
	{
		errno = ENODEV;
		return FALSE;
        }

	return TRUE;
}

char *
nsswitch_map_lookup(map, name, av, statp)
        MAP *map;
        char *name;
        char **av;
        int *statp;
{
	register MAP *mymap;
	register char *vp;
	char *str;
	STAB *s;
	u_short selection_flags;
	int i;
	bool domain_wide_only = FALSE;
	bool test_domain_wide_only = FALSE;
	char *myname;

	/* in solaris, lookup path is defined */
	/* in /etc/nsswitch.conf	      */

	struct __nsw_switchconfig *nsw_conf;
	enum __nsw_parse_err pserr;
	struct __nsw_lookup *lk;
	int nsw_rc;
	char *info_type;

	if (tTd(38, 1)) {
                printf("nsswitch_map_lookup(%s) against %s, \n", name, map->map_file);
	}

	if (av) {
		for (i = 0; av[i]; i++) {
			if (av[i][0] == '-') {
				if (av[i][1] == 'D')
					test_domain_wide_only = TRUE;
				else if (av[i][1] == 'd')
					domain_wide_only = TRUE; 
			}
		}
	}

	if (INFO_TYPE(map) == NULL)
		info_type = INFO_TYPE_DEFAULT;
	else	info_type = INFO_TYPE(map);

	if ((nsw_conf = __nsw_getconfig(info_type, &pserr)) == NULL)
                lk = lkp_default.lookups;
	else	lk = nsw_conf->lookups;


	*statp = EX_NOTFOUND;
	vp = NULL;
	if (!strncmp("Alias", map->map_mname, strlen("Alias")))
		myname ="aliases";
	else	myname = map->map_mname;
	while (!vp && lk) { 
		char map_name[MAXNAME];

		sprintf(map_name, "%s%c%s", myname, 
			 NSW_DELIMITER,lk->service_name);
		s = stab(map_name, ST_MAP, ST_FIND);
		if (s == NULL) {
			if (tTd(38, 1))
                		printf(
		"nsswitch_map_lookup(%s) can not found %s\n", map_name);
			*statp = EX_UNAVAILABLE;
		} else {
			mymap = &s->s_map;
			/* see if we need to skip table */
			if (domain_wide_only && 
		    	    !(mymap->map_mflags & MF_DOMAIN_WIDE) ) {
				/* skip local table  */
				if (tTd(38, 1))
					printf(
			"nsswitch_map_lookup(%s): skipping \"%s\"\n",
                                		name, lk->service_name);
				lk = lk->next;
				continue;
			}
			if (!bitset(MF_OPEN, mymap->map_mflags)) {
				*statp = EX_UNAVAILABLE;
			} else {	if (tTd(38, 1))
					printf(
			"nsswitch_map_lookup(%s): looking up via \"%s\"\n",
                                		name, lk->service_name);
				vp = mymap->map_class->map_lookup(mymap, name, NULL, statp);
				nsw_rc = translate(*statp);
			}
		} 
		if (__NSW_ACTION(lk, nsw_rc) == __NSW_RETURN)
			break;
		lk = lk->next;
	}

	if (test_domain_wide_only) {
		if (nsw_rc == __NSW_SUCCESS) {
			if (mymap->map_mflags & MF_DOMAIN_WIDE)
				vp = "D";
			else 	vp = "L";
		 } else if (nsw_rc == __NSW_NOTFOUND)
			vp = "N";
		else	vp = "U";
		str = map_rewrite(map, vp, strlen(vp), av);
		return str;
	}

	if (nsw_rc  == __NSW_SUCCESS) {
		if (bitset(MF_MATCHONLY, map->map_mflags))
			str = map_rewrite(map, name, strlen(name), NULL);
		else {	
			char *ptr;

			ptr = newstr(vp);
			str = map_rewrite(map, ptr, strlen(vp), av);
			free(ptr);
		}

		return str;
	}
	if (nsw_rc  == __NSW_UNAVAIL) {
		if (tTd(38, 1))
			printf("nsswitch_map_lookup(%s): can't access \"%s\" tables\n",
                                name, lk->service_name);

		if (LogLevel >= 7)
                       syslog(LOG_WARNING,
                            "can't access \"%s\" tables",
                            lk->service_name);
	}

	return NULL;
}

/*
**  NSSWITCH_MAP_STORE
*/

void
nsswitch_map_store(map, lhs, rhs)
        MAP *map;
        char *lhs;
        char *rhs;
{
	struct __nsw_switchconfig *nsw_conf;
	enum __nsw_parse_err pserr;
	struct __nsw_lookup *lk;
	register MAP *mymap;
	STAB *s;

	if (tTd(38, 1))
                printf("nsswitch_map_store(%s):%s, %s\n", 
		    map->map_file, lhs, rhs);


	if ((nsw_conf = __nsw_getconfig(map->map_file, &pserr)) == NULL)
                nsw_conf = &lkp_default;
	lk = nsw_conf->lookups;


	while (lk) { 
		char map_name[MAXNAME];

		sprintf(map_name, "%s%c%s", map->map_file, 
			 NSW_DELIMITER,lk->service_name);
		s = stab(map_name, ST_MAP, ST_FIND);
		if (s != NULL) {
			mymap = &s->s_map;
			mymap->map_class->map_store(mymap, lhs, rhs);
		}
		lk = lk->next;
	}
}


/*
**  NSSWITCH_MAP_CLOSE
*/

void
nsswitch_map_close(map)
        MAP *map;
{
        /* nothing */
}


void 
name_to_infotype_source(name, info_type, source)
char *name;
char *info_type;
char *source;
{
	char *p;
	int len;
	char switch_map_name[MAXNAME];
	STAB *s;
	MAP *switch_map;

	info_type[0] = '\0';
	if (name == NULL)
		return;
	if ((p = strchr(name, NSW_DELIMITER)) == NULL) 
		return;
	strcpy(source, &p[1]);
	len = p - name;
	strncpy(switch_map_name, name, len);
	switch_map_name[len] = '\0';

	s = stab(switch_map_name, ST_MAP, ST_FIND);
	if  (s == NULL)
		return;

	switch_map = &s->s_map;
	if (INFO_TYPE(switch_map) == NULL)
		strcpy(info_type, INFO_TYPE_DEFAULT);
	else	strcpy(info_type, INFO_TYPE(switch_map));
}

bool
in_nsswitch_path(info_type, source)
char *info_type, *source;
{
	char *p;
	struct __nsw_switchconfig *nsw_conf;
	enum __nsw_parse_err pserr;
	struct __nsw_lookup *lk;
	bool found;

	if ((info_type == NULL) || (info_type[0] == '\0'))
		return FALSE;
	if ((source == NULL) || (source[0] == '\0'))
		return FALSE;
	
	if ((nsw_conf = __nsw_getconfig(info_type, &pserr)) == NULL)
                nsw_conf = &lkp_default;
	lk = nsw_conf->lookups;

	found = FALSE;
	while (lk) {
 		if (!strcmp(source, lk->service_name)) {
			found = TRUE;
			break;
		}
		lk = lk->next;
	}
	return found;
}

translate(stat)
int stat;
{
	switch (stat) {
	case EX_OK:
		return __NSW_SUCCESS;
	case EX_NOTFOUND: 
		return __NSW_NOTFOUND;
	case EX_TEMPFAIL:
		return __NSW_TRYAGAIN;
	deafult:
		return __NSW_UNAVAIL;
	}
}


ns_rebuildaliases(map, automatic)
MAP *map;
bool automatic;
{
	char *info_type;
	struct __nsw_switchconfig *nsw_conf;
	enum __nsw_parse_err pserr;
	struct __nsw_lookup *lk;
	STAB *s;
	MAP *mymap;
	bool is_ns_switched();


	if (strcmp("nsswitch", map->map_class->map_cname)) {
		if (!is_ns_switched(map->map_mname))
			rebuildaliases(map, FALSE);
		return;
	}

	if (INFO_TYPE(map) == NULL)
		info_type = INFO_TYPE_DEFAULT;
	else	info_type = INFO_TYPE(map);

	if ((nsw_conf = __nsw_getconfig(info_type, &pserr)) == NULL)
                lk = lkp_default.lookups;
	else	lk = nsw_conf->lookups;


	/* only rebuild aliases if it is in the path */
	while (lk) { 
		char map_name[MAXNAME];

		sprintf(map_name, "%s%c%s", map->map_file, 
			 NSW_DELIMITER,lk->service_name);
		s = stab(map_name, ST_MAP, ST_FIND);
		if (s != NULL) {
			mymap = &s->s_map;
			mymap->map_mflags |= MF_ALIAS;
			rebuildaliases(mymap, automatic);
			
		} 
		lk = lk->next;
	}
}


ns_set_aliases_flag(map)
MAP *map;
{
	char *info_type;
	struct __nsw_switchconfig *nsw_conf;
	enum __nsw_parse_err pserr;
	struct __nsw_lookup *lk;
	STAB *s;
	MAP *mymap;


	if (strcmp("nsswitch", map->map_class->map_cname)) {
		return;
	}

	if (INFO_TYPE(map) == NULL)
		info_type = INFO_TYPE_DEFAULT;
	else	info_type = INFO_TYPE(map);

	if ((nsw_conf = __nsw_getconfig(info_type, &pserr)) == NULL)
                lk = lkp_default.lookups;
	else	lk = nsw_conf->lookups;

	while (lk) { 
		char map_name[MAXNAME];

		sprintf(map_name, "%s%c%s", map->map_file, 
			 NSW_DELIMITER,lk->service_name);
		s = stab(map_name, ST_MAP, ST_FIND);
		if (s != NULL) {
			mymap = &s->s_map;
			mymap->map_mflags |= MF_ALIAS;
		} 
		lk = lk->next;
	}
}


/* retrun ture if mapname is switched via the nsswitch path */
bool
is_ns_switched(mapname)
char *mapname;
{
	char info_type[MAXNAME];
	char source[MAXNAME];

	if (strchr(mapname, NSW_DELIMITER))
		return TRUE;
	return FALSE;
}


/* retrun ture if mapname is switch off in the nsswitch path */
bool
ns_switched_off(mapname)
char *mapname;
{
	char info_type[MAXNAME];
	char source[MAXNAME];

        name_to_infotype_source(mapname, info_type, source);
        if ((info_type[0] != '\0')  &&
	     !in_nsswitch_path(info_type, source))
		return TRUE;
	else	return FALSE;

}

#endif /*NSSWITCH*/
