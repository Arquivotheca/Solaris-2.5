/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)fnutils.cc	1.3	94/08/13 SMI"

#include <rpcsvc/nis.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define	HOST_TEMP_FILE "/tmp/hx_nis_hosts"
#define	USER_TEMP_FILE "/tmp/hx_nis_users"

// Return 1 if error encountered; 0 if OK
static int
print_user_entry(char *, nis_object *ent, void *udata)
{
	FILE *outfile = (FILE *)udata;
	long entry_type, t;

	// extract user name from entry
	t = *(long *)
	    (ent->EN_data.en_cols.en_cols_val[0].ec_value.ec_value_val);
	entry_type = ntohl(t);

	if (entry_type == ENTRY_OBJ) {
		fprintf(stderr, "Encountered object that is not an entry\n");
		return (1);
	}

	// print out user name
	fprintf(outfile, "%s\n", ENTRY_VAL(ent, 0));
	return (0);
}


// Return 1 if error encountered; 0 if OK
static int
print_host_entry(char *, nis_object *ent, void *udata)
{
	FILE *outfile = (FILE *) udata;
	long entry_type, t;

	// extract host name from entry
	t = *(long *)
	    (ent->EN_data.en_cols.en_cols_val[0].ec_value.ec_value_val);
	entry_type = ntohl(t);

	if (entry_type == ENTRY_OBJ) {
		fprintf(stderr, "Encountered object that is not an entry\n");
		return (1);
	}

	// print out canonical host name, host name pair
	fprintf(outfile, "%s %s\n", ENTRY_VAL(ent, 0), ENTRY_VAL(ent, 1));
	return (0);
}

FILE *
get_user_file(const char *program_name, const char *domainname)
{
	unsigned nisflags = 0;   // FOLLOW_LINKS?
	nis_result* res = 0;
	char tname[NIS_MAXNAMELEN+1];
	FILE *userfile;

	unlink(USER_TEMP_FILE);
	userfile = fopen(USER_TEMP_FILE, "w");
	if (userfile == NULL) {
		fprintf(stderr, "%s: could not open file %s for write\n",
			program_name, USER_TEMP_FILE);
		return (NULL);
	}

	sprintf(tname, "passwd.org_dir.%s", domainname);
	if (tname[strlen(tname)-1] != '.')
		strcat(tname, ".");

	res = nis_list(tname, nisflags, print_user_entry, (void *)userfile);
	if ((res->status != NIS_CBRESULTS) &&
	    (res->status != NIS_NOTFOUND)) {
		nis_perror(res->status, "can't list table");
	}
	nis_freeresult(res);
	fclose(userfile);

	// open file for read
	if ((userfile = fopen(USER_TEMP_FILE, "r")) == NULL) {
		fprintf(stderr, "%s: could not open file %s for read\n",
			program_name, USER_TEMP_FILE);
		return (NULL);
	}
	return (userfile);
}

FILE *
get_host_file(const char *program_name, const char *domainname)
{
	FILE *hostfile;
	char tname[NIS_MAXNAMELEN+1];
	unsigned nisflags = 0;   // FOLLOW_LINKS?
	nis_result* res = 0;

	unlink(HOST_TEMP_FILE);
	hostfile = fopen(HOST_TEMP_FILE, "w");
	if (hostfile == NULL) {
		fprintf(stderr, "%s: could not open file %s for write\n",
			program_name, HOST_TEMP_FILE);
		return (NULL);
	}

	sprintf(tname, "hosts.org_dir.%s", domainname);
	if (tname[strlen(tname)-1] != '.')
		strcat(tname, ".");

	res = nis_list(tname, nisflags, print_host_entry, (void *)hostfile);
	if ((res->status != NIS_CBRESULTS) &&
	    (res->status != NIS_NOTFOUND)) {
		nis_perror(res->status, "can't list table");
	}
	nis_freeresult(res);
	fclose(hostfile);

	// open file for read
	if ((hostfile = fopen(HOST_TEMP_FILE, "r")) == NULL) {
		fprintf(stderr, "%s: could not open file %s for read\n",
			program_name, HOST_TEMP_FILE);
		return (NULL);
	}
	return (hostfile);
}

void
free_host_file(FILE *hostfile)
{
	fclose(hostfile);
	unlink(HOST_TEMP_FILE);
}

void
free_user_file(FILE *userfile)
{
	fclose(userfile);
	unlink(USER_TEMP_FILE);
}
