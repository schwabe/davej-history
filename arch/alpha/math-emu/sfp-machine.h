/* Machine-dependent software floating-point definitions.  Sparc64 version.
   Copyright (C) 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <linux/sched.h>
#include <asm/fpu.h>

#define _FP_W_TYPE_SIZE		64
#define _FP_W_TYPE		unsigned long
#define _FP_WS_TYPE		signed long
#define _FP_I_TYPE		long

#define _FP_MUL_MEAT_S(R,X,Y)	_FP_MUL_MEAT_1_imm(S,R,X,Y)
#define _FP_MUL_MEAT_D(R,X,Y)	_FP_MUL_MEAT_1_wide(D,R,X,Y,umul_ppmm)
#define _FP_MUL_MEAT_Q(R,X,Y)	_FP_MUL_MEAT_2_wide(Q,R,X,Y,umul_ppmm)

#define _FP_DIV_MEAT_S(R,X,Y)	_FP_DIV_MEAT_1_imm(S,R,X,Y,_FP_DIV_HELP_imm)
#define _FP_DIV_MEAT_D(R,X,Y)	_FP_DIV_MEAT_1_udiv(D,R,X,Y)
#define _FP_DIV_MEAT_Q(R,X,Y)	_FP_DIV_MEAT_2_udiv_64(Q,R,X,Y)

#define _FP_NANFRAC_S		_FP_QNANBIT_S
#define _FP_NANFRAC_D		_FP_QNANBIT_D
#define _FP_NANFRAC_Q		_FP_QNANBIT_Q, 0

/* On some architectures float-to-int conversions return a result
   code.  On others (e.g. Sparc) they return 0.  */
#define _FTOI_RESULT(X)		X##_r

#define _FP_KEEPNANFRACP	1

/*
 * Alpha Architecture Manual Section 4.7.10.4: Propagating NaN Values,
 * summary:
 *
 * The first of the following rules that is applicable governs the
 * value returned:
 *	1: If X is a quiet NaN, copy X to the result.
 *	2: If X is a signaling NaN, the result is the canonical quiet NaN
 *	   with the same sign as X
 *	3: If Y is a quiet NaN, copy Y to the result.
 *	4: If Y is a signaling NaN, the result is the canonical quiet NaN
 *	   with the same sign as Y
 *	5: The result is the canonical quiet NaN with a sign bit of 1
 *
 * In addition, in cases (2) and (4) above we set EFLAG_INVALID.
 */

#define _FP_IS_NAN(fs, Z)	(Z##_c == FP_CLS_NAN)
#define _FP_IS_QNAN(fs, Z)	(Z##_f & _FP_QNANBIT_##fs)

#define _FP_CHOOSENAN(fs, wc, R, X, Y)				\
  do {								\
    R##_r |= (X##_r | Y##_r);					\
    if (_FP_IS_NAN(fs, Y)) {					\
	R##_s = Y##_s;						\
	R##_c = FP_CLS_NAN;					\
	if (_FP_IS_QNAN(fs, Y)) {	/* Rule 1 */		\
	    _FP_FRAC_COPY_##wc(R,Y);				\
	} else {			/* Rule 2 */		\
	    _FP_FRAC_SET_##wc(R,Y##_f | _FP_QNANBIT_##fs); 	\
	    R##_r = EFLAG_INVALID;				\
	}							\
    } else if (_FP_IS_NAN(fs, X)) {				\
	R##_s = X##_s;						\
	R##_c = FP_CLS_NAN;					\
	if (_FP_IS_QNAN(fs, X)) {	/* Rule 3 */		\
	    _FP_FRAC_COPY_##wc(R,X);				\
	} else {			/* Rule 4 */		\
	    _FP_FRAC_SET_##wc(R,X##_f | _FP_QNANBIT_##fs); 	\
	    R##_r |= EFLAG_INVALID;				\
	}							\
    } else {				/* Rule 5 */		\
	R##_s = 1;						\
	R##_c = FP_CLS_NAN;					\
	_FP_FRAC_SET_##wc(R,_FP_QNANBIT_##fs | EFLAG_MASK); 	\
    }								\
  } while(0)

/* Rules 3 and 4 don't apply to functions of only one argument */
#define _FP_CHOOSENAN_1(fs, wc, R, X)				\
  do {								\
    if (_FP_IS_NAN(fs, X)) {					\
	R##_s = X##_s;						\
	R##_c = FP_CLS_NAN;					\
	if (_FP_IS_QNAN(fs, X)) {	/* Rule 1 */		\
	    _FP_FRAC_COPY_##wc(R,X);				\
	} else {			/* Rule 2 */		\
	    _FP_FRAC_SET_##wc(R,X##_f | _FP_QNANBIT_##fs); 	\
	    R##_r |= EFLAG_INVALID;				\
	}							\
    } else {				/* Rule 5 */		\
	R##_s = 1;						\
	R##_c = FP_CLS_NAN;					\
	_FP_FRAC_SET_##wc(R,_FP_QNANBIT_##fs | EFLAG_MASK); 	\
    }								\
  } while(0)

