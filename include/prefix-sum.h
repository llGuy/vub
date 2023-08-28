#ifndef _PREFIX_SUM_H_
#define _PREFIX_SUM_H_

#if defined(__cplusplus)
namespace PrefixSum {
typedef unsigned int ELEMT;
#else
#define ELEMT uint
#endif

struct ProcessorDescriptor {
  int status;

  ELEMT blockAggregate;
  ELEMT blockInclusivePrefix;

  /* Padding (may have to manually adjust) */
  ELEMT pad;
};

/* Unfortunately, these have to be compile-time constants. */
#define WARP_SIZE 32

#define NUM_VALUES_PER_THREAD 16
#define NUM_THREADS_PER_BLOCK 128

#if defined(__cplusplus)
} /* namespace PrefixSum */
#endif

#endif
