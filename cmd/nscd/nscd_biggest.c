/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)nscd_biggest.c	1.1	94/12/05 SMI"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * routines to find largest n numbers, carrying 4 bytes of data
 */

int * maken(int n)
{
	int * ret; 
	
	n++;
	
	ret = (int*) memset(malloc( 2 * n *sizeof(int)), -1, 2*n*sizeof(int));
	ret[0] = n -1 ;
	return(ret);
}

insertn(int * table, int n, int data)
{
	int size = *table;
	int guess,base, last;

	if(table[1] > n)
		return(-1);

	if(table[size] < n)  /* biggest so far */
		guess = size;
	else {	
		base = 1;
		last = size;
		while(last >= base) {
			guess = (last+base)/2;
			if(table[guess] == n)
				goto doit;
			if(table[guess] > n )
				last = guess -1;
			else 
				base = guess + 1;
		}
	 	guess = last;	
	}
    doit:
	memmove(table+1, table+2, sizeof(int) * (guess-1));
	memmove(table+1+size, table+2+size, sizeof(int) * (guess-1));
	table[guess+size] = data;
	table[guess] = n;
	/*
	for(i=0;i<=size;i++)
	  printf("%d ", table[i]);
	  printf("\n");
	*/
	return(0);
}
/*
int
main(int argc, char * argv[])
{
	char buffer[80];
	int i,j;
	int * test = maken(j=atoi(argv[1]));

	while(gets(buffer))
	   {
	  insertn(test,atoi(buffer));}


	for(i=1;i<j;i++)
	    printf("%d ", test[i]);
	printf("\n");
}

main()
{
	long long s, gethrtime();
	int i;
	int *j;
	j = maken(200);
	s=gethrtime();
	for(i=0;i<10000;i++)

		insertn(j,i);
	printf("time %lf seconds\n", (double)(gethrtime()-s)/1.e9);
	exit(0);
	}
*/

