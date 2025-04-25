/*
 *  FixPoint.h - Provides fixed point arithmetic (for use in SID.cpp)
 *
 *  (C) 1997 Andreas Dehmel
 *
 *  Frodo Copyright (C) Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  You need to define FIXPOINT_PREC (number of fractional bits) and
 *  ldSINTAB (ld of the size of the sinus table) as well M_PI
 *  _before_ including this file.
 *  Requires at least 32bit ints!
 */


#define FIXPOINT_BITS	32
// Sign-bit
#define FIXPOINT_SIGN	(1<<(FIXPOINT_BITS-1))


/*
 *  Elementary functions for the FixPoint class
 */

// Multiplies two fixpoint numbers, result is a fixpoint number.
static inline int fixmult(int x, int y)
{
  register unsigned int a,b;
  register bool sign;

  sign = (x ^ y) < 0;
  if (x < 0) {x = -x;}
  if (y < 0) {y = -y;}
  // a, b : integer part; x, y : fractional part. All unsigned now (for shift right)!!!
  a = (((unsigned int)x) >> FIXPOINT_PREC); x &= ~(a << FIXPOINT_PREC);
  b = (((unsigned int)y) >> FIXPOINT_PREC); y &= ~(b << FIXPOINT_PREC);
  x = ((a*b) << FIXPOINT_PREC) + (a*y + b*x) +
      ((unsigned int)((x*y) + (1 << (FIXPOINT_PREC-1))) >> FIXPOINT_PREC);
#ifdef FIXPOINT_SIGN
  if (x < 0) {x ^= FIXPOINT_SIGN;}
#endif
  if (sign) {x = -x;}
  return(x);
}


// Multiplies a fixpoint number with an integer, result is a 32 bit (!) integer in
// contrast to using the standard member-functions which can provide only (32-FIXPOINT_PREC)
// valid bits.
static inline int intmult(int x, int y)	// x is fixpoint, y integer
{
  register unsigned int i,j;
  register bool sign;

  sign = (x ^ y) < 0;
  if (x < 0) {x = -x;}
  if (y < 0) {y = -y;}
  i = (((unsigned int)x) >> 16); x &= ~(i << 16);	// split both into 16.16 parts
  j = (((unsigned int)y) >> 16); y &= ~(j << 16);
#if FIXPOINT_PREC <= 16
  // This '32' is independent of the number of bits used, it's due to the 16 bit shift
  i = ((i*j) << (32 - FIXPOINT_PREC)) + ((i*y + j*x) << (16 - FIXPOINT_PREC)) +
      ((unsigned int)(x*y + (1 << (FIXPOINT_PREC - 1))) >> FIXPOINT_PREC);
#else
  {
    register unsigned int h;

    h = (i*y + j*x);
    i = ((i*j) << (32 - FIXPOINT_PREC)) + (h >> (FIXPOINT_PREC - 16));
    h &= ((1 << (FIXPOINT_PREC - 16)) - 1); x *= y;
    i += (x >> FIXPOINT_PREC); x &= ((1 << FIXPOINT_PREC) - 1);
    i += (((h + (x >> 16)) + (1 << (FIXPOINT_PREC - 17))) >> (FIXPOINT_PREC - 16));
  }
#endif
#ifdef FIXPOINT_SIGN
  if (i < 0) {i ^= FIXPOINT_SIGN;}
#endif
  if (sign) {i = -i;}
  return(i);
}


// Computes the product of a fixpoint number with itself.
static inline int fixsquare(int x)
{
  register unsigned int a;

  if (x < 0) {x = -x;}
  a = (((unsigned int)x) >> FIXPOINT_PREC); x &= ~(a << FIXPOINT_PREC);
  x = ((a*a) << FIXPOINT_PREC) + ((a*x) << 1) +
      ((unsigned int)((x*x) + (1 << (FIXPOINT_PREC-1))) >> FIXPOINT_PREC);
#ifdef FIXPOINT_SIGN
  if (x < 0) {x ^= FIXPOINT_SIGN;}
#endif
  return(x);
}


// Computes the square root of a fixpoint number.
static inline int fixsqrt(int x)
{
  register int test, step;

  if (x < 0) return(-1); 
  if (x == 0) return(0);
  step = (x <= (1<<FIXPOINT_PREC)) ? (1<<FIXPOINT_PREC) : (1<<((FIXPOINT_BITS - 2 + FIXPOINT_PREC)>>1));
  test = 0;
  while (step != 0)
  {
    register int h;

    h = fixsquare(test + step);
    if (h <= x) {test += step;}
    if (h == x) break;
    step >>= 1;
  }
  return(test);
}


