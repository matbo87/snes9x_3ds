#include "3dssnes9x.h"
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <3ds.h>
#ifdef BIT
#undef BIT
#endif
#include "fxinst_test_framework.h"
#include "fxinst_tests.h"

#if RUN_GSU_TESTS == 1

// If 1, failed tests will be printed, and GSU permutations will run in the innermost loop for better debugging.
//   - Additionally, if 1, test results will NOT print on loop after all tests are finished.
// If 0, GSU permutations will be run in the outermost loop to improve performance.
// Not recommended for use with multithreading
#define PRINT_FAILURES 1

// If 1, a test will exit immediately if any individual run fails.
// If 0, a test will continue to run if any individual run fails.
#define FAIL_EARLY 1

// If 1, tests will run on as many CPU cores as possible.
// If 0, tests will run only on the main thread.
#define ENABLE_MULTITHREADING 0

// If 1, tests will run a full NZCV flag permutation.
// If 0, tests will run with their individually-configured flag permutations.
#define FORCE_NZCV 1

#if ENABLE_MULTITHREADING == 1
#define PRINTF_MUTEX(...) do {     \
    LightLock_Lock(&printLock);    \
    printf(__VA_ARGS__);           \
    LightLock_Unlock(&printLock);  \
} while (0)
#else
#define PRINTF_MUTEX(...) printf(__VA_ARGS__)
#endif

#define LIKELY(cond_) __builtin_expect(!!(cond_), 1)
#define UNLIKELY(cond_) __builtin_expect(!!(cond_), 0)
#define ARRAY_COUNT(arr_) ((size_t) (sizeof(arr_) / sizeof(arr_[0])))

typedef enum
{
    FAIL = 0,
    SUCCESS = 1,
} FX_TestResult;

typedef struct
{
    FX_TestResult type;
    int seconds;
} FX_TestReport;

typedef struct
{
    FX_Gsu GSU[0xF + 1];
    uint8 count;
    uint8 flagBits;
} FX_GsuBatch;

typedef struct
{
    bool hasRun;
    uint8 threadId;
    FX_TestReport result;
    FX_TestReport (*const runner)(void);
    const char* name;
    const FX_GsuBatch GSU;
} FX_Test;

// Flag print result
typedef struct
{
    char buf[sizeof("---- ----")];
} FX_FlagString;

// Flag print result
typedef struct
{
    char buf[sizeof("----")];
} FX_FlagStringShort;

typedef struct
{
    Thread thread; // Pointer type
    bool running;
} FX_TestThread;

static bool hasRun = false;
static volatile uint32 numFailed, numSuccess;
static volatile FX_TestThread threads[3];
static volatile bool runnersCanStart;
static volatile u32 nextTest;
static LightLock getNextTestLock, printLock;

static constexpr uint32 gsuToArm(FX_Gsu GSU)
{
    uint32 armFlags = 0;
    if (TEST_S)  armFlags |= ARM_NEGATIVE;
    if (TEST_Z)  armFlags |= ARM_ZERO;
    if (TEST_OV) armFlags |= ARM_OVERFLOW;
    if (TEST_CY) armFlags |= ARM_CARRY;
    return armFlags;
}

static constexpr FX_Gsu gsuCreate(uint8 flags)
{
    return (FX_Gsu) {
        .vCarry    = ((flags & PACKED_C) ? 1         : 0),
        .vZero     = ((flags & PACKED_Z) ? 0         : 1),
        .vSign     = ((flags & PACKED_N) ? 0x8000    : 0),
        .vOverflow = ((flags & PACKED_V) ? INT32_MAX : 0),
        .armFlags  = (flags << ARM_V_SHIFT),
    };
}

static constexpr FX_GsuBatch gsuPermute(uint8 reads, uint8 writes)
{
    FX_GsuBatch batch = {0};
    batch.flagBits = (reads | ~writes) & 0xF; // We need to test reads and non-written variables

    for (uint8 i = 0; i <= 0xF; i++)
        if (((i & ~batch.flagBits) == 0))
            batch.GSU[batch.count++] = gsuCreate(i);

    return batch;
}