#define _FP_CHOOSENAN_SQRT	_FP_CHOOSENAN_1


#define __FP_UNPACK_DENORM(fs, wc, X)					\
  do {									\
	_FP_I_TYPE _shift;						\
	X##_r |= EFLAG_DENORM;						\
	if (_FP_DENORM_TO_ZERO) {					\
	    /* Crunching a nonzero denorm to zero necessarily makes */  \
	    /* the result inexact */					\
	    X##_r |= EFLAG_INEXACT;					\
	    _FP_FRAC_SET_##wc(X, 0);					\
	    X##_c = FP_CLS_ZERO;					\
	} else {							\
	    _FP_FRAC_CLZ_##wc(_shift, X);				\
	    _shift -= _FP_FRACXBITS_##fs;				\
	    _FP_FRAC_SLL_##wc(X, (_shift+_FP_WORKBITS));		\
	    X##_e -= _FP_EXPBIAS_##fs - 1 + _shift;			\
	    X##_c = FP_CLS_NORMAL;					\
	}								\
  } while (0)

#define __FP_UNPACK_RAW_1(fs, X, val)	\
  do {					\
    union _FP_UNION_##fs *_flo =	\
    	(union _FP_UNION_##fs *)val;	\
					\
    X##_f = _flo->bits.frac;		\
    X##_e = _flo->bits.exp;		\
    X##_s = _flo->bits.sign;		\
  } while (0)

#define __FP_UNPACK_RAW_2(fs, X, val)	\
  do {					\
    union _FP_UNION_##fs *_flo =	\
    	(union _FP_UNION_##fs *)val;	\
					\
    X##_f0 = _flo->bits.frac0;		\
    X##_f1 = _flo->bits.frac1;		\
    X##_e  = _flo->bits.exp;		\
    X##_s  = _flo->bits.sign;		\
  } while (0)

#define __FP_UNPACK_S(X,val)		\
  do {					\
    __FP_UNPACK_RAW_1(S,X,val);		\
    _FP_UNPACK_CANONICAL(S,1,X);	\
  } while (0)

#define __FP_UNPACK_D(X,val)		\
  do {					\
    __FP_UNPACK_RAW_1(D,X,val);		\
    _FP_UNPACK_CANONICAL(D,1,X);	\
  } while (0)

#define __FP_UNPACK_Q(X,val)		\
  do {					\
    __FP_UNPACK_RAW_2(Q,X,val);		\
    _FP_UNPACK_CANONICAL(Q,2,X);	\
  } while (0)

#define __FP_PACK_RAW_1(fs, val, X)	\
  do {					\
    union _FP_UNION_##fs *_flo =	\
    	(union _FP_UNION_##fs *)val;	\
					\
    _flo->bits.frac = X##_f;		\
    _flo->bits.exp  = X##_e;		\
    _flo->bits.sign = X##_s;		\
  } while (0)
  
#define __FP_PACK_RAW_2(fs, val, X)	\
  do {					\
    union _FP_UNION_##fs *_flo =	\
    	(union _FP_UNION_##fs *)val;	\
					\
    _flo->bits.frac0 = X##_f0;		\
    _flo->bits.frac1 = X##_f1;		\
    _flo->bits.exp   = X##_e;		\
    _flo->bits.sign  = X##_s;		\
  } while (0)


/* Alpha rules for handling certain exceptional cases are different
 * enough that we simply define our own versions here to override
 * the ones in op-common.h
 */

#define _FP_ADD(fs, wc, R, X, Y)					     \
do {									     \
  /* Propagate any flags that may have been set during unpacking */	     \
  R##_r |= (X##_r | Y##_r);						     \
  switch (_FP_CLS_COMBINE(X##_c, Y##_c))				     \
  {									     \
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_NORMAL):			     \
    {									     \
      /* shift the smaller number so that its exponent matches the larger */ \
      _FP_I_TYPE diff = X##_e - Y##_e;					     \
									     \
      if (diff < 0)							     \
	{								     \
	  diff = -diff;							     \
	  if (diff <= _FP_WFRACBITS_##fs)				     \
	    _FP_FRAC_SRS_##wc(X, diff, _FP_WFRACBITS_##fs);		     \
	  else if (!_FP_FRAC_ZEROP_##wc(X)) {				     \
	    _FP_FRAC_SET_##wc(X, _FP_MINFRAC_##wc);			     \
	    R##_r |= EFLAG_INEXACT;					     \
	  }								     \
	  else								     \
	    _FP_FRAC_SET_##wc(X, _FP_ZEROFRAC_##wc);			     \
	  R##_e = Y##_e;						     \
	}								     \
      else								     \
	{								     \
	  if (diff > 0)							     \
	    {								     \
	      if (diff <= _FP_WFRACBITS_##fs)				     \
	        _FP_FRAC_SRS_##wc(Y, diff, _FP_WFRACBITS_##fs);		     \
	      else if (!_FP_FRAC_ZEROP_##wc(Y))	{			     \
	        _FP_FRAC_SET_##wc(Y, _FP_MINFRAC_##wc);			     \
	         R##_r |= EFLAG_INEXACT;				     \
	      }								     \
	      else							     \
	        _FP_FRAC_SET_##wc(Y, _FP_ZEROFRAC_##wc);		     \
	    }								     \
	  R##_e = X##_e;						     \
	}								     \
									     \
      R##_c = FP_CLS_NORMAL;						     \
									     \
      if (X##_s == Y##_s)						     \
	{								     \
	  R##_s = X##_s;						     \
	  _FP_FRAC_ADD_##wc(R, X, Y);					     \
	  if (_FP_FRAC_OVERP_##wc(fs, R))				     \
	    {								     \
	      _FP_FRAC_SRS_##wc(R, 1, _FP_WFRACBITS_##fs);		     \
	      R##_e++;							     \
	    }								     \
	}								     \
      else								     \
	{								     \
	  R##_s = X##_s;						     \
	  _FP_FRAC_SUB_##wc(R, X, Y);					     \
	  if (_FP_FRAC_ZEROP_##wc(R))					     \
	    {								     \
	      /* return an exact zero */				     \
	      if (FP_ROUNDMODE == FP_RND_MINF)				     \
		R##_s |= Y##_s;						     \
	      else							     \
		R##_s &= Y##_s;						     \
	      R##_c = FP_CLS_ZERO;					     \
	    }								     \
	  else								     \
	    {								     \
	      if (_FP_FRAC_NEGP_##wc(R))				     \
		{							     \
		  _FP_FRAC_SUB_##wc(R, Y, X);				     \
		  R##_s = Y##_s;					     \
		}							     \
									     \
	      /* renormalize after subtraction */			     \
	      _FP_FRAC_CLZ_##wc(diff, R);				     \
	      diff -= _FP_WFRACXBITS_##fs;				     \
	      if (diff)							     \
		{							     \
		  R##_e -= diff;					     \
		  _FP_FRAC_SLL_##wc(R, diff);				     \
		}							     \
	    }								     \
	}								     \
      break;								     \
    }									     \
									     \
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_NAN):				     \
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_NORMAL):			     \
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_INF):				     \
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_ZERO):				     \
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_NAN):			     \
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_NAN):				     \
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_NAN):				     \
    _FP_CHOOSENAN(fs, wc, R, X, Y);					     \
    break;								     \
									     \
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_ZERO):			     \
    R##_e = X##_e;							     \
    _FP_FRAC_COPY_##wc(R, X);						     \
    R##_s = X##_s;							     \
    R##_c = X##_c;							     \
    break;								     \
									     \
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_NORMAL):			     \
    R##_e = Y##_e;							     \
    _FP_FRAC_COPY_##wc(R, Y);						     \
    R##_s = Y##_s;							     \
    R##_c = Y##_c;							     \
    break;								     \
									     \
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_INF):				     \
    if (X##_s != Y##_s)							     \
      {									     \
	/* +INF + -INF => NAN */					     \
	_FP_FRAC_SET_##wc(R, _FP_NANFRAC_##fs);				     \
	R##_s = X##_s ^ Y##_s;						     \
	R##_c = FP_CLS_NAN;						     \
	R##_r |= EFLAG_INVALID;						     \
	break;								     \
      }									     \
    /* FALLTHRU */							     \
									     \
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_NORMAL):			     \
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_ZERO):				     \
    R##_s = X##_s;							     \
    R##_c = FP_CLS_INF;							     \
    break;								     \
									     \
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_INF):			     \
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_INF):				     \
    R##_s = Y##_s;							     \
    R##_c = FP_CLS_INF;							     \
    break;								     \
									     \
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_ZERO):			     \
    /* make sure the sign is correct */					     \
    if (FP_ROUNDMODE == FP_RND_MINF)					     \
      R##_s = X##_s | Y##_s;						     \
    else								     \
      R##_s = X##_s & Y##_s;						     \
    R##_c = FP_CLS_ZERO;						     \
    break;								     \
									     \
  default:								     \
    abort();								     \
  }									     \
} while (0)