// Divides a fixpoint number by another fixpoint number, yielding a fixpoint result.
static inline int fixdiv(int x, int y)
{
  register int res, mask;
  register bool sign;

  sign = (x ^ y) < 0;
  if (x < 0) {x = -x;}
  if (y < 0) {y = -y;}
  mask = (1<<FIXPOINT_PREC); res = 0;
  while (x > y) {y <<= 1; mask <<= 1;}
  while (mask != 0)
  {
    if (x >= y) {res |= mask; x -= y;}
    mask >>= 1; y >>= 1;
  }
#ifdef FIXPOINT_SIGN
  if (res < 0) {res ^= FIXPOINT_SIGN;}
#endif
  if (sign) {res = -res;}
  return(res);
}





/*
 *  The C++ Fixpoint class. By no means exhaustive...
 *  Since it contains only one int data, variables of type FixPoint can be
 *  passed directly rather than as a reference.
 */

class FixPoint
{
private:
  int x;

public:
  FixPoint(void);
  FixPoint(int y);
  ~FixPoint(void);

  // conversions
  int Value(void);
  int round(void);
  operator int(void);

  // unary operators
  FixPoint sqrt(void);
  FixPoint sqr(void);
  FixPoint abs(void);
  FixPoint operator+(void);
  FixPoint operator-(void);
  FixPoint operator++(void);
  FixPoint operator--(void);

  // binary operators
  int imul(int y);
  FixPoint operator=(FixPoint y);
  FixPoint operator=(int y);
  FixPoint operator+(FixPoint y);
  FixPoint operator+(int y);
  FixPoint operator-(FixPoint y);
  FixPoint operator-(int y);
  FixPoint operator/(FixPoint y);
  FixPoint operator/(int y);
  FixPoint operator*(FixPoint y);
  FixPoint operator*(int y);
  FixPoint operator+=(FixPoint y);
  FixPoint operator+=(int y);
  FixPoint operator-=(FixPoint y);
  FixPoint operator-=(int y);
  FixPoint operator*=(FixPoint y);
  FixPoint operator*=(int y);
  FixPoint operator/=(FixPoint y);
  FixPoint operator/=(int y);
  FixPoint operator<<(int y);
  FixPoint operator>>(int y);
  FixPoint operator<<=(int y);
  FixPoint operator>>=(int y);

  // conditional operators
  bool operator<(FixPoint y);
  bool operator<(int y);
  bool operator<=(FixPoint y);
  bool operator<=(int y);
  bool operator>(FixPoint y);
  bool operator>(int y);
  bool operator>=(FixPoint y);
  bool operator>=(int y);
  bool operator==(FixPoint y);
  bool operator==(int y);
  bool operator!=(FixPoint y);
  bool operator!=(int y);
};


/*
 *  int gets treated differently according to the case:
 *
 *  a) Equations (=) or condition checks (==, <, <= ...): raw int (i.e. no conversion)
 *  b) As an argument for an arithmetic operation: conversion to fixpoint by shifting
 *
 *  Otherwise loading meaningful values into FixPoint variables would be very awkward.
 */

FixPoint::FixPoint(void) {x = 0;}

FixPoint::FixPoint(int y) {x = y;}

FixPoint::~FixPoint(void) {;}

inline int FixPoint::Value(void) {return(x);}

inline int FixPoint::round(void) {return((x + (1 << (FIXPOINT_PREC-1))) >> FIXPOINT_PREC);}

inline FixPoint::operator int(void) {return(x);}


// unary operators
inline FixPoint FixPoint::sqrt(void) {return(fixsqrt(x));}

inline FixPoint FixPoint::sqr(void) {return(fixsquare(x));}

inline FixPoint FixPoint::abs(void) {return((x < 0) ? -x : x);}

inline FixPoint FixPoint::operator+(void) {return(x);}

inline FixPoint FixPoint::operator-(void) {return(-x);}

inline FixPoint FixPoint::operator++(void) {x += (1 << FIXPOINT_PREC); return x;}

inline FixPoint FixPoint::operator--(void) {x -= (1 << FIXPOINT_PREC); return x;}


// binary operators
inline int FixPoint::imul(int y) {return(intmult(x,y));}

inline FixPoint FixPoint::operator=(FixPoint y) {x = y.Value(); return x;}

inline FixPoint FixPoint::operator=(int y) {x = y; return x;}

inline FixPoint FixPoint::operator+(FixPoint y) {return(x + y.Value());}

inline FixPoint FixPoint::operator+(int y) {return(x + (y << FIXPOINT_PREC));}

inline FixPoint FixPoint::operator-(FixPoint y) {return(x - y.Value());}

inline FixPoint FixPoint::operator-(int y) {return(x - (y << FIXPOINT_PREC));}

inline FixPoint FixPoint::operator/(FixPoint y) {return(fixdiv(x,y.Value()));}

inline FixPoint FixPoint::operator/(int y) {return(x/y);}

inline FixPoint FixPoint::operator*(FixPoint y) {return(fixmult(x,y.Value()));}

inline FixPoint FixPoint::operator*(int y) {return(x*y);}

