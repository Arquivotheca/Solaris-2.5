/* Copyright 1990 Sun Microsystems. Inc */

#pragma ident	"@(#)printgprof.c	1.8	93/06/07 SMI"

/* - - - - -- - - - -- - - - - - - - - - - - - - - - - - - - - -*/

#include "gprof.h"

extern int find_run_directory(char *, char *, char *, char **, char *);
extern char * getenv(char *);

#include <ctype.h>
#include <string.h>
#include <sys/param.h>

#ifdef SPARC
extern char * demangle();
#endif

char *strstr();
char *parsename();
char name_buffer[512];

printprof()
{
    register nltype	*np;
    nltype		**sortednlp;
    int			index;
    int 		print_count = number_funcs_toprint ;
    bool		print_flag = TRUE ;

    actime = 0.0;
    printf( "\f\n" );
    flatprofheader();
	/*
	 *	Sort the symbol table in by time
	 */
    sortednlp = (nltype **) calloc( nname , sizeof(nltype *) );
    if ( sortednlp == (nltype **) 0 ) {
	fprintf( stderr , "[printprof] ran out of memory for time sorting\n" );
    }
    for ( index = 0 ; index < nname ; index += 1 ) {
	sortednlp[ index ] = &nl[ index ];
    }
    qsort( sortednlp , nname , sizeof(nltype *) , timecmp );
    for ( index = 0 ; index < nname && print_flag ; index += 1 ) {
	np = sortednlp[ index ];
	flatprofline( np );
	if (nflag) {
	   if (--print_count == 0)
		print_flag = FALSE ;
	}
    }
    actime = 0.0;
    cfree( sortednlp );
}

timecmp( npp1 , npp2 )
    nltype **npp1, **npp2;
{
    double	timediff;
    long	calldiff;

    timediff = (*npp2) -> time - (*npp1) -> time;
    if ( timediff > 0.0 )
	return 1 ;
    if ( timediff < 0.0 )
	return -1;
    calldiff = (*npp2) -> ncall - (*npp1) -> ncall;
    if ( calldiff > 0 )
	return 1;
    if ( calldiff < 0 )
	return -1;
    return( strcmp( (*npp1) -> name , (*npp2) -> name ) );
}

    /*
     *	header for flatprofline
     */
flatprofheader()
{
    
    if ( bflag ) {
	printblurb( FLAT_BLURB );
    }
    printf( "\ngranularity: each sample hit covers %d byte(s)" ,
	    (long) scale * sizeof(UNIT) );
    if ( totime > 0.0 ) {
	printf( " for %.2f%% of %.2f seconds\n\n" ,
		100.0/totime , totime / hz );
    } else {
	printf( " no time accumulated\n\n" );
	    /*
	     *	this doesn't hurt sinc eall the numerators will be zero.
	     */
	totime = 1.0;
    }
    printf( "%5.5s %10.10s %8.8s %8.8s %8.8s %8.8s %-8.8s\n" ,
	    "% ", "cumulative", "self ", "", "self ", "total ", "");
    printf( "%5.5s %10.10s %8.8s %8.8s %8.8s %8.8s %-8.8s\n" ,
	    "time" , "seconds " , "seconds" , "calls" ,
	    "ms/call" , "ms/call" , "name" );
}

flatprofline( np )
    register nltype	*np;
{
    if ( zflag == 0 && np -> ncall == 0 && np -> time == 0 ) {
	return;
    }
    actime += np -> time;
    printf( "%5.1f %10.2f %8.2f" ,
	100 * np -> time / totime , actime / hz , np -> time / hz );
    if ( np -> ncall != 0 ) {
	printf( " %8d %8.2f %8.2f  " , np -> ncall ,
	    1000 * np -> time / hz / np -> ncall ,
	    1000 * ( np -> time + np -> childtime ) / hz / np -> ncall );
    } else {
#ifdef SPARC
	if (!Cflag)
#endif
	    printf( " %8.8s %8.8s %8.8s ", "", "", "" );
#ifdef SPARC
	else
	    printf( " %8.8s %8.8s %8.8s  ", "", "", "" );
#endif
    }
    printname( np );
#ifdef SPARC
    if (Cflag)
	print_demangled_name(55,np);
#endif
    printf( "\n" );
}