#define _FP_MUL(fs, wc, R, X, Y)			\
do {							\
  /* Propagate any flags that may have been set during unpacking */	     \
  R##_r |= (X##_r | Y##_r);						     \
  R##_s = X##_s ^ Y##_s;				\
  switch (_FP_CLS_COMBINE(X##_c, Y##_c))		\
  {							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_NORMAL):	\
    R##_c = FP_CLS_NORMAL;				\
    R##_e = X##_e + Y##_e + 1;				\
							\
    _FP_MUL_MEAT_##fs(R,X,Y);				\
							\
    if (_FP_FRAC_OVERP_##wc(fs, R))			\
      _FP_FRAC_SRS_##wc(R, 1, _FP_WFRACBITS_##fs);	\
    else						\
      R##_e--;						\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_NORMAL):	\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_INF):		\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_ZERO):		\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_NAN):		\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_NAN):	\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_NAN):		\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_NAN):		\
    _FP_CHOOSENAN(fs, wc, R, X, Y);			\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_INF):		\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_NORMAL):	\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_NORMAL):	\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_ZERO):	\
    _FP_FRAC_COPY_##wc(R, X);				\
    R##_c = X##_c;					\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_INF):	\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_ZERO):	\
    _FP_FRAC_COPY_##wc(R, Y);				\
    R##_c = Y##_c;					\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_ZERO):		\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_INF):		\
    R##_c = FP_CLS_NAN;					\
    _FP_FRAC_SET_##wc(R, _FP_NANFRAC_##fs);		\
    R##_s = 1; /* Alpha SRM rule */			\
    break;						\
							\
  default:						\
    abort();						\
  }							\
} while (0)


