#include "3dssnes9x.h"
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <3ds.h>
#undef BIT
#include "fxinst_test_framework.h"
#include "fxinst_tests.h"

#if RUN_GSU_TESTS == 1

#define LIKELY(cond_) __builtin_expect(!!(cond_), 1)
#define UNLIKELY(cond_) __builtin_expect(!!(cond_), 0)

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned char bool8;
typedef signed char int8;
typedef short int16;
typedef int int32;

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

typedef struct Fx_Test_
{
    const char* name;
    void* testFunc;
    FX_TestResult (*runner) (const struct Fx_Test_*, bool printFailures);
} FX_Test;

static bool hasRun = false;

#define ARRAY_COUNT(arr_) ((size_t) (sizeof(arr_) / sizeof(arr_[0])))

static inline uint32 gsuToArm(FX_Gsu GSU)
{
    uint32 armFlags = 0;
    if (TEST_S)  armFlags |= ARM_NEGATIVE;
    if (TEST_Z)  armFlags |= ARM_ZERO;
    if (TEST_OV) armFlags |= ARM_OVERFLOW;
    if (TEST_CY) armFlags |= ARM_CARRY;
    return armFlags;
}

static inline FX_Gsu gsuCreate(bool vCarry, bool vZero, bool vSign, bool vOverflow)
{
    FX_Gsu GSU = {
        .vCarry    = vCarry    ? 1         : 0,
        .vZero     = vZero     ? 0         : 1,
        .vSign     = vSign     ? 0x8000    : 0,
        .vOverflow = vOverflow ? INT32_MAX : 0,
    };
    GSU.armFlags = gsuToArm(GSU);
    return GSU;
}

static FX_FlagStringShort printGsu(FX_Gsu gsu)
{
    FX_FlagStringShort pf;
    pf.buf[0] = (gsu.armFlags & ARM_NEGATIVE) ? 'N' : '-';
    pf.buf[1] = (gsu.armFlags & ARM_ZERO)     ? 'Z' : '-';
    pf.buf[2] = (gsu.armFlags & ARM_CARRY)    ? 'C' : '-';
    pf.buf[3] = (gsu.armFlags & ARM_OVERFLOW) ? 'V' : '-';
    pf.buf[4] = '\0';
    return pf;
}

static FX_FlagString printFlags(FX_Result f)
{
    FX_FlagString pf;
    pf.buf[0] = (f.armFlags & PACKED_N) ? 'N' : '-';
    pf.buf[1] = (f.armFlags & PACKED_Z) ? 'Z' : '-';
    pf.buf[2] = (f.armFlags & PACKED_C) ? 'C' : '-';
    pf.buf[3] = (f.armFlags & PACKED_V) ? 'V' : '-';
    pf.buf[4] = ' ';
    pf.buf[5] = (f.gsuFlags & PACKED_N) ? 'S' : '-';
    pf.buf[6] = (f.gsuFlags & PACKED_Z) ? 'Z' : '-';
    pf.buf[7] = (f.gsuFlags & PACKED_C) ? 'C' : '-';
    pf.buf[8] = (f.gsuFlags & PACKED_V) ? 'V' : '-';
    pf.buf[9] = '\0';
    return pf;
}

static const char* fxTestResultToString(FX_TestResult res)
{
    switch(res) {
        case FAIL: return "FAIL";
        case SUCCESS: return "PASS";
        case SKIP: return "SKIP";
        default: return "????";
    }
}

// Test all combinations of: 1x u16, 1x overflow bit (17 bits total)
FX_TestResult fxinst_test_run_v1_v(const FX_Test* test, bool printFailures)
{
    FX_Result (*testFunc) (const FX_Gsu*, uint16) = test->testFunc;
    const FX_Gsu GSU[2] = {
        gsuCreate(0, 0, 0, 0),
        gsuCreate(0, 0, 0, 1),
    };

    bool success = true;
    for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
        for (uint8 flg = 0; flg < ARRAY_COUNT(GSU); flg++)
        {
            FX_Result res = testFunc(&GSU[flg], v1);
            if (UNLIKELY(res.armFlags != res.gsuFlags))
            {
                success = false;
                if (printFailures)
                    printf("FLG %hu %s | %s\n", v1, printGsu(GSU[flg]).buf, printFlags(res).buf);
            }
            if (UNLIKELY(res.result != res.expected))
            {
                success = false;
                if (printFailures)
                    printf("VAL %hu %s = %hu, exp %hu\n", v1, printGsu(GSU[flg]).buf, res.result, res.expected);
            }
        }
    }
    
    return success ? SUCCESS : FAIL;
}