gprofheader()
{

    if ( bflag ) {
	printblurb( CALLG_BLURB );
    }
    printf( "\ngranularity: each sample hit covers %d byte(s)" ,
	    (long) scale * sizeof(UNIT) );
    if ( printtime > 0.0 ) {
	printf( " for %.2f%% of %.2f seconds\n\n" ,
		100.0/printtime , printtime / hz );
    } else {
	printf( " no time propagated\n\n" );
	    /*
	     *	this doesn't hurt, since all the numerators will be 0.0
	     */
	printtime = 1.0;
    }
    printf( "%6.6s %5.5s %7.7s %11.11s %7.7s/%-7.7s     %-8.8s\n" ,
	"" , "" , "" , "" , "called" , "total" , "parents" , "" );
    printf( "%-6.6s %5.5s %7.7s %11.11s %7.7s+%-7.7s %-8.8s\t%5.5s\n" ,
	"index" , "%time" , "self" , "descendents" ,
	"called" , "self" , "name" , "index" );
    printf( "%6.6s %5.5s %7.7s %11.11s %7.7s/%-7.7s     %-8.8s\n" ,
	"" , "" , "" , "" , "called" , "total" , "children" , "" );
    printf( "\n" );
}

gprofline( np )
    register nltype	*np;
{
    char	kirkbuffer[ BUFSIZ ];

    sprintf( kirkbuffer , "[%d]" , np -> index );
    printf( "%-6.6s %5.1f %7.2f %11.2f" ,
	    kirkbuffer ,
	    100 * ( np -> propself + np -> propchild ) / printtime ,
	    np -> propself / hz ,
	    np -> propchild / hz );
    if ( ( np -> ncall + np -> selfcalls ) != 0 ) {
	printf( " %7d" , np -> ncall );
	if ( np -> selfcalls != 0 ) {
	    printf( "+%-7d " , np -> selfcalls );
	} else {
	    printf( " %7.7s " , "" );
	}
    } else {
	printf( " %7.7s %7.7s " , "" , "" );
    }
    printname( np );
#ifdef SPARC
    if (Cflag)
	print_demangled_name(50, np);
#endif
    printf( "\n" );
}

printgprof(timesortnlp)
    nltype	**timesortnlp;
{
    int		index;
    nltype	*parentp;
    int 	print_count = number_funcs_toprint ;
    bool	count_flag = TRUE ;

	/*
	 *	Print out the structured profiling list
	 */
    gprofheader();
    for ( index = 0; index < nname + ncycle && count_flag ; index ++ ) {
	parentp = timesortnlp[ index ];
	if ( zflag == 0 &&
	     parentp -> ncall == 0 &&
	     parentp -> selfcalls == 0 &&
	     parentp -> propself == 0 &&
	     parentp -> propchild == 0 ) {
	    continue;
	}
	if ( ! parentp -> printflag ) {
	    continue;
	}
	if ( parentp -> name == 0 && parentp -> cycleno != 0 ) {
		/*
		 *	cycle header
		 */
	    printcycle( parentp );
	    printmembers( parentp );
	} else {
	    printparents( parentp );
	    gprofline( parentp );
	    printchildren( parentp );
	}
	printf( "\n" );
	printf( "-----------------------------------------------\n" );
	printf( "\n" );
	if (nflag) {
		--print_count;
		if (print_count == 0)
			count_flag = FALSE;
	}
    }
    cfree( timesortnlp );
}

    /*
     *	sort by decreasing propagated time
     *	if times are equal, but one is a cycle header,
     *		say that's first (e.g. less, i.e. -1).
     *	if one's name doesn't have an underscore and the other does,
     *		say the one is first.
     *	all else being equal, sort by names.
     */