static constexpr FX_FlagStringShort printFlags(uint8 flags)
{
    return (FX_FlagStringShort) {
        .buf = {
            [0] = (flags & PACKED_N) ? 'N' : '-',
            [1] = (flags & PACKED_Z) ? 'Z' : '-',
            [2] = (flags & PACKED_C) ? 'C' : '-',
            [3] = (flags & PACKED_V) ? 'V' : '-',
            [4] = '\0',
        }
    };
}

static constexpr FX_FlagStringShort printGsu(FX_Gsu gsu)
{
    return printFlags(gsu.armFlags >> (31 - PACKED_N_SHIFT));
}

static constexpr FX_FlagString printResultFlags(FX_Result f)
{
    return (FX_FlagString) {
        .buf = {
            [0] = (f.armFlags & PACKED_N) ? 'N' : '-',
            [1] = (f.armFlags & PACKED_Z) ? 'Z' : '-',
            [2] = (f.armFlags & PACKED_C) ? 'C' : '-',
            [3] = (f.armFlags & PACKED_V) ? 'V' : '-',
            [4] = ' ',
            [5] = (f.gsuFlags & PACKED_N) ? 'S' : '-',
            [6] = (f.gsuFlags & PACKED_Z) ? 'Z' : '-',
            [7] = (f.gsuFlags & PACKED_C) ? 'C' : '-',
            [8] = (f.gsuFlags & PACKED_V) ? 'V' : '-',
            [9] = '\0',
        }
    };
}

static constexpr FX_FlagString printResultFlags32(FX_Result32 f)
{
    return printResultFlags((FX_Result) {.gsuFlags = f.gsuFlags, .armFlags = f.armFlags});
}

static constexpr const char* fxTestResultToString(FX_TestResult res)
{
    switch(res) {
        case FAIL: return "FAIL";
        case SUCCESS: return "PASS";
        default: return "????";
    }
}

// Test all combinations of 1x u16 (16 bits total, plus GSU)
static inline FX_TestResult fxinst_test_run_v1(FX_Result (*testFunc) (const FX_Gsu*, uint16), const FX_GsuBatch gsuBatch)
{
    bool success = true;

#if PRINT_FAILURES == 1
    for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
        for (uint8 gsu = 0; gsu < gsuBatch.count; gsu++)
#else
    for (uint8 gsu = 0; gsu < gsuBatch.count; gsu++) {
        #pragma GCC unroll 64
        for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++)
#endif
        {
            const FX_Gsu* GSU = &gsuBatch.GSU[gsu];
            FX_Result res = testFunc(GSU, v1);
            if (UNLIKELY(res.armFlags != res.gsuFlags))
            {
                success = false;
                if (PRINT_FAILURES)
                    PRINTF_MUTEX("FLG %hu %s | %s\n", v1, printGsu(*GSU).buf, printResultFlags(res).buf);
            }
            if (UNLIKELY(res.result != res.expected))
            {
                success = false;
                if (PRINT_FAILURES)
                    PRINTF_MUTEX("VAL %hu %s = %hu, exp %hu\n", v1, printGsu(*GSU).buf, res.result, res.expected);
            }
            if (FAIL_EARLY && !success)
                goto loop_end;
        }
    }

loop_end:
    return success ? SUCCESS : FAIL;
}

