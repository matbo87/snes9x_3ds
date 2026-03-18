
#ifndef _SAR_H_
#define _SAR_H_


#include "port.h"



#ifdef RIGHTSHIFT_IS_SAR
#define SAR(b, n) ((b)>>(n))
#else

static inline int8 SAR(const int8 b, const int n){
#ifndef RIGHTSHIFT_INT8_IS_SAR
    if (b < 0) {
        const uint8 ub = (uint8)b;
        const uint8 sign_mask = (uint8)~(((uint8)~0) >> n);
        return (int8)((ub >> n) | sign_mask);
    }
#endif
    return b>>n;
}

static inline int16 SAR(const int16 b, const int n){
#ifndef RIGHTSHIFT_INT16_IS_SAR
    if (b < 0) {
        const uint16 ub = (uint16)b;
        const uint16 sign_mask = (uint16)~(((uint16)~0) >> n);
        return (int16)((ub >> n) | sign_mask);
    }
#endif
    return b>>n;
}

static inline int32 SAR(const int32 b, const int n){
#ifndef RIGHTSHIFT_INT32_IS_SAR
    if (b < 0) {
        const uint32 ub = (uint32)b;
        const uint32 sign_mask = (uint32)~(((uint32)~0) >> n);
        return (int32)((ub >> n) | sign_mask);
    }
#endif
    return b>>n;
}

static inline int64 SAR(const int64 b, const int n){
#ifndef RIGHTSHIFT_INT64_IS_SAR
    if (b < 0) {
        const unsigned long long ub = (unsigned long long)b;
        const unsigned long long sign_mask = ~((~0ULL) >> n);
        return (int64)((ub >> n) | sign_mask);
    }
#endif
    return b>>n;
}

#endif

#endif