#define _FP_DIV(fs, wc, R, X, Y)			\
do {							\
  /* Propagate any flags that may have been set during unpacking */ \
  R##_r |= (X##_r | Y##_r);				\
  R##_s = X##_s ^ Y##_s;				\
  switch (_FP_CLS_COMBINE(X##_c, Y##_c))		\
  {							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_NORMAL):	\
    R##_c = FP_CLS_NORMAL;				\
    R##_e = X##_e - Y##_e;				\
							\
    _FP_DIV_MEAT_##fs(R,X,Y);				\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_NORMAL):	\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_INF):		\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_ZERO):		\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_NAN):		\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_NAN):	\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_NAN):		\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_NAN):		\
    _FP_CHOOSENAN(fs, wc, R, X, Y);			\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_INF):	\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_INF):		\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_NORMAL):	\
    R##_c = FP_CLS_ZERO;				\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_ZERO):	\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_ZERO):		\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_NORMAL):	\
    R##_c = FP_CLS_INF;					\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_INF):		\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_ZERO):	\
    R##_c = FP_CLS_NAN;					\
    _FP_FRAC_SET_##wc(R, _FP_NANFRAC_##fs);		\
    R##_s = 1;	/* Alpha SRM rule */			\
    break;						\
							\
  default:						\
    abort();						\
  }							\
} while (0)