// Test all combinations of 1x u16 (32 bits total, plus GSU)
static inline FX_TestResult fxinst_test_run_v1_v2(FX_Result (*testFunc) (const FX_Gsu*, uint16, uint16), const FX_GsuBatch gsuBatch)
{
    bool success = true;

#if PRINT_FAILURES == 1
    for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
        for (uint32 v2 = 0; v2 <= UINT16_MAX; v2++) {
            for (uint8 gsu = 0; gsu < gsuBatch.count; gsu++)
#else
    for (uint8 gsu = 0; gsu < gsuBatch.count; gsu++) {
        for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
            #pragma GCC unroll 64
            for (uint32 v2 = 0; v2 <= UINT16_MAX; v2++)
#endif
            {
                const FX_Gsu* GSU = &gsuBatch.GSU[gsu];
                FX_Result res = testFunc(GSU, v1, v2);
                if (UNLIKELY(res.armFlags != res.gsuFlags))
                {
                    success = false;
                    if (PRINT_FAILURES)
                        PRINTF_MUTEX("FLG %hu %hu %s | %s\n", v1, v2, printGsu(*GSU).buf, printResultFlags(res).buf);
                }
                if (UNLIKELY(res.result != res.expected))
                {
                    success = false;
                    if (PRINT_FAILURES)
                        PRINTF_MUTEX("VAL %hu %hu %s = %hu, exp %hu\n", v1, v2, printGsu(*GSU).buf, res.result, res.expected);
                }
                if (FAIL_EARLY && !success)
                    goto loop_end;
            }
        }
    }

loop_end:
    return success ? SUCCESS : FAIL;
}

// Test all combinations of 1x u16 (32 bits total, plus GSU), but with 32-bit results.
static inline FX_TestResult fxinst_test_run_v1_v2_res32(FX_Result32 (*testFunc) (const FX_Gsu*, uint16, uint16), const FX_GsuBatch gsuBatch)
{
    bool success = true;

#if PRINT_FAILURES == 1
    for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
        for (uint32 v2 = 0; v2 <= UINT16_MAX; v2++) {
            for (uint8 gsu = 0; gsu < gsuBatch.count; gsu++)
#else
    for (uint8 gsu = 0; gsu < gsuBatch.count; gsu++) {
        for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
            #pragma GCC unroll 64
            for (uint32 v2 = 0; v2 <= UINT16_MAX; v2++)
#endif
            {
                const FX_Gsu* GSU = &gsuBatch.GSU[gsu];
                FX_Result32 res = testFunc(GSU, v1, v2);
                if (UNLIKELY(res.armFlags != res.gsuFlags))
                {
                    success = false;
                    if (PRINT_FAILURES)
                        PRINTF_MUTEX("FLG %hu %hu %s | %s\n", v1, v2, printGsu(*GSU).buf, printResultFlags32(res).buf);
                }
                if (UNLIKELY(res.result != res.expected))
                {
                    success = false;
                    if (PRINT_FAILURES)
                        PRINTF_MUTEX("VAL %hu %hu %s = %hu, exp %hu\n", v1, v2, printGsu(*GSU).buf, res.result, res.expected);
                }
                if (FAIL_EARLY && !success)
                    goto loop_end;
            }
        }
    }

loop_end:
    return success ? SUCCESS : FAIL;
}

// Test all combinations of 1x u16 (32 bits total, plus GSU)
static inline FX_TestResult fxinst_test_run_v1_imm(FX_Result (*testFunc) (const FX_Gsu*, uint16, uint8), const FX_GsuBatch gsuBatch)
{
    bool success = true;

#if PRINT_FAILURES == 1
    for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
        for (uint32 imm = 0; imm <= 0xf; imm++) {
            for (uint8 gsu = 0; gsu < gsuBatch.count; gsu++)
#else
    for (uint8 gsu = 0; gsu < gsuBatch.count; gsu++) {
        for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
            #pragma GCC unroll 16
            for (uint32 imm = 0; imm <= 0xf; imm++)
#endif
            {
                const FX_Gsu* GSU = &gsuBatch.GSU[gsu];
                FX_Result res = testFunc(GSU, v1, imm);
                if (UNLIKELY(res.armFlags != res.gsuFlags))
                {
                    success = false;
                    if (PRINT_FAILURES)
                        PRINTF_MUTEX("FLG %hu %hhu %s | %s\n", v1, imm, printGsu(*GSU).buf, printResultFlags(res).buf);
                }
                if (UNLIKELY(res.result != res.expected))
                {
                    success = false;
                    if (PRINT_FAILURES)
                        PRINTF_MUTEX("VAL %hu %hhu %s = %hu, exp %hu\n", v1, imm, printGsu(*GSU).buf, res.result, res.expected);
                }
                if (FAIL_EARLY && !success)
                    goto loop_end;
            }
        }
    }

loop_end:
    return success ? SUCCESS : FAIL;
}