int
totalcmp( npp1 , npp2 )
    nltype	**npp1;
    nltype	**npp2;
{
    register nltype	*np1 = *npp1;
    register nltype	*np2 = *npp2;
    double		diff;

    diff =    ( np1 -> propself + np1 -> propchild )
	    - ( np2 -> propself + np2 -> propchild );
    if ( diff < 0.0 )
	    return 1;
    if ( diff > 0.0 )
	    return -1;
    if ( np1 -> name == 0 && np1 -> cycleno != 0 ) 
	return -1;
    if ( np2 -> name == 0 && np2 -> cycleno != 0 )
	return 1;
    if ( np1 -> name == 0 )
	return -1;
    if ( np2 -> name == 0 )
	return 1;
    if ( *(np1 -> name) != '_' && *(np2 -> name) == '_' )
	return -1;
    if ( *(np1 -> name) == '_' && *(np2 -> name) != '_' )
	return 1;
    if ( np1 -> ncall > np2 -> ncall )
	return -1;
    if ( np1 -> ncall < np2 -> ncall ) 
	return 1;
    return strcmp( np1 -> name , np2 -> name );
}

printparents( childp )
    nltype	*childp;
{
    nltype	*parentp;
    arctype	*arcp;
    nltype	*cycleheadp;

    if ( childp -> cyclehead != 0 ) {
	cycleheadp = childp -> cyclehead;
    } else {
	cycleheadp = childp;
    }
    if ( childp -> parents == 0 ) {
	printf( "%6.6s %5.5s %7.7s %11.11s %7.7s %7.7s     <spontaneous>\n" ,
		"" , "" , "" , "" , "" , "" );
	return;
    }
    sortparents( childp );
    for ( arcp = childp -> parents ; arcp ; arcp = arcp -> arc_parentlist ) {
	parentp = arcp -> arc_parentp;
	if ( childp == parentp ||
	     ( childp->cycleno != 0 && parentp->cycleno == childp->cycleno ) ) {
		/*
		 *	selfcall or call among siblings
		 */
	    printf( "%6.6s %5.5s %7.7s %11.11s %7d %7.7s     " ,
		    "" , "" , "" , "" ,
		    arcp -> arc_count , "" );
	    printname( parentp );
#ifdef SPARC
	    if (Cflag)
		print_demangled_name(54, parentp);
#endif
	    printf( "\n" );
	} else {
		/*
		 *	regular parent of child
		 */
	    printf( "%6.6s %5.5s %7.2f %11.2f %7d/%-7d     " ,
		    "" , "" ,
		    arcp -> arc_time / hz , arcp -> arc_childtime / hz ,
		    arcp -> arc_count , cycleheadp -> ncall );
	    printname( parentp );
#ifdef SPARC
	    if (Cflag)
		print_demangled_name(54, parentp);
#endif
	    printf( "\n" );
	}
    }
}

printchildren( parentp )
    nltype	*parentp;
{
    nltype	*childp;
    arctype	*arcp;

    sortchildren( parentp );
    arcp = parentp -> children;
    for ( arcp = parentp -> children ; arcp ; arcp = arcp -> arc_childlist ) {
	childp = arcp -> arc_childp;
	if ( childp == parentp ||
	    ( childp->cycleno != 0 && childp->cycleno == parentp->cycleno ) ) {
		/*
		 *	self call or call to sibling
		 */
	    printf( "%6.6s %5.5s %7.7s %11.11s %7d %7.7s     " ,
		    "" , "" , "" , "" , arcp -> arc_count , "" );
	    printname( childp );
#ifdef SPARC
	    if (Cflag)
		print_demangled_name(54, childp);
#endif
	    printf( "\n" );
	} else {
		/*
		 *	regular child of parent
		 */
	    if (childp -> cyclehead)
	    	printf( "%6.6s %5.5s %7.2f %11.2f %7d/%-7d     " ,
		    "" , "" ,
		    arcp -> arc_time / hz , arcp -> arc_childtime / hz ,
		    arcp -> arc_count , childp -> cyclehead -> ncall );
	    else
		printf( "%6.6s %5.5s %7.2f %11.2f %7d %7.7s    " ,
                    "" , "" ,
                    arcp -> arc_time / hz , arcp -> arc_childtime / hz ,
                    arcp -> arc_count, "" );
	    printname( childp );
#ifdef SPARC
	    if (Cflag)
		print_demangled_name(54, childp);
#endif
	    printf( "\n" );
	}
    }
}


