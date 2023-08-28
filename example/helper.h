#pragma once

#include <stdio.h>

#include "types.h"

#define PANIC_AND_EXIT(reason) {                                       \
  printf("\n*** Panic (%s:%d): %s ***\n", __FILE__, __LINE__, reason); \
  exit(-1);                                                            \
  }

inline uint32 popCount(uint32 bits) 
{
#ifndef __GNUC__
  return __popcnt(bits);
#else
  return __builtin_popcount(bits);
#endif
}

inline int divideRoundUp(int a, int b)
{
  return (a + b-1) / b;
}

template <typename T>
inline T roundUp(T input, T boundary)
{
  return boundary * ((input + (boundary - 1)) / boundary);
}
