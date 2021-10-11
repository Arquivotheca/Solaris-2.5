
#include "ktest.h"
#include <netdb.h>

#define bcopy(a,b,cnt) memcpy(b,a,cnt)
#define bzero(s,cnt) memset(s,0,cnt)

main(argc, argv)
int argc;
char *argv[];
{

	CLIENT * clnt;
	int sock = RPC_ANYSOCK;
	enum clnt_stat stat;
	pass pass1, *pass2;
	char *service;
	char instance[32];
	char host[32];
	char synchost[32];
        extern char *optarg;		/* command-line option string */
	int c;
	int window;
	int loop = 1;
	int i;
	char transport[10]; /* upd /tcp */
	int retval;
	struct hostent *h;
	int usekerbcred = 1;

	/* defaults */
	service = (char *)NULL;
	if (gethostname(host, 32))  {
		printf("gethostname failed\n");
		exit(1);
	}

	strcpy(instance, host);
	window = 60;
	strcpy(transport, "udp");
	*synchost = '\0';

	/* get command-line options */
	while ((c = getopt(argc, argv, "Ns:i:h:n:w:t:x:")) != EOF) {
                switch(c) {
		    case 'N':
			/* use null credentials */
			usekerbcred = 0;
			break;
		    case 's': 
			service = (char *)strdup (optarg);
			break;
		    case 'i':
			strcpy(instance, optarg);
			break;
		    case 'h':
			strcpy(host, optarg);
			break;
		    case 'n':
			loop =  (int) atoi(optarg);
			break;
		    case 'w':
			window =  (int) atoi(optarg);
			break;
		    case 't':
			strcpy(transport, optarg);
			break;
		    case 'x':
			strcpy(synchost, optarg);
			break;

		    case '?':		/* error */
                        usage(1);
                        break;
                }
	}
	if (service == (char *) NULL)
		  usage();

	pass1.cksum = 50;
	pass1.timestamp = 100;
	printf(" Loop = %d\n",loop);
	if ((clnt = clnt_create(host, TEST, TEST_VERS, transport)) == NULL) {
		clnt_pcreateerror(host);
		exit(1);
	}
	printf("contacting service = %s, instance = %s on host = %s\n",
	       service, instance, host);
	if (*synchost) {
		printf("  [using host %s as time sync host]\n", synchost);
	}
	if (usekerbcred) {
	  if ( (clnt->cl_auth = authkerb_seccreate( service, instance,
			(char *) NULL,  window, *synchost? synchost : NULL,
				&retval))
	    == NULL) {
		printf("Could not get kerberos auth client handle: %s\n",
		       retval > 0? krb_err_txt[retval] : "system error");
		exit(1);
	  }
	}
	for (i = 0; i <loop; i++) {
		pass1.cksum = 50 + i;
		sleep(1);
		if ((pass2 = test_proc_1(&pass1, clnt)) == NULL) {
			clnt_perror(clnt,"Call failed");
		}
		else {
			if (pass2->cksum == pass1.cksum -1) 
			  printf("..correct response \n");
			else 
			  printf(" ...wrong response from server\n");
			    
		}
	}
	exit (0);
}




usage()
{
	fprintf(stderr,"\t\tUsage: \n\
			-s service (required) \n\
			-N         (use NULL credentials)\n\
			-h hostname\n\
			-i instance \n\
			-n number\n\
			-w window\n\
			-x synchost\n\
			-t transport\n");
	exit(1);

}