#ifdef SPARC
char *demangled_name();
#endif


printname( selfp )
    nltype	*selfp;
{
    register char  *c;
#ifdef SPARC
    c=demangled_name(selfp);
#endif

    if ( selfp -> name != 0 ) {
#ifdef SPARC
	if (!Cflag)
#endif
	    printf( "%s" , selfp -> name );
#ifdef SPARC
	else
	    printf( "%s" , c );
#endif

#	ifdef DEBUG
	    if ( debug & DFNDEBUG ) {
		printf( "{%d} " , selfp -> toporder );
	    }
	    if ( debug & PROPDEBUG ) {
		printf( "%5.2f%% " , selfp -> propfraction );
	    }
#	endif DEBUG
    }
    if ( selfp -> cycleno != 0 ) {
	printf( "\t<cycle %d>" , selfp -> cycleno );
    }
    if ( selfp -> index != 0 ) {
	if ( selfp -> printflag ) {
	    printf( " [%d]" , selfp -> index );
	} else {
	    printf( " (%d)" , selfp -> index );
	}
    }
}

#ifdef SPARC
print_demangled_name(n,selfp)
nltype	*selfp;
int n;
{
	char *d,*c;
	register i;
	c = selfp->name;
	if (strcmp(c,(d=demangled_name(selfp))) == 0)
		return;
	else {
		printf("\n");
		for (i=1;i<n;i++)
			printf(" ");
		printf("[%s]",selfp->name);
	}
}
#endif


char * exotic();

#ifdef SPARC
char * demangled_name(selfp)
	nltype *selfp;
{
  extern char *DemangleAndFormat( char*, char*);

  return (DemangleAndFormat( selfp->name, "%s"));
}
#endif

sortchildren( parentp )
    nltype	*parentp;
{
    arctype	*arcp;
    arctype	*detachedp;
    arctype	sorted;
    arctype	*prevp;

	/*
	 *	unlink children from parent,
	 *	then insertion sort back on to sorted's children.
	 *	    *arcp	the arc you have detached and are inserting.
	 *	    *detachedp	the rest of the arcs to be sorted.
	 *	    sorted	arc list onto which you insertion sort.
	 *	    *prevp	arc before the arc you are comparing.
	 */
    sorted.arc_childlist = 0;
    for (  (arcp = parentp -> children)&&(detachedp = arcp -> arc_childlist);
	    arcp ;
	   (arcp = detachedp)&&(detachedp = detachedp -> arc_childlist)) {
	    /*
	     *	consider *arcp as disconnected
	     *	insert it into sorted
	     */
	for (   prevp = &sorted ;
		prevp -> arc_childlist ;
		prevp = prevp -> arc_childlist ) {
	    if ( arccmp( arcp , prevp -> arc_childlist ) != LESSTHAN ) {
		break;
	    }
	}
	arcp -> arc_childlist = prevp -> arc_childlist;
	prevp -> arc_childlist = arcp;
    }
	/*
	 *	reattach sorted children to parent
	 */
    parentp -> children = sorted.arc_childlist;
}

sortparents( childp )
    nltype	*childp;
{
    arctype	*arcp;
    arctype	*detachedp;
    arctype	sorted;
    arctype	*prevp;

	/*
	 *	unlink parents from child,
	 *	then insertion sort back on to sorted's parents.
	 *	    *arcp	the arc you have detached and are inserting.
	 *	    *detachedp	the rest of the arcs to be sorted.
	 *	    sorted	arc list onto which you insertion sort.
	 *	    *prevp	arc before the arc you are comparing.
	 */
    sorted.arc_parentlist = 0;
    for (  (arcp = childp -> parents)&&(detachedp = arcp -> arc_parentlist);
	    arcp ;
	   (arcp = detachedp)&&(detachedp = detachedp -> arc_parentlist)) {
	    /*
	     *	consider *arcp as disconnected
	     *	insert it into sorted
	     */
	for (   prevp = &sorted ;
		prevp -> arc_parentlist ;
		prevp = prevp -> arc_parentlist ) {
	    if ( arccmp( arcp , prevp -> arc_parentlist ) != GREATERTHAN ) {
		break;
	    }
	}
	arcp -> arc_parentlist = prevp -> arc_parentlist;
	prevp -> arc_parentlist = arcp;
    }
	/*
	 *	reattach sorted arcs to child
	 */
    childp -> parents = sorted.arc_parentlist;
}

    /*
     *	print a cycle header
     */
