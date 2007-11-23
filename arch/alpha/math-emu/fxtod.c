#include "soft-fp.h"
#include "double.h"

int FXTOD(void *rd, void *rs2)
{
	FP_DECL_D(R);
	long a = *(long *)rs2;

	FP_FROM_INT_D(R, a, 64, long);
	return __FP_PACK_D(rd, R);
}
