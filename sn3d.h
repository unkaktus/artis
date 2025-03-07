#ifndef SN3D_H
#define SN3D_H

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "artisoptions.h"
#include "globals.h"

extern FILE *output_file;
#ifndef __CUDA_ARCH__
// host code

#define __artis_assert(e)                                                                                            \
  if (!(e)) {                                                                                                        \
    if (output_file != NULL) {                                                                                       \
      (void)fprintf(output_file, "[rank %d] %s:%d: failed assertion `%s' in function %s\n", globals::rank_global,    \
                    __FILE__, __LINE__, #e, __PRETTY_FUNCTION__);                                                    \
    }                                                                                                                \
    (void)fprintf(stderr, "[rank %d] %s:%d: failed assertion `%s' in function %s\n", globals::rank_global, __FILE__, \
                  __LINE__, #e, __PRETTY_FUNCTION__);                                                                \
    abort();                                                                                                         \
  }
//

#define assert_always(e) __artis_assert(e)

#if defined TESTMODE && TESTMODE
#define assert_testmodeonly(e) __artis_assert(e)
#else
#define assert_testmodeonly(e) ((void)0)
#endif

// #define printout(...) fprintf(output_file, __VA_ARGS__)

extern int tid;

template <typename... Args>
static int printout(const char *format, Args... args) {
  if (globals::startofline[tid]) {
    time_t now_time = time(NULL);
    char s[32] = "";
    strftime(s, 32, "%FT%TZ", gmtime(&now_time));
    fprintf(output_file, "%s ", s);
  }
  globals::startofline[tid] = (format[strlen(format) - 1] == '\n');
  return fprintf(output_file, format, args...);
}

static int printout(const char *format) {
  if (globals::startofline[tid]) {
    time_t now_time = time(NULL);
    char s[32] = "";
    strftime(s, 32, "%FT%TZ", gmtime(&now_time));
    fprintf(output_file, "%s ", s);
  }
  globals::startofline[tid] = (format[strlen(format) - 1] == '\n');
  return fprintf(output_file, "%s", format);
}

static inline int get_bflutindex(const int tempindex, const int element, const int ion, const int level,
                                 const int phixstargetindex) {
  const int contindex = -1 - globals::elements[element].ions[ion].levels[level].cont_index + phixstargetindex;

  return tempindex * globals::nbfcontinua + contindex;
}

#ifdef _OPENMP
#ifndef __CUDACC__
#define safeadd(var, val) _Pragma("omp atomic update") var += val
#else
#define safeadd(var, val) var += val
#endif
#else
#define safeadd(var, val) var += val
#endif

#else
// device code

#define printout(...) printf(__VA_ARGS__)

#define assert_always(e) assert(e)

#if defined TESTMODE && TESTMODE
#define assert_testmodeonly(e) assert(e)
#else
#define assert_testmodeonly(e) ((void)0)
#endif

#define safeadd(var, val) atomicAdd(&var, val)

#endif

#define safeincrement(var) safeadd(var, 1)

#include <gsl/gsl_integration.h>
#include <stdarg.h>  /// MK: needed for printout()

#include "cuda.h"

// #define DO_TITER
// #define FORCE_LTE

#if (DETAILED_BF_ESTIMATORS_ON && !NO_LUT_PHOTOION)
#error Must use NO_LUT_PHOTOION with DETAILED_BF_ESTIMATORS_ON
#endif

#if !defined MPI_ON
// #define MPI_ON //only needed for debugging MPI, the makefile will switch this on
#endif

#ifdef MPI_ON
#include <mpi.h>
#endif

// #define _OPENMP
#ifdef _OPENMP
#include "omp.h"
#endif

#define COOLING_UNDEFINED -99

#define RPKT_EVENTTYPE_BB 550
#define RPKT_EVENTTYPE_CONT 551

extern int tid;
extern __managed__ bool use_cellhist;
extern __managed__ bool neutral_flag;
#ifndef __CUDA_ARCH__

#include <gsl/gsl_rng.h>
extern gsl_rng *rng;  // pointer for random number generator
#else
extern __device__ void *rng;
#endif
extern gsl_integration_workspace *gslworkspace;
extern __managed__ int myGpuId;

#ifdef _OPENMP
#pragma omp threadprivate(tid, myGpuId, use_cellhist, neutral_flag, rng, gslworkspace, output_file)
#endif

#include "globals.h"
#include "vectors.h"

static inline void gsl_error_handler_printout(const char *reason, const char *file, int line, int gsl_errno) {
  if (gsl_errno != 18)  // roundoff error
  {
    printout("WARNING: gsl (%s:%d): %s (Error code %d)\n", file, line, reason, gsl_errno);
    // abort();
  }
}

static FILE *fopen_required(const char *filename, const char *mode) {
  FILE *file = fopen(filename, mode);
  if (file == NULL) {
    printout("ERROR: Could not open file '%s' for mode '%s'.\n", filename, mode);
    abort();
  }

  return file;
}

static int get_timestep(const double time) {
  assert_always(time >= globals::tmin);
  assert_always(time < globals::tmax);
  for (int nts = 0; nts < globals::ntstep; nts++) {
    const double tsend = (nts < (globals::ntstep - 1)) ? globals::time_step[nts + 1].start : globals::tmax;
    if (time >= globals::time_step[nts].start && time < tsend) {
      return nts;
    }
  }
  assert_always(false);  // could not find matching timestep

  return -1;
}

__host__ __device__ inline int get_max_threads(void) {
#ifdef __CUDA_ARCH__
  return MCUDATHREADS;
#elif defined _OPENMP
  return omp_get_max_threads();
#else
  return 1;
#endif
}

__host__ __device__ inline int get_num_threads(void) {
#ifdef __CUDA_ARCH__
  return blockDim.x * blockDim.y * blockDim.z;
#elif defined _OPENMP
  return omp_get_num_threads();
#else
  return 1;
#endif
}

__host__ __device__ inline int get_thread_num(void) {
#ifdef __CUDA_ARCH__
  return threadIdx.x + blockDim.x * blockIdx.x;
#elif defined _OPENMP
  return omp_get_thread_num();
#else
  return 0;
#endif
}

#endif  // SN3D_H