printcycle( cyclep )
    nltype	*cyclep;
{
    char	kirkbuffer[ BUFSIZ ];

    sprintf( kirkbuffer , "[%d]" , cyclep -> index );
    printf( "%-6.6s %5.1f %7.2f %11.2f %7d" ,
	    kirkbuffer ,
	    100 * ( cyclep -> propself + cyclep -> propchild ) / printtime ,
	    cyclep -> propself / hz ,
	    cyclep -> propchild / hz ,
	    cyclep -> ncall );
    if ( cyclep -> selfcalls != 0 ) {
	printf( "+%-7d" , cyclep -> selfcalls );
    } else {
	printf( " %7.7s" , "" );
    }
    printf( " <cycle %d as a whole>\t[%d]\n" ,
	    cyclep -> cycleno , cyclep -> index );
}

    /*
     *	print the members of a cycle
     */
printmembers( cyclep )
    nltype	*cyclep;
{
    nltype	*memberp;

    sortmembers( cyclep );
    for ( memberp = cyclep -> cnext ; memberp ; memberp = memberp -> cnext ) {
	printf( "%6.6s %5.5s %7.2f %11.2f %7d" , 
		"" , "" , memberp -> propself / hz , memberp -> propchild / hz ,
		memberp -> ncall );
	if ( memberp -> selfcalls != 0 ) {
	    printf( "+%-7d" , memberp -> selfcalls );
	} else {
	    printf( " %7.7s" , "" );
	}
	printf( "     " );
	printname( memberp );
#ifdef SPARC
	if (Cflag)
	    print_demangled_name(54, memberp);
#endif
	printf( "\n" );
    }
}

    /*
     *	sort members of a cycle
     */
sortmembers( cyclep )
    nltype	*cyclep;
{
    nltype	*todo;
    nltype	*doing;
    nltype	*prev;

	/*
	 *	detach cycle members from cyclehead,
	 *	and insertion sort them back on.
	 */
    todo = cyclep -> cnext;
    cyclep -> cnext = 0;
    for (  (doing = todo)&&(todo = doing -> cnext);
	    doing ;
	   (doing = todo )&&(todo = doing -> cnext )){
	for ( prev = cyclep ; prev -> cnext ; prev = prev -> cnext ) {
	    if ( membercmp( doing , prev -> cnext ) == GREATERTHAN ) {
		break;
	    }
	}
	doing -> cnext = prev -> cnext;
	prev -> cnext = doing;
    }
}

    /*
     *	major sort is on propself + propchild,
     *	next is sort on ncalls + selfcalls.
     */
int
membercmp( this , that )
    nltype	*this;
    nltype	*that;
{
    double	thistime = this -> propself + this -> propchild;
    double	thattime = that -> propself + that -> propchild;
    long	thiscalls = this -> ncall + this -> selfcalls;
    long	thatcalls = that -> ncall + that -> selfcalls;

    if ( thistime > thattime ) {
	return GREATERTHAN;
    }
    if ( thistime < thattime ) {
	return LESSTHAN;
    }
    if ( thiscalls > thatcalls ) {
	return GREATERTHAN;
    }
    if ( thiscalls < thatcalls ) {
	return LESSTHAN;
    }
    return EQUALTO;
}
    /*
     *	compare two arcs to/from the same child/parent.
     *	- if one arc is a self arc, it's least.
     *	- if one arc is within a cycle, it's less than.
     *	- if both arcs are within a cycle, compare arc counts.
     *	- if neither arc is within a cycle, compare with
     *		arc_time + arc_childtime as major key
     *		arc count as minor key
     */
