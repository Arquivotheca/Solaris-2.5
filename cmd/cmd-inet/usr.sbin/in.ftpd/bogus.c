/*temporary*/
#ident	"@(#)bogus.c	1.2	92/07/14 SMI"
sigsetjmp(x,y)
{
return(setjmp(x));
}

siglongjmp(x,y)
{
sigsetmask(0);
return(longjmp(x,y));
}

