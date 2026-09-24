#ifndef FXINST_TEST_FRAMEWORK_H
#define FXINST_TEST_FRAMEWORK_H 1
#ifdef __cplusplus
extern "C" {
#endif

#include "3dssnes9x.h"

#if RUN_GSU_TESTS == 1
void fxinst_test_run(void);
void fxinst_test_print_results(void);
void fxinst_test_reset(void);
#else
static inline void fxinst_test_run(void)           {} // Stub
static inline void fxinst_test_print_results(void) {} // Stub
static inline void fxinst_test_reset(void)         {} // Stub
#endif // RUN_GSU_TESTS

#ifdef __cplusplus
}
#endif // Extern C
#endif // FXINST_TEST_FRAMEWORK_H
