/*
   Copyright stuff

   Use of this program, for any purpose, is granted the author,
   Ian Kaplan, as long as this copyright notice is included in
   the source code or any source code derived from this program.
   The user assumes all responsibility for using this code.

   Ian Kaplan, October 1996

*/

#define HI 0
#define LO 1

void set128(unsigned long *n, unsigned long hi, unsigned long lo)
{
    n[HI] = hi;
    n[LO] = lo;
}

int eq128(unsigned long *n1, unsigned long *n2)
{
    return((n1[HI] == n2[HI]) && (n1[LO] == n2[LO]));
}

int gt128(unsigned long *n1, unsigned long *n2)
{
    return((n1[HI] > n2[HI]) || 
	((n1[HI] == n2[HI]) && (n1[LO] > n2[LO])));
}

int lt128(unsigned long *n1, unsigned long *n2)
{
    return((n1[HI] < n2[HI]) || 
	((n1[HI] == n2[HI]) && (n1[LO] < n2[LO])));
}


void copy128(unsigned long *dest, unsigned long *src)
{
    dest[HI] = src[HI];
    dest[LO] = src[LO];
}

/* Shift the given bit into the octaword from the right
 * (i.e. left-shift-1, or in low bit).  If "bit" is zero,
 * then this is a simple left shift.
 */
void shiftin128(unsigned long *n, unsigned long bit)
{
    n[HI] <<= 1;
    if(n[LO] & 0x8000000000000000) {
	n[HI] |= 1;
    }
    n[LO] = (n[LO] << 1) | bit;
}

void sub128(unsigned long *n1, unsigned long *n2, unsigned long *result)
{
    if(n1[LO] < n2[LO]) {
        result[LO] = n1[LO] - n2[LO];
	result[HI] = n1[HI] - n2[HI] - 1;
    }
    else {
        result[LO] = n1[LO] - n2[LO];
	result[HI] = n1[HI] - n2[HI];
    }
}
	

void udiv128(unsigned long *dividend,
                     unsigned long *divisor,
                     unsigned long *quotient,
                     unsigned long *remainder )
{
  unsigned long zero[2];
  unsigned long t[2], num_bits;
  unsigned long q, bit;
  unsigned long rem[2];
  int i;

  set128(remainder, 0, 0);
  set128(quotient, 0, 0);
  set128(zero, 0, 0);

  if (eq128(divisor, zero)) {
    return;
  }

  if(gt128(divisor, dividend)) {
    copy128(remainder, dividend);
    return;
  }

  if (eq128(divisor, dividend)) {
    set128(quotient, 0, 1);
    return;
  }

  num_bits = 128;

  while(1) {
    bit = (dividend[HI] & 0x8000000000000000) >> 63;
    copy128(rem, remainder);
    shiftin128(rem, bit);
    if(lt128(rem, divisor)) break;
    copy128(remainder, rem);
    shiftin128(dividend, 0);
    num_bits--;
  }

  for (i = 0; i < num_bits; i++) {
    bit = (dividend[HI] & 0x8000000000000000) >> 63;
    shiftin128(remainder, bit);
    sub128(remainder, divisor, t);
    q = !((t[HI] & 0x8000000000000000) >> 63);
    shiftin128(dividend, 0);
    shiftin128(quotient, q);
    if (q) {
       copy128(remainder, t);
     }
  }
}  /* unsigned_divide128 */

