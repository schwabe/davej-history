#include "soft-fp.h"
#include "double.h"

int CMPTXX(void *rc, void *rb, void *ra, int type)
{
	FP_DECL_D(A); FP_DECL_D(B);
	long ret;
	
	__FP_UNPACK_D(A, ra);
	__FP_UNPACK_D(B, rb);
	FP_CMP_D(ret, A, B, 3);
	if(ret == type) {
	    *(unsigned long *)rc = 0x4000000000000000;
	}
	else if((type == CMPTXX_LE) && 
		((ret == CMPTXX_LT) || (ret == CMPTXX_EQ))) {
	    *(unsigned long *)rc = 0x4000000000000000;
	}
	else {
	    *(unsigned long *)rc = 0;
	}
	return 0;
}