// Test all combinations of: 1x u16, 1x carry bit, 1x overflow bit (18 bits total)
FX_TestResult fxinst_test_run_v1_cv(const FX_Test* test, bool printFailures)
{
    FX_Result (*testFunc) (const FX_Gsu*, uint16) = test->testFunc;
    const FX_Gsu GSU[] = {
        gsuCreate(0, 0, 0, 0),
        gsuCreate(1, 0, 0, 0),
        gsuCreate(0, 0, 0, 1),
        gsuCreate(1, 0, 0, 1),
    };

    bool success = true;
    for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
        for (uint8 flg = 0; flg < ARRAY_COUNT(GSU); flg++)
        {
            FX_Result res = testFunc(&GSU[flg], v1);
            if (UNLIKELY(res.armFlags != res.gsuFlags))
            {
                success = false;
                if (printFailures)
                    printf("FLG %hu %s | %s\n", v1, printGsu(GSU[flg]).buf, printFlags(res).buf);
            }
            if (UNLIKELY(res.result != res.expected))
            {
                success = false;
                if (printFailures)
                    printf("VAL %hu %s = %hu, exp %hu\n", v1, printGsu(GSU[flg]).buf, res.result, res.expected);
            }
        }
    }
    
    return success ? SUCCESS : FAIL;
}

// Test all combinations of: 2x u16 (32 bits total)
FX_TestResult fxinst_test_run_v1_v2(const FX_Test* test, bool printFailures)
{
    FX_Result (*testFunc) (const FX_Gsu*, uint16, uint16) = test->testFunc;
    const FX_Gsu GSU = {0};

    bool success = true;
    for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
        for (uint32 v2 = 0; v2 <= UINT16_MAX; v2++) {
            FX_Result res = testFunc(&GSU, v1, v2);
            if (UNLIKELY(res.armFlags != res.gsuFlags))
            {
                success = false;
                if (printFailures)
                    printf("FLG %hu %hu | %s\n", v1, v2, printFlags(res).buf);
            }
            if (UNLIKELY(res.result != res.expected))
            {
                success = false;
                if (printFailures)
                    printf("VAL %hu %hu = %hu, exp %hu\n", v1, v2, res.result, res.expected);
            }
        }
    }
    
    return success ? SUCCESS : FAIL;
}

// Test all combinations of: 2x u16, 1x carry bit (33 bits total)
FX_TestResult fxinst_test_run_v1_v2_c(const FX_Test* test, bool printFailures)
{
    FX_Result (*testFunc) (const FX_Gsu*, uint16, uint16) = test->testFunc;
    const FX_Gsu GSU[2] = {
        gsuCreate(0, 0, 0, 0),
        gsuCreate(1, 0, 0, 0),
    };

    bool success = true;
    for (uint32 v1 = 0; v1 <= UINT16_MAX; v1++) {
        for (uint32 v2 = 0; v2 <= UINT16_MAX; v2++) {
            for (uint8 flg = 0; flg < ARRAY_COUNT(GSU); flg++)
            {
                FX_Result res = testFunc(&GSU[flg], v1, v2);
                if (UNLIKELY(res.armFlags != res.gsuFlags))
                {
                    success = false;
                    if (printFailures)
                        printf("FLG %hu %hu %s | %s\n", v1, v2, printGsu(GSU[flg]).buf, printFlags(res).buf);
                }
                if (UNLIKELY(res.result != res.expected))
                {
                    success = false;
                    if (printFailures)
                        printf("VAL %hu %hu %s = %hu, exp %hu\n", v1, v2, printGsu(GSU[flg]).buf, res.result, res.expected);
                }
            }
        }
    }

    return success ? SUCCESS : FAIL;
}

#define TEST(func_, runner_) (FX_Test) {.name = #func_, .testFunc = func_, .runner = runner_}
FX_Test tests[] = {
    // TEST(fxtest_lsr, fxinst_test_run_v1_v),                      // Passed in commit ab942e0
    // TEST(fxtest_rol, fxinst_test_run_v1_cv),                     // Passed in commit ea737fc
    TEST(fxtest_loop, fxinst_test_run_v1_cv),                       // Passed in commit WYATT_TODO
    // TEST(fxtest_add_r, fxinst_test_run_v1_v2),                   // Passed in commit ab942e0
    // TEST(fxtest_adc_r, fxinst_test_run_v1_v2_c),                 // Passed in commit 972ae9a
};

void fxinst_test_run(bool printFailures)
{
    if (hasRun)
        return;
    hasRun = true;

    printf("     --- GSU Instruction Tests ---\n");
    printf("    These will take a very long time\n");
    // printf("      Press Start to Skip a Test\n");
    printf("Printing individual failures: %c\n", printFailures ? 'Y' : 'N');
    
    uint32 numFailed = 0, numSuccess = 0, numSkipped = 0;
    for (size_t i = 0; i < ARRAY_COUNT(tests); i++)
    {
        FX_Test* t = &tests[i];
        printf("Test %s... ", t->name);
        TickCounter tc;
        osTickCounterStart(&tc);
        FX_TestResult result = t->runner(t, printFailures);
        osTickCounterUpdate(&tc);

        switch(result) {
            case FAIL: numFailed++; break;
            case SUCCESS: numSuccess++; break;
            case SKIP: numSkipped++; break;
        }

        printf("%s in %ds\n", fxTestResultToString(result), (int) (osTickCounterRead(&tc) / 1000.0));
    }
    
    printf("%d passed  %d failed  %d skipped\n", numSuccess, numFailed, numSkipped);
}

void fxinst_test_reset(void)
{
    hasRun = false;
}

#endif