#define F_V    (PACKED_V)
#define F_C    (PACKED_C)
#define F_CV   (PACKED_C | PACKED_V)
#define F_Z    (PACKED_Z)
#define F_ZV   (PACKED_Z | PACKED_V)
#define F_ZC   (PACKED_Z | PACKED_C)
#define F_ZCV  (PACKED_Z | PACKED_C | PACKED_V)
#define F_N    (PACKED_N)
#define F_NV   (PACKED_N | PACKED_V)
#define F_NC   (PACKED_N | PACKED_C)
#define F_NCV  (PACKED_N | PACKED_C | PACKED_V)
#define F_NZ   (PACKED_N | PACKED_Z)
#define F_NZV  (PACKED_N | PACKED_Z | PACKED_V)
#define F_NZC  (PACKED_N | PACKED_Z | PACKED_C)
#define F_NZCV (PACKED_N | PACKED_Z | PACKED_C | PACKED_V)

#define TESTS(GEN_, OFF_) \
    OFF_(fxtest_lsr,     fxinst_test_run_v1,          0,   F_NZC,  "Passed in commit 104bf93 (NZCV)")  \
    OFF_(fxtest_rol,     fxinst_test_run_v1,          F_C, F_NZC,  "Passed in commit 104bf93 (NZCV)")  \
    OFF_(fxtest_loop,    fxinst_test_run_v1,          0,   F_NZ,   "Passed in commit 104bf93 (NZCV)")  \
    GEN_(fxtest_swap,    fxinst_test_run_v1,          0,   F_NZ,   "Passed in commit WYATT_TODO (NZCV)")  \
    OFF_(fxtest_not,     fxinst_test_run_v1,          0,   F_NZ,   "Passed in commit 104bf93 (NZCV)")  \
    OFF_(fxtest_sex,     fxinst_test_run_v1,          0,   F_NZ,   "Passed in commit 104bf93 (NZCV)")  \
    OFF_(fxtest_asr,     fxinst_test_run_v1,          0,   F_NZC,  "Passed in commit 104bf93 (NZCV)")  \
    OFF_(fxtest_div2,    fxinst_test_run_v1,          0,   F_NZC,  "Passed in commit 104bf93 (NZCV)")  \
    OFF_(fxtest_ror,     fxinst_test_run_v1,          F_C, F_NZC,  "Passed in commit 104bf93 (NZCV)")  \
    GEN_(fxtest_lob,     fxinst_test_run_v1,          0,   F_NZ,   "Passed in commit 60e78ef (NZCV)")  \
    OFF_(fxtest_from_r,  fxinst_test_run_v1,          0,   F_NZV,  "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_hib,     fxinst_test_run_v1,          0,   F_NZ,   "Passed in commit 104bf93 (NZCV)")  \
    OFF_(fxtest_inc_r,   fxinst_test_run_v1,          0,   F_NZ,   "Passed in commit 104bf93 (NZCV)")  \
    OFF_(fxtest_dec_r,   fxinst_test_run_v1,          0,   F_NZ,   "Passed in commit 104bf93 (NZCV)")  \
    OFF_(fxtest_add_i,   fxinst_test_run_v1_imm,      0,   F_NZCV, "Passed in commit 89e3b57 (NZCV)")  \
    OFF_(fxtest_adc_i,   fxinst_test_run_v1_imm,      F_C, F_NZCV, "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_sub_i,   fxinst_test_run_v1_imm,      0,   F_NZCV, "Passed in commit 89e3b57 (NZCV)")  \
    OFF_(fxtest_and_i,   fxinst_test_run_v1_imm,      0,   F_NZ,   "Passed in commit 89e3b57 (NZCV)")  \
    OFF_(fxtest_bic_i,   fxinst_test_run_v1_imm,      0,   F_NZ,   "Passed in commit 89e3b57 (NZCV)")  \
    OFF_(fxtest_mult_i,  fxinst_test_run_v1_imm,      0,   F_NZ,   "Passed in commit 89e3b57 (NZCV)")  \
    OFF_(fxtest_umult_i, fxinst_test_run_v1_imm,      0,   F_NZ,   "Passed in commit 89e3b57 (NZCV)")  \
    OFF_(fxtest_or_i,    fxinst_test_run_v1_imm,      0,   F_NZ,   "Passed in commit 89e3b57 (NZCV)")  \
    OFF_(fxtest_xor_i,   fxinst_test_run_v1_imm,      0,   F_NZ,   "Passed in commit 89e3b57 (NZCV)")  \
    OFF_(fxtest_add_r,   fxinst_test_run_v1_v2,       0,   F_NZCV, "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_adc_r,   fxinst_test_run_v1_v2,       F_C, F_NZCV, "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_sub_r,   fxinst_test_run_v1_v2,       0,   F_NZCV, "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_sbc_r,   fxinst_test_run_v1_v2,       F_C, F_NZCV, "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_cmp_r,   fxinst_test_run_v1_v2,       0,   F_NZCV, "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_merge,   fxinst_test_run_v1_v2,       0,   F_NZCV, "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_and_r,   fxinst_test_run_v1_v2,       0,   F_NZ,   "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_bic_r,   fxinst_test_run_v1_v2,       0,   F_NZ,   "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_mult_r,  fxinst_test_run_v1_v2,       0,   F_NZ,   "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_umult_r, fxinst_test_run_v1_v2,       0,   F_NZ,   "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_fmult,   fxinst_test_run_v1_v2,       0,   F_NZC,  "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_lmult,   fxinst_test_run_v1_v2_res32, 0,   F_NZC,  "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_or_r,    fxinst_test_run_v1_v2,       0,   F_NZ,   "Passed in commit 30ac96e (NZCV)")  \
    OFF_(fxtest_xor_r,   fxinst_test_run_v1_v2,       0,   F_NZ,   "Passed in commit 30ac96e (NZCV)")