#define _FP_TO_INT(fs, wc, r, X, rsize, rsigned)			\
  ({									\
    switch (X##_c)							\
      {									\
      case FP_CLS_ZERO:							\
	r = 0;								\
	break;								\
      case FP_CLS_NAN:                                              	\
	r = 0;								\
	X##_r |= EFLAG_INVALID;						\
	break;								\
      case FP_CLS_INF:							\
	r = 0;								\
	X##_r |= (EFLAG_INVALID | EFLAG_INEXACT);			\
	break;								\
      case FP_CLS_NORMAL:						\
	if (X##_e < 0)							\
	  {								\
	    r = 0;							\
	    X##_r |= EFLAG_INEXACT;					\
	  }								\
	else								\
	  {								\
	    if (X##_e >= rsize - (rsigned != 0)) {			\
		/* Overflow.  On alpha, set the INV bit and proceed */	\
		/* JRP - I *believe* the proper behavior is to set */   \
		/*       INV and write a true zero... need to check */  \
		X##_r |= EFLAG_INVALID;					\
		r = 0;							\
		break;							\
	    }								\
	    if (_FP_W_TYPE_SIZE*wc < rsize)				\
	      {								\
		_FP_FRAC_ASSEMBLE_##wc(r, X, rsize);			\
		r <<= X##_e - _FP_WFRACBITS_##fs;			\
	      }								\
	    else							\
	      {								\
		if (X##_e >= _FP_WFRACBITS_##fs)			\
		  _FP_FRAC_SLL_##wc(X, (X##_e - _FP_WFRACBITS_##fs + 1)); \
		else							\
		  _FP_FRAC_SRL_##wc(X, (_FP_WFRACBITS_##fs - X##_e - 1)); \
		_FP_FRAC_ASSEMBLE_##wc(r, X, rsize);			\
	      }								\
	    if (rsigned && X##_s)					\
	      r = -r;							\
	  }								\
	break;								\
      }									\
      X##_r;								\
  })


/* We only actually write to the destination register if exceptions
   signalled (if any) will not trap.  */
#define __FPU_TEM		(current->tss.flags & IEEE_TRAP_ENABLE_MASK)
#define __FPU_TRAP_P(bits)	((__FPU_TEM & (bits)) != 0)

#define __FP_PACK_S(val,X)			\
({  int __exc = _FP_PACK_CANONICAL(S,1,X);	\
    if (!__exc || !__FPU_TRAP_P(__exc))		\
        __FP_PACK_RAW_1(S,val,X);		\
    __exc;					\
})

#define __FP_PACK_D(val,X)			\
({  int __exc = _FP_PACK_CANONICAL(D,1,X);	\
    if(!__exc || !__FPU_TRAP_P(__exc))		\
        __FP_PACK_RAW_1(D,val,X);		\
    __exc;					\
})

#define __FP_PACK_Q(val,X)			\
({  int __exc = _FP_PACK_CANONICAL(Q,2,X);	\
    if(!__exc || !__FPU_TRAP_P(__exc))		\
        __FP_PACK_RAW_2(Q,val,X);		\
    __exc;					\
})

/* Obtain the current rounding mode. */
#define FP_ROUNDMODE		mode
#define FP_RND_NEAREST		(FPCR_DYN_NORMAL >> FPCR_DYN_SHIFT)
#define FP_RND_ZERO		(FPCR_DYN_CHOPPED >> FPCR_DYN_SHIFT)
#define FP_RND_PINF		(FPCR_DYN_PLUS >> FPCR_DYN_SHIFT)
#define FP_RND_MINF		(FPCR_DYN_MINUS >> FPCR_DYN_SHIFT)


#define add_ssaaaa(sh, sl, ah, al, bh, bl) \
  ((sl) = (al) + (bl), (sh) = (ah) + (bh) + ((sl) < (al)))

#define sub_ddmmss(sh, sl, ah, al, bh, bl) \
  ((sl) = (al) - (bl), (sh) = (ah) - (bh) - ((al) < (bl)))

#define umul_ppmm(wh, wl, u, v)			\
  __asm__ ("mulq %2,%3,%1; umulh %2,%3,%0"	\
	   : "=r" ((UDItype)(wh)),		\
	     "=&r" ((UDItype)(wl))		\
	   : "r" ((UDItype)(u)),		\
	     "r" ((UDItype)(v)))

extern __complex__ unsigned long udiv128(unsigned long, unsigned long,
					 unsigned long, unsigned long);

#define udiv_qrnnd(q, r, n1, n0, d)	\
  do {					\
    __complex__ unsigned long x_;	\
    x_ = udiv128((n0), (n1), 0, (d));	\
    (q) = __real__ x_;			\
    (r) = __imag__ x_;			\
  } while (0)

#define UDIV_NEEDS_NORMALIZATION 1  

#define abort()			goto bad_insn

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN -1
#endif
#define __BYTE_ORDER __LITTLE_ENDIAN

/* Exception flags. */
#define EFLAG_INVALID		IEEE_TRAP_ENABLE_INV
#define EFLAG_OVERFLOW		IEEE_TRAP_ENABLE_OVF
#define EFLAG_UNDERFLOW		IEEE_TRAP_ENABLE_UNF
#define EFLAG_DIVZERO		IEEE_TRAP_ENABLE_DZE
#define EFLAG_INEXACT		IEEE_TRAP_ENABLE_INE
#define EFLAG_DENORM		IEEE_TRAP_ENABLE_DNO
#define EFLAG_MASK		IEEE_TRAP_ENABLE_MASK

#define _FP_DENORM_TO_ZERO	((current->tss.flags) & IEEE_MAP_DMZ)

/* Comparison operations */
#define CMPTXX_EQ		0
#define CMPTXX_LT		-1
#define CMPTXX_GT		1
#define CMPTXX_LE		2
#define CMPTXX_UN		3
