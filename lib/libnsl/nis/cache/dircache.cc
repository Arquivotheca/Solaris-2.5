/*
 *	dircache.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)dircache.cc	1.10	93/03/18 SMI"

/*
 * Ported from SCCS version :
 * "@(#)dircache.cc  1.13  91/03/07  Copyr 1988 Sun Micro";
 *
 * This file contains some of the methods on the DirCache class
 * that is the parent class for both MgrCache, and ClientCache.
 */

#include 	<stdio.h>
#include 	<stdlib.h>
#include 	<sys/types.h>
#include 	<sys/file.h>
#include 	<syslog.h>
#include 	<string.h>

#include 	"dircache.h"

#ifdef TDRPC
#define	FREETYPE (char *)
#else
#define FREETYPE (void *)
#endif

extern "C" int strcasecmp(const char *, const char *);


/* 
 * places the name in the NisCfile strucure into an array.
 * returns total string length 
 */

int
NisCfileEntry :: get_broken_name( char** ret ) 
{
	int 		i;
	register char* 	start = dir_name;
	int 		cache_offset = 0;
	
	for ( i = 0; i < dirLevels; i++) {
		ret[i] = start+cache_offset;
		cache_offset += strlen(start+cache_offset) + 1;
	}
	ret[i] = NULL;
	return(cache_offset);
}



/* returns a name from the broken up name.
 * allocates space for the name and returns that.
 * This should be freed by the calling routine.
 * returns NULL if cannot allocate space.
 */

char* 
NisCfileEntry :: make_name()
{
	int 	namelen;
	int	k;
	char 	*name;
	char	**t;
	char	**tmp;

	if ( (t = (char**) malloc((dirLevels + 1) * (sizeof(char*)))) == NULL)
		return(NULL);
	tmp = t;
	namelen = get_broken_name(t);
	if ( (name = (char*) malloc(namelen + 1)) == NULL) {
		free(FREETYPE t);
		return(NULL);
	}
	// write out dot separated name
	name[0] = NULL;
	for(k = dirLevels, t += (dirLevels - 1); k > 0;  k--) {
		strcat(name, *t--);
		strcat(name, ".");	
	}	
	free(FREETYPE tmp);
	return(name);

}

/*
 * sets the offset to the next entry
 * This includes the size of the header for each entry
 * and the size of the (data portion) of the entry itself
 * aligns the offset properly
 */

void
NisCfileEntry :: set_offset()
{
	offset = __nis_align(dataOffset + size);

}



// sets the offset to the data portion
// aligns the offset properly
void
NisCfileEntry :: set_dataoffset(u_int off)
{
	dataOffset = __nis_align(off);

}



// Gives a pointer to the data portion of this NisCfileEntry 
char* 
NisCfileEntry :: datap()
{
	return ((char*) this + dataOffset);
}



u_long 
NisCfileEntry :: get_ttl()
{
	long 		ttl;
        struct timeval 	now;

	// adjust absolute time to ttl
	getthetime(&now);
	ttl = expTime - now.tv_sec;
	return ( (ttl < 0) ? (u_long)0 : (u_long)ttl );
}


void
NisCfileEntry :: set_ttl(u_long ttl)
{
        struct timeval 	now;

	getthetime(&now);
	// adjust ttl to absolute time
	expTime = ttl + now.tv_sec;
}


/*
 * takes an array of strings representing the broken up name
 * and stores it in the file.
 * returns the length needed to store the dir name
 */
u_int
NisCfileEntry :: set_dirname(char **name, int levels)
{
	int 	i;
	int 	tlen;

	// copy the broken up name into the file.
	for (i = 0, tlen = 0; i < levels; i++) {
		strcpy( (this->dir_name + tlen ), name[i]);
		tlen += strlen(name[i]) + 1;
	}
	dirLevels = levels;
	return (tlen);
}





int
NisCfileEntry :: distance(char **test, int tlevels, bool_t *is_prefix)
{
	char 	*mine;
	int 	dist = 0;
	int 	common = 0;
	int 	cache_offset = 0;
	int 	minLevels = (this->dirLevels > tlevels) ? tlevels: dirLevels;

	// Don't count common components 
	for ( mine = dir_name; minLevels; minLevels--) {
		if (strcasecmp(mine, *test))
			break;
		test++;
		mine += strlen(mine) + 1;
		common++;
	}
	dist = dirLevels + tlevels - common - common;
	if (dirLevels == common) 
		*is_prefix = TRUE;
	else
		*is_prefix = FALSE;

	return dist;
}

bool_t
NisDirCache :: isValid()
{   
	if (isInValid == NULL)
		return (FALSE);
	return ( !(*isInValid) );
}


void 
NisDirCache :: print()
{
	NisCfileEntry 	*cfp;
	int 		i;
	int 		max = *count;
	
	
	if (!isValid() ) {
		syslog(LOG_ALERT, "cachefile corrupted: restart nis_cachemgr: %m");
		return;
	}
	if (__nis_debuglevel) {
		if ((__nis_debuglevel != 6) && (__nis_debuglevel != 1))  {   
			// hack for special format in nisshowcache
			printf("Cache header:\n");
			printf("\tvers:\t%d\n\tmgr_uaddr:\t%s\n\tcache entries:\t%d\n",
			       *version, uaddr, (*count + 1));
			printf("\tfilesize: %d\tused: %d\tfree: %d\n\n", 
			       *mapLen,  *totalSize, (*mapLen - *totalSize));
		}
		if (__nis_debuglevel != 6) 
			printf("Cold Start Directory:\n");	
		getHomeEntry()->print();
		for( i = 0, cfp = (getHomeEntry())->next(); i < max; 
		    i++, cfp = cfp->next()) {
			if (__nis_debuglevel != 6) 
				printf("\nNisDirCacheEntry[%d]:\n", i+1);
			cfp->print();
		}
	}
}




void 
NisCfileEntry :: print()
{
	NisDirCacheEntry 	*cep;
	char 			*tmp[NIS_MAXNAMELEN];

	if (__nis_debuglevel == 1) {
		printf("\tdir_name:");
		get_broken_name(tmp);
		print_dir_name(tmp, dirLevels);
		printf("\n");
		return;
	}

	if (__nis_debuglevel > 2) {
		cep = (NisDirCacheEntry*) calloc( 1, sizeof (NisDirCacheEntry ));
		cep->myConstructor( this );
		
		if (__nis_debuglevel ) {
			cep->print(); 
			cep->myDestructor();
		}
		free ( FREETYPE cep);
	}
}


void 
NisCfileEntry :: printDirName()
{
	int 	i;
	char 	*mine = dir_name;

	if (!__nis_debuglevel)
		return;

	for (i = 0; i < dirLevels; i++) {
		printf(".%s", mine);
		mine += strlen(mine) + 1;
	}
}



void
print_dir_name(char **name, int levels)
{
	int 	i;
	char 	**tmp = name;

	if (!__nis_debuglevel)
		return;
	printf("'");
	for (i = levels - 1; i >= 0; i--) {
		printf("%s.", tmp[i]);
	}
	printf("'");
}