// Generates a test function.
#define TEST_FUNC_OFF(func_, runner_, reads_, writes_, pass_str_)
#define TEST_FUNC_GEN(func_, runner_, reads_, writes_, pass_str_)              \
FX_TestReport test_run_ ## func_(void)                                         \
{                                                                              \
    const FX_GsuBatch GSU = gsuPermute(FORCE_NZCV ? F_NZCV : reads_, writes_); \
    TickCounter tc;                                                            \
    osTickCounterStart(&tc);                                                   \
    FX_TestResult result = runner_(func_, GSU);                                \
    osTickCounterUpdate(&tc);                                                  \
                                                                               \
    int seconds = osTickCounterRead(&tc) / 1000.0;                             \
    return (FX_TestReport) {.type = result, .seconds = seconds};               \
}

// Generates an FX_Test array.
#define TEST_ARRAY_OFF(func_, runner_, reads_, writes_, pass_str_)
#define TEST_ARRAY_GEN(func_, runner_, reads_, writes_, pass_str_) \
(FX_Test) {                                                        \
    .runner = test_run_ ## func_,                                  \
    .name = #func_,                                                \
    .GSU = gsuPermute(FORCE_NZCV ? F_NZCV : reads_, writes_),      \
},

// Create the test functions
TESTS(TEST_FUNC_GEN, TEST_FUNC_OFF)

// Create the FX_Test array
FX_Test tests[] {
    TESTS(TEST_ARRAY_GEN, TEST_ARRAY_OFF)
};

// Nullable
static FX_Test* getNextTest(void)
{
	LightLock_Lock(&getNextTestLock);

    FX_Test* test = NULL;
    if (nextTest < ARRAY_COUNT(tests)) {
        test = &tests[nextTest];
        nextTest += 1;
    }

    LightLock_Unlock(&getNextTestLock);

    return test;
}