int
arccmp( thisp , thatp )
    arctype	*thisp;
    arctype	*thatp;
{
    nltype	*thisparentp = thisp -> arc_parentp;
    nltype	*thischildp = thisp -> arc_childp;
    nltype	*thatparentp = thatp -> arc_parentp;
    nltype	*thatchildp = thatp -> arc_childp;
    double	thistime;
    double	thattime;

#   ifdef DEBUG
	if ( debug & TIMEDEBUG ) {
	    printf( "[arccmp] " );
	    printname( thisparentp );
	    printf( " calls " );
	    printname ( thischildp );
	    printf( " %f + %f %d/%d\n" ,
		    thisp -> arc_time , thisp -> arc_childtime ,
		    thisp -> arc_count , thischildp -> ncall );
	    printf( "[arccmp] " );
	    printname( thatparentp );
	    printf( " calls " );
	    printname( thatchildp );
	    printf( " %f + %f %d/%d\n" ,
		    thatp -> arc_time , thatp -> arc_childtime ,
		    thatp -> arc_count , thatchildp -> ncall );
	    printf( "\n" );
	}
#   endif DEBUG
    if ( thisparentp == thischildp ) {
	    /* this is a self call */
	return LESSTHAN;
    }
    if ( thatparentp == thatchildp ) {
	    /* that is a self call */
	return GREATERTHAN;
    }
    if ( thisparentp -> cycleno != 0 && thischildp -> cycleno != 0 &&
	thisparentp -> cycleno == thischildp -> cycleno ) {
	    /* this is a call within a cycle */
	if ( thatparentp -> cycleno != 0 && thatchildp -> cycleno != 0 &&
	    thatparentp -> cycleno == thatchildp -> cycleno ) {
		/* that is a call within the cycle, too */
	    if ( thisp -> arc_count < thatp -> arc_count ) {
		return LESSTHAN;
	    }
	    if ( thisp -> arc_count > thatp -> arc_count ) {
		return GREATERTHAN;
	    }
	    return EQUALTO;
	} else {
		/* that isn't a call within the cycle */
	    return LESSTHAN;
	}
    } else {
	    /* this isn't a call within a cycle */
	if ( thatparentp -> cycleno != 0 && thatchildp -> cycleno != 0 &&
	    thatparentp -> cycleno == thatchildp -> cycleno ) {
		/* that is a call within a cycle */
	    return GREATERTHAN;
	} else {
		/* neither is a call within a cycle */
	    thistime = thisp -> arc_time + thisp -> arc_childtime;
	    thattime = thatp -> arc_time + thatp -> arc_childtime;
	    if ( thistime < thattime )
		return LESSTHAN;
	    if ( thistime > thattime )
		return GREATERTHAN;
	    if ( thisp -> arc_count < thatp -> arc_count )
		return LESSTHAN;
	    if ( thisp -> arc_count > thatp -> arc_count )
		return GREATERTHAN;
	    return EQUALTO;
	}
    }
}

printblurb( blurbname )
    char	*blurbname;
{
    FILE	*blurbfile;
    int		input;
    char blurb_directory[MAXPATHLEN];
    char current_work_directory[MAXPATHLEN];

    current_work_directory[0] = '.';
    current_work_directory[1] = '\0';
    if (find_run_directory(prog_name, current_work_directory, blurb_directory,                                 (char **) 0, getenv("PATH")) != 0) {
	(void)fprintf(stderr,"Error in finding run directory.");
	return;
    }
    else {
	strcat(blurb_directory, blurbname);
    }


    blurbfile = fopen( blurb_directory , "r" );
    if ( blurbfile == NULL ) {
	perror( blurb_directory );
	return;
    }
    while ( ( input = getc( blurbfile ) ) != EOF ) {
	putchar( input );
    }
    fclose( blurbfile );
}

char *s1, *s2;


int
namecmp( npp1 , npp2 )
    nltype **npp1, **npp2;
{
#ifdef SPARC
    if (!Cflag)
#endif
	return( strcmp( (*npp1) -> name , (*npp2) -> name ) );
#ifdef SPARC
    else {
	striped_name(s1, npp1);
	striped_name(s2, npp2);
	return( strcmp(s1, s2) );
    }
#endif
}

