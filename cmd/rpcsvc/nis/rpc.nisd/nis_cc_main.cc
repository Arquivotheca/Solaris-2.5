#pragma ident	"@(#)nis_cc_main.cc	1.3	92/07/17 SMI" 

/*
 * This is needed to  to satisfy the C++ system that needs the main
 * routine to be compiled with C++ compiler if you link with a library
 * that has C++ code and static intializers. 
 * 
 */
extern "C" {
	actual_main(int, char**);
}

main(int argc, char*argv[])
{
	/* call the actual main() routine in zns_main.c */

	actual_main(argc, argv);
}