static void threadFunc(void* param)
{
    volatile FX_TestThread* thread = (volatile FX_TestThread*) param;
    thread->running = true;

    while (!runnersCanStart)
        svcSleepThread(100000000); // 100 millis

    size_t threadId = thread - threads;
    
    FX_Test* test = NULL;
    while ((test = getNextTest()) != NULL) {
        test->threadId = (uint8) threadId;
        // PRINTF_MUTEX("Thread %d starting %s\n", thread - threads, test->name);
        TickCounter tc;
        osTickCounterStart(&tc);
        test->result = test->runner();
        osTickCounterUpdate(&tc);
        
        test->hasRun = true;
        switch(test->result.type) {
            case FAIL: AtomicIncrement(&numFailed); break;
            case SUCCESS: AtomicIncrement(&numSuccess); break;
        }

        PRINTF_MUTEX("%u [%s] %s: %s in %ds\n", threadId, printFlags(test->GSU.flagBits).buf, test->name, fxTestResultToString(test->result.type), test->result.seconds);
    }

    thread->running = false;
}

static bool startThread(size_t id)
{
    threads[id].thread = threadCreate(threadFunc, (void*) &threads[id], 0x4000, 0x18, id, true);
    if (threads[id].thread != NULL)
        while (!threads[id].running)
            svcSleepThread(100000000); // 100 millis
    
    return threads[id].thread != NULL;
}

void fxinst_test_run(void)
{
    if (hasRun) {
        if (!PRINT_FAILURES)
            fxinst_test_print_results();
        return;
    }
    hasRun = true;
    runnersCanStart = false;

    LightLock_Init(&getNextTestLock);
    LightLock_Init(&printLock);

    printf("     --- GSU Instruction Tests ---\n");
    printf("    These will take a very long time\n");
    printf("Print individual failures: %c\n", PRINT_FAILURES ? 'Y' : 'N');
    printf("Exit failed tests early: %c\n", FAIL_EARLY ? 'Y' : 'N');
    printf("Force NZCV: %c\n", FORCE_NZCV ? 'Y' : 'N');

    bool new3ds = false;
    u32 oldTimeLimit;
    APT_CheckNew3DS(&new3ds);
    printf("New 3DS Detected: %c\n", new3ds ? 'Y' : 'N');
    printf("Number of tests enabled: %u\n", ARRAY_COUNT(tests));

    if (ENABLE_MULTITHREADING)
    {
        APT_GetAppCpuTimeLimit(&oldTimeLimit);
        APT_SetAppCpuTimeLimit(80);
        printf("Running on threads: 0");
        printf(", %c", startThread(1) ? '1' : '-');
        if (new3ds)
            printf(", %c", startThread(2) ? '2' : '-');
        printf("\n");
    }
    else
    {
        printf("Multithreading is disabled.\n");
    }

    runnersCanStart = true;
    threadFunc((void*) &threads[0]);
    
    // Wait for all threads to finish
    for (size_t i = 0; i < ARRAY_COUNT(threads); i++)
        while (threads[i].running)
            svcSleepThread(100000000); // 100 millis

    printf("%d passed  %d failed\n", numSuccess, numFailed);

    if (ENABLE_MULTITHREADING)
        APT_SetAppCpuTimeLimit(oldTimeLimit);
}

void fxinst_test_print_results(void)
{
    if (ARRAY_COUNT(tests) == 0)
        return;

    printf("\n     --- GSU Instruction Tests ---\n");

    for (size_t i = 0; i < ARRAY_COUNT(tests); i++)
    {
        FX_Test* t = &tests[i];
        if (tests[i].hasRun)
            printf("%u [%s] %s: %s in %ds\n", t->threadId, printFlags(t->GSU.flagBits).buf, t->name, fxTestResultToString(t->result.type), t->result.seconds);
        else
            printf("%s has not run\n", t->name);
    }
    
    printf("%d passed  %d failed\n", numSuccess, numFailed);
}

// Do not call this while any tests are running.
void fxinst_test_reset(void)
{
    hasRun = false;
    runnersCanStart = false;
}

#endif