#ifdef SPARC
striped_name(s,npp)
char *s;
nltype **npp;
{
	char *d,*c;
	c =(char *) s;
	d = demangled_name(*npp);
	while ((*d != '(') && (*d != '\0')) {
		if (*d != ':') {
			*c++ = *d++;
		}
		else d++;
	}
	*c = '\0';
}
#endif

printindex()
{
    nltype		**namesortnlp;
    register nltype	*nlp;
    int			index, nnames, todo, i, j;
    char		peterbuffer[ BUFSIZ ];

	/*
	 *	Now, sort regular function name alphbetically
	 *	to create an index.
	 */
    namesortnlp = (nltype **) calloc( nname + ncycle , sizeof(nltype *) );
    if ( namesortnlp == (nltype **) 0 ) {
	fprintf( stderr , "%s: ran out of memory for sorting\n" , whoami );
    }
    for ( index = 0 , nnames = 0 ; index < nname ; index++ ) {
	if ( zflag == 0 && nl[index].ncall == 0 && nl[index].time == 0 )
		continue;
	namesortnlp[nnames++] = &nl[index];
    }
#ifdef SPARC
    if (Cflag) {
        s1 = (char *) malloc(500*sizeof(char));
        s2 = (char *) malloc(500*sizeof(char));
    }
#endif
    qsort( namesortnlp , nnames , sizeof(nltype *) , namecmp );
    for ( index = 1 , todo = nnames ; index <= ncycle ; index++ ) {
	namesortnlp[todo++] = &cyclenl[index];
    }
    printf( "\f\nIndex by function name\n\n" );
#ifdef SPARC
    if (!Cflag)
#endif
        index = ( todo + 2 ) / 3;
#ifdef SPARC
    else
        index = todo;
#endif
    for ( i = 0; i < index ; i++ ) {
#ifdef SPARC
      if (!Cflag)
      {
#endif
	for ( j = i; j < todo ; j += index ) {
	    nlp = namesortnlp[ j ];
	    if ( nlp -> printflag ) {
		sprintf( peterbuffer , "[%d]" , nlp -> index );
	    } else {
		sprintf( peterbuffer , "(%d)" , nlp -> index );
	    }
	    if ( j < nnames ) {
		printf( "%6.6s %-19.19s" , peterbuffer , nlp -> name );
	    } else {
		printf( "%6.6s " , peterbuffer );
		sprintf( peterbuffer , "<cycle %d>" , nlp -> cycleno );
		printf( "%-19.19s" , peterbuffer );
	    }
	}
#ifdef SPARC
      }
      else
      {
	nlp = namesortnlp[ i ];
	if ( nlp -> printflag ) {
		sprintf( peterbuffer , "[%d]" , nlp -> index );
	} else {
		sprintf( peterbuffer , "(%d)" , nlp -> index );
	}
	if ( i < nnames ) {
		char *d;
		d = demangled_name(nlp);
		printf("%6.6s %s\n",peterbuffer,d);
		if (d != nlp->name)
			printf( "%6.6s   [%s]" , "" , nlp -> name );
	} else {
		printf( "%6.6s " , peterbuffer );
		sprintf( peterbuffer , "<cycle %d>" , nlp -> cycleno );
		printf( "%-33.33s" , peterbuffer );
	}
      }
#endif
      printf( "\n" );
    }
    cfree( namesortnlp );
}


char dname[500];

