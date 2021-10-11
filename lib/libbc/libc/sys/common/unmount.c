#pragma ident	"@(#)unmount.c	1.3	92/07/20 SMI" 


unmount(s)
    char           *s;
{
    return umount(s);
}