inline FixPoint FixPoint::operator+=(FixPoint y) {x += y.Value(); return x;}

inline FixPoint FixPoint::operator+=(int y) {x += (y << FIXPOINT_PREC); return x;}

inline FixPoint FixPoint::operator-=(FixPoint y) {x -= y.Value(); return x;}

inline FixPoint FixPoint::operator-=(int y) {x -= (y << FIXPOINT_PREC); return x;}

inline FixPoint FixPoint::operator*=(FixPoint y) {x = fixmult(x,y.Value()); return x;}

inline FixPoint FixPoint::operator*=(int y) {x *= y; return x;}

inline FixPoint FixPoint::operator/=(FixPoint y) {x = fixdiv(x,y.Value()); return x;}

inline FixPoint FixPoint::operator/=(int y) {x /= y; return x;}

inline FixPoint FixPoint::operator<<(int y) {return(x << y);}

inline FixPoint FixPoint::operator>>(int y) {return(x >> y);}

inline FixPoint FixPoint::operator<<=(int y) {x <<= y; return x;}

inline FixPoint FixPoint::operator>>=(int y) {x >>= y; return x;}


// conditional operators
inline bool FixPoint::operator<(FixPoint y) {return(x < y.Value());}

inline bool FixPoint::operator<(int y) {return(x < y);}

inline bool FixPoint::operator<=(FixPoint y) {return(x <= y.Value());}

inline bool FixPoint::operator<=(int y) {return(x <= y);}

inline bool FixPoint::operator>(FixPoint y) {return(x > y.Value());}

inline bool FixPoint::operator>(int y) {return(x > y);}

inline bool FixPoint::operator>=(FixPoint y) {return(x >= y.Value());}

inline bool FixPoint::operator>=(int y) {return(x >= y);}

inline bool FixPoint::operator==(FixPoint y) {return(x == y.Value());}

inline bool FixPoint::operator==(int y) {return(x == y);}

inline bool FixPoint::operator!=(FixPoint y) {return(x != y.Value());}

inline bool FixPoint::operator!=(int y) {return(x != y);}



/*
 *  In case the first argument is an int (i.e. member-operators not applicable):
 *  Not supported: things like int/FixPoint. The same difference in conversions
 *  applies as mentioned above.
 */


// binary operators
inline FixPoint operator+(int x, FixPoint y) {return((x << FIXPOINT_PREC) + y.Value());}

inline FixPoint operator-(int x, FixPoint y) {return((x << FIXPOINT_PREC) - y.Value());}

inline FixPoint operator*(int x, FixPoint y) {return(x*y.Value());}


// conditional operators
inline bool operator==(int x, FixPoint y) {return(x == y.Value());}

inline bool operator!=(int x, FixPoint y) {return(x != y.Value());}

inline bool operator<(int x, FixPoint y) {return(x < y.Value());}

inline bool operator<=(int x, FixPoint y) {return(x <= y.Value());}

inline bool operator>(int x, FixPoint y) {return(x > y.Value());}

inline bool operator>=(int x, FixPoint y) {return(x >= y.Value());}



/*
 *  For more convenient creation of constant fixpoint numbers from constant floats.
 */

#define FixNo(n)	(FixPoint)((int)(n*(1<<FIXPOINT_PREC)))






/*
 *  Stuff re. the sinus table used with fixpoint arithmetic
 */


// define as global variable
FixPoint SinTable[(1<<ldSINTAB)];


#define FIXPOINT_SIN_COS_GENERIC \
  if (angle >= 3*(1<<ldSINTAB)) {return(-SinTable[(1<<(ldSINTAB+2)) - angle]);}\
  if (angle >= 2*(1<<ldSINTAB)) {return(-SinTable[angle - 2*(1<<ldSINTAB)]);}\
  if (angle >= (1<<ldSINTAB)) {return(SinTable[2*(1<<ldSINTAB) - angle]);}\
  return(SinTable[angle]);


// sin and cos: angle is fixpoint number 0 <= angle <= 2 (*PI)
static inline FixPoint fixsin(FixPoint x)
{
  int angle = x;

  angle = (angle >> (FIXPOINT_PREC - ldSINTAB - 1)) & ((1<<(ldSINTAB+2))-1);
  FIXPOINT_SIN_COS_GENERIC
}


static inline FixPoint fixcos(FixPoint x)
{
  int angle = x;

  angle = ((angle + (1<<(FIXPOINT_PREC-1))) >> (FIXPOINT_PREC - ldSINTAB - 1)) & ((1<<(ldSINTAB+2))-1);
  FIXPOINT_SIN_COS_GENERIC
}



static inline void InitFixSinTab(void)
{
  int i;
  float step;

  for (i=0, step=0; i<(1<<ldSINTAB); i++, step+=0.5/(1<<ldSINTAB))
  {
    SinTable[i] = FixNo(sin(M_PI * step));
  }
}
