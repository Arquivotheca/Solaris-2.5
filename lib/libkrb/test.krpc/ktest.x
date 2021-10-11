#ifdef RPC_HDR
%#include <stdio.h>
%#define TEST_NAME "test"
#endif

struct pass {
	int cksum;
	long timestamp;
};


program TEST {
	version TEST_VERS {
	pass test_proc(pass) = 1;
	} = 1;
} = 555556;
