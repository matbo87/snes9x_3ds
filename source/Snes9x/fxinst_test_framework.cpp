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
// If 0, GSU permutations will be run in the outermost loop to improve performance.
#define PRINT_FAILURES 1

// If 1, a test will exit immediately if any individual run fails.
// If 0, a test will continue to run if any individual run fails.
#define FAIL_EARLY 1

#define LIKELY(cond_) __builtin_expect(!!(cond_), 1)
#define UNLIKELY(cond_) __builtin_expect(!!(cond_), 0)

typedef enum
{
    FAIL = 0,
    SUCCESS = 1,
    SKIP = 2,
} FX_TestResult;

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
    FX_Gsu GSU[0xF + 1];
    uint8 count;
    uint8 flagBits;
} FX_GsuBatch;

static bool hasRun = false;

#define ARRAY_COUNT(arr_) ((size_t) (sizeof(arr_) / sizeof(arr_[0])))

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

static constexpr const char* fxTestResultToString(FX_TestResult res)
{
    switch(res) {
        case FAIL: return "FAIL";
        case SUCCESS: return "PASS";
        case SKIP: return "SKIP";
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
                    printf("FLG %hu %s | %s\n", v1, printGsu(*GSU).buf, printResultFlags(res).buf);
            }
            if (UNLIKELY(res.result != res.expected))
            {
                success = false;
                if (PRINT_FAILURES)
                    printf("VAL %hu %s = %hu, exp %hu\n", v1, printGsu(*GSU).buf, res.result, res.expected);
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
                        printf("FLG %hu %hu %s | %s\n", v1, v2, printGsu(*GSU).buf, printResultFlags(res).buf);
                }
                if (UNLIKELY(res.result != res.expected))
                {
                    success = false;
                    if (PRINT_FAILURES)
                        printf("VAL %hu %hu %s = %hu, exp %hu\n", v1, v2, printGsu(*GSU).buf, res.result, res.expected);
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
                        printf("FLG %hu %hhu %s | %s\n", v1, imm, printGsu(*GSU).buf, printResultFlags(res).buf);
                }
                if (UNLIKELY(res.result != res.expected))
                {
                    success = false;
                    if (PRINT_FAILURES)
                        printf("VAL %hu %hhu %s = %hu, exp %hu\n", v1, imm, printGsu(*GSU).buf, res.result, res.expected);
                }
                if (FAIL_EARLY && !success)
                    goto loop_end;
            }
        }
    }

loop_end:
    return success ? SUCCESS : FAIL;
}

#define TEST(func_, runner_, reads_, writes_) do {                 \
    FX_GsuBatch GSU = gsuPermute(reads_, writes_);                 \
    printf("[%s] %s... ", printFlags(GSU.flagBits).buf, #func_);   \
    TickCounter tc;                                                \
    osTickCounterStart(&tc);                                       \
    FX_TestResult result = runner_(func_, GSU);                    \
    osTickCounterUpdate(&tc);                                      \
                                                                   \
    switch(result) {                                               \
        case FAIL: numFailed++; break;                             \
        case SUCCESS: numSuccess++; break;                         \
        case SKIP: numSkipped++; break;                            \
    }                                                              \
                                                                   \
    int seconds = osTickCounterRead(&tc) / 1000.0;                 \
    printf("%s in %ds\n", fxTestResultToString(result), seconds);  \
} while (0)

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

