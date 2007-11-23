#include "soft-fp.h"
#include "single.h"

int FXTOS(void *rd, void *rs2)
{
	FP_DECL_S(R);
	long a = *(long *)rs2;

	FP_FROM_INT_S(R, a, 64, long);
	return __FP_PACK_S(rd, R);
}