char * exotic(s)
char *s;
{
	char *name;	
        register int i =0;
	register j; char *p;
	char *s1="static constructor function for ";
	name = (char *) malloc(500 *sizeof(char));
        if (strncmp(s,"__sti__",7) == 0) {
                i= 0; s+=7;
                if ((p=strstr(s,"_c_")) == NULL) {
                        if ((p=strstr(s,"_C_")) == NULL) {
                                if ((p=strstr(s,"_cc_")) == NULL) {
					if ((p=strstr(s,"_cxx_")) == NULL) {
					if ((p=strstr(s,"_h_")) == NULL)
                                        	return;
					}
                                }
                        }
                }
                else {
                        p +=3;
                        *p = '\0';
                }

		for (i=0;s1[i]!='\0';i++)
			dname[i]=s1[i];
		j=i;
		for (i=0;s[i]!='\0';i++)
			dname[j+i]=s[i];
		dname[j+i]='\0';
		free(name);
		return( (char *) dname);
	}
	else if (strncmp(s,"__std__",7) == 0) {
		char *s1="static destructor function for ";
                i = 0; s+=7;
                if ((p=strstr(s,"_c_")) == NULL) {
                        if ((p=strstr(s,"_C_")) == NULL) {
                                if ((p=strstr(s,"_cc_")) == NULL) {
					if ((p=strstr(s,"_cxx_")) == NULL) {
					if ((p=strstr(s,"_h_")) == NULL)
                                        	return;
					}
                                }
                        }
                }
                else {
                        p +=3;
                        *p = '\0';
                }

                for (i=0;s1[i]!='\0';i++)
                        dname[i]=s1[i];
                j=i;
                for (i=0;s[i]!='\0';i++)
                        dname[j+i]=s[i];
                dname[j+i]='\0';
		free(name);
                return((char *) dname);
        }
        else if (strncmp(s,"__vtbl__",8) == 0) {
                char *s1="virtual table for ";
                char *printname ;
                char *return_p = dname;
                s+=8;
                printname = parsename(s);
                return_p = '\0';
                strcat(return_p,s1);
                strcat(return_p,printname);
		free(name);
		return((char *)dname);
 
        }
        else if (strncmp(s, "__ptbl__",8) == 0) {
                char *s1="pointer to the virtual table for ";
                char *printname ;
                char *return_p = dname;
                s+=8;
                printname = parsename(s);
                return_p = '\0';
                strcat(return_p,s1);
                strcat(return_p,printname);

                free(name);
                return(return_p);

        };

	free(name);
	return(s); 
}


char *parsename(s)
char *s;
{
        char *d;
        register int len;
        char c_init;
        char *len_pointer = s;
        d = name_buffer;
        *d = '\0';
        strcat(d,"class ");
        while (isdigit(*s)) s++;
        c_init = *s;
        *s = '\0';
        len = atoi(len_pointer);
        *s = c_init;
        if (*(s+len) == '\0') { /* only one class name */
                strcat(d,s);
                return d;
        }
        else { /* two classname  %drootname__%dchildname */
                char *child;
                char *root;
                int child_len;
                char *child_len_p;
                root = s;
                child = s + len + 2;
                child_len_p = child;
                if (! isdigit(*child)) { /* ptbl file name */
                                /*  %drootname__%filename */
                        c_init = *(root + len);
                        *(root + len ) = '\0';
                        strcat(d, root);
                        *(root + len) = c_init;
                        strcat(d," in ");
                        strcat(d, child);
                        return d;
                }
 
                while (isdigit(*child)) child++;
                c_init = *child;
                *child = '\0';
                child_len = atoi(child_len_p);
                *child = c_init;
                if (*(child + child_len) == '\0') {
                        strcat(d,child);
                        strcat(d, " derived from ");
                        c_init = *(root + len);
                        *(root + len ) = '\0';
                        strcat(d, root);
                        *(root + len) = c_init;
                        return d;
                }
                else { /* %drootname__%dchildname__filename */
                        c_init = *(child + child_len);
                        *(child + child_len ) = '\0';
                        strcat(d,child);
                        *(child+child_len) = c_init;
                        strcat(d," derived from ");
                        c_init = *(root + len);
                        *(root + len ) = '\0';
                        strcat(d, root);
                        *(root + len) = c_init;
                        strcat(d , " in ");
                        strcat(d, child + child_len +2);
                        return d;
                }
        }
}

/*
char * strstr(s1,s2)
char *s1,*s2;
{
        register char *p;
        int i = strlen(s2);
        p = s1;
        while (i <= strlen(p)) {
                if (strncmp(p,s2,i) == 0)
                        return p;
                p++;
        }
        return NULL;
}        
*/