void fxinst_test_run(void)
{
    if (hasRun)
        return;
    hasRun = true;

    printf("     --- GSU Instruction Tests ---\n");
    printf("    These will take a very long time\n");
    // printf("      Press Start to Skip a Test\n");
    printf("Print individual failures: %c\n", PRINT_FAILURES ? 'Y' : 'N');
    printf("Exit failed tests early: %c\n", FAIL_EARLY ? 'Y' : 'N');
    
    uint32 numFailed = 0, numSuccess = 0, numSkipped = 0;
    
    // TEST(fxtest_lsr,   fxinst_test_run_v1,    0,   F_NZC);      // Passed in commit 2d6b68f
    // TEST(fxtest_rol,   fxinst_test_run_v1,    F_C, F_NZC);      // Passed in commit 2d6b68f
    // TEST(fxtest_loop,  fxinst_test_run_v1,    0,   F_NZ);       // Passed in commit 2d6b68f
    // TEST(fxtest_swap,  fxinst_test_run_v1,    0,   F_NZ);       // Passed in commit 29d941d
    // TEST(fxtest_not,   fxinst_test_run_v1,    0,   F_NZ);       // Passed in commit 767428a
    // TEST(fxtest_add_r, fxinst_test_run_v1_v2, 0,   F_NZCV);     // Passed in commit 2d6b68f
    // TEST(fxtest_adc_r, fxinst_test_run_v1_v2, F_C, F_NZCV);     // Passed in commit 2d6b68f
    // TEST(fxtest_add_i, fxinst_test_run_v1_imm, 0,   F_NZCV);    // Passed in commit bcf99bb
    // TEST(fxtest_adc_i, fxinst_test_run_v1_imm, F_C,   F_NZCV);  // Passed in commit bcf99bb
    // TEST(fxtest_sub_r, fxinst_test_run_v1_v2, 0,   F_NZCV);     // Passed in commit ee690e1
    // TEST(fxtest_sbc_r, fxinst_test_run_v1_v2, F_C,   F_NZCV);   // Passed in commit 21402fb
    // TEST(fxtest_sub_i, fxinst_test_run_v1_imm, 0,   F_NZCV);    // Passed in commit 6891c49
    // TEST(fxtest_cmp_r, fxinst_test_run_v1_v2, 0,   F_NZCV);     // Passed in commit 6891c49
    // TEST(fxtest_merge, fxinst_test_run_v1_v2, 0,   F_NZCV);     // Passed in commit c62ee05
    // TEST(fxtest_and_r, fxinst_test_run_v1_v2, 0,   F_NZ);       // Passed in commit 547689d
    // TEST(fxtest_bic_r, fxinst_test_run_v1_v2, 0,   F_NZ);       // Passed in commit 547689d
    // TEST(fxtest_and_i, fxinst_test_run_v1_imm, 0,   F_NZ);      // Passed in commit 547689d
    // TEST(fxtest_bic_i, fxinst_test_run_v1_imm, 0,   F_NZ);      // Passed in commit 547689d
    // TEST(fxtest_mult_r, fxinst_test_run_v1_v2, 0,   F_NZ);      // Passed in commit 4f5fc97 (run with 8-bit register range)
    // TEST(fxtest_umult_r, fxinst_test_run_v1_v2, 0,   F_NZ);     // Passed in commit 4f5fc97 (run with 8-bit register range)
    // TEST(fxtest_mult_i, fxinst_test_run_v1_imm, 0,   F_NZ);     // Passed in commit 4f5fc97
    // TEST(fxtest_umult_i, fxinst_test_run_v1_imm, 0,   F_NZ);    // Passed in commit 4f5fc97
    // TEST(fxtest_sex, fxinst_test_run_v1, 0,   F_NZ);            // Passed in commit 0f58d80
    // TEST(fxtest_asr, fxinst_test_run_v1, 0,   F_NZC);           // Passed in commit 0f58d80
    // TEST(fxtest_div2, fxinst_test_run_v1, 0,   F_NZC);          // Passed in commit 1370e6e
    // TEST(fxtest_ror, fxinst_test_run_v1, F_C,   F_NZC);         // Passed in commit 43a9aec
    // TEST(fxtest_lob, fxinst_test_run_v1, 0,   F_NZ);            // Passed in commit 59a8454
    // TEST(fxtest_fmult, fxinst_test_run_v1_v2, 0,   F_NZC);      // Passed in commit e8e55f8
    TEST(fxtest_from_r, fxinst_test_run_v1, 0,   F_NZV);        // Passed in commit WYATT_TODO
    TEST(fxtest_hib, fxinst_test_run_v1, 0,   F_NZ);            // Passed in commit WYATT_TODO
    TEST(fxtest_or_r, fxinst_test_run_v1_v2, 0,   F_NZ);        // Passed in commit WYATT_TODO
    TEST(fxtest_xor_r, fxinst_test_run_v1_v2, 0,   F_NZ);        // Passed in commit WYATT_TODO
    TEST(fxtest_or_i, fxinst_test_run_v1_imm, 0,   F_NZ);        // Passed in commit WYATT_TODO
    TEST(fxtest_xor_i, fxinst_test_run_v1_imm, 0,   F_NZ);        // Passed in commit WYATT_TODO
    
    printf("%d passed  %d failed  %d skipped\n", numSuccess, numFailed, numSkipped);
}

void fxinst_test_reset(void)
{
    hasRun = false;
}

// Copies the GSU overflow flag into ARM's CPSR.
// Tested to work with all possible 32-bit numbers.
/*void testOverflow(void)
{
    printf("Testing Overflow... ");
    bool allOk = true;
    for (uint64_t f = 0; f <= UINT32_MAX; f++) {
        uint32 flags = f;
        uint32 result;
        asm (
            "cmp %1, %1, lsr #1\n\t"
            "mrs %0, cpsr\n\t"
            : "=r" (result)
            : "r" (flags << (31 - ARM_V_SHIFT))
            : "cc"
        );

        if ((result & ARM_OVERFLOW) != (flags & ARM_OVERFLOW)) {
            allOk = false;
            break;
        }
    }
    if (allOk) printf("PASS\n"); else printf("FAIL\n");
}*/

// Copies the GSU carry flag into ARM's CPSR.
// Tested to work with all possible 32-bit numbers.
/*void testCarry(void)
{
    printf("Testing Carry... ");
    bool allOk = true;
    for (uint64_t f = 0; f <= UINT32_MAX; f++) {
        uint32 flags = f;
        uint32 result;
        asm (
            "cmn %1, %1\n\t"
            "mrs %0, cpsr\n\t"
            : "=r" (result)
            : "r" (flags << (31 - ARM_C_SHIFT))
            : "cc"
        );

        if ((result & ARM_CARRY) != (flags & ARM_CARRY)) {
            allOk = false;
            break;
        }
    }
    if (allOk) printf("PASS\n"); else printf("FAIL\n");
}*/

#endif
